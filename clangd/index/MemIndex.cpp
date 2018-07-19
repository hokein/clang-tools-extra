//===--- MemIndex.cpp - Dynamic in-memory symbol index. ----------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===-------------------------------------------------------------------===//

#include "MemIndex.h"
#include "../FuzzyMatch.h"
#include "../Logger.h"
#include <queue>
#include <set>

namespace clang {
namespace clangd {

void MemIndex::build(
    std::shared_ptr<std::vector<const Symbol *>> Syms,
    std::shared_ptr<std::vector<const SymbolRefLocation *>> SymbolRefs) {
  llvm::DenseMap<SymbolID, const Symbol *> TempIndex;
  for (const Symbol *Sym : *Syms)
    TempIndex[Sym->ID] = Sym;
  llvm::DenseMap<SymbolIDRef, std::vector<const SymbolRefLocation *>> TempXrefIndex;
  //llvm::DenseMap<SymbolIDRef, std::string> abc;
  auto cmp = [](const SymbolRefLocation* L, const SymbolRefLocation* R) {
    return L->Loc < R->Loc;
  };
  std::set<const SymbolRefLocation*, decltype(cmp)> Dep(cmp);
  //std::set<SymbolRefLocation*, decltype(cmp)> dep(cmp);

  for (const auto *Ref : *SymbolRefs) {
    //llvm::errs() << Ref->Loc << "\n";
    auto inserted_value = Dep.insert(Ref);
    if (Ref->SymID.ID()->str() == "D6F95DC7B26A83A738DE640FF8F58F72245A08CD") {
      llvm::errs() << Ref->Loc;
      llvm::errs() << "find \n!";
    }
    if (inserted_value.second) {
    if (Ref->SymID.ID()->str() == "D6F95DC7B26A83A738DE640FF8F58F72245A08CD") {
      llvm::errs() << Ref->Loc;
      llvm::errs() << "insert\n!";
    }
      TempXrefIndex[Ref->SymID].push_back(Ref);
    }
  }

  llvm::errs() << Dep.size() << "!!\n";
  // Swap out the old symbols and index.
  {
    std::lock_guard<std::mutex> Lock(Mutex);
    Index = std::move(TempIndex);
    Symbols = std::move(Syms); // Relase old symbols.

    XrefIndex = std::move(TempXrefIndex);
    Xrefs = std::move(SymbolRefs);
  }
}

bool MemIndex::fuzzyFind(
    const FuzzyFindRequest &Req,
    llvm::function_ref<void(const Symbol &)> Callback) const {
  assert(!StringRef(Req.Query).contains("::") &&
         "There must be no :: in query.");

  std::priority_queue<std::pair<float, const Symbol *>> Top;
  FuzzyMatcher Filter(Req.Query);
  bool More = false;
  {
    std::lock_guard<std::mutex> Lock(Mutex);
    for (const auto Pair : Index) {
      const Symbol *Sym = Pair.second;

      // Exact match against all possible scopes.
      if (!Req.Scopes.empty() && !llvm::is_contained(Req.Scopes, Sym->Scope))
        continue;
      if (Req.RestrictForCodeCompletion && !Sym->IsIndexedForCodeCompletion)
        continue;

      if (auto Score = Filter.match(Sym->Name)) {
        Top.emplace(-*Score * quality(*Sym), Sym);
        if (Top.size() > Req.MaxCandidateCount) {
          More = true;
          Top.pop();
        }
      }
    }
    for (; !Top.empty(); Top.pop())
      Callback(*Top.top().second);
  }
  return More;
}

void MemIndex::lookup(const LookupRequest &Req,
                      llvm::function_ref<void(const Symbol &)> Callback) const {
  for (const auto &ID : Req.IDs) {
    auto I = Index.find(ID);
    if (I != Index.end())
      Callback(*I->second);
  }
}

void MemIndex::xrefs(
    const XrefRequest &Req,
    llvm::function_ref<void(const SymbolRefLocation &)> Callback) const {
  for (const auto &ID : Req.IDs) {
    auto I = XrefIndex.find(&ID);
    if (I != XrefIndex.end()) {
      for (auto *Ref : I->second) {
        Callback(*Ref);
      }
    }
  }
}

std::unique_ptr<SymbolIndex> MemIndex::build(SymbolSlab Slab) {
  struct Snapshot {
    SymbolSlab Slab;
    std::vector<const Symbol *> Pointers;
  };
  auto Snap = std::make_shared<Snapshot>();
  Snap->Slab = std::move(Slab);
  for (auto &Sym : Snap->Slab)
    Snap->Pointers.push_back(&Sym);
  auto S = std::shared_ptr<std::vector<const Symbol *>>(std::move(Snap),
                                                        &Snap->Pointers);
  auto MemIdx = llvm::make_unique<MemIndex>();
  //MemIdx->build(std::move(S));
  return std::move(MemIdx);
}

} // namespace clangd
} // namespace clang
