#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/InlineAdvisor.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/ReplayInlineAdvisor.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/Utils/ImportedFunctionsInliningStatistics.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include <fstream>
#include <iostream>
#include <sstream>

#include "InlineAdvisorPlugin.h"

#define DOT_FORMAT

using namespace llvm;

/////////////
// Helpers //
/////////////

static inline std::string getLocString(DebugLoc loc, bool show_inlining) {
  std::string OS{};
  OS += loc->getFilename();
  OS += ":";
  OS += std::to_string(loc.getLine());
  if (loc.getCol() != 0) {
    OS += ":";
    OS += std::to_string(loc.getCol());
  }

  if (show_inlining) {
    if (DebugLoc InlinedAtDL = loc->getInlinedAt()) {
      OS += "@[";
      OS += getLocString(InlinedAtDL, true);
      OS += "]";
    }
  }

  return OS;
}

static inline void printCallGraph(Module &M) {
  // check if INLINE_ADVISOR_DOT_FORMAT is set to 1
  bool dot_format = false;
  if (const char *df = std::getenv("INLINE_ADVISOR_DOT_FORMAT")) {
    if (df[0] == '1') {
      dot_format = true;
    }
  }
  if (dot_format) {
    std::cout << "digraph {" << std::endl;
  }
  for (auto &F : M) {
    for (auto &BB : F) {
      for (auto &I : BB) {
        if (auto *CB = dyn_cast<CallBase>(&I)) {
          if (CB->getCalledFunction()) {
            // continue if this is llvm intrinsic
            if (CB->getCalledFunction()->isIntrinsic()) {
              continue;
            }
            std::string caller = CB->getCaller()->getName().str();
            // add debug info to caller
            std::string caller_loc = getLocString(CB->getDebugLoc(), false);
            std::string callee = CB->getCalledFunction()->getName().str();
            std::string location{};
            if (dot_format) {
              location =
                  caller + " -> " + callee + " [label=\"" + caller_loc + "\"]";
            } else {
              location =
                  caller + " -> " + callee + " @ " + caller_loc;
            }
            std::cout << location << std::endl;
          }
        }
      }
    }
  }
  if (dot_format) {
    std::cout << "}" << std::endl;
  }
}

/////////////////////////
// InlineAdvisorPlugin //
/////////////////////////

InlineAdvisorPlugin::InlineAdvisorPlugin(Module &M,
                                         FunctionAnalysisManager &FAM,
                                         InlineParams Params, InlineContext IC)
    : InlineAdvisor(M, FAM, IC), M(M), DefaultAdvisor(M, FAM, Params, IC) {
  // print all call caller/callee pairs
  std::cout << "Original Call Graph:" << std::endl;
  printCallGraph(M);

  // if environemnt variable INLINE_ADVISOR_MAP_FILE is set use it to call
  // parseAdviceFile
  if (const char *filename = std::getenv("INLINE_ADVISOR_MAP_FILE")) {
    parseAdviceFile(filename);
  }
}

void InlineAdvisorPlugin::onPassExit(LazyCallGraph::SCC *SCC) {
  std::cout << "Final Call Graph:" << std::endl;
  printCallGraph(M);

  std::cout << "Decisions:" << std::endl;

  // update status of decisions
  for (auto &decision : decisionsTaken) {
    bool found = false;
    for (auto &F : M) {
      for (auto &BB : F) {
        for (auto &I : BB) {
          if (auto *CB = dyn_cast<CallBase>(&I)) {
            Function *caller = CB->getCaller();
            Function *callee = CB->getCalledFunction();
            DebugLoc loc = CB->getDebugLoc();

            if (caller == decision.caller && callee == decision.callee &&
                loc == decision.loc) {
              found = true;
              break;
            }
          }
        }
      }
    }
    decision.status = found ^ decision.decision;
  }

  for (auto &decision : decisionsTaken) {
    std::cout << decision.caller->getName().str() << " -> "
              << decision.callee->getName().str() << " @ "
              << getLocString(decision.loc, false) << " : "
              << (decision.decision ? "inline" : "no-inline")
              << (decision.overridden
                      ? decision.status ? " ACCEPTED" : " REJECTED"
                      : " DEFAULT")
              << std::endl;
  }
}

std::unique_ptr<InlineAdvice> InlineAdvisorPlugin::getAdviceImpl(CallBase &CB) {
  auto advice = DefaultAdvisor.getAdvice(CB);

  Function *caller = CB.getCaller();
  Function *callee = CB.getCalledFunction();

  DebugLoc loc = advice->getOriginalCallSiteDebugLoc();

  std::string CallLocation = caller->getName().str() + " -> " +
                             callee->getName().str() + " @ " +
                             getLocString(loc, false);

  bool overriden = false;
  if (adviceMap.find(CallLocation) != adviceMap.end()) {
    overriden = true;
    advice->recordUnattemptedInlining();
    advice = std::make_unique<InlineAdvice>(this, CB, getCallerORE(CB),
                                            adviceMap[CallLocation]);
  }

  decisionsTaken.push_back(
      {caller, callee, loc, advice->isInliningRecommended(), overriden, false});

  return advice;
}

void InlineAdvisorPlugin::parseAdviceFile(std::string &&filename) {
  std::ifstream file(filename);
  std::string line;
  // read line by line and skip until Decisions
  while (std::getline(file, line)) {
    if (line == "Decisions:") {
      break;
    }
  }

  while (std::getline(file, line)) {
    std::istringstream iss(line);
    std::string tmp;
    std::string caller;
    std::string callee;
    std::string location;
    std::string decision;
    if (!(iss >> caller >> tmp >> callee >> tmp >> location >> tmp >>
          decision)) {
      break;
    }
    std::string CallLocation = caller + " -> " + callee + " @ " + location;
    adviceMap[CallLocation] = decision == "inline";
  }
}

///////////////////////
// Register the pass //
///////////////////////

InlineAdvisor *InlinePluginFactory(Module &M, FunctionAnalysisManager &FAM,
                                   InlineParams Params, InlineContext IC) {
  return new InlineAdvisorPlugin(M, FAM, Params, IC);
}

struct DefaultDynamicAdvisor : PassInfoMixin<DefaultDynamicAdvisor> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {
    PluginInlineAdvisorAnalysis DA(InlinePluginFactory);
    MAM.registerPass([&] { return DA; });
    return PreservedAnalyses::all();
  }
};

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "InlineAdvisorPlugin", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineStartEPCallback(
                [](ModulePassManager &MPM, OptimizationLevel Level) {
                  MPM.addPass(DefaultDynamicAdvisor());
                });
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &PM,
                   ArrayRef<PassBuilder::PipelineElement> InnerPipeline) {
                  if (Name == "inline-advisor-plugin") {
                    PM.addPass(DefaultDynamicAdvisor());
                    return true;
                  }
                  return false;
                });
          }};
}