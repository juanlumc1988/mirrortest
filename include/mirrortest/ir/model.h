// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// The intermediate representation every frontend produces and every backend
// consumes. Frontends never talk to backends, and nothing in here knows about
// tree-sitter, GoogleTest or QTest: that is what lets a parser or a test
// framework be swapped without touching the other side.
//
// Two halves:
//
//   * The code model (Type .. Model): classes, free functions and enums,
//     either read from a header (gen-tests) or inferred from tests (gen-impl).
//   * The test plan (Stmt .. TestPlan): framework-neutral test skeletons that
//     the GoogleTest and QTest backends give a concrete shape. GoogleTest
//     wants macros, QTest wants a QObject with private slots and moc; the
//     plan describes neither, only what each case must do.
//
// Provenance is first-class. Every Type, Method and Field records how it was
// obtained (Origin) and why (reason, where). gen-impl turns that into
// "TODO(mt):" comments and warnings; without it, an inferred guess would be
// indistinguishable from something the user actually wrote, which is the one
// thing a generator must never do.
//
// Plain aggregates with value semantics, no ownership tricks. Order matters
// everywhere (it is the emission order) so there are vectors, never hash
// maps: the output must be byte-identical across runs and platforms.

#ifndef MT_IR_MODEL_H
#define MT_IR_MODEL_H

#include <optional>
#include <string>
#include <vector>

namespace mt::ir {

// A position in an input file. `file` is the path exactly as the user gave
// it -- never made absolute -- so generated comments are reproducible from
// any checkout location.
struct SourceLoc {
    std::string file;
    unsigned    line   = 0;   // 1-based; 0 means unknown
    unsigned    column = 0;   // 1-based, in bytes

    bool known() const { return line != 0; }
};

// How a fact was obtained, in increasing order of trust. When two sources
// disagree the higher one wins and the conflict is reported, never hidden.
enum class Origin {
    Defaulted,   // nothing observed; a documented default applied (unused result -> void)
    Inferred,    // deduced from a literal or an assertion's context
    Declared,    // spelled out in source: a header, a typed variable, a cast
    Annotated,   // stated by a "// @mt:" annotation or a config override
};

enum class Access  { Public, Protected, Private };
enum class RefKind { None, LValue, RValue };

// A type as it will be written out.
//
// `spelling` is authoritative: it is what gets emitted, so a declared type
// survives the round trip exactly as the user wrote it (modulo whitespace).
// The decomposed fields exist for decisions -- pass by value or by const
// reference, what a default return value looks like -- and describe the
// common shapes only. Anything they cannot express (function pointers,
// arrays, "const char* const&") keeps its spelling and is classified
// conservatively by whoever consumes it.
struct Type {
    std::string spelling;            // "const std::string&"
    std::string base;                // "std::string": no cv, no &, no *
    bool        is_const = false;    // const on the base: const T&, const T*
    RefKind     ref = RefKind::None;
    unsigned    pointer_depth = 0;

    Origin      origin = Origin::Declared;
    std::string reason;              // provenance for TODO comments: "literal 3u"
    SourceLoc   where;

    bool empty()   const { return spelling.empty(); }
    bool is_void() const { return spelling == "void"; }
};

struct Param {
    Type        type;
    std::string name;                // may be empty: unnamed in the header
    std::optional<std::string> default_value;   // spelled as in source
};

enum class MethodKind { Normal, Constructor, Destructor, Operator, Conversion };

// Qt's extra member categories. Signals get QSignalSpy skeletons rather
// than call tests; slots and invokables are ordinary methods that also
// happen to be callable through the meta-object system.
enum class QtRole { None, Slot, Signal, Invokable };

struct Method {
    std::string name;                // "size", "operator==", "Foo", "~Foo", "operator bool"
    MethodKind  kind = MethodKind::Normal;
    Type        ret;                 // empty for constructors, destructors, conversions
    std::vector<Param> params;
    Access      access = Access::Public;

    bool is_const     = false;
    bool is_static    = false;
    bool is_virtual   = false;
    bool is_pure      = false;       // = 0
    bool is_override  = false;
    bool is_final     = false;
    bool is_noexcept  = false;       // noexcept or noexcept(true); noexcept(expr) keeps false
    bool is_explicit  = false;
    bool is_deleted   = false;       // = delete
    bool is_defaulted = false;       // = default
    bool is_template  = false;       // member template: needs configured instantiations
    RefKind ref_qualifier = RefKind::None;   // void f() &;  void f() &&;
    QtRole  qt_role = QtRole::None;

    Origin    origin = Origin::Declared;
    SourceLoc where;
    // Free text, one line each. gen-impl emits them as "TODO(mt): ..." next
    // to the declaration; gen-tests ignores them.
    std::vector<std::string> notes;
};

struct Field {
    Type        type;
    std::string name;
    Access      access = Access::Public;
    bool        is_static = false;
    Origin      origin = Origin::Declared;
    SourceLoc   where;
};

struct BaseSpec {
    std::string name;                // spelled as in source: "QObject", "::testing::Test"
    Access      access = Access::Public;
    bool        is_virtual = false;
};

struct Class {
    std::string              name;
    std::vector<std::string> ns;     // enclosing namespaces, outermost first
    bool                     is_struct = false;
    std::vector<BaseSpec>    bases;
    std::vector<Method>      methods;   // declaration order
    std::vector<Field>       fields;
    // Names of member types (nested class/struct/enum, typedef, using). Out
    // of the class they must be written Foo::Item; the backends qualify any
    // signature that mentions one.
    std::vector<std::string> nested_types;
    // "typename T, int N" for a class template; templates are only expanded
    // for instantiations listed in the configuration.
    std::optional<std::string> template_params;
    bool                     is_qobject = false;   // declared Q_OBJECT
    bool                     is_gadget = false;    // declared Q_GADGET: a Qt value type, no signals or slots
    bool                     disable_copy = false; // Q_DISABLE_COPY / Q_DISABLE_COPY_MOVE

    // The header that declares it (gen-tests) or must declare it (gen-impl),
    // spelled as an #include would spell it: "foo.h", "acme/foo.h".
    std::string              header;
    Origin                   origin = Origin::Declared;
    SourceLoc                where;
};

struct Function {
    std::string              name;
    std::vector<std::string> ns;
    Type                     ret;
    std::vector<Param>       params;
    bool                     is_noexcept = false;
    std::string              header;
    Origin                   origin = Origin::Declared;
    SourceLoc                where;
    std::vector<std::string> notes;
};

struct Enum {
    std::string              name;
    std::vector<std::string> ns;
    bool                     is_scoped = false;   // enum class / enum struct
    std::optional<std::string> underlying;        // ": std::uint8_t"
    std::vector<std::string> enumerators;
    std::string              header;
    SourceLoc                where;
};

// One #include directive, as written.
struct Include {
    std::string path;                // "foo.h", "gtest/gtest.h"
    bool        is_system = false;   // <...> rather than "..."
    SourceLoc   where;
};

// Everything known about the code side of one run.
struct Model {
    std::vector<Class>    classes;
    std::vector<Function> functions;
    std::vector<Enum>     enums;
    std::vector<Include>  includes;
};

// ---------------------------------------------------------------------------
// Test plan
// ---------------------------------------------------------------------------

enum class Framework { GTest, QTest };

// One statement of a planned test body. Arrange/act code is plain C++ that
// is valid under either framework and is carried as text; anything whose
// spelling depends on the framework (assertions, the placeholder that keeps
// an unwritten test red) is a dedicated kind the backend renders.
struct Stmt {
    enum class Kind {
        Blank,          // empty line, for readability
        Comment,        // text = comment body without "//"
        Code,           // text = one C++ statement, without the trailing ';'
        StaticAssert,   // text = condition, message = diagnostic
        Placeholder,    // text = message; FAIL()/GTEST_SKIP() or QFAIL()/QSKIP()
    };

    Kind        kind = Kind::Code;
    std::string text;
    std::string message;
    // Emit as a comment instead of code: the statement is known not to be
    // compilable as generated (an argument type nobody knows how to build),
    // but is still the best starting point for the person writing the test.
    bool        commented_out = false;
};

struct TestCase {
    std::string       name;          // a valid identifier
    std::string       target;        // "acme::Foo::size() const": for messages only
    std::vector<Stmt> body;
};

struct Fixture {
    std::string        name;         // "FooTest"
    std::vector<Field> members;
    std::vector<Stmt>  set_up;       // SetUp() / init()
    std::vector<Stmt>  tear_down;    // TearDown() / cleanup()
};

struct TestSuite {
    std::string              name;            // "FooTest"; QTest: the test class
    std::string              target_class;    // "Foo"
    std::vector<std::string> ns;              // emitted inside these namespaces
    std::optional<Fixture>   fixture;
    std::vector<TestCase>    cases;
};

// One generated test file.
struct TestPlan {
    std::string              file;            // output path, relative to --out
    std::vector<Include>     includes;
    std::vector<TestSuite>   suites;
};

}  // namespace mt::ir

#endif  // MT_IR_MODEL_H
