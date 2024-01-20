#ifndef LLVM_TRANSFORMS_BSINSTRUMENTER_H
#define LLVM_TRANSFORMS_BSINSTRUMENTER_H

#include "llvm/IR/PassManager.h"
#include "llvm/IR/InstrTypes.h"

namespace llvm {

class BsInstrumenterPass : public PassInfoMixin<BsInstrumenterPass> {
public:
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

    // we want to run with -O0 on clang first, but this pass should run anyway
    static bool isRequired() { return true; }

private:
    bool requiresSwitching(Function *F, CallBase *call) const;
};

} /* namespace llvm */

#endif /* LLVM_TRANSFORMS_BSINSTRUMENTER_H */
