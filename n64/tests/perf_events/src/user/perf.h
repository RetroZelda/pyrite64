#pragma once
// Shared state of the event and condition benchmark (PerfTests.cpp drives it).
#include "script/userScript.h"

namespace P64::User::Perf
{
  extern uint32_t sinkCalls;   // PerfSink::onInput calls (a bound script input)
  extern uint32_t codeCalls;   // code listener calls
  extern uint32_t evaluations; // PerfCond::evaluate calls
  extern bool flag;            // what every PerfCond answers

  // a script's object and data, so the benchmark can fire its output pins
  struct ScriptRef
  {
    Object* obj{};
    void* data{};
  };
  extern ScriptRef source;     // one binding on the object
  extern ScriptRef sourceBusy; // sixteen bindings on the object
  extern uint16_t targetId;    // send() target, an id below 128
  extern uint16_t targetFarId; // send() target, an id above 128
  extern uint16_t evalId;      // a condition to evaluate() and check() from code
}
