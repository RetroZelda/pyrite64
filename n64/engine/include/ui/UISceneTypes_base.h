#pragma once
// This file must be included AFTER UI_SCENE_TYPES X-macro is defined.
// It generates the UISceneType enum, UISceneTraits specializations, and GetSceneName.
// Included automatically by the generated p64/UISceneTypes.h.

#include <cstdint>

namespace P64::UI
{
    #define X(name, data) name,
    enum class UISceneType : uint8_t
    {
        UI_SCENE_TYPES
        Count
    };
    #undef X

    template<UISceneType Scene>
    struct UISceneTraits;

    #define X(name, data) \
        template<> \
        struct UISceneTraits<UISceneType::name> \
        { \
            using DataType = data; \
        };
    UI_SCENE_TYPES
    #undef X

    constexpr const char* GetSceneName(UISceneType scene)
    {
        switch(scene)
        {
            #define X(name, data) case UISceneType::name: return #name;
            UI_SCENE_TYPES
            #undef X
            default: return "Unknown";
        }
    }
}
