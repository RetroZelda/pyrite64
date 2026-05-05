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
        };

        template<UISceneType type>
        struct UIModelView final : IModelView
        {
            ModelCallback<type> model_callback;
            ViewCallback<type>  view_callback;
            void (*view_fn)(typename UISceneTraits<type>::DataType&) = nullptr;
            typename UISceneTraits<type>::DataType data{};

            void update(float deltaTime) override
            {
                if (model_callback)
                    model_callback(data, deltaTime);
            }

            void draw_2d() override
            {
                if (view_callback)
                    view_callback(data);
                else if (view_fn)
                    view_fn(data);
            }

            bool hasModel() const override { return (bool)model_callback; }
            bool hasView()  const override { return (bool)view_callback || view_fn != nullptr; }
        };

        constexpr size_t kNumScenes = static_cast<size_t>(UISceneType::Count);
        IModelView* scenes[kNumScenes] = {};
        bool states[kNumScenes] = {false};

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
        return true;
    }

    template<UISceneType Scene>
    void unbindModel()
    {
        if (auto* view = getTypedView<Scene>())
            view->model_callback = {};
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

    void setSceneState(UISceneType scene, bool enabled)
    {
        detail::states[static_cast<size_t>(scene)] = enabled;
        if (detail::controller_callback)
            detail::controller_callback(scene, enabled);
    }

    void init()
    {
        __UI_INIT_ENTRIES__
    }

    void destroy()
    {
        for (size_t i = 0; i < detail::kNumScenes; ++i)
        {
            delete detail::scenes[i];
            detail::scenes[i] = nullptr;
        }
    }

    void update(float deltaTime)
    {
        for (size_t i = 0; i < detail::kNumScenes; ++i)
        {
            if (!detail::states[static_cast<size_t>(i)]) continue;
            if (detail::scenes[i])
                detail::scenes[i]->update(deltaTime);
        }
    }

    void draw_2D()
    {
        for (size_t i = 0; i < detail::kNumScenes; ++i)
        {
            if (!detail::states[static_cast<size_t>(i)]) continue;
            if (detail::scenes[i])
                detail::scenes[i]->draw_2d();
        }
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
