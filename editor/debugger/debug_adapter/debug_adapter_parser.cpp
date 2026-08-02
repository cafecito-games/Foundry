/**************************************************************************/
/*  debug_adapter_parser.cpp                                              */
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

#include "debug_adapter_parser.h"

#include "editor/debugger/debug_adapter/debug_adapter_protocol.h"
#include "editor/debugger/editor_debugger_node.h"
#include "editor/debugger/script_editor_debugger.h"
#include "editor/export/editor_export.h"
#include "editor/export/editor_export_platform.h"
#include "editor/run/editor_run_bar.h"
#include "editor/script/script_editor_plugin.h"

void DebugAdapterParser::_bind_methods() {
	// Requests
	ClassDB::bind_method(D_METHOD("req_initialize", "params"), &DebugAdapterParser::req_initialize);
	ClassDB::bind_method(D_METHOD("req_disconnect", "params"), &DebugAdapterParser::req_disconnect);
	ClassDB::bind_method(D_METHOD("req_launch", "params"), &DebugAdapterParser::req_launch);
	ClassDB::bind_method(D_METHOD("req_attach", "params"), &DebugAdapterParser::req_attach);
	ClassDB::bind_method(D_METHOD("req_restart", "params"), &DebugAdapterParser::req_restart);
	ClassDB::bind_method(D_METHOD("req_terminate", "params"), &DebugAdapterParser::req_terminate);
	ClassDB::bind_method(D_METHOD("req_configurationDone", "params"), &DebugAdapterParser::req_configurationDone);
	ClassDB::bind_method(D_METHOD("req_pause", "params"), &DebugAdapterParser::req_pause);
	ClassDB::bind_method(D_METHOD("req_continue", "params"), &DebugAdapterParser::req_continue);
	ClassDB::bind_method(D_METHOD("req_threads", "params"), &DebugAdapterParser::req_threads);
	ClassDB::bind_method(D_METHOD("req_stackTrace", "params"), &DebugAdapterParser::req_stackTrace);
	ClassDB::bind_method(D_METHOD("req_setBreakpoints", "params"), &DebugAdapterParser::req_setBreakpoints);
	ClassDB::bind_method(D_METHOD("req_breakpointLocations", "params"), &DebugAdapterParser::req_breakpointLocations);
	ClassDB::bind_method(D_METHOD("req_scopes", "params"), &DebugAdapterParser::req_scopes);
	ClassDB::bind_method(D_METHOD("req_variables", "params"), &DebugAdapterParser::req_variables);
	ClassDB::bind_method(D_METHOD("req_next", "params"), &DebugAdapterParser::req_next);
	ClassDB::bind_method(D_METHOD("req_stepIn", "params"), &DebugAdapterParser::req_stepIn);
	ClassDB::bind_method(D_METHOD("req_stepOut", "params"), &DebugAdapterParser::req_stepOut);
	ClassDB::bind_method(D_METHOD("req_evaluate", "params"), &DebugAdapterParser::req_evaluate);
	ClassDB::bind_method(D_METHOD("req_godot/put_msg", "params"), &DebugAdapterParser::req_godot_put_msg);
}

Dictionary DebugAdapterParser::prepare_base_event() const {
	Dictionary event;
	event["type"] = "event";

	return event;
}

Dictionary DebugAdapterParser::prepare_success_response(const Dictionary &p_params) const {
	Dictionary response;
	response["type"] = "response";
	response["request_seq"] = p_params["seq"];
	response["command"] = p_params["command"];
	response["success"] = true;

	return response;
}

Dictionary DebugAdapterParser::prepare_error_response(const Dictionary &p_params, DAP::ErrorType err_type, const Dictionary &variables) const {
	Dictionary response, body;
	response["type"] = "response";
	response["request_seq"] = p_params["seq"];
	response["command"] = p_params["command"];
	response["success"] = false;
	response["body"] = body;

	DAP::Message message;
	String error, error_desc;
	switch (err_type) {
		case DAP::ErrorType::WRONG_PATH:
			error = "wrong_path";
			error_desc = "The editor and client are working on different paths; the client is on \"{clientPath}\", but the editor is on \"{editorPath}\"";
			break;
		case DAP::ErrorType::NOT_RUNNING:
			error = "not_running";
			error_desc = "Can't attach to a running session since there isn't one.";
			break;
		case DAP::ErrorType::TIMEOUT:
			error = "timeout";
			error_desc = "Timeout reached while processing a request.";
			break;
		case DAP::ErrorType::UNKNOWN_PLATFORM:
			error = "unknown_platform";
			error_desc = "The specified platform is unknown.";
			break;
		case DAP::ErrorType::MISSING_DEVICE:
			error = "missing_device";
			error_desc = "There's no connected device with specified id.";
			break;
		case DAP::ErrorType::INVALID_LAUNCH:
			error = "invalid_launch";
			error_desc = "The launch configuration is not usable: {reason}";
			break;
		case DAP::ErrorType::UNSUPPORTED_REQUEST:
			error = "unsupported_request";
			error_desc = "Request '{command}' is not supported by this debug adapter.";
			break;
		case DAP::ErrorType::NOT_STOPPED:
			// "notStopped" is the Debug Adapter Protocol's predefined short message for
			// a request that requires a suspended debuggee.
			error = "notStopped";
			error_desc = "The debuggee is running, so it cannot service the '{command}' request.";
			break;
		case DAP::ErrorType::UNKNOWN:
		default:
			error = "unknown";
			error_desc = "An unknown error has occurred when processing the request.";
			break;
	}

	message.id = err_type;
	message.format = error_desc;
	message.variables = variables;
	response["message"] = error;
	body["error"] = message.to_json();

	return response;
}

Dictionary DebugAdapterParser::req_initialize(const Dictionary &p_params) const {
	Dictionary response = prepare_success_response(p_params);
	Dictionary args = p_params["arguments"];

	Ref<DAPeer> peer = DebugAdapterProtocol::get_singleton()->get_current_peer();

	peer->linesStartAt1 = args.get("linesStartAt1", false);
	peer->columnsStartAt1 = args.get("columnsStartAt1", false);
	peer->supportsVariableType = args.get("supportsVariableType", false);
	peer->supportsInvalidatedEvent = args.get("supportsInvalidatedEvent", false);

	DAP::Capabilities caps;
	response["body"] = caps.to_json();

	DebugAdapterProtocol::get_singleton()->notify_initialized();

	if (DebugAdapterProtocol::get_singleton()->_sync_breakpoints) {
		// Send all current breakpoints
		List<String> breakpoints;
		ScriptEditor::get_singleton()->get_breakpoints(&breakpoints);
		for (const String &breakpoint : breakpoints) {
			String path = breakpoint.left(breakpoint.find_char(':', 6)); // Skip initial part of path, aka "res://"
			int line = breakpoint.substr(path.size()).to_int();

			DebugAdapterProtocol::get_singleton()->on_debug_breakpoint_toggled(path, line, true);
		}
	} else {
		// Remove all current breakpoints
		EditorDebuggerNode::get_singleton()->get_default_debugger()->_clear_breakpoints();
	}

	return response;
}

Dictionary DebugAdapterParser::req_disconnect(const Dictionary &p_params) const {
	if (!DebugAdapterProtocol::get_singleton()->get_current_peer()->attached) {
		EditorRunBar::get_singleton()->stop_playing();
	}

	return prepare_success_response(p_params);
}

Dictionary DebugAdapterParser::req_launch(const Dictionary &p_params) const {
	Dictionary args = p_params["arguments"];
	if (args.has("project") && !is_valid_project(args["project"])) {
		Dictionary variables;
		variables["clientPath"] = args["project"];
		variables["editorPath"] = ProjectSettings::get_singleton()->get_resource_path();
		return prepare_error_response(p_params, DAP::ErrorType::WRONG_PATH, variables);
	}

	LaunchRequest launch_request;
	String launch_error;
	if (!parse_launch_request(args, launch_request, launch_error)) {
		Dictionary variables;
		variables["reason"] = launch_error;
		return prepare_error_response(p_params, DAP::ErrorType::INVALID_LAUNCH, variables);
	}

	if (args.has("godot/custom_data")) {
		DebugAdapterProtocol::get_singleton()->get_current_peer()->supportsCustomData = args["godot/custom_data"];
	}

	DebugAdapterProtocol::get_singleton()->get_current_peer()->pending_launch = p_params;

	return Dictionary();
}

// Reads a required string field without letting Variant's implicit conversions turn
// a JSON number, boolean, or object into a plausible-looking path.
static bool read_launch_string(const Dictionary &p_source, const String &p_key, String &r_value) {
	if (!p_source.has(p_key)) {
		return false;
	}
	const Variant value = p_source[p_key];
	if (value.get_type() != Variant::STRING && value.get_type() != Variant::STRING_NAME) {
		return false;
	}
	r_value = value;
	return !r_value.is_empty();
}

bool DebugAdapterParser::parse_launch_request(const Dictionary &p_arguments, LaunchRequest &r_request, String &r_error) {
	r_request = LaunchRequest();

	if (!p_arguments.has("foundry/launch")) {
		return true;
	}

	const Variant launch_value = p_arguments["foundry/launch"];
	if (launch_value.get_type() != Variant::DICTIONARY) {
		r_error = "\"foundry/launch\" must be an object.";
		return false;
	}

	const Dictionary launch = launch_value;
	String kind;
	if (!read_launch_string(launch, "kind", kind) || kind != "project_test") {
		r_error = vformat("unsupported launch kind \"%s\"; expected \"project_test\".", kind);
		return false;
	}
	r_request.kind = LaunchRequest::KIND_PROJECT_TEST;

	String runner;
	if (!read_launch_string(launch, "runner", runner)) {
		r_error = "a \"project_test\" launch requires a \"runner\" script path.";
		return false;
	}
	r_request.test_launch.runner = runner;

	const Variant adapter_value = launch.get("adapter", Variant());
	if (adapter_value.get_type() != Variant::DICTIONARY) {
		r_error = "a \"project_test\" launch requires an \"adapter\" object.";
		return false;
	}

	const Dictionary adapter = adapter_value;
	const Variant version_value = adapter.get("protocolVersion", Variant());
	// JSON has a single number type, so a whole float is accepted, but a fractional
	// one must not be silently truncated into a version the client never asked for.
	bool version_is_whole_number = version_value.get_type() == Variant::INT;
	if (version_value.get_type() == Variant::FLOAT) {
		const double version_number = version_value;
		version_is_whole_number = version_number == Math::floor(version_number);
	}
	if (!version_is_whole_number) {
		r_error = "the launch adapter requires an integer \"protocolVersion\".";
		return false;
	}
	const int protocol_version = version_value;
	if (protocol_version != EditorRun::TEST_ADAPTER_PROTOCOL_VERSION) {
		r_error = vformat(
				"unsupported runner adapter protocol version %d; this editor speaks version %d.",
				protocol_version,
				EditorRun::TEST_ADAPTER_PROTOCOL_VERSION);
		return false;
	}
	r_request.test_launch.adapter_protocol_version = protocol_version;

	// The TAP report is the authoritative result of a run, and the runner rejects an
	// invocation without one, so an absent report is refused before anything starts.
	String report_path;
	if (!read_launch_string(adapter, "report", report_path)) {
		r_error = "the launch adapter requires a \"report\" path.";
		return false;
	}
	r_request.test_launch.report_path = report_path;

	const Variant ids_value = adapter.get("testIds", Variant());
	if (ids_value.get_type() != Variant::NIL) {
		if (ids_value.get_type() != Variant::ARRAY) {
			r_error = "the launch adapter's \"testIds\" must be an array of strings.";
			return false;
		}
		const Array ids = ids_value;
		for (const Variant &id : ids) {
			if (id.get_type() != Variant::STRING && id.get_type() != Variant::STRING_NAME) {
				r_error = "the launch adapter's \"testIds\" must be an array of strings.";
				return false;
			}
			const String test_id = id;
			if (test_id.is_empty()) {
				r_error = "the launch adapter's \"testIds\" must not contain empty ids.";
				return false;
			}
			// Repeated ids are preserved verbatim: the selection the client sent is the
			// selection the runner receives.
			r_request.test_launch.test_ids.push_back(test_id);
		}
	}

	return true;
}

Vector<String> DebugAdapterParser::_extract_play_arguments(const Dictionary &p_args) const {
	Vector<String> play_args;
	if (p_args.has("playArgs")) {
		Variant v = p_args["playArgs"];
		if (v.get_type() == Variant::ARRAY) {
			Array arr = v;
			for (const Variant &arg : arr) {
				play_args.push_back(String(arg));
			}
		}
	}
	return play_args;
}

Dictionary DebugAdapterParser::_launch_process(const Dictionary &p_params) const {
	Dictionary args = p_params["arguments"];
	ScriptEditorDebugger *dbg = EditorDebuggerNode::get_singleton()->get_default_debugger();
	if ((bool)args["noDebug"] != dbg->is_skip_breakpoints()) {
		dbg->debug_skip_breakpoints();
	}

	LaunchRequest launch_request;
	String launch_error;
	if (!parse_launch_request(args, launch_request, launch_error)) {
		Dictionary variables;
		variables["reason"] = launch_error;
		return prepare_error_response(p_params, DAP::ErrorType::INVALID_LAUNCH, variables);
	}

	String platform_string = args.get("platform", "host");
	if (launch_request.kind == LaunchRequest::KIND_PROJECT_TEST) {
		if (platform_string != "host") {
			Dictionary variables;
			variables["reason"] = "a \"project_test\" launch only runs on the host platform.";
			return prepare_error_response(p_params, DAP::ErrorType::INVALID_LAUNCH, variables);
		}

		const Error err = EditorRunBar::get_singleton()->play_project_test(launch_request.test_launch);
		if (err != OK) {
			return prepare_error_response(p_params, DAP::ErrorType::UNKNOWN);
		}
	} else if (platform_string == "host") {
		Vector<String> play_args = _extract_play_arguments(args);
		const String scene = args.get("scene", "main");
		if (scene == "main") {
			EditorRunBar::get_singleton()->play_main_scene(false, play_args);
		} else if (scene == "current") {
			EditorRunBar::get_singleton()->play_current_scene(false, play_args);
		} else {
			EditorRunBar::get_singleton()->play_custom_scene(scene, play_args);
		}
	} else {
		// Not limited to Android, iOS, Web.
		const int platform_idx = EditorExport::get_singleton()->get_export_platform_index_by_name(platform_string);
		if (platform_idx == -1) {
			return prepare_error_response(p_params, DAP::ErrorType::UNKNOWN_PLATFORM);
		}

		// If it is not passed, would mean first device of this platform.
		const int device_idx = args.get("device", 0);

		const EditorRunBar *run_bar = EditorRunBar::get_singleton();
		const int encoded_id = EditorExport::encode_platform_device_id(platform_idx, device_idx);
		const Error err = run_bar->start_native_device(encoded_id);
		if (err) {
			if (err == ERR_INVALID_PARAMETER) {
				return prepare_error_response(p_params, DAP::ErrorType::MISSING_DEVICE);
			} else {
				return prepare_error_response(p_params, DAP::ErrorType::UNKNOWN);
			}
		}
	}

	DebugAdapterProtocol *protocol = DebugAdapterProtocol::get_singleton();
	// Starting a launch resets EditorDebuggerNode's debugger server and its breakpoint map.
	// Restore the DAP-owned breakpoint registrations without changing their protocol IDs.
	protocol->reregister_breakpoints_after_launch();
	protocol->get_current_peer()->attached = false;
	protocol->notify_process();

	return prepare_success_response(p_params);
}

Dictionary DebugAdapterParser::req_attach(const Dictionary &p_params) const {
	ScriptEditorDebugger *dbg = EditorDebuggerNode::get_singleton()->get_default_debugger();
	if (!dbg->is_session_active()) {
		return prepare_error_response(p_params, DAP::ErrorType::NOT_RUNNING);
	}

	// An attached debuggee is not owned by this editor, so its session can only ever
	// end without a process result.
	EditorDebuggerNode::get_singleton()->begin_unowned_debug_session();
	DebugAdapterProtocol::get_singleton()->get_current_peer()->attached = true;
	DebugAdapterProtocol::get_singleton()->notify_process();
	return prepare_success_response(p_params);
}

Dictionary DebugAdapterParser::req_restart(const Dictionary &p_params) const {
	// Extract embedded "arguments" so it can be given to req_launch/req_attach
	Dictionary params = p_params, args;
	args = params["arguments"];
	args = args["arguments"];
	params["arguments"] = args;

	Dictionary response = DebugAdapterProtocol::get_singleton()->get_current_peer()->attached ? req_attach(params) : _launch_process(params);
	if (!response["success"]) {
		response["command"] = p_params["command"];
		return response;
	}

	return prepare_success_response(p_params);
}

Dictionary DebugAdapterParser::req_terminate(const Dictionary &p_params) const {
	EditorRunBar::get_singleton()->stop_playing();

	return prepare_success_response(p_params);
}

Dictionary DebugAdapterParser::req_configurationDone(const Dictionary &p_params) const {
	Ref<DAPeer> peer = DebugAdapterProtocol::get_singleton()->get_current_peer();
	if (!peer->pending_launch.is_empty()) {
		peer->res_queue.push_back(_launch_process(peer->pending_launch));
		peer->pending_launch.clear();
	}

	return prepare_success_response(p_params);
}

Dictionary DebugAdapterParser::req_pause(const Dictionary &p_params) const {
	EditorDebuggerNode *debugger_node = EditorDebuggerNode::get_singleton();
	ScriptEditorDebugger *dbg = debugger_node->get_default_debugger();
	if (!dbg->is_session_active()) {
		return prepare_error_response(p_params, DAP::ErrorType::NOT_RUNNING);
	}

	// Driving the debugger directly instead of toggling the run bar's pause widget
	// keeps this path working in a headless tooling host, where no one ever presses
	// that button. The widget is only kept in sync for an editor that has a GUI.
	EditorRunBar::get_singleton()->get_pause_button()->set_pressed_no_signal(true);

	DebugAdapterProtocol *protocol = DebugAdapterProtocol::get_singleton();
	if (dbg->is_breaked()) {
		// Already stopped, so no further stack dump is coming.
		protocol->notify_stopped_paused();
	} else {
		protocol->request_pause();
		debugger_node->debug_break();
	}

	return prepare_success_response(p_params);
}

Dictionary DebugAdapterParser::req_continue(const Dictionary &p_params) const {
	EditorDebuggerNode *debugger_node = EditorDebuggerNode::get_singleton();
	ScriptEditorDebugger *dbg = debugger_node->get_default_debugger();
	if (!dbg->is_session_active()) {
		return prepare_error_response(p_params, DAP::ErrorType::NOT_RUNNING);
	}

	EditorRunBar::get_singleton()->get_pause_button()->set_pressed_no_signal(false);

	if (dbg->is_breaked()) {
		debugger_node->debug_continue();
	}

	DebugAdapterProtocol::get_singleton()->notify_continued();

	return prepare_success_response(p_params);
}

Dictionary DebugAdapterParser::req_threads(const Dictionary &p_params) const {
	Dictionary response = prepare_success_response(p_params), body;
	response["body"] = body;

	DAP::Thread thread;

	thread.id = 1; // Hardcoded because Godot only supports debugging one thread at the moment
	thread.name = "Main";
	Array arr = { thread.to_json() };
	body["threads"] = arr;

	return response;
}

Dictionary DebugAdapterParser::req_stackTrace(const Dictionary &p_params) const {
	if (DebugAdapterProtocol::get_singleton()->_processing_stackdump) {
		return Dictionary();
	}

	Dictionary response = prepare_success_response(p_params), body;
	response["body"] = body;

	bool lines_at_one = DebugAdapterProtocol::get_singleton()->get_current_peer()->linesStartAt1;
	bool columns_at_one = DebugAdapterProtocol::get_singleton()->get_current_peer()->columnsStartAt1;

	Array arr;
	DebugAdapterProtocol *dap = DebugAdapterProtocol::get_singleton();
	for (DAP::StackFrame sf : dap->stackframe_list) {
		if (!lines_at_one) {
			sf.line--;
		}
		if (!columns_at_one) {
			sf.column--;
		}

		arr.push_back(sf.to_json());
	}

	body["stackFrames"] = arr;
	return response;
}

Dictionary DebugAdapterParser::req_setBreakpoints(const Dictionary &p_params) const {
	Dictionary response = prepare_success_response(p_params), body;
	response["body"] = body;

	Dictionary args = p_params["arguments"];
	DAP::Source source;
	source.from_json(args["source"]);

	bool lines_at_one = DebugAdapterProtocol::get_singleton()->get_current_peer()->linesStartAt1;

	if (!is_valid_path(source.path)) {
		Dictionary variables;
		variables["clientPath"] = source.path;
		variables["editorPath"] = ProjectSettings::get_singleton()->get_resource_path();
		return prepare_error_response(p_params, DAP::ErrorType::WRONG_PATH, variables);
	}

	// If path contains \, it's a Windows path, so we need to convert it to /, and make the drive letter uppercase
	if (source.path.contains_char('\\')) {
		source.path = source.path.replace_char('\\', '/');
		source.path = source.path.substr(0, 1).to_upper() + source.path.substr(1);
	}

	Array breakpoints = args["breakpoints"], lines;
	for (int i = 0; i < breakpoints.size(); i++) {
		DAP::SourceBreakpoint breakpoint;
		breakpoint.from_json(breakpoints[i]);

		lines.push_back(breakpoint.line + !lines_at_one);
	}

	// Always update the source checksum for the requested path, as it might have been modified externally.
	DebugAdapterProtocol::get_singleton()->update_source(source.path);
	Array updated_breakpoints = DebugAdapterProtocol::get_singleton()->update_breakpoints(source.path, lines);
	body["breakpoints"] = updated_breakpoints;

	return response;
}

Dictionary DebugAdapterParser::req_breakpointLocations(const Dictionary &p_params) const {
	Dictionary response = prepare_success_response(p_params), body;
	response["body"] = body;
	Dictionary args = p_params["arguments"];

	DAP::BreakpointLocation location;
	location.line = args["line"];
	if (args.has("endLine")) {
		location.endLine = args["endLine"];
	}
	Array locations = { location.to_json() };

	body["breakpoints"] = locations;
	return response;
}

Dictionary DebugAdapterParser::req_scopes(const Dictionary &p_params) const {
	Dictionary response = prepare_success_response(p_params), body;
	response["body"] = body;

	Dictionary args = p_params["arguments"];
	int frame_id = args["frameId"];
	Array scope_list;

	HashMap<DebugAdapterProtocol::DAPStackFrameID, Vector<int>>::Iterator E = DebugAdapterProtocol::get_singleton()->scope_list.find(frame_id);
	if (E) {
		const Vector<int> &scope_ids = E->value;
		ERR_FAIL_COND_V(scope_ids.size() != 3, prepare_error_response(p_params, DAP::ErrorType::UNKNOWN));
		for (int i = 0; i < 3; ++i) {
			DAP::Scope scope;
			scope.variablesReference = scope_ids[i];
			switch (i) {
				case 0:
					scope.name = "Locals";
					scope.presentationHint = "locals";
					break;
				case 1:
					scope.name = "Members";
					scope.presentationHint = "members";
					break;
				case 2:
					scope.name = "Globals";
					scope.presentationHint = "globals";
			}

			scope_list.push_back(scope.to_json());
		}
	}

	// The references above are usable straight away, but the values behind them only
	// exist once the debuggee answers this request.
	DebugAdapterProtocol::get_singleton()->request_stack_frame_vars(frame_id);

	body["scopes"] = scope_list;
	return response;
}

Dictionary DebugAdapterParser::req_variables(const Dictionary &p_params) const {
	DebugAdapterProtocol *protocol = DebugAdapterProtocol::get_singleton();

	// If _remaining_vars > 0, the debuggee is still sending a stack dump to the editor.
	if (protocol->_remaining_vars > 0) {
		return Dictionary();
	}

	Dictionary args = p_params["arguments"];
	int variable_id = args["variablesReference"];

	if (HashMap<int, Array>::Iterator E = protocol->variable_list.find(variable_id); E) {
		Dictionary response = prepare_success_response(p_params);
		Dictionary body;
		response["body"] = body;

		if (!protocol->get_current_peer()->supportsVariableType) {
			for (int i = 0; i < E->value.size(); i++) {
				Dictionary variable = E->value[i];
				variable.erase("type");
			}
		}

		body["variables"] = E ? E->value : Array();
		return response;
	}

	// `scopes` hands a client usable references before the debuggee has delivered that
	// frame's values, so a `variables` request naming one of them is legal even though
	// nothing backs it yet. The response is deferred until the values land; if nothing is
	// in flight for that frame, ask for them here so a client that never repeats `scopes`
	// still completes.
	const int frame_id = protocol->search_scope_frame_id(variable_id);
	if (frame_id >= 0) {
		if (protocol->_pending_frame_vars.has(frame_id) || protocol->request_stack_frame_vars(frame_id)) {
			return Dictionary();
		}
	}

	// If the requested variable is an object, it needs to be requested from the debuggee.
	ObjectID object_id = protocol->search_object_id(variable_id);

	if (object_id.is_null()) {
		return prepare_error_response(p_params, DAP::ErrorType::UNKNOWN);
	}

	protocol->request_remote_object(object_id);
	return Dictionary();
}

Dictionary DebugAdapterParser::req_next(const Dictionary &p_params) const {
	ScriptEditorDebugger *dbg = EditorDebuggerNode::get_singleton()->get_default_debugger();
	if (!dbg->is_session_active()) {
		return prepare_error_response(p_params, DAP::ErrorType::NOT_RUNNING);
	}
	// `debug_next()` asserts on a running debuggee, and marking the session as stepping
	// before that check would misreport the next unrelated stop as a step.
	if (!dbg->is_breaked()) {
		Dictionary variables;
		variables["command"] = p_params["command"];
		return prepare_error_response(p_params, DAP::ErrorType::NOT_STOPPED, variables);
	}

	dbg->debug_next();
	DebugAdapterProtocol::get_singleton()->_stepping = true;

	return prepare_success_response(p_params);
}

Dictionary DebugAdapterParser::req_stepIn(const Dictionary &p_params) const {
	ScriptEditorDebugger *dbg = EditorDebuggerNode::get_singleton()->get_default_debugger();
	if (!dbg->is_session_active()) {
		return prepare_error_response(p_params, DAP::ErrorType::NOT_RUNNING);
	}
	// `debug_step()` asserts on a running debuggee, and marking the session as stepping
	// before that check would misreport the next unrelated stop as a step.
	if (!dbg->is_breaked()) {
		Dictionary variables;
		variables["command"] = p_params["command"];
		return prepare_error_response(p_params, DAP::ErrorType::NOT_STOPPED, variables);
	}

	dbg->debug_step();
	DebugAdapterProtocol::get_singleton()->_stepping = true;

	return prepare_success_response(p_params);
}

Dictionary DebugAdapterParser::req_stepOut(const Dictionary &p_params) const {
	ScriptEditorDebugger *dbg = EditorDebuggerNode::get_singleton()->get_default_debugger();
	if (!dbg->is_session_active()) {
		return prepare_error_response(p_params, DAP::ErrorType::NOT_RUNNING);
	}
	// `debug_out()` asserts on a running debuggee, and marking the session as stepping
	// before that check would misreport the next unrelated stop as a step.
	if (!dbg->is_breaked()) {
		Dictionary variables;
		variables["command"] = p_params["command"];
		return prepare_error_response(p_params, DAP::ErrorType::NOT_STOPPED, variables);
	}

	dbg->debug_out();
	DebugAdapterProtocol::get_singleton()->_stepping = true;

	return prepare_success_response(p_params);
}

Dictionary DebugAdapterParser::req_evaluate(const Dictionary &p_params) const {
	Dictionary args = p_params["arguments"];
	String expression = args["expression"];
	int frame_id = args.has("frameId") ? static_cast<int>(args["frameId"]) : DebugAdapterProtocol::get_singleton()->_current_frame;

	if (HashMap<String, DAP::Variable>::Iterator E = DebugAdapterProtocol::get_singleton()->eval_list.find(expression); E) {
		Dictionary response = prepare_success_response(p_params);
		Dictionary body;
		response["body"] = body;

		DAP::Variable var = E->value;

		body["result"] = var.value;
		body["variablesReference"] = var.variablesReference;

		// Since an evaluation can alter the state of the debuggee, they are volatile, and should only be used once
		DebugAdapterProtocol::get_singleton()->eval_list.erase(E->key);
		return response;
	} else {
		DebugAdapterProtocol::get_singleton()->request_remote_evaluate(expression, frame_id);
	}
	return Dictionary();
}

Dictionary DebugAdapterParser::req_godot_put_msg(const Dictionary &p_params) const {
	Dictionary args = p_params["arguments"];

	String msg = args["message"];
	Array data = args["data"];

	EditorDebuggerNode::get_singleton()->get_default_debugger()->_put_msg(msg, data);

	return prepare_success_response(p_params);
}

Dictionary DebugAdapterParser::ev_initialized() const {
	Dictionary event = prepare_base_event();
	event["event"] = "initialized";

	return event;
}

Dictionary DebugAdapterParser::ev_process(const String &p_command) const {
	Dictionary event = prepare_base_event(), body;
	event["event"] = "process";
	event["body"] = body;

	body["name"] = OS::get_singleton()->get_executable_path();
	body["startMethod"] = p_command;

	return event;
}

Dictionary DebugAdapterParser::ev_terminated() const {
	Dictionary event = prepare_base_event();
	event["event"] = "terminated";

	return event;
}

Dictionary DebugAdapterParser::ev_exited(const int &p_exitcode) const {
	Dictionary event = prepare_base_event(), body;
	event["event"] = "exited";
	event["body"] = body;

	body["exitCode"] = p_exitcode;

	return event;
}

Dictionary DebugAdapterParser::ev_stopped() const {
	Dictionary event = prepare_base_event(), body;
	event["event"] = "stopped";
	event["body"] = body;

	body["threadId"] = 1;

	return event;
}

Dictionary DebugAdapterParser::ev_stopped_paused() const {
	Dictionary event = ev_stopped();
	Dictionary body = event["body"];

	body["reason"] = "paused";
	body["description"] = "Paused";

	return event;
}

Dictionary DebugAdapterParser::ev_stopped_exception(const String &p_error) const {
	Dictionary event = ev_stopped();
	Dictionary body = event["body"];

	body["reason"] = "exception";
	body["description"] = "Exception";
	body["text"] = p_error;

	return event;
}

Dictionary DebugAdapterParser::ev_stopped_breakpoint(const int &p_id) const {
	Dictionary event = ev_stopped();
	Dictionary body = event["body"];

	body["reason"] = "breakpoint";
	body["description"] = "Breakpoint";

	Array breakpoints = { p_id };
	body["hitBreakpointIds"] = breakpoints;

	return event;
}

Dictionary DebugAdapterParser::ev_stopped_step() const {
	Dictionary event = ev_stopped();
	Dictionary body = event["body"];

	body["reason"] = "step";
	body["description"] = "Breakpoint";

	return event;
}

Dictionary DebugAdapterParser::ev_continued() const {
	Dictionary event = prepare_base_event(), body;
	event["event"] = "continued";
	event["body"] = body;

	body["threadId"] = 1;

	return event;
}

Dictionary DebugAdapterParser::ev_output(const String &p_message, RemoteDebugger::MessageType p_type) const {
	Dictionary event = prepare_base_event(), body;
	event["event"] = "output";
	event["body"] = body;

	body["category"] = (p_type == RemoteDebugger::MessageType::MESSAGE_TYPE_ERROR) ? "stderr" : "stdout";
	body["output"] = p_message + "\r\n";

	return event;
}

Dictionary DebugAdapterParser::ev_breakpoint(const DAP::Breakpoint &p_breakpoint, const bool &p_enabled) const {
	Dictionary event = prepare_base_event(), body;
	event["event"] = "breakpoint";
	event["body"] = body;

	body["reason"] = p_enabled ? "new" : "removed";
	body["breakpoint"] = p_breakpoint.to_json();

	return event;
}

Dictionary DebugAdapterParser::ev_custom_data(const String &p_msg, const Array &p_data) const {
	Dictionary event = prepare_base_event(), body;
	event["event"] = "godot/custom_data";
	event["body"] = body;

	body["message"] = p_msg;
	body["data"] = p_data;

	return event;
}
