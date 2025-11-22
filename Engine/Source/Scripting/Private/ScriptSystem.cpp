/**
 * DRX Engine - Scripting System Implementation
 * Lua integration
 */

#include "Scripting/Public/ScriptSystem.h"
#include "Core/Public/Log.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace DRX::Scripting {

// ============================================================================
// ScriptValue Implementation
// ============================================================================

ScriptValue::ScriptValue() : type_(ScriptValueType::Nil) {}
ScriptValue::ScriptValue(bool value) : type_(ScriptValueType::Boolean) { bool_value = value; }
ScriptValue::ScriptValue(int32 value) : type_(ScriptValueType::Number) { number_value = value; }
ScriptValue::ScriptValue(float64 value) : type_(ScriptValueType::Number) { number_value = value; }
ScriptValue::ScriptValue(const String& value) : type_(ScriptValueType::String), string_value_(value) {}
ScriptValue::ScriptValue(const char* value) : type_(ScriptValueType::String), string_value_(value) {}

ScriptValue::~ScriptValue() = default;

ScriptValue::ScriptValue(const ScriptValue& other)
    : type_(other.type_), string_value_(other.string_value_), table_value_(other.table_value_) {
    if (type_ == ScriptValueType::Boolean) bool_value = other.bool_value;
    else if (type_ == ScriptValueType::Number) number_value = other.number_value;
}

ScriptValue::ScriptValue(ScriptValue&& other) noexcept
    : type_(other.type_), string_value_(std::move(other.string_value_)),
      table_value_(std::move(other.table_value_)) {
    if (type_ == ScriptValueType::Boolean) bool_value = other.bool_value;
    else if (type_ == ScriptValueType::Number) number_value = other.number_value;
    other.type_ = ScriptValueType::Nil;
}

ScriptValue& ScriptValue::operator=(const ScriptValue& other) {
    if (this != &other) {
        type_ = other.type_;
        string_value_ = other.string_value_;
        table_value_ = other.table_value_;
        if (type_ == ScriptValueType::Boolean) bool_value = other.bool_value;
        else if (type_ == ScriptValueType::Number) number_value = other.number_value;
    }
    return *this;
}

ScriptValue& ScriptValue::operator=(ScriptValue&& other) noexcept {
    if (this != &other) {
        type_ = other.type_;
        string_value_ = std::move(other.string_value_);
        table_value_ = std::move(other.table_value_);
        if (type_ == ScriptValueType::Boolean) bool_value = other.bool_value;
        else if (type_ == ScriptValueType::Number) number_value = other.number_value;
        other.type_ = ScriptValueType::Nil;
    }
    return *this;
}

bool ScriptValue::AsBool() const {
    if (type_ == ScriptValueType::Boolean) return bool_value;
    if (type_ == ScriptValueType::Number) return number_value != 0;
    return !IsNil();
}

int32 ScriptValue::AsInt() const {
    if (type_ == ScriptValueType::Number) return static_cast<int32>(number_value);
    if (type_ == ScriptValueType::String) return std::stoi(string_value_);
    return 0;
}

float64 ScriptValue::AsNumber() const {
    if (type_ == ScriptValueType::Number) return number_value;
    if (type_ == ScriptValueType::String) return std::stod(string_value_);
    return 0.0;
}

String ScriptValue::AsString() const {
    if (type_ == ScriptValueType::String) return string_value_;
    if (type_ == ScriptValueType::Number) return std::to_string(number_value);
    if (type_ == ScriptValueType::Boolean) return bool_value ? "true" : "false";
    return "";
}

ScriptValue ScriptValue::Get(const String& key) const {
    if (type_ != ScriptValueType::Table) return ScriptValue();
    auto it = table_value_.find(key);
    if (it != table_value_.end()) return *it->second;
    return ScriptValue();
}

void ScriptValue::Set(const String& key, const ScriptValue& value) {
    if (type_ != ScriptValueType::Table) {
        type_ = ScriptValueType::Table;
    }
    table_value_[key] = MakeShared<ScriptValue>(value);
}

Array<String> ScriptValue::GetKeys() const {
    Array<String> keys;
    for (const auto& [key, value] : table_value_) {
        keys.push_back(key);
    }
    return keys;
}

// ============================================================================
// LuaScript Implementation
// ============================================================================

LuaScript::LuaScript(lua_State* L, const String& script_path)
    : L_(L), script_path_(script_path) {}

LuaScript::~LuaScript() {
    if (L_ && script_ref_ != -1) {
        luaL_unref(L_, LUA_REGISTRYINDEX, script_ref_);
    }
}

bool LuaScript::Load() {
    if (!L_) return false;

    if (luaL_loadfile(L_, script_path_.c_str()) != LUA_OK) {
        DRX_LOG_ERROR("Lua load error: {}", lua_tostring(L_, -1));
        lua_pop(L_, 1);
        return false;
    }

    // Execute the chunk to get the script table
    if (lua_pcall(L_, 0, 1, 0) != LUA_OK) {
        DRX_LOG_ERROR("Lua exec error: {}", lua_tostring(L_, -1));
        lua_pop(L_, 1);
        return false;
    }

    // Store reference to the script table
    script_ref_ = luaL_ref(L_, LUA_REGISTRYINDEX);
    loaded_ = true;

    DRX_LOG_INFO("Loaded script: {}", script_path_);
    return true;
}

bool LuaScript::Reload() {
    if (script_ref_ != -1) {
        luaL_unref(L_, LUA_REGISTRYINDEX, script_ref_);
        script_ref_ = -1;
    }
    loaded_ = false;
    return Load();
}

void LuaScript::OnCreate() { CallMethod("OnCreate"); }
void LuaScript::OnDestroy() { CallMethod("OnDestroy"); }

void LuaScript::OnUpdate(float32 delta_time) {
    if (!loaded_ || script_ref_ == -1) return;

    lua_rawgeti(L_, LUA_REGISTRYINDEX, script_ref_);
    if (lua_istable(L_, -1)) {
        lua_getfield(L_, -1, "OnUpdate");
        if (lua_isfunction(L_, -1)) {
            lua_pushvalue(L_, -2);  // self
            lua_pushnumber(L_, delta_time);
            if (lua_pcall(L_, 2, 0, 0) != LUA_OK) {
                DRX_LOG_ERROR("Lua error in OnUpdate: {}", lua_tostring(L_, -1));
                lua_pop(L_, 1);
            }
        } else {
            lua_pop(L_, 1);
        }
    }
    lua_pop(L_, 1);
}

void LuaScript::OnFixedUpdate(float32 fixed_delta_time) {
    if (!loaded_ || script_ref_ == -1) return;

    lua_rawgeti(L_, LUA_REGISTRYINDEX, script_ref_);
    if (lua_istable(L_, -1)) {
        lua_getfield(L_, -1, "OnFixedUpdate");
        if (lua_isfunction(L_, -1)) {
            lua_pushvalue(L_, -2);
            lua_pushnumber(L_, fixed_delta_time);
            if (lua_pcall(L_, 2, 0, 0) != LUA_OK) {
                DRX_LOG_ERROR("Lua error in OnFixedUpdate: {}", lua_tostring(L_, -1));
                lua_pop(L_, 1);
            }
        } else {
            lua_pop(L_, 1);
        }
    }
    lua_pop(L_, 1);
}

void LuaScript::OnCollisionEnter(void* other) { CallMethod("OnCollisionEnter"); }
void LuaScript::OnCollisionStay(void* other) { CallMethod("OnCollisionStay"); }
void LuaScript::OnCollisionExit(void* other) { CallMethod("OnCollisionExit"); }
void LuaScript::OnTriggerEnter(void* other) { CallMethod("OnTriggerEnter"); }
void LuaScript::OnTriggerExit(void* other) { CallMethod("OnTriggerExit"); }

void LuaScript::CallMethod(const String& name) {
    if (!loaded_ || script_ref_ == -1) return;

    lua_rawgeti(L_, LUA_REGISTRYINDEX, script_ref_);
    if (lua_istable(L_, -1)) {
        lua_getfield(L_, -1, name.c_str());
        if (lua_isfunction(L_, -1)) {
            lua_pushvalue(L_, -2);  // self
            if (lua_pcall(L_, 1, 0, 0) != LUA_OK) {
                DRX_LOG_ERROR("Lua error in {}: {}", name, lua_tostring(L_, -1));
                lua_pop(L_, 1);
            }
        } else {
            lua_pop(L_, 1);
        }
    }
    lua_pop(L_, 1);
}

Array<ScriptValue> LuaScript::CallFunction(const String& name, const Array<ScriptValue>& args) {
    Array<ScriptValue> results;

    if (!loaded_ || script_ref_ == -1) return results;

    lua_rawgeti(L_, LUA_REGISTRYINDEX, script_ref_);
    if (lua_istable(L_, -1)) {
        lua_getfield(L_, -1, name.c_str());
        if (lua_isfunction(L_, -1)) {
            lua_pushvalue(L_, -2);  // self

            for (const auto& arg : args) {
                PushValue(arg);
            }

            int nargs = static_cast<int>(args.size()) + 1;  // +1 for self
            if (lua_pcall(L_, nargs, LUA_MULTRET, 0) != LUA_OK) {
                DRX_LOG_ERROR("Lua error calling {}: {}", name, lua_tostring(L_, -1));
                lua_pop(L_, 1);
            } else {
                int nresults = lua_gettop(L_) - 1;  // -1 for table
                for (int i = 0; i < nresults; i++) {
                    results.push_back(PopValue());
                }
            }
        } else {
            lua_pop(L_, 1);
        }
    }
    lua_pop(L_, 1);

    return results;
}

bool LuaScript::HasFunction(const String& name) const {
    if (!loaded_ || script_ref_ == -1) return false;

    lua_rawgeti(L_, LUA_REGISTRYINDEX, script_ref_);
    if (lua_istable(L_, -1)) {
        lua_getfield(L_, -1, name.c_str());
        bool has = lua_isfunction(L_, -1);
        lua_pop(L_, 2);
        return has;
    }
    lua_pop(L_, 1);
    return false;
}

ScriptValue LuaScript::GetVariable(const String& name) const {
    if (!loaded_ || script_ref_ == -1) return ScriptValue();

    lua_rawgeti(L_, LUA_REGISTRYINDEX, script_ref_);
    if (lua_istable(L_, -1)) {
        lua_getfield(L_, -1, name.c_str());
        ScriptValue value = const_cast<LuaScript*>(this)->PopValue();
        lua_pop(L_, 1);
        return value;
    }
    lua_pop(L_, 1);
    return ScriptValue();
}

void LuaScript::SetVariable(const String& name, const ScriptValue& value) {
    if (!loaded_ || script_ref_ == -1) return;

    lua_rawgeti(L_, LUA_REGISTRYINDEX, script_ref_);
    if (lua_istable(L_, -1)) {
        PushValue(value);
        lua_setfield(L_, -2, name.c_str());
    }
    lua_pop(L_, 1);
}

void LuaScript::PushValue(const ScriptValue& value) {
    switch (value.GetType()) {
        case ScriptValueType::Nil:
            lua_pushnil(L_);
            break;
        case ScriptValueType::Boolean:
            lua_pushboolean(L_, value.AsBool());
            break;
        case ScriptValueType::Number:
            lua_pushnumber(L_, value.AsNumber());
            break;
        case ScriptValueType::String:
            lua_pushstring(L_, value.AsString().c_str());
            break;
        default:
            lua_pushnil(L_);
            break;
    }
}

ScriptValue LuaScript::PopValue() {
    ScriptValue value;

    int type = lua_type(L_, -1);
    switch (type) {
        case LUA_TNIL:
            value = ScriptValue();
            break;
        case LUA_TBOOLEAN:
            value = ScriptValue(lua_toboolean(L_, -1) != 0);
            break;
        case LUA_TNUMBER:
            value = ScriptValue(lua_tonumber(L_, -1));
            break;
        case LUA_TSTRING:
            value = ScriptValue(lua_tostring(L_, -1));
            break;
        default:
            value = ScriptValue();
            break;
    }

    lua_pop(L_, 1);
    return value;
}

// ============================================================================
// ScriptEngine Implementation
// ============================================================================

ScriptEngine& ScriptEngine::Instance() {
    static ScriptEngine instance;
    return instance;
}

bool ScriptEngine::Initialize() {
    L_ = luaL_newstate();
    if (!L_) {
        DRX_LOG_ERROR("Failed to create Lua state");
        return false;
    }

    luaL_openlibs(L_);

    // Set custom error handler
    lua_atpanic(L_, LuaErrorHandler);

    // Override print
    lua_pushcfunction(L_, LuaPrint);
    lua_setglobal(L_, "print");

    // Register engine API
    RegisterEngineAPI();

    initialized_ = true;
    DRX_LOG_INFO("Script engine initialized");
    return true;
}

void ScriptEngine::Shutdown() {
    scripts_.clear();

    if (L_) {
        lua_close(L_);
        L_ = nullptr;
    }

    initialized_ = false;
    DRX_LOG_INFO("Script engine shutdown");
}

void ScriptEngine::Update(float32 delta_time) {
    for (auto& script : scripts_) {
        script->OnUpdate(delta_time);
    }
}

bool ScriptEngine::ExecuteString(const String& code) {
    if (!L_) return false;

    if (luaL_dostring(L_, code.c_str()) != LUA_OK) {
        last_error_ = lua_tostring(L_, -1);
        lua_pop(L_, 1);

        if (error_handler_) {
            error_handler_(last_error_);
        }

        DRX_LOG_ERROR("Lua error: {}", last_error_);
        return false;
    }

    return true;
}

bool ScriptEngine::ExecuteFile(const String& path) {
    if (!L_) return false;

    if (luaL_dofile(L_, path.c_str()) != LUA_OK) {
        last_error_ = lua_tostring(L_, -1);
        lua_pop(L_, 1);

        if (error_handler_) {
            error_handler_(last_error_);
        }

        DRX_LOG_ERROR("Lua error loading {}: {}", path, last_error_);
        return false;
    }

    return true;
}

void ScriptEngine::SetGlobal(const String& name, const ScriptValue& value) {
    if (!L_) return;

    switch (value.GetType()) {
        case ScriptValueType::Nil:
            lua_pushnil(L_);
            break;
        case ScriptValueType::Boolean:
            lua_pushboolean(L_, value.AsBool());
            break;
        case ScriptValueType::Number:
            lua_pushnumber(L_, value.AsNumber());
            break;
        case ScriptValueType::String:
            lua_pushstring(L_, value.AsString().c_str());
            break;
        default:
            lua_pushnil(L_);
            break;
    }

    lua_setglobal(L_, name.c_str());
}

ScriptValue ScriptEngine::GetGlobal(const String& name) const {
    if (!L_) return ScriptValue();

    lua_getglobal(L_, name.c_str());

    ScriptValue value;
    int type = lua_type(L_, -1);
    switch (type) {
        case LUA_TBOOLEAN:
            value = ScriptValue(lua_toboolean(L_, -1) != 0);
            break;
        case LUA_TNUMBER:
            value = ScriptValue(lua_tonumber(L_, -1));
            break;
        case LUA_TSTRING:
            value = ScriptValue(lua_tostring(L_, -1));
            break;
        default:
            value = ScriptValue();
            break;
    }

    lua_pop(L_, 1);
    return value;
}

void ScriptEngine::AddSearchPath(const String& path) {
    if (!L_) return;

    lua_getglobal(L_, "package");
    lua_getfield(L_, -1, "path");

    String current_path = lua_tostring(L_, -1);
    current_path += ";" + path + "/?.lua";

    lua_pop(L_, 1);
    lua_pushstring(L_, current_path.c_str());
    lua_setfield(L_, -2, "path");
    lua_pop(L_, 1);
}

bool ScriptEngine::RequireModule(const String& module_name) {
    if (!L_) return false;

    lua_getglobal(L_, "require");
    lua_pushstring(L_, module_name.c_str());

    if (lua_pcall(L_, 1, 1, 0) != LUA_OK) {
        last_error_ = lua_tostring(L_, -1);
        lua_pop(L_, 1);
        DRX_LOG_ERROR("Failed to require module {}: {}", module_name, last_error_);
        return false;
    }

    lua_pop(L_, 1);
    return true;
}

SharedPtr<LuaScript> ScriptEngine::CreateScript(const String& path) {
    auto script = MakeShared<LuaScript>(L_, path);
    if (script->Load()) {
        scripts_.push_back(script);
        return script;
    }
    return nullptr;
}

void ScriptEngine::DestroyScript(SharedPtr<LuaScript> script) {
    auto it = std::find(scripts_.begin(), scripts_.end(), script);
    if (it != scripts_.end()) {
        scripts_.erase(it);
    }
}

void ScriptEngine::RegisterFunction(const String& name, ScriptFunction func) {
    registered_functions_[name] = func;

    // Create C closure
    lua_pushlightuserdata(L_, &registered_functions_[name]);
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        auto* func_ptr = static_cast<ScriptFunction*>(lua_touserdata(L, lua_upvalueindex(1)));

        Array<ScriptValue> args;
        int nargs = lua_gettop(L);
        for (int i = 1; i <= nargs; i++) {
            int type = lua_type(L, i);
            switch (type) {
                case LUA_TBOOLEAN:
                    args.push_back(ScriptValue(lua_toboolean(L, i) != 0));
                    break;
                case LUA_TNUMBER:
                    args.push_back(ScriptValue(lua_tonumber(L, i)));
                    break;
                case LUA_TSTRING:
                    args.push_back(ScriptValue(lua_tostring(L, i)));
                    break;
                default:
                    args.push_back(ScriptValue());
                    break;
            }
        }

        Array<ScriptValue> results = (*func_ptr)(args);

        for (const auto& result : results) {
            switch (result.GetType()) {
                case ScriptValueType::Boolean:
                    lua_pushboolean(L, result.AsBool());
                    break;
                case ScriptValueType::Number:
                    lua_pushnumber(L, result.AsNumber());
                    break;
                case ScriptValueType::String:
                    lua_pushstring(L, result.AsString().c_str());
                    break;
                default:
                    lua_pushnil(L);
                    break;
            }
        }

        return static_cast<int>(results.size());
    }, 1);

    lua_setglobal(L_, name.c_str());
}

void ScriptEngine::RegisterEngineAPI() {
    // Register engine-specific functions here
    RegisterFunction("Log", [](const Array<ScriptValue>& args) -> Array<ScriptValue> {
        if (!args.empty()) {
            DRX_LOG_INFO("[Lua] {}", args[0].AsString());
        }
        return {};
    });
}

int ScriptEngine::LuaErrorHandler(lua_State* L) {
    const char* msg = lua_tostring(L, -1);
    DRX_LOG_ERROR("Lua panic: {}", msg ? msg : "unknown error");
    return 0;
}

int ScriptEngine::LuaPrint(lua_State* L) {
    int n = lua_gettop(L);
    String output;

    for (int i = 1; i <= n; i++) {
        if (i > 1) output += "\t";

        if (lua_isstring(L, i)) {
            output += lua_tostring(L, i);
        } else if (lua_isboolean(L, i)) {
            output += lua_toboolean(L, i) ? "true" : "false";
        } else if (lua_isnil(L, i)) {
            output += "nil";
        } else {
            output += lua_typename(L, lua_type(L, i));
        }
    }

    DRX_LOG_INFO("[Lua] {}", output);
    return 0;
}

// ============================================================================
// ScriptComponent Implementation
// ============================================================================

ScriptComponent::~ScriptComponent() {
    RemoveAllScripts();
}

bool ScriptComponent::AddScript(const String& path) {
    auto& engine = ScriptEngine::Instance();
    auto script = engine.CreateScript(path);
    if (script) {
        script->SetEntity(entity_);
        scripts_.push_back(script);
        return true;
    }
    return false;
}

void ScriptComponent::RemoveScript(const String& path) {
    auto it = std::find_if(scripts_.begin(), scripts_.end(),
        [&path](const SharedPtr<LuaScript>& s) { return s->GetPath() == path; });

    if (it != scripts_.end()) {
        ScriptEngine::Instance().DestroyScript(*it);
        scripts_.erase(it);
    }
}

void ScriptComponent::RemoveAllScripts() {
    for (auto& script : scripts_) {
        ScriptEngine::Instance().DestroyScript(script);
    }
    scripts_.clear();
}

void ScriptComponent::OnCreate() {
    for (auto& script : scripts_) script->OnCreate();
}

void ScriptComponent::OnDestroy() {
    for (auto& script : scripts_) script->OnDestroy();
}

void ScriptComponent::OnUpdate(float32 delta_time) {
    for (auto& script : scripts_) script->OnUpdate(delta_time);
}

void ScriptComponent::OnFixedUpdate(float32 fixed_delta_time) {
    for (auto& script : scripts_) script->OnFixedUpdate(fixed_delta_time);
}

void ScriptComponent::OnCollisionEnter(void* other) {
    for (auto& script : scripts_) script->OnCollisionEnter(other);
}

void ScriptComponent::OnCollisionStay(void* other) {
    for (auto& script : scripts_) script->OnCollisionStay(other);
}

void ScriptComponent::OnCollisionExit(void* other) {
    for (auto& script : scripts_) script->OnCollisionExit(other);
}

void ScriptComponent::OnTriggerEnter(void* other) {
    for (auto& script : scripts_) script->OnTriggerEnter(other);
}

void ScriptComponent::OnTriggerExit(void* other) {
    for (auto& script : scripts_) script->OnTriggerExit(other);
}

SharedPtr<LuaScript> ScriptComponent::GetScript(const String& path) const {
    auto it = std::find_if(scripts_.begin(), scripts_.end(),
        [&path](const SharedPtr<LuaScript>& s) { return s->GetPath() == path; });
    return it != scripts_.end() ? *it : nullptr;
}

void ScriptComponent::SetEntity(void* entity) {
    entity_ = entity;
    for (auto& script : scripts_) {
        script->SetEntity(entity);
    }
}

} // namespace DRX::Scripting
