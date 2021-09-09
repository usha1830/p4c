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
//########################### Context Json ###################################
#define TABLE_HANDLE_PREFIX 0x80000000
#define ACTION_HANDLE_PREFIX 0xC0000000

int WriteContextJson::newTableHandle = 0;
int WriteContextJson::newActionHandle = 0;

bool CollectTablesForContextJson::preorder(const IR::P4Program * /*p*/) {
        std::cout << "Inside CollectTablesForContextJson\n" << std::endl;
        for (auto kv : structure->pipelines) {
	    cstring direction = "";
        if (kv.first == "Ingress")
            direction = "ingress";
	else if (kv.first == "Egress")
 	    direction = "egress";
        IR::IndexedVector<IR::Declaration> tables;	
        auto control = kv.second->to<IR::P4Control>();
        for (auto d : control->controlLocals)
            if (d->is<IR::P4Table>()) 
                tables.push_back(d);					
            tables_map.emplace(direction, tables);
	}
        return false;	  
}


unsigned int WriteContextJson::getNewHandle(bool isTable) {
    if (isTable) {
        return TABLE_HANDLE_PREFIX | newTableHandle++;
    } else {
        return ACTION_HANDLE_PREFIX | newActionHandle++;
    }
}

void WriteContextJson::add_space(std::ostream &out, int size) {
    out << std::setfill(' ') << std::setw(size) << " ";
} 

bool WriteContextJson::preorder(const IR::P4Program * /*p*/) {
        std::cout << "Inside WriteContextJson\n" << std::endl;
        std::ostream *outCtxt = openFile(options.ctxtFile, false);
        if (outCtxt != nullptr) {
          std::ostream &out = *outCtxt;
            //TODO get schema version from config file
            cstring compilerCommand = options.DpdkCompCmd;
            compilerCommand = compilerCommand.replace("(from pragmas)", "");
            compilerCommand = compilerCommand.trim();
            std::cout << compilerCommand << std::endl;
            std::string compVersion = options.compilerVersion.c_str();
            size_t pos = compVersion.find(' ');
            if (pos != std::string::npos)
                compVersion = compVersion.substr(0, pos);
            std::cout << compVersion << std::endl;
            out << "{\n";
            out << std::setfill(' ') << std::setw(4) << " "; out << "\"program_name\": \"" << options.file <<"\",\n";
            const time_t now = time(NULL);
            char build_date[1024];
            strftime(build_date, 1024, "%c", localtime(&now));
            out << std::setfill(' ') << std::setw(4) << " "; out << "\"build_date\": \"" << build_date << "\",\n";
            out << std::setfill(' ') << std::setw(4) << " "; out << "\"compile_command\": \"" << compilerCommand << " \",\n";
            out << std::setfill(' ') << std::setw(4) << " "; out << "\"compiler_version\": \"" << compVersion << "\",\n";
            out << std::setfill(' ') << std::setw(4) << " "; out << "\"schema_version\": \"0.1" /*<< TODO schemaVersion */<< "\",\n";
            out << std::setfill(' ') << std::setw(4) << " "; out << "\"target\": \"DPDK\",\n";

            add_space(out, 4); out << "\"tables\": [";
            bool first = true;
            for (auto tm : tables_map) {
                for (auto t : tm.second) {
                    if (!first) out << ",";
                    out << "\n";
                    auto tbl = t->to<IR::P4Table>(); 
                    tbl->dbprint(std::cout);std::cout << "\n";
                    printTableCtxtJson(tm.first, tbl, out);
                    first = false;
                }
            }
            out << "\n";
            add_space(out, 4); out << "]\n";
            out << "}\n";
            outCtxt->flush();
        }
        return false;
    }

void WriteContextJson::setActionAttributes (std::map <cstring, struct actionAttributes> &actionAttrMap, const IR::P4Table *tbl) {
    std::cout << "Inside setActionAttributes\n" << std::endl;
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
  
        auto params = ::get(structure->args_struct_map, act->externalName() + "_arg_t");
        if (params)
            attr.params = params->clone();
        else
            attr.params = nullptr;
            
        attr.constant_default_action = is_const_default_action;
        attr.is_compiler_added_action = is_compiler_added_action;
        attr.allowed_as_hit_action = can_be_hit_action;
        attr.allowed_as_default_action = can_be_default_action;
        attr.actionHandle = getNewHandle(false);
        actionAttrMap.emplace(act->getName(), attr);
    }
    std::cout << "Inside setActionAttributes End\n" << std::endl;
}


void WriteContextJson::printTableCtxtJson (cstring direction, const IR::P4Table *tbl, std::ostream &out) {
    std::map <cstring, struct actionAttributes> actionAttrMap;
    cstring table_type = "match";
    if (tbl->properties->getProperty("selector"))
        table_type = "selector";
    add_space(out, 8); out << "{\n";
    add_space(out, 12); out << "\"table_type\": \"" << table_type << "\",\n";
    add_space(out, 12); out << "\"direction\": \"" << direction << "\",\n";
    add_space(out, 12); out << "\"handle\": " << getNewHandle(true) << ",\n";
    add_space(out, 12); out << "\"name\": \"" <<  tbl->name.originalName << "\",\n";
    add_space(out, 12); out << "\"size\": ";

    if (auto size = tbl->properties->getProperty("size")) {
        out << size->value << ",\n";
    } else {
        out << "65536" << ",\n";
    }
    add_space(out, 12); out << "\"p4_hidden\": ";
    if(tbl->annotations->getSingle(IR::Annotation::hiddenAnnotation)) {
        out << "true" << ",\n";
    } else {
        out << "false" << ",\n";
    }
   
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
    }	

    std::cout << "\"match_key_fields\": End\n";
    out << "\n";
    add_space(out, 12); out << "],\n";
    setActionAttributes (actionAttrMap, tbl);
    cstring default_action_name = "";

    if (tbl->getDefaultAction())    
        default_action_name = toStr(tbl->getDefaultAction());
    unsigned default_action_handle = 0xC0000000;
    add_space(out, 12); out << "\"actions\": [";    
    bool first = true;
    std::cout << "\"actionList Start\n";
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
            
        if (attr.params) {
            int position = 0;
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
        out << "\n";
        add_space(out, 20); out << "]\n";
        add_space(out, 16); out << "}";       
        first = false; 
    
    }
    out << "\n";
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
            
        if (attr.params) {
            int position = 0;
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
        out << "\n";
        add_space(out, 32); out << "]\n";
        add_space(out, 28); out << "}";       
        first = false; 
    
    }
    out << "\n";
    add_space(out, 24); out << "]\n";
    add_space(out, 20); out << "}\n";    
    add_space(out, 16); out << "]\n";
    add_space(out, 12); out << "},\n";    

/*##############*/

    add_space(out, 12); out << "\"default_action_handle\": " << default_action_handle << "\n";
    add_space(out, 8); out << "}";	
}
}
