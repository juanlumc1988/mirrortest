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

    // The conditions of the `#if` branches the declaration is in, inside the class (those the class is in are its own),
    // outermost first, each as the text of an `#if` that is true when the branch is taken: `defined(USE_DMA)`, and in
    // the `#else` of it `!(defined(USE_DMA))`. Only the syntax-tree reader reads them; libclang reads the branch that
    // builds. A test of the method is written under the same conditions.
    std::vector<std::string> guards;
};

struct Field {
    Type        type;
    std::string name;
    Access      access = Access::Public;
    bool        is_static = false;
    Origin      origin = Origin::Declared;
    SourceLoc   where;
    // As for Method::notes: one line each, emitted as "TODO(mt): ..." by gen-impl.
    std::vector<std::string> notes;
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
    // The bases whose constructors the class takes as its own (`using Base::Base;`), spelled as in `bases`.
    std::vector<std::string> inherited_constructors;
    // What concerns the class as a whole (why it has the base it has), as the TODO(mt) lines above its first member.
    std::vector<std::string> notes;
    // The conditions of the `#if` branches the class is defined in (for a nested class, inside the class around it),
    // as for Method::guards. The tests of the class are written under the same conditions.
    std::vector<std::string> guards;
    std::vector<Method>      methods;   // declaration order
    std::vector<Field>       fields;
    // Names of member types (nested class/struct/enum, typedef, using). Out
    // of the class they must be written Foo::Item; the backends qualify any
    // signature that mentions one.
    std::vector<std::string> nested_types;
    // The public enums among nested_types. A value of one is `Foo::Mode m{};`,
    // which is all a test skeleton needs to know about it.
    std::vector<std::string> nested_enums;
    // The classes and structs declared inside this one, read like any other.
    // They are not Classes of the Model -- Foo::Item is reached through Foo --
    // but a planner that wants to know whether an Item can be default
    // constructed has somewhere to look.
    std::vector<Class>       nested_classes;
    // For a nested class: the access it was declared under in its outer class.
    Access                   nested_access = Access::Public;
    // Only in the model the test planner works on (backend/nested_expand): a public nested class
    // given a place of its own, called `Outer::Inner`; this is the name of the class that holds it
    // (`Outer`, `Box<int>`), and empty for any other class.
    std::string              enclosing;
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
    std::vector<std::string> guards;   // the `#if` conditions the function is under, as for Method::guards
};

struct Enum {
    std::string              name;
    std::vector<std::string> ns;
    bool                     is_scoped = false;   // enum class / enum struct
    std::optional<std::string> underlying;        // ": std::uint8_t"
    std::vector<std::string> enumerators;
    // For an enum the tools infer: the TODO(mt) comment lines above each enumerator, the same
    // number as the enumerators (a vector of the notes of each); empty for one read from a header.
    std::vector<std::vector<std::string>> enumerator_notes;
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
        Data,           // the case's arguments, read from its data table into locals named as its columns
    };

    Kind        kind = Kind::Code;
    std::string text;
    std::string message;
    // Emit as a comment instead of code: the statement is known not to be
    // compilable as generated (an argument type nobody knows how to build),
    // but is still the best starting point for the person writing the test.
    bool        commented_out = false;
};

// One argument of a case that runs over rows of them: a local of this type and name wherever
// the body says Stmt::Kind::Data.
struct Column {
    Type        type;                // no reference, no top-level const: what a local is declared with
    std::string name;
};

// A case that runs over a table of arguments: a TEST_P over a struct of them (GoogleTest), a
// `<name>_data()` slot and QFETCH (Qt Test). The plan has the columns and one row of default
// values; the rows that matter are for the person writing the test.
struct DataTable {
    std::vector<Column> columns;
};

struct TestCase {
    std::string                name;          // a valid identifier
    std::string                target;        // "acme::Foo::size() const": for messages only
    std::vector<std::string>   guards;        // the `#if` conditions the target is under (Method::guards), outermost first
    std::vector<Stmt>          body;
    std::optional<DataTable>   data;
};

// The class a TEST_F test runs in: it owns the object under test and whatever
// that object is built from. Written for GoogleTest; Qt Test has init() and
// cleanup() for the same job and a plan for it is still to come.
struct Fixture {
    std::string              name;         // "FooTest": the suite's name, as TEST_F wants it
    // Data members in declaration order, as declarations without the ';':
    // `int initial{}`, `Foo sut`. A declaration the tool could not make is a
    // Code statement with commented_out, and a Comment before it says why.
    std::vector<Stmt>        members;
    // The constructor's initialiser list, `sut(initial)`; no constructor if empty.
    std::vector<std::string> initialisers;
    std::vector<Stmt>        set_up;       // SetUp(); no override if empty
    std::vector<Stmt>        tear_down;    // TearDown(); no override if empty
};

struct TestSuite {
    std::string              name;            // "FooTest"; QTest: the test class
    std::string              target_class;    // "Foo"
    std::vector<std::string> ns;              // emitted inside these namespaces
    std::vector<std::string> guards;          // the `#if` conditions the class is under (Class::guards), outermost first
    std::optional<Fixture>   fixture;
    std::vector<TestCase>    cases;
};

// One generated test file.
struct TestPlan {
    std::string              file;            // output path, relative to --out
    std::vector<Include>     includes;
    std::vector<TestSuite>   suites;
};

// One virtual function of an interface, as the mock of it must declare it.
struct MockMethod {
    std::string       name;
    Type              ret;
    std::vector<Type> params;
    bool              is_const = false;
    bool              is_noexcept = false;
    RefKind           ref_qualifier = RefKind::None;
};

// A gmock mock of an interface: one header of its own (GoogleTest only).
struct MockClass {
    std::string              file;            // output path, relative to --out: "mock_sensor.h"
    std::string              name;            // "MockSensor"
    std::string              base;            // "Sensor"
    std::vector<std::string> ns;              // the interface's: the mock sits next to it
    std::string              header;          // the include of the interface's header
    std::vector<MockMethod>  methods;         // declaration order; the interface's own first
    // One line each, written above the class as "TODO(mt): ..." -- what the mock leaves out and why.
    std::vector<std::string> notes;
};

}  // namespace mt::ir

#endif  // MT_IR_MODEL_H
