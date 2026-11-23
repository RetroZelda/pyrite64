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
#include "../../canvasHistory.h"
#include "../../../project/canvas/canvas.h"
#include "../../../project/project.h"
#include "../../../project/scene/object.h"
#include "../../../project/component/components.h"
#include "../../../project/events/eventBindings.h"
#include "../../../project/events/eventNesting.h"
#include "../../../project/events/eventPins.h"
#include "../../../project/events/eventRules.h"

namespace
{
  using Project::Events::Override;
  using Project::Events::Exposed;

  constexpr ImVec4 COLOR_PROBLEM{1.0f, 0.45f, 0.4f, 1.0f};

  // where edits are recorded: the scene's undo history, or a canvas's while drawing one
  void (*markFn)(const char*) = nullptr;
  const char* defaultNamespace = "scene::";
  void mark(const char* what)
  {
    if(markFn) markFn(what);
    else Editor::UndoRedo::getHistory().markChanged(what);
  }

  // names offered by the picker: engine catalog plus every name used in the open scene
  std::vector<std::string> knownNames{};
  uint64_t knownNamesFrame{~0ull};

  void collectNames(const Project::Object &obj, std::set<std::string> &names)
  {
    auto add = [&](const Project::Events::Bindings &b) {
      for(const auto &[pin, list] : b.in) names.insert(list.begin(), list.end());
      for(const auto &[pin, list] : b.out) names.insert(list.begin(), list.end());
    };
    add(obj.events);
    for(const auto &comp : obj.components) add(comp.events);
    for(const auto &ovr : obj.eventOverrides) {
      if(ovr.isLink()) names.insert(ovr.to);
      else names.insert(ovr.add.begin(), ovr.add.end());
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
    names.erase(std::string{});
    knownNames.assign(names.begin(), names.end());
    return knownNames;
  }

  // the "pick a known event" button and popup; true when 'name' was replaced
  bool drawPicker(std::string &name)
  {
    bool changed = false;
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
    return changed;
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
    if(!problem.empty())ImGui::PushStyleColor(ImGuiCol_Text, COLOR_PROBLEM);
    ImGui::InputTextWithHint("##name", "namespace::Name", &name);
    if(!problem.empty())ImGui::PopStyleColor();
    if(ImGui::IsItemDeactivatedAfterEdit()) changed = true;
    if(!problem.empty())ImGui::SetItemTooltip("%s", problem.c_str());

    ImGui::SameLine();
    if(drawPicker(name)) changed = true;

    ImGui::SameLine();
    if(ImGui::Button(ICON_MDI_CLOSE "##remove")) { remove = true; changed = true; }
    ImGui::SetItemTooltip("Remove this binding");

    if(readOnly)ImGui::EndDisabled();
    ImGui::PopID();
    return changed;
  }

  // ---- instance overrides (PLAN 6.2)

  Override* findOverride(Project::Object &instance, const std::vector<uint32_t> &path, uint64_t comp, const std::string &pin, bool input)
  {
    for(auto &ovr : instance.eventOverrides) {
      if(!ovr.isLink() && ovr.path == path && ovr.comp == comp && ovr.pin == pin && ovr.input == input) return &ovr;
    }
    return nullptr;
  }

  Override& ensureOverride(Project::Object &instance, const std::vector<uint32_t> &path, uint64_t comp, const std::string &pin, bool input)
  {
    if(auto *ovr = findOverride(instance, path, comp, pin, input)) return *ovr;
    Override ovr{};
    ovr.path = path;
    ovr.comp = comp;
    ovr.pin = pin;
    ovr.input = input;
    instance.eventOverrides.push_back(ovr);
    return instance.eventOverrides.back();
  }

  // drops empty names, and overrides that no longer change anything
  void pruneOverrides(Project::Object &instance)
  {
    for(auto &ovr : instance.eventOverrides) {
      std::erase(ovr.add, std::string{});
    }
    std::erase_if(instance.eventOverrides, [](const Override &o) { return o.empty(); });
  }

  // ---- exposed pins (PLAN 6.2)

  bool isExposed(const Project::Object &root, const std::vector<uint32_t> &path, uint64_t comp, const std::string &pin, bool input)
  {
    return std::any_of(root.eventExposed.begin(), root.eventExposed.end(), [&](const Exposed &e) {
      return e.path == path && e.comp == comp && e.pin == pin && e.input == input;
    });
  }

  void setExposed(Project::Object &root, const std::vector<uint32_t> &path, uint64_t comp, const std::string &pin, bool input, bool exposed)
  {
    std::erase_if(root.eventExposed, [&](const Exposed &e) {
      return e.path == path && e.comp == comp && e.pin == pin && e.input == input;
    });
    if(exposed) root.eventExposed.push_back({path, comp, pin, input, {}});
  }

  struct Section
  {
    Project::Events::Bindings &bindings;
    const Project::Events::PinList &pins;
    uint64_t comp; // component uuid, 0 for the object's own pins
    bool readOnly;
    const Editor::EventInspector::Target &target;
  };

  // one pin's bindings from the prefab, each with a mute toggle; and the instance's own
  bool drawOverridePin(const Section &sec, const std::string &pin, bool input)
  {
    bool changed = false;
    auto &instance = *sec.target.instance;
    const auto &map = input ? sec.bindings.in : sec.bindings.out;

    auto it = map.find(pin);
    if(it != map.end()) {
      for(const auto &name : it->second) {
        ImGui::PushID(name.c_str());
        auto *ovr = findOverride(instance, sec.target.path, sec.comp, pin, input);
        const bool muted = ovr && std::find(ovr->mute.begin(), ovr->mute.end(), name) != ovr->mute.end();

        if(sec.readOnly)ImGui::BeginDisabled();
        if(ImGui::Button(muted ? ICON_MDI_EYE_OFF "##mute" : ICON_MDI_EYE "##mute")) {
          mark(muted ? "Unmute Event Binding" : "Mute Event Binding");
          auto &target = ensureOverride(instance, sec.target.path, sec.comp, pin, input);
          if(muted) std::erase(target.mute, name);
          else target.mute.push_back(name);
          changed = true;
        }
        ImGui::SetItemTooltip(muted ? "Muted on this instance: click to hear it again"
                                    : "From the prefab: click to mute it on this instance");
        if(sec.readOnly)ImGui::EndDisabled();
        ImGui::SameLine();
        if(muted) ImGui::TextDisabled("%s (muted)", name.c_str());
        else ImGui::TextUnformatted(name.c_str());
        ImGui::PopID();
      }
    }

    // bindings this instance adds; the pointer is looked up again after every change
    if(auto *ovr = findOverride(instance, sec.target.path, sec.comp, pin, input)) {
      for(size_t i = 0; i < ovr->add.size(); ++i) {
        bool remove = false;
        if(drawBindingRow(ovr->add[i], sec.readOnly, remove)) {
          mark(remove ? "Remove Event Binding" : "Edit Event Binding");
          changed = true;
        }
        if(remove) {
          ovr->add.erase(ovr->add.begin() + (long)i);
          --i;
        }
      }
    }
    return changed;
  }

  bool drawPins(const Section &sec, bool input)
  {
    bool changed = false;
    auto &map = input ? sec.bindings.in : sec.bindings.out;
    const bool overrides = sec.target.instance != nullptr;

    // declared pins first, then pins that only exist in the file (the script lost them)
    std::vector<std::pair<std::string, bool>> rows{};
    for(const auto &pin : input ? sec.pins.in : sec.pins.out) rows.push_back({pin, true});
    for(const auto &[pin, names] : map) {
      if(sec.pins.indexOf(input, pin) < 0) rows.push_back({pin, false});
    }

    for(const auto &[pin, declared] : rows)
    {
      ImGui::PushID(pin.c_str());
      ImGui::PushID(input ? "in" : "out");
      if(!declared)ImGui::PushStyleColor(ImGuiCol_Text, COLOR_PROBLEM);
      ImGui::TextUnformatted(input ? ICON_MDI_ARROW_RIGHT_BOLD " " : ICON_MDI_ARROW_LEFT_BOLD " ");
      ImGui::SameLine(0, 0);
      ImGui::TextUnformatted(pin.c_str());
      if(!declared) {
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("This pin is no longer declared; the bindings are kept but do nothing");
      } else {
        ImGui::SetItemTooltip(input ? "Input: receives the events listed below" : "Output: fires the events listed below");
      }

      if(!sec.readOnly) {
        ImGui::SameLine();
        if(ImGui::SmallButton(ICON_MDI_PLUS "##add")) {
          mark("Add Event Binding");
          std::string fresh = Project::Events::namespaceOf(pin).empty() ? std::string{defaultNamespace} : std::string{};
          if(overrides) ensureOverride(*sec.target.instance, sec.target.path, sec.comp, pin, input).add.push_back(fresh);
          else map[pin].push_back(fresh);
          changed = true;
        }
        ImGui::SetItemTooltip(overrides ? "Bind this pin to an event, for this instance only" : "Bind this pin to an event");
      }

      if(sec.target.exposeRoot && declared) {
        auto &root = *sec.target.exposeRoot;
        bool exposed = isExposed(root, sec.target.exposePath, sec.comp, pin, input);
        ImGui::SameLine();
        if(!exposed)ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.45f);
        if(ImGui::SmallButton(ICON_MDI_EXPORT "##expose")) {
          mark(exposed ? "Hide Event Pin" : "Expose Event Pin");
          setExposed(root, sec.target.exposePath, sec.comp, pin, input, !exposed);
          changed = true;
        }
        if(!exposed)ImGui::PopStyleVar();
        ImGui::SetItemTooltip(exposed ? "Exposed: shown on this prefab's instances in outer graphs. Click to hide it"
                                      : "Expose this pin on the prefab's instances in outer graphs");
      }

      ImGui::Indent();
      // Fork: conditions. The events the script itself binds to Check (P64_CONDITION_TRIGGERS).
      if(input && declared && pin == "Check" && sec.pins.isConditionPin(true, pin)) {
        for(const auto &trigger : sec.pins.triggers) {
          ImGui::TextDisabled(ICON_MDI_LOCK " %s", trigger.c_str());
          ImGui::SetItemTooltip("Set in the script (P64_CONDITION_TRIGGERS): the condition checks itself when this event fires");
        }
      }
      if(overrides) {
        changed |= drawOverridePin(sec, pin, input);
      } else {
        auto it = map.find(pin);
        if(it != map.end()) {
          auto &names = it->second;
          for(size_t i = 0; i < names.size(); ++i) {
            bool remove = false;
            if(drawBindingRow(names[i], sec.readOnly, remove)) {
              mark(remove ? "Remove Event Binding" : "Edit Event Binding");
              changed = true;
            }
            if(remove) {
              names.erase(names.begin() + (long)i);
              --i;
            }
          }
        }
      }
      ImGui::Unindent();
      ImGui::PopID();
      ImGui::PopID();
    }
    return changed;
  }

  size_t overrideCount(const Section &sec)
  {
    if(!sec.target.instance)return 0;
    size_t count = 0;
    for(const auto &ovr : sec.target.instance->eventOverrides) {
      if(!ovr.isLink() && ovr.path == sec.target.path && ovr.comp == sec.comp) count += ovr.add.size() + ovr.mute.size();
    }
    return count;
  }

  void drawSection(const Section &sec)
  {
    if(sec.pins.empty() && sec.bindings.empty())return;

    const auto count = sec.bindings.count();
    const auto changes = overrideCount(sec);
    std::string label = std::string(ICON_MDI_TRANSIT_CONNECTION_VARIANT " Events");
    if(count > 0) label += " (" + std::to_string(count) + " bound)";
    if(changes > 0) label += " (" + std::to_string(changes) + " changed here)";
    label += "###events";

    ImGui::PushID("events");
    if(ImGui::TreeNodeEx(label.c_str(), (count + changes) > 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0))
    {
      if(sec.target.instance) {
        ImGui::TextDisabled(ICON_MDI_INFORMATION_OUTLINE " From the prefab; changes apply to '%s' only",
                            sec.target.instance->name.c_str());
      }
      bool changed = drawPins(sec, true);
      changed |= drawPins(sec, false);
      // drop empty rows once an edit is finished, so files only keep real bindings
      if(changed && !ImGui::IsAnyItemActive()) {
        if(sec.target.instance) {
          pruneOverrides(*sec.target.instance);
        } else {
          for(auto *map : {&sec.bindings.in, &sec.bindings.out}) {
            for(auto &[pin, names] : *map) std::erase(names, std::string{});
          }
        }
      }
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
}

void Editor::EventInspector::drawComponent(Project::Object &obj, Project::Component::Entry &comp, bool readOnly, const Target &target)
{
  (void)obj;
  if(!ctx.project)return;
  auto pins = Project::Events::pinsOf(*ctx.project, comp);
  drawSection({comp.events, pins, comp.uuid, readOnly, target});
}

void Editor::EventInspector::drawObject(Project::Object &obj, bool readOnly, const Target &target)
{
  drawSection({obj.events, Project::Events::objectPins(), 0, readOnly, target});
}

void Editor::EventInspector::drawLinks(Project::Object &instance, bool readOnly, bool inScene)
{
  if(!ctx.project || !instance.isPrefabInstance())return;
  const auto links = Project::Events::Nesting::parentLinks(*ctx.project, instance.uuidPrefab.value);
  if(links.empty())return;

  std::string label = ICON_MDI_LINK_VARIANT "  Parent Links (" + std::to_string(links.size()) + ")###parentLinks";
  const bool repointed = std::any_of(instance.eventOverrides.begin(), instance.eventOverrides.end(),
                                     [](const Override &o) { return o.isLink(); });
  if(!ImGui::CollapsingHeader(label.c_str(), repointed ? ImGuiTreeNodeFlags_DefaultOpen : 0))return;

  ImGui::TextDisabled(ICON_MDI_INFORMATION_OUTLINE " Events of the prefabs around '%s' that its content uses", instance.name.c_str());

  auto findLink = [&](const std::string &link) -> Override* {
    for(auto &o : instance.eventOverrides) {
      if(o.isLink() && o.link == link) return &o;
    }
    return nullptr;
  };
  // written as it is typed (undo merges the keystrokes); empty and default links are dropped when done
  auto setLink = [&](const std::string &link, const std::string &to) {
    if(auto *o = findLink(link)) o->to = to;
    else instance.eventOverrides.push_back({.link = link, .to = to});
  };

  ImGui::PushID("parentLinks");
  bool finished = false;
  for(const auto &link : links)
  {
    ImGui::PushID(link.c_str());
    const auto fallback = Project::Events::shiftPrefabNumber(link, -1);
    auto *ovr = findLink(link);

    ImGui::TextUnformatted(link.c_str());
    ImGui::SetItemTooltip("The name as the prefab writes it");
    ImGui::Indent();

    std::string to = ovr ? ovr->to : std::string{};
    const std::string hint = fallback + (inScene ? " (reaches nothing here)" : "");
    const auto problem = to.empty() ? std::string{} : Project::Events::nameProblem(to);

    if(readOnly)ImGui::BeginDisabled();
    const float buttons = ImGui::GetFrameHeight() * 2 + ImGui::GetStyle().ItemSpacing.x * 2;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttons);
    if(!problem.empty())ImGui::PushStyleColor(ImGuiCol_Text, COLOR_PROBLEM);
    if(ImGui::InputTextWithHint("##to", hint.c_str(), &to)) {
      mark("Repoint Parent Link");
      setLink(link, to);
    }
    if(!problem.empty())ImGui::PopStyleColor();
    finished |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SetItemTooltip("%s", problem.empty()
      ? "Where this link points for this instance. Empty: the same name one prefab up"
      : problem.c_str());

    ImGui::SameLine();
    if(drawPicker(to)) {
      mark("Repoint Parent Link");
      setLink(link, to);
      finished = true;
    }
    ImGui::SameLine();
    if(ImGui::Button(ICON_MDI_CLOSE "##reset") && ovr) {
      mark("Reset Parent Link");
      setLink(link, {});
      finished = true;
    }
    ImGui::SetItemTooltip("Point it back at the same name one prefab up");
    if(readOnly)ImGui::EndDisabled();

    ImGui::Unindent();
    ImGui::PopID();
  }
  ImGui::PopID();

  if(finished) {
    std::erase_if(instance.eventOverrides, [](const Override &o) {
      return o.isLink() && (o.to.empty() || o.to == Project::Events::shiftPrefabNumber(o.link, -1));
    });
  }
}

void Editor::EventInspector::drawCanvas(Project::Canvas &canvas)
{
  const auto pins = Project::Events::canvasPins(canvas);
  markFn = [](const char* what) { Editor::CanvasHistory::markChanged(what); };
  defaultNamespace = "ui::";
  ImGui::PushID("canvasPins");
  if(ImGui::CollapsingHeader(ICON_MDI_TRANSIT_CONNECTION_VARIANT "  Event Pins", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::TextDisabled(ICON_MDI_INFORMATION_OUTLINE " A canvas can use ui::, scene::, global:: and game namespaces");
    drawSection({canvas.bindings, pins, 0, false, Target{}});
  }
  ImGui::PopID();
  markFn = nullptr;
  defaultNamespace = "scene::";
}
