// ============================================================================
// tamarin_emitter.h - Code generator that emits idiomatic Tamarin source code
// ============================================================================

#pragma once

#include "bindgen/c_types.h"
#include <string>
#include <ostream>

namespace tamarin::bindgen {

struct EmitterOptions {
    std::string moduleName;
    std::string stripPrefix;
    bool emitComments = true;
};

class TamarinEmitter {
public:
    explicit TamarinEmitter(EmitterOptions options = {});

    void emit(const CHeaderModel& model, std::ostream& out) const;

private:
    EmitterOptions options_;

    std::string transformName(const std::string& name) const;
    std::string transformType(const std::string& type) const;
};

} // namespace tamarin::bindgen
