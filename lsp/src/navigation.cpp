// ============================================================================
// navigation.cpp - Tamarin LSP Hover, Definition, References, Document Symbols
// ============================================================================

#include "lsp/navigation.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace tamarin::lsp {

static bool isIdentChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

std::string getIdentifierAtPosition(const VfsDocument& doc, const Position& pos, Range* outRange) {
    const auto& text = doc.text();
    if (text.empty()) return "";

    size_t offset = doc.positionToOffset(pos);
    if (offset >= text.size() && offset > 0) {
        offset = text.size() - 1;
    }

    if (!isIdentChar(text[offset]) && offset > 0 && isIdentChar(text[offset - 1])) {
        offset--;
    }

    if (!isIdentChar(text[offset])) {
        return "";
    }

    size_t start = offset;
    while (start > 0 && isIdentChar(text[start - 1])) {
        start--;
    }

    size_t end = offset;
    while (end < text.size() && isIdentChar(text[end])) {
        end++;
    }

    if (outRange) {
        *outRange = Range{doc.offsetToPosition(start), doc.offsetToPosition(end)};
    }

    return text.substr(start, end - start);
}

std::string formatType(const ast::TypeNode* type) {
    if (!type) return "void";

    switch (type->kind) {
        case ast::TypeNodeKind::Primitive: {
            auto* p = static_cast<const ast::PrimitiveTypeNode*>(type);
            switch (p->primKind) {
                case ast::PrimitiveKind::Void:         return "void";
                case ast::PrimitiveKind::Bool:         return "bool";
                case ast::PrimitiveKind::Char:         return "char";
                case ast::PrimitiveKind::Byte:         return "byte";
                case ast::PrimitiveKind::Short:        return "short";
                case ast::PrimitiveKind::Int:          return "int";
                case ast::PrimitiveKind::Long:         return "long";
                case ast::PrimitiveKind::UnsignedChar: return "unsigned char";
                case ast::PrimitiveKind::UnsignedShort:return "unsigned short";
                case ast::PrimitiveKind::UnsignedInt:  return "unsigned int";
                case ast::PrimitiveKind::UnsignedLong: return "unsigned long";
                case ast::PrimitiveKind::Float:        return "float";
                case ast::PrimitiveKind::Double:       return "double";
                case ast::PrimitiveKind::SizeT:        return "size_t";
                case ast::PrimitiveKind::PtrdiffT:     return "ptrdiff_t";
                case ast::PrimitiveKind::U8:           return "u8";
                case ast::PrimitiveKind::U16:          return "u16";
                case ast::PrimitiveKind::U32:          return "u32";
                case ast::PrimitiveKind::U64:          return "u64";
                case ast::PrimitiveKind::U128:         return "u128";
                case ast::PrimitiveKind::Usize:        return "usize";
                case ast::PrimitiveKind::I8:           return "i8";
                case ast::PrimitiveKind::I16:          return "i16";
                case ast::PrimitiveKind::I32:          return "i32";
                case ast::PrimitiveKind::I64:          return "i64";
                case ast::PrimitiveKind::I128:         return "i128";
                case ast::PrimitiveKind::Isize:        return "isize";
                case ast::PrimitiveKind::Float4:       return "float4";
                case ast::PrimitiveKind::Float8:       return "float8";
                case ast::PrimitiveKind::Int4:         return "int4";
                case ast::PrimitiveKind::Int8:         return "int8";
                case ast::PrimitiveKind::Int16:        return "int16";
            }
            return "unknown";
        }
        case ast::TypeNodeKind::Named: {
            auto* n = static_cast<const ast::NamedTypeNode*>(type);
            std::string s = n->name;
            if (!n->typeArgs.empty()) {
                s += "<";
                for (size_t i = 0; i < n->typeArgs.size(); ++i) {
                    if (i > 0) s += ", ";
                    s += formatType(n->typeArgs[i].get());
                }
                s += ">";
            }
            return s;
        }
        case ast::TypeNodeKind::Reference: {
            auto* r = static_cast<const ast::ReferenceTypeNode*>(type);
            std::string s;
            if (r->isPointer) s += "*";
            else s += "&";
            if (r->isConst) s += "const ";
            s += formatType(r->referentType.get());
            return s;
        }
        case ast::TypeNodeKind::Array: {
            auto* a = static_cast<const ast::ArrayTypeNode*>(type);
            return formatType(a->elementType.get()) + "[]";
        }
        case ast::TypeNodeKind::Handle: {
            auto* h = static_cast<const ast::HandleTypeNode*>(type);
            return "Handle<" + formatType(h->innerType.get()) + ">";
        }
        case ast::TypeNodeKind::Task: {
            auto* t = static_cast<const ast::TaskTypeNode*>(type);
            return "Task<" + formatType(t->elementType.get()) + ">";
        }
        case ast::TypeNodeKind::Slab: {
            auto* s = static_cast<const ast::SlabTypeNode*>(type);
            return "Slab<" + formatType(s->elementType.get()) + ">";
        }
        case ast::TypeNodeKind::Auto: {
            auto* a = static_cast<const ast::AutoTypeNode*>(type);
            return a->isConst ? "const auto" : "auto";
        }
        case ast::TypeNodeKind::Tuple: {
            auto* tup = static_cast<const ast::TupleTypeNode*>(type);
            std::string s = "(";
            for (size_t i = 0; i < tup->elements.size(); ++i) {
                if (i > 0) s += ", ";
                s += formatType(tup->elements[i].get());
            }
            s += ")";
            return s;
        }
        case ast::TypeNodeKind::Function: {
            auto* fn = static_cast<const ast::FunctionTypeNode*>(type);
            std::string s = "fn(";
            for (size_t i = 0; i < fn->paramTypes.size(); ++i) {
                if (i > 0) s += ", ";
                s += formatType(fn->paramTypes[i].get());
            }
            s += ") -> " + formatType(fn->returnType.get());
            return s;
        }
    }
    return "unknown";
}

// ── Recursive statement walker for local variables ──────────────────────────

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

// ── Hover ───────────────────────────────────────────────────────────────────

std::optional<Hover> getHover(const VfsDocument& doc, const AnalysisResult& analysis, const Position& pos) {
    Range wordRange;
    std::string ident = getIdentifierAtPosition(doc, pos, &wordRange);
    if (ident.empty()) return std::nullopt;

    // 1. Search inside functions for parameters and local variables
    for (const auto& decl : analysis.program.decls) {
        if (!decl) continue;

        if (decl->kind == ast::DeclKind::Function) {
            const auto& fn = static_cast<const ast::FunctionDecl&>(*decl);

            for (const auto& p : fn.params) {
                if (p.name == ident) {
                    std::string md = "```tamarin\n(parameter) " + formatType(p.type.get()) + " " + p.name + "\n```";
                    return Hover{md, wordRange};
                }
            }

            const ast::VarDeclStmt* localVar = nullptr;
            findLocalVar(fn.body.get(), ident, localVar);
            if (localVar) {
                std::string md = "```tamarin\n";
                if (localVar->isConst) md += "const ";
                md += formatType(localVar->type.get()) + " " + localVar->name + "\n```";
                return Hover{md, wordRange};
            }
        } else if (decl->kind == ast::DeclKind::Struct) {
            const auto& st = static_cast<const ast::StructDecl&>(*decl);
            for (const auto& f : st.fields) {
                if (f.name == ident) {
                    std::string md = "```tamarin\n(field) " + formatType(f.type.get()) + " " + st.name + "::" + f.name + "\n```";
                    return Hover{md, wordRange};
                }
            }
            for (const auto& m : st.methods) {
                if (m.name == ident) {
                    std::string md = "```tamarin\n(method) ";
                    if (m.isVirtual) md += "virtual ";
                    md += formatType(m.returnType.get()) + " " + st.name + "::" + m.name + "(";
                    for (size_t i = 0; i < m.params.size(); ++i) {
                        if (i > 0) md += ", ";
                        md += formatType(m.params[i].type.get()) + " " + m.params[i].name;
                    }
                    md += ")\n```";
                    return Hover{md, wordRange};
                }
            }
        }
    }

    // 2. Search top-level declarations
    for (const auto& decl : analysis.program.decls) {
        if (!decl) continue;

        if (decl->kind == ast::DeclKind::Function) {
            const auto& fn = static_cast<const ast::FunctionDecl&>(*decl);
            if (fn.name == ident) {
                std::string md = "```tamarin\n";
                if (fn.isAsync) md += "async ";
                md += formatType(fn.returnType.get()) + " " + fn.name + "(";
                for (size_t i = 0; i < fn.params.size(); ++i) {
                    if (i > 0) md += ", ";
                    md += formatType(fn.params[i].type.get()) + " " + fn.params[i].name;
                }
                md += ")\n```";
                return Hover{md, wordRange};
            }
        } else if (decl->kind == ast::DeclKind::Struct) {
            const auto& st = static_cast<const ast::StructDecl&>(*decl);
            if (st.name == ident) {
                std::string md = "```tamarin\n";
                if (st.layout == ast::LayoutDirective::CLayout) md += "@c_layout ";
                else if (st.layout == ast::LayoutDirective::Scan) md += "@layout(scan) ";
                md += (st.isClass ? "class " : "struct ") + st.name + " {\n";
                for (const auto& f : st.fields) {
                    md += "    " + formatType(f.type.get()) + " " + f.name + ";\n";
                }
                for (const auto& m : st.methods) {
                    md += "    " + formatType(m.returnType.get()) + " " + m.name + "(...);\n";
                }
                md += "}\n```";
                return Hover{md, wordRange};
            }
        } else if (decl->kind == ast::DeclKind::Enum) {
            const auto& en = static_cast<const ast::EnumDecl&>(*decl);
            if (en.name == ident) {
                std::string md = "```tamarin\nenum " + en.name + " {\n";
                for (const auto& v : en.variants) {
                    md += "    " + v.name + ",\n";
                }
                md += "}\n```";
                return Hover{md, wordRange};
            }
            for (const auto& v : en.variants) {
                if (v.name == ident) {
                    std::string md = "```tamarin\nenum " + en.name + "::" + v.name + "\n```";
                    return Hover{md, wordRange};
                }
            }
        } else if (decl->kind == ast::DeclKind::Trait) {
            const auto& tr = static_cast<const ast::TraitDecl&>(*decl);
            if (tr.name == ident) {
                std::string md = "```tamarin\ntrait " + tr.name + "\n```";
                return Hover{md, wordRange};
            }
        } else if (decl->kind == ast::DeclKind::Const) {
            const auto& cn = static_cast<const ast::ConstDecl&>(*decl);
            if (cn.name == ident) {
                std::string md = "```tamarin\nconst " + formatType(cn.type.get()) + " " + cn.name + "\n```";
                return Hover{md, wordRange};
            }
        } else if (decl->kind == ast::DeclKind::Var) {
            const auto& gv = static_cast<const ast::GlobalVarDecl&>(*decl);
            if (gv.name == ident) {
                std::string md = "```tamarin\n" + formatType(gv.type.get()) + " " + gv.name + "\n```";
                return Hover{md, wordRange};
            }
        }
    }

    return std::nullopt;
}

// ── Go-To-Definition ────────────────────────────────────────────────────────

std::optional<Location> getDefinition(const VfsDocument& doc, const AnalysisResult& analysis, const Position& pos) {
    std::string ident = getIdentifierAtPosition(doc, pos);
    if (ident.empty()) return std::nullopt;

    auto makeLoc = [&](SourceLocation loc) -> Location {
        Position start = doc.offsetToPosition(loc.offset);
        Position end = doc.offsetToPosition(loc.offset + ident.size());
        return Location{doc.uri(), Range{start, end}};
    };

    // 1. Search inside functions for local parameters and variables
    for (const auto& decl : analysis.program.decls) {
        if (!decl) continue;

        if (decl->kind == ast::DeclKind::Function) {
            const auto& fn = static_cast<const ast::FunctionDecl&>(*decl);

            for (const auto& p : fn.params) {
                if (p.name == ident && p.loc.isValid()) {
                    return makeLoc(p.loc);
                }
            }

            const ast::VarDeclStmt* localVar = nullptr;
            findLocalVar(fn.body.get(), ident, localVar);
            if (localVar && localVar->loc.isValid()) {
                return makeLoc(localVar->loc);
            }
        } else if (decl->kind == ast::DeclKind::Struct) {
            const auto& st = static_cast<const ast::StructDecl&>(*decl);
            for (const auto& f : st.fields) {
                if (f.name == ident && f.loc.isValid()) {
                    return makeLoc(f.loc);
                }
            }
            for (const auto& m : st.methods) {
                if (m.name == ident && m.loc.isValid()) {
                    return makeLoc(m.loc);
                }
            }
        }
    }

    // 2. Search top-level declarations
    for (const auto& decl : analysis.program.decls) {
        if (!decl) continue;

        if (decl->kind == ast::DeclKind::Function) {
            const auto& fn = static_cast<const ast::FunctionDecl&>(*decl);
            if (fn.name == ident && fn.loc.isValid()) return makeLoc(fn.loc);
        } else if (decl->kind == ast::DeclKind::Struct) {
            const auto& st = static_cast<const ast::StructDecl&>(*decl);
            if (st.name == ident && st.loc.isValid()) return makeLoc(st.loc);
        } else if (decl->kind == ast::DeclKind::Enum) {
            const auto& en = static_cast<const ast::EnumDecl&>(*decl);
            if (en.name == ident && en.loc.isValid()) return makeLoc(en.loc);
            for (const auto& v : en.variants) {
                if (v.name == ident && v.loc.isValid()) return makeLoc(v.loc);
            }
        } else if (decl->kind == ast::DeclKind::Trait) {
            const auto& tr = static_cast<const ast::TraitDecl&>(*decl);
            if (tr.name == ident && tr.loc.isValid()) return makeLoc(tr.loc);
        } else if (decl->kind == ast::DeclKind::Const) {
            const auto& cn = static_cast<const ast::ConstDecl&>(*decl);
            if (cn.name == ident && cn.loc.isValid()) return makeLoc(cn.loc);
        } else if (decl->kind == ast::DeclKind::Var) {
            const auto& gv = static_cast<const ast::GlobalVarDecl&>(*decl);
            if (gv.name == ident && gv.loc.isValid()) return makeLoc(gv.loc);
        }
    }

    return std::nullopt;
}

// ── Find References ─────────────────────────────────────────────────────────

std::vector<Location> getReferences(const VfsDocument& doc, const AnalysisResult& /*analysis*/, const Position& pos) {
    std::string ident = getIdentifierAtPosition(doc, pos);
    if (ident.empty()) return {};

    std::vector<Location> refs;
    const auto& text = doc.text();
    size_t cur = 0;

    while ((cur = text.find(ident, cur)) != std::string::npos) {
        bool startBoundary = (cur == 0) || !isIdentChar(text[cur - 1]);
        bool endBoundary = (cur + ident.size() >= text.size()) || !isIdentChar(text[cur + ident.size()]);

        if (startBoundary && endBoundary) {
            Position startPos = doc.offsetToPosition(cur);
            Position endPos = doc.offsetToPosition(cur + ident.size());
            refs.push_back(Location{doc.uri(), Range{startPos, endPos}});
        }
        cur += ident.size();
    }

    return refs;
}

// ── Document Symbols ────────────────────────────────────────────────────────

std::vector<DocumentSymbol> getDocumentSymbols(const VfsDocument& doc, const AnalysisResult& analysis) {
    std::vector<DocumentSymbol> symbols;

    for (const auto& decl : analysis.program.decls) {
        if (!decl) continue;

        if (decl->kind == ast::DeclKind::Function) {
            const auto& fn = static_cast<const ast::FunctionDecl&>(*decl);
            Position start = doc.offsetToPosition(fn.loc.offset);
            Position selEnd = doc.offsetToPosition(fn.loc.offset + fn.name.size());
            Range selRange{start, selEnd};

            DocumentSymbol sym;
            sym.name = fn.name;
            sym.kind = SymbolKind::Function;
            sym.detail = formatType(fn.returnType.get()) + "(...)";
            sym.range = selRange;
            sym.selectionRange = selRange;
            symbols.push_back(std::move(sym));
        } else if (decl->kind == ast::DeclKind::Struct) {
            const auto& st = static_cast<const ast::StructDecl&>(*decl);
            Position start = doc.offsetToPosition(st.loc.offset);
            Position selEnd = doc.offsetToPosition(st.loc.offset + st.name.size());
            Range selRange{start, selEnd};

            DocumentSymbol sym;
            sym.name = st.name;
            sym.kind = st.isClass ? SymbolKind::Class : SymbolKind::Struct;
            sym.range = selRange;
            sym.selectionRange = selRange;

            for (const auto& f : st.fields) {
                Position fStart = doc.offsetToPosition(f.loc.offset);
                Position fEnd = doc.offsetToPosition(f.loc.offset + f.name.size());
                DocumentSymbol fSym;
                fSym.name = f.name;
                fSym.kind = SymbolKind::Field;
                fSym.detail = formatType(f.type.get());
                fSym.range = Range{fStart, fEnd};
                fSym.selectionRange = Range{fStart, fEnd};
                sym.children.push_back(std::move(fSym));
            }
            for (const auto& m : st.methods) {
                Position mStart = doc.offsetToPosition(m.loc.offset);
                Position mEnd = doc.offsetToPosition(m.loc.offset + m.name.size());
                DocumentSymbol mSym;
                mSym.name = m.name;
                mSym.kind = SymbolKind::Method;
                mSym.detail = formatType(m.returnType.get()) + "(...)";
                mSym.range = Range{mStart, mEnd};
                mSym.selectionRange = Range{mStart, mEnd};
                sym.children.push_back(std::move(mSym));
            }
            symbols.push_back(std::move(sym));
        } else if (decl->kind == ast::DeclKind::Enum) {
            const auto& en = static_cast<const ast::EnumDecl&>(*decl);
            Position start = doc.offsetToPosition(en.loc.offset);
            Position selEnd = doc.offsetToPosition(en.loc.offset + en.name.size());
            Range selRange{start, selEnd};

            DocumentSymbol sym;
            sym.name = en.name;
            sym.kind = SymbolKind::Enum;
            sym.range = selRange;
            sym.selectionRange = selRange;

            for (const auto& v : en.variants) {
                Position vStart = doc.offsetToPosition(v.loc.offset);
                Position vEnd = doc.offsetToPosition(v.loc.offset + v.name.size());
                DocumentSymbol vSym;
                vSym.name = v.name;
                vSym.kind = SymbolKind::EnumMember;
                vSym.range = Range{vStart, vEnd};
                vSym.selectionRange = Range{vStart, vEnd};
                sym.children.push_back(std::move(vSym));
            }
            symbols.push_back(std::move(sym));
        } else if (decl->kind == ast::DeclKind::Trait) {
            const auto& tr = static_cast<const ast::TraitDecl&>(*decl);
            Position start = doc.offsetToPosition(tr.loc.offset);
            Position selEnd = doc.offsetToPosition(tr.loc.offset + tr.name.size());
            Range selRange{start, selEnd};

            DocumentSymbol sym;
            sym.name = tr.name;
            sym.kind = SymbolKind::Interface;
            sym.range = selRange;
            sym.selectionRange = selRange;
            symbols.push_back(std::move(sym));
        } else if (decl->kind == ast::DeclKind::Const) {
            const auto& cn = static_cast<const ast::ConstDecl&>(*decl);
            Position start = doc.offsetToPosition(cn.loc.offset);
            Position selEnd = doc.offsetToPosition(cn.loc.offset + cn.name.size());
            Range selRange{start, selEnd};

            DocumentSymbol sym;
            sym.name = cn.name;
            sym.kind = SymbolKind::Constant;
            sym.detail = formatType(cn.type.get());
            sym.range = selRange;
            sym.selectionRange = selRange;
            symbols.push_back(std::move(sym));
        } else if (decl->kind == ast::DeclKind::Var) {
            const auto& gv = static_cast<const ast::GlobalVarDecl&>(*decl);
            Position start = doc.offsetToPosition(gv.loc.offset);
            Position selEnd = doc.offsetToPosition(gv.loc.offset + gv.name.size());
            Range selRange{start, selEnd};

            DocumentSymbol sym;
            sym.name = gv.name;
            sym.kind = SymbolKind::Variable;
            sym.detail = formatType(gv.type.get());
            sym.range = selRange;
            sym.selectionRange = selRange;
            symbols.push_back(std::move(sym));
        }
    }

    return symbols;
}

} // namespace tamarin::lsp
