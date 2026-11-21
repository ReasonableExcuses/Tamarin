cmake_minimum_required(VERSION 3.20)
project(Tamarin
    VERSION 0.16.0
    LANGUAGES C CXX
    DESCRIPTION "Tamarin - A C-Simple, Memory-Safe Systems Language Compiler"
)

# ── C++ Standard ───────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ── Compiler Options ──────────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /utf-8 /Zc:__cplusplus /EHsc /permissive- /FS)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    add_compile_options(-Wall -Wextra)
endif()

find_package(Threads REQUIRED)

# ── LLVM Discovery ────────────────────────────────────────────────────
find_package(LLVM CONFIG QUIET)
if(LLVM_FOUND)
    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION} at ${LLVM_DIR}")
endif()

# ── Clang / libclang Discovery ─────────────────────────────────────────
find_path(CLANG_C_INCLUDE_DIR NAMES clang-c/Index.h
    PATHS
    "C:/Program Files/LLVM/include"
    ${LLVM_INCLUDE_DIRS}
    /usr/include
    /usr/local/include
    /opt/homebrew/include
)
find_library(CLANG_C_LIBRARY NAMES libclang clang
    PATHS
    "C:/Program Files/LLVM/lib"
    ${LLVM_LIBRARY_DIRS}
    /usr/lib
    /usr/local/lib
    /opt/homebrew/lib
)

if(CLANG_C_INCLUDE_DIR AND CLANG_C_LIBRARY)
    message(STATUS "Found libclang: ${CLANG_C_LIBRARY} (headers: ${CLANG_C_INCLUDE_DIR})")
    set(TAMARIN_HAS_BINDGEN ON)
else()
    message(WARNING "libclang not found; tamarin-bindgen will be disabled")
    set(TAMARIN_HAS_BINDGEN OFF)
endif()

# ── Configurable Product Name ─────────────────────────────────────────
set(TAMARIN_PRODUCT_NAME "Tamarin" CACHE STRING "Public product name")
set(TAMARIN_CODENAME "tamarin" CACHE STRING "Internal codename")
set(TAMARIN_FILE_EXTENSION ".tmn" CACHE STRING "Source file extension")

if(LLVM_FOUND AND EXISTS "${LLVM_TOOLS_BINARY_DIR}/clang")
    set(TAMARIN_CLANG_DETECT "${LLVM_TOOLS_BINARY_DIR}/clang")
elseif(LLVM_FOUND AND EXISTS "${LLVM_TOOLS_BINARY_DIR}/clang.exe")
    set(TAMARIN_CLANG_DETECT "${LLVM_TOOLS_BINARY_DIR}/clang.exe")
elseif(WIN32)
    find_program(TAMARIN_CLANG_DETECT NAMES clang.exe clang PATHS "C:/Program Files/LLVM/bin" ENV PATH)
else()
    find_program(TAMARIN_CLANG_DETECT NAMES clang clang-21 clang-20 clang-19 clang-18 clang-17 PATHS ENV PATH)
endif()

if(TAMARIN_CLANG_DETECT)
    set(TAMARIN_CLANG_PATH "${TAMARIN_CLANG_DETECT}" CACHE FILEPATH "Path to clang compiler executable")
else()
    if(WIN32)
        set(TAMARIN_CLANG_PATH "clang.exe" CACHE FILEPATH "Path to clang compiler executable")
    else()
        set(TAMARIN_CLANG_PATH "clang" CACHE FILEPATH "Path to clang compiler executable")
    endif()
endif()

configure_file(
    "${CMAKE_SOURCE_DIR}/include/tamarin/config.h.in"
    "${CMAKE_BINARY_DIR}/generated/tamarin/config.h"
)

# ── Include Directories ───────────────────────────────────────────────
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/troop/include
    ${CMAKE_SOURCE_DIR}/bindgen/include
    ${CMAKE_SOURCE_DIR}/lsp/include
    ${CMAKE_BINARY_DIR}/generated
    ${CLANG_C_INCLUDE_DIR}
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
target_link_libraries(tamarin_runtime PUBLIC Threads::Threads)
if(CMAKE_DL_LIBS)
    target_link_libraries(tamarin_runtime PUBLIC ${CMAKE_DL_LIBS})
endif()

# ── Tamarin Runtime Library Static CRT (for statically linked executables) ──
add_library(tamarin_runtime_static STATIC
    src/runtime/page_provider.cpp
    src/runtime/concurrency.cpp
)
target_include_directories(tamarin_runtime_static PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/generated
)
target_link_libraries(tamarin_runtime_static PUBLIC Threads::Threads)
if(CMAKE_DL_LIBS)
    target_link_libraries(tamarin_runtime_static PUBLIC ${CMAKE_DL_LIBS})
endif()
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

# ── Bindgen Library & Executable ──────────────────────────────────────
if(TAMARIN_HAS_BINDGEN)
    add_library(bindgen_lib STATIC
        bindgen/src/c_parser.cpp
        bindgen/src/tamarin_emitter.cpp
        bindgen/src/cli.cpp
    )
    target_include_directories(bindgen_lib PUBLIC
        ${CMAKE_SOURCE_DIR}/bindgen/include
        ${CLANG_C_INCLUDE_DIR}
    )
    target_link_libraries(bindgen_lib PUBLIC ${CLANG_C_LIBRARY})

    add_executable(tamarin-bindgen bindgen/src/main.cpp)
    target_link_libraries(tamarin-bindgen PRIVATE bindgen_lib)
    if(MSVC)
        set_target_properties(tamarin-bindgen PROPERTIES LINK_FLAGS "/STACK:8388608")
    endif()
endif()

# ── LSP Library & Executable ──────────────────────────────────────────
add_library(lsp_lib STATIC
    lsp/src/json.cpp
    lsp/src/transport.cpp
    lsp/src/vfs.cpp
    lsp/src/analyzer.cpp
    lsp/src/navigation.cpp
    lsp/src/completion.cpp
    lsp/src/server.cpp
    lsp/src/cli.cpp
)
target_include_directories(lsp_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/lsp/include
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/generated
)
target_link_libraries(lsp_lib PUBLIC tamarin_lib)

add_executable(tamarin-lsp lsp/src/main.cpp)
target_link_libraries(tamarin-lsp PRIVATE lsp_lib)
if(MSVC)
    set_target_properties(tamarin-lsp PROPERTIES LINK_FLAGS "/STACK:8388608")
endif()

# ── Compiler Executable ───────────────────────────────────────────────
add_executable(tamarin src/main.cpp)
if(TAMARIN_HAS_BINDGEN)
    target_link_libraries(tamarin PRIVATE tamarin_lib troop_lib bindgen_lib lsp_lib)
else()
    target_link_libraries(tamarin PRIVATE tamarin_lib troop_lib lsp_lib)
endif()

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
    tests/codegen/memory_safety_test.cpp
    tests/codegen/concurrency_soundness_test.cpp
    tests/codegen/ir_llvm_soundness_test.cpp
    tests/codegen/compiler_performance_test.cpp
    tests/modules/module_test.cpp
    tests/modules/stdlib_test.cpp
    troop/tests/troop_test.cpp
    tests/lsp/lsp_test.cpp
)

target_include_directories(tamarin_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/troop/include
    ${CMAKE_SOURCE_DIR}/bindgen/include
    ${CMAKE_SOURCE_DIR}/lsp/include
    ${CMAKE_BINARY_DIR}/generated
    ${CMAKE_SOURCE_DIR}/tests
)

if(TAMARIN_HAS_BINDGEN)
    target_sources(tamarin_tests PRIVATE tests/bindgen/bindgen_test.cpp)
    target_link_libraries(tamarin_tests PRIVATE tamarin_lib troop_lib bindgen_lib lsp_lib)
else()
    target_link_libraries(tamarin_tests PRIVATE tamarin_lib troop_lib lsp_lib)
endif()

if(MSVC)
    if(TAMARIN_HAS_BINDGEN)
        set_target_properties(tamarin troop tamarin_tests tamarin-bindgen tamarin-lsp PROPERTIES LINK_FLAGS "/STACK:8388608")
    else()
        set_target_properties(tamarin troop tamarin_tests tamarin-lsp PROPERTIES LINK_FLAGS "/STACK:8388608")
    endif()
endif()

add_test(NAME TamarinUnitTests COMMAND tamarin_tests)

# ── Install ───────────────────────────────────────────────────────────
if(TAMARIN_HAS_BINDGEN)
    install(TARGETS tamarin troop tamarin-bindgen tamarin-lsp RUNTIME DESTINATION bin)
else()
    install(TARGETS tamarin troop tamarin-lsp RUNTIME DESTINATION bin)
endif()
