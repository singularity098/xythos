// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "state/Settings.hpp"

#include "state/Paths.hpp"

#include <nlohmann/json.hpp>

namespace xythos {

using nlohmann::json;

std::string Settings::toJson() const
{
    return json {
        { "version", kVersion },
        { "namDir", namDir },
        { "irDir", irDir },
        { "a2Lite", a2Lite },
        { "normalize", normalize },
        { "parallel", parallel },
        { "accent", accent },
        { "accentColor", accentColor },
        { "highlightStyle", inkMode },
        { "highlightColors", inkColors },
        { "savedColors", savedColors },
        { "favorites", favorites },
        { "embedFiles", embedFiles },
        { "defaultPreset", defaultPreset },
        { "recentPresets", recentPresets },
        { "checkUpdates", checkUpdates },
        { "dismissedUpdate", dismissedUpdate },
        { "cabImages", cabImages },
        { "pedalImages", pedalImages },
        { "ampImages", ampImages },
        { "blockDefaults", blockDefaults },
    }.dump(2);
}

Settings Settings::fromJson(std::string_view text)
{
    Settings settings;
    try
    {
        const json root = json::parse(text);
        if (!root.is_object())
            return settings;
        auto get = [&root](const char* key, auto& value) {
            const auto it = root.find(key);
            if (it == root.end())
                return;
            try
            {
                value = it->get<std::remove_reference_t<decltype(value)>>();
            }
            catch (const json::exception&)
            {
            }
        };
        get("namDir", settings.namDir);
        get("irDir", settings.irDir);
        get("a2Lite", settings.a2Lite);
        get("normalize", settings.normalize);
        get("parallel", settings.parallel);
        get("accent", settings.accent);
        get("accentColor", settings.accentColor);
        get("highlightStyle", settings.inkMode);
        get("highlightColors", settings.inkColors);
        get("savedColors", settings.savedColors);
        get("favorites", settings.favorites);
        get("embedFiles", settings.embedFiles);
        get("defaultPreset", settings.defaultPreset);
        get("recentPresets", settings.recentPresets);
        get("checkUpdates", settings.checkUpdates);
        get("dismissedUpdate", settings.dismissedUpdate);
        get("cabImages", settings.cabImages);
        get("pedalImages", settings.pedalImages);
        get("ampImages", settings.ampImages);
        get("blockDefaults", settings.blockDefaults);
    }
    catch (const json::exception&)
    {
    }
    return settings;
}

void Settings::addRecentPreset(const std::string& path)
{
    std::erase(recentPresets, path);
    recentPresets.insert(recentPresets.begin(), path);
    if (recentPresets.size() > kMaxRecentPresets)
        recentPresets.resize(kMaxRecentPresets);
}

std::filesystem::path Settings::defaultPath()
{
    return configDir() / "settings.json";
}

Settings Settings::load(const std::filesystem::path& path)
{
    const std::optional<std::string> text = readFile(path);
    return text ? fromJson(*text) : Settings {};
}

bool Settings::save(const std::filesystem::path& path) const
{
    return writeFileAtomic(path, toJson());
}

} // namespace xythos
