/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: the event bus core (docs/todo/event-graph/PLAN.md, sections 3.5, 3.6 and 4.2).
// Queue, subscriptions and delivery rules, with fixed memory. No libdragon or engine includes,
// so the host unit tests compile this file as is. The N64 side is in bus.cpp.
#pragma once
#include <cstdint>

#include "events/config.h"
#include "events/types.h"

namespace P64::Events::Core
{
  /** A subscription handle. 'gen' makes a handle stale once its slot is freed and reused. */
  struct Handle
  {
    uint16_t slot{0xFFFF};
    uint16_t gen{0};

    [[nodiscard]] constexpr bool isValid() const { return slot != 0xFFFF; }
  };

  enum class Warning : uint8_t
  {
    QueueFull,   // the queue had no room: the event was dropped
    HopLimit,    // the chain went past the hop limit: the event was dropped
    DrainBudget, // a drain delivered its budget: the rest waits for the next drain
    PoolFull,    // every subscription slot is in use: the listener was not added
  };

  using WarnFn = void(*)(Warning warning, uint32_t eventHash, void* user);

  struct QueuedEvent
  {
    Event ev{};
    uint16_t instance{}; // scope instance: 0 for global, scene and ui; an object id otherwise
    uint8_t hops{};
    Scope scope{};
  };

  using TraceFn = void(*)(const QueuedEvent& event, uint32_t listeners, void* user);

  /** Delivers a Scope::Direct event (send()) to its target object. Returns the listeners called. */
  using DirectFn = uint32_t(*)(const QueuedEvent& event, void* user);

  class BusCore
  {
    public:
      static constexpr uint32_t QUEUE_SIZE   = P64_EVENT_QUEUE_SIZE;
      static constexpr uint32_t MAX_SUBS     = P64_EVENT_MAX_SUBSCRIPTIONS;
      static constexpr uint32_t INDEX_SIZE   = P64_EVENT_INDEX_SIZE;
      static constexpr uint32_t MAILBOX_SIZE = P64_EVENT_MAILBOX_SIZE;
      static constexpr uint32_t MAX_HOPS     = P64_EVENT_MAX_HOPS;
      static constexpr uint32_t DRAIN_BUDGET = P64_EVENT_DRAIN_BUDGET;
      static constexpr uint32_t PENDING_DEAD = 32; // unsubscribes during delivery before a full sweep

      static_assert(QUEUE_SIZE > 0 && QUEUE_SIZE <= 0xFFFF, "P64_EVENT_QUEUE_SIZE out of range");
      static_assert(MAX_SUBS > 0 && MAX_SUBS < 0xFFFF, "P64_EVENT_MAX_SUBSCRIPTIONS out of range");
      static_assert(INDEX_SIZE > 0 && (INDEX_SIZE & (INDEX_SIZE - 1)) == 0, "P64_EVENT_INDEX_SIZE must be a power of two");
      static_assert(MAX_HOPS < 0xFF, "P64_EVENT_MAX_HOPS out of range");

      BusCore() { reset(); }

      /** Drops every queued event, subscription and counter. Handlers set below are kept. */
      void reset();

      /**
       * Adds a listener for (hash, instance). Listeners of the same event are called in the
       * order they subscribed. A listener added during a delivery does not receive the event
       * being delivered, but does receive later ones.
       */
      Handle subscribe(uint32_t hash, uint16_t instance, ListenerFn fn, void* user);

      /** Removes a listener. Safe during delivery and with stale handles. */
      bool unsubscribe(Handle handle);

      /** Removes every listener keyed to this object (instance != 0). */
      uint32_t unsubscribeInstance(uint16_t instance);

      /** Removes every listener keyed to any object, used when a scene is destroyed. */
      uint32_t unsubscribeAllInstances();

      /**
       * Queues an event. Called from a listener, the event gets one more hop than the event
       * being delivered and inherits its instigator unless one is given.
       * @return false if the event was dropped (queue full or hop limit)
       */
      bool post(uint32_t hash, Scope scope, uint16_t instance, uint32_t value,
                uint16_t sender, uint16_t instigator = 0);

      /** Delivers an event right away, without queueing it (used outside the frame loop). */
      bool deliverNow(uint32_t hash, Scope scope, uint16_t instance, uint32_t value, uint16_t sender);

      /**
       * Delivers queued events in order, including events queued by listeners during this
       * drain, until the queue is empty or 'budget' events were delivered.
       * @return events delivered
       */
      uint32_t drain(uint32_t budget = DRAIN_BUDGET);

      /** Interrupt mailbox. The caller keeps interrupts from running during these calls. */
      bool mailboxPush(uint32_t hash, uint32_t value);
      /** Moves the mailbox into the queue (global scope, sender 0). @return events moved */
      uint32_t mailboxFlush();

      /** Drops queued scene, prefab and object events, used when a scene is destroyed. */
      uint32_t dropQueuedSceneEvents();

      [[nodiscard]] uint32_t queuedCount() const { return queueCount; }
      [[nodiscard]] uint32_t listenerCount(uint32_t hash, uint16_t instance) const;
      [[nodiscard]] bool isDelivering() const { return depth > 0; }
      /** The instigator of the event being delivered, 0 outside a delivery. */
      [[nodiscard]] uint16_t currentInstigator() const { return depth > 0 ? ctx.instigator : 0; }
      [[nodiscard]] Stats getStats() const;

      void setWarnHandler(WarnFn fn, void* user) { warnFn = fn; warnUser = user; }
      void setTraceHandler(TraceFn fn, void* user) { traceFn = fn; traceUser = user; }
      void setDirectHandler(DirectFn fn, void* user) { directFn = fn; directUser = user; }

    private:
      static constexpr uint16_t NIL = 0xFFFF;
      static constexpr uint8_t FLAG_ALIVE = 1 << 0;
      static constexpr uint8_t FLAG_DEAD  = 1 << 1; // unsubscribed during a delivery, unlinked after it

      struct Sub
      {
        uint32_t hash;
        uint16_t instance;
        uint16_t next;  // next slot in the bucket chain, or next free slot
        uint16_t gen;
        uint8_t flags;
        uint8_t pad;
        ListenerFn fn;
        void* user;
      };

      struct MailboxEntry
      {
        uint32_t hash;
        uint32_t value;
      };

      struct Context
      {
        uint32_t hash{};
        uint16_t instigator{};
        uint8_t hops{};
      };

      Sub subs[MAX_SUBS]{};
      uint16_t buckets[INDEX_SIZE]{};
      uint16_t freeHead{NIL};

      QueuedEvent queue[QUEUE_SIZE]{};
      uint32_t queueHead{0};
      uint32_t queueCount{0};

      MailboxEntry mailbox[MAILBOX_SIZE]{};
      uint32_t mailboxCount{0};

      uint16_t pendingDead[PENDING_DEAD]{};
      uint32_t pendingDeadCount{0};
      bool pendingDeadOverflow{false};

      Context ctx{};
      uint32_t depth{0};
      Stats stats{};

      WarnFn warnFn{nullptr};
      void* warnUser{nullptr};
      TraceFn traceFn{nullptr};
      void* traceUser{nullptr};
      DirectFn directFn{nullptr};
      void* directUser{nullptr};

      [[nodiscard]] static uint32_t bucketOf(uint32_t hash, uint16_t instance);
      bool enqueue(const QueuedEvent& event);
      void dispatch(const QueuedEvent& event);
      void markDead(uint16_t slot);
      void unlink(uint16_t slot);
      void release(uint16_t slot);
      void sweepDead();
      void warn(Warning warning, uint32_t hash);
  };
}
