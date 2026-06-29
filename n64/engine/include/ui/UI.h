#pragma once

#include "utility/ObjectDelegate.h"
#include "scene/event.h"
#include "ui/uiElemState.h"
#include "p64/UISceneTypes.h"

#include <cstdint>
#include <type_traits>


namespace P64
{
    namespace UI
    {
        // Outbound event channel passed to the model callback so gameplay can SEND
        // named events into this UI scene. Components RECEIVE these (Phase 3); gameplay
        // never receives. Backed by a fixed per-scene outbox in the UIModelView.
        struct EventSink
        {
            ObjectEvent* outbox{};
            uint8_t*     count{};
            uint8_t      cap{};
            uint16_t     sender{};

            void send(uint16_t type, uint32_t value = 0)
            {
                if (outbox && count && *count < cap)
                    outbox[(*count)++] = ObjectEvent{ sender, type, value };
            }

            // Typed overload (parity with playAnimation): send a named event enum directly,
            // e.g. events.send(HUDEvent::Foo). Scoped enums don't implicitly convert to uint16_t,
            // so this is unambiguous with the raw overload (the int-based generated FireEvent and
            // any explicit uint16_t still resolve to send(uint16_t)).
            template<typename E, typename = std::enable_if_t<std::is_enum_v<E>>>
            void send(E type, uint32_t value = 0) { send((uint16_t)type, value); }
        };

        // Type-safe RECEIVE side (parity with the typed send): compare an incoming ObjectEvent
        // against a named event enum in a component script's onEvent, e.g.
        //   if (P64::UI::eventIs(event, HUDData::Event::HealthChanged)) { ... }
        template<typename E, typename = std::enable_if_t<std::is_enum_v<E>>>
        inline bool eventIs(const ObjectEvent& ev, E type) { return ev.type == (uint16_t)type; }

        // UIElemState / BehaviorPhase come from "ui/uiElemState.h" (included above) so
        // the generated UISceneTypes.h data structs can use UIElemState too.
        template<UISceneType Scene>
        using BehaviorFn = void(*)(typename UISceneTraits<Scene>::DataType&, uint16_t ownerId,
                                   BehaviorPhase phase, float dt, const ObjectEvent* ev);

        // Generated per-canvas animation-tick function (Phase 5): advances playing
        // clips, writes per-element AnimState offsets, fires timeline events into the
        // same outbox gameplay uses (delivered to component scripts this frame).
        template<UISceneType Scene>
        using AnimFn = void(*)(typename UISceneTraits<Scene>::DataType&, AnimPlayback* clips,
                               EventSink& events, uint16_t ownerId, float dt);

        template<UISceneType Scene>
        using ModelCallback = P64::User::Utility::ObjectDelegate<void(typename UISceneTraits<Scene>::DataType&, EventSink&, float)>;

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

        // Bind the generated component-script/focus "behavior" function for a scene.
        // `focusableCount` is the number of focusable scripted elements (drives focus nav).
        template<UISceneType Scene>
        bool bindBehaviorFn(BehaviorFn<Scene> fn, uint8_t focusableCount);
        template<UISceneType Scene>
        void unbindBehaviorFn();

        // Bind the generated animation-tick function. `autoPlayMask` bit i = clip i auto-plays.
        template<UISceneType Scene>
        bool bindAnimFn(AnimFn<Scene> fn, uint8_t clipCount, uint16_t autoPlayMask);
        template<UISceneType Scene>
        void unbindAnimFn();

        // Animation playback control by typed clip enum (codegen emits a <Name>Anim enum per
        // canvas; the enumerator value is the clip index). e.g. playAnimation<HUD>(HUDAnim::Bar).
        template<UISceneType Scene>
        void playAnimation(typename UISceneTraits<Scene>::Anim clip, bool restart = true);
        template<UISceneType Scene>
        void stopAnimation(typename UISceneTraits<Scene>::Anim clip);
        template<UISceneType Scene>
        void seekAnimation(typename UISceneTraits<Scene>::Anim clip, float seconds);

        // Returns a pointer to a scene's live Data (nullptr if not bound). Generated
        // per-repeater item-clip control functions use this to reach items[i].__pb (Phase 5e).
        template<UISceneType Scene>
        typename UISceneTraits<Scene>::DataType* getViewData();

        bool bindController(ControllerCallback callback);
        void unbindController();

        // Enable/disable a UI scene. `layer` sets draw order: higher layers draw on
        // top of lower ones (ties broken by scene enum index). Default 0 keeps the
        // historical enum-index ordering. Layering is established by gameplay code here.
        void setSceneState(UISceneType scene, bool enabled, int layer = 0);

        void init();
        void destroy();
        void update(float deltaTime);
        void draw_2D();

        void addDebugMenu();
    }
}
