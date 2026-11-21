// ============================================================================
// completion.cpp - Tamarin LSP Code Completion & Signature Help
// ============================================================================

#include "lsp/completion.h"
#include "lsp/navigation.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace tamarin::lsp {

static bool isIdentChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

static std::string getTypeName(const ast::TypeNode* type) {
    if (!type) return "";
    if (type->kind == ast::TypeNodeKind::Named) {
        return static_cast<const ast::NamedTypeNode*>(type)->name;
    }
    if (type->kind == ast::TypeNodeKind::Reference) {
        return getTypeName(static_cast<const ast::ReferenceTypeNode*>(type)->referentType.get());
    }
    if (type->kind == ast::TypeNodeKind::Handle) {
        return getTypeName(static_cast<const ast::HandleTypeNode*>(type)->innerType.get());
    }
    return "";
}

static void findLocalVar(const ast::Stmt* stmt, const std::string& name, const ast::VarDeclStmt*& outVar) {
    if (!stmt || outVar) return;

    if (stmt->kind == ast::StmtKind::VarDecl) {
        auto* v = static_cast<const ast::VarDeclStmt*>(stmt);
        if (v->name == name) {
            outVar = v;
            return;
        }
    } else if (stmt->kind == ast::StmtKind::Block) {
        auto* b = static_cast<const ast::BlockStmt*>(stmt);
        for (const auto& s : b->stmts) {
            findLocalVar(s.get(), name, outVar);
            if (outVar) return;
        }
    } else if (stmt->kind == ast::StmtKind::If) {
        auto* ifs = static_cast<const ast::IfStmt*>(stmt);
        findLocalVar(ifs->thenBlock.get(), name, outVar);
        if (outVar) return;
        findLocalVar(ifs->elseBlock.get(), name, outVar);
    } else if (stmt->kind == ast::StmtKind::While) {
        auto* ws = static_cast<const ast::WhileStmt*>(stmt);
        findLocalVar(ws->body.get(), name, outVar);
    } else if (stmt->kind == ast::StmtKind::For) {
        auto* fs = static_cast<const ast::ForStmt*>(stmt);
        findLocalVar(fs->init.get(), name, outVar);
        if (outVar) return;
        findLocalVar(fs->body.get(), name, outVar);
    } else if (stmt->kind == ast::StmtKind::ParallelFor) {
        auto* pfs = static_cast<const ast::ParallelForStmt*>(stmt);
        findLocalVar(pfs->init.get(), name, outVar);
        if (outVar) return;
        findLocalVar(pfs->body.get(), name, outVar);
    }
}

static void collectAllLocalVars(const ast::Stmt* stmt, std::vector<const ast::VarDeclStmt*>& outVars) {
    if (!stmt) return;

    if (stmt->kind == ast::StmtKind::VarDecl) {
        outVars.push_back(static_cast<const ast::VarDeclStmt*>(stmt));
    } else if (stmt->kind == ast::StmtKind::Block) {
        auto* b = static_cast<const ast::BlockStmt*>(stmt);
        for (const auto& s : b->stmts) {
            collectAllLocalVars(s.get(), outVars);
        }
    } else if (stmt->kind == ast::StmtKind::If) {
        auto* ifs = static_cast<const ast::IfStmt*>(stmt);
        collectAllLocalVars(ifs->thenBlock.get(), outVars);
        collectAllLocalVars(ifs->elseBlock.get(), outVars);
    } else if (stmt->kind == ast::StmtKind::While) {
        auto* ws = static_cast<const ast::WhileStmt*>(stmt);
        collectAllLocalVars(ws->body.get(), outVars);
    } else if (stmt->kind == ast::StmtKind::For) {
        auto* fs = static_cast<const ast::ForStmt*>(stmt);
        collectAllLocalVars(fs->init.get(), outVars);
        collectAllLocalVars(fs->body.get(), outVars);
    } else if (stmt->kind == ast::StmtKind::ParallelFor) {
        auto* pfs = static_cast<const ast::ParallelForStmt*>(stmt);
        collectAllLocalVars(pfs->init.get(), outVars);
        collectAllLocalVars(pfs->body.get(), outVars);
    }
}

// ── Code Completion ─────────────────────────────────────────────────────────

std::vector<CompletionItem> getCompletions(
    const VfsDocument& doc,
    const AnalysisResult& analysis,
    const Position& pos)
{
    std::vector<CompletionItem> items;
    std::unordered_set<std::string> seen;

    auto addItem = [&](CompletionItem item) {
        if (seen.insert(item.label).second) {
            items.push_back(std::move(item));
        }
    };

    const auto& text = doc.text();
    size_t offset = doc.positionToOffset(pos);
    if (offset > text.size()) offset = text.size();

    // 1. Check for Dot Trigger / Member Access: `var.` or `var.prefix`
    bool isDot = false;
    std::string targetVar;

    if (offset > 0) {
        size_t scan = offset;
        // Skip current partial identifier under cursor
        while (scan > 0 && isIdentChar(text[scan - 1])) {
            --scan;
        }
        // Skip trailing spaces if any
        size_t dotPos = scan;
        while (dotPos > 0 && text[dotPos - 1] == ' ') {
            --dotPos;
        }
        if (dotPos > 0 && text[dotPos - 1] == '.') {
            isDot = true;
            size_t varEnd = dotPos - 1;
            while (varEnd > 0 && text[varEnd - 1] == ' ') {
                --varEnd;
            }
            size_t varStart = varEnd;
            while (varStart > 0 && isIdentChar(text[varStart - 1])) {
                --varStart;
            }
            if (varStart < varEnd) {
                targetVar = text.substr(varStart, varEnd - varStart);
            }
        }
    }

    if (isDot && !targetVar.empty()) {
        // Resolve type of targetVar
        std::string structTypeName;

        for (const auto& decl : analysis.program.decls) {
            if (!decl) continue;

            if (decl->kind == ast::DeclKind::Function) {
                const auto& fn = static_cast<const ast::FunctionDecl&>(*decl);
                for (const auto& p : fn.params) {
                    if (p.name == targetVar) {
                        structTypeName = getTypeName(p.type.get());
                        break;
                    }
                }
                if (!structTypeName.empty()) break;

                const ast::VarDeclStmt* lv = nullptr;
                findLocalVar(fn.body.get(), targetVar, lv);
                if (lv) {
                    structTypeName = getTypeName(lv->type.get());
                    break;
                }
            } else if (decl->kind == ast::DeclKind::Var) {
                const auto& gv = static_cast<const ast::GlobalVarDecl&>(*decl);
                if (gv.name == targetVar) {
                    structTypeName = getTypeName(gv.type.get());
                    break;
                }
            } else if (decl->kind == ast::DeclKind::Struct) {
                const auto& st = static_cast<const ast::StructDecl&>(*decl);
                if (st.name == targetVar) {
                    structTypeName = st.name; // static member access
                    break;
                }
            }
        }

        // If struct type name resolved, find the struct and populate fields & methods
        if (!structTypeName.empty()) {
            for (const auto& decl : analysis.program.decls) {
                if (!decl || decl->kind != ast::DeclKind::Struct) continue;
                const auto& st = static_cast<const ast::StructDecl&>(*decl);
                if (st.name == structTypeName) {
                    for (const auto& f : st.fields) {
                        CompletionItem ci;
                        ci.label = f.name;
                        ci.kind = CompletionItemKind::Field;
                        ci.detail = formatType(f.type.get());
                        ci.documentation = "Field of `" + st.name + "`";
                        addItem(std::move(ci));
                    }
                    for (const auto& m : st.methods) {
                        CompletionItem ci;
                        ci.label = m.name;
                        ci.kind = CompletionItemKind::Method;
                        ci.detail = formatType(m.returnType.get()) + "(...)";
                        ci.documentation = "Method of `" + st.name + "`";
                        addItem(std::move(ci));
                    }
                    return items;
                }
            }
        }
    }

    // 2. Global & Local Identifier Completion
    // a) Local variables & parameters
    for (const auto& decl : analysis.program.decls) {
        if (!decl || decl->kind != ast::DeclKind::Function) continue;
        const auto& fn = static_cast<const ast::FunctionDecl&>(*decl);
        for (const auto& p : fn.params) {
            CompletionItem ci;
            ci.label = p.name;
            ci.kind = CompletionItemKind::Variable;
            ci.detail = formatType(p.type.get());
            ci.documentation = "Parameter of `" + fn.name + "`";
            addItem(std::move(ci));
        }

        std::vector<const ast::VarDeclStmt*> lvs;
        collectAllLocalVars(fn.body.get(), lvs);
        for (const auto* lv : lvs) {
            CompletionItem ci;
            ci.label = lv->name;
            ci.kind = CompletionItemKind::Variable;
            ci.detail = formatType(lv->type.get());
            addItem(std::move(ci));
        }
    }

    // b) Top-level declarations
    for (const auto& decl : analysis.program.decls) {
        if (!decl) continue;

        if (decl->kind == ast::DeclKind::Function) {
            const auto& fn = static_cast<const ast::FunctionDecl&>(*decl);
            CompletionItem ci;
            ci.label = fn.name;
            ci.kind = CompletionItemKind::Function;
            ci.detail = formatType(fn.returnType.get()) + "(...)";
            addItem(std::move(ci));
        } else if (decl->kind == ast::DeclKind::Struct) {
            const auto& st = static_cast<const ast::StructDecl&>(*decl);
            CompletionItem ci;
            ci.label = st.name;
            ci.kind = st.isClass ? CompletionItemKind::Class : CompletionItemKind::Struct;
            ci.detail = (st.isClass ? "class " : "struct ") + st.name;
            addItem(std::move(ci));
        } else if (decl->kind == ast::DeclKind::Enum) {
            const auto& en = static_cast<const ast::EnumDecl&>(*decl);
            CompletionItem ci;
            ci.label = en.name;
            ci.kind = CompletionItemKind::Enum;
            ci.detail = "enum " + en.name;
            addItem(std::move(ci));

            for (const auto& v : en.variants) {
                CompletionItem vi;
                vi.label = v.name;
                vi.kind = CompletionItemKind::EnumMember;
                vi.detail = en.name + "::" + v.name;
                addItem(std::move(vi));
            }
        } else if (decl->kind == ast::DeclKind::Trait) {
            const auto& tr = static_cast<const ast::TraitDecl&>(*decl);
            CompletionItem ci;
            ci.label = tr.name;
            ci.kind = CompletionItemKind::Interface;
            ci.detail = "trait " + tr.name;
            addItem(std::move(ci));
        } else if (decl->kind == ast::DeclKind::Const) {
            const auto& cn = static_cast<const ast::ConstDecl&>(*decl);
            CompletionItem ci;
            ci.label = cn.name;
            ci.kind = CompletionItemKind::Constant;
            ci.detail = "const " + formatType(cn.type.get());
            addItem(std::move(ci));
        } else if (decl->kind == ast::DeclKind::Var) {
            const auto& gv = static_cast<const ast::GlobalVarDecl&>(*decl);
            CompletionItem ci;
            ci.label = gv.name;
            ci.kind = CompletionItemKind::Variable;
            ci.detail = formatType(gv.type.get());
            addItem(std::move(ci));
        }
    }

    // c) Built-in Types
    static const char* const kBuiltinTypes[] = {
        "int", "int8", "int16", "int32", "int64",
        "uint", "uint8", "uint16", "uint32", "uint64",
        "float", "float32", "float64",
        "bool", "char", "string", "void", "auto"
    };
    for (const auto* bt : kBuiltinTypes) {
        CompletionItem ci;
        ci.label = bt;
        ci.kind = CompletionItemKind::Class;
        ci.detail = "built-in type";
        addItem(std::move(ci));
    }

    // d) Language Keywords
    static const char* const kKeywords[] = {
        "const", "if", "else", "while", "for", "parallel", "reduce",
        "spawn", "sync", "match", "select", "case", "default",
        "struct", "class", "enum", "trait", "impl", "import", "return",
        "async", "await", "sizeof", "alignof", "as", "break", "continue",
        "interface", "public", "private", "protected", "virtual", "override",
        "true", "false", "null"
    };
    for (const auto* kw : kKeywords) {
        CompletionItem ci;
        ci.label = kw;
        ci.kind = CompletionItemKind::Keyword;
        addItem(std::move(ci));
    }

    return items;
}

// ── Signature Help ──────────────────────────────────────────────────────────

std::optional<SignatureHelp> getSignatureHelp(
    const VfsDocument& doc,
    const AnalysisResult& analysis,
    const Position& pos)
{
    const auto& text = doc.text();
    size_t offset = doc.positionToOffset(pos);
    if (offset > text.size()) offset = text.size();
    if (offset == 0) return std::nullopt;

    // Backward scan to locate enclosing '('
    int parenDepth = 0;
    int commaCount = 0;
    size_t callParenOffset = std::string::npos;

    for (size_t i = offset; i > 0; --i) {
        char c = text[i - 1];
        if (c == ')') {
            parenDepth++;
        } else if (c == '(') {
            if (parenDepth == 0) {
                callParenOffset = i - 1;
                break;
            }
            parenDepth--;
        } else if (c == ',' && parenDepth == 0) {
            commaCount++;
        }
    }

    if (callParenOffset == std::string::npos) return std::nullopt;

    // Scan backwards from '(' to get function identifier
    size_t fnEnd = callParenOffset;
    while (fnEnd > 0 && text[fnEnd - 1] == ' ') {
        --fnEnd;
    }
    size_t fnStart = fnEnd;
    while (fnStart > 0 && isIdentChar(text[fnStart - 1])) {
        --fnStart;
    }
    if (fnStart >= fnEnd) return std::nullopt;

    std::string fnName = text.substr(fnStart, fnEnd - fnStart);

    // Find function declaration in AST
    const ast::FunctionDecl* matchedFn = nullptr;
    const ast::MethodDecl* matchedMethod = nullptr;
    std::string ownerStruct;

    for (const auto& decl : analysis.program.decls) {
        if (!decl) continue;

        if (decl->kind == ast::DeclKind::Function) {
            const auto& fn = static_cast<const ast::FunctionDecl&>(*decl);
            if (fn.name == fnName) {
                matchedFn = &fn;
                break;
            }
        } else if (decl->kind == ast::DeclKind::Struct) {
            const auto& st = static_cast<const ast::StructDecl&>(*decl);
            for (const auto& m : st.methods) {
                if (m.name == fnName) {
                    matchedMethod = &m;
                    ownerStruct = st.name;
                    break;
                }
            }
            if (matchedMethod) break;
        }
    }

    if (!matchedFn && !matchedMethod) return std::nullopt;

    SignatureInformation sig;
    if (matchedFn) {
        sig.label = formatType(matchedFn->returnType.get()) + " " + matchedFn->name + "(";
        for (size_t i = 0; i < matchedFn->params.size(); ++i) {
            if (i > 0) sig.label += ", ";
            std::string pStr = formatType(matchedFn->params[i].type.get()) + " " + matchedFn->params[i].name;
            sig.label += pStr;

            ParameterInformation pInfo;
            pInfo.label = pStr;
            sig.parameters.push_back(std::move(pInfo));
        }
        sig.label += ")";
        sig.documentation = "Function `" + matchedFn->name + "`";
        sig.activeParameter = std::min(commaCount, static_cast<int>(matchedFn->params.size() > 0 ? matchedFn->params.size() - 1 : 0));
    } else if (matchedMethod) {
        sig.label = formatType(matchedMethod->returnType.get()) + " " + ownerStruct + "::" + matchedMethod->name + "(";
        for (size_t i = 0; i < matchedMethod->params.size(); ++i) {
            if (i > 0) sig.label += ", ";
            std::string pStr = formatType(matchedMethod->params[i].type.get()) + " " + matchedMethod->params[i].name;
            sig.label += pStr;

            ParameterInformation pInfo;
            pInfo.label = pStr;
            sig.parameters.push_back(std::move(pInfo));
        }
        sig.label += ")";
        sig.documentation = "Method `" + ownerStruct + "::" + matchedMethod->name + "`";
        sig.activeParameter = std::min(commaCount, static_cast<int>(matchedMethod->params.size() > 0 ? matchedMethod->params.size() - 1 : 0));
    }

    SignatureHelp help;
    help.activeSignature = 0;
    help.activeParameter = sig.activeParameter;
    help.signatures.push_back(std::move(sig));

    return help;
}

} // namespace tamarin::lsp
