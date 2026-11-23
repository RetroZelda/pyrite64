/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: conditions, the engine side (docs/todo/conditionals/PLAN.md). The rules a check follows are
// in events/conditionTypes.h, the provider table in conditionCore.cpp.
#include "events/conditions.h"

#include "conditionCore.h"
#include "events/pins.h"
#include "lib/logger.h"
#include "scene/scene.h"
#include "scene/sceneManager.h"
#include "scene/components/code.h"
#include "scene/components/eventLinks.h"

namespace
{
  using namespace P64;
  using Conditions::Header;
  using Events::Pins::ConditionIn;
  using Events::Pins::ConditionOut;
  namespace Flag = Conditions::Flag;

  constexpr uint8_t PIN_BASE = Events::Pins::CONDITION_PIN_BASE;
  constexpr uint32_t MAX_DEPTH = 8;         // conditions reading conditions; a loop ends here
  constexpr uint32_t MAX_PER_NAME = 8;      // conditions read through one binding
  constexpr uint32_t MAX_WARNINGS = 8;      // per scene, so a loop cannot flood the log

  constexpr uint32_t hashOf(const char* name)
  {
    size_t len = 0;
    while(name[len]) ++len;
    return Events::pinHash(name, len);
  }

  // send(obj, "Check"_pin) and friends reach a condition by these names
  constexpr uint32_t INPUT_HASHES[] = {
    hashOf(Events::Pins::CONDITION_IN[0]), hashOf(Events::Pins::CONDITION_IN[1]),
    hashOf(Events::Pins::CONDITION_IN[2]), hashOf(Events::Pins::CONDITION_IN[3]),
  };
  static_assert(sizeof(INPUT_HASHES) / sizeof(INPUT_HASHES[0]) == Events::Pins::CONDITION_IN_LIST.count);

  // the internal re-check: not an identifier, so no "..."_pin can produce it
  constexpr uint32_t RECHECK = hashOf("Condition.Recheck");

  constinit Conditions::Core::Providers providers{};
  constinit uint32_t depth = 0;
  constinit uint32_t warnings = 0;
  constinit uint32_t checks = 0;

  bool allowWarning()
  {
    if(warnings >= MAX_WARNINGS) return false;
    ++warnings;
    return true;
  }

  Header& headerOf(Comp::Code& code)
  {
    return *static_cast<Header*>(code.getCodeData());
  }

  // the condition script at a component index, or nullptr
  Comp::Code* conditionAt(Object& obj, uint32_t comp)
  {
    if(comp >= obj.compCount) return nullptr;
    const auto &ref = obj.getCompRefs()[comp];
    if(ref.type != Comp::Code::ID) return nullptr;
    auto *code = reinterpret_cast<Comp::Code*>(reinterpret_cast<char*>(&obj) + ref.offset);
    return (code->usedFunctions & Comp::Code::FN_CONDITION) ? code : nullptr;
  }

  bool evaluateCode(Object& obj, Comp::Code& code)
  {
    if(depth >= MAX_DEPTH) {
      if(allowWarning()) {
        Log::warn("[conditions] object %u: conditions read each other more than %u deep (a loop?), answered false",
          (unsigned)obj.id, (unsigned)MAX_DEPTH);
      }
      return false;
    }
    ++depth;
    const bool answer = code.script->evaluate(obj, code.getCodeData());
    --depth;
    return answer;
  }

  // 'valueIsAnswer': a check no event with a value of its own asked for (a logic node following its
  // inputs), so the outputs carry the answer, 1 or 0
  void checkCode(Object& obj, Comp::Code& code, uint32_t value, bool valueIsAnswer = false)
  {
    auto &header = headerOf(code);
    if(!Conditions::isEnabled(header)) return;

    const auto outcome = Conditions::applyAnswer(header, evaluateCode(obj, code));
    ++checks;
    if(valueIsAnswer) value = outcome.answer ? 1 : 0;

    const void* data = code.getCodeData();
    auto pin = [](ConditionOut out) { return static_cast<uint8_t>(PIN_BASE + static_cast<uint8_t>(out)); };
    Events::fire(obj, data, pin(outcome.answer ? ConditionOut::True : ConditionOut::False), value);
    if(outcome.changed) {
      Events::fire(obj, data, pin(outcome.answer ? ConditionOut::NewlyTrue : ConditionOut::NewlyFalse), value);
    }
    Events::fire(obj, data, pin(ConditionOut::Result), outcome.answer ? 1 : 0);
  }

  template<typename Fn>
  void forEachCondition(Object& obj, Fn fn)
  {
    for(uint32_t c = 0; c < obj.compCount; ++c) {
      if(auto *code = conditionAt(obj, c)) fn(*code);
    }
  }
}

bool P64::Conditions::evaluate(Object& obj)
{
  for(uint32_t c = 0; c < obj.compCount; ++c) {
    if(auto *code = conditionAt(obj, c)) return evaluateCode(obj, *code);
  }
  return false;
}

void P64::Conditions::check(Object& obj, uint32_t value)
{
  forEachCondition(obj, [&](Comp::Code& code) { checkCode(obj, code, value); });
}

void P64::Conditions::setEnabled(Object& obj, bool enabled)
{
  forEachCondition(obj, [&](Comp::Code& code) { setEnabled(headerOf(code), enabled); });
}

void P64::Conditions::reset(Object& obj)
{
  forEachCondition(obj, [&](Comp::Code& code) { reset(headerOf(code)); });
}

void P64::Conditions::notifyData(Object& obj, void* data, [[maybe_unused]] const Events::Event& ev)
{
  // one re-check per wave: while it waits in the queue, further inputs change nothing
  auto &header = *static_cast<Header*>(data);
  if(!isEnabled(header) || (header.flags & Flag::PENDING)) return;
  if(Events::Internal::sendChecked(obj.id, RECHECK, 0, obj.id)) header.flags |= Flag::PENDING;
}

P64::Conditions::Inputs::Inputs(Object& obj, const void* data, uint8_t pin)
{
  auto *links = obj.getComponent<Comp::EventLinks>();
  if(!links) return;
  const uint8_t comp = Events::Internal::componentIndexOf(obj, data);
  auto &scene = SceneManager::getCurrent();

  for(uint32_t i = 0; i < links->count; ++i)
  {
    const auto &binding = links->bindings()[i];
    if(!binding.isInput() || binding.rec.comp != comp || binding.rec.pin != pin) continue;

    Core::Provider found[MAX_PER_NAME];
    const uint32_t count = providers.find(binding.rec.hash, binding.scope(), binding.instance, found, MAX_PER_NAME);
    if(count == 0) {
      ++total; // nothing provides this name (yet): a false input
      continue;
    }
    for(uint32_t k = 0; k < count; ++k) {
      ++total;
      Object* other = scene.getObjectById(found[k].object);
      Comp::Code* code = other ? conditionAt(*other, found[k].comp) : nullptr;
      if(code && evaluateCode(*other, *code)) ++truthy;
    }
  }
}

P64::Conditions::Stats P64::Conditions::getStats()
{
  return {
    .providers = providers.count(),
    .providersPeak = providers.peak(),
    .maxProviders = Core::Providers::MAX,
    .checks = checks,
  };
}

void P64::Conditions::Internal::onCodeInit(Object& obj, Comp::Code& code)
{
  reset(headerOf(code));

  // every event this condition's Result is bound to: logic nodes find the condition by them
  auto *links = obj.getComponent<Comp::EventLinks>();
  if(!links) return;
  const uint8_t comp = Events::Internal::componentIndexOf(obj, &code);
  const uint8_t resultPin = static_cast<uint8_t>(PIN_BASE + static_cast<uint8_t>(ConditionOut::Result));

  for(uint32_t i = 0; i < links->count; ++i)
  {
    const auto &binding = links->bindings()[i];
    if(binding.isInput() || binding.rec.comp != comp || binding.rec.pin != resultPin) continue;
    if(!providers.add(binding.rec.hash, binding.scope(), binding.instance, obj.id, comp) && allowWarning()) {
      Log::warn("[conditions] all %u provider entries in use, object %u cannot be read by logic nodes "
        "(P64_CONDITION_MAX_PROVIDERS)", (unsigned)Core::Providers::MAX, (unsigned)obj.id);
    }
  }
}

void P64::Conditions::Internal::onCodeDestroy(Object& obj, Comp::Code& code)
{
  providers.remove(obj.id, Events::Internal::componentIndexOf(obj, &code));
}

void P64::Conditions::Internal::onPin(Object& obj, Comp::Code& code, uint8_t pin, const Events::Event& ev)
{
  if(!(code.usedFunctions & Comp::Code::FN_CONDITION)) return;
  auto &header = headerOf(code);
  switch(static_cast<ConditionIn>(pin))
  {
    case ConditionIn::Enable:  setEnabled(header, true); break;
    case ConditionIn::Disable: setEnabled(header, false); break;
    case ConditionIn::Check:   checkCode(obj, code, ev.value); break;
    case ConditionIn::Reset:   reset(header); break;
    default: break;
  }
}

uint32_t P64::Conditions::Internal::onDirect(Object& obj, Comp::Code& code, const Events::Event& ev)
{
  if(!(code.usedFunctions & Comp::Code::FN_CONDITION)) return 0;

  if(ev.id == RECHECK) {
    auto &header = headerOf(code);
    if(!(header.flags & Flag::PENDING)) return 0; // another condition script of this object asked
    header.flags = static_cast<uint8_t>(header.flags & ~Flag::PENDING);
    checkCode(obj, code, 0, true);
    return 1;
  }

  for(uint8_t pin = 0; pin < Events::Pins::CONDITION_IN_LIST.count; ++pin) {
    if(ev.id != INPUT_HASHES[pin]) continue;
    onPin(obj, code, pin, ev);
    return 1;
  }
  return 0;
}

void P64::Conditions::Internal::onSceneDestroyed()
{
  providers.clear();
  depth = 0;
  warnings = 0;
  checks = 0;
}
