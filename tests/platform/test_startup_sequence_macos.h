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
	~GateStateGuard() { StartupInputGateMacOS::set_suppressed(previous); }
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

} // namespace TestStartupSequenceMacOS
