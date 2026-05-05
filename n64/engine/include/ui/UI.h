#pragma once

#include "utility/ObjectDelegate.h"
#include "p64/UISceneTypes.h"

#include <cstdint>


namespace P64
{
    namespace UI
    {
        template<UISceneType Scene>
        using ModelCallback = P64::User::Utility::ObjectDelegate<void(typename UISceneTraits<Scene>::DataType&, float)>;

        template<UISceneType Scene>
        using ViewCallback  = P64::User::Utility::ObjectDelegate<void(const typename UISceneTraits<Scene>::DataType&)>;

        using ControllerCallback = P64::User::Utility::ObjectDelegate<void(UISceneType, bool)>;

        // Bind a model-update callback for the given scene (game logic fills the data each frame).
        template<UISceneType Scene>
        bool bindModel(ModelCallback<Scene> callback);
        template<UISceneType Scene>
        void unbindModel();

        // Bind a view callback (draws using the data). Uses ObjectDelegate — requires a live Object&.
        template<UISceneType Scene>
        bool bindView(ViewCallback<Scene> callback);
        template<UISceneType Scene>
        void unbindView();

        // Bind a stateless view function — no Object& required. Used by auto-generated canvas code.
        // Takes DataType& (non-const) because AssetRef::get() is non-const (lazy-loads on first call).
        template<UISceneType Scene>
        bool bindViewFn(void(*fn)(typename UISceneTraits<Scene>::DataType&));
        template<UISceneType Scene>
        void unbindViewFn();

        bool bindController(ControllerCallback callback);
        void unbindController();

        void setSceneState(UISceneType scene, bool enabled);

        void init();
        void destroy();
        void update(float deltaTime);
        void draw_2D();

        void addDebugMenu();
    }
}
