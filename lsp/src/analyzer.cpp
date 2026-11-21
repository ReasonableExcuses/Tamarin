// ============================================================================
// analyzer.cpp - Tamarin LSP Document Analyzer Implementation
// ============================================================================

#include "lsp/analyzer.h"
#include "tamarin/lexer/lexer.h"
#include "tamarin/parser/parser.h"
#include "tamarin/diagnostics/diagnostic_engine.h"

#include <cctype>
#include <sstream>

namespace tamarin::lsp {

AnalysisResult DocumentAnalyzer::analyze(const VfsDocument& doc) {
    AnalysisResult result;
    SourceManager sm;
    std::string path = uriToPath(doc.uri());
    auto fileId = sm.addSource(path, doc.text());

    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser parser(lex, diag);

    try {
        result.program = parser.parseProgram();
    } catch (...) {
        // Safe recovery; diagnostics already logged in diag
    }

    auto sema = std::make_shared<sema::Sema>(diag);
    sema->setLibraryMode(true);

    try {
        sema->analyze(result.program);
    } catch (...) {
        // Safe recovery; diagnostics already logged in diag
    }
    result.sema = sema;

    const auto& text = doc.text();
    for (const auto& d : diag.getDiagnostics()) {
        Diagnostic lspDiag;
        lspDiag.message = d.message;
        lspDiag.source = "tamarin";

        switch (d.severity) {
            case Severity::Error:
                lspDiag.severity = DiagnosticSeverity::Error;
                break;
            case Severity::Warning:
                lspDiag.severity = DiagnosticSeverity::Warning;
                break;
            case Severity::Note:
                lspDiag.severity = DiagnosticSeverity::Information;
                break;
        }

        if (d.location.isValid()) {
            Position startPos = doc.offsetToPosition(d.location.offset);
            size_t endOffset = d.location.offset;

            if (endOffset < text.size()) {
                if (std::isalnum(static_cast<unsigned char>(text[endOffset])) || text[endOffset] == '_') {
                    while (endOffset < text.size() &&
                           (std::isalnum(static_cast<unsigned char>(text[endOffset])) || text[endOffset] == '_')) {
                        endOffset++;
                    }
                } else if (text[endOffset] != '\n' && text[endOffset] != '\r') {
                    endOffset++;
                }
            }
            if (endOffset <= d.location.offset && endOffset < text.size()) {
                endOffset = d.location.offset + 1;
            }

            Position endPos = doc.offsetToPosition(endOffset);
            lspDiag.range = Range{startPos, endPos};
        } else {
            lspDiag.range = Range{Position{0, 0}, Position{0, 0}};
        }

        result.diagnostics.push_back(std::move(lspDiag));
    }

    return result;
}

void DocumentAnalyzer::updateCache(const std::string& uri, AnalysisResult result) {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_[normalizeUri(uri)] = std::make_shared<AnalysisResult>(std::move(result));
}

std::shared_ptr<AnalysisResult> DocumentAnalyzer::getCached(const std::string& uri) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cache_.find(normalizeUri(uri));
    if (it != cache_.end()) {
        return it->second;
    }
    return nullptr;
}

void DocumentAnalyzer::evictCache(const std::string& uri) {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.erase(normalizeUri(uri));
}

void DocumentAnalyzer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
}

} // namespace tamarin::lsp
