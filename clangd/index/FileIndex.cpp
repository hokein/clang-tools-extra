//===--- FileIndex.cpp - Indexes for files. ------------------------ C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "FileIndex.h"
#include "SymbolCollector.h"
#include "clang/Index/IndexingAction.h"
#include "clang/Lex/Preprocessor.h"

namespace clang {
namespace clangd {

SymbolRefSlab indexASTRef(ASTContext &AST, std::shared_ptr<Preprocessor> PP,
                          llvm::ArrayRef<std::string> URISchemes) {
  llvm::errs() << "Index ast ref\n";
  XrefKindSet Filter = (XrefKindSet)XrefKind::Declarataion |
                       (XrefKindSet)XrefKind::Definition |
                       (XrefKindSet)XrefKind::Reference;
  SymbolReferenceCollector Collector(&AST, Filter);

  index::IndexingOptions IndexOpts;
  IndexOpts.SystemSymbolFilter =
      index::IndexingOptions::SystemSymbolFilterKind::All;
  IndexOpts.IndexFunctionLocals = true;
  std::vector<Decl *> decls = {AST.getTranslationUnitDecl()->decls().begin(),
                               AST.getTranslationUnitDecl()->decls().end()};
  //for (const auto* D : decls) {
    //llvm::errs() << "local top level Decls\n";
    //D->dump();
  //}
  indexTopLevelDecls(AST, decls, Collector, IndexOpts);
  auto R = Collector.takeSymbols();
  //for (const auto S : R) {
    //llvm::errs() << S.Loc << "\n";
  //}
  return R;
}

SymbolSlab indexAST(ASTContext &AST, std::shared_ptr<Preprocessor> PP,
                    llvm::ArrayRef<std::string> URISchemes) {
  SymbolCollector::Options CollectorOpts;
  // FIXME(ioeric): we might also want to collect include headers. We would need
  // to make sure all includes are canonicalized (with CanonicalIncludes), which
  // is not trivial given the current way of collecting symbols: we only have
  // AST at this point, but we also need preprocessor callbacks (e.g.
  // CommentHandler for IWYU pragma) to canonicalize includes.
  CollectorOpts.CollectIncludePath = false;
  CollectorOpts.CountReferences = false;
  if (!URISchemes.empty())
    CollectorOpts.URISchemes = URISchemes;
  CollectorOpts.Origin = SymbolOrigin::Dynamic;

  SymbolCollector Collector(std::move(CollectorOpts));
  Collector.setPreprocessor(PP);
  index::IndexingOptions IndexOpts;
  // We only need declarations, because we don't count references.
  IndexOpts.SystemSymbolFilter =
      index::IndexingOptions::SystemSymbolFilterKind::DeclarationsOnly;
  IndexOpts.IndexFunctionLocals = false;

  std::vector<const Decl *> TopLevelDecls(
      AST.getTranslationUnitDecl()->decls().begin(),
      AST.getTranslationUnitDecl()->decls().end());
  index::indexTopLevelDecls(AST, TopLevelDecls, Collector, IndexOpts);

  return Collector.takeSymbols();
}

FileIndex::FileIndex(std::vector<std::string> URISchemes)
    : URISchemes(std::move(URISchemes)) {}

void FileSymbols::update(PathRef Path, std::unique_ptr<SymbolSlab> Slab,
                         std::unique_ptr<SymbolRefSlab> RefSlab) {
  std::lock_guard<std::mutex> Lock(Mutex);
  if (!Slab) {
    FileToSlabs.erase(Path);
    FileToRefSlabs.erase(Path);
  } else {
    FileToSlabs[Path] = std::move(Slab);
    FileToRefSlabs[Path] = std::move(RefSlab);
  }
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

std::shared_ptr<std::vector<const SymbolRefLocation *>>
FileSymbols::allSymbolRefs() {
  struct Snapshot {
    std::vector<const SymbolRefLocation *> Pointers;
    std::vector<std::shared_ptr<SymbolRefSlab>> KeepAlive;
  };

  auto Snap = std::make_shared<Snapshot>();
  {
    std::lock_guard<std::mutex> Lock(Mutex);

    for (const auto &FileAndSlab : FileToRefSlabs) {
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

void FileIndex::update(PathRef Path, ASTContext *AST,
                       std::shared_ptr<Preprocessor> PP) {
  if (!AST) {
    FSymbols.update(Path, nullptr, nullptr);
  } else {
    assert(PP);
    auto Slab = llvm::make_unique<SymbolSlab>();
    *Slab = indexAST(*AST, PP, URISchemes);
    auto RefSlab = llvm::make_unique<SymbolRefSlab>();
    *RefSlab = indexASTRef(*AST, PP, URISchemes);
    FSymbols.update(Path, std::move(Slab), std::move(RefSlab));
  }
  auto Symbols = FSymbols.allSymbols();
  Index.build(std::move(Symbols), FSymbols.allSymbolRefs());
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

void FileIndex::xrefs(const XrefRequest &Req,
      llvm::function_ref<void(const SymbolRefLocation&)> Callback) const {
  Index.xrefs(Req, Callback);
}

} // namespace clangd
} // namespace clang
