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

#include "dpdkContext.h"
#include "backend.h"
#include "printUtils.h"

namespace DPDK {

/* Unique handle for action and table */
#define TABLE_HANDLE_PREFIX 0x80000000
#define ACTION_HANDLE_PREFIX 0xC0000000

unsigned CollectTablesForContextJson::newTableHandle = 0;

/* This function sets certain attributes for each table */
void CollectTablesForContextJson::setTableAttributes() {
        for (auto kv : structure->pipelines) {
	    cstring direction = "";
        if (kv.first == "Ingress")
            direction = "ingress";
	else if (kv.first == "Egress")
 	    direction = "egress";
        auto control = kv.second->to<IR::P4Control>();
        for (auto d : control->controlLocals)
            if (auto tbl = d->to<IR::P4Table>()) {
                struct TableAttributes tblAttr;
                tblAttr.direction = direction;
                tblAttr.tableHandle = getNewTableHandle();
                tableAttrmap.emplace(tbl->name.originalName, tblAttr);
                tables.push_back(d);
            }
	}
}

bool CollectTablesForContextJson::preorder(const IR::P4Program * /*p*/) {
        std::cout << "Inside CollectTablesForContextJson\n" << std::endl;
        setTableAttributes();
        return false;
}

// Returns a unique ID for table
unsigned int CollectTablesForContextJson::getNewTableHandle() {
        return TABLE_HANDLE_PREFIX | newTableHandle++;
}

unsigned WriteContextJson::newActionHandle = 0;

// Returns a unique ID for action
unsigned int WriteContextJson::getNewActionHandle() {
        return ACTION_HANDLE_PREFIX | newActionHandle++;
}

// Helper function for pretty printing into JSON file
void WriteContextJson::add_space(std::ostream &out, int size) {
    out << std::setfill(' ') << std::setw(size) << " ";
}

/* Main function for generating context JSON */
bool WriteContextJson::preorder(const IR::P4Program * /*p*/) {
        if (!options.ctxtFile.isNullOrEmpty()) {
            std::ostream *outCtxt = openFile(options.ctxtFile, false);
            if (outCtxt != nullptr) {
                std::ostream &out = *outCtxt;
                /* Fetch required information from options */
                const time_t now = time(NULL);
                char build_date[1024];
                strftime(build_date, 1024, "%c", localtime(&now));
                cstring compilerCommand = options.DpdkCompCmd;
                compilerCommand = compilerCommand.replace("(from pragmas)", "");
                compilerCommand = compilerCommand.trim();

                /* Print program level information*/
                out << "{\n";
                add_space(out, 4); out << "\"program_name\": \"" << options.file <<"\",\n";
                add_space(out, 4); out << "\"build_date\": \"" << build_date << "\",\n";
                add_space(out, 4); out << "\"compile_command\": \"" << compilerCommand << " \",\n";
                add_space(out, 4); out << "\"compiler_version\": \"" << options.compilerVersion << "\",\n";
                add_space(out, 4); out << "\"schema_version\": \"0.1\",\n";
                add_space(out, 4); out << "\"target\": \"DPDK\",\n";

                /* Table array starts here */
                add_space(out, 4); out << "\"tables\": [";
                bool first = true;
                for (auto t : tables) {
                    if (!first) out << ",";
                    out << "\n";
                    auto tbl = t->to<IR::P4Table>();
                    tbl->dbprint(std::cout);std::cout << "\n";
                    printTableCtxtJson(tbl, out);
                    first = false;
                }
                if (!first) {
                    out << "\n"; add_space(out, 4);
                }
                out << "]\n";
                out << "}\n";
                outCtxt->flush();
            }
        }
    return false;
}

/* This function sets certain attributes for each action within a table */
void WriteContextJson::setActionAttributes (
    std::map <cstring, struct actionAttributes> &actionAttrMap, const IR::P4Table *tbl) {
    for (auto act : tbl->getActionList()->actionList) {
        struct actionAttributes attr;
        auto action_decl = refmap->getDeclaration(act->getPath())->to<IR::P4Action>();
        bool has_constant_default_action = false;
        auto prop = tbl->properties->getProperty(IR::TableProperties::defaultActionPropertyName);
        if (prop && prop->isConstant)
            has_constant_default_action = true;
        bool is_const_default_action = false;
        bool can_be_hit_action = true;
        bool is_compiler_added_action = false;
        bool can_be_default_action = !has_constant_default_action;

        // First, check for action annotations
        auto table_only_annot = action_decl->annotations->getSingle("tableonly");
        auto default_only_annot = action_decl->annotations->getSingle("defaultonly");
        auto hidden = action_decl->annotations->getSingle(IR::Annotation::hiddenAnnotation);

        if (table_only_annot) {
            can_be_default_action = false;
        }
        if (default_only_annot) {
            can_be_hit_action = false;
        }
        if (hidden) {
            is_compiler_added_action = true;
        }
        // Second, see if this action is the default action and/or constant default action
        auto default_action = tbl->getDefaultAction();
        if (default_action) {
            if (auto mc = default_action->to<IR::MethodCallExpression>()) {
                default_action = mc->method;
            }

            auto path = default_action->to<IR::PathExpression>();
                if (!path)
                    BUG("Default action path %s cannot be found", default_action);
            auto actName = refmap->getDeclaration(path->path, true)->getName();

            if (actName == act->getName()) {
                if (has_constant_default_action) {
                    can_be_default_action = true;  // this is the constant default action
                    is_const_default_action = true;
                }
            }
        }

        /* DPDK target takes a structure as parameter for Actions. So, all action
           parameters are collected into a structure by an earlier pass. */
        auto params = ::get(structure->args_struct_map, act->externalName() + "_arg_t");
        if (params)
            attr.params = params->clone();
        else
            attr.params = nullptr;

        attr.constant_default_action = is_const_default_action;
        attr.is_compiler_added_action = is_compiler_added_action;
        attr.allowed_as_hit_action = can_be_hit_action;
        attr.allowed_as_default_action = can_be_default_action;
        attr.actionHandle = getNewActionHandle();
        actionAttrMap.emplace(act->getName(), attr);
    }
}

/* Print a single table object in Context */
void WriteContextJson::printTableCtxtJson (const IR::P4Table *tbl, std::ostream &out) {
    std::map <cstring, struct actionAttributes> actionAttrMap;
    /* Table type can one of "match", "selection" and "action
       Match table is a regular P4 table, selection table and action tables are compiler
       generated tables when psa_implementation is action_selector or action_profile */
    cstring table_type = "match";
    auto hidden = tbl->annotations->getSingle(IR::Annotation::hiddenAnnotation);
    auto selector = tbl->properties->getProperty("selector");
    if (hidden) {
        table_type = selector ? "selection" : "action";
    }

    auto mainTableAttr = ::get(tableAttrmap, tbl->name.originalName);
    add_space(out, 8); out << "{\n";
    add_space(out, 12); out << "\"table_type\": \"" << table_type << "\",\n";
    add_space(out, 12); out << "\"direction\": \"" << mainTableAttr.direction << "\",\n";
    add_space(out, 12); out << "\"handle\": " << mainTableAttr.tableHandle << ",\n";
    add_space(out, 12); out << "\"name\": \"" <<  tbl->name.originalName << "\",\n";
    add_space(out, 12); out << "\"size\": ";

    if (auto size = tbl->properties->getProperty("size")) {
        out << size->value << ",\n";
    } else {
        // default table size for DPDK
        out << "65536" << ",\n";
    }
    add_space(out, 12); out << "\"p4_hidden\": ";
    if (hidden) {
        out << "true" << ",\n";
    } else {
        out << "false" << ",\n";
    }

    if (!selector) {
        if (!hidden) {
            add_space (out, 12); out << "\"action_data_table_refs\": [";
            if (tableAttrmap.count(tbl->name.originalName + "_member_table")) {
                auto tableAttr = ::get(tableAttrmap, tbl->name.originalName + "_member_table");
                add_space (out, 16); out << "\n{\n";
                add_space(out, 20); out << "\"name\": \"" <<  tbl->name.originalName + "_member_table" <<"\",\n";
                add_space(out, 20); out << "\"handle\": \"" << tableAttr.tableHandle   <<"\"\n";
                add_space (out, 16); out << "}\n";
                add_space(out, 12);
            }
            out << "],\n";
            add_space (out, 12); out << "\"selection_table_refs\": [";
            if (tableAttrmap.count(tbl->name.originalName + "_group_table")) {
                auto tableAttr = ::get(tableAttrmap, tbl->name.originalName + "_group_table");
                add_space (out, 16); out << "\n{\n";
                add_space(out, 20); out << "\"name\": \"" << tbl->name.originalName + "_group_table" <<"\",\n";
                add_space(out, 20); out << "\"handle\": \"" <<  tableAttr.tableHandle <<"\"\n";
                add_space (out, 16); out << "}\n";
                add_space(out, 12);
            }
            out << "],\n";
            add_space (out, 12); out << "\"match_key_fields\": [";
            auto match_keys = tbl->getKey();

            if (match_keys) {
                int index = 0;
                for (auto mkf : match_keys->keyElements) {
                    if (auto mExpr = mkf->expression->to<IR::Member>()) {
                    if (index) out << ",";
                    out << "\n";
                    add_space(out, 16); out << "{\n";
                    add_space(out, 20); out << "\"name\": \"" <<  toStr(mkf->expression) <<"\",\n";
                    add_space(out, 20); out << "\"instance_name\": \"" <<  toStr(mExpr->expr) <<"\",\n";
                    add_space(out, 20); out << "\"field_name\": \"" <<  mExpr->member.name <<"\",\n";
                    add_space(out, 20); out << "\"match_type\": \"" <<  toStr(mkf->matchType) <<"\",\n";
                    add_space(out, 20); out << "\"start_bit\": 0,\n";
                    add_space(out, 20); out << "\"bit_width\": " << mkf->expression->type->width_bits() << ",\n";
                    add_space(out, 20); out << "\"bit_width_full\": "  << mkf->expression->type->width_bits() << ",\n";
                    add_space(out, 20); out << "\"index\":" << index << "\n";
                    add_space(out, 20); out << "} ";
                    index++;
                    } else {
                        // Match keys must be part of header or metadata structures
                        BUG("%1%: invalid match key", mkf->expression);
                    }
                }
                if (index) {
                    out << "\n";add_space(out, 12);
                }
            }
            out << "],\n";
        }

        setActionAttributes (actionAttrMap, tbl);
        cstring default_action_name = "";

        unsigned default_action_handle = 0;
        if (tbl->getDefaultAction())
            default_action_name = toStr(tbl->getDefaultAction());
        add_space(out, 12); out << "\"actions\": [";
        bool first = true;
        for (auto action : tbl->getActionList()->actionList) {
            struct actionAttributes attr = ::get(actionAttrMap, action->getName());
              if (toStr(action->expression) == default_action_name)
                default_action_handle = attr.actionHandle;
            if (!first) out << ",";
            out << "\n";
            add_space(out, 16); out << "{\n";
            add_space(out, 20); out << "\"name\": \"" << toStr(action->expression) << "\",\n";
            add_space(out, 20); out << "\"handle\": " << attr.actionHandle << ",\n";
            add_space(out, 20); out << "\"constant_default_action\": " << std::boolalpha << attr.constant_default_action << ",\n";
            add_space(out, 20); out << "\"is_compiler_added_action\": " << std::boolalpha << attr.is_compiler_added_action  << ",\n";
            add_space(out, 20); out << "\"allowed_as_hit_action\": " << std::boolalpha << attr.allowed_as_hit_action << ",\n";
            add_space(out, 20); out << "\"allowed_as_default_action\": " << std::boolalpha << attr.allowed_as_default_action << ",\n";
            add_space(out, 20); out << "\"p4_parameters\": [";

            int position = 0;
            if (attr.params) {
                int index = 0;
                for (auto param : *(attr.params)) {
                  if (position) out << ",";
                  out << "\n";
                  add_space(out, 24); out << "{\n";
                  add_space(out, 28); out << "\"name\": \"" << param->name.originalName << "\",\n";
                  add_space(out, 28); out << "\"start_bit\": 0,\n";
                  add_space(out, 28); out << "\"bit_width\": " << param->type->width_bits() << ",\n";
                  add_space(out, 28); out << "\"position\": " << position++ << ",\n";
                  add_space(out, 28); out << "\"index\": "  << index/8 <<  "\n";
                  add_space(out, 24); out << "}";
                  index += param->type->width_bits();
                }
            }
            if (position) {
                out << "\n"; add_space(out, 20);
            }
            out << "]\n";
            add_space(out, 16); out << "}";
            first = false;

        }
        if (!first) out << "\n";
        add_space(out, 12); out << "],\n";
        add_space(out, 12); out << "\"match_attributes\": {\n";
        add_space(out, 16); out << "\"stage_tables\": [\n";
        add_space(out, 20); out << "{\n";
        add_space(out, 24); out << "\"action_format\": [";
        first = true;

        for (auto action : tbl->getActionList()->actionList) {
            struct actionAttributes attr = ::get(actionAttrMap, action->getName());
              if (toStr(action->expression) == default_action_name)
                default_action_handle = attr.actionHandle;
            if (!first) out << ",";
            out << "\n";
            add_space(out, 28); out << "{\n";
            add_space(out, 32); out << "\"action_name\": \"" << toStr(action->expression) << "\",\n";
            add_space(out, 32); out << "\"action_handle\": " << attr.actionHandle << ",\n";
            add_space(out, 32); out << "\"immediate_fields\": [";

            int position = 0;
            if (attr.params) {
                int index = 0;
                for (auto param : *(attr.params)) {
                  if (position) out << ",";
                  out << "\n";
                  add_space(out, 36); out << "{\n";
                  add_space(out, 40); out << "\"param_name\": \"" << param->name.originalName << "\",\n";
                  add_space(out, 40); out << "\"dest_start\": "  << index/8 <<  ",\n";
                  add_space(out, 40); out << "\"dest_width\": " << param->type->width_bits() << "\n";
                  add_space(out, 36); out << "}";
                  index += param->type->width_bits();
                  position++;
                }
            }
            if (position) {
                out << "\n"; add_space(out, 32);
            }
            out << "]\n";
            add_space(out, 28); out << "}";
            first = false;

        }
        out << "\n";
        add_space(out, 24); out << "]\n";
        add_space(out, 20); out << "}\n";
        add_space(out, 16); out << "]\n";
        add_space(out, 12); out << "},\n";
        add_space(out, 12); out << "\"default_action_handle\": " << default_action_handle << "\n";
    } else {
        // selection table is a special compiler generated table for tables with action selector implementation.
        add_space(out, 12); out << "\"max_n_groups\": ";
        if (auto n_groups = tbl->properties->getProperty("n_groups_max")) {
            out << n_groups->value << ",\n";
        } else {
            out << "0" << ",\n";
        }
        add_space(out, 12); out << "\"max_n_members_per_group\": ";
        if (auto n_members = tbl->properties->getProperty("n_members_per_group_max")) {
            out << n_members->value << ",\n";
        } else {
            out << "0" << ",\n";
        }
        cstring actionDataTableName = tbl->name.originalName;
        actionDataTableName = actionDataTableName.replace("_group_table", "_member_table");
        add_space(out, 12); out << "\"bound_to_action_data_table_handle\": " << actionDataTableName << "\n";

    }
    add_space(out, 8); out << "}";
}
}
