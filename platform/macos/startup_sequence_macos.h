/**************************************************************************/
/*  startup_sequence_macos.h                                              */
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

#include <cstdint>

// User input arriving while the editor is still booting.
//
// The staged boot below lets the run loop turn between boot phases, which means AppKit will
// happily deliver clicks and key presses to a half-constructed editor. The gate discards those
// events at the `-[FoundryApplication sendEvent:]` ingress, before the responder chain, the
// display server callbacks or the buffered input queue can observe them. Suppression is not the
// same as `DisplayServerMacOS::drop_events()`: accumulated input is buffered rather than dropped,
// so an event that reaches `Input` during boot would still be replayed once boot finishes.
class StartupInputGateMacOS {
	static bool suppressed;

public:
	static void set_suppressed(bool p_suppressed) { suppressed = p_suppressed; }
	static bool is_suppressed() { return suppressed; }

	// `p_ns_event_type` is an `NSEventType`. True for the event types that carry user input;
	// AppKit's own bookkeeping events (window activation, system defined, periodic) keep flowing
	// so window state stays correct while the editor boots.
	static bool is_user_input_event_type(unsigned long p_ns_event_type);

	// True when the event must not be delivered to the application at all.
	//
	// A native modal session — a startup error alert, for instance — drives its own event loop
	// and owns the whole event stream while it is up. Discarding input there would leave a dialog
	// that can never be dismissed, so the gate stands down for as long as one is running.
	static bool should_discard_event(unsigned long p_ns_event_type, bool p_native_modal_session) {
		return suppressed && !p_native_modal_session && is_user_input_event_type(p_ns_event_type);
	}
};

// Window and application callbacks arriving while the editor is still booting.
//
// `StartupInputGateMacOS` above stops user input at the `-[FoundryApplication sendEvent:]` ingress,
// which is necessary but not sufficient. The root `Window` installs its DisplayServer callbacks
// during `NOTIFICATION_ENTER_TREE`, before the rest of the editor tree has entered or become ready,
// so once the outer run loop turns mid-boot AppKit can drive rect, focus, DPI and titlebar
// callbacks straight into a partially built tree. Those arrive as AppKit notifications rather than
// as `NSEvent`s, so they never pass through the input gate at all.
//
// While engaged, the engine sees none of them. State is coalesced per window and replayed once,
// after boot completes, as the final observable state — a window that was resized four times during
// boot produces one rect callback against the finished tree, not four against a half-built one.
class StartupBootGateMacOS {
public:
	// Coalescible window state. Anything not listed here is a *request* rather than state and is
	// discarded outright while gated; see `should_discard_request()`.
	enum WindowState {
		WINDOW_STATE_RECT, // Position or size; the current rect is re-read at replay.
		WINDOW_STATE_DPI,
		WINDOW_STATE_TITLEBAR,
		WINDOW_STATE_FOCUS, // Carries a value: the last focused/unfocused observed wins.
		WINDOW_STATE_MOUSE, // Carries a value: the last enter/exit observed wins.
		WINDOW_STATE_MAX,
	};

	// Replays one coalesced state change. `p_value` is meaningful for FOCUS and MOUSE only.
	typedef void (*ReplayCallback)(int64_t p_window_id, WindowState p_state, bool p_value, void *p_userdata);

	static bool is_engaged() { return engaged; }
	static void engage();

	// Replays coalesced state, then disengages. Input stays gated by the caller across the replay,
	// so a callback cannot be interleaved with input that was discarded while it accumulated.
	static void release_and_replay(ReplayCallback p_replay, void *p_userdata);

	// Drops all pending state without replaying it. For a boot that failed: the tree that would
	// have received the callbacks does not exist.
	static void abandon();

	// True when the engine callback must not run. Records the change for replay.
	static bool defer_window_state(int64_t p_window_id, WindowState p_state, bool p_value = false);

	// True when the caller must drop the event entirely. Requests — a close button, a file drop —
	// are user intent, and the gate discards user intent rather than deferring it. Replaying a
	// boot-time close would quit the editor the instant it finished launching.
	static bool should_discard_request() { return engaged; }

	static bool has_pending_state();

	// Test seam; boot state is process wide.
	static void reset();

private:
	static bool engaged;
};

// Staged boot for the AppKit GUI path.
//
// Running the whole engine and editor boot inline inside `applicationDidFinishLaunching:` leaves
// the run loop unserviced for seconds while a window is already on screen, which macOS reports as
// an unresponsive application (the spinning wait cursor). This sequence splits the remaining boot
// phases so a `kCFRunLoopBeforeWaiting` observer can run exactly one phase per run loop turn.
//
// The phase bodies are virtual so tests can observe the ordering, the failure handling and the
// input gate lifecycle without booting an editor.
class StartupSequenceMacOS {
public:
	enum Phase {
		PHASE_MAIN_START,
		PHASE_MAIN_LOOP_INITIALIZE,
		PHASE_RUNNING,
		PHASE_FAILED,
	};

	enum StepResult {
		STEP_PENDING, // Boot phases remain; the run loop turns before the next one.
		STEP_RUNNING, // Boot finished, steady state main loop iteration takes over.
		STEP_EXIT_SUCCESS,
		STEP_EXIT_FAILURE,
	};

	Phase get_phase() const { return phase; }

	// Runs the phase that is due and returns what the caller must do next. Must be called from
	// the run loop, once per turn.
	//
	// A phase that fails leaves the sequence in `PHASE_FAILED`, which reports `STEP_EXIT_FAILURE`
	// for good: it stopped part way through whatever it was doing, and running it again would
	// repeat the part that already happened.
	//
	// Re-entrant calls do nothing and report `STEP_PENDING`. The observer that drives this is
	// registered in the common run loop modes, which include the modal panel mode, so a boot
	// phase that raises a native alert — a startup error, say — spins a nested run loop that
	// calls straight back in while that same phase is still on the stack. Running it again would
	// raise the alert again, without bound.
	StepResult step();

	// Engages the input gate for the whole boot. Called once, before the first `step()`.
	void begin();

	// Runs once when boot completes, after the main loop is initialized and *before* the input gate
	// is lifted. That ordering is the point: the coalesced window state has to reach the finished
	// tree before user input starts flowing, so a replayed callback cannot interleave with input
	// that arrived while that state was accumulating.
	typedef void (*BootCompleteCallback)(void *p_userdata);
	void set_boot_complete_callback(BootCompleteCallback p_callback, void *p_userdata);

	virtual ~StartupSequenceMacOS() = default;

protected:
	// Mirrors `Main::start()`: `EXIT_SUCCESS` when boot may continue.
	virtual int _main_start();
	// A `Main::start()` that ran a command line tool leaves no main loop behind.
	virtual bool _has_main_loop() const;
	virtual void _main_loop_initialize();

	// Abandons the boot. A phase body calls this when it cannot finish and must not be retried.
	void _fail();

private:
	StepResult _run_phase();

	Phase phase = PHASE_MAIN_START;
	bool stepping = false;
	BootCompleteCallback boot_complete_callback = nullptr;
	void *boot_complete_userdata = nullptr;
};
