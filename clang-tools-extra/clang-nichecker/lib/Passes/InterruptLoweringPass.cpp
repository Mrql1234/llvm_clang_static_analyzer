#include "clang-nichecker/Passes/InterruptLoweringPass.h"
#include "clang-nichecker/Support/SourceUtils.h"
#include "llvm/Support/FormatVariadic.h"

using namespace clang;

namespace clang::nichecker {

llvm::StringRef InterruptLoweringPass::name() const {
  return "interrupt-lowering";
}

llvm::Error InterruptLoweringPass::run(const PipelineContext &Context,
                                       TransformResult &Result) const {
  if (Result.Summary.Kind != ProgramKind::InterruptDriven)
    return llvm::Error::success();

  if (Result.Summary.UsesPthreadCreate) {
    Result.Notes.push_back(
        "phase2: 检测到 pthread_create，推测输入已处于多线程中间态，跳过重复中断降级");
    return llvm::Error::success();
  }

  const FunctionDecl *Main = Result.Summary.MainFunction;
  if (!Main || !Main->hasBody()) {
    Result.Notes.push_back(
        "phase2: 未找到主文件中的 main 定义，保持原始源码不变");
    return llvm::Error::success();
  }

  const SourceManager &SM = Context.CI.getSourceManager();
  const LangOptions &LangOpts = Context.CI.getLangOpts();

  std::optional<std::string> MainBodyText =
      getSourceText(Main->getBody()->getSourceRange(), SM, LangOpts);
  if (!MainBodyText) {
    Result.Notes.push_back(
        "phase2: 无法提取 main 函数体源码，保持原始源码不变");
    return llvm::Error::success();
  }

  std::optional<unsigned> BeginOffset = getFileOffset(Main->getBeginLoc(), SM);
  SourceLocation EndLoc =
      Lexer::getLocForEndOfToken(Main->getEndLoc(), 0, SM, LangOpts);
  std::optional<unsigned> EndOffset = getFileOffset(EndLoc, SM);
  if (!BeginOffset || !EndOffset || *BeginOffset > *EndOffset ||
      *EndOffset > Result.Source.size()) {
    Result.Notes.push_back(
        "phase2: 无法定位 main 定义源码区间，保持原始源码不变");
    return llvm::Error::success();
  }

  std::string Replacement;
  llvm::raw_string_ostream OS(Replacement);
  OS << "void *main_task(void *__cs_param_main_task_arg)\n"
     << *MainBodyText << "\n\n";
  OS << "int main()\n{\n";
  OS << "  pthread_t __cs_local_main_t0;\n";
  OS << "  pthread_create(&__cs_local_main_t0, 0, main_task, 0);\n";
  OS << "  return 0;\n";
  OS << "}\n";
  OS.flush();

  Result.PendingReplacements.push_back(
      TextReplacement{*BeginOffset, *EndOffset - *BeginOffset, Replacement});
  Result.Notes.push_back(
      "phase2: 已将中断驱动输入归一化为有界多线程入口(main -> main_task + wrapper main)");
  return llvm::Error::success();
}

} // namespace clang::nichecker
