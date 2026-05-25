#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_NICHECKER_SUPPORT_SOURCEUTILS_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_NICHECKER_SUPPORT_SOURCEUTILS_H

#include "clang-nichecker/Support/Types.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <optional>

namespace clang::nichecker {

bool startsWithISR(llvm::StringRef Name);
bool isMainFileLocation(SourceLocation Loc, const SourceManager &SM);
bool hasHeaderInclude(llvm::StringRef Source, llvm::StringRef Header);
bool hasPthreadTypedefLike(llvm::StringRef Source);

std::optional<std::string> getSourceText(SourceRange Range,
                                         const SourceManager &SM,
                                         const LangOptions &LangOpts);
std::optional<unsigned> getFileOffset(SourceLocation Loc,
                                      const SourceManager &SM);
std::optional<unsigned> getTokenLength(SourceLocation Loc,
                                       const SourceManager &SM,
                                       const LangOptions &LangOpts);

std::string defaultOutputPath(llvm::StringRef InputPath);
std::string getLineIndent(llvm::StringRef Source, unsigned Offset);
unsigned getLineStartOffset(llvm::StringRef Source, unsigned Offset);

void applyReplacements(std::string &Source,
                       std::vector<TextReplacement> Replacements);
llvm::Error writeFile(llvm::StringRef Path, llvm::StringRef Content);

} // namespace clang::nichecker

#endif
