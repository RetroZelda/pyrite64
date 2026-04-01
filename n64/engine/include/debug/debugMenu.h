#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <variant> 

// TODO: This should probably get tied to a __DEBUG variable via the makefile
#define DEBUG_MENU_ENABLED 1

#if DEBUG_MENU_ENABLED
    #define TWEAKABLE static
#else
    #define TWEAKABLE constexpr static
#endif

namespace Debug::Menu
{
    #define DEBUG_MENU_TYPES \
        X(bool, Boolean) \
        X(int8_t, Int8) \
        X(int16_t, Int16) \
        X(int32_t, Int32) \
        X(int64_t, Int64) \
        X(uint8_t, Uint8) \
        X(uint16_t, Uint16) \
        X(uint32_t, Uint32) \
        X(uint64_t, Uint64) \
        X(float, Float) \
        X(double, Double)

    #define X(Type, Name) Name,
    enum class DebugVarType : uint8_t
    {
        DEBUG_MENU_TYPES
        INVALID
    };
    #undef X

    #define X(Type, Name) Type,
    using MenuValue = std::variant<DEBUG_MENU_TYPES std::monostate>;
    #undef X

    struct RegisteredDebugVar
    {
        const char*     path; // e.g. path separated with | (e.g: "Enemy|MovementSpeed")
        void*           ptr;
        DebugVarType    type;
        MenuValue       step;
    };

    class DebugRegistry
    {
    public:
        static DebugRegistry& Get()
        {
            static DebugRegistry instance;
            return instance;
        }

        const std::vector<RegisteredDebugVar>& GetAllVars() const
        {
            return m_vars;
        }

        // Called by the registrar (you don't call this directly)
        void Register(const char* path, void* ptr, DebugVarType type, MenuValue step)
        {
            m_vars.push_back({ path, ptr, type, step });
        }

    private:
        std::vector<RegisteredDebugVar> m_vars;
    };

    template<typename T> DebugVarType GetDebugVarType()
    {
        static_assert(false, "Unsupported debug variable type");
        return DebugVarType::INVALID;
    }

    #define X(Type, Name) template<> inline DebugVarType GetDebugVarType<Type>() { return DebugVarType::Name; }
    DEBUG_MENU_TYPES
    #undef X
    
    template<typename T>
    struct DebugRegistrar
    {
        DebugRegistrar(const char* path, T* varPtr, T stepSize = T(1))
        {
            DebugRegistry::Get().Register(path, varPtr, GetDebugVarType<T>(), stepSize);
        }
    };
};

#if DEBUG_MENU_ENABLED
    #define REGISTER_TWEAKABLE_VAR_STEP(Type, Path, Name, DefaultValue, StepSize) \
        TWEAKABLE Type Name = (DefaultValue); \
        static Debug::Menu::DebugRegistrar<Type> debug_registrar_##Name(Path, &Name, StepSize);
#else
    #define REGISTER_TWEAKABLE_VAR_STEP(Type, Path, Name, DefaultValue, StepSize) \
        TWEAKABLE Type Name = (DefaultValue);
#endif

#define REGISTER_TWEAKABLE_VAR(Type, Path, Name, DefaultValue) REGISTER_TWEAKABLE_VAR_STEP(Type, Path, Name, DefaultValue, Type(1))