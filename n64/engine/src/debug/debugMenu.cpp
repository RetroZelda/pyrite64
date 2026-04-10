#include "debug/debugMenu.h"
#include "lib/hash.h"
#include "eeprom.h"

#include <stdint.h>
#include <unordered_map>

namespace Debug::Menu
{

    // used to verify the data exists
    constexpr uint32_t DATA_HEADER = P64::Lib::Hash::fnv32("MENU VARS");

    static size_t automaticSaveOffset = -1; // >0 wil lauto save
    static int32_t totalSizeCache = -1;


    const std::vector<RegisteredDebugVar>& DebugRegistry::GetAllVars() const
    {
        return m_vars;
    }

    std::vector<RegisteredDebugVar>& DebugRegistry::GetAllVars()
    {
        return m_vars;
    }

    // Called by the registrar (you don't call this directly)
    void DebugRegistry::Register(const char* path, void* ptr, DebugVarType type, MenuValue step)
    {
        m_vars.push_back({ P64::Lib::Hash::fnv32(path), path, ptr, type, step });
    }

    int32_t enableAutomaticSaveVariables(bool enable, size_t desiredOffset)
    {
        automaticSaveOffset = (enable && eeprom_present() != EEPROM_NONE) ? desiredOffset : -1;
        return getVariableSaveByteSize();
    }

    int32_t getAutomaticSaveOffset()
    {
        return automaticSaveOffset;
    }

    int32_t getVariableSaveByteSize()
    {
#if DEBUG_MENU_ENABLED
        if(totalSizeCache < 0)
        {
            totalSizeCache = 0;

            for (const RegisteredDebugVar& var : Debug::Menu::DebugRegistry::Get().GetAllVars())
            {
                #define X(Type, Name) case Debug::Menu::DebugVarType::Name: totalSizeCache += sizeof(Type); break;
                switch(var.type)
                {
                    DEBUG_MENU_TYPES
                    default: break;
                }
                #undef X
            }
            totalSizeCache += Debug::Menu::DebugRegistry::Get().GetAllVars().size() * sizeof(uint32_t); // count the hashes
            totalSizeCache +=  sizeof(uint32_t) * 2; // count the header and the size data
        }
        return totalSizeCache;
#else
        return 0;
#endif // DEBUG_MENU_ENABLED
    }

    int32_t saveVariables(size_t offset)
    {
#if DEBUG_MENU_ENABLED
        eeprom_write_bytes(&DATA_HEADER, offset, sizeof(uint32_t));
        int32_t writtenDataCount = offset + sizeof(uint32_t) * 2; // *2 so we can write the total size of the data we wrote
        for (const RegisteredDebugVar& var : Debug::Menu::DebugRegistry::Get().GetAllVars())
        {            
            #define X(Type, Name) \
                case Debug::Menu::DebugVarType::Name: {\
                    eeprom_write_bytes(&var.pathHash, writtenDataCount, sizeof(uint32_t)); \
                    eeprom_write_bytes(var.ptr, writtenDataCount + sizeof(uint32_t), sizeof(Type)); \
                    writtenDataCount += sizeof(uint32_t) + sizeof(Type); \
                } break;
            switch(var.type)
            {
                DEBUG_MENU_TYPES
                default: break;
            }
            #undef X
        }

        // writes the data count after the DATA_HEADER hash value
        writtenDataCount -= offset;
        eeprom_write_bytes(&writtenDataCount, sizeof(uint32_t), sizeof(uint32_t));
        return writtenDataCount;
#else
        return 0;
#endif // DEBUG_MENU_ENABLED
    }

    int32_t loadVariables(size_t offset)
    {
#if DEBUG_MENU_ENABLED
        uint32_t headerValue = 0;
        int32_t dataToRead = 0;
        eeprom_read_bytes(&headerValue, offset, sizeof(uint32_t));
        if(headerValue != DATA_HEADER)
        {
            // nothing exists to read
            return -1;
        }

        eeprom_read_bytes(&dataToRead, sizeof(uint32_t), sizeof(int32_t));
        int32_t readDataCount = offset + sizeof(uint32_t) * 2;

        // place all the vars into an unordered map so we can more quickly write hte values that we discover
        // we are going to be getting tons of cache misses anyway, so allocating this new chunk of data is probably fine
        std::vector<RegisteredDebugVar>& allVars = Debug::Menu::DebugRegistry::Get().GetAllVars();
        std::unordered_map<uint32_t, RegisteredDebugVar*> varMap;
        varMap.reserve(allVars.size());
        for(RegisteredDebugVar& var : allVars)
        {
            varMap.emplace(var.pathHash, &var);
        }
        
        // we can reuse headerValue to read in the path hash to find our data
        int result = 0;
        dataToRead += offset;
        while(readDataCount < dataToRead)
        {
            eeprom_read_bytes(&headerValue, readDataCount, sizeof(uint32_t));
            readDataCount += sizeof(uint32_t);

            RegisteredDebugVar* foundVar = varMap.at(headerValue);
            #define X(Type, Name) \
                case Debug::Menu::DebugVarType::Name: {\
                    eeprom_read_bytes(foundVar->ptr, readDataCount, sizeof(Type));\
                    readDataCount += sizeof(Type);\
                } break;
            switch(foundVar->type)
            {
                DEBUG_MENU_TYPES
                default: break;
            }
            #undef X
        }


        return readDataCount - offset;
#else
        return 0;
#endif // DEBUG_MENU_ENABLED
    }

};