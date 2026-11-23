#include "script/userScript.h"
#include "scene/sceneManager.h"
#include "./testLog.h"

// An object that listens and posts from its own callbacks, the way game scripts will.
namespace P64::Script::C3E7A1B5D9F20461
{
  using namespace P64::User::EventTest;

  P64_DATA(
    P64::Events::Listener pingListener;
    uint32_t fixedFrame;
    uint8_t postedFixed;
    uint8_t postedUpdate;
    uint16_t pad;
  );

  void onPing(const Events::Event& ev, void* user)
  {
    auto &obj = *static_cast<Object*>(user);
    add(Rec::Ping, ev);
    Events::post(PONG, ev.value + 1, obj.id);
  }

  void init(Object& obj, Data *data)
  {
    data->postedFixed = 0;
    data->postedUpdate = 0;
    data->pingListener = Events::listen(PING, onPing, &obj);
  }

  void destroy(Object& obj, Data *data)
  {
    Events::unlisten(data->pingListener);
  }

  void fixedUpdate(Object& obj, Data *data, float fixedDeltaTime)
  {
    if(activeTest != T_DRAINS || data->postedFixed) return;
    data->postedFixed = 1;
    data->fixedFrame = currFrame;
    add(Rec::Fixed, 0, obj.id);
    Events::post(FROM_FIXED, 0, obj.id);
  }

  void update(Object& obj, Data *data, float deltaTime)
  {
    if(activeTest != T_DRAINS || !data->postedFixed || data->postedUpdate) return;
    data->postedUpdate = 1;
    add(Rec::Update, 0, obj.id);
    Events::post(FROM_UPDATE, 0, obj.id);
  }
}
