#ifndef LLVM_TRANSFORMS_BSGLOBAL_H
#define LLVM_TRANSFORMS_BSGLOBAL_H

#include <vector>

#include "llvm/IR/PassManager.h"
#include "llvm/IR/InstrTypes.h"

namespace llvm {

class BsGlobalPass : public PassInfoMixin<BsGlobalPass> {
public:
    BsGlobalPass();
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

    static bool isRequired() { return true; }

private:
    using workingMode_t = enum {keep};
    workingMode_t workingMode;
};

} /* namespace llvm */

#endif /* LLVM_TRANSFORMS_BSGLOBAL_H */
