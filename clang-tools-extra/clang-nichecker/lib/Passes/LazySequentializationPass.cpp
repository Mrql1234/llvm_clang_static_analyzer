#include "clang-nichecker/Passes/LazySequentializationPass.h"
#include "clang-nichecker/Support/LegacyJarRunner.h"
#include "clang-nichecker/Support/SourceUtils.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/FormatVariadic.h"

#include <optional>
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

using namespace clang;
using namespace llvm;

namespace clang::nichecker {

namespace {

struct ThreadPlan {
  const FunctionDecl *Function = nullptr;
  std::string Name;
  unsigned Index = 0;
  unsigned StatementCount = 0;
  bool IsMainThread = false;
  bool IsInterrupt = false;
  unsigned Priority = 0;
  std::string Kind = "random";
  unsigned Period = 0;
  std::string Event;
  unsigned Constraint = 0;
};

bool isLazySeqRequested(const PipelineOptions &Options) {
  return Options.PipelineProfile == "lazy" ||
         StringRef(Options.PipelineSpec).contains("lazyseq");
}

const InterruptInfo *findInterruptInfo(StringRef Name,
                                       const ProgramSummary &Summary) {
  for (const InterruptInfo &Info : Summary.InterruptInfos)
    if (Name == Info.Name || Name.starts_with(Info.Name + "_"))
      return &Info;
  return nullptr;
}

// Duplicator appends an instance suffix to ISR entry names. Keep the original
// handler name as the timer/event family identity.
StringRef baseThreadName(StringRef Name) {
  size_t Separator = Name.rfind('_');
  return Separator == StringRef::npos ? Name : Name.take_front(Separator);
}

std::string buildPythonInputGate(const ThreadPlan &Plan,
                                 const std::vector<ThreadPlan> &Plans,
                                 bool IsMultiThreaded,
                                 bool SkipMainTaskGate = false) {
  if (Plan.IsMainThread)
    return "";
  if (SkipMainTaskGate && Plan.Name == "main_task_0")
    return "";

  unsigned HighestPriority = 0;
  for (const ThreadPlan &Candidate : Plans)
    if (!Candidate.IsMainThread)
      HighestPriority = std::max(HighestPriority, Candidate.Priority);

  std::string Gate;
  raw_string_ostream OS(Gate);
  bool First = true;
  auto appendCompletion = [&](const ThreadPlan &Candidate) {
    if (!First)
      OS << " && ";
    First = false;
    OS << "(__cs_pc[" << Candidate.Index << "] == 0 || __cs_pc["
       << Candidate.Index << "] == " << Candidate.StatementCount << ")";
  };
  if (!IsMultiThreaded && Plan.Name == "main_task_0") {
    for (const ThreadPlan &Candidate : Plans) {
      if (Candidate.Index == Plan.Index || Candidate.Priority < Plan.Priority)
        continue;
      if (Candidate.Kind != "random" ||
          (Candidate.Priority != HighestPriority && Candidate.Name != "main"))
        appendCompletion(Candidate);
    }
    return OS.str();
  }

  if (!IsMultiThreaded && (Plan.Kind == "event" || Plan.Kind == "timer")) {
    bool HasRandomOrTimer = false;
    for (const ThreadPlan &Candidate : Plans)
      HasRandomOrTimer |= Candidate.IsInterrupt &&
                          (Candidate.Kind == "random" ||
                           Candidate.Kind == "timer");
    if (!HasRandomOrTimer)
      return "";
    for (const ThreadPlan &Candidate : Plans) {
      if (Candidate.Index == Plan.Index ||
          Candidate.Priority < Plan.Priority)
        continue;
      const bool SameEventFamily =
          Plan.Kind == "event" && Candidate.Kind == "event" &&
          baseThreadName(Candidate.Name) == baseThreadName(Plan.Name);
      const bool SameTimerFamily =
          Plan.Kind == "timer" && Candidate.Kind == "timer" &&
          baseThreadName(Candidate.Name) == baseThreadName(Plan.Name);
      if ((Plan.Kind == "event" && (SameEventFamily ||
                                    Candidate.Kind == "timer")) ||
          (Plan.Kind == "timer" && (SameTimerFamily ||
                                    Candidate.Kind == "event")))
        appendCompletion(Candidate);
    }
    return OS.str();
  }

  for (const ThreadPlan &Candidate : Plans) {
    if (Candidate.Index == Plan.Index)
      continue;
    if (IsMultiThreaded) {
      if (Candidate.Priority >= Plan.Priority)
        appendCompletion(Candidate);
      continue;
    }

    // Python lazyseq lets highest-priority random interrupts preempt freely.
    // Lower-priority random interrupts only wait for non-highest candidates.
    if (Plan.Priority != HighestPriority &&
        Candidate.Priority >= Plan.Priority &&
        Candidate.Priority != HighestPriority)
      appendCompletion(Candidate);
  }
  return OS.str();
}

bool isHighestPriorityInterrupt(const ThreadPlan &Plan,
                                const std::vector<ThreadPlan> &Plans,
                                bool IsMultiThreaded) {
  if (IsMultiThreaded || !Plan.IsInterrupt)
    return false;
  for (const ThreadPlan &Candidate : Plans)
    if (Candidate.IsInterrupt && Candidate.Priority > Plan.Priority)
      return false;
  return true;
}

bool isDeferredInterrupt(const ThreadPlan &Plan) {
  return Plan.IsInterrupt && (Plan.Kind == "event" || Plan.Kind == "timer");
}

bool hasRandomConstraint(const std::vector<ThreadPlan> &Plans) {
  for (const ThreadPlan &Plan : Plans)
    if (Plan.IsInterrupt && Plan.Kind == "random" && Plan.Constraint != 0)
      return true;
  return false;
}

bool hasTimerConstraint(const std::vector<ThreadPlan> &Plans) {
  for (const ThreadPlan &Plan : Plans)
    if (Plan.IsInterrupt && Plan.Kind == "timer" && Plan.Constraint != 0)
      return true;
  return false;
}

std::string buildRandomTimeGate(const ThreadPlan &Plan,
                                const std::vector<ThreadPlan> &Plans,
                                bool IsMultiThreaded, unsigned Round) {
  if (IsMultiThreaded || Round == 0 || !hasRandomConstraint(Plans) ||
      !Plan.IsInterrupt || Plan.Kind != "random")
    return "";
  return "(__cs_counter - __cs_counter_before >= " +
         std::to_string(Plan.Constraint) + ")";
}

std::string normalizeEventText(StringRef Text) {
  std::string Normalized;
  for (char Character : Text)
    if (!std::isspace(static_cast<unsigned char>(Character)) &&
        Character != ';')
      Normalized += Character;
  return Normalized;
}

std::optional<std::string> sourceText(const Stmt *Node,
                                      const PipelineContext &Context) {
  return getSourceText(Node->getSourceRange(), Context.getSourceManager(),
                       Context.getLangOpts());
}

std::optional<std::string> sourceText(const Expr *Node,
                                      const PipelineContext &Context) {
  return getSourceText(Node->getSourceRange(), Context.getSourceManager(),
                       Context.getLangOpts());
}

std::string indentLines(StringRef Text, StringRef Prefix) {
  std::string Output;
  raw_string_ostream OS(Output);
  size_t Start = 0;
  while (Start <= Text.size()) {
    size_t End = Text.find('\n', Start);
    StringRef Line =
        End == StringRef::npos ? Text.substr(Start) : Text.slice(Start, End);
    OS << Prefix << Line;
    if (End == StringRef::npos)
      break;
    OS << '\n';
    Start = End + 1;
  }
  return OS.str();
}

std::string ensureStatementTerminator(StringRef Text, const Stmt *Node) {
  std::string Result = Text.rtrim(" \t\r\n").str();
  if ((isa<Expr>(Node) || isa<DeclStmt>(Node) || isa<ReturnStmt>(Node) ||
       isa<BreakStmt>(Node) || isa<ContinueStmt>(Node) || isa<GotoStmt>(Node) ||
       isa<IfStmt>(Node) || isa<WhileStmt>(Node) || isa<ForStmt>(Node)) &&
      !Result.empty() && !StringRef(Result).ends_with(";")) {
    // Control-statement source ranges omit a non-compound body's semicolon.
    if (!StringRef(Result).ends_with("}"))
      Result += ';';
  }
  return Result;
}

const CallExpr *getDirectCall(const Stmt *Node) {
  const auto *ExprNode = dyn_cast<Expr>(Node);
  if (!ExprNode)
    return nullptr;
  ExprNode = ExprNode->IgnoreParenImpCasts();
  return dyn_cast<CallExpr>(ExprNode);
}

std::string getCalleeName(const CallExpr *Call, const PipelineContext &Context) {
  if (const FunctionDecl *Callee = Call->getDirectCallee())
    return Callee->getNameAsString();
  std::optional<std::string> Name = sourceText(Call->getCallee(), Context);
  return Name ? *Name : "";
}

class VisibleLazyStatementCollector
    : public RecursiveASTVisitor<VisibleLazyStatementCollector> {
public:
  explicit VisibleLazyStatementCollector(const PipelineContext &Context)
      : Context(Context) {}

  bool VisitDeclRefExpr(DeclRefExpr *Reference) {
    const auto *Variable = dyn_cast<VarDecl>(Reference->getDecl());
    if (Variable && Variable->hasGlobalStorage())
      Visible = true;
    return true;
  }

  bool VisitCallExpr(CallExpr *Call) {
    const std::string Name = getCalleeName(Call, Context);
    // The Python global-access analysis exposes synchronization calls as
    // scheduling points even when their address is held in a local variable.
    if (Name == "pthread_mutex_init" || Name == "pthread_mutex_lock" ||
        Name == "pthread_mutex_unlock" || Name == "pthread_mutex_destroy" ||
        Name == "pthread_cond_wait_1" || Name == "pthread_cond_wait_2" ||
        Name == "pthread_cond_signal" || Name == "pthread_cond_broadcast" ||
        Name == "pthread_barrier_wait_1" ||
        Name == "pthread_barrier_wait_2")
      Visible = true;
    return true;
  }

  bool isVisible() const { return Visible; }

private:
  const PipelineContext &Context;
  bool Visible = false;
};

bool isVisibleLazyStatement(const Stmt *Statement, const ThreadPlan &Plan,
                            const PipelineContext &Context) {
  // Instrumenter injects event/timer activation after every main-task
  // assignment. Its continuation PC must point at a concrete resume label.
  if (Plan.Name == "main_task_0")
    if (const auto *Assignment = dyn_cast<BinaryOperator>(Statement))
      if (Assignment->isAssignmentOp())
        return true;

  VisibleLazyStatementCollector Collector(Context);
  Collector.TraverseStmt(const_cast<Stmt *>(Statement));
  return Collector.isVisible();
}

const FunctionDecl *functionFromThreadStartArgument(const CallExpr *Call) {
  if (!Call || Call->getNumArgs() < 3)
    return nullptr;
  const Expr *ThreadStart = Call->getArg(2)->IgnoreParenImpCasts();
  while (const auto *Cast = dyn_cast<ExplicitCastExpr>(ThreadStart))
    ThreadStart = Cast->getSubExpr()->IgnoreParenImpCasts();
  if (const auto *Address = dyn_cast<UnaryOperator>(ThreadStart)) {
    if (Address->getOpcode() == UO_AddrOf)
      ThreadStart = Address->getSubExpr()->IgnoreParenImpCasts();
  }
  const auto *Reference = dyn_cast<DeclRefExpr>(ThreadStart);
  return Reference ? dyn_cast<FunctionDecl>(Reference->getDecl()) : nullptr;
}

std::string buildDefaultArguments(const FunctionDecl *FD) {
  std::string Arguments;
  raw_string_ostream OS(Arguments);
  for (unsigned Index = 0; Index < FD->getNumParams(); ++Index) {
    if (Index)
      OS << ", ";
    OS << "0";
  }
  return OS.str();
}

std::optional<TextReplacement>
renameFunction(const FunctionDecl *FD, StringRef NewName,
               const PipelineContext &Context) {
  std::optional<unsigned> Offset =
      getFileOffset(FD->getLocation(), Context.getSourceManager());
  std::optional<unsigned> Length = getTokenLength(
      FD->getLocation(), Context.getSourceManager(), Context.getLangOpts());
  if (!Offset || !Length || !*Length)
    return std::nullopt;
  return TextReplacement{*Offset, *Length, NewName.str()};
}

std::string buildRuntimePrelude(const std::vector<ThreadPlan> &Plans,
                                bool NondetCondvarWakeups) {
  std::string Lines;
  raw_string_ostream OS(Lines);
  const unsigned LockSlots = Plans.size() + 1;
  OS << "/* Native lazyseq runtime state. instrumenter adds CBMC bitwidths later. */\n";
  OS << "extern void __VERIFIER_assume(int);\n";
  OS << "extern void __VERIFIER_error(void);\n";
  if (hasTimerConstraint(Plans))
    OS << "extern int nondet_int(void);\n";
  OS << "#ifndef NULL\n#define NULL 0\n#endif\n";
  OS << "#ifndef assert\n#define assert(Condition) \\\n  do { if (!(Condition)) __VERIFIER_error(); } while (0)\n#endif\n";
  OS << "static unsigned __cs_active_thread[" << Plans.size() << "] = {1};\n";
  OS << "static unsigned __cs_disable_thread[" << Plans.size() << "] = {0};\n";
  // Python instrumenter keeps timer phases signed: a nondeterministic phase
  // is constrained to [0, 2 * constraint] before the next period starts.
  OS << "static int __cs_timer_counter[" << Plans.size() << "] = {0};\n";
  OS << "static unsigned __cs_counter = 0;\n";
  OS << "static unsigned __cs_counter_before = 0;\n";
  OS << "static unsigned __cs_pc[" << Plans.size() << "] = {0};\n";
  OS << "static unsigned __cs_pc_cs[" << Plans.size() << "] = {0};\n";
  OS << "static unsigned __cs_thread_index = 0;\n";
  OS << "static unsigned __cs_last_thread = 0;\n";
  OS << "static void *__cs_threadargs[" << Plans.size() << "] = {0};\n";
  OS << "static const unsigned __cs_thread_lines[" << Plans.size() << "] = {";
  for (size_t Index = 0; Index < Plans.size(); ++Index) {
    if (Index)
      OS << ", ";
    OS << Plans[Index].StatementCount;
  }
  OS << "};\n";
  OS << "static void *__cs_lock_address[" << LockSlots << "] = {0};\n";
  OS << "static unsigned __cs_lock_owner[" << LockSlots << "] = {0};\n\n";
  OS << "static void *__cs_cond_address[" << LockSlots << "] = {0};\n";
  OS << "static unsigned __cs_cond_state[" << LockSlots << "] = {0};\n";
  OS << "static void *__cs_barrier_address[" << LockSlots << "] = {0};\n";
  OS << "static unsigned __cs_barrier_initial[" << LockSlots << "] = {0};\n";
  OS << "static unsigned __cs_barrier_current[" << LockSlots << "] = {0};\n\n";
  OS << "static void *__cs_key_values[1][" << Plans.size() + 1 << "] = {{0}};\n";
  OS << "static void (*__cs_key_destructor[1])(void *) = {0};\n";
  OS << "static unsigned __cs_current_key = 0;\n\n";
  OS << "static void __cs_pthread_mutex_lock(void *Address);\n";
  OS << "static void __cs_pthread_mutex_unlock(void *Address);\n\n";
  OS << "#define __CS_LAZY_IF(T, A, B) \\\n";
  OS << "  if ((__cs_pc[(T)] > (A)) || ((A) >= __cs_pc_cs[(T)])) goto B\n";
  OS << "#define __CS_PTHREAD_CREATE(NewThread, Attr, Start, Arg, Id) \\\n";
  OS << "  do { *(NewThread) = (Id); __cs_active_thread[(Id)] = 1; \\\n";
  OS << "       __cs_threadargs[(Id)] = (void *)(Arg); } while (0)\n\n";
  OS << "static unsigned __cs_lock_slot(void *Address)\n{\n";
  OS << "  unsigned Index;\n";
  OS << "  for (Index = 0; Index < " << LockSlots << "; ++Index) {\n";
  OS << "    if (__cs_lock_address[Index] == Address || !__cs_lock_address[Index]) {\n";
  OS << "      __cs_lock_address[Index] = Address;\n";
  OS << "      return Index;\n    }\n  }\n";
  OS << "  return 0;\n}\n\n";
  OS << "static unsigned __cs_cond_slot(void *Address)\n{\n";
  OS << "  unsigned Index;\n";
  OS << "  for (Index = 0; Index < " << LockSlots << "; ++Index) {\n";
  OS << "    if (__cs_cond_address[Index] == Address || !__cs_cond_address[Index]) {\n";
  OS << "      __cs_cond_address[Index] = Address;\n";
  OS << "      return Index;\n    }\n  }\n";
  OS << "  return 0;\n}\n\n";
  OS << "static unsigned __cs_barrier_slot(void *Address)\n{\n";
  OS << "  unsigned Index;\n";
  OS << "  for (Index = 0; Index < " << LockSlots << "; ++Index) {\n";
  OS << "    if (__cs_barrier_address[Index] == Address || !__cs_barrier_address[Index]) {\n";
  OS << "      __cs_barrier_address[Index] = Address;\n";
  OS << "      return Index;\n    }\n  }\n";
  OS << "  return 0;\n}\n\n";
  OS << "static void __cs_pthread_mutex_init(void *Address)\n{\n";
  OS << "  __cs_lock_owner[__cs_lock_slot(Address)] = 0;\n}\n\n";
  OS << "static void __cs_pthread_cond_init(void *Address)\n{\n";
  OS << "  __cs_cond_state[__cs_cond_slot(Address)] = 0;\n}\n\n";
  OS << "static void __cs_pthread_cond_destroy(void *Address)\n{\n";
  OS << "  __cs_cond_state[__cs_cond_slot(Address)] = 0;\n}\n\n";
  OS << "static void __cs_pthread_cond_wait_1(void *Condition, void *Mutex)\n{\n";
  OS << "  (void)Condition;\n  __cs_pthread_mutex_unlock(Mutex);\n}\n\n";
  OS << "static void __cs_pthread_cond_wait_2(void *Condition, void *Mutex)\n{\n";
  if (NondetCondvarWakeups) {
    OS << "  unsigned __cs_wakeup;\n";
    OS << "  if (__cs_wakeup)\n";
    OS << "    __VERIFIER_assume(__cs_cond_state[__cs_cond_slot(Condition)] == 1);\n";
  } else {
    OS << "  __VERIFIER_assume(__cs_cond_state[__cs_cond_slot(Condition)] == 1);\n";
  }
  OS << "  __cs_pthread_mutex_lock(Mutex);\n}\n\n";
  OS << "static void __cs_pthread_cond_signal(void *Condition)\n{\n";
  OS << "  __cs_cond_state[__cs_cond_slot(Condition)] = 1;\n}\n\n";
  OS << "static void __cs_pthread_cond_broadcast(void *Condition)\n{\n";
  OS << "  __cs_cond_state[__cs_cond_slot(Condition)] = 1;\n}\n\n";
  OS << "static void __cs_pthread_barrier_init(void *Address, unsigned Count)\n{\n";
  OS << "  unsigned Slot = __cs_barrier_slot(Address);\n";
  OS << "  __cs_barrier_initial[Slot] = Count;\n";
  OS << "  __cs_barrier_current[Slot] = Count;\n}\n\n";
  OS << "static void __cs_pthread_barrier_destroy(void *Address)\n{\n";
  OS << "  unsigned Slot = __cs_barrier_slot(Address);\n";
  OS << "  __cs_barrier_initial[Slot] = 0;\n  __cs_barrier_current[Slot] = 0;\n}\n\n";
  OS << "static void __cs_pthread_barrier_wait_1(void *Address)\n{\n";
  OS << "  unsigned Slot = __cs_barrier_slot(Address);\n";
  OS << "  __VERIFIER_assume(__cs_barrier_current[Slot] > 0);\n";
  OS << "  --__cs_barrier_current[Slot];\n}\n\n";
  OS << "static void __cs_pthread_barrier_wait_2(void *Address)\n{\n";
  OS << "  unsigned Slot = __cs_barrier_slot(Address);\n";
  if (NondetCondvarWakeups) {
    OS << "  unsigned __cs_wakeup;\n";
    OS << "  if (__cs_wakeup)\n";
    OS << "    __VERIFIER_assume(__cs_barrier_current[Slot] == 0);\n";
  } else {
    OS << "  __VERIFIER_assume(__cs_barrier_current[Slot] == 0);\n";
  }
  OS << "  __cs_barrier_current[Slot] = __cs_barrier_initial[Slot];\n}\n\n";
  OS << "static unsigned __cs_pthread_self(void)\n{\n";
  OS << "  return __cs_thread_index + 1;\n}\n\n";
  OS << "static void __cs_pthread_key_create(unsigned *Key, void (*Destructor)(void *))\n{\n";
  OS << "  if (__cs_current_key < 1) {\n";
  OS << "    *Key = __cs_current_key;\n";
  OS << "    __cs_key_destructor[__cs_current_key++] = Destructor;\n";
  OS << "  }\n}\n\n";
  OS << "static void __cs_pthread_setspecific(unsigned Key, void *Value)\n{\n";
  OS << "  if (Key < 1)\n    __cs_key_values[Key][__cs_pthread_self()] = Value;\n}\n\n";
  OS << "static void *__cs_pthread_getspecific(unsigned Key)\n{\n";
  OS << "  return Key < 1 ? __cs_key_values[Key][__cs_pthread_self()] : 0;\n}\n\n";
  OS << "static void __cs_pthread_exit(void)\n{\n";
  OS << "  unsigned Key;\n";
  OS << "  for (Key = 0; Key < 1; ++Key)\n";
  OS << "    if (__cs_key_destructor[Key] && __cs_key_values[Key][__cs_pthread_self()])\n";
  OS << "      __cs_key_destructor[Key](__cs_key_values[Key][__cs_pthread_self()]);\n}\n\n";
  OS << "static void __cs_pthread_mutex_lock(void *Address)\n{\n";
  OS << "  unsigned Slot = __cs_lock_slot(Address);\n";
  OS << "  __VERIFIER_assume(__cs_lock_owner[Slot] == 0);\n";
  OS << "  __cs_lock_owner[Slot] = __cs_thread_index + 1;\n}\n\n";
  OS << "static void __cs_pthread_mutex_unlock(void *Address)\n{\n";
  OS << "  unsigned Slot = __cs_lock_slot(Address);\n";
  OS << "  __VERIFIER_assume(__cs_lock_owner[Slot] == __cs_thread_index + 1);\n";
  OS << "  __cs_lock_owner[Slot] = 0;\n}\n\n";
  OS << "static void __cs_pthread_mutex_destroy(void *Address)\n{\n";
  OS << "  __cs_lock_owner[__cs_lock_slot(Address)] = 0;\n}\n\n";
  OS << "static void __cs_pthread_join(unsigned ThreadId)\n{\n";
  OS << "  __VERIFIER_assume(__cs_pc[ThreadId] == __cs_thread_lines[ThreadId]);\n}\n\n";
  return OS.str();
}

Expected<std::vector<std::vector<bool>>>
buildRoundSelections(StringRef Schedule, unsigned Rounds, unsigned ThreadCount) {
  std::vector<std::string> RoundSpecs;
  if (!Schedule.trim().empty()) {
    SmallVector<StringRef, 8> Parts;
    Schedule.split(Parts, ':', -1, false);
    for (StringRef Part : Parts) {
      Part = Part.trim();
      if (!Part.empty())
        RoundSpecs.push_back(Part.str());
    }
  }

  const unsigned EffectiveRounds = std::max<unsigned>(Rounds, RoundSpecs.size());
  std::vector<std::vector<bool>> Selections(
      EffectiveRounds, std::vector<bool>(ThreadCount, RoundSpecs.empty()));
  for (unsigned Round = 0; Round < EffectiveRounds; ++Round) {
    if (Round >= RoundSpecs.size()) {
      std::fill(Selections[Round].begin(), Selections[Round].end(), true);
      continue;
    }
    SmallVector<StringRef, 8> Entries;
    StringRef(RoundSpecs[Round]).split(Entries, ',', -1, false);
    for (StringRef Entry : Entries) {
      Entry = Entry.trim();
      if (Entry.empty())
        continue;
      if (Entry == "+") {
        std::fill(Selections[Round].begin(), Selections[Round].end(), true);
        break;
      }
      unsigned Index = 0;
      if (Entry.getAsInteger(10, Index) || Index >= ThreadCount)
        return createStringError(inconvertibleErrorCode(),
                                 "lazyseq --schedule 包含无效线程编号: " +
                                     Entry.str());
      Selections[Round][Index] = true;
    }
  }
  // Python lazyseq always lets main enter the first context to create threads.
  if (!Selections.empty())
    Selections.front()[0] = true;
  return Selections;
}

std::string buildTimerPhaseInitialization(const std::vector<ThreadPlan> &Plans) {
  std::string Initialization;
  raw_string_ostream OS(Initialization);
  StringSet<> TimerFamilies;
  for (const ThreadPlan &Plan : Plans) {
    if (Plan.Kind != "timer" || Plan.Constraint == 0)
      continue;
    const std::string Family = baseThreadName(Plan.Name).str();
    if (!TimerFamilies.insert(Family).second)
      continue;
    OS << "  __cs_timer_counter[" << Plan.Index << "] = nondet_int();\n";
    OS << "  __VERIFIER_assume(__cs_timer_counter[" << Plan.Index
       << "] >= 0 && __cs_timer_counter[" << Plan.Index << "] <= "
       << 2 * Plan.Constraint << ");\n";
  }
  return OS.str();
}

std::string buildScheduler(const std::vector<ThreadPlan> &Plans,
                           const std::vector<std::vector<bool>> &Selections,
                           bool IsMultiThreaded) {
  std::string Scheduler;
  raw_string_ostream OS(Scheduler);
  const unsigned Rounds = static_cast<unsigned>(Selections.size());

  OS << "\nint main(void)\n{\n";
  OS << buildTimerPhaseInitialization(Plans);
  for (unsigned Round = 0; Round < Rounds; ++Round) {
    OS << "  /* lazyseq round " << Round << " */\n";
    for (const ThreadPlan &Plan : Plans) {
      if (!Selections[Round][Plan.Index])
        continue;
      const std::string Temp =
          formatv("__cs_tmp_t{0}_r{1}", Plan.Index, Round).str();
      const std::string InputGate =
          buildPythonInputGate(Plan, Plans, IsMultiThreaded, Round == 0);
      const std::string TimeGate =
          buildRandomTimeGate(Plan, Plans, IsMultiThreaded, Round);
      const bool FirstActivationOnly =
          isHighestPriorityInterrupt(Plan, Plans, IsMultiThreaded);
      OS << "  {\n";
      OS << "    unsigned " << Temp << ";\n";
      if (!Plan.IsMainThread) {
        OS << "    if (!__cs_active_thread[" << Plan.Index
           << "] || __cs_disable_thread[" << Plan.Index << "] == 1";
        if (!InputGate.empty())
          OS << " || !(" << InputGate << ")";
        if (!TimeGate.empty())
          OS << " || !(" << TimeGate << ")";
        if (FirstActivationOnly)
          OS << " || __cs_pc[" << Plan.Index << "] != 0";
        OS << ")\n      goto " << Temp << "_done;\n";
      }
      OS << "    __cs_thread_index = " << Plan.Index << ";\n";
      OS << "    __cs_pc_cs[" << Plan.Index << "] = " << Temp << ";\n";
      if (Round == 0 && Plan.IsMainThread)
        OS << "    __VERIFIER_assume(__cs_pc_cs[" << Plan.Index
           << "] > 0);\n";
      else
        OS << "    __VERIFIER_assume(__cs_pc_cs[" << Plan.Index
           << "] >= __cs_pc[" << Plan.Index << "]);\n";
      OS << "    __VERIFIER_assume(__cs_pc_cs[" << Plan.Index << "] <= "
         << Plan.StatementCount << ");\n";
      if (Plan.IsMainThread)
        OS << "    main_thread(" << buildDefaultArguments(Plan.Function)
           << ");\n";
      else
        OS << "    " << Plan.Name << "(__cs_threadargs[" << Plan.Index
           << "]);\n";
      OS << "    __cs_pc[" << Plan.Index << "] = __cs_pc_cs["
         << Plan.Index << "];\n";
      if (!Plan.IsMainThread)
        OS << "  " << Temp << "_done:\n";
      OS << "    ;\n  }\n";
    }
    if (hasRandomConstraint(Plans))
      OS << "  __cs_counter_before = __cs_counter;\n";
  }
  // Python lazyseq schedules main once more after the worker rounds. This is
  // where joins and the tail of a partially executed main_thread can finish.
  const ThreadPlan &MainPlan = Plans.front();
  const std::string FinalMainTemp =
      formatv("__cs_tmp_t0_r{0}", Rounds).str();
  OS << "  /* lazyseq final main context */\n";
  OS << "  {\n";
  OS << "    unsigned " << FinalMainTemp << ";\n";
  OS << "    if (__cs_active_thread[0] && __cs_disable_thread[0] != 1) {\n";
  OS << "      __cs_thread_index = 0;\n";
  OS << "      __cs_pc_cs[0] = " << FinalMainTemp << ";\n";
  OS << "      __VERIFIER_assume(__cs_pc_cs[0] >= __cs_pc[0]);\n";
  OS << "      __VERIFIER_assume(__cs_pc_cs[0] <= "
     << MainPlan.StatementCount << ");\n";
  OS << "      main_thread(" << buildDefaultArguments(MainPlan.Function)
     << ");\n";
  OS << "      __cs_pc[0] = __cs_pc_cs[0];\n";
  OS << "    }\n";
  OS << "  }\n";
  OS << "  return 0;\n}\n";
  return OS.str();
}

std::string buildNoRoundRobinScheduler(const std::vector<ThreadPlan> &Plans,
                                       unsigned Rounds, bool IsMultiThreaded) {
  std::string Scheduler;
  raw_string_ostream OS(Scheduler);
  const ThreadPlan &MainPlan = Plans.front();

  auto emitCall = [&](const ThreadPlan &Plan) {
    if (Plan.IsMainThread)
      OS << "      main_thread(" << buildDefaultArguments(Plan.Function)
         << ");\n";
    else
      OS << "      " << Plan.Name << "(__cs_threadargs[" << Plan.Index
         << "]);\n";
  };
  auto emitWorker = [&](const ThreadPlan &Plan, unsigned Round,
                        bool IsFirstRound) {
    const std::string Temp =
        formatv("__cs_tmp_t{0}_r{1}", Plan.Index, Round).str();
    const std::string Run =
        formatv("__cs_run_t{0}_r{1}", Plan.Index, Round).str();
    const std::string InputGate =
        buildPythonInputGate(Plan, Plans, IsMultiThreaded, IsFirstRound);
    const std::string TimeGate = buildRandomTimeGate(
        Plan, Plans, IsMultiThreaded, IsFirstRound ? 0 : Round);
    const bool FirstActivationOnly =
        isHighestPriorityInterrupt(Plan, Plans, IsMultiThreaded);
    OS << "  {\n";
    if (!IsFirstRound)
      OS << "    __VERIFIER_assume(__cs_last_thread != " << Plan.Index
         << ");\n";
    OS << "    unsigned " << Temp << ";\n";
    OS << "    unsigned " << Run << " = (" << Temp
       << " && (__cs_active_thread[" << Plan.Index << "] == 1)"
       << " && (__cs_disable_thread[" << Plan.Index << "] != 1))";
    if (!InputGate.empty())
      OS << " && (" << InputGate << ")";
    if (!TimeGate.empty())
      OS << " && (" << TimeGate << ")";
    if (FirstActivationOnly)
      OS << " && (__cs_pc[" << Plan.Index << "] == 0)";
    OS << ";\n";
    OS << "    if (" << Run << ") {\n";
    OS << "      __cs_thread_index = " << Plan.Index << ";\n";
    if (IsFirstRound)
      OS << "      __cs_pc_cs[" << Plan.Index << "] = " << Temp << ";\n";
    else
      OS << "      __cs_pc_cs[" << Plan.Index << "] = __cs_pc["
         << Plan.Index << "] + " << Temp << ";\n";
    if (!IsFirstRound)
      OS << "      __VERIFIER_assume(__cs_pc_cs[" << Plan.Index
         << "] >= __cs_pc[" << Plan.Index << "]);\n";
    OS << "      __VERIFIER_assume(__cs_pc_cs[" << Plan.Index << "] <= "
       << Plan.StatementCount << ");\n";
    emitCall(Plan);
    OS << "      __cs_last_thread = " << Plan.Index << ";\n";
    OS << "      __cs_pc[" << Plan.Index << "] = __cs_pc_cs["
       << Plan.Index << "];\n";
    OS << "    }\n  }\n";
  };

  OS << "\nint main(void)\n{\n";
  OS << buildTimerPhaseInitialization(Plans);
  for (unsigned Round = 0; Round < Rounds; ++Round) {
    OS << "  /* lazyseq no-round-robin round " << Round << " */\n";
    const std::string MainTemp = formatv("__cs_tmp_t0_r{0}", Round).str();
    const std::string MainRun = formatv("__cs_run_t0_r{0}", Round).str();
    OS << "  {\n";
    if (Round == 0) {
      OS << "    unsigned " << MainTemp << ";\n";
      OS << "    __VERIFIER_assume(" << MainTemp << " > 0);\n";
      OS << "    __cs_thread_index = 0;\n";
      OS << "    __cs_pc_cs[0] = " << MainTemp << ";\n";
      OS << "    __VERIFIER_assume(__cs_pc_cs[0] <= "
         << MainPlan.StatementCount << ");\n";
      emitCall(MainPlan);
      OS << "    __cs_last_thread = 0;\n";
      OS << "    __cs_pc[0] = __cs_pc_cs[0];\n";
    } else {
      OS << "    __VERIFIER_assume(__cs_last_thread != 0);\n";
      OS << "    unsigned " << MainTemp << ";\n";
      OS << "    unsigned " << MainRun << " = (" << MainTemp
         << " && (__cs_active_thread[0] == 1));\n";
      OS << "    if (" << MainRun << ") {\n";
      OS << "      __cs_thread_index = 0;\n";
      OS << "      __cs_pc_cs[0] = __cs_pc[0] + " << MainTemp << ";\n";
      OS << "      __VERIFIER_assume(__cs_pc_cs[0] >= __cs_pc[0]);\n";
      OS << "      __VERIFIER_assume(__cs_pc_cs[0] <= "
         << MainPlan.StatementCount << ");\n";
      emitCall(MainPlan);
      OS << "      __cs_last_thread = 0;\n";
      OS << "      __cs_pc[0] = __cs_pc_cs[0];\n";
      OS << "    }\n";
    }
    OS << "  }\n";
    for (const ThreadPlan &Plan : Plans)
      if (!Plan.IsMainThread)
        emitWorker(Plan, Round, Round == 0);
    if (hasRandomConstraint(Plans))
      OS << "  __cs_counter_before = __cs_counter;\n";
  }

  const std::string FinalTemp = formatv("__cs_tmp_t0_r{0}", Rounds).str();
  OS << "  /* lazyseq no-round-robin final main context */\n";
  OS << "  {\n    unsigned " << FinalTemp << ";\n";
  OS << "    if (__cs_active_thread[0] == 1 && __cs_disable_thread[0] != 1) {\n";
  OS << "      __cs_thread_index = 0;\n";
  OS << "      __cs_pc_cs[0] = __cs_pc[0] + " << FinalTemp << ";\n";
  OS << "      __VERIFIER_assume(__cs_pc_cs[0] >= __cs_pc[0]);\n";
  OS << "      __VERIFIER_assume(__cs_pc_cs[0] <= "
     << MainPlan.StatementCount << ");\n";
  emitCall(MainPlan);
  OS << "    }\n  }\n";
  OS << "  return 0;\n}\n";
  return OS.str();
}

std::string buildContextBoundedScheduler(const std::vector<ThreadPlan> &Plans,
                                         unsigned Contexts,
                                         bool IsMultiThreaded) {
  std::string Scheduler;
  raw_string_ostream OS(Scheduler);
  const ThreadPlan &MainPlan = Plans.front();

  auto emitCall = [&](const ThreadPlan &Plan, StringRef Indent) {
    if (Plan.IsMainThread)
      OS << Indent << "main_thread(" << buildDefaultArguments(Plan.Function)
         << ");\n";
    else
      OS << Indent << Plan.Name << "(__cs_threadargs[" << Plan.Index
         << "]);\n";
  };
  auto emitContextCall = [&](const ThreadPlan &Plan, unsigned Context,
                             StringRef Indent) {
    const std::string InputGate =
        buildPythonInputGate(Plan, Plans, IsMultiThreaded);
    const bool FirstActivationOnly =
        isHighestPriorityInterrupt(Plan, Plans, IsMultiThreaded);
    OS << Indent << "__cs_thread_index = " << Plan.Index << ";\n";
    OS << Indent << "__VERIFIER_assume(__cs_active_thread[" << Plan.Index
       << "]);\n";
    OS << Indent << "__VERIFIER_assume(__cs_disable_thread[" << Plan.Index
       << "] != 1);\n";
    if (!InputGate.empty())
      OS << Indent << "__VERIFIER_assume(" << InputGate << ");\n";
    if (FirstActivationOnly)
      OS << Indent << "__VERIFIER_assume(__cs_pc[" << Plan.Index
         << "] == 0);\n";
    OS << Indent << "__VERIFIER_assume(__cs_cs[" << Context
       << "] >= __cs_pc_cs[" << Plan.Index << "]);\n";
    OS << Indent << "__VERIFIER_assume(__cs_cs[" << Context << "] <= "
       << Plan.StatementCount << ");\n";
    OS << Indent << "__cs_pc_cs[" << Plan.Index << "] = __cs_cs["
       << Context << "];\n";
    emitCall(Plan, Indent);
    OS << Indent << "__cs_pc[" << Plan.Index << "] = __cs_pc_cs["
       << Plan.Index << "];\n";
  };

  OS << "\nint main(void)\n{\n";
  OS << buildTimerPhaseInitialization(Plans);
  OS << "  unsigned __cs_tid[" << Contexts << "];\n";
  OS << "  unsigned __cs_cs[" << Contexts << "];\n";
  OS << "  unsigned __cs_context;\n";
  OS << "  __cs_tid[0] = 0;\n";
  for (unsigned Context = 0; Context < Contexts; ++Context) {
    OS << "  /* lazyseq context " << Context << " */\n";
    OS << "  __cs_context = " << Context << ";\n";
    if (Context == 0) {
      OS << "  __cs_thread_index = 0;\n";
      OS << "  __VERIFIER_assume(__cs_cs[0] >= __cs_pc_cs[0]);\n";
      OS << "  __VERIFIER_assume(__cs_cs[0] <= " << MainPlan.StatementCount
         << ");\n";
      OS << "  __cs_pc_cs[0] = __cs_cs[0];\n";
      emitCall(MainPlan, "  ");
      OS << "  __cs_pc[0] = __cs_pc_cs[0];\n";
      continue;
    }
    for (const ThreadPlan &Plan : Plans) {
      OS << "  if (__cs_tid[" << Context << "] == " << Plan.Index
         << ") {\n";
      emitContextCall(Plan, Context, "    ");
      OS << "  }\n";
    }
  }
  OS << "  return 0;\n}\n";
  return OS.str();
}

class ThreadEntryCollector : public RecursiveASTVisitor<ThreadEntryCollector> {
public:
  explicit ThreadEntryCollector(ASTContext &AST)
      : AST(AST), SM(AST.getSourceManager()) {}

  bool VisitCallExpr(CallExpr *Call) {
    const FunctionDecl *Callee = Call->getDirectCallee();
    if (!Callee || Callee->getName() != "pthread_create" ||
        !isMainFileLocation(Call->getBeginLoc(), SM))
      return true;
    if (const FunctionDecl *Entry = functionFromThreadStartArgument(Call))
      Entries.push_back(Entry);
    return true;
  }

  const std::vector<const FunctionDecl *> &entries() const { return Entries; }

private:
  ASTContext &AST;
  SourceManager &SM;
  std::vector<const FunctionDecl *> Entries;
};

class LazyPlanCollector {
public:
  LazyPlanCollector(ASTContext &AST, const ProgramSummary &Summary)
      : AST(AST), Summary(Summary), SM(AST.getSourceManager()) {}

  Expected<std::vector<ThreadPlan>> collect() const {
    StringMap<const FunctionDecl *> Definitions;
    for (Decl *DeclNode : AST.getTranslationUnitDecl()->decls()) {
      const auto *FD = dyn_cast<FunctionDecl>(DeclNode);
      if (!FD || !FD->hasBody() || !isMainFileLocation(FD->getLocation(), SM) ||
          FD->getName().empty())
        continue;
      Definitions[FD->getName()] = FD;
    }

    const FunctionDecl *Main = Summary.MainFunction;
    if (!Main || !Main->hasBody() || !isMainFileLocation(Main->getLocation(), SM)) {
      auto It = Definitions.find("main");
      Main = It == Definitions.end() ? nullptr : It->second;
    }
    if (!Main)
      return createStringError(inconvertibleErrorCode(),
                               "lazyseq 未找到 main 函数定义");

    std::vector<ThreadPlan> Plans;
    Plans.push_back(ThreadPlan{Main, Main->getNameAsString(), 0, 0, true});
    ThreadEntryCollector EntryCollector(AST);
    EntryCollector.TraverseDecl(AST.getTranslationUnitDecl());
    StringSet<> SeenEntries;
    for (const FunctionDecl *Entry : EntryCollector.entries()) {
      const FunctionDecl *Definition = Entry ? Entry->getDefinition() : nullptr;
      if (!Definition || Definition == Main ||
          !isMainFileLocation(Definition->getLocation(), SM))
        continue;
      const std::string Name = Definition->getNameAsString();
      // Duplicator makes every static create entry distinct. Keep the old
      // de-duplication fallback when lazyseq is invoked without that pass.
      if (!SeenEntries.insert(Name).second)
        continue;
      const InterruptInfo *Info = findInterruptInfo(Name, Summary);
      Plans.push_back(ThreadPlan{
          Definition, Name, static_cast<unsigned>(Plans.size()), 0, false,
          Info != nullptr, Info ? Info->Priority : 0,
          Info ? Info->Kind : "random", Info ? Info->Period : 0,
          Info ? Info->Event : "", Info ? Info->Constraint : 0});
    }
    if (Plans.size() == 1) {
      return createStringError(inconvertibleErrorCode(),
                               "lazyseq 未找到 pthread_create 对应的线程函数");
    }
    return Plans;
  }

private:
  ASTContext &AST;
  const ProgramSummary &Summary;
  SourceManager &SM;
};

std::string rewriteCreateCall(const CallExpr *Call, unsigned ThreadIndex,
                              const PipelineContext &Context) {
  std::optional<std::string> ThreadId = sourceText(Call->getArg(0), Context);
  std::optional<std::string> Attributes =
      Call->getNumArgs() >= 2 ? sourceText(Call->getArg(1), Context)
                              : std::optional<std::string>("0");
  std::optional<std::string> StartRoutine =
      Call->getNumArgs() >= 3 ? sourceText(Call->getArg(2), Context)
                              : std::optional<std::string>("0");
  std::optional<std::string> ThreadArg =
      Call->getNumArgs() >= 4 ? sourceText(Call->getArg(3), Context)
                              : std::optional<std::string>("0");
  if (!ThreadId || !Attributes || !StartRoutine || !ThreadArg)
    return "";
  return formatv("__CS_PTHREAD_CREATE({0}, {1}, {2}, {3}, {4});", *ThreadId,
                 *Attributes, *StartRoutine, *ThreadArg, ThreadIndex)
      .str();
}

std::string rewriteJoinCall(const CallExpr *Call, const PipelineContext &Context) {
  if (Call->getNumArgs() < 1)
    return "";
  std::optional<std::string> ThreadId = sourceText(Call->getArg(0), Context);
  if (!ThreadId)
    return "";
  return formatv("__cs_pthread_join((unsigned)({0}));", *ThreadId)
      .str();
}

std::optional<std::string>
rewriteISRMaskCall(const CallExpr *Call, StringRef CalleeName,
                   const StringMap<unsigned> &ThreadIndices,
                   unsigned CurrentThreadIndex, const PipelineContext &Context) {
  if (Call->getNumArgs() != 1)
    return std::nullopt;
  std::optional<std::string> Argument = sourceText(Call->getArg(0), Context);
  if (!Argument)
    return std::nullopt;
  std::string CompactArgument;
  for (char Character : *Argument)
    if (!std::isspace(static_cast<unsigned char>(Character)))
      CompactArgument += Character;
  if (CompactArgument.empty())
    return std::nullopt;
  const bool AllThreads = CompactArgument == "-1";
  const unsigned Value = CalleeName == "disable_isr" ? 1 : 0;
  std::string Replacement;
  raw_string_ostream OS(Replacement);
  bool Matched = false;
  for (const auto &Entry : ThreadIndices) {
    StringRef Name = Entry.getKey();
    const unsigned Index = Entry.getValue();
    if ((AllThreads && Index != CurrentThreadIndex) ||
        (!AllThreads && Name.size() >= 3 &&
         Name[Name.size() - 3] == CompactArgument.front())) {
      OS << "__cs_disable_thread[" << Index << "] = " << Value << "; ";
      Matched = true;
    }
  }
  // Python also updates main_thread for disable_isr(-1), including when it
  // is the caller itself.
  if (AllThreads) {
    OS << "__cs_disable_thread[0] = " << Value << ";";
    Matched = true;
  }
  return Matched ? std::optional<std::string>(OS.str()) : std::nullopt;
}

std::optional<std::string>
rewriteNestedRuntimeCall(const CallExpr *Call,
                         const StringMap<unsigned> &ThreadIndices,
                         unsigned CurrentThreadIndex,
                         const PipelineContext &Context) {
  const std::string CalleeName = getCalleeName(Call, Context);
  if (CalleeName == "disable_isr" || CalleeName == "enable_isr")
    return rewriteISRMaskCall(Call, CalleeName, ThreadIndices,
                              CurrentThreadIndex, Context);
  if (CalleeName == "pthread_create") {
    const FunctionDecl *Entry = functionFromThreadStartArgument(Call);
    auto It = Entry ? ThreadIndices.find(Entry->getName())
                    : ThreadIndices.end();
    if (It == ThreadIndices.end())
      return std::nullopt;
    std::string Replacement = rewriteCreateCall(Call, It->second, Context);
    if (StringRef(Replacement).ends_with(";"))
      Replacement.pop_back();
    return Replacement;
  }
  if (CalleeName == "pthread_join")
    return rewriteJoinCall(Call, Context);

  auto addressArgument = [&](StringRef RuntimeName) -> std::optional<std::string> {
    if (Call->getNumArgs() < 1)
      return std::nullopt;
    std::optional<std::string> Address = sourceText(Call->getArg(0), Context);
    return Address ? std::optional<std::string>(RuntimeName.str() + "((void *)" +
                                                *Address + ")")
                   : std::nullopt;
  };
  if (CalleeName == "pthread_mutex_init")
    return addressArgument("__cs_pthread_mutex_init");
  if (CalleeName == "pthread_mutex_lock")
    return addressArgument("__cs_pthread_mutex_lock");
  if (CalleeName == "pthread_mutex_unlock")
    return addressArgument("__cs_pthread_mutex_unlock");
  if (CalleeName == "pthread_mutex_destroy")
    return addressArgument("__cs_pthread_mutex_destroy");
  if (CalleeName == "pthread_cond_init")
    return addressArgument("__cs_pthread_cond_init");
  if (CalleeName == "pthread_cond_destroy")
    return addressArgument("__cs_pthread_cond_destroy");
  if (CalleeName == "pthread_cond_signal")
    return addressArgument("__cs_pthread_cond_signal");
  if (CalleeName == "pthread_cond_broadcast")
    return addressArgument("__cs_pthread_cond_broadcast");
  if (CalleeName == "pthread_barrier_destroy")
    return addressArgument("__cs_pthread_barrier_destroy");
  if (CalleeName == "pthread_barrier_wait_1")
    return addressArgument("__cs_pthread_barrier_wait_1");
  if (CalleeName == "pthread_barrier_wait_2")
    return addressArgument("__cs_pthread_barrier_wait_2");
  if (CalleeName == "pthread_cond_wait_1" ||
      CalleeName == "pthread_cond_wait_2") {
    if (Call->getNumArgs() < 2)
      return std::nullopt;
    std::optional<std::string> Condition = sourceText(Call->getArg(0), Context);
    std::optional<std::string> Mutex = sourceText(Call->getArg(1), Context);
    if (!Condition || !Mutex)
      return std::nullopt;
    return "__cs_" + CalleeName + "((void *)" + *Condition +
           ", (void *)" + *Mutex + ")";
  }
  if (CalleeName == "pthread_barrier_init") {
    if (Call->getNumArgs() < 3)
      return std::nullopt;
    std::optional<std::string> Address = sourceText(Call->getArg(0), Context);
    std::optional<std::string> Count = sourceText(Call->getArg(2), Context);
    if (!Address || !Count)
      return std::nullopt;
    return "__cs_pthread_barrier_init((void *)" + *Address +
           ", (unsigned)(" + *Count + "))";
  }
  if (CalleeName == "pthread_key_create") {
    if (Call->getNumArgs() < 2)
      return std::nullopt;
    std::optional<std::string> Key = sourceText(Call->getArg(0), Context);
    std::optional<std::string> Destructor = sourceText(Call->getArg(1), Context);
    if (!Key || !Destructor)
      return std::nullopt;
    return "__cs_pthread_key_create((unsigned *)" + *Key + ", " +
           *Destructor + ")";
  }
  if (CalleeName == "pthread_setspecific") {
    if (Call->getNumArgs() < 2)
      return std::nullopt;
    std::optional<std::string> Key = sourceText(Call->getArg(0), Context);
    std::optional<std::string> Value = sourceText(Call->getArg(1), Context);
    if (!Key || !Value)
      return std::nullopt;
    return "__cs_pthread_setspecific((unsigned)(" + *Key + "), (void *)" +
           *Value + ")";
  }
  return std::nullopt;
}

std::string generatedLabelName(const ThreadPlan &Plan, unsigned StatementIndex) {
  return formatv("__cs_label_{0}_{1}", Plan.Name, StatementIndex).str();
}

std::string rewriteThreadReturnValue(const Expr *Value,
                                     const PipelineContext &Context) {
  std::optional<std::string> Text = sourceText(Value, Context);
  if (!Text)
    return "0";
  const auto *Call = dyn_cast<CallExpr>(Value->IgnoreParenImpCasts());
  if (!Call)
    return *Text;
  const std::string CalleeName = getCalleeName(Call, Context);
  if (CalleeName == "pthread_self")
    return "__cs_pthread_self()";
  if (CalleeName == "pthread_getspecific" && Call->getNumArgs() >= 1) {
    if (std::optional<std::string> Key = sourceText(Call->getArg(0), Context))
      return "__cs_pthread_getspecific((unsigned)(" + *Key + "))";
  }
  return *Text;
}

std::string buildDeferredTimerActivation(const ThreadPlan &Plan,
                                         const std::vector<ThreadPlan> &Plans,
                                         unsigned StatementIndex) {
  std::string Activation;
  raw_string_ostream OS(Activation);
  const StringRef YieldReturn = Plan.Function->getReturnType()->isVoidType()
                                    ? "return;"
                                    : "return 0;";
  StringSet<> TimerFamilies;
  for (const ThreadPlan &Candidate : Plans) {
    if (Candidate.Kind != "timer" || Candidate.Period == 0)
      continue;
    const std::string Family = baseThreadName(Candidate.Name).str();
    if (!TimerFamilies.insert(Family).second)
      continue;
    const unsigned TriggerPeriod = Candidate.Period + Candidate.Constraint;
    OS << "\n__cs_timer_counter[" << Candidate.Index << "]++;\n";
    OS << "if (__cs_timer_counter[" << Candidate.Index << "] == "
       << TriggerPeriod << ") {\n";
    if (Candidate.Constraint == 0)
      OS << "  __cs_timer_counter[" << Candidate.Index << "] = 0;\n";
    else {
      OS << "  __cs_timer_counter[" << Candidate.Index
         << "] = nondet_int();\n";
      OS << "  __VERIFIER_assume(__cs_timer_counter[" << Candidate.Index
         << "] >= 0 && __cs_timer_counter[" << Candidate.Index << "] <= "
         << 2 * Candidate.Constraint << ");\n";
    }
    for (const ThreadPlan &Instance : Plans) {
      if (Instance.Kind != "timer" || baseThreadName(Instance.Name) != Family)
        continue;
      OS << "  if (!__cs_active_thread[" << Instance.Index << "]) {\n";
      OS << "    __cs_active_thread[" << Instance.Index << "] = 1;\n";
      OS << "    __cs_pc[" << Instance.Index << "] = 0;\n";
      OS << "    __cs_pc_cs[" << Instance.Index << "] = 0;\n";
      OS << "    __cs_threadargs[" << Instance.Index << "] = 0;\n";
      OS << "    __cs_pc_cs[" << Plan.Index << "] = " << StatementIndex + 1
         << "; " << YieldReturn << "\n  }\n";
    }
    OS << "}\n";
  }
  return OS.str();
}

class ThreadExitCollector
    : public RecursiveASTVisitor<ThreadExitCollector> {
public:
  ThreadExitCollector(const PipelineContext &Context, unsigned BaseOffset)
      : Context(Context), BaseOffset(BaseOffset) {}

  bool VisitReturnStmt(ReturnStmt *Return) {
    std::optional<unsigned> Offset =
        getFileOffset(Return->getBeginLoc(), Context.getSourceManager());
    std::optional<std::string> Original = sourceText(Return, Context);
    if (!Offset || !Original || *Offset < BaseOffset)
      return true;

    unsigned Length = static_cast<unsigned>(Original->size());
    // clang's ReturnStmt token range ends before the statement semicolon.
    if (*Offset + Length < Context.CurrentSource.size() &&
        Context.CurrentSource[*Offset + Length] == ';')
      ++Length;

    std::string Replacement = "__cs_pthread_exit(); return";
    if (const Expr *Value = Return->getRetValue())
      Replacement += " " + rewriteThreadReturnValue(Value, Context);
    Replacement += ";";
    Replacements.push_back(TextReplacement{
        *Offset - BaseOffset, Length, std::move(Replacement)});
    return true;
  }

  std::vector<TextReplacement> takeReplacements() {
    return std::move(Replacements);
  }

private:
  const PipelineContext &Context;
  unsigned BaseOffset;
  std::vector<TextReplacement> Replacements;
};

class NestedRuntimeCallCollector
    : public RecursiveASTVisitor<NestedRuntimeCallCollector> {
public:
  NestedRuntimeCallCollector(const PipelineContext &Context, unsigned BaseOffset,
                             const StringMap<unsigned> &ThreadIndices,
                             unsigned CurrentThreadIndex)
      : Context(Context), BaseOffset(BaseOffset), ThreadIndices(ThreadIndices),
        CurrentThreadIndex(CurrentThreadIndex) {}

  bool VisitCallExpr(CallExpr *Call) {
    std::optional<std::string> Replacement =
        rewriteNestedRuntimeCall(Call, ThreadIndices, CurrentThreadIndex, Context);
    if (!Replacement)
      return true;
    std::optional<unsigned> Offset =
        getFileOffset(Call->getBeginLoc(), Context.getSourceManager());
    std::optional<std::string> Original = sourceText(Call, Context);
    if (!Offset || !Original || *Offset < BaseOffset)
      return true;
    Replacements.push_back(TextReplacement{
        *Offset - BaseOffset, static_cast<unsigned>(Original->size()),
        std::move(*Replacement)});
    return true;
  }

  std::vector<TextReplacement> takeReplacements() {
    return std::move(Replacements);
  }

private:
  const PipelineContext &Context;
  unsigned BaseOffset;
  const StringMap<unsigned> &ThreadIndices;
  unsigned CurrentThreadIndex;
  std::vector<TextReplacement> Replacements;
};

class NestedTimerActivationCollector
    : public RecursiveASTVisitor<NestedTimerActivationCollector> {
public:
  NestedTimerActivationCollector(const PipelineContext &Context,
                                 unsigned BaseOffset, const ThreadPlan &Plan,
                                 const std::vector<ThreadPlan> &Plans,
                                 unsigned StatementIndex)
      : Context(Context), BaseOffset(BaseOffset), Plan(Plan), Plans(Plans),
        StatementIndex(StatementIndex) {}

  bool VisitBinaryOperator(BinaryOperator *Assignment) {
    if (!Assignment->isAssignmentOp())
      return true;
    std::optional<unsigned> EndOffset =
        getFileOffset(Assignment->getEndLoc(), Context.getSourceManager());
    std::optional<unsigned> TokenLength = getTokenLength(
        Assignment->getEndLoc(), Context.getSourceManager(), Context.getLangOpts());
    if (!EndOffset || !TokenLength || *EndOffset < BaseOffset)
      return true;
    unsigned InsertOffset = *EndOffset + *TokenLength;
    while (InsertOffset < Context.CurrentSource.size() &&
           std::isspace(static_cast<unsigned char>(
               Context.CurrentSource[InsertOffset])))
      ++InsertOffset;
    if (InsertOffset >= Context.CurrentSource.size() ||
        Context.CurrentSource[InsertOffset] != ';')
      return true;
    ++InsertOffset;
    if (InsertOffset < BaseOffset)
      return true;

    std::string Activation;
    if (hasRandomConstraint(Plans))
      Activation += "\n__cs_counter++;";
    Activation += buildDeferredTimerActivation(Plan, Plans, StatementIndex);
    if (!Activation.empty())
      Replacements.push_back(TextReplacement{InsertOffset - BaseOffset, 0,
                                             std::move(Activation)});
    return true;
  }

  std::vector<TextReplacement> takeReplacements() {
    return std::move(Replacements);
  }

private:
  const PipelineContext &Context;
  unsigned BaseOffset;
  const ThreadPlan &Plan;
  const std::vector<ThreadPlan> &Plans;
  unsigned StatementIndex;
  std::vector<TextReplacement> Replacements;
};

std::string rewriteNestedLazyContent(const Stmt *Statement, StringRef Original,
                                     bool RewriteThreadReturns,
                                     const StringMap<unsigned> &ThreadIndices,
                                     unsigned CurrentThreadIndex,
                                     const ThreadPlan *Plan,
                                     const std::vector<ThreadPlan> *Plans,
                                     unsigned StatementIndex,
                                     const PipelineContext &Context) {
  std::optional<unsigned> BaseOffset =
      getFileOffset(Statement->getBeginLoc(), Context.getSourceManager());
  if (!BaseOffset)
    return Original.str();

  std::vector<TextReplacement> Replacements;
  if (RewriteThreadReturns) {
    ThreadExitCollector ExitCollector(Context, *BaseOffset);
    ExitCollector.TraverseStmt(const_cast<Stmt *>(Statement));
    Replacements = ExitCollector.takeReplacements();
  }
  NestedRuntimeCallCollector RuntimeCollector(Context, *BaseOffset,
                                              ThreadIndices, CurrentThreadIndex);
  RuntimeCollector.TraverseStmt(const_cast<Stmt *>(Statement));
  std::vector<TextReplacement> RuntimeReplacements =
      RuntimeCollector.takeReplacements();
  Replacements.insert(Replacements.end(), RuntimeReplacements.begin(),
                      RuntimeReplacements.end());
  if (Plan && Plans && Plan->Name == "main_task_0" &&
      !isa<BinaryOperator>(Statement)) {
    NestedTimerActivationCollector TimerCollector(Context, *BaseOffset, *Plan,
                                                   *Plans, StatementIndex);
    TimerCollector.TraverseStmt(const_cast<Stmt *>(Statement));
    std::vector<TextReplacement> TimerReplacements =
        TimerCollector.takeReplacements();
    Replacements.insert(Replacements.end(), TimerReplacements.begin(),
                        TimerReplacements.end());
  }
  std::string Rewritten = Original.str();
  applyReplacements(Rewritten, std::move(Replacements));
  return Rewritten;
}

std::string rewriteFunctionBody(
    const ThreadPlan &Plan, const StringMap<unsigned> &ThreadIndices,
    const StringSet<> &DeferredThreadNames,
    const std::vector<ThreadPlan> &Plans,
    const PipelineContext &Context, unsigned &StatementCount) {
  const auto *Body = dyn_cast<CompoundStmt>(Plan.Function->getBody());
  if (!Body)
    return "";

  std::string HoistedDeclarations;
  for (const Stmt *Statement : Body->body()) {
    const auto *DeclStatement = dyn_cast<DeclStmt>(Statement);
    if (!DeclStatement)
      continue;
    for (const clang::Decl *DeclNode : DeclStatement->decls()) {
      const auto *Variable = dyn_cast<VarDecl>(DeclNode);
      if (!Variable || Variable->getName().empty())
        continue;
      HoistedDeclarations +=
          "  static " + Variable->getType().getAsString() + " " +
          Variable->getNameAsString() + ";\n";
    }
  }

  std::string Output = "{\n" + HoistedDeclarations;
  std::vector<const Stmt *> Statements(Body->body_begin(), Body->body_end());
  std::vector<bool> VisibleStatements;
  VisibleStatements.reserve(Statements.size());
  for (const Stmt *Statement : Statements)
    VisibleStatements.push_back(isVisibleLazyStatement(Statement, Plan, Context));

  unsigned VisibleCount =
      static_cast<unsigned>(std::count(VisibleStatements.begin(),
                                       VisibleStatements.end(), true));
  // A thread without shared accesses is still a schedulable unit. Keep its
  // complete body in one segment rather than generating a zero-sized thread.
  if (VisibleCount == 0) {
    if (!VisibleStatements.empty())
      VisibleStatements.back() = true;
    VisibleCount = 1;
  }

  unsigned StatementIndex = 0;
  if (!Statements.empty())
    Output += formatv("  __CS_LAZY_IF({0}, 0, {1});\n", Plan.Index,
                      generatedLabelName(Plan, 1))
                  .str();
  else
    Output += generatedLabelName(Plan, 1) + ":\n  ;\n";

  for (size_t Position = 0; Position < Statements.size(); ++Position) {
    const Stmt *Statement = Statements[Position];
    std::optional<std::string> Text = sourceText(Statement, Context);
    if (!Text)
      continue;

    std::string Rewritten = rewriteNestedLazyContent(
        Statement, ensureStatementTerminator(*Text, Statement),
        !Plan.IsMainThread, ThreadIndices, Plan.Index, &Plan, &Plans,
        StatementIndex, Context);
    if (!Plan.IsMainThread) {
      if (const auto *Return = dyn_cast<ReturnStmt>(Statement)) {
        Rewritten = "__cs_pthread_exit();";
        if (const Expr *Value = Return->getRetValue())
          Rewritten += " return " + rewriteThreadReturnValue(Value, Context) + ";";
        else
          Rewritten += " return;";
      }
    }
    if (const auto *DeclStatement = dyn_cast<DeclStmt>(Statement)) {
      Rewritten.clear();
      for (const clang::Decl *DeclNode : DeclStatement->decls()) {
        const auto *Variable = dyn_cast<VarDecl>(DeclNode);
        if (!Variable || !Variable->hasInit())
          continue;
        std::optional<std::string> Initializer =
            sourceText(Variable->getInit(), Context);
        if (!Initializer)
          continue;
        if (!Rewritten.empty())
          Rewritten += "\n";
        Rewritten += Variable->getNameAsString() + " = " + *Initializer + ";";
      }
    }

    if (const CallExpr *Call = getDirectCall(Statement)) {
      const std::string CalleeName = getCalleeName(Call, Context);
      if (CalleeName == "pthread_create") {
        const FunctionDecl *Entry = functionFromThreadStartArgument(Call);
        auto It =
            Entry ? ThreadIndices.find(Entry->getName()) : ThreadIndices.end();
        if (It != ThreadIndices.end()) {
          if (DeferredThreadNames.contains(Entry->getName()))
            Rewritten = "__cs_active_thread[" + std::to_string(It->second) +
                        "] = 0;";
          else
            Rewritten = rewriteCreateCall(Call, It->second, Context);
        }
      } else if (CalleeName == "pthread_join") {
        Rewritten = rewriteJoinCall(Call, Context);
      } else if (CalleeName == "pthread_mutex_init") {
        if (Call->getNumArgs() >= 1) {
          if (std::optional<std::string> Address = sourceText(Call->getArg(0), Context))
            Rewritten = "__cs_pthread_mutex_init((void *)" + *Address + ");";
        }
      } else if (CalleeName == "pthread_mutex_lock") {
        if (Call->getNumArgs() >= 1) {
          if (std::optional<std::string> Address = sourceText(Call->getArg(0), Context))
            Rewritten = "__cs_pthread_mutex_lock((void *)" + *Address + ");";
        }
      } else if (CalleeName == "pthread_mutex_unlock") {
        if (Call->getNumArgs() >= 1) {
          if (std::optional<std::string> Address = sourceText(Call->getArg(0), Context))
            Rewritten = "__cs_pthread_mutex_unlock((void *)" + *Address + ");";
        }
      } else if (CalleeName == "pthread_mutex_destroy") {
        if (Call->getNumArgs() >= 1) {
          if (std::optional<std::string> Address = sourceText(Call->getArg(0), Context))
            Rewritten = "__cs_pthread_mutex_destroy((void *)" + *Address + ");";
        }
      } else if (CalleeName == "pthread_exit") {
        Rewritten = "__cs_pthread_exit(); return;";
      } else if (CalleeName == "pthread_cond_init" ||
                 CalleeName == "pthread_cond_destroy" ||
                 CalleeName == "pthread_cond_signal" ||
                 CalleeName == "pthread_cond_broadcast" ||
                 CalleeName == "pthread_barrier_destroy" ||
                 CalleeName == "pthread_barrier_wait_1" ||
                 CalleeName == "pthread_barrier_wait_2") {
        if (Call->getNumArgs() >= 1)
          if (std::optional<std::string> Address = sourceText(Call->getArg(0), Context)) {
            std::string RuntimeName = "__cs_" + CalleeName;
            Rewritten = RuntimeName + "((void *)" + *Address + ");";
          }
      } else if (CalleeName == "pthread_cond_wait_1" ||
                 CalleeName == "pthread_cond_wait_2") {
        if (Call->getNumArgs() >= 2) {
          std::optional<std::string> Condition = sourceText(Call->getArg(0), Context);
          std::optional<std::string> Mutex = sourceText(Call->getArg(1), Context);
          if (Condition && Mutex)
            Rewritten = "__cs_" + CalleeName + "((void *)" + *Condition +
                        ", (void *)" + *Mutex + ");";
        }
      } else if (CalleeName == "pthread_barrier_init") {
        if (Call->getNumArgs() >= 3) {
          std::optional<std::string> Address = sourceText(Call->getArg(0), Context);
          std::optional<std::string> Count = sourceText(Call->getArg(2), Context);
          if (Address && Count)
            Rewritten = "__cs_pthread_barrier_init((void *)" + *Address +
                        ", (unsigned)(" + *Count + "));";
        }
      } else if (CalleeName == "pthread_key_create") {
        if (Call->getNumArgs() >= 2) {
          std::optional<std::string> Key = sourceText(Call->getArg(0), Context);
          std::optional<std::string> Destructor = sourceText(Call->getArg(1), Context);
          if (Key && Destructor)
            Rewritten = "__cs_pthread_key_create((unsigned *)" + *Key + ", " +
                        *Destructor + ");";
        }
      } else if (CalleeName == "pthread_setspecific") {
        if (Call->getNumArgs() >= 2) {
          std::optional<std::string> Key = sourceText(Call->getArg(0), Context);
          std::optional<std::string> Value = sourceText(Call->getArg(1), Context);
          if (Key && Value)
            Rewritten = "__cs_pthread_setspecific((unsigned)(" + *Key +
                        "), (void *)" + *Value + ");";
        }
      } else if (CalleeName == "pthread_getspecific") {
        if (Call->getNumArgs() >= 1) {
          if (std::optional<std::string> Key = sourceText(Call->getArg(0), Context))
            Rewritten = "__cs_pthread_getspecific((unsigned)(" + *Key + "));";
        }
      } else if (CalleeName == "pthread_self") {
        Rewritten = "__cs_pthread_self();";
      }
    }
    if (Plan.Name == "main_task_0") {
      const StringRef YieldReturn = Plan.Function->getReturnType()->isVoidType()
                                        ? "return;"
                                        : "return 0;";
      const std::string StatementText = normalizeEventText(*Text);
      for (const ThreadPlan &Candidate : Plans) {
        if (Candidate.Kind != "event" || Candidate.Event.empty() ||
            StatementText != normalizeEventText(Candidate.Event))
          continue;
        Rewritten += "\nif (!__cs_active_thread[" +
                     std::to_string(Candidate.Index) + "]) {";
        Rewritten += "\n  __cs_active_thread[" +
                     std::to_string(Candidate.Index) + "] = 1;";
        Rewritten += "\n  __cs_pc[" + std::to_string(Candidate.Index) +
                     "] = 0;";
        Rewritten += "\n  __cs_pc_cs[" +
                     std::to_string(Candidate.Index) + "] = 0;";
        Rewritten += "\n  __cs_threadargs[" +
                     std::to_string(Candidate.Index) + "] = 0;";
        Rewritten += "\n  __cs_pc_cs[" + std::to_string(Plan.Index) +
                     "] = " + std::to_string(StatementIndex + 1) +
                     "; " + YieldReturn.str() + "\n}";
      }
      const auto *Assignment = dyn_cast<BinaryOperator>(Statement);
      if (Assignment && Assignment->isAssignmentOp()) {
        if (hasRandomConstraint(Plans))
          Rewritten += "\n__cs_counter++;";
        Rewritten += buildDeferredTimerActivation(Plan, Plans, StatementIndex);
      }
    }

    if (Rewritten.empty())
      continue;
    Output += indentLines(Rewritten, "  ");
    if (!VisibleStatements[Position]) {
      Output += "\n";
      continue;
    }

    ++StatementIndex;
    Output += "\n" + generatedLabelName(Plan, StatementIndex) + ":\n  ;\n";
    if (StatementIndex < VisibleCount)
      Output += formatv("  __CS_LAZY_IF({0}, {1}, {2});\n", Plan.Index,
                        StatementIndex,
                        generatedLabelName(Plan, StatementIndex + 1))
                    .str();
  }
  Output += "  __cs_pthread_exit();\n";
  if (Plan.Function->getReturnType()->isVoidType())
    Output += "  return;\n";
  else
    Output += "  return 0;\n";
  Output += "}\n";
  StatementCount = StatementIndex;
  return Output;
}

} // namespace

llvm::StringRef LazySequentializationPass::name() const { return "lazyseq"; }

llvm::Error LazySequentializationPass::run(const PipelineContext &Context,
                                           TransformResult &Result) const {
  if (!isLazySeqRequested(Context.Options))
    return Error::success();
  if (Result.Summary.Kind == ProgramKind::Sequential) {
    Result.Notes.push_back("phase5: lazyseq 跳过顺序输入");
    return Error::success();
  }

  LazyPlanCollector Collector(Context.getASTContext(), Result.Summary);
  Expected<std::vector<ThreadPlan>> PlansOrErr = Collector.collect();
  if (!PlansOrErr)
    return PlansOrErr.takeError();
  std::vector<ThreadPlan> Plans = std::move(*PlansOrErr);

  StringMap<unsigned> ThreadIndices;
  for (const ThreadPlan &Plan : Plans)
    ThreadIndices[Plan.Name] = Plan.Index;
  StringSet<> DeferredThreadNames;
  for (const ThreadPlan &Plan : Plans)
    if (isDeferredInterrupt(Plan))
      DeferredThreadNames.insert(Plan.Name);

  std::vector<TextReplacement> Replacements;
  for (ThreadPlan &Plan : Plans) {
    std::optional<unsigned> BodyOffset =
        getFileOffset(Plan.Function->getBody()->getBeginLoc(),
                      Context.getSourceManager());
    std::optional<std::string> BodyText = sourceText(Plan.Function->getBody(), Context);
    if (!BodyOffset || !BodyText)
      continue;

    unsigned StatementCount = 0;
    std::string RewrittenBody =
        rewriteFunctionBody(Plan, ThreadIndices, DeferredThreadNames, Plans,
                            Context, StatementCount);
    if (RewrittenBody.empty())
      continue;
    Plan.StatementCount = StatementCount;
    Replacements.push_back(TextReplacement{*BodyOffset,
                                           static_cast<unsigned>(BodyText->size()),
                                           std::move(RewrittenBody)});
  }

  if (Replacements.size() != Plans.size()) {
    return createStringError(inconvertibleErrorCode(),
                             "lazyseq 无法读取所有目标函数的函数体");
  }
  if (std::optional<TextReplacement> Rename =
          renameFunction(Plans.front().Function, "main_thread", Context))
    Replacements.push_back(*Rename);

  std::string Source = materializeSource(Context, Result);
  applyReplacements(Source, Replacements);
  Expected<std::vector<std::vector<bool>>> Selections = buildRoundSelections(
      Context.Options.Schedule, Context.Options.Rounds,
      static_cast<unsigned>(Plans.size()));
  if (!Selections)
    return Selections.takeError();
  const unsigned EffectiveRounds = Selections->size();
  Result.Source =
      buildRuntimePrelude(Plans, Context.Options.NondetCondvarWakeups) + Source +
                  (Context.Options.NoRoundRobin
                       ? buildNoRoundRobinScheduler(
                             Plans, EffectiveRounds,
                             Result.Summary.Kind == ProgramKind::MultiThreaded)
                       : Context.Options.Contexts
                             ? buildContextBoundedScheduler(Plans,
                                                            Context.Options.Contexts,
                                                            Result.Summary.Kind ==
                                                                ProgramKind::MultiThreaded)
                             : buildScheduler(
                                   Plans, *Selections,
                                   Result.Summary.Kind == ProgramKind::MultiThreaded));
  Result.PendingReplacements.clear();
  Result.Notes.push_back(
      formatv("phase5: native lazyseq 重写了 {0} 个线程函数并生成 {1} 轮调度器",
              Plans.size(), EffectiveRounds)
          .str());
  if (Context.Options.NoRoundRobin)
    Result.Notes.push_back("phase5: lazyseq 使用 Python norobin 等价调度器");
  else if (Context.Options.Contexts)
    Result.Notes.push_back(
        formatv("phase5: lazyseq 使用 Python context-bounded 调度器（contexts={0}）",
                Context.Options.Contexts)
            .str());
  if (Result.Summary.Kind == ProgramKind::InterruptDriven) {
    Result.Notes.push_back(
        "phase5: native lazyseq 已迁移随机 ISR priority、event 触发、timer 周期激活及 timer 多实例；完整时间约束尚待收敛");
  }
  return Error::success();
}

} // namespace clang::nichecker
