#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>


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

static std::unique_ptr<llvm::legacy::FunctionPassManager> fpm;

// In order to get per-function optimizations going, we need to set up a FunctionPassManager to hold and organize
// the LLVM optimizations that we want to run
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
            return  builder->CreateAdd(left_code, right_code, "add_binary_op");
        }

        if(op == "-") {
            return  builder->CreateSub(left_code, right_code, "minus_binary_op");
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

struct Statement {
};

struct FnStatement : Statement {
    std::string name;
    Expression *body;

    FnStatement(std::string name, Expression *body) {
        this->name = std::move(name);
        this->body = body;
    }

    llvm::Function *codegen() {

        // function type decl
        llvm::FunctionType *ft = llvm::FunctionType::get(llvm::Type::getInt32Ty(*context), false);
        llvm::Function *func = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name, module.get());

        // func body decl
        llvm::BasicBlock *block = llvm::BasicBlock::Create(*context, name + "_body", func);
        builder->SetInsertPoint(block);

        auto ret = body->codegen();
        builder->CreateRet(ret);
        llvm::verifyFunction(*func);

        fpm->run(*func);

        return func;
    }
};

int main() {
    // -------------------------
    // setup llvm for ir codegen
    // -------------------------
    context = std::make_unique<llvm::LLVMContext>();
    module = std::make_unique<llvm::Module>("llvm-module", *context);
    builder = std::make_unique<llvm::IRBuilder<>>(*context);
    fpm = std::make_unique<llvm::legacy::FunctionPassManager>(module.get());

    // Do simple "peephole" optimizations and bit-twiddling optzns.
    fpm->add(llvm::createInstructionCombiningPass());
    // Reassociate expressions.
    fpm->add(llvm::createReassociatePass());
    // Eliminate Common SubExpressions.
    fpm->add(llvm::createGVNPass());
    // Simplify the control flow graph (deleting unreachable blocks, etc).
    fpm->add(llvm::createCFGSimplificationPass());

    fpm->doInitialization();

    auto top_function = FnStatement(
            "get_number",
            new BinaryExpression(new NumberExpression(100),"+",new NumberExpression(200))
    );

    top_function.codegen();
    module->print(llvm::outs(), nullptr);

    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();

    auto TargetTriple = llvm::sys::getDefaultTargetTriple();
    module->setTargetTriple(TargetTriple);

    std::string Error;
    auto Target = llvm::TargetRegistry::lookupTarget(TargetTriple, Error);

    // Print an error and exit if we couldn't find the requested target.
    // This generally occurs if we've forgotten to initialise the
    // TargetRegistry or we have a bogus target triple.
    if (!Target) {
        llvm::errs() << Error;
        return 1;
    }

    auto CPU = "generic";
    auto Features = "";

    llvm::TargetOptions opt;
    auto RM = llvm::Optional<llvm::Reloc::Model>();
    auto TheTargetMachine =
            Target->createTargetMachine(TargetTriple, CPU, Features, opt, RM);

    module->setDataLayout(TheTargetMachine->createDataLayout());

    auto Filename = "../output.o";
    std::error_code EC;
    llvm::raw_fd_ostream dest(Filename, EC, llvm::sys::fs::OF_None);

    if (EC) {
        llvm::errs() << "Could not open file: " << EC.message();
        return 1;
    }

    llvm::legacy::PassManager pass;
    auto FileType = llvm::CGFT_ObjectFile;

    if (TheTargetMachine->addPassesToEmitFile(pass, dest, nullptr, FileType)) {
        llvm::errs() << "TheTargetMachine can't emit a file of this type";
        return 1;
    }

    pass.run(*module);
    dest.flush();

    llvm::outs() << "Wrote " << Filename << "\n";

    return 0;
}