//
// Unit tests for Lockfile load/save (M17): strict JSON parsing via nlohmann/json.
//
// Covers: field round-trip (including the dependencies load path that was
// previously broken), malformed-input rejection, and the missing-file contract.
//

#include "test_harness.h"
#include "Lockfile.h"

#include <filesystem>
#include <fstream>
#include <string>

using namespace angara;

// Unique temp path per test invocation so parallel runs don't collide.
static std::string unique_tmp(const char* suffix) {
    auto base = std::filesystem::temp_directory_path();
    auto p = base / ("angara_test_lockfile_" +
                     std::to_string(reinterpret_cast<uintptr_t>(&base)) +
                     suffix);
    return p.string();
}

// ── Round-trip ──

TEST(lockfile_roundtrip_preserves_all_fields) {
    std::string path = unique_tmp("_rt.lock");

    Lockfile out;
    out.set_path(path);

    LockfileEntry io;
    io.name = "io";
    io.version = "1.2.3";
    io.sha256 = "abc123def";
    io.has_native = true;
    io.source_modules = {"io", "io/file"};
    io.dependencies = {{"net", "^2.0.0"}, {"fs", "~1.5.0"}};
    out.set("io", io);

    LockfileEntry empty_deps;
    empty_deps.name = "core";
    empty_deps.version = "0.4.1";
    empty_deps.sha256 = "deadbeef";
    empty_deps.has_native = false;
    empty_deps.source_modules = {};
    empty_deps.dependencies = {};
    out.set("core", empty_deps);

    ASSERT_TRUE(out.save());

    Lockfile in;
    ASSERT_TRUE(in.load(path));
    ASSERT_EQ(in.format_version(), 1);

    const LockfileEntry* got_io = in.get("io");
    ASSERT_TRUE(got_io != nullptr);
    ASSERT_EQ(got_io->name, "io");
    ASSERT_EQ(got_io->version, "1.2.3");
    ASSERT_EQ(got_io->sha256, "abc123def");
    ASSERT_EQ(got_io->has_native, true);
    ASSERT_EQ(got_io->source_modules.size(), 2u);
    ASSERT_EQ(got_io->source_modules[0], "io");
    ASSERT_EQ(got_io->source_modules[1], "io/file");
    ASSERT_EQ(got_io->dependencies.size(), 2u);
    ASSERT_EQ(got_io->dependencies.at("net"), "^2.0.0");
    ASSERT_EQ(got_io->dependencies.at("fs"), "~1.5.0");

    // M17 bug fix: dependencies were never loaded before; assert the non-empty
    // case explicitly, then the empty case below.
    const LockfileEntry* got_core = in.get("core");
    ASSERT_TRUE(got_core != nullptr);
    ASSERT_EQ(got_core->dependencies.size(), 0u);
    ASSERT_EQ(got_core->has_native, false);
    ASSERT_EQ(got_core->source_modules.size(), 0u);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(lockfile_roundtrip_format_version) {
    std::string path = unique_tmp("_fv.lock");

    Lockfile out;
    out.set_path(path);
    out.set("x", LockfileEntry{.name = "x", .version = "1.0.0"});
    // Bump format version: set_path doesn't expose a setter, so verify the
    // default round-trips instead (1). The value is read on load.
    ASSERT_TRUE(out.save());

    Lockfile in;
    ASSERT_TRUE(in.load(path));
    ASSERT_EQ(in.format_version(), 1);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// ── Malformed input rejection (the core of M17) ──

TEST(lockfile_rejects_truncated_json) {
    std::string path = unique_tmp("_trunc.lock");
    {
        std::ofstream f(path);
        f << "{ \"version\": 1, \"packages\": { \"io\": { \"version\": \"1.0.0\"";  // truncated
    }

    Lockfile in;
    ASSERT_FALSE(in.load(path));   // malformed → false
    ASSERT_EQ(in.entries().size(), 0u);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(lockfile_rejects_stray_token) {
    std::string path = unique_tmp("_stray.lock");
    {
        std::ofstream f(path);
        f << "this is not json at all }}}";
    }

    Lockfile in;
    ASSERT_FALSE(in.load(path));
    ASSERT_EQ(in.entries().size(), 0u);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(lockfile_rejects_bad_utf8) {
    std::string path = unique_tmp("_utf8.lock");
    {
        std::ofstream f(path, std::ios::binary);
        // Valid-shaped JSON but with an invalid UTF-8 continuation byte (0xff).
        std::string bad = "{ \"version\": 1, \"packages\": { \"\xff\": {} } }";
        f.write(bad.data(), static_cast<std::streamsize>(bad.size()));
    }

    Lockfile in;
    ASSERT_FALSE(in.load(path));
    ASSERT_EQ(in.entries().size(), 0u);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(lockfile_rejects_array_top_level) {
    std::string path = unique_tmp("_arr.lock");
    {
        std::ofstream f(path);
        f << "[1, 2, 3]";  // valid JSON but wrong type — not an object
    }

    Lockfile in;
    ASSERT_FALSE(in.load(path));
    ASSERT_EQ(in.entries().size(), 0u);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// ── Missing-file contract ──

TEST(lockfile_missing_file_returns_true) {
    std::string path = unique_tmp("_missing.lock");
    std::error_code rm;
    std::filesystem::remove(path, rm);  // ensure it doesn't exist

    Lockfile in;
    ASSERT_TRUE(in.load(path));           // missing lockfile is OK
    ASSERT_EQ(in.entries().size(), 0u);
}

// ── Empty packages object ──

TEST(lockfile_empty_packages_loads_clean) {
    std::string path = unique_tmp("_empty.lock");
    {
        std::ofstream f(path);
        f << "{ \"version\": 2, \"packages\": {} }";
    }

    Lockfile in;
    ASSERT_TRUE(in.load(path));
    ASSERT_EQ(in.format_version(), 2);
    ASSERT_EQ(in.entries().size(), 0u);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}
