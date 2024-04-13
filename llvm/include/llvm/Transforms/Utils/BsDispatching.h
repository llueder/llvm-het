#ifndef LLVM_TRANSFORMS_BSDISPATCHING_H
#define LLVM_TRANSFORMS_BSDISPATCHING_H

#include <vector>
#include <string>

#include "llvm/IR/PassManager.h"
#include "llvm/IR/InstrTypes.h"

namespace llvm {

/**
This pass is an extended version of BsSplitter, that is now unused.
It inserts a dispatcher function for specialized functions in the common variant
and renames the functions in the A/B variants.
*/
class BsDispatchingPass : public PassInfoMixin<BsDispatchingPass> {
public:
    BsDispatchingPass();
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

        bool isSpecialized(std::string functionName);
    };

    static VariantingInfo readFile(std::string fname, std::string moduleName);
    std::string getModuleName(const Module &M);
    bool endsWith(const std::string& str, const std::string& end);

    using variantType_t = enum {common, A, B};
    variantType_t variantType;
};

} /* namespace llvm */

#endif /* LLVM_TRANSFORMS_BSDISPATCHING_H */
