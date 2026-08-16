#include "clang-nichecker/Support/SourceUtils.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>

using namespace clang;
using namespace llvm;

namespace clang::nichecker {

std::string joinList(const std::vector<std::string> &Items) {
  if (Items.empty())
    return "-";

  std::string Out;
  raw_string_ostream OS(Out);
  for (size_t I = 0; I < Items.size(); ++I) {
    if (I)
      OS << ", ";
    OS << Items[I];
  }
  return OS.str();
}

std::string joinNotes(const std::vector<std::string> &Notes) {
  if (Notes.empty())
    return "-";

  std::string Out;
  raw_string_ostream OS(Out);
  for (size_t I = 0; I < Notes.size(); ++I) {
    if (I)
      OS << " | ";
    OS << Notes[I];
  }
  return OS.str();
}

bool startsWithISR(StringRef Name) {
  return Name.starts_with("interrupt") || Name.starts_with("ISR_") ||
         Name.starts_with("isr_") ||
         Name.starts_with("CAN_ISR") || Name.starts_with("TIMER_ISR");
}

bool isMainFileLocation(SourceLocation Loc, const SourceManager &SM) {
  if (Loc.isInvalid())
    return false;
  return SM.isWrittenInMainFile(SM.getExpansionLoc(Loc));
}

bool hasHeaderInclude(StringRef Source, StringRef Header) {
  SmallString<64> AnglePattern;
  SmallString<64> QuotePattern;
  raw_svector_ostream AngleOS(AnglePattern);
  raw_svector_ostream QuoteOS(QuotePattern);
  AngleOS << "#include <" << Header << ">";
  QuoteOS << "#include \"" << Header << "\"";
  return Source.contains(AnglePattern) || Source.contains(QuotePattern);
}

bool hasPthreadTypedefLike(StringRef Source) {
  return Source.contains("typedef") && Source.contains("pthread_t");
}

std::optional<std::string> getSourceText(SourceRange Range,
                                         const SourceManager &SM,
                                         const LangOptions &LangOpts) {
  if (Range.isInvalid())
    return std::nullopt;

  CharSourceRange CharRange = CharSourceRange::getTokenRange(Range);
  bool Invalid = false;
  StringRef Text = Lexer::getSourceText(CharRange, SM, LangOpts, &Invalid);
  if (Invalid)
    return std::nullopt;
  return Text.str();
}

std::optional<unsigned> getFileOffset(SourceLocation Loc,
                                      const SourceManager &SM) {
  if (!isMainFileLocation(Loc, SM))
    return std::nullopt;

  SourceLocation Expanded = SM.getExpansionLoc(Loc);
  if (Expanded.isInvalid())
    return std::nullopt;
  return SM.getFileOffset(Expanded);
}

std::optional<unsigned> getTokenLength(SourceLocation Loc,
                                       const SourceManager &SM,
                                       const LangOptions &LangOpts) {
  if (!isMainFileLocation(Loc, SM))
    return std::nullopt;

  SourceLocation Expanded = SM.getExpansionLoc(Loc);
  if (Expanded.isInvalid())
    return std::nullopt;
  return Lexer::MeasureTokenLength(Expanded, SM, LangOpts);
}

std::string defaultOutputPath(StringRef InputPath) {
  SmallString<256> Buffer(InputPath);
  sys::path::replace_extension(Buffer, "seq.c");
  return std::string(Buffer);
}

std::string getLineIndent(StringRef Source, unsigned Offset) {
  if (Offset > Source.size())
    return "";

  size_t LineStart = Source.rfind('\n', Offset);
  if (LineStart == StringRef::npos) {
    LineStart = 0;
  } else {
    ++LineStart;
  }

  std::string Indent;
  while (LineStart < Offset &&
         (Source[LineStart] == ' ' || Source[LineStart] == '\t')) {
    Indent.push_back(Source[LineStart]);
    ++LineStart;
  }
  return Indent;
}

unsigned getLineStartOffset(StringRef Source, unsigned Offset) {
  if (Offset > Source.size())
    return Offset;

  size_t LineStart = Source.rfind('\n', Offset);
  if (LineStart == StringRef::npos)
    return 0;
  return static_cast<unsigned>(LineStart + 1);
}

void applyReplacements(std::string &Source,
                       std::vector<TextReplacement> Replacements) {
  llvm::sort(Replacements, [](const TextReplacement &LHS,
                              const TextReplacement &RHS) {
    return LHS.Offset > RHS.Offset;
  });

  for (const TextReplacement &Replacement : Replacements) {
    if (Replacement.Offset > Source.size())
      continue;
    Source.replace(Replacement.Offset, Replacement.Length, Replacement.Text);
  }
}

Error writeFile(StringRef Path, StringRef Content) {
  std::error_code EC;
  raw_fd_ostream OS(Path, EC, sys::fs::OF_TextWithCRLF);
  if (EC)
    return errorCodeToError(EC);
  OS << Content;
  return Error::success();
}

} // namespace clang::nichecker
