#include "llvm/Transforms/Utils/BsGlobal.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"

#include <fstream>
#include <string>
#include <algorithm>
#include <filesystem>

using namespace llvm;
using namespace std; // against llvm coding standard

/*
Two problems:
1. Linkage scope is enlarged, which may cause name clashes and is against programmer's expectation
2. Relies on the fact that duplicated global variables with external linkage are combined, which is
   potentially not a desirable feature and will be changed in the linker.
*/

static cl::opt<string> workingModeInput("mode", cl::desc("Specify working mode"), cl::value_desc("keep"));

BsGlobalPass::BsGlobalPass() : PassInfoMixin<BsGlobalPass>() {
    if(workingModeInput.compare("keep") == 0) {
        workingMode = workingMode_t::keep;
    } else {
        errs() << "bs-global pass is used, but no mode or invalid mode is given!\n";
        errs() << "the error reporting is ugly but I do not find a better way.\n";
        report_fatal_error("");
    }
}

PreservedAnalyses BsGlobalPass::run(Module &M, ModuleAnalysisManager &AM) {
    vector<GlobalVariable*> variablesToDelete;
    for(GlobalVariable& GV : M.globals()) {
        if(GV.hasInternalLinkage()) {
            errs() << GV << " is internal\n";
            if(workingMode == workingMode_t::keep) {
                GV.setLinkage(GlobalValue::LinkageTypes::ExternalLinkage);
                GV.setDSOLocal(false);
            } else {
            }
        }
    }
    for(GlobalVariable* GV : variablesToDelete) {
        GV->eraseFromParent();
    }

    return PreservedAnalyses::none();
}
