#include "SimpleInliner.h"

#include <sstream>

#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"

#include "RewriteUtils.h"
#include "TransformationManager.h"

using namespace clang;
using namespace llvm;

static const char *DescriptionMsg =
"A really simple inliner. \
This transformation does a simple source-to-source \
inlining. To avoid the abuse of inlining, I put \
some constraints on the size of a function which \
can be inlined - if a function has less than 10 statements, \
then it's legitimate. \n\
\n\
Steps of inlining: \n\
  * create a tmp var for function return value; \n\
  * create a new block which is a copy of the inlined function; \n\
  * at the top of this newly block, inlined function's parameters \
will be declared as local vars with callexpr's arguments as their \
initialization values (if any) \n\
  * inside this newly block, replace all return statements as \
assignment statements, where the LHS is the created tmp var \
(Note that if the inlined function returns void, then \
this step is skipped) \n\
  * replace the callexpr with tmp var above \n\
\n\
Each transformation iteration only transforms one callexpr, \
also it will keep the inlined function body unchanged. \
If the inlined body has no reference anymore, c_delta \
will remove it entirely. \n";

static RegisterTransformation<SimpleInliner>
         Trans("simple-inliner", DescriptionMsg);

class SimpleInlinerCollectionVisitor : public 
  RecursiveASTVisitor<SimpleInlinerCollectionVisitor> {

public:

  explicit SimpleInlinerCollectionVisitor(SimpleInliner *Instance)
    : ConsumerInstance(Instance),
      NumStmts(0)
  { }

  bool VisitCallExpr(CallExpr *CE);
  bool VisitBreakStmt(BreakStmt *S);
  bool VisitCompoundStmt(CompoundStmt *S);
  bool VisitContinueStmt(ContinueStmt *S);
  bool VisitDeclStmt(DeclStmt *S);
  bool VisitDoStmt(DoStmt *S);
  bool VisitForStmt(ForStmt *S);
  bool VisitGotoStmt(GotoStmt *S);
  bool VisitIfStmt(IfStmt *S);
  bool VisitIndirectGotoStmt(IndirectGotoStmt *S);
  bool VisitReturnStmt(ReturnStmt *S);
  bool VisitSwitchCase(SwitchCase *S);
  bool VisitSwitchStmt(SwitchStmt *S);
  bool VisitWhileStmt(WhileStmt *S);
  bool VisitBinaryOperator(BinaryOperator *S);

  unsigned int getNumStmts(void) {
    return NumStmts;
  }

  void setNumStmts(unsigned int Num) {
    NumStmts = Num;
  }

private:

  SimpleInliner *ConsumerInstance;

  unsigned int NumStmts;
};

class SimpleInlinerFunctionVisitor : public 
  RecursiveASTVisitor<SimpleInlinerFunctionVisitor> {

public:

  explicit SimpleInlinerFunctionVisitor(SimpleInliner *Instance)
    : ConsumerInstance(Instance)
  { }

  bool VisitReturnStmt(ReturnStmt *RS);

  bool VisitDeclRefExpr(DeclRefExpr *DRE);

private:

  SimpleInliner *ConsumerInstance;

};

class SimpleInlinerStmtVisitor : public 
  RecursiveASTVisitor<SimpleInlinerStmtVisitor> {

public:

  explicit SimpleInlinerStmtVisitor(SimpleInliner *Instance)
    : ConsumerInstance(Instance),
      CurrentStmt(NULL),
      NeedParen(false)
  { }

  bool VisitCompoundStmt(CompoundStmt *S);

  bool VisitIfStmt(IfStmt *IS);

  bool VisitForStmt(ForStmt *FS);

  bool VisitWhileStmt(WhileStmt *WS);

  bool VisitDoStmt(DoStmt *DS);

  bool VisitCaseStmt(CaseStmt *CS);

  bool VisitDefaultStmt(DefaultStmt *DS);

  void visitNonCompoundStmt(Stmt *S);

  bool VisitCallExpr(CallExpr *CallE);

private:

  SimpleInliner *ConsumerInstance;

  Stmt *CurrentStmt;

  bool NeedParen;

};


bool SimpleInlinerCollectionVisitor::VisitCallExpr(CallExpr *CE)
{
  FunctionDecl *FD = CE->getDirectCallee();
  if (!FD)
    return true;

  ConsumerInstance->AllCallExprs.push_back(CE);
  ConsumerInstance->CalleeToCallerMap[CE] = ConsumerInstance->CurrentFD;
  NumStmts++;
  return true;
}

bool SimpleInlinerCollectionVisitor::VisitBreakStmt(BreakStmt *S)
{
  NumStmts++;
  return true;
}

bool SimpleInlinerCollectionVisitor::VisitCompoundStmt(CompoundStmt *S)
{
  NumStmts++;
  return true;
}

bool SimpleInlinerCollectionVisitor::VisitContinueStmt(ContinueStmt *S)
{
  NumStmts++;
  return true;
}

bool SimpleInlinerCollectionVisitor::VisitDeclStmt(DeclStmt *S)
{
  NumStmts++;
  return true;
}

bool SimpleInlinerCollectionVisitor::VisitDoStmt(DoStmt *S)
{
  NumStmts++;
  return true;
}

bool SimpleInlinerCollectionVisitor::VisitForStmt(ForStmt *S)
{
  NumStmts++;
  return true;
}

bool SimpleInlinerCollectionVisitor::VisitGotoStmt(GotoStmt *S)
{
  NumStmts++;
  return true;
}

bool SimpleInlinerCollectionVisitor::VisitIfStmt(IfStmt *S)
{
  NumStmts++;
  return true;
}

bool SimpleInlinerCollectionVisitor::VisitIndirectGotoStmt(IndirectGotoStmt *S)
{
  NumStmts++;
  return true;
}

bool SimpleInlinerCollectionVisitor::VisitReturnStmt(ReturnStmt *S)
{
  NumStmts++;
  return true;
}

bool SimpleInlinerCollectionVisitor::VisitSwitchCase(SwitchCase *S)
{
  NumStmts++;
  return true;
}

bool SimpleInlinerCollectionVisitor::VisitSwitchStmt(SwitchStmt *S)
{
  NumStmts++;
  return true;
}

bool SimpleInlinerCollectionVisitor::VisitWhileStmt(WhileStmt *S)
{
  NumStmts++;
  return true;
}

bool SimpleInlinerCollectionVisitor::VisitBinaryOperator(BinaryOperator *S)
{
  NumStmts++;
  return true;
}

bool SimpleInlinerFunctionVisitor::VisitReturnStmt(ReturnStmt *RS)
{
  ConsumerInstance->ReturnStmts.push_back(RS);
  return true;
}

bool SimpleInlinerFunctionVisitor::VisitDeclRefExpr(DeclRefExpr *DRE)
{
  const ValueDecl *OrigDecl = DRE->getDecl();
  const ParmVarDecl *PD = dyn_cast<ParmVarDecl>(OrigDecl);
  if (PD)
     ConsumerInstance->ParmRefs.push_back(DRE); 
  return true;
}

bool SimpleInlinerStmtVisitor::VisitCompoundStmt(CompoundStmt *CS)
{
  for (CompoundStmt::body_iterator I = CS->body_begin(),
       E = CS->body_end(); I != E; ++I) {
    CurrentStmt = (*I);
    TraverseStmt(*I);
  }
  return false;
}

void SimpleInlinerStmtVisitor::visitNonCompoundStmt(Stmt *S)
{
  if (!S)
    return;

  CompoundStmt *CS = dyn_cast<CompoundStmt>(S);
  if (CS) {
    VisitCompoundStmt(CS);
    return;
  }

  CurrentStmt = (S);
  NeedParen = true;
  TraverseStmt(S);
  NeedParen = false;
}

// It is used to handle the case where if-then or else branch
// is not treated as a CompoundStmt. So it cannot be traversed
// from VisitCompoundStmt, e.g.,
//   if (x)
//     foo(bar())
bool SimpleInlinerStmtVisitor::VisitIfStmt(IfStmt *IS)
{
  Expr *E = IS->getCond();
  TraverseStmt(E);

  Stmt *ThenB = IS->getThen();
  visitNonCompoundStmt(ThenB);

  Stmt *ElseB = IS->getElse();
  visitNonCompoundStmt(ElseB);

  return false;
}

// It causes unsound transformation because 
// the semantics of loop execution has been changed. 
// For example,
//   int foo(int x)
//   {
//     int i;
//     for(i = 0; i < bar(bar(x)); i++)
//       ...
//   }
// will be transformed to:
//   int foo(int x)
//   {
//     int i;
//     int tmp_var = bar(x);
//     for(i = 0; i < bar(tmp_var); i++)
//       ...
//   }
bool SimpleInlinerStmtVisitor::VisitForStmt(ForStmt *FS)
{
  Stmt *Init = FS->getInit();
  TraverseStmt(Init);

  Expr *Cond = FS->getCond();
  TraverseStmt(Cond);

  Expr *Inc = FS->getInc();
  TraverseStmt(Inc);

  Stmt *Body = FS->getBody();
  visitNonCompoundStmt(Body);
  return false;
}

bool SimpleInlinerStmtVisitor::VisitWhileStmt(WhileStmt *WS)
{
  Expr *E = WS->getCond();
  TraverseStmt(E);

  Stmt *Body = WS->getBody();
  visitNonCompoundStmt(Body);
  return false;
}

bool SimpleInlinerStmtVisitor::VisitDoStmt(DoStmt *DS)
{
  Expr *E = DS->getCond();
  TraverseStmt(E);

  Stmt *Body = DS->getBody();
  visitNonCompoundStmt(Body);
  return false;
}

bool SimpleInlinerStmtVisitor::VisitCaseStmt(CaseStmt *CS)
{
  Stmt *Body = CS->getSubStmt();
  visitNonCompoundStmt(Body);
  return false;
}

bool SimpleInlinerStmtVisitor::VisitDefaultStmt(DefaultStmt *DS)
{
  Stmt *Body = DS->getSubStmt();
  visitNonCompoundStmt(Body);
  return false;
}

bool SimpleInlinerStmtVisitor::VisitCallExpr(CallExpr *CallE) 
{
  if (ConsumerInstance->TheCallExpr == CallE) {
    ConsumerInstance->TheStmt = CurrentStmt;
    ConsumerInstance->NeedParen = NeedParen;
    // Stop recursion
    return false;
  }
  return true;
}
void SimpleInliner::Initialize(ASTContext &context) 
{
  Context = &context;
  SrcManager = &Context->getSourceManager();
  NameQueryWrap = 
    new TransNameQueryWrap(RewriteUtils::getTmpVarNamePrefix());
  CollectionVisitor = new SimpleInlinerCollectionVisitor(this);
  FunctionVisitor = new SimpleInlinerFunctionVisitor(this);
  StmtVisitor = new SimpleInlinerStmtVisitor(this);
  TheRewriter.setSourceMgr(Context->getSourceManager(), 
                           Context->getLangOptions());
}

void SimpleInliner::HandleTopLevelDecl(DeclGroupRef D) 
{
  for (DeclGroupRef::iterator I = D.begin(), E = D.end(); I != E; ++I) {
    FunctionDecl *FD = dyn_cast<FunctionDecl>(*I);
    if (!(FD && FD->isThisDeclarationADefinition()))
      continue;

    CurrentFD = FD;
    CollectionVisitor->setNumStmts(0);
    CollectionVisitor->TraverseDecl(FD);

    if ((CollectionVisitor->getNumStmts() <= MaxNumStmts) &&
        !FD->isVariadic()) {
      ValidFunctionDecls.insert(FD->getCanonicalDecl());
    }
  }
}

void SimpleInliner::HandleTranslationUnit(ASTContext &Ctx)
{
  doAnalysis();
  if (QueryInstanceOnly)
    return;

  if (TransformationCounter > ValidInstanceNum) {
    TransError = TransMaxInstanceError;
    return;
  }

  TransAssert(CurrentFD && "NULL CurrentFD!");
  TransAssert(TheCallExpr && "NULL TheCallExpr!");

  Ctx.getDiagnostics().setSuppressAllDiagnostics(false);

  NameQueryWrap->TraverseDecl(Ctx.getTranslationUnitDecl());
  NamePostfix = NameQueryWrap->getMaxNamePostfix() + 1;

  FunctionVisitor->TraverseDecl(CurrentFD);
  StmtVisitor->TraverseDecl(TheCaller);

  TransAssert(TheStmt && "NULL TheStmt!");
  replaceCallExpr();

  if (Ctx.getDiagnostics().hasErrorOccurred() ||
      Ctx.getDiagnostics().hasFatalErrorOccurred())
    TransError = TransInternalError;
}

bool SimpleInliner::isValidArgExpr(const Expr *E)
{
  TransAssert(E && "NULL Expr!");
  switch(E->getStmtClass()) {
  case Expr::FloatingLiteralClass:
  case Expr::StringLiteralClass:
  case Expr::IntegerLiteralClass:
  case Expr::GNUNullExprClass:
  case Expr::CharacterLiteralClass: // Fall-through
    return true;
  
  case Expr::ParenExprClass:
    return isValidArgExpr(cast<ParenExpr>(E)->getSubExpr());

  case Expr::ImplicitCastExprClass:
  case Expr::CStyleCastExprClass: // Fall-through
    return isValidArgExpr(cast<CastExpr>(E)->getSubExpr());

  case Expr::MemberExprClass:
    return true;

  case Expr::ArraySubscriptExprClass: {
    const ArraySubscriptExpr *AE = cast<ArraySubscriptExpr>(E);
    return isValidArgExpr(AE->getIdx());
  }

  case Expr::DeclRefExprClass:
    return true;

  default:
    return false;
  }
  TransAssert(0 && "Unreachable code!");
  return false;
}

bool SimpleInliner::hasValidArgExprs(const CallExpr *CE)
{
  for(CallExpr::const_arg_iterator I = CE->arg_begin(), E = CE->arg_end();
      I != E; ++I) {
    if (!isValidArgExpr(*I))
      return false;
  }
  return true;
}

void SimpleInliner::doAnalysis(void)
{
  for (SmallVector<CallExpr *, 10>::iterator CI = AllCallExprs.begin(),
       CE = AllCallExprs.end(); CI != CE; ++CI) {

    FunctionDecl *CalleeDecl = (*CI)->getDirectCallee(); 
    TransAssert(CalleeDecl && "Bad CalleeDecl!");
    FunctionDecl *CanonicalDecl = CalleeDecl->getCanonicalDecl();
    if (!ValidFunctionDecls.count(CanonicalDecl))
      continue;

    if (!hasValidArgExprs(*CI))
      continue;

    ValidInstanceNum++;
    if (TransformationCounter == ValidInstanceNum) {
      // It's possible the direct callee is not a definition
      if (!CalleeDecl->isThisDeclarationADefinition()) {
        CalleeDecl = CalleeDecl->getFirstDeclaration();
        for(FunctionDecl::redecl_iterator RI = CalleeDecl->redecls_begin(),
            RE = CalleeDecl->redecls_end(); RI != RE; ++RI) {
          if ((*RI)->isThisDeclarationADefinition()) {
            CalleeDecl = (*RI);
            break;
          }
        }
      }
      TransAssert(CalleeDecl->isThisDeclarationADefinition() && 
                  "Bad CalleeDecl!");
      CurrentFD = CalleeDecl;
      TheCaller = CalleeToCallerMap[(*CI)];
      TransAssert(TheCaller && "NULL TheCaller!");
      TheCallExpr = (*CI);
    }
  }
}

std::string SimpleInliner::getNewTmpName(void)
{
  std::stringstream SS;
  SS << RewriteUtils::getTmpVarNamePrefix() << NamePostfix;
  NamePostfix++;
  return SS.str();
}

void SimpleInliner::createReturnVar(void)
{
  const Type *FDType = CurrentFD->getResultType().getTypePtr();
  const Type *CallExprType = TheCallExpr->getCallReturnType().getTypePtr();

  // We don't need tmp var
  if (FDType->isVoidType() && CallExprType->isVoidType()) {
    return; 
  }

  TmpVarName = getNewTmpName();
  std::string VarStr = TmpVarName;
  CurrentFD->getResultType().getAsStringInternal(VarStr, 
                               Context->getPrintingPolicy());
  VarStr += ";";
  RewriteUtils::addLocalVarToFunc(VarStr, TheCaller,
                                 &TheRewriter, SrcManager);
}

void SimpleInliner::generateParamStrings(void)
{
  unsigned int ArgNum = TheCallExpr->getNumArgs();
  FunctionDecl *FD = TheCallExpr->getDirectCallee();
  unsigned int Idx;

  for(Idx = 0; Idx < FD->getNumParams(); ++Idx) {
    const ParmVarDecl *PD = FD->getParamDecl(Idx);
    std::string ParmStr = PD->getNameAsString();
    PD->getType().getAsStringInternal(ParmStr, 
                                      Context->getPrintingPolicy());
    if (Idx < ArgNum) {
      const Expr *Arg = TheCallExpr->getArg(Idx);
      ParmStr += " = ";
      std::string ArgStr("");
      RewriteUtils::getExprString(Arg, ArgStr, &TheRewriter, SrcManager);
      ParmStr += ArgStr;
    }
    ParmStr += ";\n";
    ParmStrings.push_back(ParmStr);
  }
}

void SimpleInliner::insertReturnStmt
      (std::vector< std::pair<ReturnStmt *, int> > &SortedReturnStmts,
       ReturnStmt *RS, int Off)
{
  std::pair<ReturnStmt *, int> ReturnStmtOffPair(RS, Off);
  if (SortedReturnStmts.empty()) {
    SortedReturnStmts.push_back(ReturnStmtOffPair);
    return;
  }

  std::vector< std::pair<ReturnStmt *, int> >::iterator I, E;
  for(I = SortedReturnStmts.begin(), E = SortedReturnStmts.end(); I != E; ++I) {
    int TmpOff = (*I).second;
    if (Off < TmpOff)
      break;
  }

  if (I == E)
    SortedReturnStmts.push_back(ReturnStmtOffPair);
  else 
    SortedReturnStmts.insert(I, ReturnStmtOffPair);
}

void SimpleInliner::sortReturnStmtsByOffs(const char *StartBuf, 
       std::vector< std::pair<ReturnStmt *, int> > &SortedReturnStmts)
{
  for (ReturnStmtsVector::iterator I = ReturnStmts.begin(), 
       E = ReturnStmts.end(); I != E; ++I) {
    ReturnStmt *RS = (*I);
    SourceLocation RSLocStart = RS->getLocStart();
    const char *RSStartBuf = SrcManager->getCharacterData(RSLocStart);
    int Off = RSStartBuf - StartBuf;
    TransAssert((Off >= 0) && "Bad Offset!");
    insertReturnStmt(SortedReturnStmts, RS, Off);
  }
}

void SimpleInliner::copyFunctionBody(void)
{
  Stmt *Body = CurrentFD->getBody();
  TransAssert(Body && "NULL Body!");

  std::string FuncBodyStr("");
  RewriteUtils::getStmtString(Body, FuncBodyStr, &TheRewriter, SrcManager);
  TransAssert(FuncBodyStr[0] == '{');

  SourceLocation StartLoc = Body->getLocStart();
  const char *StartBuf = SrcManager->getCharacterData(StartLoc);

  std::vector< std::pair<ReturnStmt *, int> > SortedReturnStmts;
  sortReturnStmtsByOffs(StartBuf, SortedReturnStmts);

  // Now we start rewriting
  int Delta = 1; // skip the first { symbol
  for(SmallVector<std::string, 10>::iterator I = ParmStrings.begin(),
       E = ParmStrings.end(); I != E; ++I) {
    std::string PStr = (*I);
    FuncBodyStr.insert(Delta, PStr);
    Delta += PStr.size();
  }

  // restore the effect of {
  Delta--;
  int ReturnSZ = 6;
  std::string TmpVarStr = TmpVarName + " = ";
  int TmpVarNameSize = static_cast<int>(TmpVarStr.size());

  for(std::vector< std::pair<ReturnStmt *, int> >::iterator
      I = SortedReturnStmts.begin(), E = SortedReturnStmts.end(); 
      I != E; ++I) {

    ReturnStmt *RS = (*I).first;
    int Off = (*I).second + Delta;
    Expr *Exp = RS->getRetValue();
    if (Exp) {
      const Type *T = Exp->getType().getTypePtr();
      if (!T->isVoidType()) {
        FuncBodyStr.replace(Off, ReturnSZ, TmpVarStr);
        Delta += (TmpVarNameSize - ReturnSZ);
        continue;
      }
    }
    FuncBodyStr.replace(Off, ReturnSZ, "");
    Delta -= ReturnSZ;
  }

  RewriteUtils::addStringBeforeStmt(TheStmt, FuncBodyStr, NeedParen,
                                    &TheRewriter, SrcManager);
}

void SimpleInliner::replaceCallExpr(void)
{
  // Create a new tmp var for return value
  createReturnVar();
  generateParamStrings();
  copyFunctionBody();

  RewriteUtils::replaceExprNotInclude(TheCallExpr, TmpVarName,
                            &TheRewriter, SrcManager);
}

SimpleInliner::~SimpleInliner(void)
{
  if (CollectionVisitor)
    delete CollectionVisitor;
  if (FunctionVisitor)
    delete FunctionVisitor;
  if (StmtVisitor)
    delete StmtVisitor;
}

