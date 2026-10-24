// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Which header a type name needs, which names a spelling mentions, and when
// `return {};` is a valid way to produce a value.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "mirrortest/ir/classify.h"
#include "mirrortest/ir/type.h"

using namespace mt;

namespace {

using Names = std::vector<std::string>;

// ---- system_include_for -------------------------------------------------------------------------------------

struct IncludeCase {
    const char* name;
    const char* header;
};

TEST(IrClassifyInclude, TheStandardLibraryByItsHeader) {
    const IncludeCase cases[] = {
        {"std::string", "string"},
        {"std::wstring", "string"},
        {"std::u16string", "string"},
        {"std::string_view", "string_view"},
        {"std::vector", "vector"},
        {"std::map", "map"},
        {"std::multimap", "map"},
        {"std::set", "set"},
        {"std::multiset", "set"},
        {"std::unordered_map", "unordered_map"},
        {"std::unordered_set", "unordered_set"},
        {"std::array", "array"},
        {"std::list", "list"},
        {"std::deque", "deque"},
        {"std::queue", "queue"},
        {"std::stack", "stack"},
        {"std::bitset", "bitset"},
        {"std::optional", "optional"},
        {"std::variant", "variant"},
        {"std::tuple", "tuple"},
        {"std::pair", "utility"},
        {"std::function", "functional"},
        {"std::unique_ptr", "memory"},
        {"std::shared_ptr", "memory"},
        {"std::weak_ptr", "memory"},
        {"std::atomic", "atomic"},
        {"std::mutex", "mutex"},
        {"std::thread", "thread"},
        {"std::ostream", "iosfwd"},
        {"std::runtime_error", "stdexcept"},
        {"std::exception", "exception"},
        {"std::initializer_list", "initializer_list"},
        {"std::chrono::seconds", "chrono"},
        {"std::chrono::steady_clock::time_point", "chrono"},
        {"std::byte", "cstddef"},
        {"std::nullptr_t", "cstddef"},
    };
    for (const IncludeCase& c : cases) EXPECT_EQ(ir::system_include_for(c.name), c.header) << c.name;
}

TEST(IrClassifyInclude, CTypesWithOrWithoutStd) {
    for (const char* name : {"size_t", "std::size_t", "::size_t", "ptrdiff_t", "std::ptrdiff_t"}) {
        EXPECT_EQ(ir::system_include_for(name), "cstddef") << name;
    }
    for (const char* name : {"uint8_t", "int8_t", "uint16_t", "int32_t", "uint64_t", "std::uint32_t", "intptr_t", "uintptr_t",
                             "intmax_t", "uintmax_t", "int_fast16_t", "uint_least8_t", "int_least64_t"}) {
        EXPECT_EQ(ir::system_include_for(name), "cstdint") << name;
    }
    EXPECT_EQ(ir::system_include_for("FILE"), "cstdio");
    EXPECT_EQ(ir::system_include_for("std::FILE"), "cstdio");
}

TEST(IrClassifyInclude, SpellingsThatLookLikeFixedWidthTypesButAreNot) {
    for (const char* name : {"int", "int_t", "uint_t", "int7_t", "int128_t", "int_fast7_t", "uint8", "my_uint8_t_", "xint8_t", "int_fast_t"}) {
        EXPECT_EQ(ir::system_include_for(name), "") << name;
    }
}

TEST(IrClassifyInclude, QtClassesAreTheirOwnHeaderAndItsTypedefsLiveInQtGlobal) {
    EXPECT_EQ(ir::system_include_for("QString"), "QString");
    EXPECT_EQ(ir::system_include_for("QObject"), "QObject");
    EXPECT_EQ(ir::system_include_for("QVariantMap"), "QVariantMap");
    for (const char* name : {"qint64", "quint8", "qreal", "qsizetype", "qulonglong"}) {
        EXPECT_EQ(ir::system_include_for(name), "QtGlobal") << name;
    }
}

TEST(IrClassifyInclude, NamesThatNeedNothingOrAreNotKnown) {
    // Without the prefix the standard library is somebody else's name: a test
    // that says `using namespace std` has it put back before the backend sees it.
    for (const char* name : {"", "Foo", "acme::Foo", "string", "vector", "std::move", "std::unknown", "std::", "Q", "Qt", "QX::Y",
                             "qstring", "int", "unsigned int", "testing::Test"}) {
        EXPECT_EQ(ir::system_include_for(name), "") << name;
    }
}

TEST(IrClassifyInclude, TheTableIsSortedOrLookupsWouldMissEntries) {
    // find_entry binary-searches; one entry out of order hides its neighbours.
    // Probing the first, last and a middle name catches the common slip.
    EXPECT_EQ(ir::system_include_for("std::array"), "array");
    EXPECT_EQ(ir::system_include_for("std::wstring"), "string");
    EXPECT_EQ(ir::system_include_for("std::map"), "map");
    EXPECT_EQ(ir::system_include_for("std::queue"), "queue");
}

// ---- mentioned_names ---------------------------------------------------------------------------------------------

TEST(IrClassifyMentioned, QualifiedIdentifiersInOrderWithoutRepeats) {
    EXPECT_EQ(ir::mentioned_names("std::map<std::string, Foo::Item>"), (Names{"std::map", "std::string", "Foo::Item"}));
    EXPECT_EQ(ir::mentioned_names("std::pair<Item, Item>"), (Names{"std::pair", "Item"}));
    EXPECT_EQ(ir::mentioned_names("a::b::C<d::E>&"), (Names{"a::b::C", "d::E"}));
}

TEST(IrClassifyMentioned, KeywordsBuiltinTypesAndNumbersAreNotNames) {
    EXPECT_EQ(ir::mentioned_names("const unsigned int*"), Names{});
    EXPECT_EQ(ir::mentioned_names("unsigned long long"), Names{});
    EXPECT_EQ(ir::mentioned_names("wchar_t"), Names{});
    EXPECT_EQ(ir::mentioned_names("std::array<Item, 3u>"), (Names{"std::array", "Item"}));
    EXPECT_EQ(ir::mentioned_names("std::array<int, 0x1F>"), (Names{"std::array"}));
    EXPECT_EQ(ir::mentioned_names("Foo<1'000, 1.5f>"), (Names{"Foo"}));
    EXPECT_EQ(ir::mentioned_names("Foo<1.f, 2.e3, 0x1'Fu>"), (Names{"Foo"}));
    EXPECT_EQ(ir::mentioned_names("struct Foo"), (Names{"Foo"}));
    EXPECT_EQ(ir::mentioned_names("typename T::type"), (Names{"T::type"}));
}

TEST(IrClassifyMentioned, ALeadingGlobalScopeIsDropped) {
    EXPECT_EQ(ir::mentioned_names("::acme::Foo"), (Names{"acme::Foo"}));
    EXPECT_EQ(ir::mentioned_names("std::vector<::Foo>"), (Names{"std::vector", "Foo"}));
}

TEST(IrClassifyMentioned, FunctionPointersAndArraysAndOddInput) {
    EXPECT_EQ(ir::mentioned_names("void (*)(Foo, int)"), (Names{"Foo"}));
    EXPECT_EQ(ir::mentioned_names("Foo[3]"), (Names{"Foo"}));
    EXPECT_EQ(ir::mentioned_names(""), Names{});
    EXPECT_EQ(ir::mentioned_names("&*<>,"), Names{});
    EXPECT_EQ(ir::mentioned_names("Foo::"), (Names{"Foo"}));
    EXPECT_EQ(ir::mentioned_names("::"), Names{});
    EXPECT_EQ(ir::mentioned_names("a::1"), (Names{"a"}));
}

TEST(IrClassifyMentioned, EveryPrefixOfASpellingIsHandled) {
    const std::string text = "const std::map<std::string, ::acme::Foo::Item*>& (*)(int, unsigned long, Foo<1'0>)";
    for (std::size_t n = 0; n <= text.size(); ++n) {
        const Names names = ir::mentioned_names(text.substr(0, n));
        for (const std::string& name : names) {
            ASSERT_FALSE(name.empty()) << "cut at " << n;
            EXPECT_NE(name.back(), ':') << "cut at " << n << ": " << name;
        }
    }
}

// ---- is_default_returnable ---------------------------------------------------------------------------------

bool returnable(const std::string& spelling, const std::vector<const ir::Model*>& models = {},
                const std::vector<std::string>& user = {}) {
    return ir::is_default_returnable(ir::type_from_spelling(spelling), models, user);
}

TEST(IrClassifyReturnable, FundamentalTypesAndTheirTypedefs) {
    for (const char* t : {"int", "unsigned int", "unsigned long long", "char", "bool", "double", "float", "short", "wchar_t", "size_t",
                          "std::size_t", "uint8_t", "std::int64_t", "ptrdiff_t", "qint64", "qreal", "std::byte", "std::nullptr_t"}) {
        EXPECT_TRUE(returnable(t)) << t;
    }
}

TEST(IrClassifyReturnable, PointersWhateverTheyPointAt) {
    EXPECT_TRUE(returnable("const char*"));
    EXPECT_TRUE(returnable("Foo*"));
    EXPECT_TRUE(returnable("void*"));
    EXPECT_TRUE(returnable("Foo**"));
}

TEST(IrClassifyReturnable, ReferencesAndVoidNever) {
    EXPECT_FALSE(returnable("int&"));
    EXPECT_FALSE(returnable("const std::string&"));
    EXPECT_FALSE(returnable("Foo&&"));
    EXPECT_FALSE(returnable("void"));
    EXPECT_FALSE(returnable(""));
}

TEST(IrClassifyReturnable, StandardValueTypes) {
    for (const char* t : {"std::string", "std::wstring", "std::string_view", "std::vector<int>", "std::vector<NoDefault>",
                          "std::map<int, std::string>", "std::unordered_map<int, int>", "std::set<int>", "std::list<int>",
                          "std::deque<int>", "std::optional<NoDefault>", "std::unique_ptr<NoDefault>", "std::shared_ptr<int>",
                          "std::function<void(int)>", "std::chrono::seconds", "std::chrono::steady_clock::time_point",
                          "std::atomic<int>", "std::bitset<8>"}) {
        EXPECT_TRUE(returnable(t)) << t;
    }
}

TEST(IrClassifyReturnable, PairTupleAndArrayAreOnlyAsGoodAsWhatTheyHold) {
    EXPECT_TRUE(returnable("std::pair<int, std::string>"));
    EXPECT_FALSE(returnable("std::pair<int, NoDefault>"));
    EXPECT_TRUE(returnable("std::tuple<int, double, std::string>"));
    EXPECT_FALSE(returnable("std::tuple<int, NoDefault>"));
    EXPECT_TRUE(returnable("std::array<int, 3>"));
    EXPECT_FALSE(returnable("std::array<NoDefault, 3>"));
    EXPECT_TRUE(returnable("std::pair<std::pair<int, int>, std::vector<NoDefault>>"));
    EXPECT_FALSE(returnable("std::pair<std::pair<int, NoDefault>, int>"));
}

TEST(IrClassifyReturnable, QtClasses) {
    EXPECT_TRUE(returnable("QString"));
    EXPECT_TRUE(returnable("QList<int>"));
    EXPECT_TRUE(returnable("QVariant"));
}

TEST(IrClassifyReturnable, ATypeNobodyKnowsIsNotReturnable) {
    EXPECT_FALSE(returnable("Mystery"));
    EXPECT_FALSE(returnable("acme::Mystery"));
    EXPECT_FALSE(returnable("std::mystery"));
    EXPECT_FALSE(returnable("void (*)(int)[2]"));
}

TEST(IrClassifyReturnable, TheConfigurationVouchesForItsTypes) {
    EXPECT_FALSE(returnable("Mystery"));
    EXPECT_TRUE(returnable("Mystery", {}, {"Mystery"}));
    EXPECT_TRUE(returnable("acme::Mystery", {}, {"acme::Mystery"}));
    EXPECT_TRUE(returnable("::acme::Mystery", {}, {"acme::Mystery"}));
    EXPECT_FALSE(returnable("Other", {}, {"Mystery"}));
}

ir::Method ctor(std::size_t arity, bool is_explicit = false, bool with_defaults = false) {
    ir::Method m;
    m.name = "Foo";
    m.kind = ir::MethodKind::Constructor;
    m.is_explicit = is_explicit;
    for (std::size_t i = 0; i < arity; ++i) {
        ir::Param p;
        p.type = ir::make_type("int");
        if (with_defaults) p.default_value = "0";
        m.params.push_back(p);
    }
    return m;
}

ir::Model model_with(std::vector<ir::Method> methods, std::vector<std::string> ns = {}) {
    ir::Model model;
    ir::Class c;
    c.name = "Foo";
    c.ns = std::move(ns);
    c.methods = std::move(methods);
    model.classes.push_back(c);
    return model;
}

TEST(IrClassifyReturnable, AGeneratedClassNeedsABraceConstructor) {
    const ir::Model none = model_with({});
    const ir::Model plain = model_with({ctor(0)});
    const ir::Model only_arg = model_with({ctor(1)});
    const ir::Model explicit_default = model_with({ctor(0, true)});
    const ir::Model defaults = model_with({ctor(2, false, true)});
    const ir::Model explicit_defaults = model_with({ctor(2, true, true)});
    const ir::Model both = model_with({ctor(1), ctor(0)});

    EXPECT_TRUE(returnable("Foo", {&none}));       // no constructors: the implicit one
    EXPECT_TRUE(returnable("Foo", {&plain}));
    EXPECT_FALSE(returnable("Foo", {&only_arg}));  // declaring one removes the implicit default
    EXPECT_FALSE(returnable("Foo", {&explicit_default}));   // `return {};` is copy-list-initialisation
    EXPECT_TRUE(returnable("Foo", {&defaults}));
    EXPECT_FALSE(returnable("Foo", {&explicit_defaults}));
    EXPECT_TRUE(returnable("Foo", {&both}));
}

TEST(IrClassifyReturnable, ADefaultConstructorThatIsNotPublicIsNoUse) {
    ir::Method hidden = ctor(0);
    hidden.access = ir::Access::Private;
    const ir::Model model = model_with({hidden});
    EXPECT_FALSE(returnable("Foo", {&model}));
    EXPECT_FALSE(ir::is_default_constructible(ir::type_from_spelling("Foo"), {&model}, {}));
    ir::Method protected_ctor = ctor(0);
    protected_ctor.access = ir::Access::Protected;
    const ir::Model other = model_with({protected_ctor, ctor(0)});
    EXPECT_TRUE(returnable("Foo", {&other})) << "a public one beside it";
}

TEST(IrClassifyReturnable, DeletedAndAbstractClassesAreNot) {
    ir::Method deleted = ctor(0);
    deleted.is_deleted = true;
    const ir::Model with_deleted = model_with({deleted});
    EXPECT_FALSE(returnable("Foo", {&with_deleted}));

    ir::Method pure;
    pure.name = "run";
    pure.is_pure = true;
    const ir::Model abstract = model_with({pure});
    EXPECT_FALSE(returnable("Foo", {&abstract}));
}

TEST(IrClassifyReturnable, ClassesAreFoundByQualifiedOrSuffixName) {
    const ir::Model model = model_with({}, {"acme"});
    EXPECT_TRUE(returnable("acme::Foo", {&model}));
    EXPECT_TRUE(returnable("Foo", {&model}));       // written inside namespace acme
    EXPECT_TRUE(returnable("::acme::Foo", {&model}));
    EXPECT_FALSE(returnable("other::Foo", {&model}));
    EXPECT_FALSE(returnable("cme::Foo", {&model}));  // a suffix must start at a "::"
}

TEST(IrClassifyReturnable, EnumsAreValueInitialisable) {
    ir::Model model;
    ir::Enum e;
    e.name = "Mode";
    e.ns = {"acme"};
    model.enums.push_back(e);
    EXPECT_TRUE(returnable("acme::Mode", {&model}));
    EXPECT_TRUE(returnable("Mode", {&model}));
    EXPECT_FALSE(returnable("Mode"));
}

TEST(IrClassifyReturnable, TemplateInstancesOfAKnownClassFollowTheClass) {
    const ir::Model model = model_with({});
    EXPECT_TRUE(returnable("Foo<int>", {&model}));
}

// ---- is_default_constructible -------------------------------------------------------------------------------------

bool constructible(const std::string& spelling, const std::vector<const ir::Model*>& models = {},
                   const std::vector<std::string>& user = {}) {
    return ir::is_default_constructible(ir::type_from_spelling(spelling), models, user);
}

TEST(IrClassifyConstructible, ReferencesAndConstAreIgnoredBecauseTheCallerDropsThem) {
    EXPECT_TRUE(constructible("int&"));
    EXPECT_TRUE(constructible("const std::string&"));
    EXPECT_TRUE(constructible("const double"));
    EXPECT_TRUE(constructible("std::vector<int>&&"));
    EXPECT_FALSE(constructible("Mystery&"));
    EXPECT_FALSE(constructible("void"));
    EXPECT_FALSE(constructible(""));
}

TEST(IrClassifyConstructible, AnExplicitDefaultConstructorIsFineForDirectListInitialisation) {
    const ir::Model explicit_default = model_with({ctor(0, true)});
    EXPECT_FALSE(returnable("Foo", {&explicit_default}));
    EXPECT_TRUE(constructible("Foo", {&explicit_default}));
    EXPECT_TRUE(constructible("const Foo&", {&explicit_default}));
}

TEST(IrClassifyConstructible, ConstructorsThatNeedArgumentsOrAreDeletedOrAbstractAreNot) {
    ir::Method deleted = ctor(0);
    deleted.is_deleted = true;
    ir::Method pure;
    pure.name = "run";
    pure.is_pure = true;
    const ir::Model needs_args = model_with({ctor(1)});
    const ir::Model deleted_default = model_with({deleted});
    const ir::Model abstract = model_with({pure});
    EXPECT_FALSE(constructible("Foo", {&needs_args}));
    EXPECT_FALSE(constructible("Foo", {&deleted_default}));
    EXPECT_FALSE(constructible("Foo", {&abstract}));
}

TEST(IrClassifyConstructible, PointersAndTheConfigurationsTypes) {
    EXPECT_TRUE(constructible("Mystery*"));
    EXPECT_TRUE(constructible("const char*"));
    EXPECT_FALSE(constructible("Mystery"));
    EXPECT_TRUE(constructible("Mystery", {}, {"Mystery"}));
}

TEST(IrClassifyConstructible, APublicNestedClassIsFoundThroughItsOuter) {
    ir::Model model = model_with({});
    ir::Class item;
    item.name = "Item";
    item.ns = {"acme"};
    item.nested_access = ir::Access::Public;
    ir::Class secret = item;
    secret.name = "Secret";
    secret.nested_access = ir::Access::Private;
    ir::Class needs;
    needs.name = "Needs";
    needs.ns = {"acme"};
    needs.methods = {ctor(1)};
    ir::Class deep;
    deep.name = "Deep";
    deep.ns = {"acme"};
    item.nested_classes = {deep};
    model.classes[0].ns = {"acme"};
    model.classes[0].nested_classes = {item, secret, needs};

    EXPECT_TRUE(constructible("Foo::Item", {&model}));
    EXPECT_TRUE(constructible("acme::Foo::Item", {&model}));
    EXPECT_TRUE(constructible("const Foo::Item&", {&model}));
    EXPECT_TRUE(constructible("Foo::Item::Deep", {&model}));
    EXPECT_FALSE(constructible("Foo::Secret", {&model})) << "private";
    EXPECT_FALSE(constructible("Foo::Needs", {&model})) << "needs an argument";
    EXPECT_TRUE(constructible("Item", {&model})) << "a name matches by its suffix, as inside the outer class's own signatures";
    EXPECT_FALSE(constructible("Other::Item", {&model}));
}

TEST(IrClassifyConstructible, APublicNestedEnumIsFoundThroughItsOuter) {
    ir::Model model = model_with({});
    model.classes[0].ns = {"acme"};
    model.classes[0].nested_enums = {"Mode"};
    ir::Class item;
    item.name = "Item";
    item.ns = {"acme"};
    item.nested_access = ir::Access::Public;
    item.nested_enums = {"Kind"};
    ir::Class hidden = item;
    hidden.name = "Hidden";
    hidden.nested_access = ir::Access::Private;
    model.classes[0].nested_classes = {item, hidden};

    EXPECT_TRUE(constructible("Foo::Mode", {&model}));
    EXPECT_TRUE(constructible("acme::Foo::Mode", {&model}));
    EXPECT_TRUE(constructible("const Foo::Mode&", {&model}));
    EXPECT_TRUE(constructible("Foo::Item::Kind", {&model}));
    EXPECT_FALSE(constructible("Foo::Hidden::Kind", {&model})) << "its class is private";
    EXPECT_FALSE(constructible("Foo::Other", {&model})) << "not an enum of the class";
    EXPECT_TRUE(constructible("Kind", {&model})) << "a name matches by its suffix, as for the nested classes";
    EXPECT_FALSE(constructible("Other::Kind", {&model}));
    EXPECT_FALSE(constructible("Foo::Mode", {})) << "without the model it is nobody";
}

TEST(IrClassifyReturnable, ANullModelIsSkipped) {
    const ir::Model model = model_with({});
    EXPECT_TRUE(returnable("Foo", {nullptr, &model}));
}

}  // namespace
