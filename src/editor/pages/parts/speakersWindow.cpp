/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: dialog trees (docs/todo/dialog/PLAN.md, section 7.5).
#include "speakersWindow.h"

#include <format>

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "IconsMaterialDesignIcons.h"

#include "../../../context.h"
#include "../../../project/project.h"
#include "../../../project/dialog/speakers.h"
#include "../../../project/locale/locProject.h"

namespace
{
  bool assetCombo(const char* id, Project::FileType type, uint64_t &uuid, const char* none)
  {
    auto *cur = uuid ? ctx.project->getAssets().getEntryByUUID(uuid) : nullptr;
    bool changed = false;
    if(ImGui::BeginCombo(id, cur ? cur->name.c_str() : none)) {
      if(ImGui::Selectable(none, uuid == 0)) { uuid = 0; changed = true; }
      for(const auto &e : ctx.project->getAssets().getTypeEntries(type)) {
        if(ImGui::Selectable(e.name.c_str(), e.getUUID() == uuid)) { uuid = e.getUUID(); changed = true; }
      }
      ImGui::EndCombo();
    }
    return changed;
  }

  float hexToFloat(const std::string &hex, int at)
  {
    try { return static_cast<float>(std::stoul(hex.substr(at, 2), nullptr, 16)) / 255.0f; } catch(...) { return 1.0f; }
  }
}

struct Editor::SpeakersWindow::State
{
  std::string projectPath{};
  Project::Dialog::Speakers speakers{};
  std::vector<std::string> errors{};
  int selected{-1};
  std::string newId{};
  std::string nameBuf{};
  int nameFor{-1};
  bool nameActive{false};
};

Editor::SpeakersWindow::SpeakersWindow() : state{std::make_unique<State>()} {}
Editor::SpeakersWindow::~SpeakersWindow() = default;

void Editor::SpeakersWindow::draw()
{
  auto &st = *state;
  if(!ctx.project) { ImGui::TextDisabled("No project is open"); return; }
  if(st.projectPath != ctx.project->getPath()) {
    st = State{};
    st.projectPath = ctx.project->getPath();
    Project::Dialog::loadSpeakers(st.projectPath, st.speakers, st.errors);
  }
  bool changed = false;

  ImGui::BeginChild("##list", ImVec2(180, 0), ImGuiChildFlags_Borders);
  for(size_t i = 0; i < st.speakers.list.size(); ++i) {
    if(ImGui::Selectable(st.speakers.list[i].id.c_str(), st.selected == static_cast<int>(i))) st.selected = static_cast<int>(i);
  }
  ImGui::Separator();
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##newid", "new speaker id", &st.newId);
  const bool idOk = !st.newId.empty() && Project::Loc::isValidKey(st.newId) && st.newId.find('.') == std::string::npos
                    && st.speakers.indexOf(st.newId) < 0;
  ImGui::BeginDisabled(!idOk);
  if(ImGui::Button(ICON_MDI_PLUS " Add speaker", ImVec2(-1, 0))) {
    Project::Dialog::Speaker s{.id = st.newId, .name = "speaker." + st.newId};
    st.speakers.list.push_back(s);
    st.selected = static_cast<int>(st.speakers.list.size()) - 1;
    st.newId.clear();
    changed = true;
  }
  ImGui::EndDisabled();
  for(const auto &e : st.errors) ImGui::TextColored({1, 0.45f, 0.4f, 1}, "%s", e.c_str());
  ImGui::EndChild();
  ImGui::SameLine();

  ImGui::BeginChild("##detail", ImVec2(0, 0));
  if(st.selected >= 0 && st.selected < static_cast<int>(st.speakers.list.size())) {
    auto &s = st.speakers.list[st.selected];
    ImGui::Text(ICON_MDI_ACCOUNT_VOICE " %s", s.id.c_str());
    ImGui::SameLine();
    if(ImGui::SmallButton(ICON_MDI_DELETE " Remove")) {
      st.speakers.list.erase(st.speakers.list.begin() + st.selected);
      st.selected = -1;
      changed = true;
      ImGui::EndChild();
      Project::Dialog::saveSpeakers(st.projectPath, st.speakers);
      return;
    }

    ImGui::PushItemWidth(240);
    // the name, in the source language (other languages in the Strings window)
    if(Project::Loc::hasLocalization(st.projectPath)) {
      auto &loc = Project::Loc::edit(*ctx.project);
      const int src = loc.sourceIndex();
      if(!st.nameActive || st.nameFor != st.selected) {
        st.nameFor = st.selected;
        st.nameBuf.clear();
        if(src >= 0) {
          auto it = loc.tables[src].entries.find(s.name);
          if(it != loc.tables[src].entries.end()) st.nameBuf = it->second.text;
        }
      }
      ImGui::InputText("Name", &st.nameBuf);
      if(ImGui::IsItemActivated()) st.nameActive = true;
      if(ImGui::IsItemDeactivated()) st.nameActive = false;
      if(ImGui::IsItemDeactivatedAfterEdit() && src >= 0) {
        Project::Loc::Uses one{};
        one[s.name].push_back({"speaker " + s.id, {}, 0});
        loc.addMissing(one);
        loc.setText(s.name, static_cast<size_t>(src), st.nameBuf);
        Project::Loc::saveCached(*ctx.project);
      }
      ImGui::SameLine();
      ImGui::TextDisabled("%s", s.name.c_str());
    } else {
      ImGui::TextDisabled("Name key: %s (make locale/ in the Strings window to give it a text)", s.name.c_str());
    }

    int lang = s.language == "paired" ? 1 : 0;
    const char* langs[] = {"the game's language", "the paired language"};
    if(ImGui::Combo("Speaks", &lang, langs, 2)) { s.language = lang ? "paired" : "game"; changed = true; }
    ImGui::SetItemTooltip("The language of this speaker's lines, unless a line says otherwise (the manager's wife speaks the paired one)");

    float col[4] = {hexToFloat(s.nameColor, 1), hexToFloat(s.nameColor, 3), hexToFloat(s.nameColor, 5), 1.0f};
    if(ImGui::ColorEdit3("Name color", col, ImGuiColorEditFlags_NoInputs)) {
      s.nameColor = std::format("#{:02x}{:02x}{:02x}", int(col[0] * 255.0f + 0.5f), int(col[1] * 255.0f + 0.5f), int(col[2] * 255.0f + 0.5f));
      changed = true;
    }
    if(ImGui::InputTextWithHint("Font set", "the language's own", &s.fonts)) changed = true;
    if(ImGui::InputTextWithHint("Talk clip", "animation while talking", &s.talkClip)) changed = true;

    ImGui::SeparatorText("Voice blip");
    if(assetCombo("Sound", Project::FileType::AUDIO, s.blipSound, "none")) changed = true;
    if(ImGui::SliderInt("Every n letters", &s.blipEvery, 1, 8)) changed = true;
    if(ImGui::SliderFloat("Pitch spread", &s.blipPitch, 0.0f, 0.5f, "%.2f")) changed = true;

    ImGui::SeparatorText("Portraits");
    int remove = -1;
    for(size_t i = 0; i < s.portraits.size(); ++i) {
      ImGui::PushID(static_cast<int>(i));
      ImGui::SetNextItemWidth(100);
      if(ImGui::InputText("##pname", &s.portraits[i].first)) changed = true;
      ImGui::SameLine();
      ImGui::SetNextItemWidth(180);
      if(assetCombo("##pimg", Project::FileType::IMAGE, s.portraits[i].second, "no image")) changed = true;
      ImGui::SameLine();
      if(ImGui::SmallButton(ICON_MDI_CLOSE)) remove = static_cast<int>(i);
      if(i == 0) { ImGui::SameLine(); ImGui::TextDisabled("(default)"); }
      ImGui::PopID();
    }
    if(remove >= 0) { s.portraits.erase(s.portraits.begin() + remove); changed = true; }
    if(ImGui::SmallButton(ICON_MDI_PLUS " portrait")) {
      s.portraits.emplace_back(s.portraits.empty() ? "normal" : "portrait" + std::to_string(s.portraits.size() + 1), 0);
      changed = true;
    }
    ImGui::PopItemWidth();
  } else {
    ImGui::TextDisabled("Pick a speaker, or add one. Lines in dialog trees name their speaker by id.");
  }
  ImGui::EndChild();

  if(changed) Project::Dialog::saveSpeakers(st.projectPath, st.speakers);
}
