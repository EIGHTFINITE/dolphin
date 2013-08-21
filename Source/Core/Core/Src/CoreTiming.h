// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _CORETIMING_H
#define _CORETIMING_H

// This is a system to schedule events into the emulated machine's future. Time is measured
// in main CPU clock cycles.

// To schedule an event, you first have to register its type. This is where you pass in the
// callback. You then schedule events using the type id you get back.

// See HW/SystemTimers.cpp for the main part of Dolphin's usage of this scheduler.

// The int cyclesLate that the callbacks get is how many cycles late it was.
// So to schedule a new event on a regular basis:
// inside callback:
//   ScheduleEvent(periodInCycles - cyclesLate, callback, "whatever")

#include "Common.h"

#include <string>

#include "ChunkFile.h"

namespace CoreTiming
{

void Init();
void Shutdown();

typedef void (*TimedCallback)(u64 userdata, int cyclesLate);

u64 GetTicks();
u64 GetIdleTicks();

void DoState(PointerWrap &p);

// Returns the event_type identifier. if name is not unique, an existing event_type will be discarded.
int RegisterEvent(const char *name, TimedCallback callback);
void UnregisterAllEvents();

// userdata MAY NOT CONTAIN POINTERS. userdata might get written and reloaded from disk,
// when we implement state saves.
void ScheduleEvent(int cyclesIntoFuture, int event_type, u64 userdata=0);
void ScheduleEvent_Threadsafe(int cyclesIntoFuture, int event_type, u64 userdata=0);
void ScheduleEvent_Threadsafe_Immediate(int event_type, u64 userdata=0);

// We only permit one event of each type in the queue at a time.
void RemoveEvent(int event_type);
void RemoveThreadsafeEvent(int event_type);
void RemoveAllEvents(int event_type);
bool IsScheduled(int event_type);
void Advance();
void MoveEvents();
void ProcessFifoWaitEvents();

// Pretend that the main CPU has executed enough cycles to reach the next event.
void Idle();

// Clear all pending events. This should ONLY be done on exit or state load.
void ClearPendingEvents();

void LogPendingEvents();
void SetMaximumSlice(int maximumSliceLength);
void ResetSliceLength();

void RegisterAdvanceCallback(void (*callback)(int cyclesExecuted));

std::string GetScheduledEventsSummary();

u32 GetFakeDecStartValue();
void SetFakeDecStartValue(u32 val);
u64 GetFakeDecStartTicks();
void SetFakeDecStartTicks(u64 val);
u64 GetFakeTBStartValue();
void SetFakeTBStartValue(u64 val);
u64 GetFakeTBStartTicks();
void SetFakeTBStartTicks(u64 val);

void ForceExceptionCheck(int cycles);

extern int downcount;
extern int slicelength;

}; // end of namespace

#endif
