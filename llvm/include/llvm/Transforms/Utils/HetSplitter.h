#ifndef LLVM_TRANSFORMS_HETSPLITTER_H
#define LLVM_TRANSFORMS_HETSPLITTER_H

#include <vector>

#include "llvm/IR/PassManager.h"
#include "llvm/IR/InstrTypes.h"

namespace llvm {

class HetSplitterPass : public PassInfoMixin<HetSplitterPass> {
public:
    HetSplitterPass();
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

    // we want to run with -O0 on clang first, but this pass should run anyway
    static bool isRequired() { return true; }

private:
    class VariantingInfo {
    private:
        std::vector<std::string> functions;
        bool variantAll = false;
    public:
        void addFunction(std::string name);
        void setAll();

        bool shallBeVarianted(std::string functionName);
    };

    static VariantingInfo readFile(std::string fname, std::string moduleName);

    using variantType_t = enum {common, A, B};
    variantType_t variantType;
};

} /* namespace llvm */

#endif /* LLVM_TRANSFORMS_HETSPLITTER_H */
