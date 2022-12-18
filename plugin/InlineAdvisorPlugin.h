#pragma once

#include "llvm/ADT/StringSet.h"
#include "llvm/Analysis/InlineAdvisor.h"

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

  struct InlineDecision {
    Function* caller;
    Function* callee;
    DebugLoc loc;
    bool decision;
    bool overridden;
    bool status;
  };
  std::vector<InlineDecision> decisionsTaken;

  void parseAdviceFile(std::string&& filename);
};
} // namespace llvm