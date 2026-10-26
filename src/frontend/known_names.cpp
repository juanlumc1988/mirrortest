// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/frontend/known_names.h"

#include <algorithm>

namespace mt::frontend {

namespace {

using Name = std::string_view;

// Every table is sorted -- the compiler checks, below -- and searched by bisection.
template <std::size_t N>
constexpr bool is_sorted_unique(const Name (&table)[N]) {
    for (std::size_t i = 1; i < N; ++i) {
        if (!(table[i - 1] < table[i])) return false;
    }
    return true;
}

template <std::size_t N>
bool contains(const Name (&table)[N], Name name) {
    const Name* end = table + N;
    const Name* it = std::lower_bound(table, end, name);
    return it != end && *it == name;
}

bool is_upper(char c) { return c >= 'A' && c <= 'Z'; }
bool is_lower(char c) { return c >= 'a' && c <= 'z'; }
bool is_digit(char c) { return c >= '0' && c <= '9'; }

constexpr Name kStandardCTypes[] = {
    "FILE", "clock_t", "div_t", "fpos_t", "int16_t", "int32_t", "int64_t", "int8_t",
    "int_fast16_t", "int_fast32_t", "int_fast64_t", "int_fast8_t", "int_least16_t", "int_least32_t",
    "int_least64_t", "int_least8_t", "intmax_t", "intptr_t", "jmp_buf", "ldiv_t", "lldiv_t",
    "max_align_t", "mbstate_t", "nullptr_t", "off_t", "ptrdiff_t", "qint16", "qint32", "qint64", "qint8",
    "qintptr", "qlonglong", "qptrdiff", "qreal", "qsizetype", "quint16", "quint32", "quint64", "quint8",
    "quintptr", "qulonglong", "sig_atomic_t", "size_t", "ssize_t", "time_t", "tm", "uchar", "uint",
    "uint16_t", "uint32_t", "uint64_t", "uint8_t", "uint_fast16_t", "uint_fast32_t", "uint_fast64_t",
    "uint_fast8_t", "uint_least16_t", "uint_least32_t", "uint_least64_t", "uint_least8_t", "uintmax_t",
    "uintptr_t", "ulong", "ushort", "va_list", "wchar_t", "wint_t",
};

constexpr Name kStdTypes[] = {
    "allocator", "any", "array", "atomic", "bad_alloc", "bad_cast", "basic_string", "bitset", "byte",
    "condition_variable", "deque", "domain_error", "equal_to", "exception", "fstream",
    "function", "greater", "hash", "ifstream", "initializer_list", "invalid_argument", "iostream",
    "istream", "istringstream", "length_error", "less", "list", "logic_error", "map", "minus", "multimap",
    "multiset", "mutex", "nullopt_t", "ofstream", "optional", "ostream", "ostringstream", "out_of_range",
    "overflow_error", "pair", "plus", "priority_queue", "queue", "range_error", "recursive_mutex",
    "reference_wrapper", "runtime_error", "set", "shared_ptr", "span", "stack", "string", "string_view",
    "stringstream", "thread", "tuple", "underflow_error", "unique_lock", "unique_ptr", "unordered_map",
    "unordered_multimap", "unordered_multiset", "unordered_set", "variant", "vector", "weak_ptr",
    "wstring",
};

// Sorted by ASCII, so "abort" < "abs" < "acos".
constexpr Name kCFunctions[] = {
    "abort", "abs", "acos", "alignas", "alignof", "asin", "assert", "atan", "atan2", "atexit", "atof",
    "atoi", "atol", "atoll", "bsearch", "calloc", "ceil", "clearerr", "clock", "cos", "cosh", "decltype",
    "difftime", "div", "exit", "exp", "fabs", "fclose", "feof", "ferror", "fflush", "fgetc", "fgets",
    "floor", "fmod", "fopen", "fprintf", "fputc", "fputs", "fread", "free", "freopen", "frexp", "fscanf",
    "fseek", "ftell", "fwrite", "getc", "getchar", "getenv", "gets", "gmtime", "isalnum", "isalpha",
    "iscntrl", "isdigit", "isgraph", "islower", "isprint", "ispunct", "isspace", "isupper", "isxdigit",
    "labs", "ldexp", "ldiv", "llabs", "lldiv", "localtime", "log", "log10", "longjmp", "malloc", "memchr",
    "memcmp", "memcpy", "memmove", "memset", "mktime", "modf", "noexcept", "offsetof", "perror", "pow",
    "printf", "putc", "putchar", "puts", "qsort", "raise", "rand", "realloc", "remove", "rename", "rewind",
    "round", "scanf", "setjmp", "signal", "sin", "sinh", "snprintf", "sprintf", "sqrt", "srand", "sscanf",
    "static_assert", "strcat", "strchr", "strcmp", "strcpy", "strcspn", "strerror", "strftime", "strlen",
    "strncat", "strncmp", "strncpy", "strpbrk", "strrchr", "strspn", "strstr", "strtod", "strtok",
    "strtol", "strtoll", "strtoul", "strtoull", "system", "tan", "tanh", "time", "tolower", "toupper",
    "trunc", "typeid", "ungetc", "va_arg", "va_copy", "va_end", "va_start", "vfprintf", "vprintf",
    "vsnprintf", "vsprintf",
};

constexpr Name kStdFunctions[] = {
    "accumulate", "advance", "async", "begin", "bind", "clamp", "copy", "copy_n", "count", "count_if",
    "cref", "data", "distance", "empty", "end", "fill", "fill_n", "find", "find_if", "forward", "get",
    "getline", "lock", "make_optional", "make_pair", "make_shared", "make_tuple", "make_unique", "max",
    "min", "minmax", "move", "next", "prev", "ref", "remove_if", "reverse", "size", "sort", "stable_sort",
    "stod", "stof", "stoi", "stol", "stold", "stoll", "stoul", "stoull", "swap", "tie", "to_string",
    "transform", "unique",
};

// Unscoped, for when the file says `using namespace testing;`.
constexpr Name kTestingTypes[] = {
    "Action", "ActionInterface", "AssertionResult", "Cardinality", "CardinalityInterface",
    "Environment", "Expectation", "ExpectationSet", "InSequence", "Matcher", "MatcherInterface",
    "Message", "Mock", "NaggyMock", "NiceMock", "Sequence", "StrictMock", "Test",
    "TestEventListener", "TestInfo", "TestSuite", "TestWithParam", "Types", "UnitTest",
    "WithParamInterface",
};

constexpr Name kFrameworkMembers[] = {
    "GetParam", "HasFailure", "HasFatalFailure", "HasNonfatalFailure", "IsSkipped", "RecordProperty",
    "SetUp", "SetUpTestCase", "SetUpTestSuite", "TearDown", "TearDownTestCase", "TearDownTestSuite",
    "TestBody",
};

constexpr Name kForeignNamespaces[] = {
    "QTest", "Qt", "__gnu_cxx", "std", "testing",
};

// The header of the hook stub policy (backend/hook_header.h).
constexpr Name kHookFunctions[] = {
    "mt_current_not_implemented_handler", "mt_default_not_implemented_handler", "mt_not_implemented",
    "mt_set_not_implemented_handler",
};
constexpr Name kHookTypes[] = {"mt_not_implemented_handler"};

static_assert(is_sorted_unique(kStandardCTypes), "kStandardCTypes must be sorted and free of duplicates");
static_assert(is_sorted_unique(kHookFunctions), "kHookFunctions must be sorted and free of duplicates");
static_assert(is_sorted_unique(kHookTypes), "kHookTypes must be sorted and free of duplicates");
static_assert(is_sorted_unique(kStdTypes), "kStdTypes must be sorted and free of duplicates");
static_assert(is_sorted_unique(kCFunctions), "kCFunctions must be sorted and free of duplicates");
static_assert(is_sorted_unique(kStdFunctions), "kStdFunctions must be sorted and free of duplicates");
static_assert(is_sorted_unique(kTestingTypes), "kTestingTypes must be sorted and free of duplicates");
static_assert(is_sorted_unique(kFrameworkMembers), "kFrameworkMembers must be sorted and free of duplicates");
static_assert(is_sorted_unique(kForeignNamespaces), "kForeignNamespaces must be sorted and free of duplicates");

}  // namespace

bool is_macro_like(std::string_view name) {
    bool letter = false;
    for (char c : name) {
        if (is_lower(c)) return false;
        if (is_upper(c)) letter = true;
        else if (!is_digit(c) && c != '_') return false;
    }
    return letter;
}

bool is_qt_class(std::string_view name) {
    return name.size() >= 2 && name[0] == 'Q' && is_upper(name[1]);
}

bool is_qt_function(std::string_view name) {
    return name.size() >= 2 && name[0] == 'q' && is_upper(name[1]);
}

bool is_standard_c_type(std::string_view name) { return contains(kStandardCTypes, name); }
bool is_std_type(std::string_view name)        { return contains(kStdTypes, name); }
bool is_c_function(std::string_view name)      { return contains(kCFunctions, name); }
bool is_std_function(std::string_view name)    { return contains(kStdFunctions, name); }
bool is_hook_function(std::string_view name)   { return contains(kHookFunctions, name); }
bool is_hook_type(std::string_view name)       { return contains(kHookTypes, name); }
bool is_testing_type(std::string_view name)    { return contains(kTestingTypes, name); }
bool is_framework_member(std::string_view name) { return contains(kFrameworkMembers, name); }
bool is_foreign_namespace(std::string_view name) { return contains(kForeignNamespaces, name); }

}  // namespace mt::frontend
