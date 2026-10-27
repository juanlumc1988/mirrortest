# Writing tests that mirrortest understands

`mirrortest gen-impl` reads a test file that does not compile yet and works out
what the code under test must look like. It does not run your tests and it does
not know your project: it reads **only what the test says**. This page is about
saying it in a way the tool can read — and what to do when that is not
possible.

The rules below are the user's view of [docs/design.md](design.md), section 7,
which is the authority for every detail. The other direction, `gen-tests`, has
its own [section](#test-skeletons-from-a-header).

- [What it needs from a test](#what-it-needs-from-a-test)
- [Which files it writes, and where](#which-files-it-writes-and-where)
- [Merging](#merging)
- [Annotations](#annotations)
- [What the stubs do](#what-the-stubs-do)
- [Test skeletons from a header](#test-skeletons-from-a-header)
- [Configuration](#configuration)
- [Diagnostics](#diagnostics)
- [Limits](#limits)

## What it needs from a test

### Say the type where the type matters

A generated signature is only as good as the evidence for it. The strongest
evidence is a **declared type**; after that, a **comparison** with a typed
value; after that, a **literal**.

```cpp
TEST(RepoTest, Lookup) {
    Repo repo;
    std::string name = repo.name();           // name() returns std::string
    EXPECT_EQ(repo.size(), 3u);               // size() returns unsigned int
    EXPECT_TRUE(repo.contains(7));            // contains(int) returns bool
    std::vector<int> ids = repo.ids();        // ids() returns std::vector<int>
    repo.limit(7u);                           // limit(unsigned int), returning void
}
```

What a result is compared with decides its type:

| In the test | The type |
|---|---|
| `EXPECT_EQ(x.f(), 3u)` (and `NE`, `LT`, `LE`, `GT`, `GE`) | the type of the other side: `unsigned int` |
| `EXPECT_TRUE(x.f())`, `if (x.f())`, `!x.f()`, `x.f() && y` | `bool` |
| `EXPECT_STREQ(x.f(), "a")` | `const char*` |
| `EXPECT_FLOAT_EQ`, `EXPECT_DOUBLE_EQ`, `EXPECT_NEAR` | `float`, `double`, `double` |
| `T v = x.f();` | `T` |
| `v = x.f();` with `v` declared `T` | `T` |
| `EXPECT_EQ(x.f(), std::string("a"))`, `QString("a")` | the cast's type |
| `x.f();` and nothing else | `void` (with a warning) |

If the result is never needed, the method returns `void`. One use that does need
a value wins over any number of statements that discard it.

### Literals and their suffixes

A literal has a type, and the type of an argument becomes the type of a
parameter. Write the suffix when the type matters:

| Literal | Type |
|---|---|
| `3`, `-7`, `0xFF`, `0b101`, `1'000` | `int` |
| `3u`, `3l`, `3ul`, `3ll`, `3ull` | `unsigned int`, `long`, `unsigned long`, `long long`, `unsigned long long` |
| `1.5`, `1.5f`, `1.5l` | `double`, `float`, `long double` |
| `'a'` | `char` |
| `true` | `bool` |
| `"text"` | `const char*` (change it with `string_literal_param`) |
| `"text"s`, `"text"sv` | `std::string`, `std::string_view` |
| `5ms`, `2h` | `std::chrono::milliseconds`, `std::chrono::hours` |

`long` is 4 bytes on 32-bit targets and on 64-bit Windows and 8 on 64-bit Linux, so
a literal that only fits one of them gets a warning. A literal larger than `int` without a suffix is `long
long`.

### Don't use `auto` for the results of the code under test

```cpp
auto item = repo.find(1);          // error: mt-auto -- what is find() returning?
Item item = repo.find(1);          // fine
```

`auto` hides exactly the thing the tool is trying to learn. It is an **error**
by default (nothing is written; the message gives the line). Three ways out:

- declare the type (best);
- let a later use in the test say it: `EXPECT_EQ(item, expected)` where `expected`
  has a declared type, or `Item copy = item;`;
- annotate the call: `// @mt: returns Item` (see [Annotations](#annotations));
- or accept a guess for the whole run: `--auto-policy todo` turns the error into a
  warning, uses `int` (or `placeholder_type`) and leaves a `TODO(mt)`.

`auto x = Foo(3);` is fine — the right-hand side names its type.

### A call on the result of another call

```cpp
EXPECT_EQ(repo.find(1).name(), std::string("root"));
```

For `name()` to be generated in the right class, the tool must know what
`find(1)` returns. Say so once and the chain resolves:

```cpp
// @mt: returns Item
EXPECT_EQ(repo.find(1).name(), std::string("root"));
```

or declare it elsewhere (`Item item = repo.find(1);`). Without it, you get
`mt-chained-call`.

### Operators

A variable of a generated class that the test compares or indexes asks for the
operator it uses:

```cpp
Point a(1, 2), b(1, 2);
EXPECT_TRUE(a == b);       // bool operator==(const Point& b) const;
EXPECT_LT(a, b);           // bool operator<(const Point& b) const;
EXPECT_EQ(a[0], 1);        // int operator[](int) const;
grid[3] = 7;               // int& operator[](int);
```

`==`, `!=`, `<`, `<=`, `>` and `>=` (also as `EXPECT_EQ` … `EXPECT_GE`,
`QCOMPARE` and `QCOMPARE_NE` …) are `const` and return `bool`, however they were
reached; the parameter is whatever the other side is (`const Point&`, `int`).
`EXPECT_EQ(sensor.position(), origin)` makes `position()` return `origin`'s type
and that type the operator. `operator[]` is `const` and returns what it is
compared with or assigned to; if the test assigns to it (`grid[3] = 7`,
`grid[3] += 1`) it returns a reference and is not `const`. Other operators
(`+`, `<<`, unary ones) are not inferred, and neither is an operator whose
left operand is not an object of a generated class (`5 == a` needs a free
function). A variable has to be declared with its class for this: the result of
a call you have not typed (`EXPECT_EQ(a.next(), b.next())`) says nothing.

### Enums

`Color::Red` as a value, when `Color` is not a class the test builds or calls, not a
namespace and not declared anywhere, is taken for an enum:

```cpp
paint.set(Color::Red);                  // void set(Color arg1);
EXPECT_EQ(paint.color(), Color::Blue);  // Color color();
```

It generates `enum class Color { Red, Blue };` (each enumerator once, in order of use,
with a note of where) in the header that matches, else the first the test includes, and
the headers that use it include that one. Not an enum: anything the libraries or your
headers declare, a name that does not begin with a capital (`acme::kMax` is a namespace
constant), one the test calls (`Color::parse()`), builds with arguments or calls
methods on. `Limits::kMax` with a capital and nothing else looks like an enum and is
taken for one: the first enumerator's note says why, so you can fix it by hand. Enums
are always `enum class`: a test that writes `Red` without the qualifier needs it edited.

### Exceptions

`EXPECT_THROW(parser.parse("x"), ParseError)` names an exception class but not what it
derives from, and a class that does not derive from anything cannot be thrown. So a
`ParseError` that no header provides is reported (`mt-unknown-type`), and you declare
it. To have it generated, say what such classes derive from in the configuration:

```json
{ "exception_base": "std::runtime_error" }
```

```cpp
class ParseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;   // throw ParseError("why") works
};
```

in the header that matches (`parse_error.h`) or the first the test includes. Standard
exceptions (`std::runtime_error`), the file's own and your headers' are not generated.
With `no_exceptions` the option does nothing.

### Constructors

One constructor is generated per **arity** used. A single parameter is
`explicit`, unless the test copy-initialises (`Foo f = 5;`). A default
constructor is generated only when the test uses it and another constructor
exists; otherwise the implicit one is enough. Copy and move are never
generated.

```cpp
Foo a;        // Foo()        (only if some other constructor is also used)
Foo b(3);     // explicit Foo(int)
Foo c(3, 4);  // Foo(int, int)
```

`Foo f();` declares a *function*, not an object; the tool warns about the most
vexing parse and does not see a constructor there.

### Static methods, namespaces, free functions

- `Foo::create()` with a capitalised `Foo` is a **static method** of a class
  `Foo`. `acme::run()` is a **function** in the namespace `acme` when the file
  shows `acme` to be a namespace (`namespace acme`, `using namespace acme`, a
  qualified type). When the tool cannot tell it guesses by the capital letter
  and says so (`mt-ambiguous-scope`).
- A class is put in the namespace the test says: `acme::Foo`, a test written
  inside `namespace acme { ... }`, or one `using namespace acme;`. With several
  `using namespace` the namespace is the configuration's `default_namespace`,
  else global, with a warning.
- Unqualified calls (`clamp(5, 0, 3)`) are free functions of the code under test.
  Not generated: names in `ignore_macros`, `ALL_CAPS` names (macros — unless the
  test also uses the same name as a class), the C library (`strlen`, `memcpy`,
  `assert`, …), and `std::` calls.
- `Foo(3)` as an expression is a construction of `Foo` when the file uses `Foo`
  as a class elsewhere; `acme::Foo(3)` likewise. Otherwise it is a call to a
  function called `Foo`.

### Fixtures

`TEST_F` bodies see the members of the fixture class declared in the same file:

```cpp
class StackTest : public ::testing::Test {
protected:
    void SetUp() override { stack_.push(1); }
    Stack stack_;                        // needs Stack to be default constructible
};

TEST_F(StackTest, Pops) { EXPECT_EQ(stack_.pop(), 1); }
```

A member of class type is a construction too (`Stack stack_;` needs a default
constructor; `Stack stack_{4};` a constructor taking an `int`), and so are the
constructor's initialisers. A member the constructor initialises is not also
default-constructed — if that constructor is defined *inside* the class.

### Fakes and mocks

The code under test is written against an interface, and the test hands it a
double: a class of the file that derives from the interface.

```cpp
class MockSensor : public Sensor {
public:
    MOCK_METHOD(int, read, (), (override));
    MOCK_METHOD(std::string, name, (), (const, override));
};

TEST(ControllerTest, ReportsWhatTheSensorReads) {
    testing::NiceMock<MockSensor> sensor;
    Controller controller(&sensor);       // Controller(Sensor*), not Controller(MockSensor*)
    EXPECT_CALL(sensor, read()).WillOnce(testing::Return(42));
    EXPECT_EQ(controller.value(), 42);
}
```

- `Sensor` is generated as an interface: `virtual ~Sensor() = default;` and one
  pure virtual function for each function the double overrides —with the
  signature the double gives it— and `Controller` takes a `Sensor*`. The same for
  a fake you write by hand (it needs `override` on the functions, which is what
  tells them from its own), and for the old macros (`MOCK_METHOD1(f, void(int))`,
  `MOCK_CONST_METHOD0(g, bool())`).
- Whatever the double is wrapped in (`NiceMock`, `StrictMock`, `NaggyMock`), or
  held by (`std::unique_ptr<MockSensor>`, `std::vector<FakeSensor*>`), the
  parameter that receives it is typed with the interface.
- If the interface already exists in a header the test includes, nothing is
  generated for it, and the parameter is still typed with it.
- A double with several bases is attributed to the first one, with a note.
- Put the `#include` of the interface's header in the test, as you must for the
  test to compile: it says where `Sensor` goes.
- Fakes and fixtures in an anonymous namespace are read like any other.

### Constness, pointers, smart pointers

- A method called through a `const` reference, a `const` object or a `const`
  fixture member is `const`. Nothing else makes a method `const`; when a method
  with no arguments is only compared, the note "consider making this const"
  tells you to look.
- Arguments: a class-typed variable is taken as `const T&`; fundamental types,
  enums and pointers by value; `std::move(x)` makes the parameter by-value (with
  a note).
- `std::make_unique<Foo>(3)`, `std::make_shared<Foo>(3)` and `new Foo(3)`
  construct `Foo` with those arguments; `p->run()` is a call on `Foo`.
- `using namespace std;` is understood, and the generated header says
  `std::string`, never `string`.

### Qt Test

`gen-impl` reads Qt Test files as it reads GoogleTest ones: the test class is
yours (never generated), the slots are the tests, and `QCOMPARE`, `QVERIFY` and
the `QTRY_` family say what `EXPECT_EQ` and `EXPECT_TRUE` say. It tells which
framework a file uses from its includes, a slot, or `QTEST_MAIN`; `--framework
qtest` settles it when nothing does.

- `QCOMPARE(foo.name(), "x")` makes `name()` return `QString`, not
  `std::string`.
- `QFETCH(T, v)` declares `v` of type `T`; the type is used but not
  constructed, so a class you fetch does not need a default constructor. Name
  `T` as you mean it: `QFETCH(acme::Foo, foo)` makes `foo.run()` a call on
  `acme::Foo`.
- `QTest::addColumn<acme::Foo>("foo")` asks for `acme::Foo` to exist.
- `QTEST(actual, "column")` says nothing about `actual`; compare with
  `QCOMPARE` where the type matters.
- `QBENCHMARK { ... }` is read as the block it guards, and `#include
  "tst_foo.moc"` is left to the build.
- A signal given to `QSignalSpy` as `&Class::signal` is not seen, so it is not
  declared.

### What is *not* generated

Anything the test declares itself (fixtures, doubles, helper classes, enums,
aliases), the standard library, Qt's classes and functions, GoogleTest and gmock
(`NiceMock`, `Test`, … are theirs under `using namespace testing;` too), and every class or
function that an existing header the test includes declares (see below). List
more with `external_types` and `external_functions`.

Exception types in `EXPECT_THROW(stmt, MyError)` are not generated either — an
exception class needs a base the test does not state. Declare it in a header
the test includes, or you get `mt-unknown-type`, or say what such classes derive
from with `exception_base` (see Exceptions above).

## Which files it writes, and where

The headers to generate are **the `#include "..."` of the test that do not
exist**. They are looked for next to the test, then in the `-I` directories in
order, then in `--out-include` (or `--out`). `#include <...>`, GoogleTest, Qt and
the standard library are never generated.

```console
$ mirrortest gen-impl tests/foo_test.cpp --out src -I include
```

If the project is built with CMake you do not have to repeat its include
directories: `--compile-commands build/compile_commands.json` (written by
`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`) adds the `-I`, `-isystem` and `-iquote`
directories of every compilation in the file, after the `-I` you give. It works in
`gen-tests` too. Every entry counts, whether or not it is the test you are
generating; if two targets of the project use different directories under the same
name, give a database with only the one that matters, or the `-I` yourself.

- A class goes to **the header whose name matches** (`FooBar` → `foo_bar.h`,
  ignoring case, `_` and `-`), else to the first header the test asks for. With
  no header to generate at all, each class and function gets a file named from
  it (`HttpServer` → `http_server.h`) and an `mt-no-include` warning: add the
  `#include` to the test.
- A header the test includes that nothing was assigned to is still written, empty,
  so that the test compiles.
- A header that exists on `-I` or next to the test is **your** API: its classes
  are not generated again, and a generated header that mentions one of its types
  includes it. The exception is the output directory's own headers: they are
  generated again, however `-I` leads there, and the next point applies.
- Headers go to `<out-include>/<path as included>`; sources next to them under
  `<out-src>` (`acme/foo.h` → `acme/foo.cpp`, whatever the number of classes). A
  header with nothing to define has no source file.
- **Nothing is ever destroyed.** A file that exists and differs is left alone and
  the new text goes to `<file>.new`; `--overwrite force` replaces it. A re-run
  that changes nothing says `unchanged` and does not touch the files.
- **`--overwrite merge`** is for the second test of a TDD session: it adds what
  the tests now ask for to the files of the earlier run and leaves everything
  else — see [Merging](#merging).
- If anything is an error, **nothing is written**. `--dry-run` shows the diffs,
  `--check` exits 4 if anything would change.
- Two headers that include each other are broken with a forward declaration where
  only a name is needed; two classes holding each other *by value* cannot be
  (`mt-unsupported`).

Several test files in one run are fine as long as they do not write the same
file with different content.

## Merging

```console
$ mirrortest gen-impl tests/foo_test.cpp --out src --overwrite merge
merged 2 members into src/acme/foo.h
merged 2 members into src/acme/foo.cpp
```

The generated files fence what belongs to the tool in regions:

```cpp
class Foo {
public:
    // <mt:begin acme::Foo>
    ...
    // <mt:end acme::Foo>
};
```

A merge **only inserts**, before an `<mt:end>` line, with that line's
indentation and the file's own line endings. Code outside the markers is never
edited and nothing is ever deleted: implement a stub, add members of your own,
move one out of the region — all safe.

- A member is "already there" if the class has one of that **name and number of
  parameters**, wherever in the class it is (overloads of one arity are told
  apart by their parameter types). If it is there with other types
  (`int size() const` where the tests say `unsigned int size()`), it is left as
  it is with an `mt-merge-mismatch` warning.
- A definition goes into the source only if the header, as merged, declares that
  member, and the source does not already define it.
- Missing `#include` lines go into the `includes` region (a warning if there is
  none).
- Where it cannot do that safely the new text goes to a `.new` file beside the
  original with a warning: a class without a region (`mt-merge-no-region`), or a
  result that would parse worse than the original (`mt-merge-broke`). Regions that
  are malformed — nested, unclosed, repeated — are an error
  (`mt-merge-region`) and nothing is written until you fix them.
- Do not delete the `// <mt:begin>` / `// <mt:end>` lines of a class you want
  merged into.

`--dry-run` shows the merge as a diff first; `--check` exits 4 if there is
anything to merge.

## Annotations

When the code of the test cannot say it, a comment can. Annotations are line
comments starting `// @mt:`; a typo such as `// @mt returns int` is an error
rather than an ordinary comment.

**On a statement** (the line it ends, or the statement that follows it). The
statement must contain exactly one call of the code under test:

```cpp
// @mt: returns std::vector<int>
auto ids = repo.ids();

helper(repo);                 // @mt: ignore          -- not code under test
foo.set(1);                   // @mt: overload        -- another overload, not a conflict
```

**At file level**, naming the member:

```cpp
// @mt: Repo::count returns std::size_t
// @mt: Repo::name const
// @mt: Repo::at param 1 std::size_t
// @mt: Repo::create static
// @mt: acme::Repo::close noexcept; acme::Repo::open virtual
// @mt: class Cache namespace acme::util
// @mt: class Cache header "cache.h"
```

The same facts can go in the `overrides` of the configuration file. The
annotation, being nearer the code, wins.

## What the stubs do

The default is to **fail loudly**: a generated function throws, so a test that
calls it fails with a message that names the signature.

```cpp
unsigned int Foo::size() const {
    throw std::logic_error("mirrortest: not implemented: acme::Foo::size() const");
}
```

| `--stub-policy` | Body | Needs |
|---|---|---|
| `throw` (default) | `throw std::logic_error("mirrortest: not implemented: ...");` | `<stdexcept>` |
| `abort` | `std::abort();` | `<cstdlib>` |
| `default` | `return {};` (nothing for `void`) | — |
| `hook` | `mt_not_implemented("...");`, then `return {};` (nothing for `void`) | `"mt_not_implemented.h"`, written for you |

`--no-exceptions` makes `throw` behave as `abort`, for projects that build with
`-fno-exceptions` (and `--no-rtti` is a promise that no `typeid` or
`dynamic_cast` is emitted). `--no-stl` does the same, because a stub that throws
needs `std::logic_error`, and also reports every generated header that has to include a
header of the standard library for a type your tests use (`mt-no-stl`; `<cstddef>`,
`<cstdint>`, `<cstdio>`, `<cstdlib>` and Qt's do not count): the header is written as the
test has it, and the warning tells you where the project's rule and the test disagree. `default` can make a test pass by accident — a stub
that returns `0` satisfies `EXPECT_EQ(x.f(), 0)` — so it says
`mt-stub-may-pass`; a return type that `{}` cannot build (a reference, a class
without a default constructor) falls back to `abort` with a warning. List types
you know are default-constructible in `default_constructible_types`.

### `hook`: let every test run

Qt Test ends the program at the first exception, so with `throw` the slots after
the first one never say which stubs they are missing. With `--stub-policy hook`
each stub calls `mt_not_implemented("<signature>")` from a header the tool writes
for you, `mt_not_implemented.h`, next to the others. By default that **throws** (or,
with `--no-exceptions` or `--no-stl`, prints the signature and aborts), so nothing
changes until you install a handler that makes the running test fail and returns:

```cpp
#include <QtTest>
#include "mt_not_implemented.h"

namespace {
void fail_the_slot(const char* signature) { QTest::qFail(signature, __FILE__, __LINE__); }
struct Install { Install() { mt_set_not_implemented_handler(&fail_the_slot); } } install;
}
```

Put it in any file of the test program, or in the test itself — `#include
"mt_not_implemented.h"` there is not something the tool generates, and neither are
`mt_set_not_implemented_handler` and the other names of that header. With it in the
program, every slot runs and each reports the signatures it needs
(`FAIL!  : TestCounter::incrementsByOne(zero) acme::Counter::increment()`). In GoogleTest
use `ADD_FAILURE() << signature`. After the hook returns, the stub gives an empty
value as `default` does; a type that `{}` cannot build aborts, with a warning. A
handler that returns *without* failing the test lets it pass without the code it
tests, so make it fail.

Constructors and destructors are always empty, so a test runs on to the first
stub that fails instead of stopping at its first line. Parameter names are
commented out in definitions (`int /*arg1*/`), so an unused parameter is not a
`-Wunused-parameter`.

Every generated declaration that rests on a guess carries a comment saying what
it rests on:

```cpp
// TODO(mt): return type inferred from literal 3u (tests/foo_test.cpp:12)
unsigned int size() const;
```

and everything between `// <mt:begin ...>` and `// <mt:end ...>` markers belongs
to the tool, which is what lets a later run find its own output again.

## Test skeletons from a header

```console
$ mirrortest gen-tests include/acme/foo.h -I include --out tests/
created tests/foo_test.cpp
```

One file per class (`naming.gtest_file`, `{stem}_test.cpp`), one `TEST` per public
constructor, destructor and method, in the namespace of the class and a suite
called after it (`FooTest`). Overloads are told apart by their types
(`set_int`, `set_std_string`), operators have names (`OperatorEq`,
`ConversionToBool`).

Every test starts with `FAIL() << "TODO(mt): write the test for ..."` (with
`--placeholder skip`, `GTEST_SKIP()`), so a skeleton is red until somebody writes
it. What follows the placeholder is **compiled but never run**: it builds the
object (`Foo sut;`, or through the simplest public constructor), declares one
local per argument (`int count{};`), makes the call and leaves the assertion as a
`TODO(mt)`. It checks that the API still has this shape without ever calling a
legacy method with zero values.

Where it cannot do that, it says so and keeps the work as a comment: an
argument of a type it does not know (`// TODO(mt): an argument of type 'Mystery'
cannot be built here`, then the `sut`, the locals and the call commented out
together, every argument named), an abstract class, a class with no constructor
it can call. Say that a type can be built with `default_constructible_types` in
the configuration.

How the test includes the header: `--include-as PATH`, else the path below the
first `-I` directory that contains it (those of `--compile-commands` come after the
ones given with `-I`), else the file name.

By default the object under test is built inside each test. With `{"gtest":
{"fixture": true}}` in the configuration the file has one fixture class that owns
it, and the tests are `TEST_F`: the arguments its constructor needs are members of
the fixture, `SetUp()` is there for the state every test starts from, and a
constructor test builds a second object called `object`. Mind the price: the
fixture builds `sut` before the test body, so the class's constructor runs even
though the test is still red, which the plain skeleton never does. Use it for
classes whose construction is safe.

### Reading the header with libclang

By default the header is read by tree-sitter, which needs nothing and tolerates a
header that does not compile. If the project's headers do compile, `--frontend
libclang` (or `"frontend": "libclang"`) has a compiler front end read them instead.
It is in the build only if it was asked for (`-DMT_WITH_LIBCLANG=ON`, needs
libclang: `apt install libclang-dev`, any version from 14 to 22, which are the ones tested); otherwise the
option says so and stops.

```console
$ mirrortest gen-tests include/acme/port.h --out tests/ -I include \
      --frontend libclang --clang-arg=-DUSE_DMA --std c++17
```

What changes: only the branch of an `#if` that builds is read (tree-sitter keeps
both), what a macro declares is read (`GETTER(int, size)` is a method; no
`ignore_macros` for an export macro), a pointer to member or a struct named by a
`typedef` is read as it is. What does not: the types are still the ones **you
wrote** (`unsigned` stays `unsigned`), so the tests it writes are the ones
tree-sitter writes for the same header, byte for byte.

libclang needs to find what the header includes: the `-I` of the run (and those of
`--compile-commands`) are given to it, and `--clang-arg` adds the rest (`-DNAME=1`,
`-isystem /abs/dir`; written absolute, because libclang resolves a relative path
against the process's directory). The standard of `--std` is passed too. What it
cannot resolve — a type nobody declared, a header it did not find — is reported
as an `mt-parse` warning at its place ("libclang: unknown type name 'Mystery'"),
and the declaration is still read with the type as you wrote it. Qt's headers are
not needed for the signal and slot roles, only to resolve `QObject` and the value
classes. Tests are always read by tree-sitter: they are code that may not compile yet.

### Cases over a table of arguments

By default a method with arguments is one test with a local for each. With
`{"gtest": {"parameterised": true}}` it is a `TEST_P`: a struct of the arguments
(`CalcTest_add_Params`), the test reading one row into locals of the same names, and

```cpp
// TODO(mt): one CalcTest_add_Params{...} for each case to run
INSTANTIATE_TEST_SUITE_P(Cases, CalcTest_add, ::testing::Values(CalcTest_add_Params{}));
```

with a single row of default values, so the test is as red as before; add the
rows that matter. It combines with `fixture` (the class derives from the
fixture) and needs GoogleTest 1.10 or later. With `{"qtest": {"data_driven":
true}}` Qt Test gets a `<name>_data()` slot with one column for each argument and
a `default` row, and `QFETCH` in the test.

Constructors, the destructor, signals, methods without arguments, and calls with
an argument that is a mock, an rvalue reference or cannot be copied (a
`unique_ptr`) stay as they were. For Qt Test also a type with a comma, and
`const char*`, which `QTest::addColumn` refuses.

### Inheritance and refused copies

A class that derives from others starts its suite with an `Inherits` test: a
`static_assert(std::is_base_of<Base, Derived>::value, …)` for each base, whatever
its access, so a refactoring that moves the class in the hierarchy breaks the
build of the test. A class that deletes its copy or its move (`= delete`, or
`Q_DISABLE_COPY`) gets `IsNotCopyable` and `IsNotMovable`: `static_assert(!std::is_copy_constructible<T>::value, …)`
and the like, one for each of the constructor and the assignment it deletes. These are
the only generated tests that are not red: there is nothing left to write, and they
pass when the file compiles. What the class inherits is not tested again in its suite;
test it with the base. An overriding function is the class's own and has its test.
A defaulted or implicit copy is not asserted.

### Nested classes

A public class nested in a class is tested like any other, by the name that
reaches it from the namespace: `Tree::Node sut;` in `TEST(Tree_NodeTest, …)`,
written to `tree_node_test.cpp`. A protected or private nested class has no test
(the test could not name it), nor does a nested class template, and a nested
class with nothing public to test (a struct of data) is not mentioned. Mocks
(`gtest.mocks`) are not made of nested interfaces.

### Class templates

A class template is tested for the instantiations you list in the
configuration; the key is its qualified name and each entry the text between the
`<>`:

```json
{ "template_instantiations": { "acme::Box": ["int", "std::string, 8"] } }
```

You get a file and a suite for each: `box_int_test.cpp` with `Box_intTest`, and
`box_std_string_8_test.cpp`, with `Box<int> sut;` where the plain case has
`Box sut;`. A parameter you leave out takes its default. The members have to be in
the header (a template has no `.cpp`), and a template you do not list is skipped
with a warning that shows the line to add. Mocks (`gtest.mocks`) are not made of
instantiations.

### Mocks of interfaces

With `{"gtest": {"mocks": true}}` in the configuration, every **interface** of the
header -- a class with a pure virtual function, its own or one a base in the same
header left pure -- also gets a gmock mock in a header of its own:

```cpp
// mock_sensor.h
#include <gmock/gmock.h>
#include "sensor.h"

class MockSensor : public Sensor {
public:
    MOCK_METHOD(int, read, (), (override));
    MOCK_METHOD((std::map<int, std::string>), table, ((const std::map<int, int>&)), (const, override));
};
```

One `MOCK_METHOD` for each pure virtual function that nothing in between
implements (public or not: a mock that left one out would still be abstract),
with `const`, `noexcept`, `ref(&)`/`ref(&&)` and `override`. A return type or
argument with a comma in it is in parentheses, which is how gmock is told the
comma is not the macro's. Names are `naming.mock_class` (`Mock{Class}`) and
`naming.mock_file` (`mock_{stem}.h`); the mock sits in the namespace of the
interface and next to the tests (`--out`).

What it cannot mock it says as a `TODO(mt)` above the class: an operator, a
conversion operator, a C-style variadic function, and a base class that is not
in the header (it may have pure functions of its own). Class templates get no
mock, and neither does an interface whose mock name the header declares already.
Nothing is written without the option, and Qt Test has no mocks.

The mock is also what the other tests build with. A class that takes an interface
of the header -- `Controller(Sensor& sensor, Logger* logger)` -- gets
`testing::NiceMock<MockSensor> sensor;` and `Controller sut(sensor, &logger);`
instead of a commented-out test, in the constructor under `sut`, in a method's
arguments and in a fixture's members; a `std::unique_ptr<Sensor>` is given a
`new testing::NiceMock<MockSensor>` and a `std::shared_ptr<Sensor>` a
`std::make_shared<...>()`. The test then includes the mock header. An interface
whose mock needs to be finished by hand (it has a `TODO(mt)`) is not used.

With `--framework qtest` the file is `tst_foo.cpp` and holds a `QObject` called
`TestFoo` whose private slots are the tests, an entry point (`--qt-main
appless|guiless|gui`, `guiless` by default) and `#include "tst_foo.moc"` -- the
name moc's output must have for AUTOMOC to find it, which is why the name of the
file is not free. The placeholder is `QFAIL(...)`, or `QSKIP(...)` with
`--placeholder skip`. A signal is a test here: `sut` and a `QSignalSpy` on it,
and a `TODO(mt)` to make it emit and to look at the spy. A method called `init`,
`cleanup`, `initTestCase` or `cleanupTestCase`, or one that ends in `_data`, would
be a function Qt Test calls by itself, so its test is a slot with `_call` after
the name (`init_call`).

Not tested, and why: signals; deleted, protected and private members; inherited members (see Inheritance and refused copies); member
templates, and class templates you do not list in `template_instantiations`; free functions (the
run prints a note). A member declared under `#if` has its test under the same `#if`
(`#if defined(USE_DMA)` … `#endif`; in the `#else` of it `#if !(defined(USE_DMA))`), so the tests build in every configuration
and a configuration has the tests of the members it declares; a class under `#if` has all its tests under it, and the
`#ifndef X_H` / `#define X_H` that guards a header against being included twice is no condition. The conditions
use the macros of the header, which is included first, and the compiler flags of the build that has the tests.
`--frontend libclang` reads the branch that builds, so it writes no `#if`. Existing
files are never overwritten: a differing one gets a `.new` beside it, and
`--overwrite merge` does the same for tests for now.

## Configuration

`--config FILE.json` (comments allowed) or `FILE.yaml`. Precedence: built-in defaults, then the
file, then the command line. Unknown keys and wrong values are errors, all
reported at once. The full list is in [docs/design.md](design.md), section 14;
the ones people reach for first:

```json
{
  "std": "c++14",
  "default_namespace": "acme",
  "guard": "pragma-once",
  "file_header": "// Copyright (c) ACME. All rights reserved.",
  "stub_policy": "abort",
  "auto_policy": "todo",
  "placeholder_type": "int",
  "string_literal_param": "const std::string&",
  "type_map": { "unsigned int": "std::uint32_t" },
  "by_value_types": ["Handle"],
  "external_types": ["LegacyDriver"],
  "external_functions": ["board_reset"],
  "ignore_macros": ["MY_CHECK"],
  "exception_base": "std::runtime_error",
  "overrides": { "acme::Repo::count": { "returns": "std::size_t" } }
}
```

The same configuration as YAML (`--config mt.yaml`, or `.yml`), if you prefer to
comment it and to write the header as a block:

```yaml
std: c++14
default_namespace: acme
guard: pragma-once
file_header: |
  // Copyright (c) ACME. All rights reserved.
stub_policy: abort
type_map:
  unsigned int: std::uint32_t
by_value_types: [Handle]
```

The keys and the errors are those of the JSON. A plain `true`, `false` and number have that type;
anything quoted is text, and `yes`/`no`/`on`/`off` are not booleans. Anchors and aliases work; merge
keys (`<<`), tags other than `!!str`, a key twice in one mapping and more than one document are
errors. A build made with `-DMT_WITH_YAML=OFF` refuses a YAML file and says so.

`type_map` rewrites what was *inferred* (never what you declared): handy to turn
every `unsigned int` into `std::uint32_t`. `file_header` is emitted at the top of
every file, and replaces nothing — the generator's own `// Generated by
mirrortest ...` line follows it. `--templates DIR` overrides the layout
templates; see [templates.md](templates.md).

## Diagnostics

Printed in the GCC format (`file:line:col: severity: message [code]`), sorted,
and understood by editors and CI. `--strict` makes every warning an error. Exit
codes: 0 success, 1 diagnostics with errors, 2 usage, 3 I/O, 4 `--check` found
differences.

| Code | Meaning | What to do |
|---|---|---|
| `mt-auto` | `auto` holds a result of the code under test | declare the type, or `// @mt: returns T`, or `--auto-policy todo` |
| `mt-unknown-return`, `mt-unknown-param` | nothing says the type | compare it with a typed value, declare it, or annotate |
| `mt-chained-call` | a call on a result whose type is unknown | `// @mt: returns T` on the statement |
| `mt-unused-result` | the result is never used, so the method is `void` | expected for commands; otherwise use the value |
| `mt-conflict` | two uses disagree; the better evidence won | make the uses agree, or `// @mt: overload` |
| `mt-literal`, `mt-literal-range` | an unsupported or out-of-range literal | add a suffix, or cast |
| `mt-vexing-parse` | `Foo f();` declares a function | write `Foo f;` |
| `mt-ambiguous-scope` | `X::m()` could be a class or a namespace; several `using namespace` | qualify the name, or `default_namespace`, or `// @mt: class X namespace N` |
| `mt-unknown-receiver`, `mt-unknown-fixture` | a receiver or fixture could not be located | declare it in the test |
| `mt-unknown-type` | an exception type is neither known nor generated | include its header, or set `exception_base` |
| `mt-no-include` | no `#include` of the test claims a class or function | add it to the test, or `// @mt: class X header "x.h"` |
| `mt-annotation` | a malformed annotation, or one that does not hit exactly one call | fix the comment; use the file-level form for several calls |
| `mt-unsupported` | a construct outside the supported subset (see [Limits](#limits)), a QTest file, or two test files that write the same file differently | see the message |
| `mt-stub-may-pass` | `--stub-policy default` | by design; a reminder |
| `mt-no-stl` | `--no-stl`, and a generated header that includes a standard library header for a type of the test | use a type the project has, or `type_map` |
| `mt-framework` | the test framework could not be told, or the file uses both | `--framework gtest` |
| `mt-merge-region` | an error: the regions of a file being merged are nested, unclosed or repeated | fix the `// <mt:begin>` / `// <mt:end>` lines |
| `mt-merge-no-region`, `mt-merge-mismatch`, `mt-merge-broke` | warnings of `--overwrite merge`: a class has no region (so a `.new` is written), a member exists with other types (left alone), a merge would have broken the file | see [Merging](#merging) |
| `mt-config`, `mt-io` | a bad configuration file, a file that cannot be read or written | fix the file or path |

## Limits

Honest list of what it does not do (yet):

- Merging adds members; it does not reconcile a changed one (a mismatch is
  reported) and never removes a member the tests no longer mention.
- One test file per header, in practice: two files that would write the same
  header differently are an error.
- Templates (`Box<int>`) get a warning and a plain class; template function
  calls get `mt-unsupported`.
- Inheritance is inferred only from test doubles (above); conversion operators
  are not inferred from tests, and of the other operators only the comparisons
  and `operator[]` (above).
- `EXPECT_CALL(mock, f(...))` says nothing about `f`: declare what the code
  under test calls by calling it.
- Namespace-scope variables in a test file are not visited.
- `TYPED_TEST` bodies are skipped (with a warning).
- A constructor defined *outside* its class does not stop the member it
  initialises from also asking for a default constructor.
- With `--frontend libclang`, a header that does not compile is read with warnings, and whatever libclang
  cannot resolve is as unknown to it as to anybody; class template specialisations, function templates
  and inherited constructors are skipped with the same warnings as with tree-sitter.
