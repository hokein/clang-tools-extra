
#include "SymbolOccurrenceCollector.h"
#include "clang/Index/USRGeneration.h"
#include "../SourceCode.h"
#include "llvm/ADT/STLExtras.h"

namespace clang {
namespace clangd {
namespace {
llvm::Optional<SymbolID> getSymbolID(const Decl *D) {
  llvm::SmallString<128> USR;
  if (index::generateUSRForDecl(D, USR)) {
    return None;
  }
  return SymbolID(USR);
}
llvm::Optional<SymbolLocation>
getTokenLocation(SourceLocation TokLoc, const ASTContext* ASTCtx,
                 std::string &FileURIStorage) {
  const auto& SM = ASTCtx->getSourceManager();
  auto TokenLength = clang::Lexer::MeasureTokenLength(TokLoc, SM,
                                                      ASTCtx->getLangOpts());

  auto CreatePosition = [&SM](SourceLocation Loc) {
    auto LSPLoc = sourceLocToPosition(SM, Loc);
    SymbolLocation::Position Pos;
    Pos.Line = LSPLoc.line;
    Pos.Column = LSPLoc.character;
    return Pos;
  };

  SymbolLocation Result;
  Result.Start = CreatePosition(TokLoc);
  auto EndLoc = TokLoc.getLocWithOffset(TokenLength);
  Result.End = CreatePosition(EndLoc);

  const auto* F = SM.getFileEntryForID(SM.getFileID(TokLoc));
  if (!F)
    return llvm::None;
  auto FilePath = getAbsoluteFilePath(F, SM);
  if (!FilePath)
    return llvm::None;
  FileURIStorage = URI::createFile(*FilePath).toString();
  Result.FileURI = FileURIStorage;
  return Result;
}

SymbolOccurrenceKind ToOccurrenceKind(index::SymbolRoleSet Roles) {
  SymbolOccurrenceKind Kind;
  for (auto Mask : {SymbolOccurrenceKind::Declaration,
                    SymbolOccurrenceKind::Definition,
                    SymbolOccurrenceKind::Reference}) {
    if (Roles & static_cast<unsigned>(Mask)) {
      Kind |= Mask;
    }
  }
  return Kind;
}
} // namespace

bool SymbolOccurrenceCollector::handleDeclOccurence(
    const Decl *D, index::SymbolRoleSet Roles,
    ArrayRef<index::SymbolRelation> Relations, SourceLocation Loc,
    index::IndexDataConsumer::ASTNodeInfo ASTNode) {
  if (D->isImplicit())
    return true;

  std::string FileURI;

  auto AddOccurrence = [&](SourceLocation L, const SymbolID& ID) {
    if (auto Location = getTokenLocation(Loc, ASTCtx, FileURI)) {
      SymbolOccurrence Occurrence;
      Occurrence.Location = *Location;
      Occurrence.Kind = ToOccurrenceKind(Roles);
      Occurrences.insert(ID, Occurrence);
    }
  };
  if (static_cast<unsigned>(Filter) & Roles) {
    if (auto ID = getSymbolID(D)) {
      if (!SelectedIDs || llvm::is_contained(*SelectedIDs, *ID)) {
        AddOccurrence(Loc, *ID);
      }
    }
  }
  return true;
}

} // namespace clangd
} // namespace clang
