/*
Copyright 2022 Intel Corp.

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

#include "dpdkMetadata.h"
#include "dpdkUtils.h"

namespace DPDK {
const IR::Node* AddNewMetadataFields::preorder(IR::DpdkStructType *st) {
    if (isMetadataStruct(st)) {
        for (auto nf : newMetadataFields) {
                st->fields.push_back(nf);
        }
        auto newSt = new IR::DpdkStructType(st->srcInfo, st->name,
                                          st->annotations, st->fields);
        newMetadataFields.clear();
        return newSt;
    }
    return st;
}

// make sure new decls and fields name are unique
void DirectionToRegRead::uniqueNames(IR::DpdkAsmProgram *p) {
    for (auto st : p->structType) {
        if (isMetadataStruct(st)) {
            for (auto field : st->fields)
                mng.usedName(field->name);
        }
    }
    reg_read_tmp = mng.newName("reg_read_tmp");
    left_shift_tmp = mng.newName("left_shift_tmp");

    // "network_port_mask" name is used in dpdk for initialzing direction port mask
    // make sure no such decls exist with that name
    registerInstanceName = "network_port_mask";
    for (auto decl : p->externDeclarations) {
        usedNames.insert(decl->name);
    }

    if (usedNames.count(registerInstanceName))
        ::error(ErrorType::ERR_UNSUPPORTED_ON_TARGET,"decl name %s is reserved for dpdk pna",
                registerInstanceName);
}

const IR::Node* DirectionToRegRead::preorder(IR::DpdkAsmProgram *p) {
    uniqueNames(p);
    addMetadataField(reg_read_tmp, IR::Type::Bits::get(64));
    addMetadataField(left_shift_tmp, IR::Type::Bits::get(64));
    p->externDeclarations.push_back(addRegDeclInstance(registerInstanceName));
    IR::IndexedVector<IR::DpdkAsmStatement> stmts;
    for (auto stmt : p->statements) {
        auto stmtList = stmt->to<IR::DpdkListStatement>()->clone();
        stmts.push_back(replaceDirection(stmtList));
    }
    p->statements = stmts;
    return p;
}

// create and add register declaration instance to program
IR::DpdkExternDeclaration* DirectionToRegRead::addRegDeclInstance(cstring instanceName) {
    auto typepath = new IR::Path("Register");
    auto type = new IR::Type_Name(typepath);
    auto typeargs = new IR::Vector<IR::Type>({IR::Type::Bits::get(64),
                                              IR::Type::Bits::get(32)});
    auto spectype = new IR::Type_Specialized(type, typeargs);
    auto args = new IR::Vector<IR::Argument>();
    args->push_back(new IR::Argument(new IR::Constant(IR::Type::Bits::get(32), 1)));
    auto annot = IR::Annotations::empty;
    annot->addAnnotationIfNew(IR::Annotation::nameAnnotation,
                              new IR::StringLiteral(instanceName));
    auto decl = new IR::DpdkExternDeclaration(instanceName, annot, spectype, args,
                    nullptr);
    return decl;
}

// add new fields in metadata structure
void DirectionToRegRead::addMetadataField(cstring fieldName, const IR::Type* type) {
    newMetadataFields.push_back(new IR::StructField(IR::ID(fieldName),
                                type));
}

// check member expression using metadata direction field
bool DirectionToRegRead::isDirection(const IR::Member *m) {
    if (m == nullptr)
        return false;
     return m->member.name == "pna_main_input_metadata_direction"
         || m->member.name == "pna_pre_input_metadata_direction"
         || m->member.name == "pna_main_parser_input_metadata_direction";
}

IR::DpdkListStatement* DirectionToRegRead::replaceDirection(IR::DpdkListStatement *l) {
    // collect used labels name for generating new unique labels
    for (auto stmt : l->statements) {
        if (auto ls = stmt->to<IR::DpdkLabelStatement>())
            mng.usedName(ls->label);
    }
    l->statements = replaceDirectionWithRegRead(l->statements);
    newStmts.clear();
    return l;
}

const IR::Node *DirectionToRegRead::postorder(IR::DpdkAction *a) {
    // collect used labels name for generating new unique labels
    for (auto stmt : a->statements) {
        if (auto ls = stmt->to<IR::DpdkLabelStatement>())
            mng.usedName(ls->label);
    }
    a->statements = replaceDirectionWithRegRead(a->statements);
    newStmts.clear();
    return a;
}

// replace direction field uses with register read i.e.
// istd.direction =
// (!(((direction_port_mask.read(0) & (32w0x1 << istd.input_port)) >> istd.input_port))) == 0
// ? NET_TO_HOST : HOST_TO_NET
void DirectionToRegRead::replaceDirection(const IR::Member *m) {
    if (isInitialized[m->member.name])
        return;
    auto reade = new IR::Member(new IR::PathExpression(IR::ID("m")),
                                IR::ID(reg_read_tmp));
    auto reads = new IR::DpdkRegisterReadStatement(reade, registerInstanceName,
                                                   new IR::Constant(IR::Type::Bits::get(32),
                                                   0));
    auto shld = new IR::Member(new IR::PathExpression(IR::ID("m")),
                               IR::ID(left_shift_tmp));
    auto mov = new IR::DpdkMovStatement(shld, new IR::Constant(IR::Type::Bits::get(64), 1));
    auto inputPort = new IR::Member(new IR::PathExpression(IR::ID("m")),
                                          IR::ID(dirToInput[m->member.name]));
    auto shl = new IR::DpdkShlStatement(shld, shld, inputPort);
    auto and0 = new IR::DpdkAndStatement(shld, shld, reade);
    auto shr0 = new IR::DpdkShrStatement(shld, shld, inputPort);
    auto label_true = mng.newName("LABEL_TRUE");
    auto label_end = mng.newName("LABEL_END");
    auto cmp = new IR::DpdkJmpEqualStatement(label_true, shld,
                                            new IR::Constant(IR::Type::Bits::get(64), 0));
    auto mov0 = new IR::DpdkMovStatement(m, new IR::Constant(IR::Type::Bits::get(32), 0));
    auto jmp = new IR::DpdkJmpLabelStatement(label_end);
    auto label1 = new IR::DpdkLabelStatement(label_true);
    auto mov1 = new IR::DpdkMovStatement(m, new IR::Constant(IR::Type::Bits::get(32), 1));
    auto label2 = new IR::DpdkLabelStatement(label_end);
    newStmts.push_back(reads);
    newStmts.push_back(mov);
    newStmts.push_back(shl);
    newStmts.push_back(and0);
    newStmts.push_back(shr0);
    newStmts.push_back(cmp);
    newStmts.push_back(mov0);
    newStmts.push_back(jmp);
    newStmts.push_back(label1);
    newStmts.push_back(mov1);
    newStmts.push_back(label2);
    isInitialized[m->member.name] = true;
}

IR::IndexedVector<IR::DpdkAsmStatement>
DirectionToRegRead::replaceDirectionWithRegRead(IR::IndexedVector<IR::DpdkAsmStatement> stmts) {
    for (auto s : stmts) {
        if (auto jc = s->to<IR::DpdkJmpCondStatement>()) {
            if (isDirection(jc->src1->to<IR::Member>()))
                replaceDirection(jc->src1->to<IR::Member>());
            else if (isDirection(jc->src2->to<IR::Member>()))
                   replaceDirection(jc->src2->to<IR::Member>());
        } else if (auto u = s->to<IR::DpdkUnaryStatement>()) {
            if (isDirection(u->src->to<IR::Member>()))
                replaceDirection(u->src->to<IR::Member>());
        }
        newStmts.push_back(s);
    }
    return newStmts;
}

IR::IndexedVector<IR::StructField> AddNewMetadataFields::newMetadataFields = {};

// check member expression using metadata pass field
// "recircid" instruction takes the pass metadata type as argument to fetch the pass_id.
bool PrependPassRecircId::isPass(const IR::Member *m) {
    if (m == nullptr)
        return false;
     return m->member.name == "pna_main_input_metadata_pass"
         || m->member.name == "pna_pre_input_metadata_pass"
         || m->member.name == "pna_main_parser_input_metadata_pass";
}

const IR::Node *PrependPassRecircId::postorder(IR::DpdkListStatement *l) {
    l->statements = prependPassWithRecircid(l->statements);
    newStmts.clear();
    return l;
}

const IR::Node *PrependPassRecircId::postorder(IR::DpdkAction *a) {
    a->statements = prependPassWithRecircid(a->statements);
    newStmts.clear();
    return a;
}

IR::IndexedVector<IR::DpdkAsmStatement>
PrependPassRecircId::prependPassWithRecircid(IR::IndexedVector<IR::DpdkAsmStatement> stmts) {
    for (auto s : stmts) {
        if (auto jc = s->to<IR::DpdkJmpCondStatement>()) {
            if (isPass(jc->src1->to<IR::Member>()))
                newStmts.push_back(new IR::DpdkRecircidStatement(jc->src1->to<IR::Member>()));
            else if (isPass(jc->src2->to<IR::Member>()))
                newStmts.push_back(new IR::DpdkRecircidStatement(jc->src2->to<IR::Member>()));
        } else if (auto u = s->to<IR::DpdkUnaryStatement>()) {
            if (isPass(u->src->to<IR::Member>())) {
                newStmts.push_back(new IR::DpdkRecircidStatement(u->src->to<IR::Member>()));
            }
        } else if (auto u = s->to<IR::DpdkCastStatement>()) {
            if (isPass(u->src->to<IR::Member>())) {
                newStmts.push_back(new IR::DpdkRecircidStatement(u->src->to<IR::Member>()));
            }
        }
        newStmts.push_back(s);
    }
    return newStmts;
}

}  // namespace DPDK
