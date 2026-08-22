/**************************************************************************/
/*  test_type_completeness_schedule.h                                     */
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

#include "fs_type_completeness_schedule.h"

#include "core/os/mutex.h"
#include "core/os/os.h"
#include "core/os/semaphore.h"
#include "core/os/thread.h"
#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "tests/test_macros.h"

namespace FSTests {

#ifdef THREADS_ENABLED

namespace {

// One concurrent cell reduced to its schedule: a writer that publishes and a reader that may only
// observe afterwards. What the cell publishes is the order in which the two participants recorded
// their steps, so a schedule that did not impose the declared interleaving shows up as a different
// observation rather than as a flaky pass.
struct ScheduleRehearsal {
	FSCompletenessScheduleController controller;
	Vector<String> observed;
	Mutex observed_mutex;
	FSCompletenessScheduleController::WaitOutcome reader_outcome =
			FSCompletenessScheduleController::WAIT_UNDECLARED;
	FSCompletenessScheduleController::WaitOutcome writer_outcome =
			FSCompletenessScheduleController::WAIT_UNDECLARED;

	void observe(const String &p_step) {
		MutexLock lock(observed_mutex);
		observed.push_back(p_step);
	}
};

void schedule_rehearsal_reader(void *p_rehearsal) {
	ScheduleRehearsal *rehearsal = static_cast<ScheduleRehearsal *>(p_rehearsal);
	// The reader may only read what the writer published, so every step it takes is behind the
	// barrier the writer releases.
	rehearsal->reader_outcome = rehearsal->controller.wait("write_published", 5000);
	rehearsal->observe("read");
	rehearsal->controller.arrive("read_observed");
	rehearsal->controller.release("read_observed");
}

// A participant that waits for a barrier nobody will ever release. Used to prove that a stuck
// schedule ends in a decision rather than in a hang.
void schedule_stuck_participant(void *p_controller) {
	FSCompletenessScheduleController *controller =
			static_cast<FSCompletenessScheduleController *>(p_controller);
	controller->wait("never_released", 0);
}

// Two participants parked on two different barriers of one schedule, so an abandonment has to name
// both positions rather than only the one the deciding participant was at.
struct ScheduleStandstill {
	FSCompletenessScheduleController controller;
	FSCompletenessScheduleController::WaitOutcome first_outcome =
			FSCompletenessScheduleController::WAIT_RELEASED;
	FSCompletenessScheduleController::WaitOutcome second_outcome =
			FSCompletenessScheduleController::WAIT_RELEASED;
	Semaphore first_parked;
	Semaphore second_parked;
};

void schedule_standstill_first(void *p_standstill) {
	ScheduleStandstill *standstill = static_cast<ScheduleStandstill *>(p_standstill);
	standstill->first_parked.post();
	standstill->first_outcome = standstill->controller.wait("first_barrier", 0);
}

void schedule_standstill_second(void *p_standstill) {
	ScheduleStandstill *standstill = static_cast<ScheduleStandstill *>(p_standstill);
	standstill->second_parked.post();
	standstill->second_outcome = standstill->controller.wait("second_barrier", 0);
}

// Two participants parked on two barriers of a schedule that is about to be abandoned, and a release
// that arrives afterwards. The release must not be able to turn either of their decided timeouts into
// a WAIT_RELEASED that the trace does not record.
struct ScheduleLateRelease {
	FSCompletenessScheduleController controller;
	FSCompletenessScheduleController::WaitOutcome first_outcome =
			FSCompletenessScheduleController::WAIT_RELEASED;
	FSCompletenessScheduleController::WaitOutcome second_outcome =
			FSCompletenessScheduleController::WAIT_RELEASED;
	Semaphore first_started;
	Semaphore second_started;
};

void schedule_late_release_first(void *p_late) {
	ScheduleLateRelease *late = static_cast<ScheduleLateRelease *>(p_late);
	late->first_started.post();
	late->first_outcome = late->controller.wait("first_barrier", 0);
}

void schedule_late_release_second(void *p_late) {
	ScheduleLateRelease *late = static_cast<ScheduleLateRelease *>(p_late);
	late->second_started.post();
	late->second_outcome = late->controller.wait("second_barrier", 0);
}

Vector<String> run_publish_then_read_rehearsal(ScheduleRehearsal &r_rehearsal) {
	REQUIRE_EQ(r_rehearsal.controller.declare(
					   Vector<String>({ "write_published", "read_observed" }), 2),
			OK);
	Thread reader;
	reader.start(schedule_rehearsal_reader, &r_rehearsal);
	r_rehearsal.observe("write");
	REQUIRE_EQ(r_rehearsal.controller.release("write_published"), OK);
	r_rehearsal.writer_outcome = r_rehearsal.controller.wait("read_observed", 5000);
	reader.wait_to_finish();
	return r_rehearsal.observed;
}

} // namespace

TEST_SUITE("[Modules][FoundryScript][TypeCompleteness] Schedule") {
	TEST_CASE("TypeCompleteness Schedule imposes the declared interleaving twice over") {
		// Determinism is the whole reason the controller exists: the same schedule run twice in one
		// test has to produce the same observation, or a concurrency cell is measuring the machine.
		ScheduleRehearsal first;
		const Vector<String> first_observation = run_publish_then_read_rehearsal(first);
		ScheduleRehearsal second;
		const Vector<String> second_observation = run_publish_then_read_rehearsal(second);

		CHECK_EQ(first_observation, Vector<String>({ "write", "read" }));
		CHECK_EQ(second_observation, first_observation);
		CHECK_EQ(first.reader_outcome, FSCompletenessScheduleController::WAIT_RELEASED);
		CHECK_EQ(first.writer_outcome, FSCompletenessScheduleController::WAIT_RELEASED);
		CHECK_EQ(second.reader_outcome, first.reader_outcome);
		CHECK_EQ(second.writer_outcome, first.writer_outcome);
		CHECK_EQ(first.controller.trace(),
				Vector<String>({ "release:write_published", "arrive:read_observed",
						"release:read_observed" }));
		CHECK_FALSE(first.controller.timed_out());
		CHECK_FALSE(second.controller.timed_out());
		CHECK_EQ(first.controller.trace(), second.controller.trace());
	}

	TEST_CASE("TypeCompleteness Schedule decides a stuck schedule instead of hanging on it") {
		FSCompletenessScheduleController controller;
		REQUIRE_EQ(controller.declare(Vector<String>({ "never_released" }), 2), OK);
		Thread blocked;
		blocked.start(schedule_stuck_participant, &controller);
		// The second participant is the last one that could have released the barrier, so its own wait
		// proves nothing can. No budget is given: the decision is the proof, not the clock.
		const FSCompletenessScheduleController::WaitOutcome outcome = controller.wait("never_released", 0);
		blocked.wait_to_finish();

		CHECK_EQ(outcome, FSCompletenessScheduleController::WAIT_TIMED_OUT);
		CHECK(controller.timed_out());
		// Both participants lost a wait, so both are in the trace: the one that proved the schedule
		// stuck and the one that was parked when it did.
		CHECK_EQ(controller.trace(),
				Vector<String>({ "timeout:never_released", "timeout:never_released" }));
		CHECK_EQ(String(FSCompletenessScheduleController::SCHEDULE_TIMEOUT_STATUS), "schedule_timeout");
	}

	TEST_CASE("TypeCompleteness Schedule names every parked participant when it abandons a schedule") {
		// The trace is what a timed-out cell retains as its diagnosis, so it has to say where each
		// participant was standing, not only where the one that noticed was standing.
		ScheduleStandstill standstill;
		REQUIRE_EQ(standstill.controller.declare(
						   Vector<String>({ "first_barrier", "second_barrier", "third_barrier" }), 3),
				OK);
		Thread first;
		Thread second;
		first.start(schedule_standstill_first, &standstill);
		second.start(schedule_standstill_second, &standstill);
		standstill.first_parked.wait();
		standstill.second_parked.wait();
		// A budget rather than the stuck proof, because the two participants may not have reached
		// their waits yet; either way the schedule can never reach a barrier nobody releases.
		const FSCompletenessScheduleController::WaitOutcome deciding =
				standstill.controller.wait("third_barrier", 2000);
		first.wait_to_finish();
		second.wait_to_finish();

		CHECK_EQ(deciding, FSCompletenessScheduleController::WAIT_TIMED_OUT);
		CHECK_EQ(standstill.first_outcome, FSCompletenessScheduleController::WAIT_TIMED_OUT);
		CHECK_EQ(standstill.second_outcome, FSCompletenessScheduleController::WAIT_TIMED_OUT);
		CHECK(standstill.controller.timed_out());
		// One record per participant, each naming the barrier that participant was standing at. Which
		// of the three notices the standstill first is not the schedule's business - a stuck schedule
		// has no order left to impose - so the records are compared as a set.
		Vector<String> trace = standstill.controller.trace();
		trace.sort();
		CHECK_EQ(trace,
				Vector<String>({ "timeout:first_barrier", "timeout:second_barrier",
						"timeout:third_barrier" }));
	}

	TEST_CASE("TypeCompleteness Schedule refuses a release that arrives after it abandoned") {
		// The trace of an abandoned schedule records a timeout for every parked participant. A release
		// accepted afterwards would hand one of them a WAIT_RELEASED the trace never mentions, and which
		// of the two readings won would depend on which woken thread reached the lock first. The
		// repetition is there to shake exactly that ordering out.
		for (int attempt = 0; attempt < 25; attempt++) {
			CAPTURE(attempt);
			ScheduleLateRelease late;
			REQUIRE_EQ(late.controller.declare(
							   Vector<String>({ "first_barrier", "second_barrier", "third_barrier" }), 3),
					OK);
			Thread first;
			Thread second;
			first.start(schedule_late_release_first, &late);
			second.start(schedule_late_release_second, &late);
			late.first_started.wait();
			late.second_started.wait();

			const FSCompletenessScheduleController::WaitOutcome deciding =
					late.controller.wait("third_barrier", 2000);
			// The release races the two waking participants, and has to lose whichever way it lands.
			const Error late_release = late.controller.release("first_barrier");
			first.wait_to_finish();
			second.wait_to_finish();

			CHECK_EQ(deciding, FSCompletenessScheduleController::WAIT_TIMED_OUT);
			CHECK_EQ(late.first_outcome, FSCompletenessScheduleController::WAIT_TIMED_OUT);
			CHECK_EQ(late.second_outcome, FSCompletenessScheduleController::WAIT_TIMED_OUT);
			CHECK_EQ(late_release, ERR_LOCKED);
			CHECK_EQ(late.controller.arrive("first_barrier"), ERR_LOCKED);
			CHECK(late.controller.timed_out());

			Vector<String> trace = late.controller.trace();
			trace.sort();
			CHECK_EQ(trace,
					Vector<String>({ "timeout:first_barrier", "timeout:second_barrier",
							"timeout:third_barrier" }));
		}
	}

	TEST_CASE("TypeCompleteness Schedule ends a wait whose budget elapses outside the controller") {
		// The stuck-schedule proof only covers participants that are inside the controller. A
		// participant that never reaches it - stalled in product code, or gone - leaves no proof to
		// draw, and the budget is the only thing between that and a run that never ends.
		FSCompletenessScheduleController controller;
		REQUIRE_EQ(controller.declare(Vector<String>({ "released_by_the_absent_participant" }), 2), OK);
		const uint64_t started_at_msec = OS::get_singleton()->get_ticks_msec();
		const FSCompletenessScheduleController::WaitOutcome outcome =
				controller.wait("released_by_the_absent_participant", 50);
		const uint64_t elapsed_msec = OS::get_singleton()->get_ticks_msec() - started_at_msec;

		CHECK_EQ(outcome, FSCompletenessScheduleController::WAIT_TIMED_OUT);
		CHECK(controller.timed_out());
		CHECK_GE(elapsed_msec, uint64_t(50));
		CHECK_EQ(controller.trace(),
				Vector<String>({ "timeout:released_by_the_absent_participant" }));
	}

	TEST_CASE("TypeCompleteness Schedule refuses a schedule that could change while it runs") {
		FSCompletenessScheduleController controller;
		CHECK_EQ(controller.declare(Vector<String>(), 2), ERR_INVALID_PARAMETER);
		CHECK_EQ(controller.declare(Vector<String>({ "only" }), 1), ERR_INVALID_PARAMETER);
		CHECK_EQ(controller.declare(Vector<String>({ String() }), 2), ERR_INVALID_PARAMETER);
		CHECK_EQ(controller.declare(Vector<String>({ "twice", "twice" }), 2), ERR_INVALID_PARAMETER);
		REQUIRE_EQ(controller.declare(Vector<String>({ "first", "second" }), 2), OK);
		CHECK_EQ(controller.declare(Vector<String>({ "first" }), 2), ERR_ALREADY_IN_USE);
	}

	TEST_CASE("TypeCompleteness Schedule refuses a release that is not the declared next one") {
		FSCompletenessScheduleController controller;
		REQUIRE_EQ(controller.declare(Vector<String>({ "first", "second" }), 2), OK);
		CHECK_EQ(controller.release("second"), ERR_UNAVAILABLE);
		CHECK_EQ(controller.release("absent"), ERR_INVALID_PARAMETER);
		CHECK_EQ(controller.arrive("absent"), ERR_INVALID_PARAMETER);
		CHECK_EQ(controller.wait("absent", 0), FSCompletenessScheduleController::WAIT_UNDECLARED);
		REQUIRE_EQ(controller.release("first"), OK);
		CHECK_EQ(controller.release("first"), ERR_UNAVAILABLE);
		REQUIRE_EQ(controller.release("second"), OK);
		// A barrier already released is not something to wait for, whoever asks and whenever.
		CHECK_EQ(controller.wait("first", 0), FSCompletenessScheduleController::WAIT_RELEASED);
		CHECK_EQ(controller.trace(),
				Vector<String>({ "release:first", "release:second" }));
		CHECK_FALSE(controller.timed_out());
	}
}

#else // No threads.

TEST_SUITE("[Modules][FoundryScript][TypeCompleteness] Schedule") {
	TEST_CASE("TypeCompleteness Schedule refuses a schedule this build cannot run") {
		// One thread cannot rendezvous two participants, so the schedule is refused rather than
		// attempted. Anything else would let a concurrency cell report an interleaving it never had.
		FSCompletenessScheduleController controller;
		CHECK_EQ(controller.declare(Vector<String>({ "write_published" }), 2), ERR_UNAVAILABLE);
		CHECK_FALSE(controller.timed_out());
		CHECK(controller.trace().is_empty());
	}
}

#endif // THREADS_ENABLED

} // namespace FSTests
