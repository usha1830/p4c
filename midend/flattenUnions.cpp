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

#include "flattenUnions.h"

namespace P4 {
const IR::Node* HandleValidityHeaderUnion::setInvalidforRest(const IR::Statement *s,
                                         const IR::Member *m, const IR::Type_HeaderUnion *hu,
                       cstring exclude, bool setValidforCurrMem) {
    auto code_block = new IR::IndexedVector<IR::StatOrDecl>;
    std::cout << "Header union setValid handling" << std::endl;
    code_block->push_back(s);
    if (setValidforCurrMem) {
        auto method = new IR::Member(s->srcInfo, new IR::Member(m->expr,IR::ID(exclude)),
                                     IR::ID(IR::Type_Header::setValid));
        auto mc = new IR::MethodCallExpression(s->srcInfo, method,
                                               new IR::Vector<IR::Argument>());
        typeMap->setType(method, new IR::Type_Method(IR::Type_Void::get(),
                         new IR::ParameterList(), IR::Type_Header::setValid));
        code_block->push_back(new IR::MethodCallStatement(mc->srcInfo, mc));
    }

    for (auto sfu : hu->fields) {
        std::cout << "setValid for" << sfu->name << std::endl;
        if (exclude != sfu->name.name) {  //setInvalid for rest of the fields
            auto method = new IR::Member(s->srcInfo, new IR::Member(m->expr,sfu->name),
                                         IR::ID(IR::Type_Header::setInvalid));
            auto mc = new IR::MethodCallExpression(s->srcInfo, method,
                                                   new IR::Vector<IR::Argument>());
            typeMap->setType(method, new IR::Type_Method(IR::Type_Void::get(),
                             new IR::ParameterList(), IR::Type_Header::setInvalid));
            code_block->push_back(new IR::MethodCallStatement(mc->srcInfo, mc));
        }
    }
    return new IR::BlockStatement(*code_block);
}

const IR::Node * HandleValidityHeaderUnion::expandIsValid(const IR::Statement *a, const IR::MethodCallExpression *mce) {
        auto mi = P4::MethodInstance::resolve(mce, refMap, typeMap);
        if (auto bim = mi->to<P4::BuiltInMethod>()) {
            if (bim->name == "isValid") {   //hdr.u.isValid() or u.isValid
                std::cout << "isValid" << a << std::endl;
                if (auto huType = bim->appliedTo->type->to<IR::Type_HeaderUnion>()) { //u  or hdr.u
                    cstring tmp = refMap->newName("tmp");
                    IR::PathExpression *tmpVar = new IR::PathExpression(IR::ID(tmp));
                    toInsert.push_back(new IR::Declaration_Variable(IR::ID(tmp), IR::Type_Bits::get(32)));
                    auto code_block = new IR::IndexedVector<IR::StatOrDecl>;
                    code_block->push_back(new IR::AssignmentStatement(a->srcInfo,
                                          tmpVar, new IR::Constant(IR::Type_Bits::get(32), 0)));
                    for (auto sfu : huType->fields) {
                        std::cout << "isValid for" << sfu->name << std::endl;
                        auto method = new IR::Member(a->srcInfo, new IR::Member(bim->appliedTo, sfu->name),
                                                     IR::ID(IR::Type_Header::isValid));
                        auto mc = new IR::MethodCallExpression(a->srcInfo, method,
                                                               new IR::Vector<IR::Argument>());
                        typeMap->setType(method, new IR::Type_Method(IR::Type_Void::get(),
                                         new IR::ParameterList(), IR::Type_Header::isValid));
                        auto addOp = new IR::Add(a->srcInfo, tmpVar, new IR::Constant(IR::Type_Bits::get(32), 1));
                        auto assn = new IR::AssignmentStatement(a->srcInfo, tmpVar, addOp);
                        code_block->push_back(new IR::IfStatement(a->srcInfo, mc, assn, nullptr));
                    }
                    auto cond = new IR::Equ(a->srcInfo, tmpVar, new IR::Constant(IR::Type_Bits::get(32), 1));
                    if (auto  assn = a->to<IR::AssignmentStatement>())
                        code_block->push_back(new IR::AssignmentStatement(a->srcInfo, assn->left, cond));
                    else if (auto ifs = a->to<IR::IfStatement>())
                        code_block->push_back(new IR::IfStatement(a->srcInfo, cond, ifs->ifTrue, ifs->ifFalse));
                    else if (auto switchs = a->to<IR::SwitchStatement>()) {
                        code_block->push_back(new IR::SwitchStatement(a->srcInfo, cond, switchs->cases));
                    }
                    return new IR::BlockStatement(*code_block);
                }
            }
        }
        return a;
}

// Already simplified to elementwise initialization->  u.h1 = {elem1, elem2....}
// Already simplified to elementwise copy->  u = u1

const IR::Node* HandleValidityHeaderUnion::postorder(IR::AssignmentStatement* a) {
    std::cout << "Reached ASSn" << a << std::endl;
    auto left = a->left;
    auto right = a->right;
    if (auto mce = right->to<IR::MethodCallExpression>()) {
        // a = u.isValid() or <hdr/m>.u.isValid
        return expandIsValid(a, mce);
    } else if (auto lhs = left->to<IR::Member>()) {
        if (auto huType = lhs->expr->type->to<IR::Type_HeaderUnion>()) {
            if (auto hdr = right->type->to<IR::Type_Header>() && !right->is<IR::Member>()) {
            //  u.h1 = my_h1
            auto isValid = new IR::Member(right->srcInfo, right,
                              IR::ID(IR::Type_Header::isValid));
            auto result = new IR::MethodCallExpression(right->srcInfo, IR::Type::Boolean::get(),
                                                   isValid);
            typeMap->setType(isValid, new IR::Type_Method(IR::Type::Boolean::get(),
                                          new IR::ParameterList(), IR::Type_Header::isValid));

            auto trueBlock = setInvalidforRest(a, lhs, huType, lhs->member.name, true);
            auto method1 = new IR::Member(left->srcInfo, left,
                                              IR::ID(IR::Type_Header::setInvalid));
            auto mc1 = new IR::MethodCallExpression(left->srcInfo, method1,
                          new IR::Vector<IR::Argument>());
            typeMap->setType(method1, new IR::Type_Method(IR::Type_Void::get(),
                                new IR::ParameterList(), IR::Type_Header::setInvalid));
            auto ifFalse = new IR::MethodCallStatement(mc1->srcInfo, mc1);
/*            if (auto parser = findOrigCtxt<IR::P4Parser>()) {
                // TODO if is not allowed within parser, find alternate way
                return trueBlock->to<IR::BlockStatement>();
            } else {
*/	    
                auto ifStatement = new IR::IfStatement(a->srcInfo, result,
                                               trueBlock->to<IR::BlockStatement>(), ifFalse);
                return ifStatement;
  //          }
            } else if (auto rhs = right->to<IR::Member>()) {
                if (auto rhsUtype = rhs->expr->type->to<IR::Type_HeaderUnion>()) {
                    std::cout << "Handle u.h1 = u1.h1" << a << std::endl;
                    //TODO check what can be done in parser context
                    auto isValid = new IR::Member(right->srcInfo, right,
                                      IR::ID(IR::Type_Header::isValid));
                    auto result = new IR::MethodCallExpression(right->srcInfo, IR::Type::Boolean::get(),
                                                           isValid);
                    typeMap->setType(isValid, new IR::Type_Method(IR::Type::Boolean::get(),
                                                  new IR::ParameterList(), IR::Type_Header::isValid));

                    auto trueBlock = setInvalidforRest(a, lhs, huType, lhs->member.name, true);
                    auto method1 = new IR::Member(left->srcInfo, left,
                                                      IR::ID(IR::Type_Header::setInvalid));
                    auto mc1 = new IR::MethodCallExpression(left->srcInfo, method1,
                                  new IR::Vector<IR::Argument>());
                    typeMap->setType(method1, new IR::Type_Method(IR::Type_Void::get(),
                                        new IR::ParameterList(), IR::Type_Header::setInvalid));
                    auto ifFalse = new IR::MethodCallStatement(mc1->srcInfo, mc1);
                    // TODO if is not allowed within parser, find alternate way
    /*                if (auto parser = findOrigCtxt<IR::P4Parser>()) {
                        return trueBlock->to<IR::BlockStatement>();
                    } else {
		    */
                        auto ifStatement = new IR::IfStatement(a->srcInfo, result,
                                                trueBlock->to<IR::BlockStatement>(), ifFalse);
                        return ifStatement;
//                    }
                }
            }
        } else if (auto leftMem = lhs->expr->to<IR::Member>()) { // hdr.u1.h1 or u1.h1
                // Handling hdr.u1.h1.data = <constant>
                if (auto huType = leftMem->expr->type->to<IR::Type_HeaderUnion>()) { // hdr.u1 or u1
                    if (leftMem->type->is<IR::Type_Header>() && right->is<IR::Constant>()) { //h1.data = constant
                        return setInvalidforRest(a, leftMem, huType, leftMem->member.name, true);
                     }
                }
        }
    }
    return a;
}

const IR::Node* HandleValidityHeaderUnion::postorder(IR::IfStatement *a) {
    std::cout << "Reached IFS" << a << std::endl;
    if (auto mce = a->condition->to<IR::MethodCallExpression>()) {
        return expandIsValid(a, mce);
     }
    return a;
}

const IR::Node* HandleValidityHeaderUnion::postorder(IR::SwitchStatement* a) {
    std::cout << "Reached SWITCHS" << a << std::endl;
    if (auto mce = a->expression->to<IR::MethodCallExpression>()) {
        return expandIsValid(a, mce);
    }
    return a;
}

// Assumes nested structs are flattened before this pass
const IR::Node* HandleValidityHeaderUnion::postorder(IR::MethodCallStatement* mcs) {
        std::cout << "Reach MCS " << mcs << std::endl;
    auto mi = P4::MethodInstance::resolve(mcs->methodCall, refMap, typeMap);
    if (auto a = mi->to<P4::BuiltInMethod>()) {
        if (a->name == "setValid") {   //hdr.u.h1.setValid() or u.h1.setValid
            if (auto m = a->appliedTo->to<IR::Member>()) {
                if (auto huType = m->expr->type->to<IR::Type_HeaderUnion>())  //u.h1  or hdr.u.h1
                    return setInvalidforRest(mcs, m, huType, m->member.name, false);
            }
        }
    }
   std::cout << "Returning MCS " << mcs << std::endl;
   return mcs;
}

const IR::Node* HandleValidityHeaderUnion::postorder(IR::P4Action* action) {
    if (toInsert.empty())
        return action;
    auto body = new IR::BlockStatement(action->body->srcInfo);
    for (auto a : toInsert)
        body->push_back(a);
    for (auto s : action->body->components)
        body->push_back(s);
    action->body = body;
    toInsert.clear();
    return action;
}

const IR::Node* HandleValidityHeaderUnion::postorder(IR::Function* function) {
    if (toInsert.empty())
        return function;
    auto body = new IR::BlockStatement(function->body->srcInfo);
    for (auto a : toInsert)
        body->push_back(a);
    for (auto s : function->body->components)
        body->push_back(s);
    function->body = body;
    toInsert.clear();
    return function;
}

const IR::Node* HandleValidityHeaderUnion::postorder(IR::P4Parser* parser) {
    if (toInsert.empty())
        return parser;
    parser->parserLocals.append(toInsert);
    toInsert.clear();
    return parser;
}

const IR::Node* HandleValidityHeaderUnion::postorder(IR::P4Control* control) {
    if (toInsert.empty())
        return control;
    control->controlLocals.append(toInsert);
    toInsert.clear();
    return control;
}


const IR::Node* DoFlattenHeaderUnion::postorder(IR::Type_Struct* s) {
      std::cout << "Reached Type_Struct" << std::endl;
    IR::IndexedVector<IR::StructField> fields;
    for (auto sf : s->fields) {
        auto ftype = typeMap->getType(sf, true);
        if (ftype->is<IR::Type_HeaderUnion>()) {
            for (auto sfu : ftype->to<IR::Type_HeaderUnion>()->fields) {
//                cstring uName = refMap->newName(sf->name.name + "_" + sfu->name.name);
                cstring uName  = sf->name.name + "_" + sfu->name.name;
		std::cout << "Type Struct Name " << uName << std::endl;
                auto uType = sfu->type->getP4Type();
                fields.push_back(new IR::StructField(IR::ID(uName), uType));
            }
        } else {
            fields.push_back(sf);
        }
    }
    return new IR::Type_Struct(s->name, s->annotations, fields);
}

const IR::Node* DoFlattenHeaderUnion::postorder(IR::Declaration_Variable *dv) {
      std::cout << "Reached Declaration_Variable" << std::endl;
    auto ftype = typeMap->getTypeType(dv->type, true);
    if (ftype->is<IR::Type_HeaderUnion>()) {
        for (auto sfu : ftype->to<IR::Type_HeaderUnion>()->fields) {
//             cstring uName = refMap->newName(dv->name.name + "_" + sfu->name.name);
             cstring uName = dv->name.name + "_" + sfu->name.name;
	     std::cout << "New DV name " << uName << std::endl;
             auto uType =  sfu->type->getP4Type();
             toInsert.push_back(new IR::Declaration_Variable(IR::ID(uName), uType));
        }
    }
    return dv;
}

const IR::Node* DoFlattenHeaderUnion::postorder(IR::Member* m) {
      std::cout << "Reached Member" << std::endl;
    if (m->expr->type->to<IR::Type_HeaderUnion>()) {
        if (auto huf = m->expr->to<IR::Member>()) {
            return new IR::Member(huf->expr,IR::ID(huf->member.name+"_"+m->member.name));
        } else if (auto huf = m->expr->to<IR::PathExpression>()) {
            return new IR::PathExpression(IR::ID(huf->path->name.name + "_" + m->member.name));
        }
    }
    return m;
}

const IR::Node* DoFlattenHeaderUnion::postorder(IR::P4Action* action) {
    if (toInsert.empty())
        return action;
    auto body = new IR::BlockStatement(action->body->srcInfo);
    for (auto a : toInsert)
        body->push_back(a);
    for (auto s : action->body->components)
        body->push_back(s);
    action->body = body;
    toInsert.clear();
    return action;
}

const IR::Node* DoFlattenHeaderUnion::postorder(IR::Function* function) {
    if (toInsert.empty())
        return function;
    auto body = new IR::BlockStatement(function->body->srcInfo);
    for (auto a : toInsert)
        body->push_back(a);
    for (auto s : function->body->components)
        body->push_back(s);
    function->body = body;
    toInsert.clear();
    return function;
}

const IR::Node* DoFlattenHeaderUnion::postorder(IR::P4Parser* parser) {
    if (toInsert.empty())
        return parser;
    parser->parserLocals.append(toInsert);
    toInsert.clear();
    return parser;
}

const IR::Node* DoFlattenHeaderUnion::postorder(IR::P4Control* control) {
    if (toInsert.empty())
        return control;
    control->controlLocals.append(toInsert);
    toInsert.clear();
    return control;
}
}  // namespace P4
