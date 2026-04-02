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
        uint32_t        pathHash; // cache of the path hash
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

        std::vector<RegisteredDebugVar>& GetAllVars();
        const std::vector<RegisteredDebugVar>& GetAllVars() const;

        // Called by the registrar (you don't call this directly)
        void Register(const char* path, void* ptr, DebugVarType type, MenuValue step);

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

    /**
     * @brief Enable or disable automatica saving of the variables to eeprom
     * 
     * This function sets the desire to automatically save the state of the variables.
     * The automatic saving and loading will be handled inside overlay.cpp when a variable changes
     * 
     * @param enable if true will automatically save the variables to eeprom.
     * @return int Number of bytes that the variables will consume into eeprom.
     */
    int32_t enableAutomaticSaveVariables(bool enable, size_t desiredOffset);

    /**
     * @brief Get the offset into eeprom for automatic saving the variables
     *  
     * @return int Number of bytes that the variables will consume into eeprom.
     */
    int32_t getAutomaticSaveOffset();

    /**
     * @brief Get the number of bytes that the variables will consume in eeprom
     *  
     * @return int Number of bytes that the variables will consume into eeprom.  <0 if disabled
     */
    int32_t getVariableSaveByteSize();

    /**
     * @brief Write variables into eeprom
     * 
     * This function writes the variable data to the eeprom in the cartridge to the provided offset
     * and will return the number of bytes written, or negative value in case of error.
     * 
     * @param offset Offset in eeprom to write to. Allowed range is 0 to 0x7FFF.
     * @return int Number of bytes written, or negative value in case of error.
     */
    int32_t saveVariables(size_t offset);

    /**
     * @brief Read variables from eeprom
     * 
     * This function reads the variables from the eeprom in the cartridge from the provided offset
     * and will return the number of bytes read, or nagiative value in case of error
     * 
     * @param offset Offset in eeprom to read from. Allowed range is 0 to 0x7FFF.
     * @return int Number of bytes read, or negative value in case of error.
     */
    int32_t loadVariables(size_t offset);
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