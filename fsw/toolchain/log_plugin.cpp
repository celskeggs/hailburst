// Based on source code from
// https://github.com/sinelaw/elfs-clang-plugins/blob/master/warn_unused_result/warn_unused_result.cpp

#include <clang/Frontend/FrontendPluginRegistry.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Lex/MacroInfo.h>
#include <clang/AST/Decl.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/AST/ASTContext.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/raw_ostream.h>

#include <map>
#include <set>
#include <iostream>
#include <string>
#include <cinttypes>

using namespace clang;
using namespace clang::ast_matchers;

class CollectLogDictionaryMatchCallback : public MatchFinder::MatchCallback {
public:
    CollectLogDictionaryMatchCallback() {}

    virtual void run(const MatchFinder::MatchResult &Result) {
        ASTContext *context = Result.Context;
        DiagnosticsEngine &D = context->getDiagnostics();
        const CallExpr *debugExpr = Result.Nodes.getNodeAs<CallExpr>("callSite");

        D.Report(debugExpr->getExprLoc(),
                 D.getCustomDiagID(DiagnosticsEngine::Remark, "Found call to debugf"));
    }
};

class CollectLogDictionaryAction : public PluginASTAction {
protected:
    StatementMatcher loggingInvocationMatcher =
            callExpr(callee(functionDecl(hasName("debugf")))).bind("callSite");

    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, llvm::StringRef filename) override {
        MatchFinder *const find = new MatchFinder();
        find->addMatcher(loggingInvocationMatcher, new CollectLogDictionaryMatchCallback());
        return find->newASTConsumer();
    }

    bool ParseArgs(const CompilerInstance &CI, const std::vector<std::string> &args) override {
        if (args.size() == 0) {
            return true;
        }
        DiagnosticsEngine &D = CI.getDiagnostics();
        D.Report(D.getCustomDiagID(DiagnosticsEngine::Error,
                                   "collect_log_dict plugin: invalid arguments"));
        return false;
    }

    ActionType getActionType() override {
        return AddAfterMainAction;
    }
};

static FrontendPluginRegistry::Add <CollectLogDictionaryAction> ADD(
        "collect_log_dict", "Collect a logging dictionary by scanning source code");
