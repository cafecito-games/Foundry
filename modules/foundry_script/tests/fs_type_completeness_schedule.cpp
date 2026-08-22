/**************************************************************************/
/*  fs_type_completeness_schedule.cpp                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                              GODOT ENGINE                              */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "fs_type_completeness_schedule.h"

#include "core/os/os.h"

namespace FSTests {

const char *FSCompletenessScheduleController::SCHEDULE_TIMEOUT_STATUS = "schedule_timeout";

FSCompletenessScheduleController::~FSCompletenessScheduleController() {
	// Every participant is required to have finished before the controller goes away. Abandoning the
	// schedule here ends any wait that outlived that contract instead of leaving it parked on a
	// condition variable that is about to be destroyed.
	MutexLock lock(mutex);
	abandon_locked(false);
}

Error FSCompletenessScheduleController::declare(
		const Vector<String> &p_barrier_names, int p_participant_count) {
#ifndef THREADS_ENABLED
	// A schedule rendezvouses at least two participants, and this build has one thread: Thread::start
	// never runs its callback and a wait can never be notified. Refusing the schedule outright is the
	// only reading that is not a lie - a cell that cannot impose its interleaving has observed
	// nothing, and a controller that let it try would spin instead of saying so.
	return ERR_UNAVAILABLE;
#else
	MutexLock lock(mutex);
	if (!barriers.is_empty()) {
		return ERR_ALREADY_IN_USE;
	}
	if (p_barrier_names.is_empty() || p_participant_count < 2) {
		return ERR_INVALID_PARAMETER;
	}
	HashMap<String, int> declared;
	for (const String &name : p_barrier_names) {
		if (name.is_empty() || declared.has(name)) {
			return ERR_INVALID_PARAMETER;
		}
		declared.insert(name, declared.size());
	}
	for (const String &name : p_barrier_names) {
		Barrier barrier;
		barrier.name = name;
		barriers.push_back(barrier);
	}
	barrier_index = declared;
	participants = p_participant_count;
	return OK;
#endif
}

Error FSCompletenessScheduleController::arrive(const String &p_name) {
	MutexLock lock(mutex);
	const int *index = barrier_index.getptr(p_name);
	if (index == nullptr) {
		return ERR_INVALID_PARAMETER;
	}
	if (abandoned) {
		return ERR_LOCKED;
	}
	recorded_trace.push_back("arrive:" + p_name);
	return OK;
}

Error FSCompletenessScheduleController::release(const String &p_name) {
	MutexLock lock(mutex);
	const int *index = barrier_index.getptr(p_name);
	if (index == nullptr) {
		return ERR_INVALID_PARAMETER;
	}
	// Abandonment is terminal. A schedule that could not be carried out cannot be resumed, and a
	// release accepted afterwards would release a barrier whose waiters the trace already records as
	// timed out - so the status a wait returns and the trace the cell retains could disagree, and which
	// of the two won would depend on the order the machine happened to wake threads in.
	if (abandoned) {
		return ERR_LOCKED;
	}
	// Out-of-order and repeated releases are refused rather than tolerated: the release order is what
	// makes the interleaving the manifest declares the one the cell actually observes.
	if (*index != next_release) {
		return ERR_UNAVAILABLE;
	}
	barriers[*index].released = true;
	next_release++;
	recorded_trace.push_back("release:" + p_name);
	signal.notify_all();
	return OK;
}

FSCompletenessScheduleController::WaitOutcome FSCompletenessScheduleController::wait(
		const String &p_name, uint32_t p_budget_msec) {
	const uint64_t started_at_msec = OS::get_singleton()->get_ticks_msec();
	MutexLock lock(mutex);
	const int *index = barrier_index.getptr(p_name);
	if (index == nullptr) {
		return WAIT_UNDECLARED;
	}
	Barrier &barrier = barriers[*index];
	while (true) {
		// The outcome of a participant that was parked when the schedule was abandoned was decided
		// then, under this lock, at the same moment its timeout was written to the trace. Consuming
		// that decision here rather than re-deriving one is what makes the status a wait returns and
		// the record the trace holds the same fact rather than two readings of the same state.
		if (barrier.decided_timeouts > 0) {
			barrier.decided_timeouts--;
			return WAIT_TIMED_OUT;
		}
		if (barrier.released) {
			return WAIT_RELEASED;
		}
		if (abandoned) {
			// The schedule was already abandoned when this participant asked, so no abandonment could
			// have decided the wait it is about to lose. The record it owes the trace is its own.
			recorded_trace.push_back("timeout:" + p_name);
			return WAIT_TIMED_OUT;
		}
		// A participant waiting for a barrier that is not released yet cannot release anything, so a
		// schedule whose last runnable participant is about to block can never reach this barrier.
		// Proving that is what replaces a sleeping retry loop: the outcome does not depend on how long
		// anything took. A participant whose barrier was already released is runnable even though it
		// has not woken yet, so it is not counted.
		bool decided = stuck_participants_locked() + 1 >= participants;
		if (!decided) {
			barrier.waiting++;
			if (p_budget_msec == 0) {
				signal.wait(lock);
			} else {
				// The elapsed time is re-read on every pass, so a wait woken by an unrelated state
				// change resumes with what is left of the budget rather than restarting it.
				const uint64_t elapsed_msec = OS::get_singleton()->get_ticks_msec() - started_at_msec;
				decided = elapsed_msec >= p_budget_msec ||
						!signal.wait_for(lock, p_budget_msec - elapsed_msec);
			}
			barrier.waiting--;
			// Whatever happened while this participant was parked, the head of the loop is the only
			// place an outcome is read, so anything it can decide takes precedence over a budget that
			// ran out at the same time.
			if (!decided || barrier.decided_timeouts > 0 || barrier.released || abandoned) {
				continue;
			}
		}
		recorded_trace.push_back("timeout:" + p_name);
		abandon_locked(true);
		return WAIT_TIMED_OUT;
	}
}

Vector<String> FSCompletenessScheduleController::trace() const {
	MutexLock lock(mutex);
	return recorded_trace;
}

bool FSCompletenessScheduleController::timed_out() const {
	MutexLock lock(mutex);
	return timed_out_flag;
}

int FSCompletenessScheduleController::stuck_participants_locked() const {
	int stuck = 0;
	for (const Barrier &barrier : barriers) {
		if (!barrier.released) {
			stuck += barrier.waiting;
		}
	}
	return stuck;
}

void FSCompletenessScheduleController::abandon_locked(bool p_timed_out) {
	timed_out_flag = timed_out_flag || p_timed_out;
	if (abandoned) {
		return;
	}
	abandoned = true;
	if (p_timed_out) {
		// Every participant parked on a barrier the schedule can no longer reach ends in a timeout, and
		// the trace has to name the barrier each one was blocked on: that is what makes the trace a
		// diagnosis rather than a notice. The records are appended here, in declared barrier order,
		// rather than by each woken participant, because the order threads wake in is not the schedule's
		// and a trace that varies between two identical runs is not evidence.
		for (Barrier &barrier : barriers) {
			if (barrier.released) {
				continue;
			}
			for (int waiter = 0; waiter < barrier.waiting; waiter++) {
				recorded_trace.push_back("timeout:" + barrier.name);
			}
			// The decision each of those records stands for, waiting to be claimed by the participant
			// it was made about. Nothing can release the barrier afterwards, so every one of them is
			// claimed by a wait that returns exactly what was written here.
			barrier.decided_timeouts += barrier.waiting;
		}
	}
	signal.notify_all();
}

} // namespace FSTests
