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
#include "core/os/thread.h"
#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "tests/test_macros.h"

namespace FSTests {

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
		const Vector<String> trace = controller.trace();
		REQUIRE_FALSE(trace.is_empty());
		CHECK_EQ(trace[trace.size() - 1], "timeout:never_released");
		CHECK_EQ(String(FSCompletenessScheduleController::SCHEDULE_TIMEOUT_STATUS), "schedule_timeout");
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

} // namespace FSTests
