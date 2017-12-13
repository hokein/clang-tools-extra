//===--- SymbolCollector.h ---------------------------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Index.h"

#include "clang/Index/IndexDataConsumer.h"
#include "clang/Index/IndexSymbol.h"
#include "clang/Tooling/Execution.h"

namespace clang {
namespace clangd {

// Collect all symbols from an AST.
//
// Clients (e.g. clangd) can use SymbolCollector together with
// index::indexTopLevelDecls to retrieve all symbols when the source file is
// changed.
class SymbolCollector : public index::IndexDataConsumer {
public:
  SymbolCollector() = default;
  SymbolCollector(tooling::ExecutionContext *Context = nullptr)
      : Context(Context) {}

  void initialize(ASTContext &Ctx) override;

  bool
  handleDeclOccurence(const Decl *D, index::SymbolRoleSet Roles,
                      ArrayRef<index::SymbolRelation> Relations, FileID FID,
                      unsigned Offset,
                      index::IndexDataConsumer::ASTNodeInfo ASTNode) override;

  void finish() override;

  SymbolSlab takeSymbols() { return std::move(Symbols); }

private:
  tooling::ExecutionContext* Context;
  std::string Filename;

  // All Symbols collected from the AST.
  SymbolSlab Symbols;
};

} // namespace clangd
} // namespace clang
