#pragma once

#include "llvm/ADT/StringSet.h"
#include "llvm/Analysis/InlineAdvisor.h"

#include <unordered_map>

namespace llvm {
class CallBase;
class Function;
class LLVMContext;
class Module;
class DebugLoc;

class InlineAdvisorPlugin : public InlineAdvisor {

public:
  InlineAdvisorPlugin(Module &M, FunctionAnalysisManager &FAM,
                  InlineParams Params, InlineContext IC);

  virtual void onPassExit(LazyCallGraph::SCC *SCC = nullptr) override;

  std::unique_ptr<InlineAdvice> getAdviceImpl(CallBase &CB) override;

private:
  Module &M;
  DefaultInlineAdvisor DefaultAdvisor;
  std::unordered_map<std::string, bool> adviceMap;

  struct FnInfo {
    Function* fn;
    std::string name;
  };

  struct InlineDecision {
    FnInfo caller;
    FnInfo callee;
    DebugLoc loc;
    bool decision;
    bool overridden;
    bool status;
  };
  std::vector<InlineDecision> decisionsTaken;
  std::map<Function*, FnInfo> deadCallTracker;

  void parseAdviceFile(std::string&& filename);
};
} // namespace llvm