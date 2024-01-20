#include "llvm/Transforms/Utils/BsInstrumenter.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"

#include <fstream>
#include <string>
#include <vector>
#include <utility>

using namespace llvm;
using namespace std; // against llvm coding standard

using callSiteInfo = pair<string, string>;

static cl::opt<string> InputFilename("switch-file", cl::desc("Specify input filename for bs-instrument"), cl::value_desc("filename"));

vector<callSiteInfo> readFile(string fname) {
    if(fname.compare("") == 0) {
        errs() << "bs-instrument pass is used, but no switch-file is given!\n";
        errs() << "the error reporting is ugly but I do not find a better way.\n";
        report_fatal_error("");
    }
    vector<callSiteInfo> infos;
    std::ifstream file(fname);
    std::string line;
    while(std::getline(file, line)) {
        const auto pos = line.find(":");
        std::string caller = line.substr(0, pos);
        // errs() << caller << ":";
        std::string callee = line.substr(pos+1, line.size());
        // errs() << callee << "\n";
        infos.push_back({caller, callee});
    }
    return infos;
}

bool BsInstrumenterPass::requiresSwitching(Function *F, CallBase *call) const {
    // errs() << "check " << call->getFunction()->getName() << " for call to " << F->getName() << "\n";
    const auto infos = readFile(InputFilename);
    // errs() << "using file " << InputFilename << "\n";
    for(const auto& info : infos) {
        if(info.first.compare(call->getFunction()->getName().data()) == 0 &&
               info.second.compare(F->getName().data()) == 0) {
            // errs() << "insert at call from " << info.first << " to " << info.second << "\n";// endl;
            return true;
        } else {
            // errs() << "not insert at call from " << info.first << " to " << info.second << "\n";//endl;
        }
    }
    return false;
}

PreservedAnalyses BsInstrumenterPass::run(Function &F, FunctionAnalysisManager &AM) {
    LLVMContext &context = F.getContext();
    Module *module = F.getParent();
    FunctionCallee checkAndSwitch = module->getOrInsertFunction("check_and_switch", Type::getVoidTy(context));

    bool modified = false;
    for(BasicBlock& bb : F){
        for(Instruction& i : bb){
            if (CallBase *callBase = dyn_cast<CallBase>(&i)) { // any difference to if (isa<CallInst>(I) || isa<InvokeInst>(I))?
                Function *callee = callBase->getCalledFunction();
                if(NULL == callee) {
                    errs() << "callee is NULL in Instruction " << i << "\n";
                    continue;
                }
                if(requiresSwitching(callee, callBase)) {
                    IRBuilder<> builder(&i);
                    builder.CreateCall(checkAndSwitch);
                    modified = true;
                    errs() << "add checkAndSwitch before call to " << callee->getName() << " from " << F.getName() << "\n";
                }
            }
        }
    }

    return modified ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
