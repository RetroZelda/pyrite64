/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: dialog trees (docs/todo/dialog/PLAN.md, section 7.5). The Speakers window: the project's
// speakers (locale/speakers.json), each with its name in every language, its default language,
// portraits, voice blip, name color, font set and talk animation.
#pragma once
#include <memory>

namespace Editor
{
  class SpeakersWindow
  {
    public:
      SpeakersWindow();
      ~SpeakersWindow();
      void draw();

    private:
      struct State;
      std::unique_ptr<State> state;
  };
}
