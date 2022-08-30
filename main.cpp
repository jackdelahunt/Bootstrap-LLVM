#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <iostream>


// The static variables will be used during code generation. context is an opaque object that owns a lot of
// core LLVM data structures, such as the type and constant value tables. We don’t need to understand it in detail,
// we just need a single instance to pass into APIs that require it.
static std::unique_ptr<llvm::LLVMContext> context;

// The builder object is a helper object that makes it easy to generate LLVM instructions. Instances of the IRBuilder
// class template keep track of the current place to insert instructions and has methods to create new instructions.
static std::unique_ptr<llvm::IRBuilder<>> builder;

// module is an LLVM construct that contains functions and global variables. In many ways, it is the top-level
// structure that the LLVM IR uses to contain code. It will own the memory for all of the IR that we generate, which is
// why the codegen() method returns a raw Value*, rather than a unique_ptr<Value>.
static std::unique_ptr<llvm::Module> module;

struct Expression {
    virtual llvm::Value *codegen() = 0;
};

struct BinaryExpression : Expression {
    Expression *left;
    std::string op;
    Expression *right;

    BinaryExpression(Expression *left, std::string op, Expression *right) {
        this->left = left;
        this->op = op;
        this->right = right;
    }

    llvm::Value * codegen() override {

        auto left_code = left->codegen();
        auto right_code = right->codegen();

        if(op == "+") {
            return  builder->CreateFAdd(left_code, right_code, "add_binary_op");
        }

        if(op == "-") {
            return  builder->CreateFSub(left_code, right_code, "minus_binary_op");
        }

        return NULL;
    }
};

struct NumberExpression : Expression {
    int number;

    NumberExpression(int number) {
        this->number = number;
    }

    llvm::Value * codegen() override {
        return llvm::ConstantInt::get(*context, llvm::APInt(32, number, true));
    }
};

int main() {
    context = std::make_unique<llvm::LLVMContext>();
    module = std::make_unique<llvm::Module>("llvm-module", *context);
    builder = std::make_unique<llvm::IRBuilder<>>(*context);

    auto left = new NumberExpression(100);
    auto right = new NumberExpression(200);

    auto binary = new BinaryExpression(left, "+", right);
    auto c = binary->codegen();


    module->print(llvm::outs(), nullptr);
    c->print(llvm::outs());

    return 0;
}