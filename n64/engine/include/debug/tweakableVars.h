#pragma once

#include <vector>
#include <string>
#include <cstdint>

#include "debug/menu.h"   // contains MenuItemType

// TODO: Tie to __DEBUG via makefile
#define DEBUG_MENU_ENABLED 1

#if DEBUG_MENU_ENABLED
    #define TWEAKABLE static
    #define TWEAKABLE_MEMBER_REF(Type) Type&
#else
    #define TWEAKABLE constexpr static
    #define TWEAKABLE_MEMBER_REF(Type) const Type
#endif

namespace P64::Debug::TweakableVars
{
    using MenuItemType = P64::Debug::MenuItemType;

    struct RegisteredDebugVar
    {
        uint32_t        pathHash;
        const char*     path;
        void*           ptr;
        MenuItemType    type;
        float           step = 1.0f;
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

        void Register(const char* path, void* ptr, MenuItemType type, float step = 1.0f);

    private:
        std::vector<RegisteredDebugVar> m_vars;
    };

    // Type → MenuItemType mapping (no X macro)
    template<typename T> MenuItemType GetMenuItemType();

    template<> inline MenuItemType GetMenuItemType<bool>()      { return MenuItemType::BOOL; }
    template<> inline MenuItemType GetMenuItemType<uint8_t>()   { return MenuItemType::U8; }
    template<> inline MenuItemType GetMenuItemType<uint16_t>()  { return MenuItemType::U16; }
    template<> inline MenuItemType GetMenuItemType<uint32_t>()  { return MenuItemType::U32; }
    template<> inline MenuItemType GetMenuItemType<float>()     { return MenuItemType::F32; }

    template<typename T>
    struct DebugRegistrar
    {
        DebugRegistrar(const char* path, T* varPtr, float stepSize = 1.0f)
        {
            DebugRegistry::Get().Register(path, varPtr, GetMenuItemType<T>(), stepSize);
        }
    };

    /**
     * @brief builds the debug menu hierarchy for all variables and returns the menu to be added into the overlay
     * 
     * Uses the new P64::Debug::Menu + fluent add() API and automatically creates nested submenus from | paths.
     * 
     * @return Menu object ready to get added into the overlay debug menu (call root.update() / root.draw()).
     */
    void buildMenu(const char* rootMenuName);

    /**
     * @brief Enable or disable automatic saving of the variables to eeprom
     */
    int32_t enableAutomaticSaveVariables(bool enable, size_t desiredOffset);

    /**
     * @brief Get the offset into eeprom for automatic saving the variables
     */
    int32_t getAutomaticSaveOffset();

    /**
     * @brief Get the number of bytes that the variables will consume in eeprom
     */
    int32_t getVariableSaveByteSize();

    /**
     * @brief Write variables into eeprom
     */
    int32_t saveVariables(size_t offset);

    /**
     * @brief Read variables from eeprom
     */
    int32_t loadVariables(size_t offset);
};

#if DEBUG_MENU_ENABLED
    #define REGISTER_TWEAKABLE_VAR_STEP(Type, Path, Name, DefaultValue, StepSize) \
        TWEAKABLE Type Name = (DefaultValue); \
        static P64::Debug::TweakableVars::DebugRegistrar<Type> debug_registrar_##Name(Path, &Name, static_cast<float>(StepSize));
#else
    #define REGISTER_TWEAKABLE_VAR_STEP(Type, Path, Name, DefaultValue, StepSize) \
        TWEAKABLE Type Name = (DefaultValue);
#endif

#define REGISTER_TWEAKABLE_VAR(Type, Path, Name, DefaultValue) \
        REGISTER_TWEAKABLE_VAR_STEP(Type, Path, Name, DefaultValue, static_cast<Type>(1))
