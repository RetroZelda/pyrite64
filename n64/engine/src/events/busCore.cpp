/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: the event bus core, see busCore.h.
#include "busCore.h"

namespace P64::Events::Core
{
  void BusCore::reset()
  {
    for(uint32_t i = 0; i < MAX_SUBS; ++i) {
      subs[i] = Sub{};
      subs[i].next = (i + 1 < MAX_SUBS) ? static_cast<uint16_t>(i + 1) : NIL;
    }
    freeHead = 0;
    for(auto &bucket : buckets) bucket = NIL;

    queueHead = 0;
    queueCount = 0;
    mailboxCount = 0;
    pendingDeadCount = 0;
    pendingDeadOverflow = false;
    ctx = {};
    depth = 0;
    stats = {};
  }

  uint32_t BusCore::bucketOf(uint32_t hash, uint16_t instance)
  {
    // event hashes are CRC-32 values and spread well already; mix in the instance
    uint32_t h = hash ^ (static_cast<uint32_t>(instance) * 0x9E3779B1u);
    h ^= h >> 16;
    return h & (INDEX_SIZE - 1);
  }

  void BusCore::warn(Warning warning, uint32_t hash)
  {
    if(warnFn) warnFn(warning, hash, warnUser);
  }

  Handle BusCore::subscribe(uint32_t hash, uint16_t instance, ListenerFn fn, void* user)
  {
    if(!fn) return {};
    if(freeHead == NIL) {
      ++stats.subscribeFailed;
      warn(Warning::PoolFull, hash);
      return {};
    }

    uint16_t slot = freeHead;
    Sub &sub = subs[slot];
    freeHead = sub.next;

    sub.hash = hash;
    sub.instance = instance;
    sub.next = NIL;
    sub.flags = FLAG_ALIVE;
    sub.fn = fn;
    sub.user = user;

    // append at the tail so listeners are called in the order they subscribed. A delivery in
    // progress only walks the entries the chain had when it started (see dispatch()).
    uint32_t b = bucketOf(hash, instance);
    if(buckets[b] == NIL) {
      buckets[b] = slot;
    } else {
      uint16_t s = buckets[b];
      while(subs[s].next != NIL) s = subs[s].next;
      subs[s].next = slot;
    }

    ++stats.subscriptions;
    if(stats.subscriptions > stats.subscriptionsPeak) stats.subscriptionsPeak = stats.subscriptions;
    return {slot, sub.gen};
  }

  bool BusCore::unsubscribe(Handle handle)
  {
    if(handle.slot >= MAX_SUBS) return false;
    Sub &sub = subs[handle.slot];
    if(sub.gen != handle.gen || !(sub.flags & FLAG_ALIVE)) return false;
    markDead(handle.slot);
    return true;
  }

  uint32_t BusCore::unsubscribeInstance(uint16_t instance)
  {
    if(instance == 0) return 0;
    uint32_t removed = 0;
    for(uint32_t i = 0; i < MAX_SUBS; ++i) {
      if((subs[i].flags & FLAG_ALIVE) && subs[i].instance == instance) {
        markDead(static_cast<uint16_t>(i));
        ++removed;
      }
    }
    return removed;
  }

  uint32_t BusCore::unsubscribeAllInstances()
  {
    uint32_t removed = 0;
    for(uint32_t i = 0; i < MAX_SUBS; ++i) {
      if((subs[i].flags & FLAG_ALIVE) && subs[i].instance != 0) {
        markDead(static_cast<uint16_t>(i));
        ++removed;
      }
    }
    return removed;
  }

  void BusCore::markDead(uint16_t slot)
  {
    Sub &sub = subs[slot];
    sub.flags = FLAG_DEAD;
    --stats.subscriptions;

    if(depth == 0) {
      // nothing is walking the chains: unlink right away
      unlink(slot);
      release(slot);
      return;
    }

    // a delivery may be walking this chain: unlink once it finished
    if(pendingDeadCount < PENDING_DEAD) {
      pendingDead[pendingDeadCount++] = slot;
    } else {
      pendingDeadOverflow = true;
    }
  }

  void BusCore::unlink(uint16_t slot)
  {
    uint32_t b = bucketOf(subs[slot].hash, subs[slot].instance);
    uint16_t prev = NIL;
    uint16_t s = buckets[b];
    while(s != NIL && s != slot) {
      prev = s;
      s = subs[s].next;
    }
    if(s == NIL) return;
    if(prev == NIL) {
      buckets[b] = subs[slot].next;
    } else {
      subs[prev].next = subs[slot].next;
    }
  }

  void BusCore::release(uint16_t slot)
  {
    Sub &sub = subs[slot];
    sub.flags = 0;
    sub.fn = nullptr;
    sub.user = nullptr;
    ++sub.gen; // makes old handles stale
    sub.next = freeHead;
    freeHead = slot;
  }

  void BusCore::sweepDead()
  {
    if(pendingDeadOverflow) {
      for(uint32_t i = 0; i < MAX_SUBS; ++i) {
        if(subs[i].flags & FLAG_DEAD) {
          unlink(static_cast<uint16_t>(i));
          release(static_cast<uint16_t>(i));
        }
      }
    } else {
      for(uint32_t i = 0; i < pendingDeadCount; ++i) {
        uint16_t slot = pendingDead[i];
        if(subs[slot].flags & FLAG_DEAD) {
          unlink(slot);
          release(slot);
        }
      }
    }
    pendingDeadCount = 0;
    pendingDeadOverflow = false;
  }

  bool BusCore::enqueue(const QueuedEvent& event)
  {
    if(queueCount >= QUEUE_SIZE) {
      ++stats.droppedQueueFull;
      warn(Warning::QueueFull, event.ev.id);
      return false;
    }
    queue[(queueHead + queueCount) % QUEUE_SIZE] = event;
    ++queueCount;
    if(queueCount > stats.queuePeak) stats.queuePeak = static_cast<uint16_t>(queueCount);
    return true;
  }

  bool BusCore::post(uint32_t hash, Scope scope, uint16_t instance, uint32_t value,
                     uint16_t sender, uint16_t instigator)
  {
    QueuedEvent event{};
    event.ev.id = hash;
    event.ev.sender = sender;
    event.ev.value = value;
    event.instance = instance;
    event.scope = scope;

    if(depth > 0) {
      // fired while handling another event: one more hop in the same chain
      if(ctx.hops >= MAX_HOPS) {
        ++stats.droppedHopLimit;
        warn(Warning::HopLimit, hash);
        return false;
      }
      event.hops = static_cast<uint8_t>(ctx.hops + 1);
      event.ev.instigator = instigator ? instigator : (ctx.instigator ? ctx.instigator : sender);
    } else {
      event.hops = 0;
      event.ev.instigator = instigator ? instigator : sender;
    }

    if(!enqueue(event)) return false;
    ++stats.posted;
    return true;
  }

  bool BusCore::deliverNow(uint32_t hash, Scope scope, uint16_t instance, uint32_t value, uint16_t sender)
  {
    QueuedEvent event{};
    event.ev.id = hash;
    event.ev.sender = sender;
    event.ev.value = value;
    event.instance = instance;
    event.scope = scope;

    if(depth > 0) {
      if(ctx.hops >= MAX_HOPS) {
        ++stats.droppedHopLimit;
        warn(Warning::HopLimit, hash);
        return false;
      }
      event.hops = static_cast<uint8_t>(ctx.hops + 1);
      event.ev.instigator = ctx.instigator ? ctx.instigator : sender;
    } else {
      event.ev.instigator = sender;
    }

    ++stats.posted;
    dispatch(event);
    return true;
  }

  void BusCore::dispatch(const QueuedEvent& event)
  {
    ++stats.delivered;

    const Context saved = ctx;
    ctx.hash = event.ev.id;
    ctx.instigator = event.ev.instigator;
    ctx.hops = event.hops;
    ++depth;

    // Walk only the entries the chain has now. Listeners subscribed by a callback are appended
    // behind them and are not called for this event. Unsubscribed entries stay linked (marked
    // dead) until the delivery ends, so the walk stays valid.
    uint32_t b = bucketOf(event.ev.id, event.instance);
    uint32_t chainLength = 0;
    for(uint16_t s = buckets[b]; s != NIL; s = subs[s].next) ++chainLength;

    uint32_t calls = 0;
    uint16_t s = buckets[b];
    for(uint32_t i = 0; i < chainLength && s != NIL; ++i) {
      const Sub &sub = subs[s];
      if((sub.flags & FLAG_ALIVE) && sub.hash == event.ev.id && sub.instance == event.instance) {
        ++calls;
        sub.fn(event.ev, sub.user);
      }
      s = subs[s].next; // read after the call: the callback may have appended behind it
    }
    stats.listenerCalls += calls;

    if(traceFn) traceFn(event, calls, traceUser);

    --depth;
    ctx = saved;
    if(depth == 0 && (pendingDeadCount > 0 || pendingDeadOverflow)) {
      sweepDead();
    }
  }

  uint32_t BusCore::drain(uint32_t budget)
  {
    if(depth > 0) return 0; // no nested drains: events posted now are handled by the running drain

    uint32_t delivered = 0;
    while(queueCount > 0)
    {
      if(delivered >= budget) {
        ++stats.budgetHits;
        warn(Warning::DrainBudget, queue[queueHead].ev.id);
        break;
      }
      const QueuedEvent event = queue[queueHead];
      queueHead = (queueHead + 1) % QUEUE_SIZE;
      --queueCount;

      dispatch(event);
      ++delivered;
    }
    return delivered;
  }

  bool BusCore::mailboxPush(uint32_t hash, uint32_t value)
  {
    if(mailboxCount >= MAILBOX_SIZE) {
      ++stats.droppedMailbox; // no warning here: this runs in interrupts, bus.cpp reports it later
      return false;
    }
    mailbox[mailboxCount++] = {hash, value};
    return true;
  }

  uint32_t BusCore::mailboxFlush()
  {
    uint32_t moved = 0;
    for(uint32_t i = 0; i < mailboxCount; ++i) {
      QueuedEvent event{};
      event.ev.id = mailbox[i].hash;
      event.ev.value = mailbox[i].value;
      event.scope = Scope::Global;
      if(enqueue(event)) {
        ++stats.posted;
        ++moved;
      }
    }
    mailboxCount = 0;
    return moved;
  }

  uint32_t BusCore::dropQueuedSceneEvents()
  {
    uint32_t kept = 0;
    uint32_t dropped = 0;
    for(uint32_t i = 0; i < queueCount; ++i) {
      const QueuedEvent &event = queue[(queueHead + i) % QUEUE_SIZE];
      if(event.scope == Scope::Global || event.scope == Scope::UI) {
        queue[(queueHead + kept) % QUEUE_SIZE] = event;
        ++kept;
      } else {
        ++dropped;
      }
    }
    queueCount = kept;
    return dropped;
  }

  uint32_t BusCore::listenerCount(uint32_t hash, uint16_t instance) const
  {
    uint32_t count = 0;
    for(uint16_t s = buckets[bucketOf(hash, instance)]; s != NIL; s = subs[s].next) {
      if((subs[s].flags & FLAG_ALIVE) && subs[s].hash == hash && subs[s].instance == instance) ++count;
    }
    return count;
  }

  Stats BusCore::getStats() const
  {
    Stats res = stats;
    res.queued = static_cast<uint16_t>(queueCount);
    return res;
  }
}
