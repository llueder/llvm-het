#include "llvm/Transforms/Utils/HetInstrumenter.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"

#include <fstream>
#include <string>

using namespace llvm;
using namespace std; // against llvm coding standard

static cl::opt<string> InputFilename("switch-file", cl::desc("Specify input filename for het-instrument"), cl::value_desc("filename"));

HetInstrumenterPass::HetInstrumenterPass() : PassInfoMixin<HetInstrumenterPass>() {
    if(InputFilename.compare("") == 0) {
        errs() << "het-instrument pass is used, but no switch-file is given!\n";
        errs() << "the error reporting is ugly but I do not find a better way.\n";
        report_fatal_error("");
    }
    callSitesToInstrument = readFile(InputFilename);
    // errs() << "using file " << InputFilename << "\n";
}

vector<HetInstrumenterPass::callSiteInfo> HetInstrumenterPass::readFile(string fname) {
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

bool HetInstrumenterPass::requiresSwitching(Function *F, CallBase *call) const {
    // errs() << "check " << call->getFunction()->getName() << " for call to " << F->getName() << "\n";
    for(const auto& callSite : callSitesToInstrument) {
        if(callSite.first.compare(call->getFunction()->getName().data()) == 0 &&
               callSite.second.compare(F->getName().data()) == 0) {
            // errs() << "insert at call from " << callSite.first << " to " << callSite.second << "\n";// endl;
            return true;
        } else {
            // errs() << "not insert at call from " << callSite.first << " to " << callSite.second << "\n";//endl;
        }
    }
    return false;
}

PreservedAnalyses HetInstrumenterPass::run(Function &F, FunctionAnalysisManager &AM) {
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
