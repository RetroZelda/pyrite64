/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "../components.h"
#include "../../../context.h"
#include "../../../editor/imgui/helper.h"
#include "../../../utils/json.h"
#include "../../../utils/jsonBuilder.h"
#include "../../../utils/binaryFile.h"
#include "../../assetManager.h"
#include "../../../editor/pages/parts/viewport3D.h"
#include "../../../renderer/scene.h"
#include "../../../utils/meshGen.h"
#include <glm/gtc/quaternion.hpp>
#include <algorithm>

#include "../../../../n64/engine/include/collision/types.h"

namespace
{
  constexpr int32_t TYPE_BOX      = 0;
  constexpr int32_t TYPE_SPHERE   = 1;
  constexpr int32_t TYPE_CYLINDER = 2;
  constexpr int32_t TYPE_CAPSULE  = 3;
  constexpr int32_t TYPE_CONE     = 4;
  constexpr int32_t TYPE_PYRAMID  = 5;
  
}

namespace Project::Component::CollBody
{
  struct Data
  {
    PROP_VEC3(halfExtend);
    PROP_VEC3(offset);
    PROP_S32(type);
    PROP_BOOL(isTrigger);
    PROP_U32(maskRead);
    PROP_U32(maskWrite);
    PROP_FLOAT(friction);
    PROP_FLOAT(bounce);
    PROP_VEC4(debugColor); // {0,0,0,0} = use default per-shape color
  };

  std::shared_ptr<void> init(Object &obj) {
    auto data = std::make_shared<Data>();
    data->halfExtend.value = {10.0f, 10.0f, 10.0f};
    data->friction.value = 0.8f;
    return data;
  }

  nlohmann::json serialize(const Entry &entry) {
    Data &data = *static_cast<Data*>(entry.data.get());
    return Utils::JSON::Builder{}
      .set(data.halfExtend)
      .set(data.offset)
      .set(data.type)
      .set(data.isTrigger)
      .set(data.maskRead)
      .set(data.maskWrite)
      .set(data.friction)
      .set(data.bounce)
      .set(data.debugColor)
      .doc;
  }

  std::shared_ptr<void> deserialize(nlohmann::json &doc) {
    auto data = std::make_shared<Data>();
    Utils::JSON::readProp(doc, data->halfExtend, glm::vec3{1.0f, 1.0f, 1.0f});
    Utils::JSON::readProp(doc, data->offset);
    Utils::JSON::readProp(doc, data->type);
    Utils::JSON::readProp(doc, data->isTrigger, false);
    Utils::JSON::readProp(doc, data->maskRead, 0x00u);
    Utils::JSON::readProp(doc, data->maskWrite, 0x00u);
    Utils::JSON::readProp(doc, data->friction, 0.8f);
    Utils::JSON::readProp(doc, data->bounce, 0.0f);
    Utils::JSON::readProp(doc, data->debugColor, glm::vec4{0.0f, 0.0f, 0.0f, 0.0f});
    return data;
  }

  void build(Object& obj, Entry &entry, Build::SceneCtx &ctx)
  {
    Data &data = *static_cast<Data*>(entry.data.get());
    ctx.fileObj.write(data.halfExtend.resolve(obj.propOverrides));
    ctx.fileObj.write(data.offset.resolve(obj.propOverrides));

    uint8_t type = (uint8_t)P64::Coll::ShapeType::Box;
    switch (data.type.resolve(obj.propOverrides)) {
      case TYPE_BOX: type = (uint8_t)P64::Coll::ShapeType::Box; break;
      case TYPE_SPHERE: type = (uint8_t)P64::Coll::ShapeType::Sphere; break;
      case TYPE_CYLINDER: type = (uint8_t)P64::Coll::ShapeType::Cylinder; break;
      case TYPE_CAPSULE: type = (uint8_t)P64::Coll::ShapeType::Capsule; break;
      case TYPE_CONE: type = (uint8_t)P64::Coll::ShapeType::Cone; break;
      case TYPE_PYRAMID: type = (uint8_t)P64::Coll::ShapeType::Pyramid; break;
    }
    ctx.fileObj.write<uint8_t>(type);
    ctx.fileObj.write<uint8_t>(data.isTrigger.resolve(obj.propOverrides));
    ctx.fileObj.write<uint8_t>(data.maskRead.resolve(obj.propOverrides));
    ctx.fileObj.write<uint8_t>(data.maskWrite.resolve(obj.propOverrides));
    ctx.fileObj.write<float>(data.friction.resolve(obj.propOverrides));
    ctx.fileObj.write<float>(data.bounce.resolve(obj.propOverrides));
    ctx.fileObj.writeRGBA(data.debugColor.resolve(obj.propOverrides));
  }

  void draw(Object &obj, Entry &entry)
  {
    Data &data = *static_cast<Data*>(entry.data.get());

    if (ImTable::start("Comp", &obj)) {
      ImTable::add("Name");
      {
        float colorBtnW = 26.0f;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - colorBtnW - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::InputText("##compname", &entry.name))
          Editor::UndoRedo::getHistory().markChanged("Edit Name");
        ImGui::SameLine();
        {
          bool locked = ImTable::isPrefabLocked();
          if (locked) ImGui::BeginDisabled();
          if (ImGui::ColorEdit4("##debugcol", glm::value_ptr(data.debugColor.value),
              ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoAlpha)) {
            // Auto-manage the alpha sentinel: non-black RGB → active (alpha=1), pure black → inactive (alpha=0)
            auto &c = data.debugColor.value;
            c.a = (c.r + c.g + c.b > 0.0f) ? 1.0f : 0.0f;
            Editor::UndoRedo::getHistory().markChanged("Edit Debug Color");
          }
          if (locked) ImGui::EndDisabled();
        }
      }

      auto &ext = data.halfExtend.resolve(obj.propOverrides);
      auto collType = data.type.resolve(obj.propOverrides);

      ImTable::addObjProp<int32_t>("Type", data.type, [](int32_t *val) -> bool {
        const char* types[] = {"Box", "Sphere", "Cylinder", "Capsule", "Cone", "Pyramid"};
        return ImGui::Combo("##type", val, types, 6);
      }, nullptr);

      if(collType == TYPE_SPHERE) {
        ImTable::addObjProp<glm::vec3>("Radius", data.halfExtend, [](glm::vec3 *val) -> bool {
          bool changed = ImGui::InputFloat("##r", &val->y);
          if (changed) { val->x = val->y; val->z = val->y; }
          return changed;
        }, nullptr);
        ext.x = ext.y; ext.z = ext.y;
      } else {
        ImTable::addObjProp("Half Size", data.halfExtend);
        if(collType == TYPE_CYLINDER || collType == TYPE_CAPSULE || collType == TYPE_CONE)
          ext.z = ext.x;
      }

      ImTable::addObjProp("Offset", data.offset);
      ImTable::addObjProp("Trigger", data.isTrigger);

      const auto& layerNames = ctx.project->conf.collLayerNames;
      auto maskLambda = [&layerNames](const char* id) {
        return [&layerNames, id](uint32_t *val) -> bool {
          std::string preview;
          for (int i = 0; i < 8; ++i) {
            if (*val & (1u << i)) {
              if (!preview.empty()) preview += "  |  ";
              preview += layerNames[i].empty() ? "??" : layerNames[i];
            }
          }
          bool isEmpty = preview.empty();
          if (isEmpty) {
            preview = "<Nothing>";
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
          }
          bool changed = false;
          bool open = ImGui::BeginCombo(id, preview.c_str());
          if (isEmpty) ImGui::PopStyleColor();
          if (open) {
            for (int i = 0; i < 8; ++i) {
              if (layerNames[i].empty()) continue;
              bool selected = (*val & (1u << i)) != 0;
              auto label = std::string{selected ? ICON_MDI_CHECKBOX_MARKED " " : ICON_MDI_CHECKBOX_BLANK_OUTLINE " "}
                         + layerNames[i] + "##" + std::to_string(i);
              if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_DontClosePopups)) {
                if (!selected) *val |= (1u << i); else *val &= ~(1u << i);
                changed = true;
              }
            }
            ImGui::EndCombo();
          }
          return changed;
        };
      };
      ImTable::addObjProp<uint32_t>("Reacts to",   data.maskRead,  maskLambda("##mr"), nullptr);
      ImTable::addObjProp<uint32_t>("Is Affecting", data.maskWrite, maskLambda("##mw"), nullptr);

      ImTable::addObjProp<float>("Friction", data.friction, [](float *val) -> bool {
        bool changed = ImGui::InputFloat("##f", val);
        if (changed) *val = std::clamp(*val, 0.0f, 1.0f);
        return changed;
      }, nullptr);
      ImTable::addObjProp<float>("Bounce", data.bounce, [](float *val) -> bool {
        bool changed = ImGui::InputFloat("##b", val);
        if (changed) *val = std::clamp(*val, 0.0f, 1.0f);
        return changed;
      }, nullptr);


      ImTable::end();
    }
  }

  void draw3D(Object& obj, Entry &entry, Editor::Viewport3D &vp, SDL_GPUCommandBuffer* cmdBuff, SDL_GPURenderPass* pass)
  {
    Data &data = *static_cast<Data*>(entry.data.get());
    auto &objPos = obj.pos.resolve(obj.propOverrides);
    auto &objRot = obj.rot.resolve(obj.propOverrides);
    auto &objScale = obj.scale.resolve(obj.propOverrides);

    glm::vec3 halfExt = data.halfExtend.resolve(obj.propOverrides) * objScale;
    glm::vec3 localOffset = data.offset.resolve(obj.propOverrides);
    glm::vec3 center = objPos + (objRot * (localOffset * objScale));
    auto type = data.type.resolve(obj.propOverrides);

    auto customColor = data.debugColor.resolve(obj.propOverrides);
    // Draw at full opacity so the viewport matches the solid swatch shown in the picker.
    const glm::vec4 lineColor = (customColor.a > 0.0f)
        ? glm::vec4{customColor.r, customColor.g, customColor.b, 1.0f}
        : ctx.prefs.colliderLineColor;

    if(type == TYPE_BOX) // Box
    {
      Utils::Mesh::addLineBox(*vp.getLines(), center, halfExt, lineColor, objRot, ctx.prefs.aabbLineThickness);
      Utils::Mesh::addLineBox(*vp.getLines(), center, halfExt + 0.002f, lineColor, objRot, ctx.prefs.aabbLineThickness);
    } else if(type == TYPE_SPHERE) // Sphere
    {
      Utils::Mesh::addLineSphere(*vp.getLines(), center, halfExt, lineColor, objRot, ctx.prefs.colliderLineThickness);
    }
    else if(type == TYPE_CYLINDER) // Cylinder
    {
      Utils::Mesh::addLineCylinder(*vp.getLines(), center, halfExt, lineColor, objRot, ctx.prefs.colliderLineThickness);
    }
    else if(type == TYPE_CAPSULE) // Capsule
    {
      Utils::Mesh::addLineCapsule(*vp.getLines(), center, halfExt, lineColor, objRot, ctx.prefs.colliderLineThickness);
    }
    else if(type == TYPE_CONE) // Cone
    {
      Utils::Mesh::addLineCone(*vp.getLines(), center, halfExt, lineColor, objRot, ctx.prefs.colliderLineThickness);
    }
    else if(type == TYPE_PYRAMID) // Pyramid
    {
      Utils::Mesh::addLinePyramid(*vp.getLines(), center, halfExt, lineColor, objRot, ctx.prefs.colliderLineThickness);
    }
  }
}
