/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: dialog trees (docs/todo/dialog/PLAN.md, section 7.5). The project's speakers,
// locale/speakers.json: a name (a text key), a default language, portraits, a voice blip, a
// name color and font set, and a talk animation.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "json.hpp"

namespace Project::Dialog
{
  struct Speaker
  {
    std::string id{};             // "tank", used by line nodes
    std::string name{};           // the key of the name, "speaker.tank" by default
    std::string language{"game"}; // game or paired: the language of the speaker's lines
    std::string nameColor{"#ffffff"};
    std::string fonts{};          // a font set for the speaker's lines, "" for the language's
    std::string talkClip{};       // the animation the speaker's model plays while talking
    uint64_t blipSound{0};        // an audio asset, 0 for none
    int blipEvery{2};             // a blip every n letters
    float blipPitch{0.1f};        // random pitch spread
    std::vector<std::pair<std::string, uint64_t>> portraits{}; // name -> image asset
  };

  struct Speakers
  {
    std::vector<Speaker> list{};

    [[nodiscard]] int indexOf(const std::string &id) const;
    [[nodiscard]] const Speaker* find(const std::string &id) const;
  };

  bool parseSpeakers(const nlohmann::json &doc, Speakers &out, std::vector<std::string> &errors);
  std::string serializeSpeakers(const Speakers &speakers);

  /** <project>/locale/speakers.json */
  std::string speakersPath(const std::string &projectPath);
  bool loadSpeakers(const std::string &projectPath, Speakers &out, std::vector<std::string> &errors);
  bool saveSpeakers(const std::string &projectPath, const Speakers &speakers);
}
