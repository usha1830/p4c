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

#ifndef BACKENDS_DPDK_CONTEXT_H_
#define BACKENDS_DPDK_CONTEXT_H_

#include "dpdkProgramStructure.h"
#include "options.h"
#include "lib/nullstream.h"
namespace DPDK {
class CollectTablesForContextJson : public Inspector {
    DpdkProgramStructure *structure;	
    std::map<const cstring, IR::IndexedVector<IR::Declaration>> &tables_map;

  public:
    CollectTablesForContextJson(DpdkProgramStructure *structure, std::map<const cstring, IR::IndexedVector<IR::Declaration>> &tables_map)
        : structure(structure), tables_map(tables_map) {}
    bool preorder(const IR::P4Program *p) override;
};

struct actionAttributes {
    bool constant_default_action;    
    bool is_compiler_added_action;
    bool allowed_as_hit_action;
    bool allowed_as_default_action;
    unsigned actionHandle;
    IR::IndexedVector<IR::Parameter> *params;
};

class WriteContextJson : public Inspector {
    P4::ReferenceMap *refmap;
    P4::TypeMap *typemap;
    DpdkProgramStructure *structure;
    DpdkOptions &options;
    std::map<const cstring, IR::IndexedVector<IR::Declaration>> &tables_map;
    static int newTableHandle;
    static int newActionHandle;
 public:
    WriteContextJson(P4::ReferenceMap *refmap, P4::TypeMap *typemap, DpdkProgramStructure *structure, 
	    DpdkOptions &options, std::map<const cstring, IR::IndexedVector<IR::Declaration>> &tables_map)
        : refmap(refmap), typemap(typemap), structure(structure), options(options), tables_map(tables_map) {}

    unsigned int getNewHandle(bool isTable);
    void add_space(std::ostream &out, int size);
    bool preorder(const IR::P4Program *p) override;
    void setActionAttributes (std::map <cstring, struct actionAttributes> &actionAttrMap, const IR::P4Table *tbl);
    void printTableCtxtJson (cstring direction, const IR::P4Table *tbl, std::ostream &out);
};
        
class GenerateContextJson : public PassManager {
    P4::ReferenceMap *refmap;
    P4::TypeMap *typemap;
    DpdkProgramStructure *structure;
    DpdkOptions &options;
    std::map<const cstring, IR::IndexedVector<IR::Declaration>> tables_map;

public:
    GenerateContextJson(
        P4::ReferenceMap *refmap, P4::TypeMap *typemap,
        DpdkProgramStructure *structure,  DpdkOptions &options)
        : refmap(refmap), typemap(typemap), structure(structure), options(options){

        addPasses( {
            new CollectTablesForContextJson(structure, tables_map),
            new WriteContextJson(refmap, typemap, structure, options, tables_map)
        });
    }
};
} // namespace DPDK
#endif
