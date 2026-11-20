// ============================================================================
// c_parser.h - C header parser using libclang
// ============================================================================

#pragma once

#include "bindgen/c_types.h"
#include <clang-c/Index.h>
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>

namespace tamarin::bindgen {

struct ParserOptions {
    std::string headerPath;
    std::vector<std::string> includeDirs;
    std::vector<std::string> defines;
    std::vector<std::string> extraClangArgs;
    std::string prefixFilter;
    bool includeSystemHeaders = false;
};

class CParser {
public:
    explicit CParser(ParserOptions options);
    ~CParser();

    bool parse(CHeaderModel& outModel, std::string& outError);

private:
    ParserOptions options_;
    CXIndex index_ = nullptr;
    CXTranslationUnit tu_ = nullptr;

    std::unordered_set<std::string> visitedStructs_;
    std::unordered_set<std::string> visitedEnums_;
    std::unordered_set<std::string> visitedTypedefs_;
    std::unordered_set<std::string> visitedFunctions_;
    std::unordered_set<std::string> opaqueStructs_;
    std::unordered_map<std::string, std::string> typedefMap_;

    bool isFromTargetHeader(CXCursor cursor) const;
    CType convertType(CXType type, CXCursor cursor = clang_getNullCursor());
    void processStruct(CXCursor cursor, CHeaderModel& model);
    void processUnion(CXCursor cursor, CHeaderModel& model);
    void processEnum(CXCursor cursor, CHeaderModel& model);
    void processFunction(CXCursor cursor, CHeaderModel& model);
    void processTypedef(CXCursor cursor, CHeaderModel& model);
    void processMacro(CXCursor cursor, CHeaderModel& model);

    static CXChildVisitResult visitTopLevel(CXCursor cursor, CXCursor parent, CXClientData clientData);
    static CXChildVisitResult visitStructFields(CXCursor cursor, CXCursor parent, CXClientData clientData);
    static CXChildVisitResult visitEnumConstants(CXCursor cursor, CXCursor parent, CXClientData clientData);
};

} // namespace tamarin::bindgen
