// ============================================================================
// symbol_table.cpp - Scoped symbol table implementation
// ============================================================================

#include "tamarin/sema/symbol_table.h"

namespace tamarin::sema {

SymbolTable::SymbolTable() {
    // Start with global scope
    pushScope();
}

void SymbolTable::pushScope() {
    scopes_.emplace_back();
}

void SymbolTable::popScope() {
    if (!scopes_.empty()) {
        scopes_.pop_back();
    }
}

bool SymbolTable::declare(const std::string& name, Symbol sym) {
    if (scopes_.empty()) return false;
    auto& current = scopes_.back();
    if (current.find(name) != current.end()) {
        return false; // Already declared in this scope
    }
    if (sym.bindingId == 0) {
        sym.bindingId = nextBindingId_++;
    }
    if (scopes_.size() == 1) {
        sym.isGlobal = true;
    }
    current.emplace(name, std::move(sym));
    return true;
}

const Symbol* SymbolTable::lookup(const std::string& name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            return &found->second;
        }
    }
    return nullptr;
}

Symbol* SymbolTable::lookupMut(const std::string& name) {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            return &found->second;
        }
    }
    return nullptr;
}

const Symbol* SymbolTable::lookupBinding(uint64_t bindingId) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        for (const auto& [n, sym] : *it) {
            if (sym.bindingId == bindingId) {
                return &sym;
            }
        }
    }
    return nullptr;
}

const Symbol* SymbolTable::lookupCurrent(const std::string& name) const {
    if (scopes_.empty()) return nullptr;
    auto found = scopes_.back().find(name);
    if (found != scopes_.back().end()) {
        return &found->second;
    }
    return nullptr;
}

size_t SymbolTable::lookupScopeDepth(const std::string& name) const {
    size_t d = scopes_.size();
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it, --d) {
        if (it->find(name) != it->end()) {
            return d;
        }
    }
    return 0;
}

} // namespace tamarin::sema

