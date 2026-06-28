#include "clang-nichecker/Passes/LegacyModulePass.h"

namespace clang::nichecker {

LegacyModulePass::LegacyModulePass(std::string PassName, std::string Note)
    : PassName(std::move(PassName)), Note(std::move(Note)) {}

llvm::StringRef LegacyModulePass::name() const { return PassName; }

llvm::Error LegacyModulePass::run(const PipelineContext &Context,
                                  TransformResult &Result) const {
  (void)Context;
  Result.Notes.push_back(Note);
  return llvm::Error::success();
}

} // namespace clang::nichecker
