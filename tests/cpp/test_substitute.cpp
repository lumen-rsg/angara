//
// Unit tests for TS-2 Phase 1: deep type substitution.
// GenericInstanceType::substitute and the free substituteTypeArgs() must
// recurse into compound types (the old versions were one-level only).

#include "test_harness.h"
#include "Type.h"

using namespace angara;

// ── GenericInstanceType::substitute ──

TEST(substitute_bare_type_param) {
    // The original one-level case: bare T -> i64.
    auto data = std::make_shared<DataType>("Box");
    std::map<std::string, std::shared_ptr<Type>> args = {{"T", std::make_shared<PrimitiveType>("i64")}};
    GenericInstanceType gi(data, args);

    auto tp = std::make_shared<TypeParameterType>("T");
    auto result = gi.substitute(tp);
    ASSERT_TRUE(result->kind == TypeKind::PRIMITIVE);
    ASSERT_EQ(result->toString(), "i64");
}

TEST(substitute_unbound_param_unchanged) {
    auto data = std::make_shared<DataType>("Box");
    std::map<std::string, std::shared_ptr<Type>> args = {{"T", std::make_shared<PrimitiveType>("i64")}};
    GenericInstanceType gi(data, args);

    auto tp = std::make_shared<TypeParameterType>("U");  // not in args
    auto result = gi.substitute(tp);
    ASSERT_TRUE(result->kind == TypeKind::TYPE_PARAM);
    ASSERT_EQ(result->toString(), "U");
}

TEST(substitute_recurses_into_list) {
    // list<T> with T->i64 should yield list<i64> (deep).
    auto data = std::make_shared<DataType>("Box");
    std::map<std::string, std::shared_ptr<Type>> args = {{"T", std::make_shared<PrimitiveType>("i64")}};
    GenericInstanceType gi(data, args);

    auto list_t = std::make_shared<ListType>(std::make_shared<TypeParameterType>("T"));
    auto result = gi.substitute(list_t);
    ASSERT_TRUE(result->kind == TypeKind::LIST);
    auto rl = std::dynamic_pointer_cast<ListType>(result);
    ASSERT_EQ(rl->element_type->toString(), "i64");
}

TEST(substitute_recurses_into_optional) {
    auto data = std::make_shared<DataType>("Box");
    std::map<std::string, std::shared_ptr<Type>> args = {{"T", std::make_shared<PrimitiveType>("string")}};
    GenericInstanceType gi(data, args);

    auto opt_t = std::make_shared<OptionalType>(std::make_shared<TypeParameterType>("T"));
    auto result = gi.substitute(opt_t);
    ASSERT_TRUE(result->kind == TypeKind::OPTIONAL);
    ASSERT_EQ(result->toString(), "string?");
}

TEST(substitute_recurses_into_nested_generic) {
    // Box<T> with T->i64, applied to a field of type Pair<T> -> Pair<i64>.
    auto outer = std::make_shared<DataType>("Box");
    std::map<std::string, std::shared_ptr<Type>> args = {{"T", std::make_shared<PrimitiveType>("i64")}};
    GenericInstanceType gi(outer, args);

    auto pair_base = std::make_shared<DataType>("Pair");
    std::map<std::string, std::shared_ptr<Type>> pair_args = {{"T", std::make_shared<TypeParameterType>("T")}};
    auto nested = std::make_shared<GenericInstanceType>(pair_base, pair_args);

    auto result = gi.substitute(nested);
    ASSERT_TRUE(result->kind == TypeKind::GENERIC_INSTANCE);
    auto rg = std::dynamic_pointer_cast<GenericInstanceType>(result);
    ASSERT_EQ(rg->type_args["T"]->toString(), "i64");  // the inner T was substituted
}

TEST(substitute_leaves_concrete_unchanged) {
    auto data = std::make_shared<DataType>("Box");
    std::map<std::string, std::shared_ptr<Type>> args = {{"T", std::make_shared<PrimitiveType>("i64")}};
    GenericInstanceType gi(data, args);

    auto s = std::make_shared<PrimitiveType>("string");
    auto result = gi.substitute(s);
    ASSERT_EQ(result->toString(), "string");
}

// ── free substituteTypeArgs ──

TEST(substitutetypeargs_bare_param) {
    std::map<std::string, std::shared_ptr<Type>> args = {{"K", std::make_shared<PrimitiveType>("string")}};
    auto result = substituteTypeArgs(std::make_shared<TypeParameterType>("K"), args);
    ASSERT_EQ(result->toString(), "string");
}

TEST(substitutetypeargs_recurses_into_function) {
    // function(T) -> T  with  T->i64  =>  function(i64) -> i64
    std::map<std::string, std::shared_ptr<Type>> args = {{"T", std::make_shared<PrimitiveType>("i64")}};
    auto tp = std::make_shared<TypeParameterType>("T");
    auto fn = std::make_shared<FunctionType>(
        std::vector<std::shared_ptr<Type>>{tp}, tp);
    auto result = substituteTypeArgs(fn, args);
    ASSERT_TRUE(result->kind == TypeKind::FUNCTION);
    auto rf = std::dynamic_pointer_cast<FunctionType>(result);
    ASSERT_EQ(rf->param_types[0]->toString(), "i64");
    ASSERT_EQ(rf->return_type->toString(), "i64");
}

TEST(substitutetypeargs_empty_args_noop) {
    std::map<std::string, std::shared_ptr<Type>> args;
    auto tp = std::make_shared<TypeParameterType>("T");
    auto result = substituteTypeArgs(tp, args);
    ASSERT_EQ(result.get(), tp.get());  // unchanged (same pointer)
}
