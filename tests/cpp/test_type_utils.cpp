//
// Unit tests for Type utility functions: integer/float classification,
// integer conversion classification, FunctionType::equals edge cases,
// sameType for compound types not covered by test_sametype.cpp.
//

#include "test_harness.h"
#include "Type.h"

using namespace angara;

// ── isInteger ──

TEST(isinteger_i8_true) {
    ASSERT_TRUE(isInteger(std::make_shared<PrimitiveType>("i8")));
}

TEST(isinteger_u64_true) {
    ASSERT_TRUE(isInteger(std::make_shared<PrimitiveType>("u64")));
}

TEST(isinteger_char_true) {
    // LANG-4: char is a 32-bit unsigned integer subtype
    ASSERT_TRUE(isInteger(std::make_shared<PrimitiveType>("char")));
}

TEST(isinteger_f64_false) {
    ASSERT_FALSE(isInteger(std::make_shared<PrimitiveType>("f64")));
}

TEST(isinteger_string_false) {
    ASSERT_FALSE(isInteger(std::make_shared<PrimitiveType>("string")));
}

TEST(isinteger_null_false) {
    ASSERT_FALSE(isInteger(nullptr));
}

TEST(isinteger_non_primitive_false) {
    ASSERT_FALSE(isInteger(std::make_shared<ListType>(std::make_shared<PrimitiveType>("i64"))));
}

// ── isFloat ──

TEST(isfloat_f32_true) {
    ASSERT_TRUE(isFloat(std::make_shared<PrimitiveType>("f32")));
}

TEST(isfloat_f64_true) {
    ASSERT_TRUE(isFloat(std::make_shared<PrimitiveType>("f64")));
}

TEST(isfloat_i64_false) {
    ASSERT_FALSE(isFloat(std::make_shared<PrimitiveType>("i64")));
}

TEST(isfloat_null_false) {
    ASSERT_FALSE(isFloat(nullptr));
}

// ── isUnsignedInteger ──

TEST(isunsigned_u8_true) {
    ASSERT_TRUE(isUnsignedInteger(std::make_shared<PrimitiveType>("u8")));
}

TEST(isunsigned_u16_true) {
    ASSERT_TRUE(isUnsignedInteger(std::make_shared<PrimitiveType>("u16")));
}

TEST(isunsigned_u32_true) {
    ASSERT_TRUE(isUnsignedInteger(std::make_shared<PrimitiveType>("u32")));
}

TEST(isunsigned_u64_true) {
    ASSERT_TRUE(isUnsignedInteger(std::make_shared<PrimitiveType>("u64")));
}

TEST(isunsigned_char_true) {
    ASSERT_TRUE(isUnsignedInteger(std::make_shared<PrimitiveType>("char")));
}

TEST(isunsigned_i64_false) {
    ASSERT_FALSE(isUnsignedInteger(std::make_shared<PrimitiveType>("i64")));
}

// ── isChar ──

TEST(ischar_char_true) {
    ASSERT_TRUE(isChar(std::make_shared<PrimitiveType>("char")));
}

TEST(ischar_i32_false) {
    ASSERT_FALSE(isChar(std::make_shared<PrimitiveType>("i32")));
}

TEST(ischar_null_false) {
    ASSERT_FALSE(isChar(nullptr));
}

// ── isNumeric ──

TEST(isnumeric_integers_true) {
    ASSERT_TRUE(isNumeric(std::make_shared<PrimitiveType>("i64")));
    ASSERT_TRUE(isNumeric(std::make_shared<PrimitiveType>("u8")));
    ASSERT_TRUE(isNumeric(std::make_shared<PrimitiveType>("char")));
}

TEST(isnumeric_floats_true) {
    ASSERT_TRUE(isNumeric(std::make_shared<PrimitiveType>("f32")));
    ASSERT_TRUE(isNumeric(std::make_shared<PrimitiveType>("f64")));
}

TEST(isnumeric_string_false) {
    ASSERT_FALSE(isNumeric(std::make_shared<PrimitiveType>("string")));
}

TEST(isnumeric_bool_false) {
    ASSERT_FALSE(isNumeric(std::make_shared<PrimitiveType>("bool")));
}

// ── intWidth ──

TEST(intwidth_i8_is_8) {
    ASSERT_EQ(intWidth(std::make_shared<PrimitiveType>("i8")), 8);
}

TEST(intwidth_u16_is_16) {
    ASSERT_EQ(intWidth(std::make_shared<PrimitiveType>("u16")), 16);
}

TEST(intwidth_i32_is_32) {
    ASSERT_EQ(intWidth(std::make_shared<PrimitiveType>("i32")), 32);
}

TEST(intwidth_char_is_32) {
    ASSERT_EQ(intWidth(std::make_shared<PrimitiveType>("char")), 32);
}

TEST(intwidth_i64_is_64) {
    ASSERT_EQ(intWidth(std::make_shared<PrimitiveType>("i64")), 64);
}

TEST(intwidth_non_integer_is_0) {
    ASSERT_EQ(intWidth(std::make_shared<PrimitiveType>("f64")), 0);
    ASSERT_EQ(intWidth(std::make_shared<PrimitiveType>("string")), 0);
}

// ── classifyIntConv ──

TEST(intconv_identical_same_type) {
    ASSERT_TRUE(classifyIntConv(
        std::make_shared<PrimitiveType>("i64"),
        std::make_shared<PrimitiveType>("i64")) == IntConv::Identical);
    ASSERT_TRUE(classifyIntConv(
        std::make_shared<PrimitiveType>("u32"),
        std::make_shared<PrimitiveType>("u32")) == IntConv::Identical);
}

TEST(intconv_widen_smaller_to_larger) {
    // u8 -> i64: target wider, signed target can represent all u8 values
    ASSERT_TRUE(classifyIntConv(
        std::make_shared<PrimitiveType>("i64"),
        std::make_shared<PrimitiveType>("u8")) == IntConv::Widen);
    // i32 -> i64
    ASSERT_TRUE(classifyIntConv(
        std::make_shared<PrimitiveType>("i64"),
        std::make_shared<PrimitiveType>("i32")) == IntConv::Widen);
}

TEST(intconv_narrow_larger_to_smaller) {
    // i64 -> u8: target narrower
    ASSERT_TRUE(classifyIntConv(
        std::make_shared<PrimitiveType>("u8"),
        std::make_shared<PrimitiveType>("i64")) == IntConv::Narrow);
    // u64 -> i8
    ASSERT_TRUE(classifyIntConv(
        std::make_shared<PrimitiveType>("i8"),
        std::make_shared<PrimitiveType>("u64")) == IntConv::Narrow);
}

TEST(intconv_signed_unsigned_same_width_narrows) {
    // u32 -> i32: same width, but unsigned to signed narrows
    ASSERT_TRUE(classifyIntConv(
        std::make_shared<PrimitiveType>("i32"),
        std::make_shared<PrimitiveType>("u32")) == IntConv::Narrow);
    // i32 -> u32: signed to unsigned also narrows
    ASSERT_TRUE(classifyIntConv(
        std::make_shared<PrimitiveType>("u32"),
        std::make_shared<PrimitiveType>("i32")) == IntConv::Narrow);
}

TEST(intconv_null_narrow) {
    ASSERT_TRUE(classifyIntConv(nullptr, std::make_shared<PrimitiveType>("i64")) == IntConv::Narrow);
    ASSERT_TRUE(classifyIntConv(std::make_shared<PrimitiveType>("i64"), nullptr) == IntConv::Narrow);
}

// ── FunctionType::equals ──

TEST(fntype_equals_same_params_and_return) {
    auto f1 = std::make_shared<FunctionType>(
        std::vector<std::shared_ptr<Type>>{std::make_shared<PrimitiveType>("i64")},
        std::make_shared<PrimitiveType>("string"));
    auto f2 = std::make_shared<FunctionType>(
        std::vector<std::shared_ptr<Type>>{std::make_shared<PrimitiveType>("i64")},
        std::make_shared<PrimitiveType>("string"));
    ASSERT_TRUE(f1->equals(*f2));
}

TEST(fntype_equals_different_return) {
    auto f1 = std::make_shared<FunctionType>(
        std::vector<std::shared_ptr<Type>>{std::make_shared<PrimitiveType>("i64")},
        std::make_shared<PrimitiveType>("string"));
    auto f2 = std::make_shared<FunctionType>(
        std::vector<std::shared_ptr<Type>>{std::make_shared<PrimitiveType>("i64")},
        std::make_shared<PrimitiveType>("bool"));
    ASSERT_FALSE(f1->equals(*f2));
}

TEST(fntype_equals_different_arity) {
    auto f1 = std::make_shared<FunctionType>(
        std::vector<std::shared_ptr<Type>>{std::make_shared<PrimitiveType>("i64")},
        std::make_shared<PrimitiveType>("void"));
    auto f2 = std::make_shared<FunctionType>(
        std::vector<std::shared_ptr<Type>>{
            std::make_shared<PrimitiveType>("i64"),
            std::make_shared<PrimitiveType>("string")},
        std::make_shared<PrimitiveType>("void"));
    ASSERT_FALSE(f1->equals(*f2));
}

TEST(fntype_equals_different_variadic) {
    auto f1 = std::make_shared<FunctionType>(
        std::vector<std::shared_ptr<Type>>{std::make_shared<PrimitiveType>("i64")},
        std::make_shared<PrimitiveType>("void"), true);
    auto f2 = std::make_shared<FunctionType>(
        std::vector<std::shared_ptr<Type>>{std::make_shared<PrimitiveType>("i64")},
        std::make_shared<PrimitiveType>("void"), false);
    ASSERT_FALSE(f1->equals(*f2));
}

TEST(fntype_equals_different_foreign) {
    auto f1 = std::make_shared<FunctionType>(
        std::vector<std::shared_ptr<Type>>{},
        std::make_shared<PrimitiveType>("void"));
    f1->is_foreign = true;
    auto f2 = std::make_shared<FunctionType>(
        std::vector<std::shared_ptr<Type>>{},
        std::make_shared<PrimitiveType>("void"));
    f2->is_foreign = false;
    ASSERT_FALSE(f1->equals(*f2));
}

TEST(fntype_equals_different_intrinsic) {
    auto f1 = std::make_shared<FunctionType>(
        std::vector<std::shared_ptr<Type>>{},
        std::make_shared<PrimitiveType>("i64"));
    f1->is_intrinsic = true;
    auto f2 = std::make_shared<FunctionType>(
        std::vector<std::shared_ptr<Type>>{},
        std::make_shared<PrimitiveType>("i64"));
    f2->is_intrinsic = false;
    ASSERT_FALSE(f1->equals(*f2));
}

TEST(fntype_equals_different_on_throw) {
    auto f1 = std::make_shared<FunctionType>(
        std::vector<std::shared_ptr<Type>>{},
        std::make_shared<PrimitiveType>("i64"));
    f1->on_throw_value = -1;
    auto f2 = std::make_shared<FunctionType>(
        std::vector<std::shared_ptr<Type>>{},
        std::make_shared<PrimitiveType>("i64"));
    // no on_throw_value set
    ASSERT_FALSE(f1->equals(*f2));
}

TEST(fntype_equals_empty_params) {
    auto f1 = std::make_shared<FunctionType>(
        std::vector<std::shared_ptr<Type>>{},
        std::make_shared<PrimitiveType>("void"));
    auto f2 = std::make_shared<FunctionType>(
        std::vector<std::shared_ptr<Type>>{},
        std::make_shared<PrimitiveType>("void"));
    ASSERT_TRUE(f1->equals(*f2));
}

// ── sameType for compound types not in test_sametype ──

TEST(sametype_pointer_same) {
    auto p1 = std::make_shared<PointerType>(std::make_shared<PrimitiveType>("i8"), 1);
    auto p2 = std::make_shared<PointerType>(std::make_shared<PrimitiveType>("i8"), 1);
    ASSERT_TRUE(sameType(p1, p2));
}

TEST(sametype_pointer_different_depth) {
    auto p1 = std::make_shared<PointerType>(std::make_shared<PrimitiveType>("i8"), 1);
    auto p2 = std::make_shared<PointerType>(std::make_shared<PrimitiveType>("i8"), 2);
    ASSERT_FALSE(sameType(p1, p2));
}

TEST(sametype_pointer_different_byval) {
    auto p1 = std::make_shared<PointerType>(std::make_shared<DataType>("S"), 1);
    p1->byval = true;
    auto p2 = std::make_shared<PointerType>(std::make_shared<DataType>("S"), 1);
    p2->byval = false;
    ASSERT_FALSE(sameType(p1, p2));
}

TEST(sametype_fixed_array_same) {
    auto a1 = std::make_shared<FixedArrayType>(std::make_shared<PrimitiveType>("u8"), 256);
    auto a2 = std::make_shared<FixedArrayType>(std::make_shared<PrimitiveType>("u8"), 256);
    ASSERT_TRUE(sameType(a1, a2));
}

TEST(sametype_fixed_array_different_size) {
    auto a1 = std::make_shared<FixedArrayType>(std::make_shared<PrimitiveType>("u8"), 256);
    auto a2 = std::make_shared<FixedArrayType>(std::make_shared<PrimitiveType>("u8"), 128);
    ASSERT_FALSE(sameType(a1, a2));
}

TEST(sametype_raw_array_same) {
    auto a1 = std::make_shared<RawArrayType>(std::make_shared<PrimitiveType>("f64"));
    auto a2 = std::make_shared<RawArrayType>(std::make_shared<PrimitiveType>("f64"));
    ASSERT_TRUE(sameType(a1, a2));
}

TEST(sametype_raw_array_different_element) {
    auto a1 = std::make_shared<RawArrayType>(std::make_shared<PrimitiveType>("f64"));
    auto a2 = std::make_shared<RawArrayType>(std::make_shared<PrimitiveType>("f32"));
    ASSERT_FALSE(sameType(a1, a2));
}

TEST(sametype_vector_same) {
    auto v1 = std::make_shared<VectorType>(std::make_shared<PrimitiveType>("f32"), 4);
    auto v2 = std::make_shared<VectorType>(std::make_shared<PrimitiveType>("f32"), 4);
    ASSERT_TRUE(sameType(v1, v2));
}

TEST(sametype_vector_different_size) {
    auto v1 = std::make_shared<VectorType>(std::make_shared<PrimitiveType>("f32"), 4);
    auto v2 = std::make_shared<VectorType>(std::make_shared<PrimitiveType>("f32"), 3);
    ASSERT_FALSE(sameType(v1, v2));
}

TEST(sametype_ref_same) {
    auto r1 = std::make_shared<RefType>(std::make_shared<PrimitiveType>("string"));
    auto r2 = std::make_shared<RefType>(std::make_shared<PrimitiveType>("string"));
    ASSERT_TRUE(sameType(r1, r2));
}

TEST(sametype_future_same) {
    auto f1 = std::make_shared<FutureType>(std::make_shared<PrimitiveType>("i64"));
    auto f2 = std::make_shared<FutureType>(std::make_shared<PrimitiveType>("i64"));
    ASSERT_TRUE(sameType(f1, f2));
}

TEST(sametype_tuple_same) {
    auto t1 = std::make_shared<TupleType>(std::vector<std::shared_ptr<Type>>{
        std::make_shared<PrimitiveType>("i64"),
        std::make_shared<PrimitiveType>("string")});
    auto t2 = std::make_shared<TupleType>(std::vector<std::shared_ptr<Type>>{
        std::make_shared<PrimitiveType>("i64"),
        std::make_shared<PrimitiveType>("string")});
    ASSERT_TRUE(sameType(t1, t2));
}

TEST(sametype_tuple_different_length) {
    auto t1 = std::make_shared<TupleType>(std::vector<std::shared_ptr<Type>>{
        std::make_shared<PrimitiveType>("i64"),
        std::make_shared<PrimitiveType>("string")});
    auto t2 = std::make_shared<TupleType>(std::vector<std::shared_ptr<Type>>{
        std::make_shared<PrimitiveType>("i64")});
    ASSERT_FALSE(sameType(t1, t2));
}

TEST(sametype_tuple_different_element_order) {
    auto t1 = std::make_shared<TupleType>(std::vector<std::shared_ptr<Type>>{
        std::make_shared<PrimitiveType>("i64"),
        std::make_shared<PrimitiveType>("string")});
    auto t2 = std::make_shared<TupleType>(std::vector<std::shared_ptr<Type>>{
        std::make_shared<PrimitiveType>("string"),
        std::make_shared<PrimitiveType>("i64")});
    ASSERT_FALSE(sameType(t1, t2));
}

TEST(sametype_record_same) {
    auto r1 = std::make_shared<RecordType>(std::map<std::string, std::shared_ptr<Type>>{
        {"x", std::make_shared<PrimitiveType>("i64")},
        {"y", std::make_shared<PrimitiveType>("f64")}});
    auto r2 = std::make_shared<RecordType>(std::map<std::string, std::shared_ptr<Type>>{
        {"x", std::make_shared<PrimitiveType>("i64")},
        {"y", std::make_shared<PrimitiveType>("f64")}});
    ASSERT_TRUE(sameType(r1, r2));
}

TEST(sametype_record_different_field_types) {
    auto r1 = std::make_shared<RecordType>(std::map<std::string, std::shared_ptr<Type>>{
        {"x", std::make_shared<PrimitiveType>("i64")}});
    auto r2 = std::make_shared<RecordType>(std::map<std::string, std::shared_ptr<Type>>{
        {"x", std::make_shared<PrimitiveType>("string")}});
    ASSERT_FALSE(sameType(r1, r2));
}

TEST(sametype_trait_object_same_interface) {
    auto trait = std::make_shared<TraitType>("Drawable");
    auto to1 = std::make_shared<TraitObjectType>(trait, nullptr);
    auto to2 = std::make_shared<TraitObjectType>(trait, nullptr);
    ASSERT_TRUE(sameType(to1, to2));
}

TEST(sametype_trait_object_different_interface) {
    auto t1 = std::make_shared<TraitType>("Drawable");
    auto t2 = std::make_shared<TraitType>("Serializable");
    auto to1 = std::make_shared<TraitObjectType>(t1, nullptr);
    auto to2 = std::make_shared<TraitObjectType>(t2, nullptr);
    ASSERT_FALSE(sameType(to1, to2));
}

TEST(sametype_singletons_equal_by_name) {
    // NilType, AnyType, etc. are singletons — same name → equal
    ASSERT_TRUE(sameType(std::make_shared<NilType>(), std::make_shared<NilType>()));
    ASSERT_TRUE(sameType(std::make_shared<AnyType>(), std::make_shared<AnyType>()));
    ASSERT_TRUE(sameType(std::make_shared<ThreadType>(), std::make_shared<ThreadType>()));
    ASSERT_TRUE(sameType(std::make_shared<MutexType>(), std::make_shared<MutexType>()));
    ASSERT_TRUE(sameType(std::make_shared<VoidType>(), std::make_shared<VoidType>()));
    ASSERT_TRUE(sameType(std::make_shared<ExceptionType>(), std::make_shared<ExceptionType>()));
}

// ── qualifiedName / displayType ──

TEST(qualifiedname_primitive_is_bare) {
    ASSERT_EQ(qualifiedName(*std::make_shared<PrimitiveType>("i64")), "i64");
}

TEST(qualifiedname_class_without_home_is_bare) {
    auto c = std::make_shared<ClassType>("Foo");
    ASSERT_EQ(qualifiedName(*c), "Foo");
}

TEST(qualifiedname_enum_with_home_is_qualified) {
    auto e = std::make_shared<EnumType>("Color");
    e->home_module = "graphics";
    ASSERT_EQ(qualifiedName(*e), "graphics::Color");
}

TEST(qualifiedname_trait_with_home) {
    auto t = std::make_shared<TraitType>("Hashable");
    t->home_module = "collections";
    ASSERT_EQ(qualifiedName(*t), "collections::Hashable");
}

TEST(qualifiedname_contract_with_home) {
    auto c = std::make_shared<ContractType>("Serializable");
    c->home_module = "io";
    // ContractType::toString returns "contract<Name>"
    ASSERT_EQ(qualifiedName(*c), "io::contract<Serializable>");
}

TEST(displaytype_bare_when_different_name) {
    auto a = std::make_shared<DataType>("Foo");
    auto b = std::make_shared<DataType>("Bar");
    // Different names, no collision => bare
    ASSERT_EQ(displayType(*a, *b), "Foo");
    ASSERT_EQ(displayType(*b, *a), "Bar");
}

TEST(displaytype_qualified_when_same_name_different_home) {
    auto a = std::make_shared<DataType>("Foo");
    a->home_module = "app";
    auto b = std::make_shared<DataType>("Foo");
    b->home_module = "lib";
    ASSERT_EQ(displayType(*a, *b), "app::Foo");
    ASSERT_EQ(displayType(*b, *a), "lib::Foo");
}

// ── Type construction / toString ──

TEST(typestring_list) {
    auto l = std::make_shared<ListType>(std::make_shared<PrimitiveType>("string"));
    ASSERT_EQ(l->toString(), "list<string>");
}

TEST(typestring_optional) {
    auto o = std::make_shared<OptionalType>(std::make_shared<PrimitiveType>("i64"));
    ASSERT_EQ(o->toString(), "i64?");
}

TEST(typestring_ref) {
    auto r = std::make_shared<RefType>(std::make_shared<PrimitiveType>("string"));
    ASSERT_EQ(r->toString(), "ref<string>");
}

TEST(typestring_future) {
    auto f = std::make_shared<FutureType>(std::make_shared<PrimitiveType>("i64"));
    ASSERT_EQ(f->toString(), "Future<i64>");
}

TEST(typestring_tuple) {
    auto t = std::make_shared<TupleType>(std::vector<std::shared_ptr<Type>>{
        std::make_shared<PrimitiveType>("i64"),
        std::make_shared<PrimitiveType>("string"),
        std::make_shared<PrimitiveType>("bool")});
    ASSERT_EQ(t->toString(), "(i64, string, bool)");
}

TEST(typestring_raw_array) {
    auto a = std::make_shared<RawArrayType>(std::make_shared<PrimitiveType>("f64"));
    ASSERT_EQ(a->toString(), "f64[]");
}

TEST(typestring_vector) {
    auto v = std::make_shared<VectorType>(std::make_shared<PrimitiveType>("f32"), 4);
    ASSERT_EQ(v->toString(), "vec4<f32>");
}

TEST(typestring_pointer_single) {
    auto p = std::make_shared<PointerType>(std::make_shared<PrimitiveType>("i8"), 1);
    ASSERT_EQ(p->toString(), "*i8");
}

TEST(typestring_pointer_double) {
    auto p = std::make_shared<PointerType>(std::make_shared<PrimitiveType>("char"), 2);
    ASSERT_EQ(p->toString(), "**char");
}

TEST(typestring_pointer_byval) {
    auto p = std::make_shared<PointerType>(std::make_shared<DataType>("S"), 1);
    p->byval = true;
    ASSERT_EQ(p->toString(), "^S");
}

TEST(typestring_fixed_array) {
    auto a = std::make_shared<FixedArrayType>(std::make_shared<PrimitiveType>("u8"), 64);
    ASSERT_EQ(a->toString(), "u8[64]");
}

TEST(typestring_generic_instance) {
    auto base = std::make_shared<DataType>("Box");
    base->type_params = {"T"};
    auto gi = std::make_shared<GenericInstanceType>(base,
        std::map<std::string, std::shared_ptr<Type>>{{"T", std::make_shared<PrimitiveType>("i64")}});
    ASSERT_EQ(gi->toString(), "Box<i64>");
}

TEST(typestring_record) {
    auto r = std::make_shared<RecordType>(std::map<std::string, std::shared_ptr<Type>>{
        {"name", std::make_shared<PrimitiveType>("string")},
        {"age", std::make_shared<PrimitiveType>("i64")}});
    // Note: map iteration order is alphabetical by key
    ASSERT_EQ(r->toString(), "{age: i64, name: string}");
}

TEST(typestring_function) {
    auto f = std::make_shared<FunctionType>(
        std::vector<std::shared_ptr<Type>>{
            std::make_shared<PrimitiveType>("i64"),
            std::make_shared<PrimitiveType>("string")},
        std::make_shared<PrimitiveType>("bool"));
    ASSERT_EQ(f->toString(), "function(i64, string) -> bool");
}

TEST(typestring_variadic_function) {
    auto f = std::make_shared<FunctionType>(
        std::vector<std::shared_ptr<Type>>{std::make_shared<PrimitiveType>("string")},
        std::make_shared<PrimitiveType>("void"), true);
    ASSERT_EQ(f->toString(), "function(string...) -> void");
}
