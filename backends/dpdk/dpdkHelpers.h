/*
Copyright 2020 Intel Corp.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#ifndef BACKENDS_DPDK_HELPER_H_
#define BACKENDS_DPDK_HELPER_H_

#include "dpdkProgramStructure.h"
#include "frontends/common/constantFolding.h"
#include "frontends/common/resolveReferences/referenceMap.h"
#include "frontends/p4/coreLibrary.h"
#include "frontends/p4/enumInstance.h"
#include "frontends/p4/evaluator/evaluator.h"
#include "frontends/p4/methodInstance.h"
#include "frontends/p4/simplify.h"
#include "frontends/p4/typeMap.h"
#include "frontends/p4/unusedDeclarations.h"
#include "midend/removeComplexExpressions.h"
#include "ir/ir.h"
#include "lib/gmputil.h"
#include "lib/json.h"

#define TOSTR_DECLA(NAME) std::ostream &toStr(std::ostream &, IR::NAME *)

namespace DPDK {
/* This class will generate a optimized jmp and label control flow.
 * Couple of examples here
 *
 * Example 1:
 * If(a && b){
 *
 * }
 * Else{
 *
 * }
 *
 * Will be translated to(in an optimal form):
 * cmp a 1
 * jneq false
 * cmp b 1
 * jneq false
 * true:
 *     // if true statements go here
 *     jmp end
 * false:
 *     // if false statements go here
 * end:
 *
 * In this case, in order to use fewer jmp, I use jneq instead of jeq to let the
 * true condition fall through the jmp statement and short-circuit the false
 * condition.
 *
 * Example 2:
 * (a && b) || c
 * cmp a 1
 * jneq half_false
 * cmp b 1
 * jneq half_false
 * jmp true
 * half_false:
 *     cmp c 1
 *     jeq true
 * false:
 *
 *     jmp end
 * true:
 *
 * end:
 *
 * In this case, it is not in an optimal form. To make it optimal, I need to
 * change (a && b) || c to c ||(a && b) and assembly code looks like this:
 * cmp c 1
 * jeq true
 * cmp a 1
 * jneq false
 * cmp b 1
 * jneq false
 * false:
 *
 *     jmp end
 * true:
 *
 * end:
 *
 * It is very important to generate as fewer jmp and label instructions as
 * possible, because the performance of DPDK is directly related to the number
 * of instructions.
 *
 * This class uses a recursive function to generate the control flow and is
 * optmized.
 */
class BranchingInstructionGeneration {
    P4::ReferenceMap *refMap;
    P4::TypeMap *typeMap;
    bool nested(const IR::Node *n) {
        if (n->is<IR::LAnd>() || n->is<IR::LOr>()) {
            return true;
        } else {
            return false;
        }
    }

  public:
    IR::IndexedVector<IR::DpdkAsmStatement> instructions;
    BranchingInstructionGeneration(P4::ReferenceMap *refMap,
                                   P4::TypeMap *typeMap)
        : refMap(refMap), typeMap(typeMap) {}
    bool generate(const IR::Expression *, cstring, cstring, bool);
};

class ConvertStatementToDpdk : public Inspector {
    static int next_label_id;
    IR::IndexedVector<IR::DpdkAsmStatement> instructions;
    P4::TypeMap *typemap;
    P4::ReferenceMap *refmap;
    DpdkProgramStructure *structure;
    const IR::P4Parser *parser = nullptr;
    IR::Type_Struct *metadataStruct;

  public:
    ConvertStatementToDpdk(
        P4::ReferenceMap *refmap, P4::TypeMap *typemap,
        DpdkProgramStructure *structure, IR::Type_Struct *metadataStruct)
        : typemap(typemap), refmap(refmap), structure(structure), metadataStruct(metadataStruct) {}
    IR::IndexedVector<IR::DpdkAsmStatement> getInstructions() {
        return instructions;
    }
    void branchingInstructionGeneration(cstring true_label, cstring false_label,
                                        const IR::Expression *expr);
    bool preorder(const IR::AssignmentStatement *a) override;
    bool preorder(const IR::IfStatement *a) override;
    bool preorder(const IR::MethodCallStatement *a) override;
    bool preorder(const IR::SwitchStatement* a) override;
    IR::Declaration_Variable *addNewTmpVarToMetadata (cstring name, const IR::Type* type);

    void add_instr(const IR::DpdkAsmStatement *s) { instructions.push_back(s); }
    IR::IndexedVector<IR::DpdkAsmStatement> &get_instr() { return instructions; }
    int get_label_num() { return next_label_id; }
    void process_logical_operation(const IR::Expression*, const IR::Operation_Binary*);
    void process_relation_operation(const IR::Expression*, const IR::Operation_Relation*);
    cstring append_parser_name(const IR::P4Parser* p, cstring);
    void set_parser(const IR::P4Parser* p) { parser = p; }
};

/**
 * only simplify complex expression in ingress/egress
 */
class ProcessControls : public P4::RemoveComplexExpressionsPolicy {
    const std::set<cstring> *process;

 public:
    explicit ProcessControls(const std::set<cstring> *process) : process(process) {
        CHECK_NULL(process);
    }
    bool convert(const IR::P4Control* control) const {
        if (process->find(control->name) != process->end())
            return true;
        return false;
    }
};

} // namespace DPDK
#endif
