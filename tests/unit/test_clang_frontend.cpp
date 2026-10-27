// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// The libclang frontend: the same model as the tree-sitter reader makes, from the same text; and the few things
// that are libclang's own (the branch of an #if that builds, what a macro declares, types it cannot resolve).

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "mirrortest/frontend/clang_frontend.h"
#include "mirrortest/frontend/code_frontend.h"
#include "model_dump.h"
#include "test_support.h"

using namespace mt;

namespace {

#define SKIP_WITHOUT_LIBCLANG() \
    if (!frontend::libclang_supported()) GTEST_SKIP() << "built without libclang"

struct Read {
    ir::Model         model;
    core::Diagnostics diagnostics;
};

Read tree_sitter(const std::string& text, const syntax::MacroTable& macros = {}) {
    Read r;
    r.model = frontend::read_header({"t.h", text}, "acme/t.h", macros, r.diagnostics);
    return r;
}

Read clang(const std::string& text, std::vector<std::string> arguments = {}, const syntax::MacroTable& macros = {}) {
    frontend::ClangOptions options;
    options.arguments = {"-std=c++17"};
    options.arguments.insert(options.arguments.end(), arguments.begin(), arguments.end());
    Read r;
    r.model = frontend::read_header_clang({"t.h", text}, "acme/t.h", macros, options, r.diagnostics);
    return r;
}

std::vector<std::string> messages(const Read& r, const char* code) {
    std::vector<std::string> out;
    for (const core::Diagnostic& d : r.diagnostics.sorted()) {
        if (d.code == code) out.push_back(d.message);
    }
    return out;
}

// ---- the build without libclang ------------------------------------------------------------------

TEST(ClangFrontendBuild, WithoutLibclangItRefusesAndSaysHowToHaveIt) {
    if (frontend::libclang_supported()) GTEST_SKIP() << "built with libclang";
    const Read r = clang("class A {};");
    EXPECT_TRUE(r.model.classes.empty());
    ASSERT_EQ(r.diagnostics.all().size(), 1u);
    EXPECT_EQ(r.diagnostics.all()[0].severity, core::Severity::Error);
    EXPECT_EQ(r.diagnostics.all()[0].code, core::code::parse);
    EXPECT_EQ(r.diagnostics.all()[0].message,
              "this build of mirrortest has no libclang frontend; use --frontend tree-sitter, or build with -DMT_WITH_LIBCLANG=ON");
    EXPECT_EQ(r.diagnostics.all()[0].where.file, "t.h");
}

// ---- the same model as the tree-sitter reader's -----------------------------------------------------

struct Snippet {
    const char* name;
    const char* text;
};

// Every one of these is a header that compiles on its own and says things the model has a field for. The two readers
// must make the same model of each, positions included.
const Snippet kSnippets[] = {
    {"members",
     R"(class Widget {
public:
    Widget();
    explicit Widget(int size, const char* name = "w");
    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;
    virtual ~Widget();
    int size() const;
    static Widget make(int n);
    void resize(int n) noexcept;
    virtual void draw() = 0;
    void paint() const &;
    void take() &&;
protected:
    void helper();
private:
    int size_;
    const char* name_;
    static int count_;
};)"},
    {"struct_fields",
     R"(struct Record {
    int id;
    unsigned flags : 3;
    double values[4];
    const char* const label;
    int (*callback)(int, void*);
    int& ref;
    long long big = 1;
    static const int kMax = 8;
    int braced{5};
    const char* text{"x"};
    double empty{};
};)"},
    {"operators",
     R"(class Num {
public:
    bool operator==(const Num& other) const;
    bool operator<(const Num& other) const;
    Num& operator[](int i);
    int operator()(int x) const;
    Num operator++(int);
    Num& operator+=(const Num&);
    explicit operator bool() const;
    operator const char*() const;
    void* operator new(unsigned long);
};
bool operator!=(const Num& a, const Num& b);)"},
    {"inheritance",
     R"(namespace base { class Root {}; template <typename T> class Tpl {}; }
class A {};
class B : public A, protected base::Root, private virtual A {};
struct C : A, virtual base::Root {};
class D final : public base::Tpl<int> {};)"},
    {"namespaces",
     R"(namespace outer {
namespace inner {
class In { public: void f(); };
}
inline namespace v1 { enum class E { X, Y }; }
}
namespace a::b::c { struct Deep { int x; }; }
namespace { class Hidden {}; }
extern "C" { int c_function(int); }
namespace outer { void again(); })"},
    {"enums",
     R"(enum Plain { One, Two = 5, Three };
enum class Scoped : unsigned char { A, B };
enum struct Other { P };
enum Opaque : int;
namespace n { enum class Q { R }; }
class Holder {
public:
    enum Mode { Fast, Slow };
    enum class Kind { K1 };
private:
    enum Secret { S };
};)"},
    {"templates",
     R"(template <typename T, int N = 4>
class Box {
public:
    Box();
    explicit Box(T value);
    T get() const;
    template <typename U> void set(U u);
    Box clone() const;
private:
    T items_[N];
};
template <typename T> struct Pair { T a; T b; };
template <> class Box<char, 1> {};
template <typename T> class Pair2 { public: void f(); };
template <typename T> T identity(T v);
template <typename... Ts> class Pack { public: void f(Ts... ts); };)"},
    {"nested",
     R"(class Outer {
public:
    struct Item { int id; };
    class Node { public: void visit(); };
    enum Color { Red };
    typedef int Id;
    using Name = const char*;
    Item make() const;
    Outer::Id id() const;
protected:
    struct Secret {};
private:
    class Impl { int x; };
};)"},
    {"parameters",
     R"(struct V {};
void many(int a = 3, const V& v = V(), int b = (1 + 2), const char* s = "x");
void unnamed(int, double);
void c_variadic(const char* fmt, ...);
void arrays(int a[3], int (&r)[4]);
void fnptr(void (*cb)(int), void (*other)(const V&, int));
void nothing(void);
auto trailing(int x) -> int;
auto trailing_ref(int x) -> const V&;
void no1() noexcept;
void no2() noexcept(true);
void no3() noexcept(false);
void no4() noexcept(sizeof(int) > 1);)"},
    {"numeric_types",
     R"(unsigned plain(unsigned a, long long b, long int c, short d, unsigned char e, signed char f);
class Sz { public: unsigned size() const; unsigned long long big(unsigned) const; };)"},
    {"qualified_names",
     R"(namespace ns { class T {}; class U {}; }
class User {
public:
    ns::T make(const ns::U& u) const;
    ::ns::T global(::ns::U* u);
    const ns::T* const pointer() const;
};)"},
    {"qt_markers",
     R"(class QObject {};
class Counter : public QObject {
    Q_OBJECT
public:
    Q_INVOKABLE int value() const;
    int plain() const;
public slots:
    void reset();
signals:
    void changed(int);
private slots:
    void tick();
};
class Value {
    Q_GADGET
    Q_DISABLE_COPY(Value)
public:
    int v;
};)"},
    {"includes",
     R"(#include <cstddef>
#include "local.h"
# include <cstdint>
#include "dir/other.h"
    #include <indented>
class A {};)"},
    {"friends_and_the_rest",
     R"(class A {
    friend class B;
    friend void helper(A&);
    static_assert(sizeof(int) >= 4, "int");
public:
    int x() const;
};
class B {};
void helper(A&);
int global_variable;
using Alias = A;
typedef A AliasTwo;)"},
    {"anonymous",
     R"(struct { int a; } variable;
class Holder { public: struct { int x; } inner; union { int u; float f; }; };
union Un { int i; float f; };
struct Fields {
    union { int i; float f; } bare_union;
    class { public: int c; } bare_class;
    enum { One, Two } bare_enum;
};)"},
    {"trailing_returns",
     R"(template <typename T> struct Vec {};
template <typename T, int N> struct Arr {};
extern int x_;
auto nested() -> Vec<Vec<int>>;
auto triple(int a) -> Vec<Vec<Vec<int>>>;
auto assigned() -> decltype(x_ = 1);
auto decl(int a, const Vec<int>& v) -> decltype(x_);
auto array_ref(int (&a)[4]) -> int (&)[4];
auto expression() -> Arr<int, (4 > 2 ? 4 : 2)>;
struct B {
    virtual ~B();
    virtual auto over() -> int;
    virtual auto fin() const noexcept -> const Vec<int>&;
    virtual auto dover(int) const & -> decltype(x_);
};
struct S : B {
    auto member() const -> Vec<Vec<int>>;
    virtual auto pure() -> Vec<int> = 0;
    virtual auto pure2() const -> Vec<Vec<int>> = 0;
    auto over() -> int override;
    auto fin() const noexcept -> const Vec<int>& final;
    auto deleted() -> int = delete;
    auto sized() const -> decltype(x_);
    virtual auto dpure() -> decltype(x_ = 1) = 0;
    auto dover(int a) const & -> decltype(x_) override;
    static auto make(int n) -> decltype(x_);
    auto with_body(int n) const -> decltype(x_) { return x_ + n; }
};)"},
    {"comments_inside_declarations",
     R"(class Annotated {
public:
    int /* the count */ count(int /* unused */, const /* mutable? */ char* name /* trailing */) const /* after */;
    // a line comment between members
    void f(); // and one after
    unsigned long /* wide */ x_ /* field */;
};
void /* free */ g(/* nothing */);)"},
    {"inline_definitions",
     R"(class Impl {
public:
    Impl() : value_(0) {}
    explicit Impl(int v) : value_(v) {}
    int value() const { return value_; }
    void set(int v) { value_ = v; }
    virtual ~Impl() = default;
    bool ok() const noexcept { return true; }
private:
    int value_;
};)"},
};

TEST(ClangFrontendParity, TheSameModelAsTreeSitterMakes) {
    SKIP_WITHOUT_LIBCLANG();
    for (const Snippet& s : kSnippets) {
        const Read a = tree_sitter(s.text);
        const Read b = clang(s.text);
        EXPECT_EQ(test::dump(b.model, true), test::dump(a.model, true)) << "snippet: " << s.name << "\n" << s.text;
    }
}

// What it says when it cannot read something is the same words, in the same places.
TEST(ClangFrontendParity, TheUnsupportedConstructsAreReportedAsTreeSitterReportsThem) {
    SKIP_WITHOUT_LIBCLANG();
    const char* text = R"(template <typename T> class Pair2 { public: void f(); };
template <> class Pair2<char> {};
template <typename T> class Pair2<T*> {};
template <typename T> T identity(T v);
class Base { public: Base(int); };
class Derived : public Base { public: using Base::Base; };
)";
    const Read a = tree_sitter(text);
    const Read b = clang(text);
    EXPECT_EQ(messages(b, "mt-unsupported"), messages(a, "mt-unsupported"));
    EXPECT_EQ(messages(a, "mt-unsupported").size(), 4u) << "and tree-sitter did say something about each";
    const std::vector<core::Diagnostic> from_tree_sitter = a.diagnostics.sorted();
    const std::vector<core::Diagnostic> from_clang = b.diagnostics.sorted();
    ASSERT_EQ(from_tree_sitter.size(), from_clang.size());
    for (std::size_t i = 0; i < from_tree_sitter.size(); ++i) {
        EXPECT_EQ(from_clang[i].where.line, from_tree_sitter[i].where.line) << from_tree_sitter[i].message;
        EXPECT_EQ(from_clang[i].where.file, from_tree_sitter[i].where.file) << from_tree_sitter[i].message;
    }
}

// ---- what libclang adds ---------------------------------------------------------------------------------

TEST(ClangFrontend, OnlyTheBranchOfAnIfThatBuildsIsRead) {
    SKIP_WITHOUT_LIBCLANG();
    const char* text = "class Port {\npublic:\n#ifdef USE_DMA\n    void transfer(const char* data, unsigned size);\n#else\n    void transfer(const char* data);\n#endif\n};\n";
    const Read plain = clang(text);
    ASSERT_EQ(plain.model.classes.size(), 1u);
    ASSERT_EQ(plain.model.classes[0].methods.size(), 1u);
    EXPECT_EQ(plain.model.classes[0].methods[0].params.size(), 1u);

    const Read dma = clang(text, {"-DUSE_DMA"});
    ASSERT_EQ(dma.model.classes[0].methods.size(), 1u);
    EXPECT_EQ(dma.model.classes[0].methods[0].params.size(), 2u);

    EXPECT_EQ(tree_sitter(text).model.classes[0].methods.size(), 2u) << "tree-sitter reads both and keeps both";
    EXPECT_TRUE(plain.diagnostics.empty());
}

TEST(ClangFrontend, WhatAMacroDeclaresIsRead) {
    SKIP_WITHOUT_LIBCLANG();
    const char* text = R"(#define GETTER(type, name) type name() const;
#define SETTER(type, name) void set_##name(type value);
class Config {
public:
    GETTER(int, size)
    GETTER(const char*, label)
    SETTER(int, size)
};)";
    const Read r = clang(text);
    ASSERT_EQ(r.model.classes.size(), 1u);
    const std::vector<ir::Method>& m = r.model.classes[0].methods;
    ASSERT_EQ(m.size(), 3u);
    EXPECT_EQ(m[0].name, "size");
    EXPECT_EQ(m[0].ret.spelling, "int");
    EXPECT_TRUE(m[0].is_const);
    EXPECT_EQ(m[1].name, "label");
    EXPECT_EQ(m[1].ret.spelling, "const char*");
    EXPECT_EQ(m[2].name, "set_size");
    ASSERT_EQ(m[2].params.size(), 1u);
    EXPECT_EQ(m[2].params[0].type.spelling, "int");
    EXPECT_FALSE(m[2].is_const);
    const Read syntax_tree = tree_sitter(text);
    ASSERT_EQ(syntax_tree.model.classes.size(), 1u);
    for (const ir::Method& made : syntax_tree.model.classes[0].methods) {
        EXPECT_NE(made.name, "size") << "a syntax tree sees a macro call, not the members it makes";
        EXPECT_NE(made.name, "label");
        EXPECT_NE(made.name, "set_size");
    }
}

TEST(ClangFrontend, AnExportMacroNeedsNoIgnoreMacrosBecauseItIsExpanded) {
    SKIP_WITHOUT_LIBCLANG();
    const char* text = "#define MYLIB_API __attribute__((visibility(\"default\")))\nclass MYLIB_API Widget {\npublic:\n    MYLIB_API int size() const;\n};\n";
    const Read r = clang(text);
    ASSERT_EQ(r.model.classes.size(), 1u);
    EXPECT_EQ(r.model.classes[0].name, "Widget");
    ASSERT_EQ(r.model.classes[0].methods.size(), 1u);
    EXPECT_EQ(r.model.classes[0].methods[0].ret.spelling, "int");
    EXPECT_TRUE(r.diagnostics.empty());
}

TEST(ClangFrontend, ATagDefinedInTheDeclarationOfAMemberIsPartOfItsTypeAndAlsoAMemberType) {
    SKIP_WITHOUT_LIBCLANG();
    const char* text = R"(struct Fields {
    struct Tag { int a; } tagged;
    union Named { int i; } named_union;
    enum Colour { Red, Blue } named_enum;
    int braced{5};
};)";
    const Read r = clang(text);
    ASSERT_EQ(r.model.classes.size(), 1u);
    const ir::Class& c = r.model.classes[0];
    ASSERT_EQ(c.fields.size(), 4u);
    EXPECT_EQ(c.fields[0].type.spelling, "struct Tag{int a;}");
    EXPECT_EQ(c.fields[1].type.spelling, "union Named{int i;}");
    EXPECT_EQ(c.fields[2].type.spelling, "enum Colour{Red, Blue}");
    EXPECT_EQ(c.fields[3].type.spelling, "int") << "the braces of an initialiser are not part of the type";
    EXPECT_EQ(c.nested_types, (std::vector<std::string>{"Tag", "Named", "Colour"})) << "which the other reader does not see";
}

TEST(ClangFrontend, ACommentInsideAFundamentalTypeIsNotPartOfIt) {
    SKIP_WITHOUT_LIBCLANG();
    const Read r = clang("struct S { unsigned /* flags */ long /* wide */ x_; };");
    ASSERT_EQ(r.model.classes.size(), 1u);
    ASSERT_EQ(r.model.classes[0].fields.size(), 1u);
    EXPECT_EQ(r.model.classes[0].fields[0].type.spelling, "unsigned long");
}

TEST(ClangFrontend, AFunctionThatReturnsAFunctionPointerHasItsOwnParametersOnly) {
    SKIP_WITHOUT_LIBCLANG();
    const char* text = R"(auto trailing() -> void (*)(int);
void (*get(int a, const char* b))(int c);
struct Holder {
    void (*callback(int a))(double b, double c);
    int (*table() const)[4];
};)";
    const Read r = clang(text);
    ASSERT_TRUE(r.diagnostics.empty());
    ASSERT_EQ(r.model.functions.size(), 2u);
    EXPECT_EQ(r.model.functions[0].name, "trailing");
    EXPECT_EQ(r.model.functions[0].ret.spelling, "void(*)(int)");
    EXPECT_TRUE(r.model.functions[0].params.empty()) << "the (int) belongs to the pointer";
    EXPECT_EQ(r.model.functions[1].name, "get");
    ASSERT_EQ(r.model.functions[1].params.size(), 2u);
    EXPECT_EQ(r.model.functions[1].params[0].name, "a");
    EXPECT_EQ(r.model.functions[1].params[1].name, "b");
    ASSERT_EQ(r.model.classes.size(), 1u);
    ASSERT_EQ(r.model.classes[0].methods.size(), 2u);
    ASSERT_EQ(r.model.classes[0].methods[0].params.size(), 1u);
    EXPECT_EQ(r.model.classes[0].methods[0].params[0].name, "a");
    EXPECT_TRUE(r.model.classes[0].methods[1].params.empty());
    EXPECT_TRUE(r.model.classes[0].methods[1].is_const);
}

TEST(ClangFrontendParity, ADynamicExceptionSpecificationIsNotQualifiers) {
    SKIP_WITHOUT_LIBCLANG();
    const char* text = R"(struct E {};
struct Legacy {
    void a() throw();
    void b() const throw(const E&);
    void c() throw(E, int);
    void d() throw(const E&&) ;
};)";
    const Read a = tree_sitter(text);
    const Read b = clang(text, {"-std=c++14"});
    EXPECT_EQ(test::dump(b.model, true), test::dump(a.model, true));
    ASSERT_EQ(b.model.classes.size(), 2u);
    const std::vector<ir::Method>& m = b.model.classes[1].methods;
    ASSERT_EQ(m.size(), 4u);
    EXPECT_TRUE(m[1].is_const);
    for (const ir::Method& made : m) EXPECT_EQ(made.ref_qualifier, ir::RefKind::None) << made.name << ": the & is inside the throw()";
}

TEST(ClangFrontend, WhatFollowsARequiresClauseIsNotAQualifier) {
    SKIP_WITHOUT_LIBCLANG();
    const char* text = R"(template <typename T> struct Guarded {
    void both() const requires (sizeof(T) > 1) && true;
    void rvalue() requires true && false;
    auto trailing() const -> int requires (sizeof(T) > 1);
};)";
    const Read r = clang(text, {"-std=c++20"});
    ASSERT_EQ(r.model.classes.size(), 1u);
    const std::vector<ir::Method>& m = r.model.classes[0].methods;
    ASSERT_EQ(m.size(), 3u);
    EXPECT_TRUE(m[0].is_const);
    EXPECT_EQ(m[0].ref_qualifier, ir::RefKind::None) << "the && of the constraint is not a reference qualifier";
    EXPECT_EQ(m[1].ref_qualifier, ir::RefKind::None);
    EXPECT_FALSE(m[1].is_const);
    EXPECT_TRUE(m[2].is_const);
    EXPECT_EQ(m[2].ret.spelling, "int");
}

TEST(ClangFrontend, AStructGivenANameByATypedefIsReadWhichTheSyntaxTreeReaderDoesNot) {
    SKIP_WITHOUT_LIBCLANG();
    const Read r = clang("typedef struct { int b; } Named;\ntypedef struct Tagged { int c; } TaggedAlias;\n");
    ASSERT_EQ(r.model.classes.size(), 2u);
    EXPECT_EQ(r.model.classes[0].name, "Named");
    EXPECT_EQ(r.model.classes[0].fields[0].name, "b");
    EXPECT_EQ(r.model.classes[1].name, "Tagged");
    EXPECT_TRUE(tree_sitter("typedef struct { int b; } Named;\n").model.classes.empty());
}

TEST(ClangFrontend, APointerToMemberIsReadWhichTheSyntaxTreeReaderDoesNot) {
    SKIP_WITHOUT_LIBCLANG();
    const char* text = "class Outer {};\nvoid fnptr(int (Outer::*pm)(char));\n";
    const Read r = clang(text);
    ASSERT_EQ(r.model.functions.size(), 1u);
    ASSERT_EQ(r.model.functions[0].params.size(), 1u);
    EXPECT_EQ(r.model.functions[0].params[0].type.spelling, "int(Outer::*)(char)");
    EXPECT_EQ(r.model.functions[0].params[0].name, "pm");
}

// ---- what it cannot read ---------------------------------------------------------------------------

TEST(ClangFrontend, ATypeItDoesNotKnowIsKeptAsWrittenAndReported) {
    SKIP_WITHOUT_LIBCLANG();
    const char* text = "class Handle {\npublic:\n    explicit Handle(Mystery m);\n    Mystery get() const;\n    const Mystery* ptr;\n};\nvoid free_one(Other o);\n";
    const Read r = clang(text);
    ASSERT_EQ(r.model.classes.size(), 1u);
    const ir::Class& c = r.model.classes[0];
    ASSERT_EQ(c.methods.size(), 2u);
    ASSERT_EQ(c.methods[0].params.size(), 1u);
    EXPECT_EQ(c.methods[0].params[0].type.spelling, "Mystery") << "not the int libclang puts in its place";
    EXPECT_EQ(c.methods[1].ret.spelling, "Mystery");
    ASSERT_EQ(c.fields.size(), 1u);
    EXPECT_EQ(c.fields[0].type.spelling, "const Mystery*");
    EXPECT_EQ(r.model.functions[0].params[0].type.spelling, "Other");

    const std::vector<std::string> parse = messages(r, "mt-parse");
    ASSERT_EQ(parse.size(), 4u);
    EXPECT_EQ(parse[0], "libclang: unknown type name 'Mystery'; declarations around it may be missing or have wrong types");
    for (const core::Diagnostic& d : r.diagnostics.all()) {
        EXPECT_EQ(d.severity, core::Severity::Warning);
        EXPECT_EQ(d.where.file, "t.h");
        EXPECT_GT(d.where.line, 0u);
    }
    EXPECT_EQ(tree_sitter(text).model.classes[0].methods[0].params[0].type.spelling, "Mystery") << "and tree-sitter says the same";
}

TEST(ClangFrontend, ABaseItCannotResolveIsStillInTheBaseClause) {
    SKIP_WITHOUT_LIBCLANG();
    const Read r = clang("class Gear : public Base, virtual protected other::Mixin<int> {};\n");
    ASSERT_EQ(r.model.classes.size(), 1u);
    ASSERT_EQ(r.model.classes[0].bases.size(), 2u);
    EXPECT_EQ(r.model.classes[0].bases[0].name, "Base");
    EXPECT_EQ(r.model.classes[0].bases[1].name, "other::Mixin<int>");
    EXPECT_TRUE(r.model.classes[0].bases[1].is_virtual);
    EXPECT_EQ(r.model.classes[0].bases[1].access, ir::Access::Protected);
    EXPECT_FALSE(messages(r, "mt-parse").empty());
}

TEST(ClangFrontend, MoreThanTenErrorsAreTenWarningsAndACount) {
    SKIP_WITHOUT_LIBCLANG();
    std::string text;
    for (int i = 0; i < 15; ++i) text += "void f" + std::to_string(i) + "(Missing" + std::to_string(i) + " m);\n";
    const Read r = clang(text);
    EXPECT_EQ(messages(r, "mt-parse").size(), 11u);
    unsigned notes = 0;
    for (const core::Diagnostic& d : r.diagnostics.all()) {
        if (d.severity == core::Severity::Note) {
            ++notes;
            EXPECT_EQ(d.message, "and 5 more libclang errors");
        }
    }
    EXPECT_EQ(notes, 1u);
}

TEST(ClangFrontend, AHeaderItIncludesThatIsNotThereIsReportedAndTheRestIsStillRead) {
    SKIP_WITHOUT_LIBCLANG();
    const Read r = clang("#include \"missing_header.h\"\nclass A { public: int f() const; };\n");
    ASSERT_EQ(r.model.includes.size(), 1u);
    EXPECT_EQ(r.model.includes[0].path, "missing_header.h");
    EXPECT_FALSE(r.model.includes[0].is_system);
    ASSERT_EQ(r.model.classes.size(), 1u);
    const std::vector<std::string> parse = messages(r, "mt-parse");
    ASSERT_EQ(parse.size(), 1u);
    EXPECT_NE(parse[0].find("'missing_header.h' file not found"), std::string::npos) << parse[0];
}

// ---- the file and the world around it ------------------------------------------------------------

TEST(ClangFrontend, TheDeclarationsOfAnIncludedHeaderAreResolvedButNotRead) {
    SKIP_WITHOUT_LIBCLANG();
    test::TempDir dir;
    test::write_file(dir.path() / "inc/other.h", "struct Other { int x; };\nclass Third {};\n");
    const std::string main_path = (dir.path() / "src/main.h").string();
    test::write_file(main_path, "");   // the file need not exist, but its directory is where an #include \"...\" starts
    frontend::ClangOptions options;
    options.arguments = {"-std=c++17", "-I" + (dir.path() / "inc").string()};
    options.absolute_path = main_path;
    core::Diagnostics diagnostics;
    const ir::Model model = frontend::read_header_clang({"src/main.h", "#include \"other.h\"\nclass Main { public: Other get() const; };\n"}, "main.h", {}, options, diagnostics);
    EXPECT_TRUE(diagnostics.empty());
    ASSERT_EQ(model.classes.size(), 1u);
    EXPECT_EQ(model.classes[0].name, "Main");
    EXPECT_EQ(model.classes[0].methods[0].ret.spelling, "Other");
    ASSERT_EQ(model.includes.size(), 1u);
    EXPECT_EQ(model.includes[0].path, "other.h");
    EXPECT_EQ(model.includes[0].where.file, "src/main.h") << "locations name the file as the user gave it";
}

TEST(ClangFrontend, AnIncludeNextToTheFileIsFoundWhateverTheProcessDirectoryIs) {
    SKIP_WITHOUT_LIBCLANG();
    test::TempDir dir;
    test::write_file(dir.path() / "pair.h", "struct Pair { int a; int b; };\n");
    frontend::ClangOptions options;
    options.arguments = {"-std=c++17"};
    options.absolute_path = (dir.path() / "user.h").string();
    core::Diagnostics diagnostics;
    const ir::Model model = frontend::read_header_clang({"user.h", "#include \"pair.h\"\nclass U { public: Pair make() const; };\n"}, "user.h", {}, options, diagnostics);
    EXPECT_TRUE(diagnostics.empty()) << (diagnostics.all().empty() ? "" : diagnostics.all()[0].message);
    ASSERT_EQ(model.classes.size(), 1u);
}

TEST(ClangFrontend, TheStandardIsTheOneItIsGiven) {
    SKIP_WITHOUT_LIBCLANG();
    const char* text = "void f(auto x);\n";   // an abbreviated function template: C++20
    EXPECT_FALSE(messages(clang(text, {"-std=c++17"}), "mt-parse").empty());
    const Read newer = clang(text, {"-std=c++20"});
    EXPECT_TRUE(messages(newer, "mt-parse").empty());
    EXPECT_EQ(messages(newer, "mt-unsupported").size(), 1u) << "a function template, which is not tested";
}

TEST(ClangFrontend, QtMacrosAreBlankedLikeInTheOtherReaderSoQtHeadersAreNotNeededForTheMarkers) {
    SKIP_WITHOUT_LIBCLANG();
    const char* text = "class QObject {};\nclass Counter : public QObject {\n    Q_OBJECT\npublic:\n    Q_INVOKABLE int value() const;\npublic slots:\n    void reset();\nsignals:\n    void changed(int);\n};\n";
    const Read r = clang(text);
    ASSERT_EQ(r.model.classes.size(), 2u);
    const ir::Class& c = r.model.classes[1];
    EXPECT_TRUE(c.is_qobject);
    ASSERT_EQ(c.methods.size(), 3u);
    EXPECT_EQ(c.methods[0].qt_role, ir::QtRole::Invokable);
    EXPECT_EQ(c.methods[1].qt_role, ir::QtRole::Slot);
    EXPECT_EQ(c.methods[2].qt_role, ir::QtRole::Signal);
    EXPECT_TRUE(r.diagnostics.empty()) << r.diagnostics.all()[0].message;
}

TEST(ClangFrontend, TheIgnoredMacrosOfTheConfigurationAreBlankedToo) {
    SKIP_WITHOUT_LIBCLANG();
    syntax::MacroTable macros;
    macros.ignore = {"MY_EXPORT"};
    const Read r = clang("class MY_EXPORT Widget { public: MY_EXPORT int size() const; };\n", {}, macros);
    ASSERT_EQ(r.model.classes.size(), 1u);
    EXPECT_EQ(r.model.classes[0].name, "Widget");
    EXPECT_TRUE(r.diagnostics.empty()) << (r.diagnostics.all().empty() ? "" : r.diagnostics.all()[0].message);
}

}  // namespace
