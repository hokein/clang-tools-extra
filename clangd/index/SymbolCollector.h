//===--- SymbolCollector.h ---------------------------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "CanonicalIncludes.h"
#include "Index.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/Index/IndexDataConsumer.h"
#include "clang/Index/IndexSymbol.h"
#include "clang/Sema/CodeCompleteConsumer.h"

namespace clang {
namespace clangd {

/// \brief Collect declarations (symbols) from an AST.
/// It collects most declarations except:
/// - Implicit declarations
/// - Anonymous declarations (anonymous enum/class/struct, etc)
/// - Declarations in anonymous namespaces
/// - Local declarations (in function bodies, blocks, etc)
/// - Declarations in main files
/// - Template specializations
/// - Library-specific private declarations (e.g. private declaration generated
/// by protobuf compiler)
///
/// See also shouldCollectSymbol(...).
///
/// Clients (e.g. clangd) can use SymbolCollector together with
/// index::indexTopLevelDecls to retrieve all symbols when the source file is
/// changed.
class SymbolCollector : public index::IndexDataConsumer {
public:
  struct Options {
    struct CollectSymbolOptions {
      bool CollectIncludePath = false;
      /// If set, this is used to map symbol #include path to a potentially
      /// different #include path.
      const CanonicalIncludes *Includes = nullptr;
      // Populate the Symbol.References field.
      bool CountReferences = false;
      // Every symbol collected will be stamped with this origin.
      SymbolOrigin Origin = SymbolOrigin::Unknown;
      /// Collect macros.
      /// Note that SymbolCollector must be run with preprocessor in order to
      /// collect macros. For example, `indexTopLevelDecls` will not index any
      /// macro even if this is true.
      bool CollectMacro = false;
    };
    struct CollectOccurrenceOptions {
      SymbolOccurrenceKind Filter;
      // A whitelist symbols which will be collected.
      // If none, all symbol occurrences will be collected.
      llvm::Optional<llvm::DenseSet<SymbolID>> IDs = llvm::None;
    };

    /// Specifies URI schemes that can be used to generate URIs for file paths
    /// in symbols. The list of schemes will be tried in order until a working
    /// scheme is found. If no scheme works, symbol location will be dropped.
    std::vector<std::string> URISchemes = {"file"};

    /// When symbol paths cannot be resolved to absolute paths (e.g. files in
    /// VFS that does not have absolute path), combine the fallback directory
    /// with symbols' paths to get absolute paths. This must be an absolute
    /// path.
    std::string FallbackDir;

    // If not null, SymbolCollector will collect symbols.
    const CollectSymbolOptions *SymOpts;
    // If not null, SymbolCollector will collect symbol occurrences.
    const CollectOccurrenceOptions *OccurrenceOpts;
  };

  SymbolCollector(Options Opts);

  ~SymbolCollector();

  /// Returns true is \p ND should be collected.
  /// AST matchers require non-const ASTContext.
  static bool shouldCollectSymbol(const NamedDecl &ND, ASTContext &ASTCtx);

  void initialize(ASTContext &Ctx) override;

  void setPreprocessor(std::shared_ptr<Preprocessor> PP) override;

  bool
  handleDeclOccurence(const Decl *D, index::SymbolRoleSet Roles,
                      ArrayRef<index::SymbolRelation> Relations,
                      SourceLocation Loc,
                      index::IndexDataConsumer::ASTNodeInfo ASTNode) override;

  bool handleMacroOccurence(const IdentifierInfo *Name, const MacroInfo *MI,
                            index::SymbolRoleSet Roles,
                            SourceLocation Loc) override;

  SymbolSlab takeSymbols() { return std::move(Symbols).build(); }

  void finish() override;

private:
  Options Opts;

  std::shared_ptr<Preprocessor> PP;

  class CollectSymbol;
  class CollectOccurrence;
  std::unique_ptr<CollectSymbol> CollectSym;
  std::unique_ptr<CollectOccurrence> CollectOccur;
  // All symbols and symbol occurrences collected from the AST.
  SymbolSlab::Builder Symbols;
};

} // namespace clangd
} // namespace clang
