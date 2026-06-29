// NOTE: Auto-Generated File!

#include "ui/UI.h"
#include "debug/debugMenu.h"
#include "debug/debugDraw.h"

#include <cstddef>

namespace P64::UI
{
    // ===================================================================
    // Private implementation
    // ===================================================================
    namespace detail
    {
        struct IModelView
        {
            virtual ~IModelView() = default;
            virtual void update(float deltaTime) = 0;
            virtual void draw_2d() = 0;
            virtual bool hasModel() const = 0;
            virtual bool hasView()  const = 0;
            virtual void runBehavior(BehaviorPhase phase, float dt, const ObjectEvent* ev) = 0;
            virtual uint8_t focusableCount() const = 0;
        };

        template<UISceneType type>
        struct UIModelView final : IModelView
        {
            ModelCallback<type> model_callback;
            ViewCallback<type>  view_callback;
            void (*view_fn)(typename UISceneTraits<type>::DataType&) = nullptr;
            BehaviorFn<type>    behavior_fn = nullptr;
            uint8_t             focusables  = 0;
            AnimFn<type>        anim_fn     = nullptr;
            AnimPlayback        clips[MAX_UI_ANIMS]{};
            uint8_t             animCount   = 0;
            typename UISceneTraits<type>::DataType data{};

            // Object id of the gameplay object that bound this scene's model
            // callback (0 if none). Used as event sender/target and as the host
            // object passed to component scripts.
            uint16_t ownerObjId{0};

            // Fixed per-scene outbound event queue. Gameplay fills it via the
            // EventSink handed to the model callback; drained to receivers each frame.
            static constexpr uint8_t MAX_UI_EVENTS = 32;
            ObjectEvent outbox[MAX_UI_EVENTS]{};
            uint8_t     outboxCount{0};

            void update(float deltaTime) override
            {
                outboxCount = 0;
                EventSink sink{ outbox, &outboxCount, MAX_UI_EVENTS, ownerObjId };
                if (model_callback)
                    model_callback(data, sink, deltaTime);
                // Animation tick AFTER the model callback so animated channels
                // composite on top of the gameplay-set base values this frame.
                // Timeline events flow into the same outbox, drained below.
                if (anim_fn)
                    anim_fn(data, clips, sink, ownerObjId, deltaTime);
                if (behavior_fn)
                {
                    // Component scripts: per-frame update, then deliver this frame's
                    // outbound events to each scripted element's onEvent.
                    behavior_fn(data, ownerObjId, BehaviorPhase::Update, deltaTime, nullptr);
                    for (uint8_t e = 0; e < outboxCount; ++e)
                        behavior_fn(data, ownerObjId, BehaviorPhase::Event, 0.0f, &outbox[e]);
                }
            }

            void draw_2d() override
            {
                if (view_callback)
                    view_callback(data);
                else if (view_fn)
                    view_fn(data);
            }

            void runBehavior(BehaviorPhase phase, float dt, const ObjectEvent* ev) override
            {
                if (behavior_fn)
                    behavior_fn(data, ownerObjId, phase, dt, ev);
            }
            uint8_t focusableCount() const override { return focusables; }

            bool hasModel() const override { return (bool)model_callback; }
            bool hasView()  const override { return (bool)view_callback || view_fn != nullptr; }
        };

        constexpr size_t kNumScenes = static_cast<size_t>(UISceneType::Count);
        IModelView* scenes[kNumScenes] = {};
        bool states[kNumScenes] = {false};
        int8_t layers[kNumScenes] = {0};  // draw order; higher = on top

        ControllerCallback controller_callback;
    }

    template<UISceneType Scene>
    detail::UIModelView<Scene>* getTypedView()
    {
        constexpr auto idx = static_cast<size_t>(Scene);
        auto* base = detail::scenes[idx];
        if (!base) return nullptr;
        return static_cast<detail::UIModelView<Scene>*>(base);
    }

    template<UISceneType Scene>
    typename UISceneTraits<Scene>::DataType* getViewData()
    {
        auto* view = getTypedView<Scene>();
        return view ? &view->data : nullptr;
    }

    // ===================================================================
    // Public binding API
    // ===================================================================
    template<UISceneType Scene>
    bool bindModel(ModelCallback<Scene> callback)
    {
        auto* view = getTypedView<Scene>();
        if (!view || view->model_callback)
            return false;
        view->model_callback = std::move(callback);
        view->ownerObjId = static_cast<uint16_t>(view->model_callback.boundObjectId());
        return true;
    }

    template<UISceneType Scene>
    void unbindModel()
    {
        if (auto* view = getTypedView<Scene>()) {
            view->model_callback = {};
            view->ownerObjId = 0;
        }
    }

    template<UISceneType Scene>
    bool bindView(ViewCallback<Scene> callback)
    {
        auto* view = getTypedView<Scene>();
        if (!view || view->view_callback)
            return false;
        view->view_callback = std::move(callback);
        return true;
    }

    template<UISceneType Scene>
    void unbindView()
    {
        if (auto* view = getTypedView<Scene>())
            view->view_callback = {};
    }

    template<UISceneType Scene>
    bool bindViewFn(void(*fn)(typename UISceneTraits<Scene>::DataType&))
    {
        auto* view = getTypedView<Scene>();
        if (!view || view->view_fn)
            return false;
        view->view_fn = fn;
        return true;
    }

    template<UISceneType Scene>
    void unbindViewFn()
    {
        if (auto* view = getTypedView<Scene>())
            view->view_fn = nullptr;
    }

    template<UISceneType Scene>
    bool bindBehaviorFn(BehaviorFn<Scene> fn, uint8_t focusableCount)
    {
        auto* view = getTypedView<Scene>();
        if (!view || view->behavior_fn)
            return false;
        view->behavior_fn = fn;
        view->focusables  = focusableCount;
        // Initialize element scripts now (the binding object is live at bind time).
        view->runBehavior(BehaviorPhase::Init, 0.0f, nullptr);
        return true;
    }

    template<UISceneType Scene>
    void unbindBehaviorFn()
    {
        if (auto* view = getTypedView<Scene>()) {
            view->runBehavior(BehaviorPhase::Destroy, 0.0f, nullptr);
            view->behavior_fn = nullptr;
            view->focusables  = 0;
        }
    }

    template<UISceneType Scene>
    bool bindAnimFn(AnimFn<Scene> fn, uint8_t clipCount, uint16_t autoPlayMask)
    {
        auto* view = getTypedView<Scene>();
        if (!view || view->anim_fn)
            return false;
        view->anim_fn   = fn;
        view->animCount = clipCount < MAX_UI_ANIMS ? clipCount : MAX_UI_ANIMS;
        for (uint8_t i = 0; i < view->animCount; ++i)
            view->clips[i].playing = (autoPlayMask >> i) & 1u;
        return true;
    }

    template<UISceneType Scene>
    void unbindAnimFn()
    {
        if (auto* view = getTypedView<Scene>()) {
            view->anim_fn   = nullptr;
            view->animCount = 0;
            for (auto& c : view->clips) c = {};
        }
    }

    template<UISceneType Scene>
    void playAnimation(typename UISceneTraits<Scene>::Anim clip, bool restart)
    {
        auto* view = getTypedView<Scene>();
        uint8_t c = (uint8_t)clip;
        if (!view || c >= view->animCount) return;
        if (restart) { view->clips[c].elapsed = 0.0f; view->clips[c].prevElapsed = 0.0f; }
        view->clips[c].playing  = 1;
        view->clips[c].finished = 0;
    }

    template<UISceneType Scene>
    void stopAnimation(typename UISceneTraits<Scene>::Anim clip)
    {
        auto* view = getTypedView<Scene>();
        uint8_t c = (uint8_t)clip;
        if (!view || c >= view->animCount) return;
        view->clips[c].playing = 0;
    }

    template<UISceneType Scene>
    void seekAnimation(typename UISceneTraits<Scene>::Anim clip, float seconds)
    {
        auto* view = getTypedView<Scene>();
        uint8_t c = (uint8_t)clip;
        if (!view || c >= view->animCount) return;
        view->clips[c].elapsed = view->clips[c].prevElapsed = seconds;
    }

    bool bindController(ControllerCallback callback)
    {
        if (detail::controller_callback)
            return false;
        detail::controller_callback = std::move(callback);
        if (detail::controller_callback)
        {
            for (size_t i = 0; i < detail::kNumScenes; ++i)
            {
                detail::controller_callback(static_cast<UISceneType>(i), detail::states[i]);
            }
        }
        return true;
    }

    void unbindController()
    {
        detail::controller_callback = {};
    }

    void setSceneState(UISceneType scene, bool enabled, int layer)
    {
        auto idx = static_cast<size_t>(scene);
        detail::states[idx] = enabled;
        detail::layers[idx] = static_cast<int8_t>(layer);
        if (detail::controller_callback)
            detail::controller_callback(scene, enabled);
    }

    void init()
    {
        // Idempotent + clean baseline. Tear down any prior instances and reset the
        // per-scene enable flags so a freshly loaded scene starts with every UI
        // scene DISABLED until gameplay enables it — this prevents a scene that
        // was enabled in the previous scene from drawing as a ghost in the new one.
        for (size_t i = 0; i < detail::kNumScenes; ++i)
        {
            delete detail::scenes[i];
            detail::scenes[i] = nullptr;
            detail::states[i] = false;
            detail::layers[i] = 0;
        }
        __UI_INIT_ENTRIES__
    }

    void destroy()
    {
        for (size_t i = 0; i < detail::kNumScenes; ++i)
        {
            // Best-effort component-script teardown (the host object may already be
            // gone by scene-unload, in which case the generated behavior fn no-ops).
            if (detail::scenes[i])
                detail::scenes[i]->runBehavior(BehaviorPhase::Destroy, 0.0f, nullptr);
            delete detail::scenes[i];
            detail::scenes[i] = nullptr;
            detail::states[i] = false;
            detail::layers[i] = 0;
        }
        detail::controller_callback = {};
    }

    void update(float deltaTime)
    {
        for (size_t i = 0; i < detail::kNumScenes; ++i)
        {
            if (!detail::states[static_cast<size_t>(i)]) continue;
            if (detail::scenes[i])
                detail::scenes[i]->update(deltaTime);
        }

        // Focus navigation: exactly one scene owns controller focus — the TOPMOST
        // enabled scene that has focusable scripted elements (highest layer, then
        // highest enum index). Only it reads input + moves focus this frame.
        int    focusOwner = -1;
        int8_t bestLayer  = 0;
        for (size_t i = 0; i < detail::kNumScenes; ++i)
        {
            if (!detail::states[i] || !detail::scenes[i]) continue;
            if (detail::scenes[i]->focusableCount() == 0) continue;
            if (focusOwner < 0 || detail::layers[i] >= bestLayer) {
                focusOwner = (int)i;
                bestLayer  = detail::layers[i];
            }
        }
        if (focusOwner >= 0)
            detail::scenes[focusOwner]->runBehavior(BehaviorPhase::FocusInput, deltaTime, nullptr);
    }

    void draw_2D()
    {
        // Collect enabled scenes, then draw sorted by (layer, enum index) so higher
        // layers render on top. Insertion sort is stable -> equal layers keep index order.
        size_t order[detail::kNumScenes];
        size_t n = 0;
        for (size_t i = 0; i < detail::kNumScenes; ++i)
            if (detail::states[i] && detail::scenes[i])
                order[n++] = i;

        for (size_t a = 1; a < n; ++a)
        {
            size_t v  = order[a];
            int8_t lv = detail::layers[v];
            size_t b  = a;
            while (b > 0 && detail::layers[order[b - 1]] > lv) {
                order[b] = order[b - 1];
                --b;
            }
            order[b] = v;
        }

        for (size_t k = 0; k < n; ++k)
            detail::scenes[order[k]]->draw_2d();
    }
    
    void addDebugMenu()
    {
        auto& menu = Debug::Overlay::addCustomMenu("UI");
        menu.onDraw = []() {
            constexpr uint8_t  step = 8;
            constexpr uint16_t x    = 15;
            uint16_t y = 45;

            Debug::printf(x, y, "UI Scenes:");
            y += step;

            Debug::printf(x + 7, y, "Scene               State  Model  View");
            y += 6;

            Debug::printf(x + 15, y, "-------------------------------");
            y += step;

            for (uint8_t i = 0; i < static_cast<uint8_t>(UISceneType::Count); ++i)
            {
                auto  type    = static_cast<UISceneType>(i);
                bool  enabled = detail::states[i];
                auto* scene   = detail::scenes[i];

                const char* state = enabled ? "ON"  : "OFF";
                const char* model = (scene && scene->hasModel()) ? "Y" : "N";
                const char* view  = (scene && scene->hasView())  ? "Y" : "N";

                Debug::printf(x + 15, y,
                    "  %-20s %-6s %-6s %-6s",
                    GetSceneName(type), state, model, view);

                y += step;
            }
        };
    }
}

__CANVAS_TEMPLATE_INSTANTIATIONS__
