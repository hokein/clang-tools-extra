//===--- IndexSourceBuilderMain.cpp ------------------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "index/Index.h"
#include "index/SymbolCollector.h"
#include "index/SymbolYAML.h"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Index/IndexingAction.h"
#include "clang/Index/IndexDataConsumer.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Execution.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ThreadPool.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"

#include <map>

using namespace llvm;
using clang::clangd::SymbolSlab;
using llvm::yaml::MappingTraits;
using llvm::yaml::IO;
using llvm::yaml::Input;

namespace clang {
namespace clangd {
class SymbolIndexActionFactory : public tooling::FrontendActionFactory {
 public:
   SymbolIndexActionFactory(tooling::ExecutionContext &Context)
       : Context(Context) {}

   clang::FrontendAction *create() override {
     index::IndexingOptions IndexOpts;
     IndexOpts.SystemSymbolFilter =
         index::IndexingOptions::SystemSymbolFilterKind::All;
     IndexOpts.IndexFunctionLocals = false;
     Collector = std::make_shared<SymbolCollector>(&Context);
     return index::createIndexingAction(Collector, IndexOpts, nullptr)
         .release();
  }

  tooling::ExecutionContext &Context;
  std::shared_ptr<SymbolCollector> Collector;
};
} // namespace clangd
} // namespace clang

static cl::OptionCategory IndexSourceCategory("index-source-builder options");

static cl::opt<std::string> OutputDir("output-dir", cl::desc(R"(
The output directory for saving the results.)"),
                                      cl::init("."),
                                      cl::cat(IndexSourceCategory));

static cl::opt<std::string> MergeDir("merge-dir", cl::desc(R"(
The directory for merging symbols.)"),
                                     cl::init(""),
                                     cl::cat(IndexSourceCategory));

bool WriteFile(llvm::StringRef OutputFile, const SymbolSlab& Symbols) {
  std::error_code EC;
  llvm::raw_fd_ostream OS(OutputFile, EC, llvm::sys::fs::F_None);
  if (EC) {
    llvm::errs() << "Can't open '" << OutputFile << "': " << EC.message()
                 << '\n';
    return false;
  }
  OS << clang::clangd::SymbolToYAML(Symbols);
  return true;
}


bool Merge(llvm::StringRef MergeDir, llvm::StringRef OutputFile) {
  std::error_code EC;
  SymbolSlab Result;
  std::mutex SymbolMutex;
  auto AddSymbols = [&](const SymbolSlab& NewSymbols) {
    // Synchronize set accesses.
    std::unique_lock<std::mutex> LockGuard(SymbolMutex);
    for (const auto &Symbol : NewSymbols) {
      auto it = Result.find(Symbol.second.ID);
      if (it == Result.end())
        Result.insert(Symbol.second);
    }
  };

  // Load all symbol files in MergeDir.
  {
    llvm::ThreadPool Pool;
    for (llvm::sys::fs::directory_iterator Dir(MergeDir, EC), DirEnd;
         Dir != DirEnd && !EC; Dir.increment(EC)) {
      // Parse YAML files in parallel.
      Pool.async(
          [&AddSymbols](std::string Path) {
            auto Buffer = llvm::MemoryBuffer::getFile(Path);
            if (!Buffer) {
              llvm::errs() << "Can't open " << Path << "\n";
              return;
            }
            auto Symbols =
                clang::clangd::SymbolFromYAML(Buffer.get()->getBuffer());
            // FIXME: Merge without creating such a heavy contention point.
            AddSymbols(Symbols);
          },
          Dir->path());
    }
  }
  WriteFile(OutputFile, Result);
  return true;
}


int main(int argc, const char **argv) {
  llvm::sys::PrintStackTraceOnErrorSignal(argv[0]);
  auto Executor = clang::tooling::createExecutorFromCommandLineArgs(
      argc, argv, IndexSourceCategory);

  if (!Executor) {
    llvm::errs() << llvm::toString(Executor.takeError()) << "\n";
    return 1;
  }

  if (!MergeDir.empty()) {
    // FIXME: createExecutorFromCommandLineArgs will print "Eror while trying to
    // load a compilation databse" for the `merge` mode, we don't want this
    // warning during merging.
    llvm::errs() << "merging\n";
    Merge(MergeDir, "index-source-no-occurrences-new.yaml");
    return 0;
  }

  std::unique_ptr<clang::tooling::FrontendActionFactory> T(
      new clang::clangd::SymbolIndexActionFactory(
          *Executor->get()->getExecutionContext()));
  auto Err = Executor->get()->execute(std::move(T));
  if (Err) {
    llvm::errs() << llvm::toString(std::move(Err)) << "\n";
    return 1;
  }
  Executor->get()->getToolResults()->forEachResult(
      [](llvm::StringRef Key, llvm::StringRef Value) {
        int FD;
        SmallString<128> ResultPath;
        llvm::sys::fs::createUniqueFile(
            OutputDir + "/" + llvm::sys::path::filename(Key) + "-%%%%%%.yaml",
            FD, ResultPath);
        llvm::raw_fd_ostream OS(FD, true);
        OS << Value;
      });

  return 0;
}
