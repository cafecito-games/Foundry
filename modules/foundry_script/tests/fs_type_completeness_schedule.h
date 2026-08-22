/**************************************************************************/
/*  fs_type_completeness_schedule.h                                       */
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

#pragma once

#include "core/error/error_list.h"
#include "core/os/condition_variable.h"
#include "core/os/mutex.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "core/templates/vector.h"

namespace FSTests {

// The rendezvous every concurrent completeness cell is scheduled through.
//
// A schedule is an ordered list of named barriers, declared in full before any participant starts.
// A participant `arrive`s at a barrier to record that it reached that point in its own work and
// `wait`s for the barrier to be released; the barriers are released in the declared order, so the
// interleaving a cell observes is the one its manifest names and not the one the machine happened to
// produce. Nothing here sleeps or spins: a wait ends when the barrier is released, when the
// controller can prove no participant can ever release it because every participant is already
// blocked, or when the declared budget elapses. The proof is what makes a concurrency cell
// reproducible - it fires long before any budget could and does not depend on how long anything
// took; the budget only bounds a participant stuck outside the controller, where no proof is
// available, so that such a schedule ends in a verdict rather than in a hung run.
//
// A schedule that ends any other way than by releasing every barrier is a `schedule_timeout` runtime
// status on the cell, never a passing observation: an interleaving the harness could not impose is
// not evidence about the product.
class FSCompletenessScheduleController {
public:
	enum WaitOutcome {
		// The barrier was released while this participant waited for it, or before it waited.
		WAIT_RELEASED,
		// The schedule cannot reach this barrier: every participant is blocked on an unreleased
		// barrier, or the budget elapsed with the barrier still unreleased.
		WAIT_TIMED_OUT,
		// The name is not part of the declared schedule. A defect in the cell, not in the product.
		WAIT_UNDECLARED,
	};

	// The runtime status a cell carries when its schedule did not complete. Spelled once so the
	// controller, the adapter that reports it, and the tests that assert on it cannot drift.
	static const char *SCHEDULE_TIMEOUT_STATUS;

	// Fixes the schedule: the barrier names in release order and how many participants take part.
	// Refuses an empty name, a repeated name, an empty schedule, fewer than two participants, and any
	// second declaration, because a schedule that can change while it runs is not a schedule.
	Error declare(const Vector<String> &p_barrier_names, int p_participant_count);

	// Records that the calling participant reached p_name. Ordering evidence only: arriving neither
	// blocks nor releases.
	Error arrive(const String &p_name);

	// Releases p_name and wakes every participant waiting for it. Barriers must be released in the
	// declared order; releasing one twice, or out of order, is refused with ERR_UNAVAILABLE, because
	// a schedule whose releases can be reordered is not the schedule the manifest declared.
	Error release(const String &p_name);

	// Blocks the calling participant until p_name is released, until the schedule is proven stuck, or
	// until p_budget_msec have elapsed. Zero waits without a budget, which is only safe when every
	// participant is inside the controller, because the stuck-schedule proof is then the bound.
	WaitOutcome wait(const String &p_name, uint32_t p_budget_msec);

	// Every participant must have finished before the controller is destroyed. Every arrival, release,
	// and timeout in the order it happened, as "arrive:<name>",
	// "release:<name>", and "timeout:<name>". This is the schedule trace a timed-out cell retains.
	Vector<String> trace() const;

	// True once any wait ended without its barrier being released.
	bool timed_out() const;

	FSCompletenessScheduleController() = default;
	~FSCompletenessScheduleController();

	FSCompletenessScheduleController(const FSCompletenessScheduleController &) = delete;
	FSCompletenessScheduleController &operator=(const FSCompletenessScheduleController &) = delete;

private:
	struct Barrier {
		String name;
		bool released = false;
		int waiting = 0;
	};

	// Binary rather than recursive: a condition variable can only wait on a lock that is released
	// exactly once, and no path here takes the lock twice.
	mutable BinaryMutex mutex;
	// One condition variable for the whole schedule rather than one gate per barrier: every state
	// change is relevant to every waiter, because a waiter re-tests both its own barrier and the
	// stuck-schedule proof, and a bounded wait has to be able to end on either.
	ConditionVariable signal;
	LocalVector<Barrier> barriers;
	HashMap<String, int> barrier_index;
	Vector<String> recorded_trace;
	int participants = 0;
	int next_release = 0;
	bool abandoned = false;
	bool timed_out_flag = false;

	// Participants currently blocked on a barrier that has not been released. Called with the mutex
	// held.
	int stuck_participants_locked() const;

	// Ends every wait in progress and every wait still to come, recording whether the schedule was
	// abandoned because it could not progress or because the controller is going away. Called with the
	// mutex held.
	void abandon_locked(bool p_timed_out);
};

} // namespace FSTests
