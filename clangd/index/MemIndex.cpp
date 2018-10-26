//===--- MemIndex.cpp - Dynamic in-memory symbol index. ----------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===-------------------------------------------------------------------===//

#include "MemIndex.h"
#include "FuzzyMatch.h"
#include "Logger.h"
#include "Quality.h"
#include "Trace.h"
#include "llvm/ADT/StringSet.h"

using namespace llvm;
namespace clang {
namespace clangd {

std::unique_ptr<SymbolIndex> MemIndex::build(SymbolSlab Slab, RefSlab Refs) {
  // Store Slab size before it is moved.
  const auto BackingDataSize = Slab.bytes() + Refs.bytes();
  llvm::StringMap<int> FileToRefs;

  for (auto Ref : Refs) {
    for (auto R : Ref.second) {
      if (FileToRefs.count(R.Location.FileURI.str()))
        ++FileToRefs[R.Location.FileURI.str()];
      else {
        FileToRefs[R.Location.FileURI.str()] = 1;
      }
    }
  }

  std::vector<std::pair<int, std::string>> AllRefs;
  std::map<int, int> NumRefsToNumFiles;
  for (auto& it : FileToRefs) {
    AllRefs.push_back(std::make_pair(it.second, it.first()));
    NumRefsToNumFiles[it.second]++;
  }
  llvm::sort(AllRefs);
  int gen_refs = 0;
  int normal_refs = 0;
  int normal_files = 0;
  llvm::StringSet<> V;
  for (auto& R : AllRefs) {
    if (V.count(R.second)) {
      log("find visited file: {0}\n", R.second);
    }
    V.insert(R.second);
    llvm::errs() << R.first << "          " << R.second << "\n";
    if (llvm::StringRef(R.second).contains("/build/") 
       && llvm::StringRef(R.second).contains(".inc")) {
      gen_refs += R.first;
    } else {
      normal_refs += R.first;
      //log("{0}       {1}", R.first, R.second);
      ++normal_files;
    }
  }
  
  for (const auto& It: NumRefsToNumFiles) {
    //log("{0},{1}", It.first, It.second);
  }
  log("sizeof(Ref) = {0}", sizeof(Ref));
  log("number of generated refs: {0}", gen_refs);
  log("number of normal refs: {0}", normal_refs);
  log("memory size of refs {0} bytes", Refs.bytes());
  log("number of normal files: {0}", normal_files);
  log("number of all files: {0}", FileToRefs.size());
  auto Data = std::make_pair(std::move(Slab), std::move(Refs));
  return llvm::make_unique<MemIndex>(Data.first, Data.second, std::move(Data),
                                     BackingDataSize);
}

bool MemIndex::fuzzyFind(const FuzzyFindRequest &Req,
                         function_ref<void(const Symbol &)> Callback) const {
  assert(!StringRef(Req.Query).contains("::") &&
         "There must be no :: in query.");
  trace::Span Tracer("MemIndex fuzzyFind");

  TopN<std::pair<float, const Symbol *>> Top(
      Req.Limit ? *Req.Limit : std::numeric_limits<size_t>::max());
  FuzzyMatcher Filter(Req.Query);
  bool More = false;
  for (const auto Pair : Index) {
    const Symbol *Sym = Pair.second;

    // Exact match against all possible scopes.
    if (!Req.AnyScope && !Req.Scopes.empty() &&
        !is_contained(Req.Scopes, Sym->Scope))
      continue;
    if (Req.RestrictForCodeCompletion &&
        !(Sym->Flags & Symbol::IndexedForCodeCompletion))
      continue;

    if (auto Score = Filter.match(Sym->Name))
      if (Top.push({*Score * quality(*Sym), Sym}))
        More = true; // An element with smallest score was discarded.
  }
  auto Results = std::move(Top).items();
  SPAN_ATTACH(Tracer, "results", static_cast<int>(Results.size()));
  for (const auto &Item : Results)
    Callback(*Item.second);
  return More;
}

void MemIndex::lookup(const LookupRequest &Req,
                      function_ref<void(const Symbol &)> Callback) const {
  trace::Span Tracer("MemIndex lookup");
  for (const auto &ID : Req.IDs) {
    auto I = Index.find(ID);
    if (I != Index.end())
      Callback(*I->second);
  }
}

void MemIndex::refs(const RefsRequest &Req,
                    function_ref<void(const Ref &)> Callback) const {
  trace::Span Tracer("MemIndex refs");
  for (const auto &ReqID : Req.IDs) {
    auto SymRefs = Refs.find(ReqID);
    if (SymRefs == Refs.end())
      continue;
    for (const auto &O : SymRefs->second)
      if (static_cast<int>(Req.Filter & O.Kind))
        Callback(O);
  }
}

size_t MemIndex::estimateMemoryUsage() const {
  return Index.getMemorySize() + Refs.getMemorySize() + BackingDataSize;
}

} // namespace clangd
} // namespace clang
