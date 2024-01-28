#include "llvm/Transforms/Utils/BsSplitter.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"

#include <fstream>
#include <string>
#include <algorithm>
#include <filesystem>

using namespace llvm;
using namespace std; // against llvm coding standard

static cl::opt<string> InputFilename("variant-file", cl::desc("Specify input filename for bs-splitter"), cl::value_desc("filename"));
static cl::opt<string> VariantName("variant", cl::desc("Specify variant to generate"), cl::value_desc("common, A, B"));


void BsSplitterPass::VariantingInfo::addFunction(std::string name) {
    functions.push_back(name);
}

void BsSplitterPass::VariantingInfo::setAll() {
    variantAll = true;
}

bool BsSplitterPass::VariantingInfo::shallBeVarianted(std::string functionName) {
    return variantAll || any_of(functions.begin(), functions.end(),
                [&](string funcName){return funcName.compare(functionName) == 0;});
}


BsSplitterPass::BsSplitterPass() : PassInfoMixin<BsSplitterPass>() {
    if(InputFilename.compare("") == 0) {
        errs() << "bs-split pass is used, but no split-file is given!\n";
        errs() << "the error reporting is ugly but I do not find a better way.\n";
        report_fatal_error("");
    }

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

BsSplitterPass::VariantingInfo BsSplitterPass::readFile(string fname, string moduleName) {
    VariantingInfo info;
    ifstream file(fname);
    string line;
    bool listen = false;
    while(getline(file, line)) {
        const auto pos = line.find(":");
        if(string::npos != pos) { // module line
            const string newModuleName = line.substr(0, pos);
            listen = moduleName.compare(newModuleName) == 0;
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

string getModuleName(const Module &M) {
    filesystem::path p(M.getName().data());
    return p.stem();
}

PreservedAnalyses BsSplitterPass::run(Module &M, ModuleAnalysisManager &AM) {
    VariantingInfo info = readFile(InputFilename, getModuleName(M));

    vector<Function*> functionsToDelete;
    for(Function& F : M) {
        // errs() << "checking " << F.getName() << ". ";
        const bool isDef = F.getInstructionCount() > 0;// F.isMaterializable();
        if(isDef) {
            // errs() << "It is defined here. ";
        }
        const bool isVariantedFunction = info.shallBeVarianted(F.getName().data());
        // if(isVariantedFunction) {
            // errs() << "It is a varianted function. ";
        // }

        const bool isVariantedFuncDev = isDef && isVariantedFunction;

        if(isVariantedFuncDev ^ (variantType != variantType_t::common)) {
            // errs() << "I remove it.";
            functionsToDelete.push_back(&F);
        }
        // errs() << "\n";
    }

    for(Function* F : functionsToDelete) {
        F->eraseFromParent();
    }

    return PreservedAnalyses::none();
}
