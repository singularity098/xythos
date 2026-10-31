// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace xythos {

// Global per-user settings and favorites, shared by every plugin instance: one JSON file in configDir().
// Every open UI reloads it when it changes on disk.
struct Settings {
    static constexpr int kVersion = 1;

    std::string namDir;          // library folders (UTF-8), empty if not set
    std::string irDir;
    bool a2Lite = false;         // default A2 size for new NAM blocks
    bool normalize = true;       // default normalization for new NAM and IR blocks
    bool parallel = true;        // run split branch B on a second core
    int accent = 7;              // index into the UI's accent colours (7: white)
    std::string accentColor;     // a colour from the picker (hex) used instead of accent; empty for none
    // ink::Mode: the highlight is the accent (0), or 2-3 colours across the window; Splotchy in the owner's colours
    // to start (2026-10-30). Stored as "highlightStyle"/"highlightColors": the earlier "inkMode"/"inkColors" (never
    // released) are ignored, so this default reaches everyone.
    int inkMode = 2;
    std::vector<std::string> inkColors = { "#7278fe", "#c688d6", "#a85eea" }; // hex, 2 or 3 (Iris to start)
    std::vector<std::string> savedColors; // colours saved from the picker (hex), oldest first
    std::set<std::string> favorites; // content hashes (XXH3-128, hex)
    bool embedFiles = true;      // exported presets carry the files they use (saved ones never do)
    std::string defaultPreset;   // loaded into new plugin instances; empty for none
    std::vector<std::string> recentPresets; // most recent first
    bool checkUpdates = true;    // ask GitHub once a day for a newer release (src/state/UpdateCheck.hpp)
    std::string dismissedUpdate; // the release whose banner was closed: it and older ones stay hidden
    std::map<std::string, std::string> cabImages; // IR content hash -> the built-in cab image the user picked for it
    std::map<std::string, std::string> pedalImages; // NAM pedal content hash -> the generic pedal image picked for it
    std::map<std::string, std::string> ampImages;   // NAM amp content hash -> the amp head image picked for it
    // The user's own defaults for new blocks (the block panel's Set as Default; owner, 2026-10-31): block typeId ->
    // control name -> value, in the control's units. Controls not listed keep the type's default.
    std::map<std::string, std::map<std::string, float>> blockDefaults;

    static constexpr size_t kMaxRecentPresets = 8;
    void addRecentPreset(const std::string& path);

    std::string toJson() const;
    // Missing or invalid fields keep their defaults
    static Settings fromJson(std::string_view json);

    static std::filesystem::path defaultPath();
    static Settings load(const std::filesystem::path& path = defaultPath());
    bool save(const std::filesystem::path& path = defaultPath()) const;

    bool operator==(const Settings&) const = default;
};

} // namespace xythos
