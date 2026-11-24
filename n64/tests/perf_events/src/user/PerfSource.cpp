#include "script/userScript.h"
#include "./perf.h"

// Has output pins the benchmark fires from outside. The "busy" one has all sixteen bound, so
// firing Out has to look through sixteen bindings.
namespace P64::Script::C5A1B2C3D4E5F621
{
  P64_DATA(
    [[P64::Name("Busy")]]
    uint32_t busy = 0;
  );
  P64_EVENTS_OUT(Out, O1, O2, O3, O4, O5, O6, O7, O8, O9, O10, O11, O12, O13, O14, O15);

  void init(Object& obj, Data *data)
  {
    (data->busy ? P64::User::Perf::sourceBusy : P64::User::Perf::source) = {&obj, data};
  }
}
