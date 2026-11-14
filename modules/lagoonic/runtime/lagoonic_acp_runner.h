/**************************************************************************/
/*  lagoonic_acp_runner.h                                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

// Lagoonic ACP runner (desktop editor module).
//
// C++ port of Core/lagoonic-desktop/src/main/acp.ts (AcpConnection,
// AcpMapper, AcpTurn, spawnAcpProcess). Shapes follow
// https://agentclientprotocol.com/protocol/prompt-turn :
// one adapter process per turn, JSON-RPC 2.0 with one message per line
// over stdio. The adapter signs in with the user's own account, so no
// API key is used here; each turn registers the bundled lagoonic-mcp
// as an MCP server.
//
// Sequence per turn (see LagoonicAcpRunner::run_turn):
//
//   spawn -> initialize -> session/new -> session/prompt -> close
//                                          |
//                                     session/cancel (kill on cancel)
//
// Contracts and limits:
// - Exactly 1 child process per turn. spawn() refuses while one is
//   running; run_turn() always closes (kills) the process on exit.
// - session_cancel() sends session/cancel AND kills the process.
// - call()/initialize() block the calling thread: run them on a worker
//   Thread, never on the main thread. poll() pumps I/O and dispatches
//   session/update + session/request_permission; callbacks/signals fire
//   on whichever thread calls poll()/session_prompt().
// - stdout is framed line-by-line; stderr is kept as a capped log
//   (12 lines x 300 chars) for failure messages.
// - Boss turn: silent=false, events go to the dock via the acp_event
//   signal. Worker turn: silent=true, no signals, the report is read
//   with get_worker_report() (first 3 sentences of the answer).
//
// NOTE (build): Engine/godot/modules/lagoonic/SCsub currently compiles
// "*.cpp" (module root) plus "editor/*.cpp" for editor targets, so this
// runtime/*.cpp pair needs one SCsub line to be compiled. That file is
// intentionally left untouched by this change.

#include "core/object/ref_counted.h"

#include "core/templates/hash_map.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

#include <string>

// ---------------------------------------------------------------- mapper

// Accumulates one turn's session/update notifications into contract
// events, so the chat looks the same whichever agent answers. Holds
// the turn's text plus one assistant message carrying the whole text
// so far. Port of AcpMapper in acp.ts.
class LagoonicAcpMapper : public RefCounted {
	GDCLASS(LagoonicAcpMapper, RefCounted);

public:
	struct ToolEntry {
		String name;
		String detail;
	};

	// Tool name for the chat. The ACP kind maps to a short name
	// (execute->Bash, read->Read, ...); a "mcp__lagoonic__X" raw name
	// becomes "Godot: X". Port of acpToolName().
	static String acp_tool_name(const Dictionary &p_call);
	// Best one-line detail for a tool call (command, file_path,
	// scenePath, query, url, ...). Port of acpToolDetail().
	static String acp_tool_detail(const Dictionary &p_call);
	// Option id answering a permission request, or "" when the agent
	// offered none usable. Port of pickAcpOption().
	static String pick_acp_option(const Dictionary &p_params, bool p_approved);
	// Tools whose progress shows in the plan card instead of a row.
	// Port of isHiddenTool(): todowrite, todoread (case-insensitive).
	static bool is_hidden_tool(const String &p_name);
	// Short install hint for a missing adapter binary.
	static String adapter_install_hint(const String &p_adapter_bin);

	LagoonicAcpMapper();

	// Lagoonic-side session id stamped on emitted events.
	void set_session_id(const String &p_session_id);
	// Starts a new assistant message for the next turn; history kept.
	void begin_turn();
	// Feeds one update dict (params.update of session/update) and
	// returns emitted events: {type:"message",...} and
	// {type:"todos",...}. agent_thought_chunk returns no events.
	Array feed_update(const Dictionary &p_update);
	void clear_history();

	String get_text() const;
	int get_tool_count() const;
	// Current assistant message {id, role, text, tools[]}.
	Dictionary build_message() const;
	Array get_history() const;
	// First p_max_sentences non-empty sentences of the accumulated
	// text. Used for the silent worker report.
	String get_worker_report(int p_max_sentences = 3) const;

protected:
	static void _bind_methods();

private:
	StringName session_id; // Lagoonic-side session id for events.
	String text;
	HashMap<String, ToolEntry> tools;
	int message_seq = 0;
	Dictionary current_message; // Empty Dictionary = none yet.
	Array message_list;

	Dictionary _ensure_message();
	Dictionary _message_event();
	Array _on_tool_call(const Dictionary &p_update);
	Array _on_tool_update(const Dictionary &p_update);
};

// ---------------------------------------------------------------- runner

// One adapter process + one JSON-RPC 2.0 session over its stdio.
// Port of AcpConnection + AcpTurn in acp.ts.
class LagoonicAcpRunner : public RefCounted {
	GDCLASS(LagoonicAcpRunner, RefCounted);

public:
	static constexpr int DEFAULT_TIMEOUT_MS = 60000;
	static constexpr int STDERR_MAX_LINES = 12;
	static constexpr int STDERR_MAX_COLS = 300;

	LagoonicAcpRunner();
	~LagoonicAcpRunner();

	// Starts the adapter: adapter_bin + args, working dir cwd,
	// extra environment env {NAME: value}. Fails (ERR_CANT_OPEN with
	// a bilingual last_error + install hint) when the binary is
	// missing, or ERR_BUSY when a turn is already running.
	Error spawn(const String &p_adapter_bin, const PackedStringArray &p_args, const String &p_cwd, const Dictionary &p_env);
	bool is_running() const;
	// ACP initialize handshake. protocol_version=1, client lagoonic/1.
	// Returns the result dict, or {error:...} on timeout/failure.
	Dictionary initialize(int p_protocol_version = 1, const String &p_client_name = "lagoonic", const String &p_client_version = "1", int p_timeout_ms = DEFAULT_TIMEOUT_MS);
	// session/new with cwd + mcpServers [{name,command,args,env[]} |
	// {name,url,headers[]}] (lagoonic-mcp stdio + lagoonic-crew http).
	// Returns the ACP session id, or "" on failure (see last_error).
	String session_new(const String &p_cwd, const Array &p_mcp_servers, int p_timeout_ms = DEFAULT_TIMEOUT_MS);
	// session/prompt. prompt_blocks defaults to [{type:text,...}].
	// Returns {stop_reason:..., } or {error:...}. stopReason
	// cancelled/refusal/max_tokens (and max_turn_requests) surface as
	// bilingual errors, like acp.ts.
	Dictionary session_prompt(const String &p_acp_session_id, const String &p_prompt_text, const Array &p_prompt_blocks = Array(), int p_timeout_ms = 0);
	// session/cancel notify + kill. Idempotent.
	void session_cancel(const String &p_acp_session_id);
	// Answers a permission request previously emitted via the
	// acp_permission signal (no-op when unknown).
	void resolve_permission(const String &p_approval_id, bool p_approved);
	// Kills the process (if any) and fails pending calls. Idempotent.
	void close();
	// Pumps process I/O for up to p_timeout_ms (0 = single pass),
	// dispatching notifications and permission requests.
	void poll(int p_timeout_ms = 0);
	// Full turn in one call: spawn -> initialize -> session/new ->
	// session/prompt -> close (kill). Returns {ok, stop_reason,
	// text, report, error, log}. Exactly 1 process, always reaped.
	Dictionary run_turn(const String &p_adapter_bin, const PackedStringArray &p_args, const String &p_cwd, const Dictionary &p_env, const Array &p_mcp_servers, const String &p_prompt, const Array &p_prompt_blocks = Array(), bool p_auto_approve = false, bool p_silent = false, const String &p_session_id = "lagoonic");

	// Options the agent lets the client choose per session (model,
	// effort, mode...), read from the session/new result: ACP
	// configOptions of type "select", or the older "models" field (set
	// with session/set_model). Each entry: {id, name, category, current,
	// values: [{value, name, description}], via_set_model}. Model first.
	static Array parse_options(const Dictionary &p_session);
	// Starts the adapter just to read its options, then closes it.
	// Blocking: call from a worker thread. Empty on failure.
	Array probe_options(const String &p_adapter_bin, const PackedStringArray &p_args, const String &p_cwd, const Dictionary &p_env);
	// Values run_turn() applies after session/new: option id -> value.
	// The key "mode" with a category "mode" option also accepts the
	// requested_mode below.
	void set_chosen_options(const Dictionary &p_choices);
	// "plan" / "default" / "" : permission mode for the turn, applied to
	// the agent's category "mode" option when it offers that value.
	void set_requested_mode(const String &p_mode);
	// Conversation memory for the next run_turn (adapters run one process
	// per turn): resume p_acp_session with session/load when the agent
	// supports it; otherwise p_transcript (earlier turns as text) is put
	// before the prompt. Both are consumed by run_turn.
	void set_resume(const String &p_acp_session, const String &p_transcript);
	// session/load: replays the conversation into the agent (its replay
	// updates are not shown). Returns false (see last_error) on failure.
	bool session_load(const String &p_acp_session, const String &p_cwd, const Array &p_mcp_servers, int p_timeout_ms = DEFAULT_TIMEOUT_MS);

	void set_session_id(const String &p_session_id);
	String get_session_id() const;
	void set_auto_approve(bool p_auto_approve);
	bool get_auto_approve() const;
	void set_silent(bool p_silent);
	bool get_silent() const;
	String get_acp_session_id() const;
	String get_log_tail() const;
	String get_last_error() const;
	// Accumulated assistant text of the current turn.
	String get_turn_text() const;
	// Silent worker report: first sentences of the turn text.
	String get_worker_report(int p_max_sentences = 3) const;
	Ref<LagoonicAcpMapper> get_mapper() const;

protected:
	static void _bind_methods();

private:
	struct PendingCall {
		bool done = false;
		bool ok = false;
		Dictionary result;
		String error;
	};

	bool running = false;
	bool stopped = false;
	bool auto_approve = false;
	bool silent = false;
	int64_t next_id = 1;
	String acp_session_id;
	String last_error;
	Dictionary session_info; // Last session/new result.
	Dictionary chosen_options;
	String requested_mode;
	String resume_acp_session;
	String resume_transcript;
	bool replaying = false; // session/load in progress: updates are history.
	// Sets the chosen options on the session; returns the options as
	// they are afterwards.
	Array _apply_options(const String &p_acp_session_id, const Array &p_options);

	Ref<LagoonicAcpMapper> mapper;
	HashMap<int64_t, PendingCall> pending;
	Vector<int64_t> pending_order;
	// approval_id -> agent request id (Variant: string or number).
	HashMap<String, Variant> approvals;
	Vector<String> approval_order;
	// approval_id -> full request params, so resolve_permission() can
	// re-run pick_acp_option() like the TS dock flow does.
	HashMap<String, Dictionary> approval_params;
	// Lagoonic-side session id stamped on events (the chat session,
	// not the ACP session id).
	String session_id = "lagoonic";

	Vector<String> stderr_log;
	std::string stdout_buf;
	std::string stderr_buf;

#ifdef _WIN32
	intptr_t proc_handle = 0;
	intptr_t stdin_write_h = 0;
	intptr_t stdout_read_h = 0;
	intptr_t stderr_read_h = 0;
	bool stdout_eof = true;
	bool stderr_eof = true;
#else
	int child_pid = -1;
	int stdin_fd = -1;
	int stdout_fd = -1;
	int stderr_fd = -1;
	bool stdout_eof = true;
	bool stderr_eof = true;
#endif

	void _set_error(const String &p_error);
	void _log_stderr_line(const String &p_line);
	bool _write_stdin(const char *p_data, size_t p_len);
	// Returns false when no process / already exited.
	bool _pump_once();
	void _pump_stdout_lines();
	void _handle_line(const String &p_line);
	void _handle_notification(const String &p_method, const Dictionary &p_params);
	void _handle_request(const Variant &p_id, const String &p_method, const Dictionary &p_params);
	void _handle_permission(const Variant &p_id, const Dictionary &p_params);
	void _send(const Dictionary &p_message);
	// Blocking request/response with timeout. Returns result, or
	// {error:...} inside *r_ok=false.
	Dictionary _call(const String &p_method, const Dictionary &p_params, int p_timeout_ms, bool &r_ok);
	void _reply(const Variant &p_id, const Dictionary &p_result);
	void _reply_error(const Variant &p_id, int p_code, const String &p_message);
	void _notify(const String &p_method, const Dictionary &p_params);
	void _fail_all(const String &p_message);
	void _on_child_exit();
	bool _alive();
	void _kill_child();
	void _close_fds();
	String _new_approval_id();
};
