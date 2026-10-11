/**
 * DRX Engine - Configuration files implementation
 */

#include "Core/Public/Config.h"
#include "Core/Public/Log.h"

// Own namespace: assimp ships another rapidjson build, identical template names would clash at link time
#define RAPIDJSON_NAMESPACE drx_rapidjson
#define RAPIDJSON_NAMESPACE_BEGIN namespace drx_rapidjson {
#define RAPIDJSON_NAMESPACE_END }
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

namespace rapidjson = drx_rapidjson;

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace DRX {

namespace {

void Flatten(const rapidjson::Value& node, const String& prefix, HashMap<String, Config::Value>& out, bool& ok,
             const String& source) {
    if (node.IsObject()) {
        for (auto it = node.MemberBegin(); it != node.MemberEnd(); ++it) {
            const String key = prefix.empty() ? String(it->name.GetString()) : prefix + "." + it->name.GetString();
            Flatten(it->value, key, out, ok, source);
        }
    } else if (node.IsBool()) {
        out[prefix] = node.GetBool();
    } else if (node.IsNumber()) {
        out[prefix] = node.GetDouble();
    } else if (node.IsString()) {
        out[prefix] = String(node.GetString(), node.GetStringLength());
    } else if (node.IsNull()) {
        // ignored: lets files keep placeholders
    } else {
        DRX_LOG_ERROR("Config {}: '{}' uses an unsupported type (arrays are not supported)", source, prefix);
        ok = false;
    }
}

String ToText(const Config::Value& value) {
    if (const bool* b = std::get_if<bool>(&value)) return *b ? "true" : "false";
    if (const float64* d = std::get_if<float64>(&value)) return fmt::format("{}", *d);
    return "\"" + std::get<String>(value) + "\"";
}

} // namespace

bool Config::LoadFromFile(const String& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        DRX_LOG_ERROR("Config file not found: {}", path);
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();

    if (!LoadFromString(buffer.str(), path)) {
        return false;
    }

    path_ = path;
    std::error_code ec;
    last_write_ = std::filesystem::last_write_time(path, ec);
    return true;
}

bool Config::LoadFromString(const String& json, const String& source_name) {
    rapidjson::Document doc;
    doc.Parse<rapidjson::kParseCommentsFlag | rapidjson::kParseTrailingCommasFlag>(json.c_str(), json.size());
    if (doc.HasParseError()) {
        DRX_LOG_ERROR("Config {}: {} at offset {}", source_name,
                      rapidjson::GetParseError_En(doc.GetParseError()), doc.GetErrorOffset());
        return false;
    }
    if (!doc.IsObject()) {
        DRX_LOG_ERROR("Config {}: root must be an object", source_name);
        return false;
    }

    HashMap<String, Value> parsed;
    bool ok = true;
    Flatten(doc, "", parsed, ok, source_name);
    if (!ok) {
        return false;
    }

    values_ = std::move(parsed);
    for (const auto& [key, value] : overrides_) {
        values_[key] = value;
    }
    if (on_reload_) {
        on_reload_(*this);
    }
    return true;
}

bool Config::SaveToFile(const String& path) const {
    rapidjson::Document doc;
    doc.SetObject();
    auto& alloc = doc.GetAllocator();

    for (const auto& [key, value] : GetAllAsText()) {
        (void)value;
        // Walk / create nested objects for the dotted key
        rapidjson::Value* node = &doc;
        size_t start = 0;
        while (true) {
            const size_t dot = key.find('.', start);
            const String part = key.substr(start, dot == String::npos ? String::npos : dot - start);
            if (dot == String::npos) {
                rapidjson::Value name(part.c_str(), alloc);
                const Value& v = values_.at(key);
                rapidjson::Value json_value;
                if (const bool* b = std::get_if<bool>(&v)) json_value.SetBool(*b);
                else if (const float64* d = std::get_if<float64>(&v)) json_value.SetDouble(*d);
                else json_value.SetString(std::get<String>(v).c_str(), alloc);
                node->AddMember(name, json_value, alloc);
                break;
            }
            auto member = node->FindMember(part.c_str());
            if (member == node->MemberEnd()) {
                rapidjson::Value name(part.c_str(), alloc);
                rapidjson::Value object(rapidjson::kObjectType);
                node->AddMember(name, object, alloc);
                member = node->FindMember(part.c_str());
            }
            node = &member->value;
            start = dot + 1;
        }
    }

    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        DRX_LOG_ERROR("Cannot write config file {}", path);
        return false;
    }
    file << buffer.GetString() << '\n';
    return static_cast<bool>(file);
}

bool Config::ReloadIfChanged() {
    if (path_.empty()) return false;

    std::error_code ec;
    const auto write_time = std::filesystem::last_write_time(path_, ec);
    if (ec || write_time == last_write_) return false;

    last_write_ = write_time;
    if (!LoadFromFile(path_)) {
        DRX_LOG_WARNING("Config {} changed but could not be parsed; keeping previous values", path_);
        return false;
    }
    DRX_LOG_INFO("Config reloaded: {} ({} values)", path_, values_.size());
    return true;
}

const Config::Value* Config::Find(const String& key) const {
    auto it = values_.find(key);
    return it != values_.end() ? &it->second : nullptr;
}

bool Config::Has(const String& key) const {
    return Find(key) != nullptr;
}

bool Config::GetBool(const String& key, bool default_value) const {
    const Value* v = Find(key);
    if (!v) return default_value;
    if (const bool* b = std::get_if<bool>(v)) return *b;
    DRX_LOG_WARNING("Config '{}' is not a bool", key);
    return default_value;
}

int32 Config::GetInt(const String& key, int32 default_value) const {
    const Value* v = Find(key);
    if (!v) return default_value;
    if (const float64* d = std::get_if<float64>(v)) {
        if (std::floor(*d) != *d) {
            DRX_LOG_WARNING("Config '{}' = {} is not a whole number; rounding", key, *d);
        }
        return static_cast<int32>(std::llround(*d));
    }
    DRX_LOG_WARNING("Config '{}' is not a number", key);
    return default_value;
}

float32 Config::GetFloat(const String& key, float32 default_value) const {
    const Value* v = Find(key);
    if (!v) return default_value;
    if (const float64* d = std::get_if<float64>(v)) return static_cast<float32>(*d);
    DRX_LOG_WARNING("Config '{}' is not a number", key);
    return default_value;
}

String Config::GetString(const String& key, const String& default_value) const {
    const Value* v = Find(key);
    if (!v) return default_value;
    if (const String* s = std::get_if<String>(v)) return *s;
    DRX_LOG_WARNING("Config '{}' is not a string", key);
    return default_value;
}

void Config::Set(const String& key, bool value) { values_[key] = value; }
void Config::Set(const String& key, int32 value) { values_[key] = static_cast<float64>(value); }
void Config::Set(const String& key, float32 value) { values_[key] = static_cast<float64>(value); }
void Config::Set(const String& key, const String& value) { values_[key] = value; }

Config::Value Config::ParseValueText(const String& text) {
    if (text == "true") return true;
    if (text == "false") return false;
    if (!text.empty()) {
        char* end = nullptr;
        const float64 number = std::strtod(text.c_str(), &end);
        if (end == text.c_str() + text.size() && std::isfinite(number)) return number;
    }
    return text;
}

void Config::AddOverride(const String& key, const String& value_text) {
    const Value value = ParseValueText(value_text);
    overrides_[key] = value;
    values_[key] = value;
}

Array<std::pair<String, String>> Config::GetAllAsText() const {
    Array<std::pair<String, String>> result;
    result.reserve(values_.size());
    for (const auto& [key, value] : values_) {
        result.emplace_back(key, ToText(value));
    }
    std::sort(result.begin(), result.end());
    return result;
}

void Config::LogAll() const {
    DRX_LOG_INFO("Config '{}' ({} values):", path_.empty() ? String("<memory>") : path_, values_.size());
    for (const auto& [key, text] : GetAllAsText()) {
        DRX_LOG_INFO("  {} = {}", key, text);
    }
}

} // namespace DRX
