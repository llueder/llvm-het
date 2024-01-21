#ifndef LLVM_TRANSFORMS_BSSPLITTER_H
#define LLVM_TRANSFORMS_BSSPLITTER_H

#include <vector>

#include "llvm/IR/PassManager.h"
#include "llvm/IR/InstrTypes.h"

namespace llvm {

class BsSplitterPass : public PassInfoMixin<BsSplitterPass> {
public:
    BsSplitterPass();
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

    // we want to run with -O0 on clang first, but this pass should run anyway
    static bool isRequired() { return true; }

private:
    static std::vector<std::string> readFile(std::string fname);
    std::vector<std::string> functionsToVariant;

    using variantType_t = enum {common, A, B};
    variantType_t variantType;
};

} /* namespace llvm */

#endif /* LLVM_TRANSFORMS_BSSPLITTER_H */
