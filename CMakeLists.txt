cmake_minimum_required(VERSION 3.20)
project(Tamarin
    VERSION 0.15.0
    LANGUAGES CXX
    DESCRIPTION "Tamarin - A C-Simple, Memory-Safe Systems Language Compiler"
)

# ── C++ Standard ───────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── MSVC Options ──────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /utf-8 /Zc:__cplusplus /EHsc /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

# ── Configurable Product Name ─────────────────────────────────────────
set(TAMARIN_PRODUCT_NAME "Tamarin" CACHE STRING "Public product name")
set(TAMARIN_CODENAME "tamarin" CACHE STRING "Internal codename")
set(TAMARIN_FILE_EXTENSION ".tmn" CACHE STRING "Source file extension")
set(TAMARIN_CLANG_PATH "C:/Program Files/LLVM/bin/clang.exe" CACHE FILEPATH "Path to clang compiler executable")

configure_file(
    "${CMAKE_SOURCE_DIR}/include/tamarin/config.h.in"
    "${CMAKE_BINARY_DIR}/generated/tamarin/config.h"
)

# ── Include Directories ───────────────────────────────────────────────
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/troop/include
    ${CMAKE_BINARY_DIR}/generated
)

# ── Tamarin Runtime Library (for generated programs) ───────────────────
add_library(tamarin_runtime STATIC
    src/runtime/page_provider.cpp
    src/runtime/concurrency.cpp
)
target_include_directories(tamarin_runtime PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/generated
)

# ── Tamarin Runtime Library Static CRT (for statically linked executables) ──
add_library(tamarin_runtime_static STATIC
    src/runtime/page_provider.cpp
    src/runtime/concurrency.cpp
)
target_include_directories(tamarin_runtime_static PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/generated
)
if(MSVC)
    set_property(TARGET tamarin_runtime_static PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ── Tamarin Compiler Library ──────────────────────────────────────────
add_library(tamarin_lib STATIC
    src/source/source_manager.cpp
    src/diagnostics/diagnostic_engine.cpp
    src/lexer/token.cpp
    src/lexer/lexer.cpp
    src/parser/parser.cpp
    src/ast/ast_printer.cpp
    src/sema/types.cpp
    src/sema/symbol_table.cpp
    src/sema/type_checker.cpp
    src/sema/monomorphizer.cpp
    src/sema/sema.cpp
    src/ir/ir.cpp
    src/ir/ir_builder.cpp
    src/ir/opt/range_analysis.cpp
    src/ir/opt/bounds_check_elimination.cpp
    src/ir/opt/alias_analysis.cpp
    src/ir/opt/pass_manager.cpp
    src/backend/llvm_codegen.cpp
    src/backend/llvm_verifier.cpp
    src/backend/c_header_generator.cpp
    src/driver/module_graph.cpp
    src/driver/driver.cpp
)

target_include_directories(tamarin_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/troop/include
    ${CMAKE_BINARY_DIR}/generated
)
target_link_libraries(tamarin_lib PUBLIC tamarin_runtime)

# ── Troop Package Manager Library ─────────────────────────────────────
add_library(troop_lib STATIC
    troop/src/semver.cpp
    troop/src/manifest.cpp
    troop/src/lockfile.cpp
    troop/src/dependency_resolver.cpp
    troop/src/package_manager.cpp
    troop/src/cli.cpp
)
target_include_directories(troop_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/troop/include
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/generated
)

# ── Compiler Executable ───────────────────────────────────────────────
add_executable(tamarin src/main.cpp)
target_link_libraries(tamarin PRIVATE tamarin_lib troop_lib)

# ── Troop Executable ──────────────────────────────────────────────────
add_executable(troop troop/src/main.cpp)
target_link_libraries(troop PRIVATE troop_lib tamarin_lib)

# ── Test Suite ────────────────────────────────────────────────────────
enable_testing()

add_executable(tamarin_tests
    tests/test_main.cpp
    tests/lexer/lexer_test.cpp
    tests/parser/parser_test.cpp
    tests/sema/sema_test.cpp
    tests/codegen/codegen_test.cpp
    tests/modules/module_test.cpp
    tests/modules/stdlib_test.cpp
    troop/tests/troop_test.cpp
)

target_include_directories(tamarin_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/troop/include
    ${CMAKE_BINARY_DIR}/generated
    ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(tamarin_tests PRIVATE tamarin_lib troop_lib)

add_test(NAME TamarinUnitTests COMMAND tamarin_tests)

# ── Install ───────────────────────────────────────────────────────────
install(TARGETS tamarin troop RUNTIME DESTINATION bin)
