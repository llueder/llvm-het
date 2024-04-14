#ifndef LLVM_TRANSFORMS_HETGLOBAL_H
#define LLVM_TRANSFORMS_HETGLOBAL_H

#include <vector>

#include "llvm/IR/PassManager.h"
#include "llvm/IR/InstrTypes.h"

namespace llvm {

class HetGlobalPass : public PassInfoMixin<HetGlobalPass> {
public:
    HetGlobalPass();
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

    static bool isRequired() { return true; }

private:
};

} /* namespace llvm */

#endif /* LLVM_TRANSFORMS_HETGLOBAL_H */
