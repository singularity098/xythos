// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include <doctest/doctest.h>

#include "state/Paths.hpp"
#include "state/Settings.hpp"

#include <algorithm>
#include <filesystem>

using namespace xythos;

TEST_CASE("settings round-trip through JSON and a file")
{
    Settings settings;
    settings.namDir = "/home/me/NAM models/ünïcode";
    settings.irDir = "C:\\IRs";
    settings.a2Lite = true;
    settings.normalize = false;
    settings.parallel = false;
    settings.accent = 3;
    settings.favorites = { "00112233445566778899aabbccddeeff", "ffeeddccbbaa99887766554433221100" };
    settings.embedFiles = false;
    settings.recentPresets = { "/a.xythos", "/b.xythos" };
    settings.defaultPreset = "/presets/Clean/Twin.xythos";
    settings.checkUpdates = false;
    settings.dismissedUpdate = "1.2.0";
    settings.cabImages = { { "00112233445566778899aabbccddeeff", "4x12-3" }, { "/irs/no hash.wav", "1x10-1" } };
    settings.pedalImages = { { "ffeeddccbbaa99887766554433221100", "pedal-7" } };
    settings.ampImages = { { "ffeeddccbbaa99887766554433221100", "amp-12" } };
    settings.blockDefaults = { { "looper", { { "Latency", 14.0f }, { "BPM", 96.0f } } }, { "delay", { { "Mix", 30.5f } } } };

    CHECK(Settings::fromJson(settings.toJson()) == settings);

    const auto path = std::filesystem::temp_directory_path() / "xythos-test-settings" / "settings.json";
    std::filesystem::remove_all(path.parent_path());
    REQUIRE(settings.save(path));
    CHECK(Settings::load(path) == settings);
}

TEST_CASE("settings fall back to defaults for missing or broken values")
{
    CHECK(Settings::fromJson("") == Settings {});
    CHECK(Settings::fromJson("not json") == Settings {});
    CHECK(Settings::fromJson("[1, 2]") == Settings {});
    CHECK(Settings::load(std::filesystem::temp_directory_path() / "xythos-no-such-settings.json") == Settings {});

    const Settings partial = Settings::fromJson(R"({"namDir": "/nam", "accent": "purple", "a2Lite": true})");
    CHECK(partial.namDir == "/nam");
    CHECK(partial.a2Lite);
    CHECK(partial.accent == 7); // wrong type: default (white)
    CHECK(partial.normalize);
}

TEST_CASE("paths convert to and from UTF-8")
{
    const std::string text = "/tmp/ämp — 1.nam";
    CHECK(pathToUtf8(pathFromUtf8(text)) == text);
}

TEST_CASE("paths show the home folder as ~")
{
    const std::string home = "/home/me";
    CHECK(shortenHomePath("/home/me/NAM", home) == "~/NAM");
    CHECK(shortenHomePath("/home/me/NAM/amps.zip!/inner/a.nam", home) == "~/NAM/amps.zip!/inner/a.nam");
    CHECK(shortenHomePath("/home/me", home) == "~");
    CHECK(shortenHomePath("/home/me/NAM", "/home/me/") == "~/NAM");
    CHECK(shortenHomePath("/home/me2/NAM", home) == "/home/me2/NAM");
    CHECK(shortenHomePath("/opt/NAM", home) == "/opt/NAM");
    CHECK(shortenHomePath("/opt/NAM", "/") == "/opt/NAM");
    CHECK(shortenHomePath("", home).empty());

    CHECK(expandHomePath("~/NAM", home) == "/home/me/NAM");
    CHECK(expandHomePath("~", home) == "/home/me");
    CHECK(expandHomePath("~other/NAM", home) == "~other/NAM");
    CHECK(expandHomePath("/opt/~/NAM", home) == "/opt/~/NAM");
    CHECK(expandHomePath(shortenHomePath("/home/me/IRs", home), home) == "/home/me/IRs");
}

TEST_CASE("recent presets: most recent first, no duplicates, bounded")
{
    Settings settings;
    for (int i = 0; i < 12; ++i)
        settings.addRecentPreset("/p" + std::to_string(i));
    settings.addRecentPreset("/p5");
    REQUIRE(settings.recentPresets.size() == Settings::kMaxRecentPresets);
    CHECK(settings.recentPresets[0] == "/p5");
    CHECK(settings.recentPresets[1] == "/p11");
    CHECK(std::count(settings.recentPresets.begin(), settings.recentPresets.end(), "/p5") == 1);
}
