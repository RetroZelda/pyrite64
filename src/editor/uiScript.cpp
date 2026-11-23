/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
#include "uiScript.h"

#include <cstdio>
#include <deque>
#include <fstream>
#include <sstream>
#include <vector>

#include <SDL3/SDL.h>
#include "imgui.h"

#include "../context.h"
#include "../project/project.h"

namespace
{
  struct Step
  {
    std::string cmd{};
    std::vector<std::string> args{};
  };

  std::deque<Step> steps{};
  int waitFrames{0};
  bool quit{false};
  float mouseX{0.0f}, mouseY{0.0f};
  bool pointerSet{false}; // the script has placed the pointer at least once

  // an interpolated move in progress
  float fromX{0.0f}, fromY{0.0f}, toX{0.0f}, toY{0.0f};
  int moveStep{0}, moveSteps{0};
  std::string textBuffer{};

  float num(const Step &s, size_t i, float fallback = 0.0f)
  {
    if(i >= s.args.size()) return fallback;
    try { return std::stof(s.args[i]); } catch(...) { return fallback; }
  }

  void pushMotion(SDL_Window *window, float x, float y)
  {
    SDL_Event ev{};
    ev.type = SDL_EVENT_MOUSE_MOTION;
    ev.motion.windowID = SDL_GetWindowID(window);
    ev.motion.x = x;
    ev.motion.y = y;
    ev.motion.xrel = x - mouseX;
    ev.motion.yrel = y - mouseY;
    SDL_PushEvent(&ev);
    // keep the OS pointer in step: the ImGui backend also reads it while no button is down
    SDL_WarpMouseInWindow(window, x, y);
    mouseX = x;
    mouseY = y;
    pointerSet = true;
  }

  void pushButton(SDL_Window *window, uint8_t button, bool down)
  {
    SDL_Event ev{};
    ev.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    ev.button.windowID = SDL_GetWindowID(window);
    ev.button.button = button;
    ev.button.down = down;
    ev.button.clicks = 1;
    ev.button.x = mouseX;
    ev.button.y = mouseY;
    SDL_PushEvent(&ev);
  }

  void pushKey(SDL_Window *window, const std::string &name)
  {
    SDL_Keycode key = SDLK_UNKNOWN;
    if(name == "Delete") key = SDLK_DELETE;
    else if(name == "Backspace") key = SDLK_BACKSPACE;
    else if(name == "Return" || name == "Enter") key = SDLK_RETURN;
    else if(name == "Escape") key = SDLK_ESCAPE;
    else if(name == "Tab") key = SDLK_TAB;
    else if(name.size() == 1) key = SDL_GetKeyFromName(name.c_str());
    if(key == SDLK_UNKNOWN) {
      printf("[ui] unknown key '%s'\n", name.c_str());
      return;
    }
    for(const bool down : {true, false}) {
      SDL_Event ev{};
      ev.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
      ev.key.windowID = SDL_GetWindowID(window);
      ev.key.key = key;
      ev.key.scancode = SDL_GetScancodeFromKey(key, nullptr);
      ev.key.down = down;
      SDL_PushEvent(&ev);
    }
  }
}

bool Editor::UiScript::load(const std::string &path)
{
  std::ifstream file{path};
  if(!file) return false;
  std::string line{};
  while(std::getline(file, line)) {
    if(line.empty() || line[0] == '#')continue;
    std::istringstream in{line};
    Step step{};
    in >> step.cmd;
    if(step.cmd == "type") {
      std::string rest{};
      std::getline(in, rest);
      if(!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
      step.args.push_back(rest);
    } else {
      std::string arg{};
      while(in >> arg) step.args.push_back(arg);
    }
    if(!step.cmd.empty()) steps.push_back(step);
  }
  printf("[ui] script: %zu steps\n", steps.size());
  return true;
}

bool Editor::UiScript::wantsQuit()
{
  return quit;
}

void Editor::UiScript::holdPointer()
{
  if(pointerSet) ImGui::GetIO().AddMousePosEvent(mouseX, mouseY);
}

void Editor::UiScript::step(SDL_Window *window)
{
  if(!window)return;
  if(moveStep < moveSteps) {
    ++moveStep;
    const float t = (float)moveStep / (float)moveSteps;
    pushMotion(window, fromX + (toX - fromX) * t, fromY + (toY - fromY) * t);
    return;
  }
  if(waitFrames > 0) { --waitFrames; return; }

  // run steps until one takes a frame of its own
  while(!steps.empty())
  {
    Step s = steps.front();
    steps.pop_front();
    if(s.cmd == "wait") { waitFrames = (int)num(s, 0, 1.0f); return; }
    if(s.cmd == "move" || s.cmd == "click" || s.cmd == "rclick") {
      fromX = mouseX; fromY = mouseY;
      toX = num(s, 0); toY = num(s, 1);
      moveSteps = s.cmd == "move" ? (int)num(s, 2, 1.0f) : 1;
      moveStep = 0;
      if(moveSteps <= 1) { pushMotion(window, toX, toY); moveSteps = 0; }
      if(s.cmd != "move") {
        const uint8_t button = s.cmd == "click" ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT;
        // press and release on later frames, so ImGui sees the pointer arrive first
        steps.push_front({"_up", {std::to_string(button)}});
        steps.push_front({"wait", {"2"}});
        steps.push_front({"_down", {std::to_string(button)}});
        steps.push_front({"wait", {"2"}});
      }
      return;
    }
    if(s.cmd == "down" || s.cmd == "_down") { pushButton(window, s.args.empty() ? SDL_BUTTON_LEFT : (uint8_t)num(s, 0, 1), true); return; }
    if(s.cmd == "up" || s.cmd == "_up") { pushButton(window, s.args.empty() ? SDL_BUTTON_LEFT : (uint8_t)num(s, 0, 1), false); return; }
    if(s.cmd == "rdown") { pushButton(window, SDL_BUTTON_RIGHT, true); return; }
    if(s.cmd == "rup") { pushButton(window, SDL_BUTTON_RIGHT, false); return; }
    if(s.cmd == "key") { if(!s.args.empty()) pushKey(window, s.args[0]); return; }
    if(s.cmd == "wheel") {
      // scroll at the pointer: positive scrolls up (content moves down), as a mouse wheel does
      SDL_Event ev{};
      ev.type = SDL_EVENT_MOUSE_WHEEL;
      ev.wheel.windowID = SDL_GetWindowID(window);
      ev.wheel.x = num(s, 1);
      ev.wheel.y = num(s, 0);
      ev.wheel.mouse_x = mouseX;
      ev.wheel.mouse_y = mouseY;
      ev.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
      SDL_PushEvent(&ev);
      return;
    }
    if(s.cmd == "type") {
      textBuffer = s.args.empty() ? std::string{} : s.args[0];
      SDL_Event ev{};
      ev.type = SDL_EVENT_TEXT_INPUT;
      ev.text.windowID = SDL_GetWindowID(window);
      ev.text.text = textBuffer.c_str(); // read while this frame's events are polled
      SDL_PushEvent(&ev);
      return;
    }
    if(s.cmd == "mark") {
      printf("[ui] mark %s\n", s.args.empty() ? "" : s.args[0].c_str());
      fflush(stdout);
      continue;
    }
    if(s.cmd == "save") { if(ctx.project) ctx.project->save(); return; }
    if(s.cmd == "quit") { quit = true; return; }
    printf("[ui] unknown step '%s'\n", s.cmd.c_str());
  }
}
