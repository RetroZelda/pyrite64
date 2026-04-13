#include "debug/tweakableVars.h"
#include "debug/debugMenu.h" 

#include "lib/hash.h"
#include "eeprom.h"

#include <unordered_map>
#include <string>
#include <vector>

namespace P64::Debug::TweakableVars
{
    // These two pools live for the entire lifetime of the program
    static std::vector<Menu> submenuPool;
    static std::vector<std::string> menuStringPool;

    // Returns a pointer to a string that will never be destroyed
    static const char* internString(const std::string& str)
    {
        menuStringPool.push_back(str);
        return menuStringPool.back().c_str();
    }

    // Recursive helper that creates the whole chain of submenus on demand
    static Menu* ensureMenu(const std::string& prefix,
                            std::unordered_map<std::string, Menu*>& pathToMenu)
    {
        std::unordered_map<std::string, Menu*>::iterator it = pathToMenu.find(prefix);
        if (it != pathToMenu.end())
        {
            return it->second;
        }

        submenuPool.emplace_back();
        Menu* newMenu = &submenuPool.back();

        if (!prefix.empty())
        {
            size_t lastPipe = prefix.find_last_of('|');
            std::string parentPrefix = (lastPipe == std::string::npos) ? "" : prefix.substr(0, lastPipe);
            std::string name         = (lastPipe == std::string::npos) ? prefix : prefix.substr(lastPipe + 1);

            Menu* parent = ensureMenu(parentPrefix, pathToMenu);

            // Use interned string so the submenu name never becomes invalid
            const char* safeName = internString(name);
            parent->add(safeName, *newMenu);
        }

        pathToMenu[prefix] = newMenu;
        return newMenu;
    }
    
    // =====================================================================
    // buildMenu() - uses new Menu fluent API + MenuItemType directly
    // =====================================================================
    void buildMenu(const char* rootMenuName)
    {
        Menu& root = Debug::Overlay::addCustomMenu(rootMenuName);
        const std::vector<RegisteredDebugVar>& allVars = DebugRegistry::Get().GetAllVars();

        // Clear both pools so we start fresh every time the menu is rebuilt
        submenuPool.clear();
        menuStringPool.clear();

        // Rough estimate: we will need at least one string per variable + submenus
        submenuPool.reserve(allVars.size());
        menuStringPool.reserve(allVars.size() * 2);

        // Quick lookup: full prefix path → Menu*
        std::unordered_map<std::string, Menu*> pathToMenu;
        pathToMenu[""] = &root;

        for (size_t i = 0; i < allVars.size(); ++i)
        {
            const RegisteredDebugVar& var = allVars[i];

            std::string fullPath = var.path;
            size_t lastPipe = fullPath.find_last_of('|');
            std::string submenuPrefix = (lastPipe == std::string::npos) ? "" : fullPath.substr(0, lastPipe);
            std::string displayName   = (lastPipe == std::string::npos) ? fullPath : fullPath.substr(lastPipe + 1);

            Menu* target = ensureMenu(submenuPrefix, pathToMenu);

            // Use interned string so the display name never becomes invalid
            const char* safeDisplayName = internString(displayName);

            switch (var.type)
            {
            case MenuItemType::BOOL:
                target->add(safeDisplayName, *static_cast<bool*>(var.ptr));
                break;

            case MenuItemType::U8:
                target->add(safeDisplayName, *static_cast<uint8_t*>(var.ptr),
                            0, 255, static_cast<uint8_t>(var.step));
                break;

            case MenuItemType::U16:
                target->add(safeDisplayName, *static_cast<uint16_t*>(var.ptr),
                            0, 65535, static_cast<uint16_t>(var.step));
                break;

            case MenuItemType::U32:
                target->add(safeDisplayName, *static_cast<uint32_t*>(var.ptr),
                            0, 0xFFFF, static_cast<uint32_t>(var.step));
                break;

            case MenuItemType::F32:
                target->add(safeDisplayName, *static_cast<float*>(var.ptr),
                            -1000000.0f, 1000000.0f, var.step);
                break;

            default:
                // unsupported type (signed ints, 64-bit, double etc.) - skip
                break;
            }
        }
    }

    // =====================================================================
    // Registry
    // =====================================================================
    const std::vector<RegisteredDebugVar>& DebugRegistry::GetAllVars() const { return m_vars; }
    std::vector<RegisteredDebugVar>& DebugRegistry::GetAllVars() { return m_vars; }

    void DebugRegistry::Register(const char* path, void* ptr, MenuItemType type, float step)
    {
        m_vars.push_back({P64::Lib::Hash::fnv32(path), path, ptr, type, step});
    }

    // =====================================================================
    // EEPROM helpers (no X macros anywhere)
    // =====================================================================
    static size_t automaticSaveOffset = static_cast<size_t>(-1);
    static int32_t totalSizeCache = -1;

    int32_t enableAutomaticSaveVariables(bool enable, size_t desiredOffset)
    {
        automaticSaveOffset = (enable && eeprom_present() != EEPROM_NONE) ? desiredOffset : static_cast<size_t>(-1);
        return getVariableSaveByteSize();
    }

    int32_t getAutomaticSaveOffset()
    {
        return static_cast<int32_t>(automaticSaveOffset);
    }

    int32_t getVariableSaveByteSize()
    {
#if DEBUG_MENU_ENABLED
        if (totalSizeCache < 0)
        {
            totalSizeCache = 0;

            const std::vector<RegisteredDebugVar>& allVars = DebugRegistry::Get().GetAllVars();

            for (size_t i = 0; i < allVars.size(); ++i)
            {
                const RegisteredDebugVar& var = allVars[i];

                switch (var.type)
                {
                case MenuItemType::BOOL:  totalSizeCache += sizeof(bool);     break;
                case MenuItemType::U8:    totalSizeCache += sizeof(uint8_t);  break;
                case MenuItemType::U16:   totalSizeCache += sizeof(uint16_t); break;
                case MenuItemType::U32:   totalSizeCache += sizeof(uint32_t); break;
                case MenuItemType::F32:   totalSizeCache += sizeof(float);    break;
                default: break;
                }
            }
            totalSizeCache += static_cast<int32_t>(allVars.size() * sizeof(uint32_t)); // hashes
            totalSizeCache += sizeof(uint32_t) * 2; // header + size
        }
        return totalSizeCache;
#else
        return 0;
#endif
    }

    int32_t saveVariables(size_t offset)
    {
#if DEBUG_MENU_ENABLED
        constexpr uint32_t DATA_HEADER = Lib::Hash::fnv32("MENU VARS");

        eeprom_write_bytes(&DATA_HEADER, offset, sizeof(uint32_t));

        int32_t written = static_cast<int32_t>(offset) + static_cast<int32_t>(sizeof(uint32_t) * 2);

        const std::vector<RegisteredDebugVar>& allVars = DebugRegistry::Get().GetAllVars();

        for (size_t i = 0; i < allVars.size(); ++i)
        {
            const RegisteredDebugVar& var = allVars[i];

            switch (var.type)
            {
            case MenuItemType::BOOL:
                eeprom_write_bytes(&var.pathHash, written, sizeof(uint32_t));
                eeprom_write_bytes(var.ptr, written + sizeof(uint32_t), sizeof(bool));
                written += sizeof(uint32_t) + sizeof(bool);
                break;
            case MenuItemType::U8:
                eeprom_write_bytes(&var.pathHash, written, sizeof(uint32_t));
                eeprom_write_bytes(var.ptr, written + sizeof(uint32_t), sizeof(uint8_t));
                written += sizeof(uint32_t) + sizeof(uint8_t);
                break;
            case MenuItemType::U16:
                eeprom_write_bytes(&var.pathHash, written, sizeof(uint32_t));
                eeprom_write_bytes(var.ptr, written + sizeof(uint32_t), sizeof(uint16_t));
                written += sizeof(uint32_t) + sizeof(uint16_t);
                break;
            case MenuItemType::U32:
                eeprom_write_bytes(&var.pathHash, written, sizeof(uint32_t));
                eeprom_write_bytes(var.ptr, written + sizeof(uint32_t), sizeof(uint32_t));
                written += sizeof(uint32_t) + sizeof(uint32_t);
                break;
            case MenuItemType::F32:
                eeprom_write_bytes(&var.pathHash, written, sizeof(uint32_t));
                eeprom_write_bytes(var.ptr, written + sizeof(uint32_t), sizeof(float));
                written += sizeof(uint32_t) + sizeof(float);
                break;
            default:
                break;
            }
        }

        int32_t totalWritten = written - static_cast<int32_t>(offset);
        eeprom_write_bytes(&totalWritten, offset + sizeof(uint32_t), sizeof(int32_t));
        return totalWritten;
#else
        return 0;
#endif
    }

    int32_t loadVariables(size_t offset)
    {
#if DEBUG_MENU_ENABLED
        uint32_t header = 0;
        eeprom_read_bytes(&header, offset, sizeof(uint32_t));

        constexpr uint32_t DATA_HEADER = Lib::Hash::fnv32("MENU VARS");
        if (header != DATA_HEADER)
            return -1;

        int32_t dataSize = 0;
        eeprom_read_bytes(&dataSize, offset + sizeof(uint32_t), sizeof(int32_t));

        int32_t readPos = static_cast<int32_t>(offset) + static_cast<int32_t>(sizeof(uint32_t) * 2);

        std::vector<RegisteredDebugVar>& allVars = DebugRegistry::Get().GetAllVars();

        std::unordered_map<uint32_t, RegisteredDebugVar*> varMap;
        varMap.reserve(allVars.size());

        for (size_t i = 0; i < allVars.size(); ++i)
        {
            varMap[allVars[i].pathHash] = &allVars[i];
        }

        while (readPos < static_cast<int32_t>(offset) + dataSize)
        {
            uint32_t hash = 0;
            eeprom_read_bytes(&hash, readPos, sizeof(uint32_t));
            readPos += sizeof(uint32_t);

            std::unordered_map<uint32_t, RegisteredDebugVar*>::iterator it = varMap.find(hash);
            if (it != varMap.end())
            {
                RegisteredDebugVar* var = it->second;

                switch (var->type)
                {
                case MenuItemType::BOOL:  
                    eeprom_read_bytes(var->ptr, readPos, sizeof(bool));  
                    readPos += sizeof(bool); 
                    break;
                case MenuItemType::U8:    
                    eeprom_read_bytes(var->ptr, readPos, sizeof(uint8_t)); 
                    readPos += sizeof(uint8_t); 
                    break;
                case MenuItemType::U16:   
                    eeprom_read_bytes(var->ptr, readPos, sizeof(uint16_t)); 
                    readPos += sizeof(uint16_t); 
                    break;
                case MenuItemType::U32:   
                    eeprom_read_bytes(var->ptr, readPos, sizeof(uint32_t)); 
                    readPos += sizeof(uint32_t); 
                    break;
                case MenuItemType::F32:   
                    eeprom_read_bytes(var->ptr, readPos, sizeof(float)); 
                    readPos += sizeof(float); 
                    break;
                default: 
                    break;
                }
            }
            else
            {
                // unknown var - we can't know the size, so we stop (data is still consistent for known vars)
                break;
            }
        }
        return dataSize;
#else
        return 0;
#endif
    }
}