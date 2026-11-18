// ============================================================================
// module_scope.h - Scope and export metadata for a Tamarin module
// ============================================================================
#pragma once

#include "tamarin/sema/symbol_table.h"
#include "tamarin/sema/types.h"

#include <string>
#include <unordered_map>

namespace tamarin::sema {

struct ModuleScope {
    std::string logicalName;
    std::string mangledPrefix; // e.g. "math_vector_" or "" for main
    bool isMainModule = false;

    // Declarations in this module
    std::unordered_map<std::string, Symbol> symbols;      // all top-level symbols (functions, consts)
    std::unordered_map<std::string, const Type*> types;   // all top-level types (structs, classes)

    // Generic templates in this module
    std::unordered_map<std::string, const ast::FunctionDecl*> genericFunctions;
    std::unordered_map<std::string, const ast::StructDecl*> genericStructs;
    std::unordered_map<std::string, const ast::EnumDecl*> genericEnums;

    // Public exports (subset of above where isPublic == true)
    std::unordered_map<std::string, Symbol> exportedSymbols;
    std::unordered_map<std::string, const Type*> exportedTypes;
    std::unordered_map<std::string, const ast::FunctionDecl*> exportedGenericFunctions;
    std::unordered_map<std::string, const ast::StructDecl*> exportedGenericStructs;
    std::unordered_map<std::string, const ast::EnumDecl*> exportedGenericEnums;

    // Imported aliases: alias -> target ModuleScope*
    std::unordered_map<std::string, const ModuleScope*> aliasToModule;
    std::unordered_map<std::string, std::string> aliasToLogicalName;
};

} // namespace tamarin::sema

