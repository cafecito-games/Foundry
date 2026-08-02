/**************************************************************************/
/*  debug_adapter_protocol.h                                              */
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

#include "core/debugger/debugger_marshalls.h"
#include "core/debugger/remote_debugger.h"
#include "core/io/stream_peer_tcp.h"
#include "core/io/tcp_server.h"
#include "editor/debugger/debug_adapter/debug_adapter_types.h"
#include "scene/debugger/scene_debugger.h"

#define DAP_MAX_BUFFER_SIZE 4194304 // 4MB
#define DAP_MAX_CLIENTS 8

class DebugAdapterParser;

struct DAPeer : RefCounted {
	Ref<StreamPeerTCP> connection;

	uint8_t req_buf[DAP_MAX_BUFFER_SIZE];
	int req_pos = 0;
	bool has_header = false;
	int content_length = 0;
	List<Dictionary> res_queue;
	int seq = 0;
	uint64_t timestamp = 0;

	// Client specific info
	bool linesStartAt1 = false;
	bool columnsStartAt1 = false;
	bool supportsVariableType = false;
	bool supportsInvalidatedEvent = false;
	bool supportsCustomData = false;

	// Internal client info
	bool attached = false;
	Dictionary pending_launch;

	Error handle_data();
	Error send_data();
	Vector<uint8_t> format_output(const Dictionary &p_params) const;
};

class DebugAdapterProtocol : public Object {
	FOUNDRY_CLASS(DebugAdapterProtocol, Object)

	friend class DebugAdapterParser;

	using DAPVarID = int;
	using DAPStackFrameID = int;

private:
	static DebugAdapterProtocol *singleton;
	DebugAdapterParser *parser = nullptr;

	List<Ref<DAPeer>> clients;
	Ref<TCPServer> server;

	Error on_client_connected();
	void on_client_disconnected(const Ref<DAPeer> &p_peer);
	void on_debug_paused();
	// The coordinated end of a debug session, carrying the debuggee's real result when
	// one could be recovered. The debugger socket closing is not proof of a result, so
	// it is deliberately not consumed here.
	void on_debug_session_ended(int64_t p_launch_id, bool p_has_result, int p_exit_code);
	void on_debug_output(const String &p_message, int p_type);
	void on_debug_breaked(const bool &p_reallydid, const bool &p_can_debug, const String &p_reason, const bool &p_has_stackdump);
	void on_debug_breakpoint_toggled(const String &p_path, const int &p_line, const bool &p_enabled);
	void on_debug_stack_dump(const Array &p_stack_dump);
	void on_debug_stack_frame_vars(const int &p_size);
	void on_debug_stack_frame_var(const Array &p_data);
	void on_debug_data(const String &p_msg, const Array &p_data);

	void reset_current_info();
	void reset_ids();
	void reset_session_state();
	void reset_stack_info();

	int parse_variant(const Variant &p_var);
	void parse_object(SceneDebuggerObject &p_obj);
	const Variant parse_object_variable(const SceneDebuggerObject::SceneDebuggerProperty &p_property);
	void parse_evaluation(DebuggerMarshalls::ScriptStackVariable &p_var);

	ObjectID search_object_id(DAPVarID p_var_id);
	// The stack frame owning `p_var_id` as one of its scope references, or `-1` when the
	// reference does not name a scope.
	DAPStackFrameID search_scope_frame_id(DAPVarID p_var_id) const;
	// Asks the debuggee for the values of `p_frame_id` and marks them as pending, so a
	// `variables` request naming one of that frame's scope references can be deferred
	// until the values arrive. Returns false when the debuggee cannot service it.
	bool request_stack_frame_vars(DAPStackFrameID p_frame_id);
	bool request_remote_object(const ObjectID &p_object_id);
	bool request_remote_evaluate(const String &p_eval, int p_stack_frame);

	const DAP::Source &fetch_source(const String &p_path);
	void update_source(const String &p_path);

	bool _initialized = false;
	bool _processing_breakpoint = false;
	bool _stepping = false;
	bool _processing_stackdump = false;
	// A client-requested pause is only reported as `stopped` once the debuggee's
	// asynchronous stack dump has arrived, so the stack trace the client asks for
	// immediately afterwards is already populated.
	bool _pending_pause = false;
	// The launch this adapter currently reports lifecycle events for. A session ended
	// notification for any other launch identity is stale and is ignored.
	bool _debug_session_live = false;
	uint64_t _active_launch_id = 0;
	int _remaining_vars = 0;
	// The frame this adapter is waiting on values for, or `-1` when nothing is
	// outstanding. The scope references `scopes` already handed out are unresolvable for
	// that whole window, so requests naming them wait instead of failing.
	int _awaited_frame_vars = -1;
	// The frame the values currently arriving belong to, or `-1` when they answer a
	// request this adapter cannot attribute.
	int _frame_vars_frame = -1;
	int _current_frame = 0;
	uint64_t _request_timeout = 5000;
	bool _sync_breakpoints = false;
	bool _reregistering_breakpoints = false;

	String _current_request;
	Ref<DAPeer> _current_peer;

	int breakpoint_id = 0;
	int stackframe_id = 0;
	DAPVarID variable_id = 0;
	List<DAP::Breakpoint> breakpoint_list;
	HashMap<String, DAP::Source> breakpoint_source_list;
	List<DAP::StackFrame> stackframe_list;
	HashMap<DAPStackFrameID, Vector<int>> scope_list;
	HashMap<DAPVarID, Array> variable_list;

	HashMap<ObjectID, DAPVarID> object_list;
	HashSet<ObjectID> object_pending_set;

	HashMap<String, DAP::Variable> eval_list;
	HashSet<String> eval_pending_list;

public:
	friend class DebugAdapterServer;

	_FORCE_INLINE_ static DebugAdapterProtocol *get_singleton() { return singleton; }
	_FORCE_INLINE_ bool is_active() const { return _initialized && clients.size() > 0; }

	bool process_message(const String &p_text);

	String get_current_request() const { return _current_request; }
	Ref<DAPeer> get_current_peer() const { return _current_peer; }

	void notify_initialized();
	void notify_process();
	void notify_terminated();
	// Only ever called with a result the debuggee actually produced; an unknown result
	// is reported by omitting this event entirely.
	void notify_exited(const int &p_exitcode);
	void notify_stopped_paused();
	void notify_stopped_exception(const String &p_error);
	void notify_stopped_breakpoint(const int &p_id);
	void notify_stopped_step();
	void notify_continued();
	// Latches a client-requested pause so the `stopped` event is deferred until the
	// stack dump lands; call `resolve_pending_pause()` when no dump will arrive.
	void request_pause();
	bool has_pending_pause() const { return _pending_pause; }
	void resolve_pending_pause();
	void notify_output(const String &p_message, RemoteDebugger::MessageType p_type);
	void notify_custom_data(const String &p_msg, const Array &p_data);
	void notify_breakpoint(const DAP::Breakpoint &p_breakpoint, const bool &p_enabled);

	Array update_breakpoints(const String &p_path, const Array &p_lines);
	void reregister_breakpoints_after_launch();
	static bool can_verify_breakpoint(const String &p_path, int p_line);
	static String breakpoint_script_path(const String &p_client_path, const String &p_resource_path);

	void poll();
	Error start(int p_port, const IPAddress &p_bind_ip);
	// Actual bound port, which differs from the requested one when 0 was requested.
	int get_local_port() const;
	void stop();

	DebugAdapterProtocol();
	~DebugAdapterProtocol();
};
