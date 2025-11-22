/**
 * DRX Engine - Scripting System
 * Lua scripting integration for game logic
 */

#pragma once

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Math/Vector3.h"
#include "Core/Public/Math/Quaternion.h"

// Forward declare Lua types
struct lua_State;

namespace DRX::Scripting {

/**
 * Script value types
 */
enum class ScriptValueType : uint32 {
    Nil,
    Boolean,
    Number,
    String,
    Table,
    Function,
    UserData,
    Thread
};

/**
 * Script value - variant type for Lua values
 */
class ScriptValue {
public:
    ScriptValue();
    ScriptValue(bool value);
    ScriptValue(int32 value);
    ScriptValue(float64 value);
    ScriptValue(const String& value);
    ScriptValue(const char* value);
    ~ScriptValue();

    ScriptValue(const ScriptValue& other);
    ScriptValue(ScriptValue&& other) noexcept;
    ScriptValue& operator=(const ScriptValue& other);
    ScriptValue& operator=(ScriptValue&& other) noexcept;

    ScriptValueType GetType() const { return type_; }
    bool IsNil() const { return type_ == ScriptValueType::Nil; }
    bool IsBool() const { return type_ == ScriptValueType::Boolean; }
    bool IsNumber() const { return type_ == ScriptValueType::Number; }
    bool IsString() const { return type_ == ScriptValueType::String; }
    bool IsTable() const { return type_ == ScriptValueType::Table; }
    bool IsFunction() const { return type_ == ScriptValueType::Function; }

    bool AsBool() const;
    int32 AsInt() const;
    float64 AsNumber() const;
    String AsString() const;

    // Table operations (if type is Table)
    ScriptValue Get(const String& key) const;
    void Set(const String& key, const ScriptValue& value);
    Array<String> GetKeys() const;

private:
    ScriptValueType type_ = ScriptValueType::Nil;
    union {
        bool bool_value;
        float64 number_value;
    };
    String string_value_;
    HashMap<String, SharedPtr<ScriptValue>> table_value_;
};

/**
 * Script function binding
 */
using ScriptFunction = std::function<Array<ScriptValue>(const Array<ScriptValue>&)>;

/**
 * Script class for component scripts
 */
class Script {
public:
    Script() = default;
    virtual ~Script() = default;

    virtual void OnCreate() {}
    virtual void OnDestroy() {}
    virtual void OnUpdate(float32 delta_time) {}
    virtual void OnFixedUpdate(float32 fixed_delta_time) {}
    virtual void OnEnable() {}
    virtual void OnDisable() {}

    // Collision events
    virtual void OnCollisionEnter(void* other) {}
    virtual void OnCollisionStay(void* other) {}
    virtual void OnCollisionExit(void* other) {}
    virtual void OnTriggerEnter(void* other) {}
    virtual void OnTriggerExit(void* other) {}

    void* GetEntity() const { return entity_; }
    void SetEntity(void* entity) { entity_ = entity; }

protected:
    void* entity_ = nullptr;
};

/**
 * Lua Script instance
 */
class LuaScript : public Script {
public:
    LuaScript(lua_State* L, const String& script_path);
    ~LuaScript() override;

    bool Load();
    bool Reload();

    void OnCreate() override;
    void OnDestroy() override;
    void OnUpdate(float32 delta_time) override;
    void OnFixedUpdate(float32 fixed_delta_time) override;

    void OnCollisionEnter(void* other) override;
    void OnCollisionStay(void* other) override;
    void OnCollisionExit(void* other) override;
    void OnTriggerEnter(void* other) override;
    void OnTriggerExit(void* other) override;

    // Call script function
    Array<ScriptValue> CallFunction(const String& name, const Array<ScriptValue>& args = {});
    bool HasFunction(const String& name) const;

    // Get/Set script variables
    ScriptValue GetVariable(const String& name) const;
    void SetVariable(const String& name, const ScriptValue& value);

    const String& GetPath() const { return script_path_; }

private:
    void CallMethod(const String& name);
    void PushValue(const ScriptValue& value);
    ScriptValue PopValue();

    lua_State* L_ = nullptr;
    String script_path_;
    int32 script_ref_ = -1;
    bool loaded_ = false;
};

/**
 * Script Engine - Lua VM wrapper
 */
class ScriptEngine {
public:
    static ScriptEngine& Instance();

    bool Initialize();
    void Shutdown();
    void Update(float32 delta_time);

    // Script execution
    bool ExecuteString(const String& code);
    bool ExecuteFile(const String& path);
    Array<ScriptValue> CallGlobalFunction(const String& name, const Array<ScriptValue>& args = {});

    // Global variables
    void SetGlobal(const String& name, const ScriptValue& value);
    ScriptValue GetGlobal(const String& name) const;

    // Module loading
    void AddSearchPath(const String& path);
    bool RequireModule(const String& module_name);

    // Script creation
    SharedPtr<LuaScript> CreateScript(const String& path);
    void DestroyScript(SharedPtr<LuaScript> script);

    // C++ function binding
    void RegisterFunction(const String& name, ScriptFunction func);
    void RegisterFunction(const String& table, const String& name, ScriptFunction func);

    // Class binding
    template<typename T>
    void RegisterClass(const String& name);

    template<typename T, typename... Args>
    void RegisterConstructor();

    template<typename T, typename R, typename... Args>
    void RegisterMethod(const String& class_name, const String& method_name, R(T::*method)(Args...));

    template<typename T, typename V>
    void RegisterProperty(const String& class_name, const String& prop_name,
                          V T::*member, bool readonly = false);

    // Error handling
    String GetLastError() const { return last_error_; }
    void SetErrorHandler(std::function<void(const String&)> handler) { error_handler_ = handler; }

    // Debug
    void EnableDebug(bool enable);
    Array<String> GetCallStack() const;
    void SetBreakpoint(const String& file, int32 line);
    void RemoveBreakpoint(const String& file, int32 line);

    lua_State* GetState() const { return L_; }

private:
    ScriptEngine() = default;
    ~ScriptEngine() = default;

    void RegisterEngineAPI();
    void RegisterMathAPI();
    void RegisterInputAPI();
    void RegisterEntityAPI();
    void RegisterPhysicsAPI();
    void RegisterAudioAPI();

    static int LuaErrorHandler(lua_State* L);
    static int LuaPrint(lua_State* L);

    lua_State* L_ = nullptr;
    String last_error_;
    std::function<void(const String&)> error_handler_;

    Array<SharedPtr<LuaScript>> scripts_;
    HashMap<String, ScriptFunction> registered_functions_;

    bool debug_enabled_ = false;
    HashMap<String, Array<int32>> breakpoints_;

    bool initialized_ = false;
};

/**
 * Script Component - attaches scripts to entities
 */
class ScriptComponent {
public:
    ScriptComponent() = default;
    ~ScriptComponent();

    bool AddScript(const String& path);
    void RemoveScript(const String& path);
    void RemoveAllScripts();

    void OnCreate();
    void OnDestroy();
    void OnUpdate(float32 delta_time);
    void OnFixedUpdate(float32 fixed_delta_time);

    void OnCollisionEnter(void* other);
    void OnCollisionStay(void* other);
    void OnCollisionExit(void* other);
    void OnTriggerEnter(void* other);
    void OnTriggerExit(void* other);

    SharedPtr<LuaScript> GetScript(const String& path) const;
    Array<SharedPtr<LuaScript>> GetAllScripts() const { return scripts_; }

    void SetEntity(void* entity);

private:
    Array<SharedPtr<LuaScript>> scripts_;
    void* entity_ = nullptr;
};

// Template implementations would go in a .inl file
// Here's a simplified version:

template<typename T>
void ScriptEngine::RegisterClass(const String& name) {
    // Create metatable for the class
    luaL_newmetatable(L_, name.c_str());

    // Set __index to itself
    lua_pushvalue(L_, -1);
    lua_setfield(L_, -2, "__index");

    // Register __gc for destructor
    lua_pushcfunction(L_, [](lua_State* L) -> int {
        T* obj = *static_cast<T**>(lua_touserdata(L, 1));
        delete obj;
        return 0;
    });
    lua_setfield(L_, -2, "__gc");

    lua_pop(L_, 1);
}

} // namespace DRX::Scripting
