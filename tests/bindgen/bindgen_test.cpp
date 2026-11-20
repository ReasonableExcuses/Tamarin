// ============================================================================
// bindgen_test.cpp - Unit tests for tamarin-bindgen
// ============================================================================

#include "test_harness.h"
#include "bindgen/c_parser.h"
#include "bindgen/tamarin_emitter.h"

#include <fstream>
#include <sstream>
#include <filesystem>

using namespace tamarin::bindgen;

namespace {

struct TempHeader {
    std::string path;
    explicit TempHeader(const std::string& filename, const std::string& content) : path(filename) {
        std::ofstream f(filename);
        f << content;
    }
    ~TempHeader() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

} // namespace

TAMARIN_TEST(Bindgen, ParseStructAndFields) {
    TempHeader th("test_bindgen_struct.h", R"(
        typedef struct Vector3 {
            float x;
            float y;
            float z;
        } Vector3;
    )");

    ParserOptions opts;
    opts.headerPath = th.path;
    CParser parser(opts);
    CHeaderModel model;
    std::string err;

    EXPECT_TRUE(parser.parse(model, err));
    EXPECT_EQ(model.structs.size(), 1);
    if (!model.structs.empty()) {
        EXPECT_EQ(model.structs[0].name, "Vector3");
        EXPECT_EQ(model.structs[0].fields.size(), 3);
        if (model.structs[0].fields.size() >= 3) {
            EXPECT_EQ(model.structs[0].fields[0].name, "x");
            EXPECT_EQ(model.structs[0].fields[0].type.tamarinType, "float");
            EXPECT_EQ(model.structs[0].fields[1].name, "y");
            EXPECT_EQ(model.structs[0].fields[2].name, "z");
        }
    }

    std::ostringstream ss;
    TamarinEmitter emitter;
    emitter.emit(model, ss);
    std::string out = ss.str();
    EXPECT_TRUE(out.find("@c_layout") != std::string::npos);
    EXPECT_TRUE(out.find("struct Vector3") != std::string::npos);
    EXPECT_TRUE(out.find("float x;") != std::string::npos);

    return true;
}

TAMARIN_TEST(Bindgen, ParseEnumsAndConstants) {
    TempHeader th("test_bindgen_enum.h", R"(
        #define MAX_BUFFER_SIZE 1024
        typedef enum {
            FLAG_NONE = 0,
            FLAG_READ = 1,
            FLAG_WRITE = 2
        } FileFlags;
    )");

    ParserOptions opts;
    opts.headerPath = th.path;
    CParser parser(opts);
    CHeaderModel model;
    std::string err;

    EXPECT_TRUE(parser.parse(model, err));
    EXPECT_EQ(model.enums.size(), 1);
    if (!model.enums.empty()) {
        EXPECT_EQ(model.enums[0].name, "FileFlags");
        EXPECT_EQ(model.enums[0].constants.size(), 3);
        if (model.enums[0].constants.size() >= 3) {
            EXPECT_EQ(model.enums[0].constants[0].name, "FLAG_NONE");
            EXPECT_EQ(model.enums[0].constants[0].value, 0);
            EXPECT_EQ(model.enums[0].constants[1].name, "FLAG_READ");
            EXPECT_EQ(model.enums[0].constants[1].value, 1);
        }
    }

    bool foundMacro = false;
    for (const auto& c : model.constants) {
        if (c.name == "MAX_BUFFER_SIZE") {
            foundMacro = true;
            EXPECT_EQ(c.value, "1024");
            break;
        }
    }
    EXPECT_TRUE(foundMacro);

    std::ostringstream ss;
    TamarinEmitter emitter;
    emitter.emit(model, ss);
    std::string out = ss.str();
    EXPECT_TRUE(out.find("enum FileFlags") != std::string::npos);
    EXPECT_TRUE(out.find("const int MAX_BUFFER_SIZE = 1024;") != std::string::npos);

    return true;
}

TAMARIN_TEST(Bindgen, ParseFunctionsAndPointers) {
    TempHeader th("test_bindgen_fn.h", R"(
        void InitWindow(int width, int height, const char* title);
        int AddNumbers(int a, int b);
        void SetUserData(void* data);
        typedef int (*LogCallback)(int level);
        void RegisterLogger(LogCallback cb);
    )");

    ParserOptions opts;
    opts.headerPath = th.path;
    CParser parser(opts);
    CHeaderModel model;
    std::string err;

    EXPECT_TRUE(parser.parse(model, err));
    EXPECT_EQ(model.functions.size(), 4);

    std::ostringstream ss;
    TamarinEmitter emitter;
    emitter.emit(model, ss);
    std::string out = ss.str();

    EXPECT_TRUE(out.find("extern \"C\" {") != std::string::npos);
    EXPECT_TRUE(out.find("void InitWindow(int width, int height, *u8 title);") != std::string::npos);
    EXPECT_TRUE(out.find("int AddNumbers(int a, int b);") != std::string::npos);
    EXPECT_TRUE(out.find("void SetUserData(*void data);") != std::string::npos);
    EXPECT_TRUE(out.find("void RegisterLogger(*void") != std::string::npos);

    return true;
}

TAMARIN_TEST(Bindgen, KeywordSanitization) {
    TempHeader th("test_bindgen_kw.h", R"(
        typedef struct Event {
            int type;
            int trait;
            int fn;
        } Event;
    )");

    ParserOptions opts;
    opts.headerPath = th.path;
    CParser parser(opts);
    CHeaderModel model;
    std::string err;

    EXPECT_TRUE(parser.parse(model, err));
    EXPECT_EQ(model.structs.size(), 1);
    if (!model.structs.empty() && model.structs[0].fields.size() >= 3) {
        EXPECT_EQ(model.structs[0].fields[0].name, "type_");
        EXPECT_EQ(model.structs[0].fields[1].name, "trait_");
        EXPECT_EQ(model.structs[0].fields[2].name, "fn_");
    }

    return true;
}
