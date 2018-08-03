#include "index/SymbolOccurrenceCollector.h"
#include "Annotations.h"
#include "TestFS.h"
#include "TestTU.h"
#include "index/SymbolCollector.h"
#include "index/SymbolYAML.h"
#include "clang/Index/IndexingAction.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/FileSystemOptions.h"
#include "clang/Basic/VirtualFileSystem.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Index/IndexingAction.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MemoryBuffer.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <memory>
#include <string>

using testing::AllOf;
using testing::Eq;
using testing::Field;
using testing::Not;
using testing::UnorderedElementsAre;
using testing::UnorderedElementsAreArray;

MATCHER_P(OccurrenceRange, Pos, "") {
  return std::tie(arg.Location.Start.Line,
                  arg.Location.Start.Column,
                  arg.Location.End.Line,
                  arg.Location.End.Column) ==
         std::tie(Pos.start.line, Pos.start.character, Pos.end.line,
                  Pos.end.character);
}
namespace clang {
namespace clangd {
namespace {

class SymbolIndexActionFactory : public tooling::FrontendActionFactory {
public:
  SymbolIndexActionFactory() = default;

  clang::FrontendAction *create() override {
    index::IndexingOptions IndexOpts;
    IndexOpts.SystemSymbolFilter =
        index::IndexingOptions::SystemSymbolFilterKind::All;
    IndexOpts.IndexFunctionLocals = false;
    SymbolOccurrenceKind Filter = SymbolOccurrenceKind::Declaration |
                                  SymbolOccurrenceKind::Definition |
                                  SymbolOccurrenceKind::Reference;
    Collector = std::make_shared<SymbolOccurrenceCollector>(Filter);
    return index::createIndexingAction(Collector, IndexOpts, nullptr).release();
  }

  std::shared_ptr<SymbolOccurrenceCollector> Collector;
};

class OccurrenceCollectorTest : public ::testing::Test {
public:
  OccurrenceCollectorTest()
      : InMemoryFileSystem(new vfs::InMemoryFileSystem),
        TestHeaderName(testPath("symbol.h")),
        TestFileName(testPath("symbol.cc")) {
    TestHeaderURI = URI::createFile(TestHeaderName).toString();
    TestFileURI = URI::createFile(TestFileName).toString();
  }

  bool runSymbolCollector(StringRef HeaderCode, StringRef MainCode,
                          const std::vector<std::string> &ExtraArgs = {}) {
    llvm::IntrusiveRefCntPtr<FileManager> Files(
        new FileManager(FileSystemOptions(), InMemoryFileSystem));

    auto Factory = llvm::make_unique<SymbolIndexActionFactory>();

    std::vector<std::string> Args = {
        "symbol_collector", "-fsyntax-only", "-xc++",
        "-std=c++11",       "-include",      TestHeaderName};
    Args.insert(Args.end(), ExtraArgs.begin(), ExtraArgs.end());
    // This allows to override the "-xc++" with something else, i.e.
    // -xobjective-c++.
    Args.push_back(TestFileName);

    tooling::ToolInvocation Invocation(
        Args,
        Factory->create(), Files.get(),
        std::make_shared<PCHContainerOperations>());

    InMemoryFileSystem->addFile(TestHeaderName, 0,
                                llvm::MemoryBuffer::getMemBuffer(HeaderCode));
    InMemoryFileSystem->addFile(TestFileName, 0,
                                llvm::MemoryBuffer::getMemBuffer(MainCode));
    Invocation.run();
    Occurrences = (Factory->Collector->takeOccurrences());
    return true;
  }

protected:
  llvm::IntrusiveRefCntPtr<vfs::InMemoryFileSystem> InMemoryFileSystem;
  std::string TestHeaderName;
  std::string TestHeaderURI;
  std::string TestFileName;
  std::string TestFileURI;
  SymbolOccurrenceSlab Occurrences;
};

TEST_F(OccurrenceCollectorTest, Reference) {
  Annotations Header(R"(
  class $ref1[[Foo]] {
  public:
    $ref2[[Foo]]() {}
    $ref3[[Foo]](int);
  };
  void f();
  )");
  Annotations Main(R"(
  void fff() {
    $ref4[[Foo]] foo;
    f();
  }
  )");
  runSymbolCollector(Header.code(), Main.code());
  auto H = TestTU::withHeaderCode(Header.code());
  auto Symbols = H.headerSymbols();
  auto Foo = findSymbol(Symbols, "Foo");
  EXPECT_FALSE(Occurrences.find(Foo.ID).empty());
  EXPECT_THAT(Occurrences.find(Foo.ID),
              UnorderedElementsAre(OccurrenceRange(Header.range("ref1")),
                                   OccurrenceRange(Header.range("ref2")),
                                   OccurrenceRange(Header.range("ref3")),
                                   OccurrenceRange(Main.range("ref4"))
                                   ));
}


} // namespace
} // namespace clangd
} // namespace clang
