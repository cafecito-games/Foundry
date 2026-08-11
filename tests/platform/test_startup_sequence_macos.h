/**************************************************************************/
/*  test_startup_sequence_macos.h                                         */
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

#include "platform/macos/startup_sequence_macos.h"

#include "tests/test_macros.h"

#include <cstdlib>

// Unit tests for the staged macOS boot from #2090. The sequence is exercised through its virtual
// phase bodies, so ordering, failure handling and the input gate lifecycle are checked without
// booting an engine. Event types come from real `NSEvent` instances built in the companion
// `.mm`, which is also where the Objective-C the rest of this file cannot use lives.

namespace TestStartupSequenceMacOS {

unsigned long key_down_event_type();
unsigned long key_up_event_type();
unsigned long left_mouse_down_event_type();
unsigned long scroll_wheel_event_type();
unsigned long flags_changed_event_type();
unsigned long magnify_event_type();
unsigned long tablet_point_event_type();
unsigned long application_defined_event_type();
unsigned long appkit_defined_event_type();
unsigned long system_defined_event_type();
unsigned long periodic_event_type();
unsigned long cursor_update_event_type();

// Restores the process-wide gate state so a failing expectation cannot leak into later tests.
class GateStateGuard {
	bool previous = false;

public:
	GateStateGuard() :
			previous(StartupInputGateMacOS::is_suppressed()) {}
	~GateStateGuard() {
		StartupInputGateMacOS::set_suppressed(previous);
		StartupBootGateMacOS::reset();
	}
};

// Records what the engine would have been told once boot finished.
struct ReplayedState {
	int64_t window_id = 0;
	StartupBootGateMacOS::WindowState state = StartupBootGateMacOS::WINDOW_STATE_MAX;
	bool value = false;
};

class ReplayRecorder {
public:
	Vector<ReplayedState> replayed;

	static void record(int64_t p_window_id, StartupBootGateMacOS::WindowState p_state, bool p_value, void *p_userdata) {
		ReplayedState entry;
		entry.window_id = p_window_id;
		entry.state = p_state;
		entry.value = p_value;
		static_cast<ReplayRecorder *>(p_userdata)->replayed.push_back(entry);
	}

	void replay() { StartupBootGateMacOS::release_and_replay(&ReplayRecorder::record, this); }

	int count_of(StartupBootGateMacOS::WindowState p_state) const {
		int total = 0;
		for (const ReplayedState &entry : replayed) {
			if (entry.state == p_state) {
				total++;
			}
		}
		return total;
	}
};

// Stands in for `-[FoundryApplication sendEvent:]`: applies the same gate and counts what would
// have reached the application.
class EventSink {
	int delivered = 0;

public:
	void send(unsigned long p_event_type, bool p_native_modal_session = false) {
		if (StartupInputGateMacOS::should_discard_event(p_event_type, p_native_modal_session)) {
			return;
		}
		delivered++;
	}

	int get_delivered() const { return delivered; }
};

// A sequence whose phase bodies are recorded rather than executed. The phase bodies double as the
// synthetic slow phases: events posted from inside them stand for input that arrives while the
// editor is still booting.
class RecordingSequence : public StartupSequenceMacOS {
public:
	int main_start_calls = 0;
	int main_loop_initialize_calls = 0;
	int main_start_result = EXIT_SUCCESS;
	bool main_loop_present = true;

	EventSink *sink = nullptr;

protected:
	virtual int _main_start() override {
		main_start_calls++;
		if (sink) {
			sink->send(key_down_event_type());
			sink->send(left_mouse_down_event_type());
		}
		return main_start_result;
	}

	virtual bool _has_main_loop() const override { return main_loop_present; }

	virtual void _main_loop_initialize() override {
		main_loop_initialize_calls++;
		if (sink) {
			sink->send(key_down_event_type());
		}
	}
};

TEST_CASE("[StartupSequence][macOS] runs one boot phase per step and then hands over to the main loop") {
	GateStateGuard guard;
	RecordingSequence sequence;
	sequence.begin();

	CHECK(sequence.get_phase() == StartupSequenceMacOS::PHASE_MAIN_START);

	CHECK(sequence.step() == StartupSequenceMacOS::STEP_PENDING);
	CHECK(sequence.main_start_calls == 1);
	CHECK(sequence.main_loop_initialize_calls == 0);
	CHECK(sequence.get_phase() == StartupSequenceMacOS::PHASE_MAIN_LOOP_INITIALIZE);

	CHECK(sequence.step() == StartupSequenceMacOS::STEP_RUNNING);
	CHECK(sequence.main_start_calls == 1);
	CHECK(sequence.main_loop_initialize_calls == 1);
	CHECK(sequence.get_phase() == StartupSequenceMacOS::PHASE_RUNNING);

	// Steady state: no further boot work, whatever the caller does.
	CHECK(sequence.step() == StartupSequenceMacOS::STEP_RUNNING);
	CHECK(sequence.main_start_calls == 1);
	CHECK(sequence.main_loop_initialize_calls == 1);
}

TEST_CASE("[StartupSequence][macOS] a failed Main::start exits without initializing the main loop") {
	GateStateGuard guard;
	RecordingSequence sequence;
	sequence.main_start_result = EXIT_FAILURE;
	sequence.begin();

	CHECK(sequence.step() == StartupSequenceMacOS::STEP_EXIT_FAILURE);
	CHECK(sequence.main_loop_initialize_calls == 0);
	CHECK(sequence.get_phase() == StartupSequenceMacOS::PHASE_MAIN_START);
	// Input stays gated: the boot never completed.
	CHECK(StartupInputGateMacOS::is_suppressed());
}

TEST_CASE("[StartupSequence][macOS] a command line tool run leaves no main loop and exits successfully") {
	GateStateGuard guard;
	RecordingSequence sequence;
	sequence.main_loop_present = false;
	sequence.begin();

	CHECK(sequence.step() == StartupSequenceMacOS::STEP_EXIT_SUCCESS);
	CHECK(sequence.main_start_calls == 1);
	CHECK(sequence.main_loop_initialize_calls == 0);
}

TEST_CASE("[StartupSequence][macOS] input arriving during a boot phase is never delivered") {
	GateStateGuard guard;
	EventSink sink;
	RecordingSequence sequence;
	sequence.sink = &sink;
	sequence.begin();

	// Key and mouse events posted from inside `Main::start()`.
	CHECK(sequence.step() == StartupSequenceMacOS::STEP_PENDING);
	CHECK(sink.get_delivered() == 0);

	// And from inside `main_loop->initialize()`, the longest phase.
	CHECK(sequence.step() == StartupSequenceMacOS::STEP_RUNNING);
	CHECK(sink.get_delivered() == 0);

	// Once boot is over the very same events go through.
	sink.send(key_down_event_type());
	sink.send(left_mouse_down_event_type());
	CHECK(sink.get_delivered() == 2);
}

// Stands in for a boot phase that raises a native alert: `NSAlert` spins a nested run loop in the
// modal panel mode, which is one of the common modes the boot observer is registered in, so the
// observer calls back into the sequence while this phase is still on the stack.
class ReentrantSequence : public StartupSequenceMacOS {
public:
	int main_start_calls = 0;
	int main_loop_initialize_calls = 0;
	StepResult nested_result = STEP_RUNNING;
	int nested_calls = 0;

protected:
	virtual int _main_start() override {
		main_start_calls++;
		if (nested_calls == 0) {
			nested_calls++;
			nested_result = step();
		}
		return EXIT_SUCCESS;
	}

	virtual bool _has_main_loop() const override { return true; }

	virtual void _main_loop_initialize() override { main_loop_initialize_calls++; }
};

TEST_CASE("[StartupSequence][macOS] a nested run loop cannot re-enter the phase that is running") {
	GateStateGuard guard;
	ReentrantSequence sequence;
	sequence.begin();

	CHECK(sequence.step() == StartupSequenceMacOS::STEP_PENDING);

	// The re-entrant call reported "still booting" and ran nothing; an unbounded alert loop is
	// exactly what would happen if it had run the phase again.
	CHECK(sequence.nested_result == StartupSequenceMacOS::STEP_PENDING);
	CHECK(sequence.main_start_calls == 1);
	CHECK(sequence.main_loop_initialize_calls == 0);
	CHECK(sequence.get_phase() == StartupSequenceMacOS::PHASE_MAIN_LOOP_INITIALIZE);

	// The sequence is not wedged: the next real turn runs the next phase.
	CHECK(sequence.step() == StartupSequenceMacOS::STEP_RUNNING);
	CHECK(sequence.main_loop_initialize_calls == 1);
	CHECK(sequence.get_phase() == StartupSequenceMacOS::PHASE_RUNNING);
}

// A boot phase that abandons the boot part way through, the way the observer's exception handler
// does when a phase raises.
class AbandoningSequence : public StartupSequenceMacOS {
public:
	int main_start_calls = 0;
	int main_loop_initialize_calls = 0;

protected:
	virtual int _main_start() override {
		main_start_calls++;
		_fail();
		return EXIT_SUCCESS;
	}

	virtual bool _has_main_loop() const override { return true; }

	virtual void _main_loop_initialize() override { main_loop_initialize_calls++; }
};

TEST_CASE("[StartupSequence][macOS] an abandoned phase fails the boot instead of being retried") {
	GateStateGuard guard;
	AbandoningSequence sequence;
	sequence.begin();

	CHECK(sequence.step() == StartupSequenceMacOS::STEP_EXIT_FAILURE);
	CHECK(sequence.main_start_calls == 1);
	CHECK(sequence.get_phase() == StartupSequenceMacOS::PHASE_FAILED);
	// The boot never completed, so input stays gated.
	CHECK(StartupInputGateMacOS::is_suppressed());

	// A later run loop turn must not run the half-finished phase again, nor advance past it.
	CHECK(sequence.step() == StartupSequenceMacOS::STEP_EXIT_FAILURE);
	CHECK(sequence.main_start_calls == 1);
	CHECK(sequence.main_loop_initialize_calls == 0);
}

TEST_CASE("[StartupSequence][macOS] the gate stays engaged for every boot phase and lifts only at the end") {
	// Not every input path runs through `-[FoundryApplication sendEvent:]`: media keys and the
	// modifier poll on application activation reach the engine directly, and consult this flag.
	GateStateGuard guard;
	RecordingSequence sequence;
	sequence.begin();
	CHECK(StartupInputGateMacOS::is_suppressed());

	CHECK(sequence.step() == StartupSequenceMacOS::STEP_PENDING);
	CHECK(StartupInputGateMacOS::is_suppressed());

	CHECK(sequence.step() == StartupSequenceMacOS::STEP_RUNNING);
	CHECK_FALSE(StartupInputGateMacOS::is_suppressed());
}

TEST_CASE("[StartupSequence][macOS] the gate discards user input and nothing else") {
	GateStateGuard guard;
	StartupInputGateMacOS::set_suppressed(true);

	CHECK(StartupInputGateMacOS::should_discard_event(key_down_event_type(), false));
	CHECK(StartupInputGateMacOS::should_discard_event(key_up_event_type(), false));
	CHECK(StartupInputGateMacOS::should_discard_event(flags_changed_event_type(), false));
	CHECK(StartupInputGateMacOS::should_discard_event(left_mouse_down_event_type(), false));
	CHECK(StartupInputGateMacOS::should_discard_event(scroll_wheel_event_type(), false));
	CHECK(StartupInputGateMacOS::should_discard_event(magnify_event_type(), false));
	CHECK(StartupInputGateMacOS::should_discard_event(tablet_point_event_type(), false));

	// AppKit's own bookkeeping must keep flowing or window state goes stale during boot.
	CHECK_FALSE(StartupInputGateMacOS::should_discard_event(appkit_defined_event_type(), false));
	CHECK_FALSE(StartupInputGateMacOS::should_discard_event(system_defined_event_type(), false));
	CHECK_FALSE(StartupInputGateMacOS::should_discard_event(application_defined_event_type(), false));
	CHECK_FALSE(StartupInputGateMacOS::should_discard_event(periodic_event_type(), false));
	CHECK_FALSE(StartupInputGateMacOS::should_discard_event(cursor_update_event_type(), false));

	StartupInputGateMacOS::set_suppressed(false);
	CHECK_FALSE(StartupInputGateMacOS::should_discard_event(key_down_event_type(), false));
	CHECK_FALSE(StartupInputGateMacOS::should_discard_event(left_mouse_down_event_type(), false));
}

TEST_CASE("[StartupSequence][macOS] a native modal session still receives input while booting") {
	GateStateGuard guard;
	EventSink sink;
	StartupInputGateMacOS::set_suppressed(true);

	// A startup error alert must remain dismissable.
	sink.send(key_down_event_type(), true);
	sink.send(left_mouse_down_event_type(), true);
	CHECK(sink.get_delivered() == 2);

	sink.send(application_defined_event_type());
	CHECK(sink.get_delivered() == 3);
}

// The boot event gate. `StartupInputGateMacOS` stops `NSEvent`s at the application ingress, but
// window state arrives as AppKit notifications that never pass through it, so these are the paths
// that could otherwise drive a half-built tree.

TEST_CASE("[StartupSequence][macOS] window state is withheld while booting and delivered after") {
	GateStateGuard guard;
	ReplayRecorder recorder;
	StartupBootGateMacOS::reset();

	// Not booting: the engine callback runs inline and nothing is queued.
	CHECK_FALSE(StartupBootGateMacOS::defer_window_state(1, StartupBootGateMacOS::WINDOW_STATE_RECT));
	CHECK_FALSE(StartupBootGateMacOS::has_pending_state());

	StartupBootGateMacOS::engage();
	CHECK(StartupBootGateMacOS::defer_window_state(1, StartupBootGateMacOS::WINDOW_STATE_RECT));
	CHECK(StartupBootGateMacOS::has_pending_state());
	CHECK(recorder.replayed.is_empty());

	recorder.replay();
	CHECK(recorder.replayed.size() == 1);
	CHECK(recorder.replayed[0].state == StartupBootGateMacOS::WINDOW_STATE_RECT);
	CHECK_FALSE(StartupBootGateMacOS::is_engaged());
}

TEST_CASE("[StartupSequence][macOS] repeated window changes collapse to one replayed state") {
	GateStateGuard guard;
	ReplayRecorder recorder;
	StartupBootGateMacOS::reset();
	StartupBootGateMacOS::engage();

	// A window being dragged and resized during a slow boot produces a stream of these. The tree
	// only needs to learn where the window ended up, not every intermediate rect.
	for (int index = 0; index < 12; index++) {
		StartupBootGateMacOS::defer_window_state(1, StartupBootGateMacOS::WINDOW_STATE_RECT);
	}
	recorder.replay();
	CHECK(recorder.count_of(StartupBootGateMacOS::WINDOW_STATE_RECT) == 1);
}

TEST_CASE("[StartupSequence][macOS] the last focus and mouse state observed is the one replayed") {
	GateStateGuard guard;
	ReplayRecorder recorder;
	StartupBootGateMacOS::reset();
	StartupBootGateMacOS::engage();

	StartupBootGateMacOS::defer_window_state(1, StartupBootGateMacOS::WINDOW_STATE_FOCUS, true);
	StartupBootGateMacOS::defer_window_state(1, StartupBootGateMacOS::WINDOW_STATE_FOCUS, false);
	StartupBootGateMacOS::defer_window_state(1, StartupBootGateMacOS::WINDOW_STATE_FOCUS, true);
	StartupBootGateMacOS::defer_window_state(1, StartupBootGateMacOS::WINDOW_STATE_MOUSE, true);
	StartupBootGateMacOS::defer_window_state(1, StartupBootGateMacOS::WINDOW_STATE_MOUSE, false);

	recorder.replay();
	CHECK(recorder.count_of(StartupBootGateMacOS::WINDOW_STATE_FOCUS) == 1);
	CHECK(recorder.count_of(StartupBootGateMacOS::WINDOW_STATE_MOUSE) == 1);
	for (const ReplayedState &entry : recorder.replayed) {
		if (entry.state == StartupBootGateMacOS::WINDOW_STATE_FOCUS) {
			CHECK(entry.value); // Ended focused.
		}
		if (entry.state == StartupBootGateMacOS::WINDOW_STATE_MOUSE) {
			CHECK_FALSE(entry.value); // Ended outside.
		}
	}
}

TEST_CASE("[StartupSequence][macOS] replayed state settles geometry before focus") {
	GateStateGuard guard;
	ReplayRecorder recorder;
	StartupBootGateMacOS::reset();
	StartupBootGateMacOS::engage();

	// Queued in the opposite order to the one they must be replayed in.
	StartupBootGateMacOS::defer_window_state(1, StartupBootGateMacOS::WINDOW_STATE_FOCUS, true);
	StartupBootGateMacOS::defer_window_state(1, StartupBootGateMacOS::WINDOW_STATE_TITLEBAR);
	StartupBootGateMacOS::defer_window_state(1, StartupBootGateMacOS::WINDOW_STATE_DPI);
	StartupBootGateMacOS::defer_window_state(1, StartupBootGateMacOS::WINDOW_STATE_RECT);

	recorder.replay();
	REQUIRE(recorder.replayed.size() == 4);
	CHECK(recorder.replayed[0].state == StartupBootGateMacOS::WINDOW_STATE_RECT);
	CHECK(recorder.replayed[1].state == StartupBootGateMacOS::WINDOW_STATE_DPI);
	CHECK(recorder.replayed[2].state == StartupBootGateMacOS::WINDOW_STATE_TITLEBAR);
	CHECK(recorder.replayed[3].state == StartupBootGateMacOS::WINDOW_STATE_FOCUS);
}

TEST_CASE("[StartupSequence][macOS] each window keeps its own coalesced state") {
	GateStateGuard guard;
	ReplayRecorder recorder;
	StartupBootGateMacOS::reset();
	StartupBootGateMacOS::engage();

	StartupBootGateMacOS::defer_window_state(7, StartupBootGateMacOS::WINDOW_STATE_FOCUS, true);
	StartupBootGateMacOS::defer_window_state(9, StartupBootGateMacOS::WINDOW_STATE_FOCUS, false);
	StartupBootGateMacOS::defer_window_state(7, StartupBootGateMacOS::WINDOW_STATE_RECT);

	recorder.replay();
	REQUIRE(recorder.replayed.size() == 3);
	// Windows replay in the order they were first seen, so the ordering is deterministic.
	CHECK(recorder.replayed[0].window_id == 7);
	CHECK(recorder.replayed[0].state == StartupBootGateMacOS::WINDOW_STATE_RECT);
	CHECK(recorder.replayed[1].window_id == 7);
	CHECK(recorder.replayed[1].value);
	CHECK(recorder.replayed[2].window_id == 9);
	CHECK_FALSE(recorder.replayed[2].value);
}

TEST_CASE("[StartupSequence][macOS] requests are discarded rather than replayed") {
	GateStateGuard guard;
	ReplayRecorder recorder;
	StartupBootGateMacOS::reset();

	CHECK_FALSE(StartupBootGateMacOS::should_discard_request());
	StartupBootGateMacOS::engage();
	CHECK(StartupBootGateMacOS::should_discard_request());

	// A close button pressed, or a file dropped, during boot. Replaying either would act on the
	// user's behalf the instant the editor finished launching.
	CHECK_FALSE(StartupBootGateMacOS::has_pending_state());
	recorder.replay();
	CHECK(recorder.replayed.is_empty());
	CHECK_FALSE(StartupBootGateMacOS::should_discard_request());
}

TEST_CASE("[StartupSequence][macOS] an abandoned boot replays nothing and lifts the gate") {
	GateStateGuard guard;
	ReplayRecorder recorder;
	StartupBootGateMacOS::reset();
	StartupBootGateMacOS::engage();
	StartupBootGateMacOS::defer_window_state(1, StartupBootGateMacOS::WINDOW_STATE_RECT);

	// The tree that would have received this does not exist.
	StartupBootGateMacOS::abandon();
	CHECK_FALSE(StartupBootGateMacOS::is_engaged());
	CHECK_FALSE(StartupBootGateMacOS::has_pending_state());

	recorder.replay();
	CHECK(recorder.replayed.is_empty());
}

TEST_CASE("[StartupSequence][macOS] begin engages the boot gate alongside the input gate") {
	GateStateGuard guard;
	StartupBootGateMacOS::reset();
	RecordingSequence sequence;

	CHECK_FALSE(StartupBootGateMacOS::is_engaged());
	sequence.begin();
	CHECK(StartupBootGateMacOS::is_engaged());
	CHECK(StartupInputGateMacOS::is_suppressed());
}

TEST_CASE("[StartupSequence][macOS] window state reaches the tree before input is let through") {
	GateStateGuard guard;
	StartupBootGateMacOS::reset();
	RecordingSequence sequence;
	sequence.begin();

	// The replay has to see input still suppressed. Otherwise a user event could interleave with
	// the state that accumulated while it was being discarded.
	static bool input_suppressed_during_replay = false;
	static bool boot_gate_pending_during_replay = false;
	input_suppressed_during_replay = false;
	boot_gate_pending_during_replay = false;

	StartupBootGateMacOS::defer_window_state(1, StartupBootGateMacOS::WINDOW_STATE_RECT);
	sequence.set_boot_complete_callback(
			[](void *) {
				input_suppressed_during_replay = StartupInputGateMacOS::is_suppressed();
				boot_gate_pending_during_replay = StartupBootGateMacOS::has_pending_state();
			},
			nullptr);

	CHECK(sequence.step() == StartupSequenceMacOS::STEP_PENDING);
	CHECK(sequence.step() == StartupSequenceMacOS::STEP_RUNNING);

	CHECK(input_suppressed_during_replay);
	CHECK(boot_gate_pending_during_replay);
	CHECK_FALSE(StartupInputGateMacOS::is_suppressed());
}

TEST_CASE("[StartupSequence][macOS] a failed boot never replays the state it collected") {
	GateStateGuard guard;
	ReplayRecorder recorder;
	StartupBootGateMacOS::reset();
	RecordingSequence sequence;
	sequence.main_start_result = EXIT_FAILURE;
	sequence.begin();
	StartupBootGateMacOS::defer_window_state(1, StartupBootGateMacOS::WINDOW_STATE_RECT);

	CHECK(sequence.step() == StartupSequenceMacOS::STEP_EXIT_FAILURE);
	// As with the input gate in #2090, a failed boot stays gated here: the caller responds by
	// terminating, and `OS_MacOS_NSApp::terminate()` is what lifts both gates so shutdown can still
	// raise a dialog. What must never happen is the state reaching a tree that was never built.
	CHECK(sequence.main_loop_initialize_calls == 0);
	CHECK_FALSE(recorder.replayed.size() > 0);
}

TEST_CASE("[StartupSequence][macOS] a phase that abandons the boot drops pending window state") {
	GateStateGuard guard;
	StartupBootGateMacOS::reset();
	AbandoningSequence sequence;
	sequence.begin();
	StartupBootGateMacOS::defer_window_state(1, StartupBootGateMacOS::WINDOW_STATE_RECT);

	CHECK(sequence.step() == StartupSequenceMacOS::STEP_EXIT_FAILURE);
	CHECK(sequence.get_phase() == StartupSequenceMacOS::PHASE_FAILED);
	CHECK_FALSE(StartupBootGateMacOS::is_engaged());
	CHECK_FALSE(StartupBootGateMacOS::has_pending_state());
}

} // namespace TestStartupSequenceMacOS
