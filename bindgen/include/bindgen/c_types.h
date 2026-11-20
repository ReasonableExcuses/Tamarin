// ============================================================================
// c_types.h - Data structures representing parsed C entities for bindgen
// ============================================================================

#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace tamarin::bindgen {

struct CType {
    std::string rawSpelling;
    std::string tamarinType;
    bool isPointer = false;
    bool isConst = false;
    bool isFunctionPointer = false;
    int64_t arraySize = -1; // -1 if not array, >= 0 for constant array
};

struct CField {
    std::string name;
    CType type;
    int bitWidth = -1; // -1 if not bitfield
    int64_t offsetInBits = 0;
    std::string comment;
};

struct CStruct {
    std::string name;
    bool isUnion = false;
    int64_t sizeInBytes = 0;
    int64_t alignInBytes = 0;
    std::vector<CField> fields;
    std::string comment;
};

struct CEnumConstant {
    std::string name;
    int64_t value = 0;
    std::string comment;
};

struct CEnum {
    std::string name;
    std::vector<CEnumConstant> constants;
    std::string comment;
};

struct CParam {
    std::string name;
    CType type;
};

struct CFunction {
    std::string name;
    CType returnType;
    std::vector<CParam> params;
    bool isVariadic = false;
    std::string comment;
};

struct CTypedef {
    std::string name;
    CType targetType;
    std::string comment;
};

struct CConstant {
    std::string name;
    std::string value;
    std::string type; // "int", "float", "string"
    std::string comment;
};

struct CHeaderModel {
    std::string headerPath;
    std::vector<CTypedef> typedefs;
    std::vector<CEnum> enums;
    std::vector<CStruct> structs;
    std::vector<CFunction> functions;
    std::vector<CConstant> constants;
};

} // namespace tamarin::bindgen
