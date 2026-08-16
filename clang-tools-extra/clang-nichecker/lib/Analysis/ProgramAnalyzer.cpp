#include "clang-nichecker/Analysis/ProgramAnalyzer.h"
#include "clang-nichecker/Support/SourceUtils.h"

#include <cctype>
#include <cstring>

#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"

using namespace clang;

namespace clang::nichecker {

static bool containsName(const std::vector<std::string> &Names,
                         llvm::StringRef Name) {
  return llvm::is_contained(Names, Name.str());
}

static const FunctionDecl *findMainFunction(ASTContext &Context) {
  SourceManager &SM = Context.getSourceManager();
  for (Decl *D : Context.getTranslationUnitDecl()->decls()) {
    const auto *FD = dyn_cast<FunctionDecl>(D);
    if (!FD || !FD->isMain() || !FD->hasBody())
      continue;
    if (isMainFileLocation(FD->getLocation(), SM))
      return FD;
  }
  return nullptr;
}

static unsigned readPriorityComment(const FunctionDecl *FD,
                                    const SourceManager &SM) {
  SourceLocation Location = SM.getSpellingLoc(FD->getBeginLoc());
  if (!Location.isValid() || SM.getFileID(Location) != SM.getMainFileID())
    return 0;

  StringRef Source = SM.getBufferData(SM.getMainFileID());
  unsigned Offset = SM.getFileOffset(Location);
  if (Offset > Source.size())
    return 0;
  size_t CurrentLine = Source.rfind('\n', Offset);
  if (CurrentLine == StringRef::npos || CurrentLine == 0)
    return 0;
  size_t PreviousEnd = CurrentLine;
  size_t PreviousBegin = Source.rfind('\n', PreviousEnd - 1);
  PreviousBegin =
      PreviousBegin == StringRef::npos ? 0 : static_cast<size_t>(PreviousBegin + 1);
  StringRef PreviousLine = Source.slice(PreviousBegin, PreviousEnd).trim();
  size_t PriorityStart = PreviousLine.find("priority");
  if (PriorityStart == StringRef::npos)
    return 0;

  StringRef Value = PreviousLine.substr(PriorityStart + strlen("priority")).trim();
  if (Value.empty())
    return 0;
  size_t NumberStart = 0;
  while (NumberStart < Value.size() &&
         !std::isdigit(static_cast<unsigned char>(Value[NumberStart])))
    ++NumberStart;
  Value = Value.substr(NumberStart);
  unsigned Priority = 0;
  return Value.getAsInteger(10, Priority) ? 0 : Priority;
}

ProgramAnalyzer::ProgramAnalyzer(ASTContext &Context)
    : Context(Context), SM(Context.getSourceManager()) {}

bool ProgramAnalyzer::VisitFunctionDecl(FunctionDecl *FD) {
  if (FD->isMain() && FD->hasBody() && isMainFileLocation(FD->getLocation(), SM))
    Summary.MainFunction = FD;

  if (!FD->hasBody())
    return true;

  llvm::StringRef Name = FD->getName();
  if (startsWithISR(Name) && !containsName(Summary.InterruptFunctions, Name)) {
    Summary.InterruptFunctions.push_back(Name.str());
    Summary.InterruptInfos.push_back(
        InterruptInfo{Name.str(), "random", readPriorityComment(FD, SM)});
  }
  return true;
}

bool ProgramAnalyzer::VisitCallExpr(CallExpr *Call) {
  const FunctionDecl *DirectCallee = Call->getDirectCallee();
  if (!DirectCallee)
    return true;

  llvm::StringRef Name = DirectCallee->getName();
  if (Name == "pthread_create") {
    Summary.UsesPthreadCreate = true;
    if (Call->getNumArgs() >= 3) {
      if (const auto *ArgRef =
              dyn_cast<DeclRefExpr>(Call->getArg(2)->IgnoreParenImpCasts())) {
        if (const auto *FD = dyn_cast<FunctionDecl>(ArgRef->getDecl())) {
          if (!containsName(Summary.ThreadEntryFunctions, FD->getName()))
            Summary.ThreadEntryFunctions.push_back(FD->getName().str());
        }
      }
    }
  } else if (Name == "enable_isr") {
    Summary.UsesEnableISR = true;
  } else if (Name == "disable_isr") {
    Summary.UsesDisableISR = true;
  }

  return true;
}

ProgramSummary ProgramAnalyzer::finalize() {
  if (!Summary.InterruptFunctions.empty() || Summary.UsesEnableISR ||
      Summary.UsesDisableISR) {
    Summary.Kind = ProgramKind::InterruptDriven;
  } else if (Summary.UsesPthreadCreate) {
    Summary.Kind = ProgramKind::MultiThreaded;
  } else {
    Summary.Kind = ProgramKind::Sequential;
  }

  return Summary;
}

ProgramSummary analyzeProgram(ASTContext &Context) {
  ProgramAnalyzer Analyzer(Context);
  Analyzer.TraverseDecl(Context.getTranslationUnitDecl());
  return Analyzer.finalize();
}

llvm::Error applyInterruptConfig(ProgramSummary &Summary,
                                 llvm::StringRef ConfigPath) {
  auto BufferOrError = llvm::MemoryBuffer::getFile(ConfigPath);
  if (!BufferOrError)
    return llvm::errorCodeToError(BufferOrError.getError());
  llvm::Expected<llvm::json::Value> Value =
      llvm::json::parse((*BufferOrError)->getBuffer());
  if (!Value)
    return Value.takeError();
  llvm::json::Object *Root = Value->getAsObject();
  if (!Root)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "ISR 配置必须是 JSON 对象: %s",
                                   ConfigPath.str().c_str());

  for (const auto &Entry : *Root) {
    const llvm::json::Object *Object = Entry.second.getAsObject();
    if (!Object)
      return llvm::createStringError(
          llvm::inconvertibleErrorCode(),
          "ISR 配置项 '%s' 必须是 JSON 对象", Entry.first.str().c_str());
    InterruptInfo *Info = nullptr;
    for (InterruptInfo &Candidate : Summary.InterruptInfos)
      if (Candidate.Name == Entry.first) {
        Info = &Candidate;
        break;
      }
    if (!Info) {
      Summary.InterruptFunctions.push_back(Entry.first.str());
      Summary.InterruptInfos.push_back(InterruptInfo{Entry.first.str()});
      Info = &Summary.InterruptInfos.back();
    }
    if (std::optional<llvm::StringRef> Kind = Object->getString("kind"))
      Info->Kind = Kind->str();
    if (std::optional<int64_t> Priority = Object->getInteger("prio")) {
      if (*Priority < 0)
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "ISR '%s' 的 prio 不能为负数",
                                       Entry.first.str().c_str());
      Info->Priority = static_cast<unsigned>(*Priority);
    }
    if (std::optional<int64_t> Period = Object->getInteger("t")) {
      if (*Period < 0)
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "ISR '%s' 的 t 不能为负数",
                                       Entry.first.str().c_str());
      Info->Period = static_cast<unsigned>(*Period);
    }
    if (std::optional<llvm::StringRef> Event = Object->getString("event"))
      Info->Event = Event->str();
    if (std::optional<int64_t> Constraint =
            Object->getInteger("constraint")) {
      if (*Constraint < 0)
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "ISR '%s' 的 constraint 不能为负数",
                                       Entry.first.str().c_str());
      Info->Constraint = static_cast<unsigned>(*Constraint);
    }
  }
  return llvm::Error::success();
}

ProgramSummary refreshSummaryForCurrentAST(ASTContext &Context,
                                           const ProgramSummary &Previous) {
  ProgramSummary Refreshed = Previous;
  Refreshed.MainFunction = findMainFunction(Context);
  return Refreshed;
}

} // namespace clang::nichecker
