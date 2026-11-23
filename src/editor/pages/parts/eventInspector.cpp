/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
#include "eventInspector.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "IconsMaterialDesignIcons.h"

#include "../../../context.h"
#include "../../undoRedo.h"
#include "../../../project/project.h"
#include "../../../project/scene/object.h"
#include "../../../project/component/components.h"
#include "../../../project/events/eventBindings.h"
#include "../../../project/events/eventPins.h"
#include "../../../project/events/eventRules.h"

namespace
{
  // names offered by the picker: engine catalog plus every name used in the open scene
  std::vector<std::string> knownNames{};
  uint64_t knownNamesFrame{~0ull};

  void collectNames(const Project::Object &obj, std::set<std::string> &names)
  {
    for(const auto *bindings : {&obj.events}) {
      for(const auto &[pin, list] : bindings->in) names.insert(list.begin(), list.end());
      for(const auto &[pin, list] : bindings->out) names.insert(list.begin(), list.end());
    }
    for(const auto &comp : obj.components) {
      for(const auto &[pin, list] : comp.events.in) names.insert(list.begin(), list.end());
      for(const auto &[pin, list] : comp.events.out) names.insert(list.begin(), list.end());
    }
    for(const auto &child : obj.children) collectNames(*child, names);
  }

  const std::vector<std::string>& getKnownNames()
  {
    // refreshed once per frame at most
    auto frame = (uint64_t)ImGui::GetFrameCount();
    if(frame == knownNamesFrame)return knownNames;
    knownNamesFrame = frame;

    std::set<std::string> names{};
    static Project::Events::Catalog engineCatalog{};
    static bool engineLoaded = false;
    if(!engineLoaded) {
      std::vector<std::string> errors{};
      engineCatalog.loadDir("data/events", true, errors);
      engineLoaded = true;
    }
    for(const auto &[name, ev] : engineCatalog.events) names.insert(name);

    if(ctx.project) {
      if(auto *scene = ctx.project->getScenes().getLoadedScene()) {
        collectNames(scene->getRootObject(), names);
      }
      for(const auto &script : ctx.project->getAssets().getTypeEntries(Project::FileType::CODE_GLOBAL)) {
        for(const auto &sys : script.systemPins) {
          for(const auto &pin : sys.in) names.insert("global::" + sys.system + "." + pin);
          for(const auto &pin : sys.out) names.insert("global::" + sys.system + "." + pin);
        }
      }
    }
    knownNames.assign(names.begin(), names.end());
    return knownNames;
  }

  // a row per binding: name field, picker and remove button. Returns true if anything changed.
  bool drawBindingRow(std::string &name, bool readOnly, bool &remove)
  {
    bool changed = false;
    const auto problem = Project::Events::nameProblem(name);

    ImGui::PushID(&name);
    if(readOnly)ImGui::BeginDisabled();

    const float buttons = ImGui::GetFrameHeight() * 2 + ImGui::GetStyle().ItemSpacing.x * 2;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttons);
    if(!problem.empty())ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{1.0f, 0.45f, 0.4f, 1.0f});
    ImGui::InputTextWithHint("##name", "namespace::Name", &name);
    if(!problem.empty())ImGui::PopStyleColor();
    if(ImGui::IsItemDeactivatedAfterEdit()) changed = true;
    if(!problem.empty())ImGui::SetItemTooltip("%s", problem.c_str());

    ImGui::SameLine();
    if(ImGui::Button(ICON_MDI_MENU_DOWN "##pick")) ImGui::OpenPopup("pick");
    ImGui::SetItemTooltip("Pick a known event");
    if(ImGui::BeginPopup("pick")) {
      static std::string filter{};
      ImGui::SetNextItemWidth(260);
      if(ImGui::IsWindowAppearing()) { filter.clear(); ImGui::SetKeyboardFocusHere(); }
      ImGui::InputTextWithHint("##filter", "Search", &filter);
      int shown = 0;
      for(const auto &known : getKnownNames()) {
        if(!filter.empty() && known.find(filter) == std::string::npos)continue;
        if(++shown > 40)break;
        if(ImGui::Selectable(known.c_str())) {
          name = known;
          changed = true;
          ImGui::CloseCurrentPopup();
        }
      }
      if(shown == 0)ImGui::TextDisabled("No known event matches");
      ImGui::EndPopup();
    }

    ImGui::SameLine();
    if(ImGui::Button(ICON_MDI_CLOSE "##remove")) { remove = true; changed = true; }
    ImGui::SetItemTooltip("Remove this binding");

    if(readOnly)ImGui::EndDisabled();
    ImGui::PopID();
    return changed;
  }

  bool drawPins(Project::Events::Bindings &bindings, const Project::Events::PinList &pins, bool input, bool readOnly)
  {
    bool changed = false;
    auto &map = input ? bindings.in : bindings.out;

    // declared pins first, then pins that only exist in the file (the script lost them)
    std::vector<std::pair<std::string, bool>> rows{};
    for(const auto &pin : input ? pins.in : pins.out) rows.push_back({pin, true});
    for(const auto &[pin, names] : map) {
      if(pins.indexOf(input, pin) < 0) rows.push_back({pin, false});
    }

    for(const auto &[pin, declared] : rows)
    {
      ImGui::PushID(pin.c_str());
      if(!declared)ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{1.0f, 0.45f, 0.4f, 1.0f});
      ImGui::TextUnformatted(input ? ICON_MDI_ARROW_RIGHT_BOLD " " : ICON_MDI_ARROW_LEFT_BOLD " ");
      ImGui::SameLine(0, 0);
      ImGui::TextUnformatted(pin.c_str());
      if(!declared) {
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("This pin is no longer declared; the bindings are kept but do nothing");
      } else {
        ImGui::SetItemTooltip(input ? "Input: receives the events listed below" : "Output: fires the events listed below");
      }

      if(!readOnly) {
        ImGui::SameLine();
        if(ImGui::SmallButton(ICON_MDI_PLUS "##add")) {
          Editor::UndoRedo::getHistory().markChanged("Add Event Binding");
          map[pin].push_back(Project::Events::namespaceOf(pin).empty() ? std::string{"scene::"} : std::string{});
        }
        ImGui::SetItemTooltip("Bind this pin to an event");
      }

      auto it = map.find(pin);
      if(it != map.end()) {
        ImGui::Indent();
        auto &names = it->second;
        for(size_t i = 0; i < names.size(); ++i) {
          bool remove = false;
          if(drawBindingRow(names[i], readOnly, remove)) {
            Editor::UndoRedo::getHistory().markChanged(remove ? "Remove Event Binding" : "Edit Event Binding");
            changed = true;
          }
          if(remove) {
            names.erase(names.begin() + (long)i);
            --i;
          }
        }
        ImGui::Unindent();
      }
      ImGui::PopID();
    }
    return changed;
  }

  void drawSection(Project::Events::Bindings &bindings, const Project::Events::PinList &pins, bool readOnly)
  {
    if(pins.empty() && bindings.empty())return;

    const auto count = bindings.count();
    std::string label = std::string(ICON_MDI_TRANSIT_CONNECTION_VARIANT " Events");
    if(count > 0) label += " (" + std::to_string(count) + " bound)";
    label += "###events";

    ImGui::PushID("events");
    if(ImGui::TreeNodeEx(label.c_str(), count > 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0))
    {
      bool changed = drawPins(bindings, pins, true, readOnly);
      changed |= drawPins(bindings, pins, false, readOnly);
      // drop empty rows once an edit is finished, so files only keep real bindings
      if(changed && !ImGui::IsAnyItemActive()) {
        for(auto *map : {&bindings.in, &bindings.out}) {
          for(auto &[pin, names] : *map) {
            names.erase(std::remove(names.begin(), names.end(), std::string{}), names.end());
          }
        }
      }
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
}

void Editor::EventInspector::drawComponent(Project::Object &obj, Project::Component::Entry &comp, bool readOnly)
{
  (void)obj;
  if(!ctx.project)return;
  auto pins = Project::Events::pinsOf(*ctx.project, comp);
  drawSection(comp.events, pins, readOnly);
}

void Editor::EventInspector::drawObject(Project::Object &obj, bool readOnly)
{
  drawSection(obj.events, Project::Events::objectPins(), readOnly);
}
