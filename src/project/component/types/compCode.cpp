/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "../components.h"
#include "../../../context.h"
#include "../../../editor/imgui/helper.h"
#include "../../../editor/imgui/notification.h"
#include "../../../utils/json.h"
#include "../../../utils/jsonBuilder.h"
#include "../../../utils/binaryFile.h"
#include "../../../utils/logger.h"
#include "../../../utils/proc.h"
#include "../../../utils/string.h"
#include <algorithm>
#include <cctype>
#include <compare>
#include <cstring>
#include <functional>
#include <iterator>
#include <unordered_map>
#include <unordered_set>

namespace Project::Component::Code
{
  struct Data
  {
    uint64_t scriptUUID{0};
    bool openScriptComboBox{true}; // Open the Script ComboBox when the component is added
    std::unordered_map<std::string, PropString> args{};
  };

  namespace
  {
    // Instance overrides of args live in an override map keyed like every other property. Nested prefab
    // nodes use the path-salted key of the authoring layer (see PropScope / Object::addPropOverride); a
    // plain object or instance root uses the bare key crc64(field name), which it shares with e.g. the
    // transform scale. Args only ever store string overrides: an entry of another type belongs to that
    // other property, it is never used as the arg's value and never changed through the arg.
    GenericValue* findStringSlot(std::unordered_map<uint64_t, GenericValue> &map, uint64_t key)
    {
      auto it = map.find(key);
      if (it == map.end() || it->second.type != GenericValue::typeToId<std::string>())return nullptr;
      return &it->second;
    }

    // Override that currently provides the arg's value: the scoped cascade first (outermost layer wins,
    // like Property::resolve), then a bare key on the object itself (also overrides authored before
    // keys were path-scoped).
    GenericValue* findArgOverride(Object &obj, const PropString &prop)
    {
      for (const auto &layer : PropScope::stack) {
        auto *map = const_cast<std::unordered_map<uint64_t, GenericValue>*>(layer.overrides);
        if (auto *ovr = findStringSlot(*map, PropScope::combine(layer.pathHash, prop.id)))return ovr;
      }
      return findStringSlot(obj.propOverrides, prop.id);
    }

    // Whether the arg is overridden on the slot edits are authored into (drives the lock toggle).
    bool hasArgOverride(Object &obj, const PropString &prop)
    {
      if (const auto *layer = PropScope::authorLayer()) {
        auto *map = const_cast<std::unordered_map<uint64_t, GenericValue>*>(layer->overrides);
        return findStringSlot(*map, PropScope::combine(layer->pathHash, prop.id)) != nullptr;
      }
      return findStringSlot(obj.propOverrides, prop.id) != nullptr;
    }

    // Only bare keys can collide with another property; a scoped key is unique to its nested slot.
    bool isArgKeyTaken(const Object &obj, const PropString &prop)
    {
      if (PropScope::authorLayer())return false;
      auto it = obj.propOverrides.find(prop.id);
      return it != obj.propOverrides.end() && it->second.type != GenericValue::typeToId<std::string>();
    }

    // Value of the arg on this object: its instance override, otherwise the value of the component
    std::string& resolveArg(Object &obj, PropString &prop)
    {
      auto *ovr = findArgOverride(obj, prop);
      return ovr ? ovr->valString : prop.value;
    }

    // Enables the override on the authoring slot, starting from the value currently shown
    // (which may come from an inner prefab's override rather than the component).
    void enableArgOverride(Object &obj, PropString &prop)
    {
      std::string current = resolveArg(obj, prop);
      obj.addPropOverride(prop);
      if (auto *ovr = findArgOverride(obj, prop))ovr->valString = current;
    }

    std::string getKeyTakenTooltip(const Utils::CPP::Field &field)
    {
      return "Can't override, another property of this object is also named '" + field.name + "'";
    }

    // Overriding an arg whose key is taken would replace the other property's override (see findArgOverride)
    void refuseArgOverride(const Object &obj, const Utils::CPP::Field &field, const std::string &label)
    {
      std::string msg = "Can't override '" + label + "' on '" + obj.name + "': its script field '" + field.name
        + "' has the same name as another property of the object, which already uses that override."
        + " Rename the field in the script to override it per instance.";
      Utils::Logger::log(msg, Utils::Logger::LEVEL_WARN);
      Editor::Noti::add(Editor::Noti::ERROR, msg);
    }

    // Instance row of an arg whose key is taken, drawn like an arg that is not overridden:
    // the component's value (read-only), with a lock that refuses to enable the override.
    void drawKeyTakenArg(
      Object &obj, const Utils::CPP::Field &field, const std::string &label, PropString &prop,
      const std::function<bool(std::string*)> &editFunc
    ) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", label.c_str());
      ImGui::TableSetColumnIndex(1);

      ImGui::PushID(&prop);
      ImGui::BeginDisabled();

      bool isOverride = false;
      if (ImGui::IconToggle(isOverride, ICON_MDI_LOCK_OPEN, ICON_MDI_LOCK, ImVec2{16,16})) {
        refuseArgOverride(obj, field, label);
      }
      ImGui::SetItemTooltip("%s", getKeyTakenTooltip(field).c_str());
      ImGui::SameLine();

      std::string value = prop.value; // a copy, nothing is written here
      editFunc(&value);

      ImGui::EndDisabled();
      ImGui::PopID();
    }

    // Component id an ObjectRef with [[P64::RequireComponent("Spline")]] asks for, -1 without the attribute.
    // The name has to match a component exactly. An unknown one (e.g. "spline") can't filter anything:
    // `warning` is set for the inspector and the problem is logged once per script field.
    int getRequiredComponent(const AssetManagerEntry &script, const Utils::CPP::Field &field, std::string &warning)
    {
      auto requireAttr = field.attr.find("P64::RequireComponent");
      if (requireAttr == field.attr.end())return -1;

      const std::string &compName = requireAttr->second;
      const char *similarName = nullptr; // same name in another case, to suggest it
      for (const auto &info : TABLE) {
        if (info.reserved)continue;
        if (compName == info.name)return info.id;

        bool isSimilar = std::equal(compName.begin(), compName.end(), info.name, info.name + strlen(info.name),
          [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); }
        );
        if (isSimilar)similarName = info.name;
      }

      std::string suggestion = similarName ? ("did you mean \"" + std::string{similarName} + "\"?") : "";
      warning = "Unknown component \"" + compName + "\"" + (similarName ? ", " + suggestion : "");

      static std::unordered_set<std::string> warned{};
      if (warned.insert(std::to_string(script.getUUID()) + ":" + field.name + ":" + compName).second) {
        Utils::Logger::log(
          "Script " + script.name + ", field " + field.name + ": P64::RequireComponent(\"" + compName
            + "\") is not a component name, so all objects are offered" + (similarName ? " (" + suggestion + ")" : "") + ".",
          Utils::Logger::LEVEL_WARN
        );
      }
      return -1;
    }
  }

  std::shared_ptr<void> init(Object &obj) {
    auto data = std::make_shared<Data>();
    return data;
  }

  /**
   * Assigns a Script to a Code component.
   * @param entry Code component entry to assign the Script to.
   * @param scriptUUID UUID of the Script.
   * @param openScriptComboBox true to auto-open the combo box.
   */
  void setScript(Entry &entry, uint64_t scriptUUID, bool openScriptComboBox)
  {
    // Reinterpret the generic component payload as Code-specific data
    Data &data = *static_cast<Data*>(entry.data.get());
    // Set the Script UUID to use
    data.scriptUUID = scriptUUID;
    // Preserve the requested combo-box behavior for the next draw call
    data.openScriptComboBox = openScriptComboBox;
  }

  nlohmann::json serialize(const Entry &entry) {
    Data &data = *static_cast<Data*>(entry.data.get());
    Utils::JSON::Builder builder{};
    builder.set("script", data.scriptUUID);

    auto args = nlohmann::json::object();
    for (auto &arg : data.args) {
      args[arg.second.name] = arg.second.value;
    }
    builder.doc["args"] = args;

    return builder.doc;
  }

  std::shared_ptr<void> deserialize(nlohmann::json &doc) {
    auto data = std::make_shared<Data>();
    data->scriptUUID = doc["script"].get<uint64_t>();
    data->openScriptComboBox = false;
    if (doc.contains("args")) {
      auto &argsObj = doc["args"];
      for (auto& [key, val] : argsObj.items()) {
        data->args[key] = PropString{key, val.get<std::string>()};
      }
    }
    return data;
  }

  void build(Object& obj, Entry &entry, Build::SceneCtx &ctx)
  {
    Data &data = *static_cast<Data*>(entry.data.get());

    auto idRes = ctx.codeIdxMapUUID.find(data.scriptUUID);
    uint16_t id = 0xDEAD;
    if (idRes == ctx.codeIdxMapUUID.end()) {
      Utils::Logger::log("Component Code: Script UUID not found: " + std::to_string(entry.uuid), Utils::Logger::LEVEL_ERROR);
    } else {
      id = idRes->second;
    }

    ctx.fileObj.write<uint16_t>(id);
    ctx.fileObj.write<uint16_t>(0);

    auto script = ctx.project->getAssets().getEntryByUUID(data.scriptUUID);
    if (!script)return;

    for (auto &field : script->params.fields) {
      auto &prop = data.args[field.name];
      // not prop.resolve(): an override of another property with the same name is not this arg's value
      std::string val = resolveArg(obj, prop);
      if (val.empty())val = field.defaultValue;
      if (val.empty())val = "0";

      if(field.type == Utils::DataType::ASSET_SPRITE || field.type == Utils::DataType::ASSET_EMITTER)
      {
        uint64_t uuid = Utils::parseU64(val);
        ctx.fileObj.write<uint32_t>(ctx.assetUUIDToIdx[uuid]);
      } else if(field.type == Utils::DataType::OBJECT_REF) {
        uint32_t uuid = static_cast<uint32_t>(Utils::parseU64(val));
        auto refObj = ctx.scene ? ctx.scene->getObjectByUUID(uuid) : nullptr;
        uint16_t objId = refObj ? refObj->runtimeId : 0;
        ctx.fileObj.write<uint32_t>(objId);
      } else if(field.type == Utils::DataType::PREFAB){
        uint64_t uuid = Utils::parseU64(val);
        ctx.fileObj.write<uint32_t>(ctx.assetUUIDToIdx[uuid]);
      } else if(field.type == Utils::DataType::string){
        // Fixed-size char[N] member: write exactly field.dataSize bytes (truncating a longer
        // value, zero-padding a shorter one, always leaving a null terminator). The runtime
        // memcpy's the whole arg blob into the script's Data struct (engine code.h), so a
        // variable-length string write would misalign every field after it.
        for (uint32_t i = 0; i < field.dataSize; ++i) {
          uint8_t c = (i + 1 < field.dataSize && i < val.size()) ? (uint8_t)val[i] : 0;
          ctx.fileObj.write<uint8_t>(c);
        }
      } else {
        try
        {
          ctx.fileObj.writeAs(val, field.type);
        } catch (...) {
          std::string error = script->getName() + ": Invalid argument value '" + val + "' for '" + field.name + "' "
            "(type-id " + std::to_string(field.type) + ")";
          Utils::Logger::log(error, Utils::Logger::LEVEL_ERROR);
          throw std::runtime_error(error);
        }
      }
    }
  }

  void draw(Object &obj, Entry &entry)
  {
    Data &data = *static_cast<Data*>(entry.data.get());

    auto &assets = ctx.project->getAssets();
    auto &scriptList = assets.getTypeEntries(FileType::CODE_OBJ);

    if (ImTable::start("Comp", &obj)) {
      ImTable::add("Name", entry.name);
      ImTable::add("Script");

      // Reserve space for the edit button so the Script combo box keeps its full row layout
      const float editButtonWidth = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.x * 2;
      const float spacing = ImGui::GetStyle().ItemSpacing.x;
      ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - editButtonWidth - spacing);
      ImTable::addAssetVecComboBox("", scriptList, data.scriptUUID, true);

      auto script = assets.getEntryByUUID(data.scriptUUID);
      ImGui::SameLine();
      if (!script) ImGui::BeginDisabled();

      // Open the selected Script in an external application
      if (ImGui::Button(ICON_MDI_PENCIL, {editButtonWidth, 0}) && script) {
        if (!Utils::Proc::openFile(script->path)) {
          Editor::Noti::add(Editor::Noti::Type::ERROR, "Failed to open File. This may be due to WSL path conversion failure.");
        }
      }
      if (!script) ImGui::EndDisabled();
      ImGui::SetItemTooltip("Edit Script");

      if (script) {

        ImTable::add("Arguments:");
        if (script->params.fields.empty()) {
          ImGui::Text("(None)");
        }

        const bool isInstanceMode = ImTable::isPrefabLocked(&obj);
        for (auto &field : script->params.fields)
        {
          std::string name{};
          auto metaName = field.attr.find("P64::Name");
          if (metaName == field.attr.end())continue;
          name = metaName->second;

          if(data.args.find(field.name) == data.args.end()) {
            // keeps id = crc64(name), the same id deserialize() creates, so prefab-instance
            // overrides made before the next save/reload stay attached to the argument
            data.args[field.name] = PropString{field.name, field.defaultValue};
          }

          auto &prop = data.args[field.name];
          // on an instance, the arg can't be overridden while another property holds its override key
          const bool isKeyTaken = isInstanceMode && isArgKeyTaken(obj, prop);

          // value args: with a taken key it is drawn read-only, with a lock refusing to override it
          auto drawArg = [&](const std::function<bool(std::string*)> &editFunc) {
            if (isKeyTaken) {
              drawKeyTakenArg(obj, field, name, prop, editFunc);
            } else {
              ImTable::addObjProp<std::string>(name, prop, editFunc, nullptr);
            }
          };

          if(field.type == Utils::DataType::ASSET_SPRITE ||
             field.type == Utils::DataType::ASSET_EMITTER ||
             field.type == Utils::DataType::OBJECT_REF ||
             field.type == Utils::DataType::PREFAB)
          {
            ImGui::PushID(static_cast<int>(prop.id & 0xFFFFFFFFULL));
            ImTable::add(name);
            
            bool isOverridden = hasArgOverride(obj, prop);
            
            // Lock toggle button
            if (isInstanceMode)
            {
              if (ImGui::IconToggle(isOverridden, ICON_MDI_LOCK_OPEN, ICON_MDI_LOCK, ImVec2{16,16})) {
                if (isKeyTaken) {
                  isOverridden = false;
                  refuseArgOverride(obj, field, name);
                } else if (isOverridden) {
                  enableArgOverride(obj, prop);
                } else {
                  obj.removePropOverride(prop);
                }
              }
              if (isKeyTaken) {
                ImGui::SetItemTooltip("%s", getKeyTakenTooltip(field).c_str());
              } else {
                ImGui::SetItemTooltip(isOverridden ? "Disable Override" : "Enable Override");
              }
              ImGui::SameLine();
            }
            
            ImTable::PrefabEditScope prefabScope(isOverridden);
            std::string resolved = resolveArg(obj, prop);
            uint64_t uuid = resolved.empty() ? 0 : Utils::parseU64(resolved);
            auto validationFunc = [&](uint64_t newId) {
                if (isInstanceMode && !hasArgOverride(obj, prop)) {
                  // e.g. something dropped onto the locked select-box
                  if (isKeyTaken) {
                    refuseArgOverride(obj, field, name);
                    return;
                  }
                  obj.addPropOverride(prop);
                }
                resolveArg(obj, prop) = std::to_string(newId);
              };
            
            if(field.type == Utils::DataType::ASSET_SPRITE) {
              const auto &assets = ctx.project->getAssets().getTypeEntries(FileType::IMAGE);
              ImTable::addAssetVecComboBox("", assets, uuid, validationFunc);
            } else if(field.type == Utils::DataType::ASSET_EMITTER) {
              const auto &assets = ctx.project->getAssets().getTypeEntries(FileType::EMITTER);
              ImTable::addAssetVecComboBox("", assets, uuid, validationFunc);
            } else if(field.type == Utils::DataType::OBJECT_REF) {
              // @TODO: do this in scene itself
              auto scene = ctx.project->getScenes().getLoadedScene();
              std::vector<ImTable::ComboEntry> objList;

              // [[P64::RequireComponent("Spline")]] only offers objects with that component
              std::string requireWarning{};
              const int requiredComp = getRequiredComponent(*script, field, requireWarning);
              auto hasRequiredComp = [requiredComp](const std::vector<Entry> &comps) {
                return std::any_of(comps.begin(), comps.end(), [requiredComp](const Entry &comp) {
                  return comp.id == requiredComp;
                });
              };

              if (scene) {
                for (auto &[id, object] : scene->objectsMap) {
                  if (requiredComp >= 0 && !hasRequiredComp(object->components)) {
                    // components of a prefab instance live on its prefab source node
                    auto srcNode = object->isPrefabInstance() ? assets.getPrefabSourceNode(*object) : nullptr;
                    if (!srcNode || !hasRequiredComp(srcNode->components))continue;
                  }

                  objList.push_back({
                  .value = object->uuid,
                  .name = object->name,
                  });
                }
              }

              // by name (case-insensitive), equal names by uuid so the order is stable
              std::sort(objList.begin(), objList.end(), [](const ImTable::ComboEntry &a, const ImTable::ComboEntry &b) {
                auto order = std::lexicographical_compare_three_way(
                  a.name.begin(), a.name.end(), b.name.begin(), b.name.end(),
                  [](unsigned char ca, unsigned char cb) { return std::tolower(ca) <=> std::tolower(cb); }
                );
                return order != 0 ? order < 0 : a.value < b.value;
              });

              // objects with the same name (e.g. a copied "Spline Path") get their uuid appended to tell them apart
              std::unordered_map<std::string, int> nameCount{};
              for (const auto &e : objList)++nameCount[e.name];
              for (auto &e : objList) {
                if (nameCount[e.name] > 1)e.name += " (#" + std::to_string(e.value) + ")";
              }
              const auto validCount = std::ssize(objList);

              // Show a reference that is filtered out or gone instead of "[None]"
              bool isListed = std::any_of(objList.begin(), objList.end(), [uuid](const ImTable::ComboEntry &e) {
                return e.value == uuid;
              });
              if (uuid != 0 && !isListed) {
                auto refObj = scene ? scene->getObjectByUUID(static_cast<uint32_t>(uuid)) : nullptr;
                objList.push_back({
                .value = static_cast<uint32_t>(uuid),
                .name = refObj ? (refObj->name + " (invalid)") : ("<missing " + std::to_string(uuid) + ">"),
                });
              }

              // combo adds a leading "[None]" row to clear the reference,
              // dropped objects are only accepted if they are in the (filtered) list
              ImTable::addObjectVecComboBox("", objList, uuid,
                [&objList, validCount](uint32_t dropId) {
                  return std::any_of(objList.begin(), objList.begin() + validCount, [dropId](const ImTable::ComboEntry &e) {
                    return e.value == dropId;
                  });
                },
                validationFunc, true
              );

              // a misspelled component name filters nothing, show that instead of silently listing everything
              if (!requireWarning.empty()) {
                ImGui::TextColored({1.0f, 0.6f, 0.2f, 1.0f}, ICON_MDI_ALERT " %s", requireWarning.c_str());
                ImGui::SetItemTooltip(
                  "P64::RequireComponent must name a component exactly as listed under \"Add Component\", e.g. \"Spline\".\n"
                  "Until then, all objects are offered."
                );
              }
            } else if(field.type == Utils::DataType::PREFAB) {
              const auto &prefabs = ctx.project->getAssets().getTypeEntries(FileType::PREFAB);
              ImTable::addAssetVecComboBox("", prefabs, uuid, validationFunc);
            }
            ImGui::PopID();
          } else if(field.type == Utils::DataType::VEC3) {
            drawArg([&](std::string *val) -> bool {
              auto values = Utils::parseFloatList(*val);
              values.resize(3, 0.0f);
              if (ImGui::InputFloat3("##", values.data())) {
                *val = Utils::floatListToString(values.data(), 3);
                return true;
              }
              return false;
            });
          } else if(field.type == Utils::DataType::QUAT) {
            drawArg([&](std::string *val) -> bool {
              auto values = Utils::parseFloatList(*val);
              if (values.empty()) values = {0,0,0,1};
              values.resize(4, 0.0f);
              if (ImGui::InputFloat4("##", values.data())) {
                *val = Utils::floatListToString(values.data(), 4);
                return true;
              }
              return false;
            });
          } else if(!field.enumValues.empty()) {
            drawArg([&](std::string *val) -> bool {
              // an empty value means the default from the script (or 0 without one), same as in build()
              std::string valStr = val->empty() ? field.defaultValue : *val;
              if (valStr.empty())valStr = "0";
              // decimal like build() reads it, 64-bit so every uint32_t value fits
              int64_t current = 0;
              bool isNumber = true;
              try {
                current = std::stoll(valStr);
              } catch (...) {
                isNumber = false;
              }

              std::string preview = isNumber ? std::to_string(current) : valStr;
              for (auto &[value, label] : field.enumValues) {
                if (isNumber && value == current) {
                  preview = label;
                  break;
                }
              }

              bool changed = false;
              if (ImGui::BeginCombo("##enum", preview.c_str())) {
                for (size_t i = 0; i < field.enumValues.size(); ++i) {
                  auto &[value, label] = field.enumValues[i];
                  bool selected = isNumber && value == current;
                  ImGui::PushID(static_cast<int>(i));
                  if (ImGui::Selectable(label.c_str(), selected) && !selected) {
                    *val = std::to_string(value);
                    changed = true;
                  }
                  if (selected)ImGui::SetItemDefaultFocus();
                  ImGui::PopID();
                }
                ImGui::EndCombo();
              }
              return changed;
            });
          } else if(!field.bitmask.empty()) {
            drawArg([&](std::string *val) -> bool {
              uint32_t mask = val->empty() ? 0u : static_cast<uint32_t>(Utils::parseU64(*val));
              if (ImTable::bitMaskCombo("##bitmask", mask, field.bitmask)) {
                *val = std::to_string(mask);
                return true;
              }
              return false;
            });
          } else {
            drawArg([](std::string *val) -> bool {
              return ImTable::typedInput<std::string>(val);
            });
          }
        }
      }

      // Need to open the Script ComboBox (the component is being added)
      if (data.openScriptComboBox) {
        ImTable::unfoldComboBox("##Script");
        data.openScriptComboBox = false;
      }

      ImTable::end();
    }
  }
}
