#pragma once

/**
 * DRX Engine - Configuration files (JSON)
 * Gameplay values live in data, not code. Values are addressed with dotted paths
 * ("player.walk_speed"), every lookup has a default, and the file can be hot-reloaded.
 */

#include "Core/Public/CoreTypes.h"

#include <filesystem>
#include <variant>

namespace DRX {

class Config {
public:
    using Value = std::variant<bool, float64, String>;

    // Loads a JSON file; on failure the previous values are kept
    bool LoadFromFile(const String& path);
    bool LoadFromString(const String& json, const String& source_name = "<memory>");
    bool SaveToFile(const String& path) const;

    // Reloads when the file changed on disk; returns true if new values were applied
    bool ReloadIfChanged();

    bool Has(const String& key) const;
    bool GetBool(const String& key, bool default_value) const;
    int32 GetInt(const String& key, int32 default_value) const;
    float32 GetFloat(const String& key, float32 default_value) const;
    String GetString(const String& key, const String& default_value) const;

    void Set(const String& key, bool value);
    void Set(const String& key, int32 value);
    void Set(const String& key, float32 value);
    void Set(const String& key, const String& value);

    // Command line style override ("true", "1.5", "text"); survives reloads and wins over file values
    void AddOverride(const String& key, const String& value_text);

    // All leaf values sorted by key, e.g. for logging at startup
    Array<std::pair<String, String>> GetAllAsText() const;
    void LogAll() const;

    const String& GetPath() const { return path_; }
    size_t GetCount() const { return values_.size(); }

    // Called after every successful (re)load
    void SetReloadCallback(Function<void(const Config&)> callback) { on_reload_ = std::move(callback); }

private:
    const Value* Find(const String& key) const;

    static Value ParseValueText(const String& text);

    HashMap<String, Value> values_;
    HashMap<String, Value> overrides_;
    String path_;
    std::filesystem::file_time_type last_write_{};
    Function<void(const Config&)> on_reload_;
};

} // namespace DRX
