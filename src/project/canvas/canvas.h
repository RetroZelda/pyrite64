/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

#include "json.hpp"          // nlohmann::json (via tiny3d include path)

namespace Project
{
    // Supported variable types for canvas data structs
    enum class CanvasVarType : uint8_t
    {
        Float = 0,
        Int,
        Bool,
        ColorRGBA,
        StringPtr,  // const char*
        SpriteRef,  // AssetRef<sprite_t>
        FontRef,    // AssetRef<font64_t>
    };

    struct CanvasVariableDef
    {
        std::string name;
        CanvasVarType type{CanvasVarType::Float};
        std::string defaultValue; // literal default, e.g. "0.0f", "true", "{0xFF,0xFF,0xFF,0xFF}"

        std::string serialize() const;
        static CanvasVariableDef deserialize(const nlohmann::json& j);
    };

    // A named event a canvas can receive. Declaration order is the stable enum
    // index used as the ObjectEvent.type wire value (see canvasBuilder).
    struct CanvasEventDef
    {
        std::string name;

        std::string serialize() const;
        static CanvasEventDef deserialize(const nlohmann::json& j);
    };

    // A property on a canvas element — either a literal value or a binding to a variable.
    // For bool props, isBound may represent a direct variable or a comparison operation.
    struct PropValue
    {
        bool isBound{false};
        std::string boundVar;   // variable name when isBound && !isOp

        nlohmann::json value{}; // literal value when !isBound

        // Comparison operation (bool props only; isBound && isOp)
        bool isOp{false};
        std::string opLhs;       // left-hand variable name
        std::string opOp;        // ">", "<", ">=", "<=", "==", "!="
        bool opRhsIsVar{false};  // true = opRhs is a variable name; false = literal
        std::string opRhs;       // right-hand variable name or literal string

        // Repeater bindings (only valid inside a repeater subtree, Phase 4):
        bool isItemField{false};  // resolves to <repeater>_items[i].<itemField>
        std::string itemField;
        bool isIndex{false};      // resolves to the loop index i

        std::string serialize() const;
        static PropValue deserialize(const nlohmann::json& j);
    };

    // One per-item field of a repeater's generated item struct. SpriteRef is
    // intentionally NOT allowed (per-item textures would break atlas/COPY batching).
    struct CanvasItemField
    {
        std::string name;
        CanvasVarType type{CanvasVarType::Int};

        std::string serialize() const;
        static CanvasItemField deserialize(const nlohmann::json& j);
    };

    // Repeater: draw an element's subtree `count` times in a grid (Phase 4).
    struct CanvasRepeater
    {
        bool enabled{false};
        std::string countVar;        // canvas Int variable giving the live count
        int   maxCount{8};           // compile-time cap on items[]
        int   columns{1};
        float spacingX{16.0f};
        float spacingY{16.0f};
        std::vector<CanvasItemField> itemFields;

        std::string serialize() const;
        static CanvasRepeater deserialize(const nlohmann::json& j);
    };

    // ---- Animation timeline (Phase 5) -------------------------------------

    // Easing applied to the segment LEAVING a keyframe. Maps 1:1 to P64::UI::Eval.
    enum class CanvasEaseType : uint8_t { Linear = 0, EaseOutCubic, EaseInOutCubic, EaseOutSin };

    // Animatable channel. Variable targets a named canvas variable; the rest are
    // per-element animation offsets composited on top of the element's base value.
    enum class CanvasTrackChannel : uint8_t
    { X = 0, Y, Rotation, Scale, Alpha, Color, Variable, FrameIndex };

    struct CanvasKeyframe
    {
        float time{0.0f};
        nlohmann::json value{};                 // float, or [r,g,b,a] for Color
        CanvasEaseType easing{CanvasEaseType::Linear};

        std::string serialize() const;
        static CanvasKeyframe deserialize(const nlohmann::json& j);
    };

    struct CanvasTimelineTrack
    {
        uint64_t targetUuid{0};                 // element being animated (0 for Variable channel)
        CanvasTrackChannel channel{CanvasTrackChannel::X};
        std::string varName;                    // used only when channel == Variable
        std::vector<CanvasKeyframe> keys;

        std::string serialize() const;
        static CanvasTimelineTrack deserialize(const nlohmann::json& j);
    };

    // A timeline event: either fires a named canvas event into the event channel,
    // or performs a built-in Seek (jump a clip to a time). Looping = Seek-to-0 at the end.
    struct CanvasTimelineEvent
    {
        enum class Action : uint8_t { FireEvent = 0, Seek };
        float time{0.0f};
        Action action{Action::FireEvent};
        std::string eventName;                  // FireEvent: the canvas event name
        int   targetAnim{-1};                   // Seek: clip index to seek (-1 = self)
        float seekTime{0.0f};                   // Seek: target time (seconds)

        std::string serialize() const;
        static CanvasTimelineEvent deserialize(const nlohmann::json& j);
    };

    // A named animation clip. Play-once (no loop flag — loop via a Seek event).
    struct CanvasAnimation
    {
        std::string name;
        float duration{1.0f};
        bool  autoPlay{false};
        // Time source: 0 = playback (elapsed advances via play/seek), 1 = driven by a variable.
        // When driven, the clip evaluates at clamp(<timeVar>, 0, duration) every frame — so
        // gameplay sets the displayed pose/frame directly (set duration=1 for a 0..1 drive).
        int   timeSource{0};
        std::string timeVar;   // driven: a canvas float var, or (element clip) a float item field
        std::vector<CanvasTimelineTrack> tracks;
        std::vector<CanvasTimelineEvent> events;

        std::string serialize() const;
        static CanvasAnimation deserialize(const nlohmann::json& j);
    };

    // Element types that can appear in a canvas hierarchy
    enum class CanvasElementType : uint8_t
    {
        Sprite = 0,
        Text,
        ColorRect,
        TextureRect,
    };

    struct CanvasElement
    {
        std::string name;
        CanvasElementType type{CanvasElementType::Sprite};
        uint64_t uuid{0};

        // Optional UI component script (a CODE_OBJ asset uuid; 0 = none). The script
        // gets per-element state + receives events / focus callbacks (buttons etc.).
        // "focusable" is authored via the generic props map (props["focusable"]).
        uint64_t scriptUuid{0};

        // Transform (all bindable)
        PropValue x{};
        PropValue y{};
        PropValue rotation{};

        PropValue visible;  // bool; bindable to a bool variable

        // Type-specific properties (keyed by property name)
        std::unordered_map<std::string, PropValue> props;

        CanvasRepeater repeater;  // optional: repeat this subtree N times (Phase 4)

        std::vector<CanvasAnimation> animations;  // element-scoped clips (Phase 5)

        std::vector<CanvasElement> children;

        std::string serialize() const;
        static CanvasElement deserialize(const nlohmann::json& j);
    };

    struct CanvasConf
    {
        int viewportWidth{320};
        int viewportHeight{240};
        bool showGrid{true};
        int gridSizeX{16};
        int gridSizeY{16};

        std::string serialize() const;
        static CanvasConf deserialize(const nlohmann::json& j);
    };

    struct Canvas
    {
        std::string name;
        std::string path;
        uint64_t uuid{0};

        CanvasConf conf;
        std::vector<CanvasVariableDef> variables;
        std::vector<CanvasEventDef> events;
        std::vector<CanvasAnimation> animations;  // canvas-scoped clips (Phase 5)
        std::vector<CanvasElement> elements;

        void load(const std::string& filePath);
        void save(const std::string& filePath) const;
        void save() const { save(path); }

        std::string serialize() const;
        void deserialize(const std::string& json);

        // Returns the C++ type string for a variable type
        static const char* varTypeToCpp(CanvasVarType t);
        // Returns the human-readable name for a variable type
        static const char* varTypeName(CanvasVarType t);
        // Returns number of supported types
        static constexpr int varTypeCount() { return 7; }
    };
}
