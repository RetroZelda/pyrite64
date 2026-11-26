/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: dialog trees (docs/todo/dialog/PLAN.md, section 7).
#include "dialog/dialog.h"

#include <cstring>

#include "events/conditions.h"
#include "scene/object.h"

namespace
{
  const P64::Dialog::Speaker* speakers{};
  uint8_t speakerTotal{};

  struct EvalCtx
  {
    const P64::Dialog::TreeView* tree;
    P64::Object* obj;
    bool (*input)(uint8_t, void*);
    void* user;
  };

  bool answerCondition(uint16_t cond, void* user)
  {
    auto &c = *static_cast<EvalCtx*>(user);
    if(cond >= c.tree->conditionCount()) return false;
    return P64::Conditions::evaluateScript(c.tree->conditionScript(cond), const_cast<void*>(c.tree->conditionData(cond)), *c.obj);
  }

  bool answerInput(uint8_t input, void* user)
  {
    auto &c = *static_cast<EvalCtx*>(user);
    return c.input ? c.input(input, c.user) : false;
  }
}

void P64::Dialog::setSpeakers(const Speaker* list, uint8_t count)
{
  speakers = list;
  speakerTotal = count;
}

uint8_t P64::Dialog::speakerCount() { return speakerTotal; }

const P64::Dialog::Speaker* P64::Dialog::speaker(uint8_t index)
{
  return index < speakerTotal ? &speakers[index] : nullptr;
}

uint8_t P64::Dialog::findSpeaker(const char* id)
{
  for(uint8_t i = 0; i < speakerTotal; ++i) {
    if(strcmp(speakers[i].id, id) == 0) return i;
  }
  return NO_SPEAKER;
}

uint16_t P64::Dialog::portraitAsset(uint8_t speakerIndex, uint8_t portrait)
{
  const Speaker* s = speaker(speakerIndex);
  if(!s || s->portraitCount == 0) return NO_ASSET;
  return s->portraits[portrait < s->portraitCount ? portrait : 0].asset;
}

bool P64::Dialog::evaluate(const TreeView &tree, int16_t expr, Object &obj, bool (*input)(uint8_t, void*), void* user)
{
  if(expr == NO_EXPR) return true;
  EvalCtx ctx{&tree, &obj, input, user};
  return runExpr(tree.expr(expr), answerCondition, answerInput, &ctx);
}
