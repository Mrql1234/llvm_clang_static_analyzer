#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_NICHECKER_PASSES_PREINLINERPASS_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_NICHECKER_PASSES_PREINLINERPASS_H
#include "clang-nichecker/Passes/PipelinePass.h"
namespace clang::nichecker { class PreInlinerPass : public PipelinePass { public: llvm::StringRef name() const override; llvm::Error run(const PipelineContext &, TransformResult &) const override; }; }
#endif
