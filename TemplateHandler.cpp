/******************************************************************************
 *
 * C++ Insights, copyright (C) by Andreas Fertig
 * Distributed under an MIT license. See LICENSE for details
 *
 ****************************************************************************/

#include "TemplateHandler.h"
#include <type_traits>
#include "ClangCompat.h"
#include "CodeGenerator.h"
#include "InsightsHelpers.h"
#include "InsightsMatchers.h"
#include "OutputFormatHelper.h"

#include "llvm/Support/Path.h"
//-----------------------------------------------------------------------------

using namespace clang;
using namespace clang::ast_matchers;
//-----------------------------------------------------------------------------

namespace clang::ast_matchers {
const internal::VariadicDynCastAllOfMatcher<Decl, VarTemplateDecl> varTemplateDecl;  // NOLINT
}  // namespace clang::ast_matchers

namespace clang::insights {

/// \brief Insert the instantiated template with the resulting code.
static OutputFormatHelper InsertInstantiatedTemplate(const Decl* decl)
{
    OutputFormatHelper outputFormatHelper{};
    outputFormatHelper.AppendNewLine();
    outputFormatHelper.AppendNewLine();

    CodeGenerator codeGenerator{outputFormatHelper};
    codeGenerator.InsertArg(decl);

    return outputFormatHelper;
}
//-----------------------------------------------------------------------------

TemplateHandler::TemplateHandler(Rewriter& rewrite, MatchFinder& matcher)
: InsightsBase(rewrite)
{
    matcher.addMatcher(
        functionDecl(allOf(unless(isExpansionInSystemHeader()),
                           unless(isMacroOrInvalidLocation()),
                           unless(hasAncestor(namespaceDecl())),
                           hasParent(functionTemplateDecl(unless(hasParent(classTemplateSpecializationDecl())),
                                                          unless(hasAncestor(cxxRecordDecl())))),
                           isTemplateInstantiationPlain()))
            .bind("func"),
        this);

    // match typical use where a class template is defined and it is used later.
    matcher.addMatcher(
        classTemplateSpecializationDecl(
            unless(anyOf(isExpansionInSystemHeader(), hasAncestor(namespaceDecl()), hasAncestor(cxxRecordDecl()))),
            hasParent(classTemplateDecl().bind("decl")))
            .bind("class"),
        this);

    // special case, where a class template is defined and somewhere else we request an explicit instantiation
    matcher.addMatcher(classTemplateSpecializationDecl(unless(anyOf(isExpansionInSystemHeader(),
                                                                    hasAncestor(namespaceDecl()),
                                                                    hasParent(classTemplateDecl()),
                                                                    isExplicitTemplateSpecialization())))
                           .bind("class"),
                       this);

    matcher.addMatcher(
        varTemplateDecl(
            unless(anyOf(isExpansionInSystemHeader(), hasAncestor(namespaceDecl()), hasParent(classTemplateDecl()))))
            .bind("vd"),
        this);

    matcher.addMatcher(classTemplateSpecializationDecl(unless(anyOf(isExpansionInSystemHeader(),
                                                                    hasAncestor(namespaceDecl()),
                                                                    hasParent(classTemplateDecl()),
                                                                    isExplicitTemplateSpecialization())))
                           .bind("class"),
                       this);
}
//-----------------------------------------------------------------------------

void TemplateHandler::run(const MatchFinder::MatchResult& result)
{
    if(const auto* functionDecl = result.Nodes.getNodeAs<FunctionDecl>("func")) {
        if(not functionDecl->getBody() && not isa<CXXDeductionGuideDecl>(functionDecl)) {
            return;
        }

        OutputFormatHelper outputFormatHelper = InsertInstantiatedTemplate(functionDecl);
        const auto         endOfCond          = FindLocationAfterSemi(GetEndLoc(functionDecl), result);

        InsertIndentedText(endOfCond.getLocWithOffset(1), outputFormatHelper);

    } else if(const auto* clsTmplSpecDecl = result.Nodes.getNodeAs<ClassTemplateSpecializationDecl>("class")) {
        // skip classes/struct's without a definition
        if(not clsTmplSpecDecl->hasDefinition()) {
            return;
        }

        OutputFormatHelper outputFormatHelper = InsertInstantiatedTemplate(clsTmplSpecDecl);

        if(const auto* clsTmplDecl = result.Nodes.getNodeAs<ClassTemplateDecl>("decl")) {
            const auto endOfCond = FindLocationAfterSemi(GetEndLoc(clsTmplDecl), result);
            InsertIndentedText(endOfCond, outputFormatHelper);

        } else {  // explicit specialization, we have to remove the specialization
            mRewrite.ReplaceText(clsTmplSpecDecl->getSourceRange(), outputFormatHelper.GetString());
        }

    } else if(const auto* vd = result.Nodes.getNodeAs<VarTemplateDecl>("vd")) {
        OutputFormatHelper outputFormatHelper = InsertInstantiatedTemplate(vd);
        const auto         endOfCond          = FindLocationAfterSemi(GetEndLoc(vd), result);

        mRewrite.ReplaceText({vd->getSourceRange().getBegin(), endOfCond.getLocWithOffset(1)},
                             outputFormatHelper.GetString());
    }
}
//-----------------------------------------------------------------------------

}  // namespace clang::insights
