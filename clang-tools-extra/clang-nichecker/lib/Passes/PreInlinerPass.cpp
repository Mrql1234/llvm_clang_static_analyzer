#include "clang-nichecker/Passes/PreInlinerPass.h"
#include "clang-nichecker/Support/SourceUtils.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "llvm/Support/FormatVariadic.h"
#include <map>
using namespace clang; using namespace llvm;
namespace clang::nichecker { namespace {
class Visitor : public RecursiveASTVisitor<Visitor> {
public: explicit Visitor(ASTContext &C) : C(C) {}
bool VisitCallExpr(CallExpr *Call) {
  const FunctionDecl *F=Call->getDirectCallee(); if (!F || !F->hasBody() || F->getReturnType()->isVoidType() || !nested(Call) || !isMainFileLocation(Call->getBeginLoc(),C.getSourceManager())) return true;
  const Stmt *Top=top(Call); if (!Top || Top==Call) return true;
  auto Text=getSourceText(Call->getSourceRange(),C.getSourceManager(),C.getLangOpts()); auto B=getFileOffset(Call->getBeginLoc(),C.getSourceManager()); auto E=getFileOffset(Lexer::getLocForEndOfToken(Call->getEndLoc(),0,C.getSourceManager(),C.getLangOpts()),C.getSourceManager()); auto T=getFileOffset(Top->getBeginLoc(),C.getSourceManager());
  if (!Text||!B||!E||!T||*E<=*B) return true;
  const std::string N="__cs_preinliner_"+std::to_string(Count++); R.push_back({*B,*E-*B,N}); Prefix[*T].push_back(F->getReturnType().getAsString()+" "+N+" = "+*Text+"; "); return true;
}
std::vector<TextReplacement> take(){for(auto &P:Prefix){std::string S; for(auto &X:P.second)S+=X; R.push_back({P.first,0,S});}return std::move(R);} unsigned count()const{return Count;}
private: bool nested(const Stmt *N)const{const Stmt *Cur=N;while(Cur){auto P=C.getParents(*Cur);if(P.empty())return false;if(P[0].get<CallExpr>())return true;if(P[0].get<CompoundStmt>())return false;Cur=P[0].get<Stmt>();}return false;} const Stmt *top(const Stmt *N)const{const Stmt *Cur=N;while(Cur){auto P=C.getParents(*Cur);if(P.empty())return nullptr;if(P[0].get<CompoundStmt>())return Cur;Cur=P[0].get<Stmt>();}return nullptr;} ASTContext &C;std::vector<TextReplacement> R;std::map<unsigned,std::vector<std::string>> Prefix;unsigned Count=0;};
} llvm::StringRef PreInlinerPass::name()const{return "preinliner";} llvm::Error PreInlinerPass::run(const PipelineContext &C,TransformResult &R)const{Visitor V(C.getASTContext());V.TraverseDecl(C.getASTContext().getTranslationUnitDecl());auto X=V.take();R.PendingReplacements.insert(R.PendingReplacements.end(),X.begin(),X.end());R.Notes.push_back(formatv("phase3: preinliner 提升了 {0} 个嵌套调用",V.count()).str());return Error::success();} }
