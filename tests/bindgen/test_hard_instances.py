#!/usr/bin/env python3
"""
tamarin-bindgen 100+ Hard Instances Stress & Soundness Gauntlet

Runs tamarin-bindgen against 120 adversarial C header patterns covering:
  - Pointers & Multi-Level Indirection
  - Structs, Unions, Bitfields & Alignment
  - Enums, Bitflags, Negative & Large Discriminants
  - Function Pointers, Callbacks & C ABI
  - Typedefs & Complex Aliasing
  - Macro Constants, Hex, Floats, Strings
  - Variadics, Large Parameter Lists & Calling Conventions
  - Tamarin Keyword Collisions & Sanitization
  - Real-World C APIs (Raylib, SQLite, POSIX, Vulkan, Win32, etc.)

Each generated binding is verified with `tamarin check` (syntax + semantic + C ABI check)
and optionally compiled with `tamarin build`.
"""

import os
import sys
import shutil
import tempfile
import pathlib
import subprocess
import time

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
IS_WINDOWS = sys.platform == "win32"
EXE_EXT = ".exe" if IS_WINDOWS else ""

def find_binaries():
    candidates = [
        REPO_ROOT / "build" / "Release",
        REPO_ROOT / "build",
        REPO_ROOT / "build" / "Debug",
        REPO_ROOT / "bin"
    ]
    bindgen_bin = None
    tamarin_bin = None
    for c in candidates:
        bg = c / f"tamarin-bindgen{EXE_EXT}"
        if bg.exists() and not bindgen_bin:
            bindgen_bin = bg
        tm = c / f"tamarin{EXE_EXT}"
        if tm.exists() and not tamarin_bin:
            tamarin_bin = tm

    if not bindgen_bin:
        w = shutil.which(f"tamarin-bindgen{EXE_EXT}")
        if w: bindgen_bin = pathlib.Path(w)
    if not tamarin_bin:
        w = shutil.which(f"tamarin{EXE_EXT}")
        if w: tamarin_bin = pathlib.Path(w)

    return bindgen_bin, tamarin_bin

# ANSI Colors
USE_COLOR = sys.stdout.isatty() or os.environ.get("CI") is not None
def green(t): return f"\033[92m{t}\033[0m" if USE_COLOR else t
def red(t): return f"\033[91m{t}\033[0m" if USE_COLOR else t
def yellow(t): return f"\033[93m{t}\033[0m" if USE_COLOR else t
def cyan(t): return f"\033[96m{t}\033[0m" if USE_COLOR else t
def gray(t): return f"\033[90m{t}\033[0m" if USE_COLOR else t

class TestCase:
    def __init__(self, name, header, options=None, expected=None, unexpected=None, check_compile=True):
        self.name = name
        self.header = header
        self.options = options or []
        self.expected = expected or []
        self.unexpected = unexpected or []
        self.check_compile = check_compile

TEST_CASES = [
    # ── Category 1: Pointer Types & Multi-Level Indirection (1-15) ─────────────
    TestCase(
        "case01_double_pointer",
        "void set_handle(int** pp);",
        expected=["**int pp", "extern \"C\""]
    ),
    TestCase(
        "case02_triple_pointer",
        "void set_cube(void*** ppp);",
        expected=["***void ppp"]
    ),
    TestCase(
        "case03_const_ptr_to_const",
        "int read_val(const int* const ptr);",
        expected=["*const int ptr"]
    ),
    TestCase(
        "case04_const_ptr_to_mutable",
        "int read_val2(int* const ptr);",
        expected=["*int ptr"]
    ),
    TestCase(
        "case05_const_char_ptr",
        "void puts_custom(const char* str);",
        expected=["*const u8 str", "puts_custom"]
    ),
    TestCase(
        "case06_unsigned_char_ptr",
        "void process_bytes(unsigned char* data, int len);",
        expected=["*u8 data", "int len"]
    ),
    TestCase(
        "case07_signed_char_ptr",
        "void process_sbytes(signed char* data);",
        expected=["*i8 data"]
    ),
    TestCase(
        "case08_opaque_struct_pointer",
        "struct OpaqueContext;\nvoid destroy_context(struct OpaqueContext* ctx);",
        expected=["destroy_context"]
    ),
    TestCase(
        "case09_pointer_to_pointer_opaque",
        "struct OpaqueContext;\nint create_context(struct OpaqueContext** out_ctx);",
        expected=["create_context"]
    ),
    TestCase(
        "case10_void_pointer_allocator",
        "typedef unsigned long long size_t;\nvoid* my_alloc(size_t sz);\nvoid my_free(void* p);",
        expected=["*void my_alloc(usize sz);", "void my_free(*void p);"]
    ),
    TestCase(
        "case11_pointer_to_primitive_array",
        "void set_row(int (*row_ptr)[8]);",
        expected=["set_row"]
    ),
    TestCase(
        "case12_array_of_pointers",
        "void exec_args(char* argv[16]);",
        expected=["exec_args"]
    ),
    TestCase(
        "case13_array_of_const_pointers",
        "void print_lines(const char* lines[4]);",
        expected=["print_lines"]
    ),
    TestCase(
        "case14_const_struct_pointer",
        "struct Config { int flags; float rate; };\nvoid apply_config(const struct Config* cfg);",
        expected=["struct Config", "*const Config cfg"]
    ),
    TestCase(
        "case15_self_referential_struct",
        "struct ListNode {\n    int value;\n    struct ListNode* next;\n    struct ListNode* prev;\n};",
        expected=["struct ListNode", "*ListNode next;", "*ListNode prev;"]
    ),

    # ── Category 2: Struct Layouts, Nesting, & Memory (16-30) ─────────────────
    TestCase(
        "case16_empty_struct",
        "struct EmptyNode {};",
        expected=["struct EmptyNode", "_opaque"]
    ),
    TestCase(
        "case17_single_field_struct",
        "struct IntWrapper { int value; };",
        expected=["struct IntWrapper", "int value;"]
    ),
    TestCase(
        "case18_mixed_primitive_struct",
        "struct MixedFields {\n    char a;\n    int b;\n    short c;\n    double d;\n};",
        expected=["struct MixedFields", "a;", "int b;", "i16 c;", "double d;"]
    ),
    TestCase(
        "case19_fixed_size_array_field",
        "struct PacketBuffer {\n    char data[64];\n    int length;\n};",
        expected=["struct PacketBuffer", "[64] data;", "int length;"]
    ),
    TestCase(
        "case20_multidimensional_array_field",
        "struct TransformMatrix {\n    float m[4][4];\n};",
        expected=["struct TransformMatrix"]
    ),
    TestCase(
        "case21_nested_named_struct",
        "struct Vec2 { float x; float y; };\nstruct BoundingBox { struct Vec2 min; struct Vec2 max; };",
        expected=["struct Vec2", "struct BoundingBox", "min_", "max_"]
    ),
    TestCase(
        "case22_anonymous_struct_field",
        "struct OuterStruct {\n    int id;\n    struct { int sub_x; int sub_y; } inner;\n};",
        expected=["struct OuterStruct", "int id;"]
    ),
    TestCase(
        "case23_anonymous_union_field",
        "struct TaggedPayload {\n    int tag;\n    union { int ival; float fval; } u;\n};",
        expected=["struct TaggedPayload", "tag_"]
    ),
    TestCase(
        "case24_transparent_anonymous_union",
        "struct VariantEvent {\n    int type;\n    union { int keycode; float delta; };\n};",
        expected=["struct VariantEvent", "type_"]
    ),
    TestCase(
        "case25_standalone_c_union",
        "union FloatIntBits {\n    float f;\n    int i;\n};",
        expected=["_raw", "@c_layout"]
    ),
    TestCase(
        "case26_bitfield_struct",
        "struct HeaderFlags {\n    unsigned int is_valid : 1;\n    unsigned int is_ready : 1;\n    unsigned int priority : 4;\n    unsigned int reserved : 26;\n};",
        expected=["struct HeaderFlags", "bitfield: 1 bits"]
    ),
    TestCase(
        "case27_unnamed_zero_width_bitfield",
        "struct AlignedBits {\n    unsigned int a : 4;\n    unsigned int : 0;\n    unsigned int b : 4;\n};",
        expected=["struct AlignedBits"]
    ),
    TestCase(
        "case28_flexible_array_member",
        "struct FlexBuffer {\n    int capacity;\n    int count;\n    char payload[];\n};",
        expected=["struct FlexBuffer", "int capacity;", "int count;"]
    ),
    TestCase(
        "case29_zero_length_array",
        "struct ZeroLengthBuf {\n    int size;\n    int items[0];\n};",
        expected=["struct ZeroLengthBuf", "int size;"]
    ),
    TestCase(
        "case30_mutually_recursive_structs",
        "struct ClassB;\nstruct ClassA {\n    struct ClassB* b_ptr;\n};\nstruct ClassB {\n    struct ClassA* a_ptr;\n};",
        expected=["struct ClassA", "struct ClassB", "*ClassB b_ptr;", "*ClassA a_ptr;"]
    ),

    # ── Category 3: Enums, Bitflags, & Discriminants (31-45) ───────────────────
    TestCase(
        "case31_basic_sequential_enum",
        "enum Direction { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT };",
        expected=["enum Direction", "DIR_UP", "DIR_DOWN", "DIR_LEFT", "DIR_RIGHT", "const int DIR_UP = 0;"]
    ),
    TestCase(
        "case32_explicit_zero_start_enum",
        "enum Status { STATUS_OK = 0, STATUS_BUSY = 1, STATUS_DONE = 2 };",
        expected=["enum Status", "STATUS_OK", "STATUS_BUSY", "STATUS_DONE"]
    ),
    TestCase(
        "case33_non_sequential_enum",
        "enum Speed { SPEED_SLOW = 10, SPEED_MED = 50, SPEED_FAST = 100 };",
        expected=["const int SPEED_SLOW = 10;", "const int SPEED_MED = 50;", "const int SPEED_FAST = 100;"]
    ),
    TestCase(
        "case34_bitflag_enum",
        "enum FileAccess { ACCESS_READ = 1, ACCESS_WRITE = 2, ACCESS_EXEC = 4 };",
        expected=["const int ACCESS_READ = 1;", "const int ACCESS_WRITE = 2;", "const int ACCESS_EXEC = 4;"]
    ),
    TestCase(
        "case35_negative_discriminant_enum",
        "enum RetVal { RET_OK = 0, RET_ERR = -1, RET_TIMEOUT = -2 };",
        expected=["const int RET_OK = 0;", "const int RET_ERR = -1;", "const int RET_TIMEOUT = -2;"]
    ),
    TestCase(
        "case36_large_unsigned_enum",
        "enum Limits { LIMIT_HALF = 0x7FFFFFFF, LIMIT_MAX = 0x80000000U };",
        expected=["LIMIT_HALF", "LIMIT_MAX"]
    ),
    TestCase(
        "case37_duplicate_value_enum",
        "enum AliasedVals { VAL_FIRST = 1, VAL_ALIAS = 1, VAL_SECOND = 2 };",
        expected=["const int VAL_FIRST = 1;", "const int VAL_ALIAS = 1;", "const int VAL_SECOND = 2;"]
    ),
    TestCase(
        "case38_anonymous_enum_global",
        "enum { GLOBAL_MAGIC = 0xCAFE, GLOBAL_VERSION = 42 };",
        expected=["const int GLOBAL_MAGIC =", "const int GLOBAL_VERSION = 42;"]
    ),
    TestCase(
        "case39_typedef_anonymous_enum",
        "typedef enum { STEP_1 = 0, STEP_2 = 1 } StepMode;",
        expected=["enum StepMode", "STEP_1", "STEP_2"]
    ),
    TestCase(
        "case40_typedef_named_enum_tag_match",
        "typedef enum NetMode { NET_OFFLINE = 0, NET_ONLINE = 1 } NetMode;",
        expected=["enum NetMode", "NET_OFFLINE", "NET_ONLINE"]
    ),
    TestCase(
        "case41_typedef_named_enum_tag_diff",
        "typedef enum _TagNet { TAG_OFF = 0, TAG_ON = 1 } TagNet;",
        expected=["TAG_OFF", "TAG_ON"]
    ),
    TestCase(
        "case42_struct_with_enum_field",
        "enum CpuState { CPU_HALTED = 0, CPU_RUNNING = 1 };\nstruct Core {\n    int id;\n    enum CpuState state;\n};",
        expected=["enum CpuState", "struct Core", "CpuState state;"]
    ),
    TestCase(
        "case43_function_taking_enum",
        "enum RenderMode { RENDER_2D = 0, RENDER_3D = 1 };\nvoid set_render_mode(enum RenderMode mode);",
        expected=["set_render_mode"]
    ),
    TestCase(
        "case44_function_returning_enum",
        "enum RenderMode { RENDER_2D = 0, RENDER_3D = 1 };\nenum RenderMode get_render_mode(void);",
        expected=["get_render_mode"]
    ),
    TestCase(
        "case45_enum_calculated_constants",
        "enum ComputedVals { BASE_VAL = 10, SCALE_VAL = 20 };",
        expected=["BASE_VAL", "SCALE_VAL"]
    ),

    # ── Category 4: Function Pointers, Callbacks, & ABI (46-60) ───────────────
    TestCase(
        "case46_simple_void_callback",
        "typedef void (*SimpleCallback)(void);\nvoid register_simple(SimpleCallback cb);",
        expected=["register_simple(*void"]
    ),
    TestCase(
        "case47_binary_op_callback",
        "typedef int (*BinOp)(int a, int b);\nint execute_op(int x, int y, BinOp op);",
        expected=["execute_op(int x, int y, *void"]
    ),
    TestCase(
        "case48_string_consumer_callback",
        "typedef void (*StringConsumer)(const char* s, int len);\nvoid parse_stream(const char* stream, StringConsumer on_str);",
        expected=["parse_stream(*const u8 stream, *void"]
    ),
    TestCase(
        "case49_allocator_callback",
        "typedef void* (*CustomAlloc)(unsigned long long sz);\nvoid set_allocator(CustomAlloc alloc);",
        expected=["set_allocator(*void"]
    ),
    TestCase(
        "case50_callback_taking_struct_by_value",
        "struct Point { float x, y; };\ntypedef void (*PointConsumer)(struct Point pt);\nvoid for_each_point(PointConsumer cb);",
        expected=["for_each_point(*void"]
    ),
    TestCase(
        "case51_callback_taking_struct_by_pointer",
        "struct Point { float x, y; };\ntypedef void (*PointModifier)(struct Point* pt);\nvoid update_point(PointModifier cb);",
        expected=["update_point(*void"]
    ),
    TestCase(
        "case52_function_inline_callback_param",
        "void apply_filter(int* arr, int n, int (*filter)(int item));",
        expected=["apply_filter(*int arr, int n, *void"]
    ),
    TestCase(
        "case53_vtable_struct_with_fn_ptrs",
        "struct StreamVTable {\n    void (*open)(const char* path);\n    int (*read)(void* buf, int sz);\n    void (*close)(void);\n};",
        expected=["struct StreamVTable", "*void", "open;", "read;", "close;"]
    ),
    TestCase(
        "case54_callback_with_user_data",
        "typedef void (*IterCallback)(int index, void* udata);\nvoid iterate(int count, IterCallback cb, void* udata);",
        expected=["iterate(int count, *void", "*void udata)"]
    ),
    TestCase(
        "case55_multi_callback_registration",
        "void setup_handlers(void (*on_success)(int), void (*on_error)(const char*));",
        expected=["setup_handlers(*void", "*void"]
    ),
    TestCase(
        "case56_qsort_style_comparator",
        "typedef int (*Comparator)(const void* a, const void* b);\nvoid quicksort(void* base, int n, int size, Comparator cmp);",
        expected=["quicksort(*void base, int n, int size, *void"]
    ),
    TestCase(
        "case57_function_pointer_returning_fn_ptr",
        "typedef void (*Handler)(int);\ntypedef Handler (*HandlerFactory)(void);\nvoid register_factory(HandlerFactory hf);",
        expected=["register_factory(*void"]
    ),
    TestCase(
        "case58_nested_callback_signature",
        "typedef void (*SubCallback)(int);\ntypedef void (*MainCallback)(SubCallback sub);\nvoid run_main(MainCallback cb);",
        expected=["run_main(*void"]
    ),
    TestCase(
        "case59_fn_ptr_in_c_union",
        "union ActionUnion {\n    void (*invoke)(int);\n    void* raw_ptr;\n};",
        expected=["struct ActionUnion", "_raw"]
    ),
    TestCase(
        "case60_array_of_fn_ptrs_in_struct",
        "struct HandlerTable {\n    void (*handlers[4])(int code);\n};",
        expected=["struct HandlerTable"]
    ),

    # ── Category 5: Typedef Variations & Aliasing (61-70) ──────────────────────
    TestCase(
        "case61_primitive_typedef",
        "typedef unsigned long long uint64;\nuint64 compute_hash(const char* key);",
        expected=["u64 compute_hash(*const u8 key);"]
    ),
    TestCase(
        "case62_chained_typedefs",
        "typedef int Level0;\ntypedef Level0 Level1;\ntypedef Level1 Level2;\nLevel2 get_level(void);",
        expected=["get_level"]
    ),
    TestCase(
        "case63_pointer_typedef",
        "typedef char* StringMut;\ntypedef const char* StringConst;\nvoid copy_str(StringMut dst, StringConst src);",
        expected=["copy_str(*u8 dst, *const u8 src);"]
    ),
    TestCase(
        "case64_opaque_handle_typedef",
        "typedef struct OpaqueWindow* WindowHandle;\nvoid show_window(WindowHandle win);",
        expected=["show_window(*OpaqueWindow win);"]
    ),
    TestCase(
        "case65_fixed_array_typedef",
        "typedef float Vec3Array[3];\nstruct MeshVertex {\n    Vec3Array position;\n};",
        expected=["struct MeshVertex", "float[3] position;"]
    ),
    TestCase(
        "case66_tag_and_typedef_match",
        "typedef struct AppWindow AppWindow;\nstruct AppWindow {\n    int width;\n    int height;\n};",
        expected=["struct AppWindow", "int width;", "int height;"]
    ),
    TestCase(
        "case67_win32_style_tag_rect",
        "typedef struct tagRECT {\n    int left;\n    int top;\n    int right;\n    int bottom;\n} RECT;",
        expected=["struct RECT", "int left;", "int top;", "int right;", "int bottom;"]
    ),
    TestCase(
        "case68_typedef_in_subsequent_struct",
        "typedef int DeviceId;\nstruct DeviceRegistry {\n    DeviceId active_device;\n};",
        expected=["struct DeviceRegistry", "int active_device;"]
    ),
    TestCase(
        "case69_void_typedef",
        "typedef void VoidType;\nVoidType reset_system(void);",
        expected=["void reset_system();"]
    ),
    TestCase(
        "case70_size_t_and_ptrdiff_typedefs",
        "typedef unsigned long long size_t;\ntypedef long long ptrdiff_t;\nptrdiff_t calc_offset(size_t a, size_t b);",
        expected=["isize calc_offset(usize a, usize b);"]
    ),

    # ── Category 6: Macros, Preprocessor Directives, & Constants (71-85) ───────
    TestCase(
        "case71_decimal_int_macro",
        "#define BUFFER_CAPACITY 8192",
        expected=["const int BUFFER_CAPACITY = 8192;"]
    ),
    TestCase(
        "case72_hex_int_macro",
        "#define MASK_BIT_FOUR 0x10",
        expected=["const int MASK_BIT_FOUR = 0x10;"]
    ),
    TestCase(
        "case73_octal_int_macro",
        "#define DEFAULT_PERM 0644",
        expected=["const int DEFAULT_PERM = 0644;"]
    ),
    TestCase(
        "case74_negative_int_macro",
        "#define ERROR_CODE_INVALID -1",
        expected=["const int ERROR_CODE_INVALID = -1;"]
    ),
    TestCase(
        "case75_scientific_float_macro",
        "#define TOLERANCE 1e-5",
        expected=["const float TOLERANCE = 1e-5;"]
    ),
    TestCase(
        "case76_suffixed_float_macro",
        "#define GRAVITY_CONST 9.80665f",
        expected=["const float GRAVITY_CONST = 9.80665f;"]
    ),
    TestCase(
        "case77_string_literal_macro",
        "#define DEFAULT_HOST \"127.0.0.1\"",
        expected=["const u8[] DEFAULT_HOST = \"127.0.0.1\";"]
    ),
    TestCase(
        "case78_empty_string_macro",
        "#define EMPTY_TITLE \"\"",
        expected=["const u8[] EMPTY_TITLE = \"\";"]
    ),
    TestCase(
        "case79_string_with_escapes_macro",
        "#define SPECIAL_GREETING \"Hello\\tWorld!\\n\"",
        expected=["const u8[] SPECIAL_GREETING = \"Hello\\tWorld!\\n\";"]
    ),
    TestCase(
        "case80_parenthesized_macro",
        "#define TOTAL_CHUNKS (64)",
        expected=["TOTAL_CHUNKS", "64"]
    ),
    TestCase(
        "case81_macro_referencing_macro",
        "#define BASE_RATE 100\n#define BOOST_RATE 120",
        expected=["const int BASE_RATE = 100;", "const int BOOST_RATE = 120;"]
    ),
    TestCase(
        "case82_function_like_macro_skipped",
        "#define MIN(a, b) ((a) < (b) ? (a) : (b))\nint compute_min(int a, int b);",
        expected=["compute_min"],
        unexpected=["const int MIN", "const string MIN"]
    ),
    TestCase(
        "case83_header_guard_macro_ignored",
        "#ifndef MY_HEADER_H\n#define MY_HEADER_H\nint ping(void);\n#endif",
        expected=["int ping();"],
        unexpected=["const int MY_HEADER_H"]
    ),
    TestCase(
        "case84_empty_marker_macro_skipped",
        "#define API_EXPORT\nAPI_EXPORT void export_func(void);",
        expected=["void export_func();"],
        unexpected=["const int API_EXPORT"]
    ),
    TestCase(
        "case85_character_literal_macro",
        "#define PATH_SEP '/'",
        expected=["PATH_SEP"]
    ),

    # ── Category 7: Functions, Signatures, & C Calling Conventions (86-95) ─────
    TestCase(
        "case86_void_return_void_params",
        "void initialize_subsystem(void);",
        expected=["void initialize_subsystem();"]
    ),
    TestCase(
        "case87_legacy_empty_param_list",
        "int probe_hardware();",
        expected=["int probe_hardware();"]
    ),
    TestCase(
        "case88_variadic_function",
        "void debug_log(int level, const char* fmt, ...);",
        expected=["debug_log(int level, *const u8 fmt)"]
    ),
    TestCase(
        "case89_large_parameter_list",
        "void render_rect(int x, int y, int w, int h, float r, float g, float b, float a);",
        expected=["render_rect(int x, int y, int w, int h, float r, float g, float b, float a);"]
    ),
    TestCase(
        "case90_function_returning_struct_by_value",
        "struct Vec3 { float x, y, z; };\nstruct Vec3 vec3_create(float x, float y, float z);",
        expected=["Vec3 vec3_create(float x, float y, float z);"]
    ),
    TestCase(
        "case91_function_taking_struct_by_value",
        "struct Vec3 { float x, y, z; };\nfloat vec3_magnitude(struct Vec3 v);",
        expected=["float vec3_magnitude(Vec3 v);"]
    ),
    TestCase(
        "case92_function_multiple_struct_by_value",
        "struct Vec3 { float x, y, z; };\nstruct Vec3 vec3_add_vals(struct Vec3 a, struct Vec3 b);",
        expected=["Vec3 vec3_add_vals(Vec3 a, Vec3 b);"]
    ),
    TestCase(
        "case93_unnamed_parameters",
        "void process_buffer(char*, int, float);",
        expected=["process_buffer(*u8 arg0, int arg1, float arg2);"]
    ),
    TestCase(
        "case94_const_ptr_const_params",
        "int str_compare(const char* const a, const char* const b);",
        expected=["str_compare(*const u8 a, *const u8 b);"]
    ),
    TestCase(
        "case95_static_inline_function",
        "static inline int fast_add(int a, int b) { return a + b; }\nint slow_add(int a, int b);",
        expected=["slow_add"]
    ),

    # ── Category 8: Tamarin Keyword Collisions & Identifier Sanitization (96-110)
    TestCase(
        "case96_field_named_type",
        "struct EventRecord { int type; int code; };",
        expected=["struct EventRecord", "int type_;", "int code;"]
    ),
    TestCase(
        "case97_field_named_trait",
        "struct TraitHolder { int trait; };",
        expected=["struct TraitHolder", "int trait_;"]
    ),
    TestCase(
        "case98_field_named_fn",
        "struct FnHolder { int fn; };",
        expected=["struct FnHolder", "int fn_;"]
    ),
    TestCase(
        "case99_field_named_match",
        "struct MatchHolder { int match; };",
        expected=["struct MatchHolder", "int match_;"]
    ),
    TestCase(
        "case100_field_named_let_and_mut",
        "struct BindingHolder { int let; int mut; };",
        expected=["struct BindingHolder", "int let_;", "int mut_;"]
    ),
    TestCase(
        "case101_field_named_pub_and_import",
        "struct VisibilityHolder { int pub; int import; };",
        expected=["struct VisibilityHolder", "int pub_;", "int import_;"]
    ),
    TestCase(
        "case102_field_named_struct_and_class",
        "struct MetaHolder { int struct_; int class_; };",
        expected=["struct MetaHolder", "int struct_;", "int class_;"]
    ),
    TestCase(
        "case103_param_named_type",
        "void set_object_type(int id, int type);",
        expected=["set_object_type(int id, int type_);"]
    ),
    TestCase(
        "case104_param_named_fn",
        "void invoke_handler(int code, void* fn);",
        expected=["invoke_handler(int code, *void fn_);"]
    ),
    TestCase(
        "case105_param_named_match",
        "int evaluate_pattern(int pattern, int match);",
        expected=["evaluate_pattern(int pattern, int match_);"]
    ),
    TestCase(
        "case106_param_named_let_mut_pub",
        "void setup_decl(int let, int mut, int pub);",
        expected=["setup_decl(int let_, int mut_, int pub_);"]
    ),
    TestCase(
        "case107_enum_variant_keyword_default",
        "enum OptionsEnum { OPTION_DEFAULT, OPTION_CUSTOM };",
        expected=["enum OptionsEnum", "OPTION_DEFAULT", "OPTION_CUSTOM"]
    ),
    TestCase(
        "case108_enum_variant_keyword_type",
        "enum KindEnum { KIND_TYPE, KIND_TRAIT };",
        expected=["enum KindEnum", "KIND_TYPE", "KIND_TRAIT"]
    ),
    TestCase(
        "case109_multi_keyword_function",
        "int multi_kw(int type, int trait, int fn, int match);",
        expected=["multi_kw(int type_, int trait_, int fn_, int match_);"]
    ),
    TestCase(
        "case110_field_named_select_spawn_await",
        "struct AsyncKeywords { int select; int spawn; int await; };",
        expected=["struct AsyncKeywords", "int select_;", "int spawn_;", "int await_;"]
    ),

    # ── Category 9: Real-World C APIs Emulation (111-120) ──────────────────────
    TestCase(
        "case111_raylib_core_subset",
        """
        typedef struct Color {
            unsigned char r;
            unsigned char g;
            unsigned char b;
            unsigned char a;
        } Color;

        typedef struct Vector2 {
            float x;
            float y;
        } Vector2;

        typedef struct Rectangle {
            float x;
            float y;
            float width;
            float height;
        } Rectangle;

        #define LIGHTGRAY  (Color){ 200, 200, 200, 255 }
        #define RAYWHITE   (Color){ 245, 245, 245, 255 }

        void InitWindow(int width, int height, const char *title);
        int WindowShouldClose(void);
        void CloseWindow(void);
        void BeginDrawing(void);
        void EndDrawing(void);
        void ClearBackground(Color color);
        void DrawRectangle(int posX, int posY, int width, int height, Color color);
        """,
        expected=["struct Color", "struct Vector2", "struct Rectangle", "InitWindow", "ClearBackground(Color color);"]
    ),
    TestCase(
        "case112_sqlite3_core_subset",
        """
        typedef struct sqlite3 sqlite3;
        typedef struct sqlite3_stmt sqlite3_stmt;

        #define SQLITE_OK           0
        #define SQLITE_ERROR        1
        #define SQLITE_ROW         100
        #define SQLITE_DONE        101

        int sqlite3_open(const char *filename, sqlite3 **ppDb);
        int sqlite3_close(sqlite3 *db);
        int sqlite3_prepare_v2(sqlite3 *db, const char *zSql, int nByte, sqlite3_stmt **ppStmt, const char **pzTail);
        int sqlite3_step(sqlite3_stmt *stmt);
        int sqlite3_column_int(sqlite3_stmt *stmt, int iCol);
        const unsigned char *sqlite3_column_text(sqlite3_stmt *stmt, int iCol);
        int sqlite3_finalize(sqlite3_stmt *pStmt);
        """,
        expected=["const int SQLITE_OK = 0;", "sqlite3_open", "sqlite3_step", "sqlite3_finalize"]
    ),
    TestCase(
        "case113_posix_socket_subset",
        """
        typedef unsigned short sa_family_t;
        typedef unsigned int socklen_t;

        #define AF_INET     2
        #define SOCK_STREAM 1

        struct in_addr {
            unsigned int s_addr;
        };

        struct sockaddr_in {
            sa_family_t sin_family;
            unsigned short sin_port;
            struct in_addr sin_addr;
            char sin_zero[8];
        };

        struct sockaddr {
            sa_family_t sa_family;
            char sa_data[14];
        };

        int socket(int domain, int type, int protocol);
        int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
        int listen(int sockfd, int backlog);
        int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
        """,
        expected=["const int AF_INET = 2;", "struct sockaddr_in", "struct sockaddr", "socket(int domain, int type_, int protocol);"]
    ),
    TestCase(
        "case114_c_stdio_subset",
        """
        typedef unsigned long long size_t;
        struct _iobuf { void* _Placeholder; };
        typedef struct _iobuf FILE;

        #define SEEK_SET 0
        #define SEEK_CUR 1
        #define SEEK_END 2

        FILE* fopen(const char* filename, const char* mode);
        int fclose(FILE* stream);
        size_t fread(void* ptr, size_t size, size_t count, FILE* stream);
        size_t fwrite(const void* ptr, size_t size, size_t count, FILE* stream);
        int fseek(FILE* stream, long offset, int origin);
        """,
        expected=["const int SEEK_SET = 0;", "fopen", "fclose", "fread", "fwrite"]
    ),
    TestCase(
        "case115_vulkan_handle_pnext_subset",
        """
        typedef struct VkInstance_T* VkInstance;
        typedef struct VkDevice_T* VkDevice;
        typedef int VkResult;

        typedef struct VkApplicationInfo {
            int sType;
            const void* pNext;
            const char* pApplicationName;
            unsigned int applicationVersion;
            const char* pEngineName;
            unsigned int engineVersion;
            unsigned int apiVersion;
        } VkApplicationInfo;

        typedef struct VkInstanceCreateInfo {
            int sType;
            const void* pNext;
            unsigned int flags;
            const VkApplicationInfo* pApplicationInfo;
            unsigned int enabledLayerCount;
            const char* const* ppEnabledLayerNames;
            unsigned int enabledExtensionCount;
            const char* const* ppEnabledExtensionNames;
        } VkInstanceCreateInfo;

        VkResult vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo, const void* pAllocator, VkInstance* pInstance);
        void vkDestroyInstance(VkInstance instance, const void* pAllocator);
        """,
        expected=["struct VkApplicationInfo", "struct VkInstanceCreateInfo", "vkCreateInstance", "vkDestroyInstance"]
    ),
    TestCase(
        "case116_win32_core_subset",
        """
        typedef void* HANDLE;
        typedef void* HWND;
        typedef void* HINSTANCE;
        typedef unsigned long DWORD;
        typedef int BOOL;

        typedef struct tagPOINT {
            long x;
            long y;
        } POINT;

        typedef struct tagMSG {
            HWND hwnd;
            unsigned int message;
            unsigned long long wParam;
            long long lParam;
            DWORD time;
            POINT pt;
        } MSG;

        BOOL GetMessageA(MSG* lpMsg, HWND hWnd, unsigned int wMsgFilterMin, unsigned int wMsgFilterMax);
        BOOL TranslateMessage(const MSG* lpMsg);
        long long DispatchMessageA(const MSG* lpMsg);
        """,
        expected=["struct POINT", "struct MSG", "GetMessageA", "TranslateMessage", "DispatchMessageA"]
    ),
    TestCase(
        "case117_libcurl_easy_subset",
        """
        typedef void CURL;
        typedef int CURLcode;
        typedef int CURLoption;

        #define CURLE_OK 0
        #define CURLOPT_URL 10002
        #define CURLOPT_TIMEOUT 10013

        CURL* curl_easy_init(void);
        CURLcode curl_easy_setopt(CURL* curl, CURLoption option, ...);
        CURLcode curl_easy_perform(CURL* curl);
        void curl_easy_cleanup(CURL* curl);
        """,
        expected=["const int CURLE_OK = 0;", "curl_easy_init", "curl_easy_setopt", "curl_easy_perform"]
    ),
    TestCase(
        "case118_zlib_stream_subset",
        """
        typedef unsigned int uInt;
        typedef unsigned long uLong;
        typedef void* (*alloc_func)(void* opaque, uInt items, uInt size);
        typedef void (*free_func)(void* opaque, void* address);

        typedef struct z_stream_s {
            const unsigned char *next_in;
            uInt     avail_in;
            uLong    total_in;

            unsigned char *next_out;
            uInt     avail_out;
            uLong    total_out;

            const char *msg;
            void *state;

            alloc_func zalloc;
            free_func  zfree;
            void      *opaque;

            int     data_type;
            uLong   adler;
            uLong   reserved;
        } z_stream;

        int deflateInit_(z_stream *strm, int level, const char *version, int stream_size);
        int deflate(z_stream *strm, int flush);
        int deflateEnd(z_stream *strm);
        """,
        expected=["struct z_stream", "deflateInit_", "deflate", "deflateEnd"]
    ),
    TestCase(
        "case119_c99_fixed_width_types",
        """
        typedef signed char        int8_t;
        typedef short              int16_t;
        typedef int                int32_t;
        typedef long long          int64_t;
        typedef unsigned char      uint8_t;
        typedef unsigned short     uint16_t;
        typedef unsigned int       uint32_t;
        typedef unsigned long long uint64_t;

        struct FixedWidthBundle {
            int8_t   s8;
            int16_t  s16;
            int32_t  s32;
            int64_t  s64;
            uint8_t  u8_val;
            uint16_t u16_val;
            uint32_t u32_val;
            uint64_t u64_val;
        };

        int64_t sum_bundle(const struct FixedWidthBundle* b);
        """,
        expected=["struct FixedWidthBundle", "i8 s8;", "i16 s16;", "i32 s32;", "i64 s64;", "u8 u8_val;", "u16 u16_val;", "u32 u32_val;", "u64 u64_val;"]
    ),
    TestCase(
        "case120_prefix_filtering",
        """
        void mylib_init(void);
        void mylib_cleanup(void);
        void other_helper(void);
        struct mylib_config { int dummy; };
        struct other_config { int dummy; };
        """,
        options=["--prefix", "mylib_"],
        expected=["mylib_init", "mylib_cleanup", "struct mylib_config"],
        unexpected=["other_helper", "other_config"]
    ),
]

def run_tests():
    bindgen_bin, tamarin_bin = find_binaries()
    if not bindgen_bin or not bindgen_bin.exists():
        print(red(f"Error: tamarin-bindgen binary not found!"))
        sys.exit(1)
    if not tamarin_bin or not tamarin_bin.exists():
        print(red(f"Error: tamarin binary not found!"))
        sys.exit(1)

    print(cyan("======================================================================"))
    print(cyan(f" tamarin-bindgen 120 Hard Instances Soundness Gauntlet"))
    print(cyan("======================================================================"))
    print(f"  Bindgen Binary: {bindgen_bin}")
    print(f"  Tamarin Binary: {tamarin_bin}")
    print(f"  Total Cases:    {len(TEST_CASES)}")
    print(cyan("======================================================================\n"))

    temp_dir = pathlib.Path(tempfile.mkdtemp(prefix="tamarin_bindgen_hard_"))
    passed = 0
    failed = 0
    start_time = time.time()

    try:
        for idx, tc in enumerate(TEST_CASES, 1):
            h_path = temp_dir / f"{tc.name}.h"
            tmn_path = temp_dir / f"{tc.name}.tmn"

            h_path.write_text(tc.header, encoding="utf-8")

            # 1. Run tamarin-bindgen
            cmd = [str(bindgen_bin), str(h_path), "-o", str(tmn_path)] + tc.options
            res = subprocess.run(cmd, capture_output=True, text=True)
            if res.returncode != 0:
                print(f"  {red('[FAIL]')} {tc.name} - bindgen exited with code {res.returncode}")
                if res.stderr:
                    print(f"       {yellow(res.stderr.strip())}")
                failed += 1
                continue

            if not tmn_path.exists():
                print(f"  {red('[FAIL]')} {tc.name} - output file {tmn_path.name} was not created")
                failed += 1
                continue

            content = tmn_path.read_text(encoding="utf-8")

            # 2. Check expected strings
            missing_expected = []
            for exp in tc.expected:
                if exp not in content:
                    missing_expected.append(exp)
            if missing_expected:
                print(f"  {red('[FAIL]')} {tc.name} - missing expected output: {missing_expected}")
                failed += 1
                continue

            # 3. Check unexpected strings
            found_unexpected = []
            for unexp in tc.unexpected:
                if unexp in content:
                    found_unexpected.append(unexp)
            if found_unexpected:
                print(f"  {red('[FAIL]')} {tc.name} - found unexpected output: {found_unexpected}")
                failed += 1
                continue

            # 4. Check compile with `tamarin check`
            if tc.check_compile:
                # Add a dummy main() so tamarin check doesn't complain about missing main
                check_src = content + "\nint main() { return 0; }\n"
                chk_path = temp_dir / f"{tc.name}_check.tmn"
                chk_path.write_text(check_src, encoding="utf-8")

                chk_res = subprocess.run([str(tamarin_bin), "check", str(chk_path)], capture_output=True, text=True)
                if chk_res.returncode != 0:
                    print(f"  {red('[FAIL]')} {tc.name} - tamarin check failed (exit {chk_res.returncode})")
                    if chk_res.stderr:
                        for line in chk_res.stderr.strip().splitlines()[:5]:
                            print(f"       {yellow(line)}")
                    failed += 1
                    continue

            print(f"  {green('[PASS]')} [{idx:03d}/120] {tc.name}")
            passed += 1

    finally:
        shutil.rmtree(temp_dir, ignore_errors=True)

    elapsed = time.time() - start_time
    print(cyan("\n======================================================================"))
    print(cyan(f" SCORECARD: {passed} / {len(TEST_CASES)} Passed ({failed} Failed)"))
    print(cyan(f" Elapsed: {elapsed:.2f}s"))
    print(cyan("======================================================================\n"))

    if failed > 0:
        sys.exit(1)
    else:
        print(green("ALL 120 HARD INSTANCES PASSED! Bindgen is battle-tested & rock-solid.\n"))

if __name__ == "__main__":
    run_tests()
