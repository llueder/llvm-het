#include "llvm/Transforms/Utils/BsDispatching.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"

#include <fstream>
#include <string>
#include <algorithm>
#include <filesystem>

using namespace llvm;
using namespace std; // against llvm coding standard

static cl::opt<string> InputFilename("variant-file", cl::desc("Specify input filename for bs-dispatch"), cl::value_desc("filename"));
static cl::opt<string> VariantName("variant", cl::desc("Specify variant to generate"), cl::value_desc("common, A, B"));


void BsDispatchingPass::VariantingInfo::addFunction(std::string name) {
    functions.push_back(name);
}

void BsDispatchingPass::VariantingInfo::setAll() {
    variantAll = true;
}

bool BsDispatchingPass::VariantingInfo::isSpecialized(std::string functionName) {
    return variantAll || any_of(functions.begin(), functions.end(),
                [&](string funcName){return funcName.compare(functionName) == 0;});
}


BsDispatchingPass::BsDispatchingPass() : PassInfoMixin<BsDispatchingPass>() {
    if(VariantName.compare("common") == 0) {
        variantType = variantType_t::common;
    } else if(VariantName.compare("A") == 0) {
        variantType = variantType_t::A;
    } else if(VariantName.compare("B") == 0) {
        variantType = variantType_t::B;
    } else {
        errs() << "bs-split pass is used, but no split-variant or invalid value is given!\n";
        errs() << "the error reporting is ugly but I do not find a better way.\n";
        report_fatal_error("");
    }
}

BsDispatchingPass::VariantingInfo BsDispatchingPass::readFile(string fname, string moduleName) {
    VariantingInfo info;
    ifstream file(fname);
    if(!file.is_open()) {
        errs() << "Given variant-file does not exist or none given!\n";
        errs() << "I will variant all functions.\n";
        info.setAll();
        return info;
    }

    string line;
    bool listen = false;
    while(getline(file, line)) {
        const auto pos = line.find(":");
        if(string::npos != pos) { // module line
            const string newModuleName = line.substr(0, pos);
            listen = (moduleName.compare(newModuleName) == 0)
                || (newModuleName.compare("*") == 0);
            // errs() << newModuleName << ", listening? " << listen << "\n";
        } else if(listen) { // function line
            if(line.compare("*") == 0) {
                info.setAll();
                // errs() << "variant all\n";
            } else {
                info.addFunction(line);
                // errs() << "variant " << line << "\n";
            }
        }
    }
    return info;
}

string BsDispatchingPass::getModuleName(const Module &M) {
    filesystem::path p(M.getName().data());
    return p.stem();
}

bool BsDispatchingPass::endsWith(const string& str, const string& end) {
    if(str.length() < end.length()) {
        return false;
    }

    return str.compare(str.length() - end.length(), end.length(), end) == 0;
}

PreservedAnalyses BsDispatchingPass::run(Module &M, ModuleAnalysisManager &AM) {
    VariantingInfo info = readFile(InputFilename, getModuleName(M));

    vector<Function*> functionsToDelete;
    for(Function& F : M) {

        string fname = F.getName().data();

        // the loop also iterates over the newly added functions. Just skip them.
        if(endsWith(fname, "_A")) {
            continue;
        }
        if(endsWith(fname, "_B")) {
            continue;
        }

        const bool isDef = F.getInstructionCount() > 0;
        if(!isDef) {
            // calls/declarations need no handling, only definition
            continue;
        }
        const bool isSpecializedFunction = info.isSpecialized(F.getName().data());


        if(isSpecializedFunction) {
            if(variantType == variantType_t::common) {
                // replace body with dispatcher function

                F.erase(F.begin(), F.end());

                LLVMContext &context = F.getContext();
                FunctionType *ftype = F.getFunctionType();

                FunctionCallee func_A = M.getOrInsertFunction(fname + "_A", ftype);
                FunctionCallee func_B = M.getOrInsertFunction(fname + "_B", ftype);


                vector<Value*> args;
                for(auto& arg : F.args()) {
                    args.push_back(&arg);
                }


                // First attempt: always call the _A variant.
                // code left here for reference
                // BasicBlock* callingBB = BasicBlock::Create(context, "callingBB", &F);
                // IRBuilder<> builder(callingBB);
                // auto call = builder.CreateCall(func_A, args);
                // if(call->getType()->isVoidTy()){
                //     builder.CreateRetVoid();
                // } else {
                //     builder.CreateRet(call);
                // }


                // always call the expensive dispatcher with check-and-switch

                BasicBlock* bb_call_A = BasicBlock::Create(context, "bb_call_A", &F);
                IRBuilder<> builder_A(bb_call_A);
                auto call_A = builder_A.CreateCall(func_A, args);
                if(call_A->getType()->isVoidTy()){
                    builder_A.CreateRetVoid();
                } else {
                    builder_A.CreateRet(call_A);
                }

                BasicBlock* bb_call_B = BasicBlock::Create(context, "bb_call_B", &F, bb_call_A);
                IRBuilder<> builder_B(bb_call_B);
                auto call_B = builder_B.CreateCall(func_B, args);
                if(call_B->getType()->isVoidTy()){
                    builder_B.CreateRetVoid();
                } else {
                    builder_B.CreateRet(call_B);
                }

                BasicBlock* bb_decider = BasicBlock::Create(context, "bb_decider", &F, bb_call_B);
                IRBuilder<> builder(bb_decider);
                FunctionCallee checkAndSwitch = M.getOrInsertFunction("check_and_switch", Type::getVoidTy(context));
                builder.CreateCall(checkAndSwitch);
                Constant* current_variant = M.getOrInsertGlobal("current_variant", Type::getInt64Ty(context));
                Constant* variant_A = M.getOrInsertGlobal("variant_A", Type::getInt64Ty(context));
                auto load_cur = builder.CreateLoad(Type::getInt64Ty(context), current_variant);
                auto load_A = builder.CreateLoad(Type::getInt64Ty(context), variant_A);
                Value* isVariantA = builder.CreateICmpEQ(load_cur, load_A);
                builder.CreateCondBr(isVariantA, bb_call_A, bb_call_B);


                // future: only delete the body here, insert correct dispatcher in bs-instrument (either with or without check)
            } else if(variantType == variantType_t::A) {
                string newName = F.getName().data() + string("_A");
                F.setName(newName);
            } else if(variantType == variantType_t::B) {
                string newName = F.getName().data() + string("_B");
                F.setName(newName);
            }
        } else if(variantType != variantType_t::common) {
            // common functions are just unchanged in the common variant and deleted in the others
            functionsToDelete.push_back(&F);
        }
    }

    for(Function* F : functionsToDelete) {
        F->eraseFromParent();
    }

    // itializers for globla variables must be unique
    if(variantType != variantType_t::common) {
        for(GlobalVariable& GV : M.globals()) {
            if(GV.hasExternalLinkage()) {
                GV.setInitializer(NULL);
            }
        }
    }

    return PreservedAnalyses::none();
}
