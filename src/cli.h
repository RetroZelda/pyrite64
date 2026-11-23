/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <string>

namespace CLI
{
  enum Result
  {
    SUCCESS,
    ERROR,
    GUI
  };

  const std::string& getProjectPath();
  /** Fork: GUI start-up selection from --scene and --object (0 and empty when not given). */
  int getStartScene();
  const std::string& getStartObject();
  const std::string& getStartWindow();
  bool getStartEdit();
  const std::string& getUiScript();
  bool isExperimentalEnabled();
  Result run(int argc, char** argv);
};
