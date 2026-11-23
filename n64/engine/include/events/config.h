/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: event bus limits (docs/todo/event-graph/PLAN.md, sections 3.5 and 4.2).
// Every value can be overridden for a project through ENGINE_CXXFLAGS in its Makefile.custom,
// for example:  ENGINE_CXXFLAGS += -DP64_EVENT_QUEUE_SIZE=256 -DP64_EVENT_TRACE=1
#pragma once

// Events that can wait in the queue at once.
#ifndef P64_EVENT_QUEUE_SIZE
  #define P64_EVENT_QUEUE_SIZE 128
#endif

// Listeners that can exist at once (code listeners and, from phase 2, bound input pins).
#ifndef P64_EVENT_MAX_SUBSCRIPTIONS
  #define P64_EVENT_MAX_SUBSCRIPTIONS 512
#endif

// Hash buckets used to find the listeners of an event. Must be a power of two.
#ifndef P64_EVENT_INDEX_SIZE
  #define P64_EVENT_INDEX_SIZE 512
#endif

// Events that interrupts can post between two frames.
#ifndef P64_EVENT_MAILBOX_SIZE
  #define P64_EVENT_MAILBOX_SIZE 16
#endif

// An event fired by a listener has one more hop than the event being handled.
// Events past this many hops are dropped with a warning: this stops endless loops.
#ifndef P64_EVENT_MAX_HOPS
  #define P64_EVENT_MAX_HOPS 16
#endif

// Events one drain delivers at most. The rest wait for the next drain.
#ifndef P64_EVENT_DRAIN_BUDGET
  #define P64_EVENT_DRAIN_BUDGET 256
#endif

// 1 prints every delivery through debugf.
#ifndef P64_EVENT_TRACE
  #define P64_EVENT_TRACE 0
#endif
