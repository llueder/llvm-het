#include "llvm/Transforms/Utils/BsSplitter.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"

#include <fstream>
#include <string>
#include <algorithm>

using namespace llvm;
using namespace std; // against llvm coding standard

static cl::opt<string> InputFilename("split-file", cl::desc("Specify input filename for bs-splitter"), cl::value_desc("filename"));
static cl::opt<string> VariantName("split-variant", cl::desc("Specify variant to generate"), cl::value_desc("common, A, B"));

BsSplitterPass::BsSplitterPass() : PassInfoMixin<BsSplitterPass>() {
    if(InputFilename.compare("") == 0) {
        errs() << "bs-split pass is used, but no split-file is given!\n";
        errs() << "the error reporting is ugly but I do not find a better way.\n";
        report_fatal_error("");
    }
    functionsToVariant = readFile(InputFilename);

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

vector<string> BsSplitterPass::readFile(string fname) {
    if(fname.compare("") == 0) {
        errs() << "bs-split pass is used, but no split-file is given!\n";
        errs() << "the error reporting is ugly but I do not find a better way.\n";
        report_fatal_error("");
    }
    vector<string> functions;
    std::ifstream file(fname);
    std::string line;
    while(std::getline(file, line)) {
        functions.push_back(line);
    }
    return functions;
}

PreservedAnalyses BsSplitterPass::run(Module &M, ModuleAnalysisManager &AM) {
    vector<Function*> functionsToDelete;
    for(Function& F : M) {
        // errs() << "checking " << F.getName() << ". ";
        const bool isDef = F.getInstructionCount() > 0;// F.isMaterializable();
        if(isDef) {
            // errs() << "It is defined here. ";
        }
        const bool isVariantedFunction = any_of(functionsToVariant.begin(), functionsToVariant.end(),
                [&F](string funcName){return funcName.compare(F.getName().data()) == 0;});
        if(isVariantedFunction) {
            // errs() << "It is a varianted function. ";
        }

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
