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
	// A blocked participant holds a reference to a gate this destructor is about to free, so the
	// schedule is abandoned first and every waiter is woken before anything is released.
	{
		MutexLock lock(mutex);
		abandon_locked(false);
	}
	for (Barrier *barrier : barriers) {
		memdelete(barrier);
	}
	barriers.clear();
}

Error FSCompletenessScheduleController::declare(
		const Vector<String> &p_barrier_names, int p_participant_count) {
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
		Barrier *barrier = memnew(Barrier);
		barrier->name = name;
		barriers.push_back(barrier);
	}
	barrier_index = declared;
	participants = p_participant_count;
	return OK;
}

Error FSCompletenessScheduleController::arrive(const String &p_name) {
	MutexLock lock(mutex);
	const int *index = barrier_index.getptr(p_name);
	if (index == nullptr) {
		return ERR_INVALID_PARAMETER;
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
	// Out-of-order and repeated releases are refused rather than tolerated: the release order is what
	// makes the interleaving the manifest declares the one the cell actually observes.
	if (*index != next_release) {
		return ERR_UNAVAILABLE;
	}
	Barrier *barrier = barriers[*index];
	barrier->released = true;
	next_release++;
	recorded_trace.push_back("release:" + p_name);
	if (barrier->waiting > 0) {
		barrier->gate.post(barrier->waiting);
	}
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
	Barrier *barrier = barriers[*index];
	while (true) {
		if (barrier->released) {
			return WAIT_RELEASED;
		}
		if (abandoned) {
			return WAIT_TIMED_OUT;
		}
		// A participant waiting for a barrier that is not released yet cannot release anything, so a
		// schedule whose last runnable participant is about to block can never reach this barrier.
		// Proving that is what replaces a sleeping retry loop: the outcome does not depend on how long
		// anything took. A participant whose barrier was already released is runnable even though it
		// has not woken yet, so it is not counted.
		if (stuck_participants_locked() + 1 >= participants) {
			recorded_trace.push_back("timeout:" + p_name);
			abandon_locked(true);
			return WAIT_TIMED_OUT;
		}
		if (p_budget_msec != 0 && OS::get_singleton()->get_ticks_msec() - started_at_msec >= p_budget_msec) {
			recorded_trace.push_back("timeout:" + p_name);
			abandon_locked(true);
			return WAIT_TIMED_OUT;
		}
		barrier->waiting++;
		// The controller's own state has to stay reachable while this participant is parked, so the
		// lock is handed back for the duration of the park and taken again on the way out. The scope's
		// lock still owns the mutex either way, so it is released exactly once however this loop ends.
		// A gate may carry a post more than the waiter it was meant for - a release and an abandon can
		// both post the same barrier - which only ever wakes a participant that re-tests the loop
		// condition and parks again.
		mutex.unlock();
		barrier->gate.wait();
		mutex.lock();
		barrier->waiting--;
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
	for (const Barrier *barrier : barriers) {
		if (!barrier->released) {
			stuck += barrier->waiting;
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
	for (Barrier *barrier : barriers) {
		if (barrier->waiting > 0) {
			barrier->gate.post(barrier->waiting);
		}
	}
}

} // namespace FSTests
