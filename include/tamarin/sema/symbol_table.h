// ============================================================================
// symbol_table.h - Scoped symbol table for name resolution
// ============================================================================
#pragma once

#include "tamarin/sema/types.h"
#include "tamarin/sema/provenance.h"
#include "tamarin/source/source_location.h"

#include <string>
#include <unordered_map>
#include <deque>

namespace tamarin::sema {

using BindingId = uint64_t;

struct Symbol {
    std::string    name;
    std::string    mangledName;
    const Type*    type         = nullptr;
    bool           isConst      = false;
    bool           isFunction   = false;
    bool           isParameter  = false;
    bool           isPublic     = false;
    bool           isGlobal     = false;
    bool           isAsync      = false;
    uint64_t       bindingId    = 0;
    uint64_t       regionId     = 0;
    ProvenanceSet  provenance;
    SourceLocation loc;
};

class SymbolTable {
public:
    SymbolTable();

    void pushScope();
    void popScope();

    /// Declare a symbol in the current scope. Returns false if already declared in current scope.
    bool declare(const std::string& name, Symbol sym);

    /// Look up a symbol from innermost to outermost scope.
    [[nodiscard]] const Symbol* lookup(const std::string& name) const;
    [[nodiscard]] Symbol* lookupMut(const std::string& name);

    /// Look up a symbol by unique binding identity.
    [[nodiscard]] const Symbol* lookupBinding(uint64_t bindingId) const;

    /// Look up a symbol only in the current (innermost) scope.
    [[nodiscard]] const Symbol* lookupCurrent(const std::string& name) const;

    /// Get scope depth where a symbol was declared (1 = global, 0 = not found).
    [[nodiscard]] size_t lookupScopeDepth(const std::string& name) const;

    /// Get current scope depth (1 = global).
    [[nodiscard]] size_t depth() const noexcept { return scopes_.size(); }

private:
    std::deque<std::unordered_map<std::string, Symbol>> scopes_;
    uint64_t nextBindingId_ = 1;
};

} // namespace tamarin::sema

