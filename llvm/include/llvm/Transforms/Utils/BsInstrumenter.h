#ifndef LLVM_TRANSFORMS_BSINSTRUMENTER_H
#define LLVM_TRANSFORMS_BSINSTRUMENTER_H

#include <utility>
#include <vector>

#include "llvm/IR/PassManager.h"
#include "llvm/IR/InstrTypes.h"

namespace llvm {

class BsInstrumenterPass : public PassInfoMixin<BsInstrumenterPass> {
public:
    BsInstrumenterPass();
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

    // we want to run with -O0 on clang first, but this pass should run anyway
    static bool isRequired() { return true; }

private:
    using callSiteInfo = std::pair<std::string, std::string>;
    std::vector<callSiteInfo> callSitesToInstrument;

    static std::vector<callSiteInfo> readFile(std::string fname);
    bool requiresSwitching(Function *F, CallBase *call) const;
};

} /* namespace llvm */

#endif /* LLVM_TRANSFORMS_BSINSTRUMENTER_H */
