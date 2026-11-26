/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: dialog trees (docs/todo/dialog/PLAN.md, section 7.5).
#include "speakers.h"

#include <filesystem>

#include "../../utils/fs.h"
#include "../../utils/prop.h"
#include "../../utils/json.h"

namespace fs = std::filesystem;

int Project::Dialog::Speakers::indexOf(const std::string &id) const
{
  for(size_t i = 0; i < list.size(); ++i) if(list[i].id == id) return static_cast<int>(i);
  return -1;
}

const Project::Dialog::Speaker* Project::Dialog::Speakers::find(const std::string &id) const
{
  const int i = indexOf(id);
  return i < 0 ? nullptr : &list[i];
}

bool Project::Dialog::parseSpeakers(const nlohmann::json &doc, Speakers &out, std::vector<std::string> &errors)
{
  out = {};
  if(doc.is_null()) return true;
  if(!doc.is_object() || !doc.contains("speakers") || !doc["speakers"].is_array()) {
    errors.push_back("speakers.json: expected {\"speakers\": [...]}");
    return false;
  }
  for(const auto &j : doc["speakers"]) {
    Speaker s{};
    s.id = j.value("id", std::string{});
    if(s.id.empty()) { errors.push_back("speakers.json: a speaker has no id"); continue; }
    if(out.indexOf(s.id) >= 0) { errors.push_back("speakers.json: the id '" + s.id + "' is used twice"); continue; }
    s.name = j.value("name", "speaker." + s.id);
    s.language = j.value("language", std::string{"game"});
    if(s.language != "game" && s.language != "paired") {
      errors.push_back("speakers.json: '" + s.id + "': language must be game or paired");
      s.language = "game";
    }
    s.nameColor = j.value("nameColor", std::string{"#ffffff"});
    s.fonts = j.value("fonts", std::string{});
    s.talkClip = j.value("talkClip", std::string{});
    if(j.contains("blip") && j["blip"].is_object()) {
      const auto &b = j["blip"];
      const auto snd = b.value("sound", std::string{"0"});
      try { s.blipSound = std::stoull(snd); } catch(...) { s.blipSound = 0; }
      s.blipEvery = b.value("every", 2);
      s.blipPitch = b.value("pitch", 0.1f);
    }
    if(j.contains("portraits") && j["portraits"].is_array()) {
      for(const auto &p : j["portraits"]) {
        uint64_t asset = 0;
        try { asset = std::stoull(p.value("image", std::string{"0"})); } catch(...) {}
        s.portraits.emplace_back(p.value("name", std::string{"normal"}), asset);
      }
    }
    out.list.push_back(std::move(s));
  }
  return true;
}

std::string Project::Dialog::serializeSpeakers(const Speakers &speakers)
{
  auto list = nlohmann::json::array();
  for(const auto &s : speakers.list) {
    nlohmann::json j = nlohmann::json::object();
    j["id"] = s.id;
    j["name"] = s.name;
    j["language"] = s.language;
    j["nameColor"] = s.nameColor;
    if(!s.fonts.empty()) j["fonts"] = s.fonts;
    if(!s.talkClip.empty()) j["talkClip"] = s.talkClip;
    j["blip"] = {{"sound", std::to_string(s.blipSound)}, {"every", s.blipEvery}, {"pitch", s.blipPitch}};
    auto portraits = nlohmann::json::array();
    for(const auto &[name, asset] : s.portraits) portraits.push_back({{"name", name}, {"image", std::to_string(asset)}});
    j["portraits"] = portraits;
    list.push_back(j);
  }
  nlohmann::json doc = {{"speakers", list}};
  return doc.dump(2) + "\n";
}

std::string Project::Dialog::speakersPath(const std::string &projectPath)
{
  return (fs::path{projectPath} / "locale" / "speakers.json").string();
}

bool Project::Dialog::loadSpeakers(const std::string &projectPath, Speakers &out, std::vector<std::string> &errors)
{
  out = {};
  const auto path = speakersPath(projectPath);
  if(!fs::exists(path)) return true;
  try {
    return parseSpeakers(Utils::JSON::loadFile(path), out, errors);
  } catch(const std::exception &e) {
    errors.push_back(std::string("speakers.json: ") + e.what());
    return false;
  }
}

bool Project::Dialog::saveSpeakers(const std::string &projectPath, const Speakers &speakers)
{
  std::error_code ec{};
  fs::create_directories(fs::path{projectPath} / "locale", ec);
  return Utils::FS::saveTextFileIfChanged(speakersPath(projectPath), serializeSpeakers(speakers));
}
