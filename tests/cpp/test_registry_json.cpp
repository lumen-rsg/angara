//
// Unit tests for RegistryClient::parse_package_json (M17): strict JSON parsing
// via nlohmann/json, replacing the hand-rolled scanner that silently accepted
// malformed registry responses.
//

#include "test_harness.h"
#include "RegistryClient.h"

#include <string>

using namespace angara;

// ── Happy path ──

TEST(registry_parse_full_response) {
    std::string body = R"({
      "name": "io",
      "versions": [
        {
          "version": "1.2.3",
          "sha256": "abcdef0123456789",
          "has_native": true,
          "source_modules": ["io", "io/file"],
          "dependencies": { "net": "^2.0.0" }
        },
        {
          "version": "2.0.0",
          "sha256": "fedcba9876543210",
          "has_native": false,
          "source_modules": [],
          "dependencies": {}
        }
      ]
    })";

    auto pkg = RegistryClient::parse_package_json(body, "io");
    ASSERT_TRUE(pkg.has_value());
    ASSERT_EQ(pkg->name, "io");
    ASSERT_EQ(pkg->versions.size(), 2u);

    // First version.
    ASSERT_EQ(pkg->versions[0].version.to_string(), "1.2.3");
    ASSERT_EQ(pkg->versions[0].sha256, "abcdef0123456789");
    ASSERT_EQ(pkg->versions[0].has_native, true);
    ASSERT_EQ(pkg->versions[0].source_modules.size(), 2u);
    ASSERT_EQ(pkg->versions[0].source_modules[0], "io");
    ASSERT_EQ(pkg->versions[0].source_modules[1], "io/file");
    ASSERT_EQ(pkg->versions[0].dependencies.size(), 1u);
    ASSERT_EQ(pkg->versions[0].dependencies.at("net"), "^2.0.0");

    // Second version: empty deps / modules.
    ASSERT_EQ(pkg->versions[1].version.to_string(), "2.0.0");
    ASSERT_EQ(pkg->versions[1].has_native, false);
    ASSERT_EQ(pkg->versions[1].source_modules.size(), 0u);
    ASSERT_EQ(pkg->versions[1].dependencies.size(), 0u);
}

TEST(registry_parse_optional_fields_default) {
    // Minimal entry: only "version" is required.
    std::string body = R"({ "versions": [ { "version": "0.1.0" } ] })";

    auto pkg = RegistryClient::parse_package_json(body, "pkg");
    ASSERT_TRUE(pkg.has_value());
    ASSERT_EQ(pkg->versions.size(), 1u);
    ASSERT_EQ(pkg->versions[0].version.to_string(), "0.1.0");
    ASSERT_EQ(pkg->versions[0].sha256, "");
    ASSERT_EQ(pkg->versions[0].has_native, false);
    ASSERT_EQ(pkg->versions[0].source_modules.size(), 0u);
    ASSERT_EQ(pkg->versions[0].dependencies.size(), 0u);
}

TEST(registry_parse_skips_unparseable_versions) {
    std::string body = R"({
      "versions": [
        { "version": "not-a-version" },
        { "version": "1.0.0" }
      ]
    })";

    auto pkg = RegistryClient::parse_package_json(body, "x");
    ASSERT_TRUE(pkg.has_value());
    ASSERT_EQ(pkg->versions.size(), 1u);
    ASSERT_EQ(pkg->versions[0].version.to_string(), "1.0.0");
}

TEST(registry_parse_skips_non_object_versions) {
    std::string body = R"({ "versions": [ "1.0.0", { "version": "1.0.0" } ] })";

    auto pkg = RegistryClient::parse_package_json(body, "x");
    ASSERT_TRUE(pkg.has_value());
    ASSERT_EQ(pkg->versions.size(), 1u);
}

// ── Malformed input rejection (the core of M17) ──

TEST(registry_parse_rejects_truncated_json) {
    std::string body = R"({ "versions": [ { "version": "1.0.0")";  // truncated
    auto pkg = RegistryClient::parse_package_json(body, "x");
    ASSERT_FALSE(pkg.has_value());
}

TEST(registry_parse_rejects_garbage) {
    std::string body = "this is not json";
    auto pkg = RegistryClient::parse_package_json(body, "x");
    ASSERT_FALSE(pkg.has_value());
}

TEST(registry_parse_rejects_empty_string) {
    auto pkg = RegistryClient::parse_package_json("", "x");
    ASSERT_FALSE(pkg.has_value());
}

TEST(registry_parse_rejects_non_object_top) {
    std::string body = "[1, 2, 3]";
    auto pkg = RegistryClient::parse_package_json(body, "x");
    ASSERT_FALSE(pkg.has_value());
}

TEST(registry_parse_rejects_missing_versions) {
    std::string body = R"({ "name": "x" })";  // no "versions" key
    auto pkg = RegistryClient::parse_package_json(body, "x");
    ASSERT_FALSE(pkg.has_value());
}

TEST(registry_parse_rejects_empty_versions) {
    std::string body = R"({ "versions": [] })";
    auto pkg = RegistryClient::parse_package_json(body, "x");
    ASSERT_FALSE(pkg.has_value());  // no parseable versions → nullopt
}
