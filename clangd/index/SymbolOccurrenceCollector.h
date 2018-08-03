
#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANGD_INDEX_OCCURRENCE_COLLECTOR_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANGD_INDEX_OCCURRENCE_COLLECTOR_H

#include "Index.h"
#include "llvm/ADT/DenseSet.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/Index/IndexDataConsumer.h"
#include "clang/Index/IndexSymbol.h"

namespace clang {
namespace clangd {

class SymbolOccurrenceCollector: public index::IndexDataConsumer {
public:
  SymbolOccurrenceCollector(SymbolOccurrenceKind Filter,
      llvm::Optional<llvm::DenseSet<SymbolID>> SelectedIDs = llvm::None)
      : Filter(Filter), SelectedIDs(SelectedIDs) {}

  void initialize(ASTContext &Ctx) override {
    ASTCtx = &Ctx;
  }

  bool
  handleDeclOccurence(const Decl *D, index::SymbolRoleSet Roles,
                      ArrayRef<index::SymbolRelation> Relations,
                      SourceLocation Loc,
                      index::IndexDataConsumer::ASTNodeInfo ASTNode) override;

  SymbolOccurrenceSlab takeOccurrences() {
    return std::move(Occurrences).build();
  }

private:
  ASTContext *ASTCtx;
  SymbolOccurrenceKind Filter;
  llvm::Optional<llvm::DenseSet<SymbolID>> SelectedIDs;
  SymbolOccurrenceSlab::Builder Occurrences;
};

} // namespace clangd
} // namespace clang

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANGD_INDEX_OCCURRENCE_COLLECTOR_H
