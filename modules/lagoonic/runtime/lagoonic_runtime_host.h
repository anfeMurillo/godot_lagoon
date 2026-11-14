/**************************************************************************/
/*  lagoonic_runtime_host.h                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             LAGOONIC ENGINE                            */
/*                   https://github.com/lagoonic/engine                   */
/**************************************************************************/
/* Copyright (c) 2026 Lagoonic contributors.                              */
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
/* License: MIT, same as the Godot Engine (see LICENSE.txt upstream).    */
/**************************************************************************/

#pragma once

// LagoonicRuntimeHost: owns the whole agent runtime for the editor dock.
//
// Single owner of LagoonicSupervisor + LagoonicOpenCodeClient +
// LagoonicEventMapper + LagoonicCrewOrchestrator. The LagoonicDock never
// touches those classes directly: it creates one host, connects the two
// signals below, and translates LagoonicAgentDock events into host calls
// (see editor/DOCK_WIRING.md for the 5-10 line patch the coordinator
// applies once the FIX-* agents land).
//
// Threading contract (mirrors the owners' headers):
//   - Call every method of this class from the main thread only.
//   - The SSE poll already runs on the client's secondary thread; its
//     frames arrive here via call_deferred (_on_sse_frame) and are
//     re-emitted as runtime_event signals. No heavy work runs on the
//     main thread: no diff fetch, no history load, no new threads
//     (crew rounds reuse the orchestrator's own round thread).
//   - Blocking client calls (prompt/abort/respond/...) are serialized
//     with client_mutex because the crew round thread also uses the
//     client (worker turns). Each call is one localhost round-trip.
//   - Server death mid-turn: supervisor's unexpected_exit marks the host
//     down and cancels the crew (like supervisor.ts onUnexpectedExit);
//     the next send_prompt() lazily restarts via ensure_server().
//   - No secrets are ever logged: error strings carry HTTP status +
//     truncated bodies only, never the server password.
//
// Signals:
//   runtime_event(Dictionary event) ... one AgentEvent mirror per SSE
//     frame (message/todos/permission/busy/error), same snake_case keys
//     LagoonicAgentDock::hook_push_agent_event expects, plus one extra
//     camelCase "sessionId" alias inside permission requests (the hook
//     reads request["sessionId"]; the mapper emits "session_id").
//   runtime_status(String status) ..... "ready" (server up), "down",
//     "session:<id>" (current session changed), "adopt:<msec>" and
//     "reload-requested" (crew side effects for the future bridge).
//
// Agent mapping (chat view modes 0 = Auto, 1 = Ask, 2 = Plan):
//   Plan -> OpenCode agent "plan"; Auto/Ask -> server default agent ("").
//   Ask behaviour (permission cards) comes from the server config's ask
//   mode, not from the agent parameter.
// Model references are "provider_id/model_id" (split at the last "/");
// blank model falls back to the default given to ensure_server(), and a
// blank default means "server default".

#include "core/object/ref_counted.h"
#include "core/os/mutex.h"
#include "core/os/thread.h"
#include "core/templates/safe_refcount.h"
#include "core/string/ustring.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

#include "lagoonic_acp_runner.h"
#include "lagoonic_crew_orchestrator.h"
#include "lagoonic_crew_server.h"
#include "lagoonic_event_mapper.h"
#include "lagoonic_opencode_client.h"
#include "lagoonic_supervisor.h"

class LagoonicHostCrewRunner; // Defined in the .cpp; heap-owned below.

class LagoonicRuntimeHost : public RefCounted {
	GDCLASS(LagoonicRuntimeHost, RefCounted);

	Ref<LagoonicSupervisor> supervisor;
	Ref<LagoonicOpenCodeClient> client;
	Ref<LagoonicEventMapper> mapper;
	Ref<LagoonicCrewOrchestrator> crew;
	LagoonicHostCrewRunner *crew_runner_impl = nullptr;

	mutable Mutex state_mutex; // Guards project_dir/current_session/server_down.
	mutable Mutex client_mutex; // Serializes blocking client calls (main + crew threads).
	bool signals_hooked = false;
	bool server_down = true;

	String project_dir;
	String current_session;
	String default_model; // "provider_id/model_id" or blank (= server default).
	String opencode_bin = "opencode"; // Resolved via PATH by OS::create_process.
	Dictionary last_secret_env; // Provider/asset keys; values never logged.
	String crew_boss_id = "opencode";

	// Boss turns of a non-OpenCode chat agent (Claude Code, Codex, ...):
	// one ACP adapter process per turn on acp_thread; its events reach the
	// main thread through deferred signal connections.
	Ref<LagoonicAcpRunner> acp_runner;
	Thread acp_thread;
	SafeFlag acp_busy;
	String acp_bin;
	PackedStringArray acp_args;
	Dictionary acp_env;
	Array acp_mcp_servers;
	String acp_cwd;
	String acp_prompt;
	String acp_session;
	bool acp_auto_approve = false;
	int acp_mode = 0; // Last chat mode, reused by crew wake-ups.

	// Crew MCP server (start_worker / list_workers) for the boss; started
	// once workers are set, polled on the main thread every frame.
	Ref<LagoonicCrewServer> crew_server;
	bool crew_poll_hooked = false;
	bool server_has_crew = false; // OpenCode's config already lists it.
	String crew_url_cache; // Guarded by state_mutex (read from the round thread).
	String crew_auth_cache;
	String acp_agent; // Boss id of the running/last ACP turn.
	Dictionary acp_choices; // Option id -> value chosen in the chat.

	// Reads an agent's options (models, effort...) without a prompt.
	Ref<LagoonicAcpRunner> acp_probe_runner;
	Thread acp_probe_thread;
	SafeFlag acp_probing;
	String probe_agent;
	String probe_bin;
	PackedStringArray probe_args;
	Dictionary probe_env;
	String probe_cwd;
	static void _acp_probe_main(void *p_userdata);
	void _on_acp_probe_finished(const String &p_agent_id, const Array &p_options);

	// Adapter of a chat agent on this machine; false (with r_error) when
	// the agent has none or it is not installed.
	bool _resolve_acp(const String &p_agent_id, String &r_bin, PackedStringArray &r_args, String &r_path, String &r_error) const;
	Error _send_acp(const String &p_boss_id, const String &p_session_id, const String &p_text, int p_mode);
	Array _acp_mcp_servers(const String &p_dir, const String &p_path_value, bool p_with_crew) const;
	String _boss_prompt(const String &p_text) const;
	void _append_boss_instruction(Array &r_parts) const;
	void _ensure_crew_server();
	void _poll_crew_server();
	friend class LagoonicHostCrewRunner;
	static void _acp_thread_main(void *p_userdata);
	void _on_acp_event(const Dictionary &p_event);
	void _on_acp_permission(const Dictionary &p_request);
	void _on_acp_finished(const Dictionary &p_result);
	Array crew_workers; // Array of {id, name} Dictionaries (set_crew cache).

	// (Re)attaches client + mapper + signals to the running supervisor.
	// Assumes the supervisor is up; cheap and idempotent.
	void _attach_client();
	void _emit_event(const Dictionary &p_event);
	void _emit_status(const String &p_status);
	void _emit_error(const String &p_message);
	static void _split_model(const String &p_ref, String &r_provider, String &r_model);
	static String _slugify(const String &p_project_dir);

	// Main-thread fan-in (client SSE arrives via call_deferred).
	void _on_sse_frame(const Dictionary &p_frame);
	void _on_sse_closed(const String &p_reason);
	void _on_session_recreated(const String &p_old_id, const String &p_new_id);
	void _on_unexpected_exit(int p_exit_code, const String &p_hint);
	// Crew-thread -> main-thread marshaling (call_deferred targets).
	void _crew_emit_chat(const String &p_session_id, const String &p_text);
	void _crew_emit_error(const String &p_session_id, const String &p_message);
	void _emit_status_deferred(const String &p_status);

protected:
	static void _bind_methods();

public:
	// Starts the server when needed (writes lagoonic.opencode.json via
	// LagoonicConfigWriter, spawns + health-polls like the TS supervisor).
	// May block for up to ~60 s on first start; call from Reconnect / first
	// send, never per keystroke. Idempotent when already up.
	Error ensure_server(const String &p_project_dir, const String &p_default_model, const Dictionary &p_secret_env);
	// Sends one user turn (creates a session when p_session_id and the
	// current one are both blank). Lazily restarts a dead server first.
	// p_mode: 0 = Auto, 1 = Ask, 2 = Plan. p_model: "provider/model" or "".
	// p_variant: esfuerzo del modelo ("" = el normal).
	Error send_prompt(const String &p_session_id, const String &p_text, int p_mode, const String &p_model, const String &p_variant = String());
	// Aborts boss + worker turns (OpenCode abort + crew cancel).
	Error abort();
	// permission_replied(id, reply): 0 = once, 1 = always, 2 = reject.
	Error respond_permission(const String &p_permission_id, int p_reply);
	Error revert_message(const String &p_message_id);
	// Creates a server session, selects it, returns its id ("" on error).
	String create_session(const String &p_title = String());
	void select_session(const String &p_session_id);
	// Chats del proyecto, mas recientes primero: [{id, title, updated_at}].
	// Excluye las sesiones efimeras de los workers del equipo.
	Array list_sessions();
	// Borra una sesion del servidor; si era la actual, queda sin sesion.
	bool delete_session(const String &p_session_id);
	bool rename_session(const String &p_session_id, const String &p_title);
	// Archivos que cambio el chat actual: [{path, additions, deletions}].
	Array get_changes();
	// Historial de una sesion como eventos "message" (mismo formato que
	// runtime_event): usuario {role:"user"} y asistente con texto,
	// herramientas y duracion ("worked_ms").
	Array load_history(const String &p_session_id);
	// (Re)configures the crew: boss agent id + Array of {id, name}.
	void set_crew(const String &p_boss_id, const Array &p_workers);
	// Keeps cached workers, switches boss only (boss_changed event).
	void set_crew_boss(const String &p_boss_id);
	// Options chosen in the chat for the ACP boss (model, effort...):
	// option id -> value, applied at the start of each turn.
	void set_acp_choices(const Dictionary &p_choices);
	// Starts the agent's adapter in the background to list its options;
	// emits {type:"acp_options", agent, options} when done. No-op while
	// another probe runs.
	void probe_acp_options(const String &p_agent_id, const String &p_cwd);
	// Adds/updates one worker entry {id, name=id} (worker_toggled event).
	void set_crew_worker(const String &p_worker_id, bool p_enabled);

	// Cheap, non-blocking: false after unexpected_exit / dead process.
	bool is_server_up() const;
	// For the dock's 0.5 s poll: refreshes the down flag without any
	// network probe (process liveness only). Returns is_server_up().
	bool poll_connection();
	String get_session() const;
	String get_project_dir() const;
	// Project folder without starting OpenCode (ACP bosses do not need it).
	void set_project_dir(const String &p_project_dir);
	// Boss that answers the chat ("opencode" or a chat agent id).
	String get_crew_boss() const;
	// Crew round thread: the wake-up as an ACP boss turn ("" = ok, else the
	// error), and one silent ACP worker turn returning its report.
	String acp_wake_boss(const String &p_boss_id, const String &p_session_id, const String &p_wake_text);
	String acp_worker_turn(const String &p_worker_id, const String &p_prompt, const Ref<LagoonicAcpRunner> &p_runner, String &r_error);
	Ref<LagoonicOpenCodeClient> get_client() const { return client; }
	Ref<LagoonicCrewOrchestrator> get_crew() const { return crew; }
	// Exposed for the crew runner (same module): serializes blocking
	// client calls between the main thread and the crew round thread.
	Mutex *get_client_mutex() const;

	void set_opencode_bin(const String &p_bin);
	String get_opencode_bin() const;

	LagoonicRuntimeHost();
	~LagoonicRuntimeHost();
};
