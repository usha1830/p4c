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
#include "ConvertToDpdkHelper.h"
#include "ir/ir.h"
#include <iostream>

namespace DPDK{

bool ConvertStatementToDpdk::preorder(const IR::AssignmentStatement* a){
    auto left = a->left;
    auto right = a->right;
    IR::DpdkAsmStatement *i;
    // handle Binary Operation
    if(auto r = right->to<IR::Operation_Binary>()){
        auto left_unroller = new TreeUnroller(collector, refmap, typemap);
        auto right_unroller = new TreeUnroller(collector, refmap, typemap);
        r->left->apply(*left_unroller);
        for(auto i: left_unroller->get_instr()){
            add_instr(i);
        }
        r->right->apply(*right_unroller);
        for(auto i: right_unroller->get_instr()){
            add_instr(i);
        }
        const IR::Expression* left_tmp = left_unroller->get_tmp();
        const IR::Expression* right_tmp = right_unroller->get_tmp();
        if(not left_tmp) left_tmp = r->left;
        if(not right_tmp) right_tmp = r->right;

        if(right->is<IR::Add>()){
            i = new IR::DpdkAddStatement(left, left_tmp, right_tmp);
        }
        else if(right->is<IR::Sub>()){
            i = new IR::DpdkSubStatement(left, left_tmp, right_tmp);
        }
        else if(right->is<IR::Shl>()){
            i = new IR::DpdkShlStatement(left, left_tmp, right_tmp);
        }
        else if(right->is<IR::Shr>()){
            i = new IR::DpdkShrStatement(left, left_tmp, right_tmp);
        }
        else if(right->is<IR::Equ>()){
            i = new IR::DpdkEquStatement(left, left_tmp, right_tmp);
        }
        else {
            std::cerr << right->node_type_name() << std::endl;
            BUG("not implemented.");
        }
    }
    // handle method call
    else if(auto m = right->to<IR::MethodCallExpression>()){
        auto mi = P4::MethodInstance::resolve(m, refmap, typemap);
        // std::cerr << m << std::endl;
        if(auto e = mi->to<P4::ExternMethod>()){
            if(e->originalExternType->getName().name == "Hash"){
                if(e->expr->arguments->size() == 1){
                    auto field = (*e->expr->arguments)[0];
                    i = new IR::DpdkGetHashStatement(e->object->getName(), field->expression, left);
                }
            }
            else if(e->originalExternType->getName().name == "InternetChecksum"){
                if(e->method->getName().name == "get"){
                    i = new IR::DpdkGetChecksumStatement(left, e->object->getName());
                }
            }
            else if(e->originalExternType->getName().name == "Register"){
                if(e->method->getName().name == "get"){
                    auto index = (*e->expr->arguments)[0]->expression;
                    i = new IR::DpdkRegisterReadStatement(left, e->object->getName(), index);
                }
            }
            else{
                std::cerr << e->originalExternType->getName() << std::endl;
                BUG("ExternMethod Not implemented");
            }
        }
        else if(auto b = mi->to<P4::BuiltInMethod>()){
            if(b->name.name == "isValid"){
                if(auto member = b->appliedTo->to<IR::Member>()){
                    i = new IR::DpdkValidateStatement(left, member->member);
                    // std::cerr << member->member << std::endl;
                }
            }
            else{
                std::cerr << b->name.name << std::endl;
                BUG("BuiltInMethod Not Implemented");
            }
        }
        else {
            BUG("MethodInstance Not implemented");
        }
    }
    else if(right->is<IR::Operation_Unary>() and not right->is<IR::Member>()){
        if(auto ca = right->to<IR::Cast>()){
            // std::cerr << ca << std::endl;
            // std::cerr << ca->expr->is<IR::Member>() << std::endl;
            i = new IR::DpdkCastStatement(left, ca->expr, ca->destType);
        }
        else if(auto n = right->to<IR::Neg>()){
            i = new IR::DpdkNegStatement(left, n->expr);
        }
        else if(auto c = right->to<IR::Cmpl>()){
            i = new IR::DpdkCmplStatement(left, c->expr);
        }
        else if(auto ln = right->to<IR::LNot>()){
            i = new IR::DpdkLNotStatement(left, ln->expr);
        }
        else{
            std::cerr << right->node_type_name() << std::endl;
            BUG("Not implemented.");
        }
    }
    else if(right->is<IR::PathExpression>() or
            right->is<IR::Member>() or
            right->is<IR::BoolLiteral>() or
            right->is<IR::Constant>()){
        i = new IR::DpdkMovStatement(a->left, a->right);
    }
    else{
        std::cerr<< right->node_type_name() << std::endl;
        BUG("Not implemented.");
    }
    add_instr(i);
    return false;
}
bool ConvertStatementToDpdk::preorder(const IR::BlockStatement* b){
    for(auto i:b->components){
        visit(i);
    }
    return false;
}

bool ConvertStatementToDpdk::preorder(const IR::IfStatement* s){
    auto true_label  = Util::printf_format("label_%d", next_label_id++);
    auto end_label = Util::printf_format("label_%d", next_label_id++);

    auto converter = new TreeUnroller(collector, refmap, typemap);
    s->condition->apply(*converter);
    for(auto i:converter->get_instr()){
        add_instr(i);
    }
    const IR::Expression * tmp = converter->get_tmp();
    if(not tmp) tmp = s->condition;

    add_instr(new IR::DpdkCmpStatement(tmp, new IR::Constant(0)));
    add_instr(new IR::DpdkJmpNotEqualStatement(true_label));
    visit(s->ifFalse);
    add_instr(new IR::DpdkJmpStatement(end_label));
    add_instr(new IR::DpdkLabelStatement(true_label));
    visit(s->ifTrue);
    add_instr(new IR::DpdkLabelStatement(end_label));
    return false;
}

bool ConvertStatementToDpdk::preorder(const IR::MethodCallStatement* s){
    auto mi = P4::MethodInstance::resolve(s->methodCall, refmap, typemap);
    if(auto a = mi->to<P4::ApplyMethod>()){
        if(a->isTableApply()){
            auto table = a->object->to<IR::P4Table>();
            add_instr(new IR::DpdkApplyStatement(table->name));
        }
        else BUG("not implemented for `apply` other than table");
    }
    else if(auto a = mi->to<P4::ExternMethod>()){
        // std::cerr << a->originalExternType->getName() << std::endl;
        // Checksum function call
        if(a->originalExternType->getName().name == "InternetChecksum"){
            if(a->method->getName().name == "add") {
                auto args = a->expr->arguments;
                const IR::Argument *arg = (*args)[0];
                if(auto l = arg->expression->to<IR::ListExpression>()){
                    for(auto field : l->components){
                        add_instr(new IR::DpdkChecksumAddStatement(a->object->getName(), field));
                    }
                }
                else ::error("The argument of InternetCheckSum.add is not a list.");
            }
        }
        // Packet emit function call
        else if(a->originalExternType->getName().name == "packet_out"){
            if(a->method->getName().name == "emit"){
                auto args = a->expr->arguments;
                auto header = (*args)[0];
                if(auto m = header->expression->to<IR::Member>()){
                    add_instr(new IR::DpdkEmitStatement(m));
                }
                else if (auto path = header->expression->to<IR::PathExpression>()){
                    add_instr(new IR::DpdkEmitStatement(path));
                }
                else ::error("One emit does not like this packet.emit(header.xxx)");
            }
        }
        else if(a->originalExternType->getName().name == "packet_in"){
            if(a->method->getName().name == "extract"){
                auto args = a->expr->arguments;
                auto header = (*args)[0];
                if(auto m = header->expression->to<IR::Member>()){
                    add_instr(new IR::DpdkExtractStatement(m));
                }
                else if(auto path = header->expression->to<IR::PathExpression>()){
                    add_instr(new IR::DpdkExtractStatement(path));
                }
                else ::error("Extract format does not like this packet.extract(header.xxx)");
            }
        }
        else if(a->originalExternType->getName().name == "Meter"){
            if(a->method->getName().name == "execute"){
                auto args = a->expr->arguments;
                auto index = (*args)[0]->expression;
                auto color = (*args)[1]->expression;
                auto meter = a->object->getName();
                add_instr(new IR::DpdkMeterExecuteStatement(meter, index, color));
            }
            else BUG("Meter function not implemented.");
        }
        else if(a->originalExternType->getName().name == "Counter"){
            if(a->method->getName().name == "count"){
                auto args = a->expr->arguments;
                auto index = (*args)[0]->expression;
                auto counter = a->object->getName();
                add_instr(new IR::DpdkCounterCountStatement(counter, index));
            }
            else BUG("Counter function not implemented");
        }
        else if(a->originalExternType->getName().name == "Register"){
            if(a->method->getName().name == "read"){
                std::cerr << "ignore this register read, because the return value is optimized." << std::endl;
            }
            else if(a->method->getName().name == "write"){
                auto args = a->expr->arguments;
                auto index = (*args)[0]->expression;
                auto src = (*args)[1]->expression;
                auto reg = a->object->getName();
                add_instr(new IR::DpdkRegisterWriteStatement(reg, index, src));
            }
        }
        else{
            std::cerr << a->originalExternType->getName() << std::endl;
            BUG("Unknown extern function.");
        }
    }
    else if(auto a = mi->to<P4::ExternFunction>()){
        if(a->method->name == "verify"){
            auto args = a->expr->arguments;
            auto condition = (*args)[0];
            auto error = (*args)[1];
            add_instr(new IR::DpdkVerifyStatement(condition->expression, error->expression));
        }
    }
    else {
        BUG("function not implemented.");
    }
    return false;
}


// IR::Expression* getMethodReturnType(
//     const IR::MethodCallExpression* mce,
//     P4::ReferenceMap* refmap,
//     P4::TypeMap* typemap){
//         auto mi = P4::MethodInstance::resolve(mce, refmap, typemap);

//     }

bool TreeUnroller::preorder(const IR::Operation_Binary *bin){
    visit(bin->left);
    const IR::Expression* left_tmp = tmp;
    visit(bin->right);
    const IR::Expression* right_tmp = tmp;
    cstring tmp_name = collector->get_next_tmp();
    tmp = new IR::PathExpression(IR::ID(tmp_name));
    if(not left_tmp) left_tmp = bin->left;
    if(not right_tmp) right_tmp = bin->right;
    IR::DpdkAsmStatement *i;
    if(bin->is<IR::Add>()) i = new IR::DpdkAddStatement(tmp, left_tmp, right_tmp);
    else if(bin->is<IR::Sub>()) i = new IR::DpdkSubStatement(tmp, left_tmp, right_tmp);
    else if(bin->is<IR::Shl>()) i = new IR::DpdkShlStatement(tmp, left_tmp, right_tmp);
    else if(bin->is<IR::Shr>()) i = new IR::DpdkShrStatement(tmp, left_tmp, right_tmp);
    else if(bin->is<IR::Equ>()) i = new IR::DpdkEquStatement(tmp, left_tmp, right_tmp);
    else if(bin->is<IR::LAnd>()) i = new IR::DpdkLAndStatement(tmp, left_tmp, right_tmp);
    else if(bin->is<IR::Leq>()) i = new IR::DpdkLeqStatement(tmp, left_tmp, right_tmp);
    else if(bin->is<IR::Lss>()) i = new IR::DpdkLssStatement(tmp, left_tmp, right_tmp);
    else if(bin->is<IR::Geq>()) i = new IR::DpdkGeqStatement(tmp, left_tmp, right_tmp);
    else if(bin->is<IR::Grt>()) i = new IR::DpdkGrtStatement(tmp, left_tmp, right_tmp);
    else if(bin->is<IR::Neq>()) i = new IR::DpdkNeqStatement(tmp, left_tmp, right_tmp);
    else{
        std::cerr << bin->node_type_name() << std::endl;
        BUG("not implemented");
    }
    add_instr(i);
    return false;
}

bool TreeUnroller::preorder(const IR::MethodCallExpression *method){
    cstring tmp_name = collector->get_next_tmp();
    tmp = new IR::PathExpression(IR::ID(tmp_name));
    auto assignment = new IR::AssignmentStatement(tmp, method);
    auto convertor = new ConvertStatementToDpdk(refmap, typemap, 0, collector);
    assignment->apply(*convertor);
    for(auto i : convertor->get_instr()){
        add_instr(i);
    }
    return false;
}

bool TreeUnroller::preorder(const IR::Member *member){
    tmp = nullptr;
    return false;
}

bool TreeUnroller::preorder(const IR::PathExpression *path){
    tmp = nullptr;
    return false;
}

bool TreeUnroller::preorder(const IR::Constant *path){
    tmp = nullptr;
    return false;
}

} // namespace DPDK