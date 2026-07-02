//
// Unit tests for TS-4 type identity: sameType(), qualifiedName(), displayType().
//
// Nominal types compare by canonical pointer identity (two distinct declarations
// of "Foo" are NOT the same type); compound types recurse structurally.

#include "test_harness.h"
#include "Type.h"

using namespace angara;

// ── sameType ──

TEST(sametype_null_both_equal) {
    std::shared_ptr<Type> a, b;
    ASSERT_TRUE(sameType(a, b));
}

TEST(sametype_null_one_unequal) {
    auto p = std::make_shared<PrimitiveType>("i64");
    ASSERT_FALSE(sameType(p, nullptr));
    ASSERT_FALSE(sameType(nullptr, p));
}

TEST(sametype_primitive_by_name) {
    auto i1 = std::make_shared<PrimitiveType>("i64");
    auto i2 = std::make_shared<PrimitiveType>("i64");
    auto u = std::make_shared<PrimitiveType>("u8");
    ASSERT_TRUE(sameType(i1, i2));   // same name, distinct objects
    ASSERT_FALSE(sameType(i1, u));
}

TEST(sametype_distinct_nominal_decls_unequal) {
    // Two distinct data declarations of the same name — the core TS-4 fix.
    auto d1 = std::make_shared<DataType>("Foo");
    auto d2 = std::make_shared<DataType>("Foo");
    ASSERT_FALSE(sameType(d1, d2));  // distinct objects, same name => NOT equal
}

TEST(sametype_shared_nominal_equal) {
    auto d = std::make_shared<DataType>("Foo");
    ASSERT_TRUE(sameType(d, d));     // same shared_ptr => equal
}

TEST(sametype_list_recurses) {
    auto li = std::make_shared<ListType>(std::make_shared<PrimitiveType>("i64"));
    auto li2 = std::make_shared<ListType>(std::make_shared<PrimitiveType>("i64"));
    auto lu = std::make_shared<ListType>(std::make_shared<PrimitiveType>("u8"));
    ASSERT_TRUE(sameType(li, li2));   // list<i64> == list<i64> (distinct objects)
    ASSERT_FALSE(sameType(li, lu));   // list<i64> != list<u8>
}

TEST(sametype_instance_by_inner_class) {
    // InstanceType is minted per-use; identity follows the inner ClassType.
    auto cls = std::make_shared<ClassType>("C");
    auto i1 = std::make_shared<InstanceType>(cls);
    auto i2 = std::make_shared<InstanceType>(cls);
    auto other = std::make_shared<ClassType>("C");
    auto i3 = std::make_shared<InstanceType>(other);
    ASSERT_TRUE(sameType(i1, i2));     // same inner class
    ASSERT_FALSE(sameType(i1, i3));    // distinct inner class (same name)
}

TEST(sametype_optional_recurses) {
    auto a = std::make_shared<OptionalType>(std::make_shared<PrimitiveType>("string"));
    auto b = std::make_shared<OptionalType>(std::make_shared<PrimitiveType>("string"));
    auto c = std::make_shared<OptionalType>(std::make_shared<PrimitiveType>("i64"));
    ASSERT_TRUE(sameType(a, b));
    ASSERT_FALSE(sameType(a, c));
}

TEST(sametype_different_kinds_unequal) {
    auto p = std::make_shared<PrimitiveType>("i64");
    auto l = std::make_shared<ListType>(std::make_shared<PrimitiveType>("i64"));
    ASSERT_FALSE(sameType(p, l));
}

// ── qualifiedName / displayType ──

TEST(qualifiedname_empty_home_is_bare) {
    DataType d("Foo");
    ASSERT_EQ(qualifiedName(d), "Foo");
}

TEST(qualifiedname_with_home_is_qualified) {
    DataType d("Foo");
    d.home_module = "lib";
    ASSERT_EQ(qualifiedName(d), "lib::Foo");
}

TEST(displaytype_bare_when_no_collision) {
    DataType a("Foo"); a.home_module = "app";
    PrimitiveType s("string");
    // Different bare names => bare display
    ASSERT_EQ(displayType(a, s), "Foo");
    ASSERT_EQ(displayType(s, a), "string");
}

TEST(displaytype_qualified_on_collision) {
    DataType a("Foo"); a.home_module = "app";
    DataType b("Foo"); b.home_module = "lib";
    // Same bare name, different home => qualified to disambiguate
    ASSERT_EQ(displayType(a, b), "app::Foo");
    ASSERT_EQ(displayType(b, a), "lib::Foo");
}
