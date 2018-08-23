//===--- FileIndex.cpp - Indexes for files. ------------------------ C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "FileIndex.h"
#include "../Logger.h"
#include "SymbolCollector.h"
#include "clang/Index/IndexingAction.h"
#include "clang/Lex/Preprocessor.h"

namespace clang {
namespace clangd {

SymbolSlab indexMainAST(ParsedAST &AST,
                        llvm::ArrayRef<std::string> URISchemes) {
  // Collect references.
  SymbolCollector::Options::CollectOccurrenceOptions Opts;
  Opts.Filter = SymbolOccurrenceKind::Declaration |
                SymbolOccurrenceKind::Definition |
                SymbolOccurrenceKind::Reference;
  SymbolCollector::Options CollectorOpts;
  CollectorOpts.OccurrenceOpts = &Opts;

  if (!URISchemes.empty())
    CollectorOpts.URISchemes = URISchemes;
  SymbolCollector Collector(std::move(CollectorOpts));
  Collector.setPreprocessor(AST.getPreprocessorPtr());

  index::IndexingOptions IndexOpts;
  // We only need declarations, because we don't count references.
  IndexOpts.SystemSymbolFilter =
      index::IndexingOptions::SystemSymbolFilterKind::None;
  IndexOpts.IndexFunctionLocals = false;

  index::indexTopLevelDecls(AST.getASTContext(), AST.getLocalTopLevelDecls(),
                            Collector, IndexOpts);
  return Collector.takeSymbols();
}

SymbolSlab indexAST(ASTContext &AST, std::shared_ptr<Preprocessor> PP,
                    llvm::Optional<llvm::ArrayRef<Decl *>> TopLevelDecls,
                    llvm::ArrayRef<std::string> URISchemes) {
  SymbolCollector::Options CollectorOpts;
  SymbolCollector::Options::CollectSymbolOptions SymbolOpts;
  // FIXME(ioeric): we might also want to collect include headers. We would need
  // to make sure all includes are canonicalized (with CanonicalIncludes), which
  // is not trivial given the current way of collecting symbols: we only have
  // AST at this point, but we also need preprocessor callbacks (e.g.
  // CommentHandler for IWYU pragma) to canonicalize includes.
  SymbolOpts.CollectIncludePath = false;
  SymbolOpts.CountReferences = false;
  if (!URISchemes.empty())
    CollectorOpts.URISchemes = URISchemes;
  SymbolOpts.Origin = SymbolOrigin::Dynamic;
  CollectorOpts.SymOpts = &SymbolOpts;

  SymbolCollector Collector(std::move(CollectorOpts));
  Collector.setPreprocessor(PP);
  index::IndexingOptions IndexOpts;
  // We only need declarations, because we don't count references.
  IndexOpts.SystemSymbolFilter =
      index::IndexingOptions::SystemSymbolFilterKind::DeclarationsOnly;
  IndexOpts.IndexFunctionLocals = false;

  std::vector<Decl *> DeclsToIndex;
  if (TopLevelDecls)
    DeclsToIndex.assign(TopLevelDecls->begin(), TopLevelDecls->end());
  else
    DeclsToIndex.assign(AST.getTranslationUnitDecl()->decls().begin(),
                        AST.getTranslationUnitDecl()->decls().end());

  index::indexTopLevelDecls(AST, DeclsToIndex, Collector, IndexOpts);

  return Collector.takeSymbols();
}

FileIndex::FileIndex(std::vector<std::string> URISchemes)
    : URISchemes(std::move(URISchemes)) {}

void FileSymbols::update(PathRef Path, std::unique_ptr<SymbolSlab> Slab) {
  std::lock_guard<std::mutex> Lock(Mutex);
  if (!Slab)
    FileToSlabs.erase(Path);
  else
    FileToSlabs[Path] = std::move(Slab);
}

std::shared_ptr<std::vector<const Symbol *>> FileSymbols::allSymbols() {
  // The snapshot manages life time of symbol slabs and provides pointers of all
  // symbols in all slabs.
  struct Snapshot {
    std::vector<const Symbol *> Pointers;
    std::vector<std::shared_ptr<SymbolSlab>> KeepAlive;
  };
  auto Snap = std::make_shared<Snapshot>();
  {
    std::lock_guard<std::mutex> Lock(Mutex);

    for (const auto &FileAndSlab : FileToSlabs) {
      Snap->KeepAlive.push_back(FileAndSlab.second);
      for (const auto &Iter : *FileAndSlab.second)
        Snap->Pointers.push_back(&Iter);
    }
  }
  auto *Pointers = &Snap->Pointers;
  // Use aliasing constructor to keep the snapshot alive along with the
  // pointers.
  return {std::move(Snap), Pointers};
}

std::vector<std::shared_ptr<SymbolSlab>> FileSymbols::allSlabs() const {
  std::lock_guard<std::mutex> Lock(Mutex);

  std::vector<std::shared_ptr<SymbolSlab>> Slabs;
  for (const auto &FileAndSlab : FileToSlabs)
    Slabs.push_back(FileAndSlab.second);
  return Slabs;
}

void FileIndex::update(PathRef Path, ASTContext *AST,
                       std::shared_ptr<Preprocessor> PP,
                       llvm::Optional<llvm::ArrayRef<Decl *>> TopLevelDecls) {
  if (!AST) {
    FSymbols.update(Path, nullptr);
  } else {
    assert(PP);
    auto Slab = llvm::make_unique<SymbolSlab>();
    *Slab = indexAST(*AST, PP, TopLevelDecls, URISchemes);
    FSymbols.update(Path, std::move(Slab));
  }
  auto Symbols = FSymbols.allSymbols();
  Index.build(std::move(Symbols));
}

void FileIndex::updateMainAST(PathRef Path, ParsedAST& AST) {
  auto SlabForMainFile = llvm::make_unique<SymbolSlab>();
  *SlabForMainFile = indexMainAST(AST, URISchemes);
  FSymbols.update(Path, std::move(SlabForMainFile));
}

bool FileIndex::fuzzyFind(
    const FuzzyFindRequest &Req,
    llvm::function_ref<void(const Symbol &)> Callback) const {
  return Index.fuzzyFind(Req, Callback);
}

void FileIndex::lookup(
    const LookupRequest &Req,
    llvm::function_ref<void(const Symbol &)> Callback) const {
  Index.lookup(Req, Callback);
}

void FileIndex::findOccurrences(
    const OccurrencesRequest &Req,
    llvm::function_ref<void(const SymbolOccurrence &)> Callback) const {
  auto OccurrencesSlabs = FSymbols.allSlabs();
  for (const auto &Slab : OccurrencesSlabs) {
  for (const auto &ID : Req.IDs) {
    for (const auto &Occurrence : Slab->findOccurrences(ID)) {
      if (static_cast<int>(Req.Filter & Occurrence.Kind))
        Callback(Occurrence);
    }
  }
}
}

} // namespace clangd
} // namespace clang
