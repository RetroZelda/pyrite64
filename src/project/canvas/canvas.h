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

        std::string serialize() const;
        static PropValue deserialize(const nlohmann::json& j);
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

        // Transform (all bindable)
        PropValue x{};
        PropValue y{};
        PropValue rotation{};

        PropValue visible;  // bool; bindable to a bool variable

        // Type-specific properties (keyed by property name)
        std::unordered_map<std::string, PropValue> props;

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
