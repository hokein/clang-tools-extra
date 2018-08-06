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

MATCHER(MyMatcher, "") {
  const clang::clangd::SymbolOccurrence& Pos = testing::get<0>(arg);
  const clang::clangd::Range& Range = testing::get<1>(arg);

  return std::tie(Pos.Location.Start.Line,
                  Pos.Location.Start.Column,
                  Pos.Location.End.Line,
                  Pos.Location.End.Column) ==
         std::tie(Range.start.line, Range.start.character, Range.end.line,
                  Range.end.character);
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

//std::vector<testing::Matcher<SymbolOccurrence>>
//OccurrencesFromTest(const Annotations& Test, llvm::StringRef RefName) {
  //std::vector<testing::Matcher<SymbolOccurrence>> Expected;

  //for (const auto  &Range: Test.ranges(RefName)) {
    ////Expected.push_back(Range);
    //Expected.push_back(MatchesRange(Range));
  //}
  //return Expected;
//}



//testing::Matcher<const std::vector<SymbolOccurrence>&>
//OccurrencesFromTest(const Annotations& Test, llvm::StringRef RefName) {
  //std::vector<SymbolOccurrence> Expected;

  //for (const auto  &Range: Test.ranges(RefName)) {
    ////Expected.push_back(Range);
  //}
  //return UnorderedElementsAreArray(Expected);
//}

//std::vector<Range>
//OccurenceRangesFromTest(const Annotations& Test, llvm::StringRef RefName) {
  //std::vector<Range> Expected;
  //for (const auto  &Range: Test.ranges(RefName)) {
    ////Expected.push_back(Range);
    //Expected.push_back(MatchesRange(Range));
  //}
  //return Expected;
//}
std::vector<Range> operator+(const std::vector<Range>& L, const std::vector<Range>& R) {
  std::vector<Range> Result = L;
  Result.insert(Result.end(), R.begin(), R.end());
  return Result;
}

TEST_F(OccurrenceCollectorTest, Reference) {
  Annotations Header(R"(
  class $ref[[Foo]] {
  public:
    $ref[[Foo]]() {}
    $ref[[Foo]](int);
  };

  class $bar[[Bar]];

  )");
  Annotations Main(R"(
  class $bar[[Bar]] {};

  void f();
  void fff() {
    $ref[[Foo]] foo;

    $bar[[Bar]] bar;
    f();
  }
  )");
  runSymbolCollector(Header.code(), Main.code());
  auto H = TestTU::withHeaderCode(Header.code());
  auto Symbols = H.headerSymbols();
  auto Foo = findSymbol(Symbols, "Foo");
  auto Bar = findSymbol(Symbols, "Bar");

  EXPECT_THAT(Occurrences.find(Foo.ID),
              testing::UnorderedPointwise(MyMatcher(), Header.ranges("ref") +
                                                           Main.ranges("ref")));

  EXPECT_THAT(Occurrences.find(Bar.ID),
              testing::UnorderedPointwise(MyMatcher(), Header.ranges("bar") +
                                                           Main.ranges("bar")));
}


} // namespace
} // namespace clangd
} // namespace clang
