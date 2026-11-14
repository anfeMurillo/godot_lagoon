/**************************************************************************/
/*  lagoonic_runtime_host.cpp                                           */
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

#include "lagoonic_runtime_host.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"

#include "lagoonic_coding_agents.h"
#include "lagoonic_crew_server.h"

#include "scene/main/scene_tree.h"
#include "lagoonic_config_writer.h"
#include "lagoonic_mcp_locator.h"

#include "core/io/file_access.h"

// ---------------------------------------------------------------------------
// Crew runner: the LagoonicCrewOrchestrator side effects, implemented on top
// of the OpenCode client. Runs on the orchestrator's round thread (never the
// main thread): blocking client calls are legal here. UI-bound effects go
// through call_deferred() into LagoonicRuntimeHost marshaling methods.
//
// V1 scope: only "opencode" workers (ephemeral sessions, deleted after the
// turn). ACP adapter workers return a failure string; wiring them needs the
// adapter command per agent id (FIX-C territory, see DOCK_WIRING.md).
// ---------------------------------------------------------------------------

class LagoonicHostCrewRunner : public LagoonicCrewRunner {
	LagoonicRuntimeHost *host = nullptr;
	mutable Mutex worker_mutex;
	String worker_session; // Ephemeral OpenCode session of the running worker.
	Ref<LagoonicAcpRunner> worker_runner; // ACP worker turn in progress, if any.
	bool worker_abort_requested = false;

public:
	explicit LagoonicHostCrewRunner(LagoonicRuntimeHost *p_host) :
			host(p_host) {}

	virtual void crew_emit_chat(const String &p_session_id, const String &p_text) override {
		if (host != nullptr) {
			host->call_deferred(SNAME("_crew_emit_chat"), p_session_id, p_text);
		}
	}

	virtual void crew_emit_error(const String &p_session_id, const String &p_message) override {
		if (host != nullptr) {
			host->call_deferred(SNAME("_crew_emit_error"), p_session_id, p_message);
		}
	}

	virtual String crew_run_worker(const String &p_worker_id, const String &p_prompt, const String &p_title, int p_number, const String &p_boss_session_id, String &r_error) override {
		(void)p_boss_session_id;
		if (host == nullptr) {
			r_error = "Runtime host is gone.";
			return String();
		}
		if (p_worker_id != "opencode") {
			// Claude Code, Codex...: one silent ACP turn; its text is the report.
			Ref<LagoonicAcpRunner> runner;
			runner.instantiate();
			worker_mutex.lock();
			worker_runner = runner;
			worker_abort_requested = false;
			worker_mutex.unlock();
			const String report = host->acp_worker_turn(p_worker_id, p_prompt, runner, r_error);
			worker_mutex.lock();
			worker_runner.unref();
			const bool aborted = worker_abort_requested;
			worker_mutex.unlock();
			if (aborted && r_error.is_empty()) {
				r_error = "Worker turn aborted.";
			}
			return report;
		}
		if (host == nullptr) {
			r_error = "Runtime host is gone.";
			return String();
		}
		Ref<LagoonicOpenCodeClient> client = host->get_client();
		const String dir = host->get_project_dir();
		if (client.is_null() || dir.is_empty()) {
			r_error = "Server is not up; cannot run a crew worker.";
			return String();
		}
		const String title = "Crew worker #" + itos(p_number) + ": " + p_title.left(80);
		String sid;
		{
			// Serialized with main-thread client calls (see host header).
			Mutex *m = host->get_client_mutex();
			m->lock();
			sid = client->lagoonic_create_session(dir, title);
			m->unlock();
		}
		if (sid.is_empty()) {
			r_error = "Could not create an ephemeral worker session.";
			return String();
		}
		worker_mutex.lock();
		worker_session = sid;
		worker_abort_requested = false;
		worker_mutex.unlock();

		Array parts;
		Dictionary text_part;
		text_part["type"] = String("text");
		text_part["text"] = p_prompt;
		parts.push_back(text_part);

		String report;
		{
			Mutex *m = host->get_client_mutex();
			m->lock();
			const Dictionary res = client->lagoonic_prompt_parts_async(sid, dir, parts, String(), String(), String());
			m->unlock();
			if (!bool(res.get("ok", false))) {
				r_error = String(res.get("error", "Worker prompt failed."));
				Mutex *m2 = host->get_client_mutex();
				m2->lock();
				client->lagoonic_delete_session(dir, sid);
				m2->unlock();
				worker_mutex.lock();
				worker_session = String();
				worker_mutex.unlock();
				return String();
			}
		}
		// Wait for the worker's assistant text to stabilize (poll the
		// ephemeral session; the SSE stream is fanned out to the boss chat
		// only, so direct polling is the way to read a worker turn).
		String last_text;
		int stable_polls = 0;
		for (int i = 0; i < 360; i++) { // 360 x 500 ms = 180 s budget.
			worker_mutex.lock();
			const bool abort_hit = worker_abort_requested;
			worker_mutex.unlock();
			if (abort_hit) {
				r_error = "Worker turn aborted.";
				break;
			}
			String text;
			Mutex *m = host->get_client_mutex();
			m->lock();
			const Array messages = client->lagoonic_list_messages(sid, dir);
			m->unlock();
			for (int mi = 0; mi < messages.size(); mi++) {
				if (messages[mi].get_type() != Variant::DICTIONARY) {
					continue;
				}
				const Dictionary msg = messages[mi];
				if (String(msg.get("role", "")) != "assistant") {
					continue;
				}
				const Array msg_parts = msg.get("parts", Array());
				for (int pi = 0; pi < msg_parts.size(); pi++) {
					if (msg_parts[pi].get_type() != Variant::DICTIONARY) {
						continue;
					}
					const Dictionary part = msg_parts[pi];
					if (String(part.get("type", "")) == "text") {
						text += String(part.get("text", ""));
					}
				}
			}
			if (!text.is_empty() && text == last_text) {
				stable_polls++;
				if (stable_polls >= 6) { // 3 s without growth: turn is done.
					report = text;
					break;
				}
			} else {
				stable_polls = 0;
				last_text = text;
			}
			OS::get_singleton()->delay_usec(500000);
		}
		if (report.is_empty()) {
			report = last_text;
		}
		{
			Mutex *m = host->get_client_mutex();
			m->lock();
			client->lagoonic_delete_session(dir, sid);
			m->unlock();
		}
		worker_mutex.lock();
		worker_session = String();
		worker_mutex.unlock();
		if (report.is_empty() && r_error.is_empty()) {
			r_error = "Worker turn timed out with no answer.";
		}
		return report;
	}

	virtual void crew_abort_worker() override {
		worker_mutex.lock();
		worker_abort_requested = true;
		const String sid = worker_session;
		const Ref<LagoonicAcpRunner> acp = worker_runner;
		worker_mutex.unlock();
		if (acp.is_valid()) {
			acp->session_cancel(acp->get_acp_session_id());
		}
		if (host != nullptr && !sid.is_empty()) {
			Ref<LagoonicOpenCodeClient> client = host->get_client();
			if (client.is_valid()) {
				Mutex *m = host->get_client_mutex();
				m->lock();
				client->lagoonic_abort(sid, host->get_project_dir());
				m->unlock();
			}
		}
	}

	virtual void crew_adopt_external_since(uint64_t p_since_msec) override {
		if (host != nullptr) {
			host->call_deferred(SNAME("_emit_status_deferred"), "adopt:" + itos((int64_t)p_since_msec));
		}
	}

	virtual void crew_request_editor_reload() override {
		if (host != nullptr) {
			host->call_deferred(SNAME("_emit_status_deferred"), String("reload-requested"));
		}
	}

	// The wake-up as a normal boss turn. The orchestrator's round loop
	// re-splices chained rounds itself (inProgress stays true), so this
	// must NOT call maybe_run() (it would be a no-op anyway).
	virtual void crew_wake_boss(const String &p_boss_session_id, const String &p_wake_text, const String &p_bubble, String &r_error) override {
		if (host == nullptr) {
			r_error = "Runtime host is gone.";
			return;
		}
		const String boss = host->get_crew_boss();
		if (!boss.is_empty() && boss != "opencode") {
			crew_emit_chat(p_boss_session_id, p_bubble);
			r_error = host->acp_wake_boss(boss, p_boss_session_id, p_wake_text);
			return;
		}
		Ref<LagoonicOpenCodeClient> client = host->get_client();
		const String dir = host->get_project_dir();
		if (client.is_null() || dir.is_empty()) {
			r_error = "Server is not up; cannot wake the boss.";
			return;
		}
		Array parts;
		Dictionary text_part;
		text_part["type"] = String("text");
		text_part["text"] = p_wake_text;
		parts.push_back(text_part);
		host->_append_boss_instruction(parts);
		Mutex *m = host->get_client_mutex();
		m->lock();
		const Dictionary res = client->lagoonic_prompt_parts_async(p_boss_session_id, dir, parts, String(), String(), String());
		m->unlock();
		if (!bool(res.get("ok", false))) {
			r_error = String(res.get("error", "Boss wake-up failed."));
			return;
		}
		crew_emit_chat(p_boss_session_id, p_bubble);
	}
};

// ---------------------------------------------------------------------------

void LagoonicRuntimeHost::_bind_methods() {
	ClassDB::bind_method(D_METHOD("ensure_server", "project_dir", "default_model", "secret_env"), &LagoonicRuntimeHost::ensure_server);
	ClassDB::bind_method(D_METHOD("send_prompt", "session_id", "text", "mode", "model", "variant"), &LagoonicRuntimeHost::send_prompt, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("abort"), &LagoonicRuntimeHost::abort);
	ClassDB::bind_method(D_METHOD("respond_permission", "permission_id", "reply"), &LagoonicRuntimeHost::respond_permission);
	ClassDB::bind_method(D_METHOD("revert_message", "message_id"), &LagoonicRuntimeHost::revert_message);
	ClassDB::bind_method(D_METHOD("create_session", "title"), &LagoonicRuntimeHost::create_session, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("select_session", "session_id"), &LagoonicRuntimeHost::select_session);
	ClassDB::bind_method(D_METHOD("list_sessions"), &LagoonicRuntimeHost::list_sessions);
	ClassDB::bind_method(D_METHOD("delete_session", "session_id"), &LagoonicRuntimeHost::delete_session);
	ClassDB::bind_method(D_METHOD("rename_session", "session_id", "title"), &LagoonicRuntimeHost::rename_session);
	ClassDB::bind_method(D_METHOD("get_changes"), &LagoonicRuntimeHost::get_changes);
	ClassDB::bind_method(D_METHOD("load_history", "session_id"), &LagoonicRuntimeHost::load_history);
	ClassDB::bind_method(D_METHOD("set_crew", "boss_id", "workers"), &LagoonicRuntimeHost::set_crew);
	ClassDB::bind_method(D_METHOD("set_crew_boss", "boss_id"), &LagoonicRuntimeHost::set_crew_boss);
	ClassDB::bind_method(D_METHOD("set_acp_choices", "choices"), &LagoonicRuntimeHost::set_acp_choices);
	ClassDB::bind_method(D_METHOD("probe_acp_options", "agent_id", "cwd"), &LagoonicRuntimeHost::probe_acp_options);
	ClassDB::bind_method(D_METHOD("set_crew_worker", "worker_id", "enabled"), &LagoonicRuntimeHost::set_crew_worker);
	ClassDB::bind_method(D_METHOD("is_server_up"), &LagoonicRuntimeHost::is_server_up);
	ClassDB::bind_method(D_METHOD("poll_connection"), &LagoonicRuntimeHost::poll_connection);
	ClassDB::bind_method(D_METHOD("get_session"), &LagoonicRuntimeHost::get_session);
	ClassDB::bind_method(D_METHOD("get_project_dir"), &LagoonicRuntimeHost::get_project_dir);
	ClassDB::bind_method(D_METHOD("set_project_dir", "project_dir"), &LagoonicRuntimeHost::set_project_dir);
	ClassDB::bind_method(D_METHOD("get_crew_boss"), &LagoonicRuntimeHost::get_crew_boss);
	ClassDB::bind_method(D_METHOD("get_crew"), &LagoonicRuntimeHost::get_crew);
	ClassDB::bind_method(D_METHOD("set_opencode_bin", "bin"), &LagoonicRuntimeHost::set_opencode_bin);
	ClassDB::bind_method(D_METHOD("get_opencode_bin"), &LagoonicRuntimeHost::get_opencode_bin);

	ClassDB::bind_method(D_METHOD("_on_sse_frame", "frame"), &LagoonicRuntimeHost::_on_sse_frame);
	ClassDB::bind_method(D_METHOD("_on_sse_closed", "reason"), &LagoonicRuntimeHost::_on_sse_closed);
	ClassDB::bind_method(D_METHOD("_on_session_recreated", "old_id", "new_id"), &LagoonicRuntimeHost::_on_session_recreated);
	ClassDB::bind_method(D_METHOD("_on_unexpected_exit", "exit_code", "hint"), &LagoonicRuntimeHost::_on_unexpected_exit);
	ClassDB::bind_method(D_METHOD("_crew_emit_chat", "session_id", "text"), &LagoonicRuntimeHost::_crew_emit_chat);
	ClassDB::bind_method(D_METHOD("_crew_emit_error", "session_id", "message"), &LagoonicRuntimeHost::_crew_emit_error);
	ClassDB::bind_method(D_METHOD("_emit_status_deferred", "status"), &LagoonicRuntimeHost::_emit_status_deferred);

	ADD_SIGNAL(MethodInfo("runtime_event", PropertyInfo(Variant::DICTIONARY, "event")));
	ADD_SIGNAL(MethodInfo("runtime_status", PropertyInfo(Variant::STRING, "status")));
}

LagoonicRuntimeHost::LagoonicRuntimeHost() {
	supervisor.instantiate();
	client.instantiate();
	mapper.instantiate();
	crew.instantiate();
	crew_runner_impl = new LagoonicHostCrewRunner(this);
	crew->set_runner(crew_runner_impl);
}

LagoonicRuntimeHost::~LagoonicRuntimeHost() {
	if (crew_poll_hooked && SceneTree::get_singleton() != nullptr) {
		SceneTree::get_singleton()->disconnect(SNAME("process_frame"), callable_mp(this, &LagoonicRuntimeHost::_poll_crew_server));
	}
	if (crew_server.is_valid()) {
		crew_server->stop();
	}
	if (acp_runner.is_valid()) {
		acp_runner->close(); // Ends a running turn so its thread returns.
	}
	if (acp_thread.is_started()) {
		acp_thread.wait_to_finish();
	}
	if (acp_probe_runner.is_valid()) {
		acp_probe_runner->close();
	}
	if (acp_probe_thread.is_started()) {
		acp_probe_thread.wait_to_finish();
	}
	if (crew.is_valid()) {
		crew->set_runner(nullptr);
		crew->cancel();
	}
	delete crew_runner_impl;
	crew_runner_impl = nullptr;
}

void LagoonicRuntimeHost::_emit_event(const Dictionary &p_event) {
	emit_signal(SNAME("runtime_event"), p_event);
}

void LagoonicRuntimeHost::_emit_status(const String &p_status) {
	emit_signal(SNAME("runtime_status"), p_status);
}

void LagoonicRuntimeHost::_emit_status_deferred(const String &p_status) {
	_emit_status(p_status);
}

void LagoonicRuntimeHost::_emit_error(const String &p_message) {
	Dictionary event;
	event["type"] = String("error");
	state_mutex.lock();
	event["session_id"] = current_session;
	state_mutex.unlock();
	event["message"] = p_message;
	_emit_event(event);
}

void LagoonicRuntimeHost::_split_model(const String &p_ref, String &r_provider, String &r_model) {
	const String ref = p_ref.strip_edges();
	const int slash = ref.rfind("/");
	if (slash < 0) {
		r_provider = String();
		r_model = ref; // May be blank: server default.
		return;
	}
	r_provider = ref.substr(0, slash);
	r_model = ref.substr(slash + 1);
}

String LagoonicRuntimeHost::_slugify(const String &p_project_dir) {
	String base = p_project_dir.strip_edges().get_file().to_lower();
	if (base.is_empty()) {
		return String("project");
	}
	String out;
	for (int i = 0; i < base.length(); i++) {
		const char32_t c = base[i];
		const bool ok = (c >= U'a' && c <= U'z') || (c >= U'0' && c <= U'9') || c == U'-' || c == U'_';
		out += ok ? String::chr(c) : String("-");
	}
	return out.is_empty() ? String("project") : out;
}

void LagoonicRuntimeHost::_attach_client() {
	state_mutex.lock();
	const String dir = project_dir;
	const String session = current_session;
	state_mutex.unlock();

	client->lagoonic_setup("http://127.0.0.1:" + itos(supervisor->get_port()), supervisor->get_password());
	if (!signals_hooked) {
		if (!supervisor->is_connected(SNAME("unexpected_exit"), callable_mp(this, &LagoonicRuntimeHost::_on_unexpected_exit))) {
			supervisor->connect(SNAME("unexpected_exit"), callable_mp(this, &LagoonicRuntimeHost::_on_unexpected_exit));
		}
		if (!client->is_connected(SNAME("lagoonic_sse_frame"), callable_mp(this, &LagoonicRuntimeHost::_on_sse_frame))) {
			client->connect(SNAME("lagoonic_sse_frame"), callable_mp(this, &LagoonicRuntimeHost::_on_sse_frame));
		}
		if (!client->is_connected(SNAME("lagoonic_sse_closed"), callable_mp(this, &LagoonicRuntimeHost::_on_sse_closed))) {
			client->connect(SNAME("lagoonic_sse_closed"), callable_mp(this, &LagoonicRuntimeHost::_on_sse_closed));
		}
		if (!client->is_connected(SNAME("lagoonic_session_recreated"), callable_mp(this, &LagoonicRuntimeHost::_on_session_recreated))) {
			client->connect(SNAME("lagoonic_session_recreated"), callable_mp(this, &LagoonicRuntimeHost::_on_session_recreated));
		}
		signals_hooked = true;
	}
	if (!client->lagoonic_is_subscribed()) {
		client->lagoonic_subscribe_events(dir, Callable());
	}
	mapper->lagoonic_configure(session);
}

Error LagoonicRuntimeHost::ensure_server(const String &p_project_dir, const String &p_default_model, const Dictionary &p_secret_env) {
	if (!p_project_dir.strip_edges().is_empty()) {
		state_mutex.lock();
		project_dir = p_project_dir.strip_edges();
		state_mutex.unlock();
	}
	if (!p_default_model.strip_edges().is_empty()) {
		state_mutex.lock();
		default_model = p_default_model.strip_edges();
		state_mutex.unlock();
	}
	if (!p_secret_env.is_empty()) {
		state_mutex.lock();
		last_secret_env = p_secret_env;
		state_mutex.unlock();
	}

	state_mutex.lock();
	const String dir = project_dir;
	const String model = default_model;
	const Dictionary secrets = last_secret_env;
	state_mutex.unlock();
	if (dir.is_empty()) {
		_emit_error("No project directory: cannot start the OpenCode server.");
		return ERR_INVALID_PARAMETER;
	}

	// The crew server must be in OpenCode's config: restart once when the
	// running server was started without it.
	_ensure_crew_server();
	state_mutex.lock();
	const bool crew_missing = !crew_url_cache.is_empty() && !server_has_crew;
	state_mutex.unlock();
	// Fast path: already up (mirrors supervisor.ts: reuse the healthy server).
	if (!crew_missing && supervisor->is_running() && supervisor->check_health()) {
		state_mutex.lock();
		server_down = false;
		state_mutex.unlock();
		_attach_client();
		_emit_status("ready");
		return OK;
	}
	supervisor->stop();

	state_mutex.lock();
	const String crew_url = crew_url_cache;
	const String crew_auth = crew_auth_cache;
	state_mutex.unlock();
	LagoonicConfigWriter writer;
	Dictionary input;
	input["project_dir"] = dir;
	input["default_model"] = model;
	if (!crew_url.is_empty()) {
		// The boss reaches start_worker / list_workers through this server.
		Dictionary crew_input;
		crew_input["url"] = crew_url;
		crew_input["authorization"] = crew_auth;
		input["crew"] = crew_input;
	}
	const String config_path = writer.default_config_path(_slugify(dir));
	const Error werr = writer.write_config(config_path, input);
	if (werr != OK) {
		_emit_error("Could not write lagoonic.opencode.json.");
		return werr;
	}
	const Error serr = supervisor->start(opencode_bin, config_path, dir, model, secrets);
	if (serr != OK) {
		state_mutex.lock();
		server_down = true;
		state_mutex.unlock();
		_emit_error("Could not start the OpenCode server.");
		_emit_status("down");
		return serr;
	}
	state_mutex.lock();
	server_down = false;
	server_has_crew = !crew_url.is_empty();
	state_mutex.unlock();
	_attach_client();
	_emit_status("ready");
	return OK;
}

Error LagoonicRuntimeHost::send_prompt(const String &p_session_id, const String &p_text, int p_mode, const String &p_model, const String &p_variant) {
	if (p_text.strip_edges().is_empty()) {
		return ERR_INVALID_PARAMETER;
	}
	// The boss chosen in Configuracion > Equipo answers the chat; only
	// OpenCode goes through the server below.
	const String boss = get_crew_boss();
	if (!boss.is_empty() && boss != "opencode") {
		return _send_acp(boss, p_session_id, p_text, p_mode);
	}
	// Lazy (re)start, like supervisor.ts onUnexpectedExit -> next message.
	if (!is_server_up() || !supervisor->check_health()) {
		const Error serr = ensure_server(String(), String(), Dictionary());
		if (serr != OK) {
			return serr;
		}
	}

	state_mutex.lock();
	String session = p_session_id.is_empty() ? current_session : p_session_id;
	const String dir = project_dir;
	String model_ref = p_model.strip_edges().is_empty() ? default_model : p_model.strip_edges();
	state_mutex.unlock();

	client_mutex.lock();
	if (session.is_empty()) {
		session = client->lagoonic_create_session(dir, String());
		if (!session.is_empty()) {
			state_mutex.lock();
			current_session = session;
			state_mutex.unlock();
			mapper->lagoonic_configure(session);
			_emit_status("session:" + session);
		}
	}
	if (session.is_empty()) {
		client_mutex.unlock();
		_emit_error("Could not create a session.");
		return ERR_CANT_CREATE;
	}
	String provider, model_id;
	_split_model(model_ref, provider, model_id);
	// Plan -> OpenCode "plan" agent; Auto/Ask -> server default ("").
	// Ask permission behaviour comes from the server config, not this param.
	const String agent = (p_mode == 2) ? String("plan") : String();
	Array parts;
	Dictionary text_part;
	text_part["type"] = String("text");
	text_part["text"] = p_text;
	parts.push_back(text_part);
	_append_boss_instruction(parts);
	const Dictionary res = client->lagoonic_prompt_parts_async(session, dir, parts, provider, model_id, agent, p_variant);
	client_mutex.unlock();

	if (bool(res.get("recreated", false))) {
		session = String(res.get("session_id", session));
		state_mutex.lock();
		current_session = session;
		state_mutex.unlock();
		mapper->lagoonic_configure(session);
		_emit_status("session:" + session);
	}
	if (!bool(res.get("ok", false))) {
		_emit_error(String(res.get("error", "Prompt failed.")));
		return ERR_CANT_CREATE;
	}
	Dictionary busy;
	busy["type"] = String("busy");
	busy["session_id"] = session;
	busy["busy"] = true;
	_emit_event(busy);
	return OK;
}

Error LagoonicRuntimeHost::abort() {
	crew->cancel();
	if (acp_busy.is_set() && acp_runner.is_valid()) {
		acp_runner->session_cancel(acp_runner->get_acp_session_id());
	}
	state_mutex.lock();
	const String session = current_session;
	const String dir = project_dir;
	state_mutex.unlock();
	if (!session.is_empty() && is_server_up()) {
		client_mutex.lock();
		client->lagoonic_abort(session, dir);
		client_mutex.unlock();
	}
	Dictionary busy;
	busy["type"] = String("busy");
	busy["session_id"] = session;
	busy["busy"] = false;
	_emit_event(busy);
	return OK;
}

Error LagoonicRuntimeHost::respond_permission(const String &p_permission_id, int p_reply) {
	if (p_permission_id.is_empty()) {
		return ERR_INVALID_PARAMETER;
	}
	if (p_permission_id.begins_with("acp-") && acp_runner.is_valid()) {
		acp_runner->resolve_permission(p_permission_id, p_reply != 2);
		return OK;
	}
	String reply_str = "once";
	if (p_reply == 1) {
		reply_str = "always";
	} else if (p_reply == 2) {
		reply_str = "reject";
	}
	state_mutex.lock();
	const String session = current_session;
	const String dir = project_dir;
	state_mutex.unlock();
	if (session.is_empty() || !is_server_up()) {
		return ERR_UNAVAILABLE;
	}
	client_mutex.lock();
	const bool ok = client->lagoonic_respond_permission(session, dir, p_permission_id, reply_str);
	client_mutex.unlock();
	return ok ? OK : FAILED;
}

Error LagoonicRuntimeHost::revert_message(const String &p_message_id) {
	if (p_message_id.is_empty()) {
		return ERR_INVALID_PARAMETER;
	}
	state_mutex.lock();
	const String session = current_session;
	const String dir = project_dir;
	state_mutex.unlock();
	if (session.is_empty() || !is_server_up()) {
		return ERR_UNAVAILABLE;
	}
	client_mutex.lock();
	const bool ok = client->lagoonic_revert(session, dir, p_message_id);
	client_mutex.unlock();
	return ok ? OK : FAILED;
}

String LagoonicRuntimeHost::create_session(const String &p_title) {
	if (!is_server_up()) {
		if (ensure_server(String(), String(), Dictionary()) != OK) {
			return String();
		}
	}
	state_mutex.lock();
	const String dir = project_dir;
	state_mutex.unlock();
	client_mutex.lock();
	const String sid = client->lagoonic_create_session(dir, p_title);
	client_mutex.unlock();
	if (sid.is_empty()) {
		_emit_error("Could not create a session.");
		return String();
	}
	state_mutex.lock();
	current_session = sid;
	state_mutex.unlock();
	mapper->lagoonic_configure(sid);
	_emit_status("session:" + sid);
	return sid;
}

void LagoonicRuntimeHost::select_session(const String &p_session_id) {
	if (p_session_id.is_empty()) {
		return;
	}
	state_mutex.lock();
	current_session = p_session_id;
	state_mutex.unlock();
	// configure() resets accumulated mapper state when the id changes, so
	// the transcript never mixes two sessions. NOTE: message history
	// backfill (list_messages -> message events) is a FIX follow-up; from
	// here on, live turns render incrementally.
	mapper->lagoonic_configure(p_session_id);
	_emit_status("session:" + p_session_id);
}

Array LagoonicRuntimeHost::list_sessions() {
	Array out;
	if (!is_server_up()) {
		return out;
	}
	const String dir = get_project_dir();
	client_mutex.lock();
	const Array rows = client->lagoonic_list_sessions(dir);
	client_mutex.unlock();
	for (int i = 0; i < rows.size(); i++) {
		const Dictionary row = rows[i];
		if (String(row.get("title", "")).begins_with(LagoonicEventMapper::LAGOONIC_WORKER_PREFIX)) {
			continue;
		}
		out.push_back(row);
	}
	// Mas recientes primero (la API 1.x no garantiza el orden).
	for (int i = 1; i < out.size(); i++) {
		for (int j = i; j > 0 && int64_t(Dictionary(out[j]).get("updated_at", 0)) > int64_t(Dictionary(out[j - 1]).get("updated_at", 0)); j--) {
			const Variant tmp = out[j];
			out[j] = out[j - 1];
			out[j - 1] = tmp;
		}
	}
	return out;
}

bool LagoonicRuntimeHost::delete_session(const String &p_session_id) {
	if (p_session_id.is_empty() || !is_server_up()) {
		return false;
	}
	const String dir = get_project_dir();
	client_mutex.lock();
	const bool ok = client->lagoonic_delete_session(dir, p_session_id);
	client_mutex.unlock();
	if (ok && get_session() == p_session_id) {
		state_mutex.lock();
		current_session = String();
		state_mutex.unlock();
		mapper->lagoonic_configure(String());
		_emit_status("session:");
	}
	return ok;
}

Array LagoonicRuntimeHost::get_changes() {
	const String session = get_session();
	if (session.is_empty() || !is_server_up()) {
		return Array();
	}
	const String dir = get_project_dir();
	client_mutex.lock();
	const Array rows = client->lagoonic_diff(session, dir);
	client_mutex.unlock();
	return rows;
}

bool LagoonicRuntimeHost::rename_session(const String &p_session_id, const String &p_title) {
	if (p_session_id.is_empty() || p_title.strip_edges().is_empty() || !is_server_up()) {
		return false;
	}
	const String dir = get_project_dir();
	client_mutex.lock();
	const bool ok = client->lagoonic_rename_session(dir, p_session_id, p_title.strip_edges());
	client_mutex.unlock();
	return ok;
}

Array LagoonicRuntimeHost::load_history(const String &p_session_id) {
	Array events;
	if (p_session_id.is_empty() || !is_server_up()) {
		return events;
	}
	const String dir = get_project_dir();
	client_mutex.lock();
	const Array messages = client->lagoonic_list_messages(p_session_id, dir);
	client_mutex.unlock();
	for (int i = 0; i < messages.size(); i++) {
		const Dictionary msg = messages[i];
		const String role = msg.get("role", "user");
		const Array parts = msg.get("parts", Array());
		String text;
		Array tools;
		Array timeline;
		int64_t first_ms = 0;
		int64_t last_ms = 0;
		for (int j = 0; j < parts.size(); j++) {
			const Dictionary part = parts[j];
			const String type = part.get("type", "");
			if (type == "text") {
				if (bool(part.get("synthetic", false))) {
					continue;
				}
				const String t = part.get("text", "");
				if (!t.strip_edges().is_empty()) {
					text += (text.is_empty() ? String() : String("\n\n")) + t;
					Dictionary entry;
					entry["kind"] = String("text");
					entry["id"] = String(part.get("id", itos(j)));
					entry["text"] = t;
					timeline.push_back(entry);
				}
			} else if (type == "tool") {
				const String raw = part.get("tool", "");
				if (raw.is_empty() || mapper->lagoonic_is_hidden_tool(raw)) {
					continue;
				}
				const Dictionary state = part.get("state", Dictionary());
				const String status = state.get("status", "");
				const String detail = mapper->lagoonic_extract_tool_detail(state);
				const String title = state.get("title", "");
				Dictionary row;
				row["id"] = String(part.get("id", itos(j)));
				row["label"] = mapper->lagoonic_tool_label(raw, detail.is_empty() ? title : detail);
				row["state"] = status == "error" ? String("error") : String("done");
				row["detail"] = status == "error" ? ("Error: " + String(state.get("error", ""))).left(240) : (detail.is_empty() ? title : detail);
				tools.push_back(row);
				Dictionary entry;
				entry["kind"] = String("tool");
				entry["id"] = row["id"];
				timeline.push_back(entry);
				const Dictionary time = state.get("time", Dictionary());
				const int64_t start = time.get("start", 0);
				const int64_t end = time.get("end", 0);
				if (start > 0 && (first_ms == 0 || start < first_ms)) {
					first_ms = start;
				}
				last_ms = MAX(last_ms, end);
			}
		}
		if (text.is_empty() && tools.is_empty()) {
			continue;
		}
		Dictionary message;
		message["id"] = String(msg.get("id", ""));
		message["role"] = role;
		message["text"] = text;
		message["tools"] = tools;
		message["timeline"] = timeline;
		message["created_at"] = msg.get("time", 0);
		if (first_ms > 0 && last_ms > first_ms) {
			message["worked_ms"] = last_ms - first_ms;
		}
		Dictionary event;
		event["type"] = String("message");
		event["session_id"] = p_session_id;
		event["history"] = true;
		event["message"] = message;
		events.push_back(event);
	}
	return events;
}

void LagoonicRuntimeHost::set_crew(const String &p_boss_id, const Array &p_workers) {
	state_mutex.lock();
	const String dir = project_dir;
	if (!p_boss_id.is_empty()) {
		crew_boss_id = p_boss_id;
	}
	crew_workers = p_workers;
	const String boss = crew_boss_id;
	state_mutex.unlock();
	crew->set_project(dir, boss);
	crew->set_workers(p_workers);
}

void LagoonicRuntimeHost::set_crew_boss(const String &p_boss_id) {
	if (p_boss_id.is_empty()) {
		return;
	}
	state_mutex.lock();
	crew_boss_id = p_boss_id;
	const Array workers = crew_workers;
	state_mutex.unlock();
	set_crew(p_boss_id, workers);
}

void LagoonicRuntimeHost::set_crew_worker(const String &p_worker_id, bool p_enabled) {
	if (p_worker_id.is_empty()) {
		return;
	}
	state_mutex.lock();
	Array workers = crew_workers;
	bool found = false;
	for (int i = 0; i < workers.size(); i++) {
		if (workers[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		Dictionary entry = workers[i];
		if (String(entry.get("id", "")) == p_worker_id) {
			found = true;
			if (!p_enabled) {
				workers.remove_at(i);
			}
			break;
		}
	}
	if (!found && p_enabled) {
		Dictionary entry;
		entry["id"] = p_worker_id;
		entry["name"] = p_worker_id;
		workers.push_back(entry);
	}
	crew_workers = workers;
	state_mutex.unlock();
	set_crew(String(), workers);
}

bool LagoonicRuntimeHost::is_server_up() const {
	state_mutex.lock();
	const bool down = server_down;
	state_mutex.unlock();
	return !down && supervisor->is_running();
}

bool LagoonicRuntimeHost::poll_connection() {
	// Cheap enough for the dock's 0.5 s poll: process liveness only, no
	// network probe (full health is checked in ensure_server/send_prompt).
	if (!supervisor->is_running()) {
		state_mutex.lock();
		const bool was_down = server_down;
		if (!was_down) {
			server_down = true;
		}
		const String session = current_session;
		state_mutex.unlock();
		if (!was_down) {
			crew->cancel();
			Dictionary event;
			event["type"] = String("error");
			event["session_id"] = session;
			event["message"] = String("OpenCode server process is gone; it restarts on the next message.");
			_emit_event(event);
			_emit_status("down");
		}
		return false;
	}
	state_mutex.lock();
	const bool down = server_down;
	state_mutex.unlock();
	return !down;
}

String LagoonicRuntimeHost::get_session() const {
	state_mutex.lock();
	const String session = current_session;
	state_mutex.unlock();
	return session;
}

String LagoonicRuntimeHost::get_project_dir() const {
	state_mutex.lock();
	const String dir = project_dir;
	state_mutex.unlock();
	return dir;
}

void LagoonicRuntimeHost::set_project_dir(const String &p_project_dir) {
	if (p_project_dir.strip_edges().is_empty()) {
		return;
	}
	state_mutex.lock();
	project_dir = p_project_dir.strip_edges();
	state_mutex.unlock();
}

String LagoonicRuntimeHost::get_crew_boss() const {
	state_mutex.lock();
	const String boss = crew_boss_id;
	state_mutex.unlock();
	return boss;
}

// ---------------------------------------------------------- ACP boss turns

namespace {

// Absolute path of p_bin in the host search dirs (PATH + npm/brew/local
// bins), or "" when absent. Apps opened from Finder get a minimal PATH,
// so execvp alone would miss npm-installed adapters.
String lagoonic_find_on_host(const String &p_bin, const Vector<String> &p_dirs) {
	const Vector<String> names = LagoonicCodingAgents::candidate_names(p_bin);
	for (int i = 0; i < p_dirs.size(); i++) {
		for (int j = 0; j < names.size(); j++) {
			const String path = p_dirs[i].path_join(names[j]);
			if (FileAccess::exists(path)) {
				return path;
			}
		}
	}
	return String();
}

} // namespace

bool LagoonicRuntimeHost::_resolve_acp(const String &p_agent_id, String &r_bin, PackedStringArray &r_args, String &r_path, String &r_error) const {
	const LagoonicCodingAgents::AgentInfo *agent = LagoonicCodingAgents::get_agent(p_agent_id);
	if (agent == nullptr || agent->acp == nullptr) {
		r_error = vformat(String::utf8("%s no se puede usar en el chat."), p_agent_id);
		return false;
	}
	// `npm prefix -g` is a subprocess: ask once per editor run.
	static const String npm_prefix = LagoonicCodingAgents::npm_global_prefix();
	const Vector<String> dirs = LagoonicCodingAgents::host_search_dirs(npm_prefix);
	const Vector<String> command = String::utf8(agent->acp).split(" ", false);
	r_bin = lagoonic_find_on_host(command[0], dirs);
	if (r_bin.is_empty()) {
		String how;
		if (agent->acp_install != nullptr) {
			how = String::utf8(" Instala su conector para el chat con: ") + String::utf8(agent->acp_install);
		}
		r_error = vformat(String::utf8("%s no está listo para el chat.%s"), String::utf8(agent->name), how);
		return false;
	}
	r_args.clear();
	for (int i = 1; i < command.size(); i++) {
		r_args.push_back(command[i]);
	}
	r_path = String();
	for (int i = 0; i < dirs.size(); i++) {
		if (!dirs[i].is_empty()) {
			r_path += (r_path.is_empty() ? "" : (LagoonicCodingAgents::is_windows() ? ";" : ":")) + dirs[i];
		}
	}
	return true;
}

void LagoonicRuntimeHost::set_acp_choices(const Dictionary &p_choices) {
	state_mutex.lock();
	acp_choices = p_choices.duplicate();
	state_mutex.unlock();
}

void LagoonicRuntimeHost::probe_acp_options(const String &p_agent_id, const String &p_cwd) {
	if (acp_probing.is_set()) {
		return;
	}
	String bin, path_value, error;
	PackedStringArray args;
	if (!_resolve_acp(p_agent_id, bin, args, path_value, error)) {
		return; // The chat shows the install hint when the user sends.
	}
	if (acp_probe_thread.is_started()) {
		acp_probe_thread.wait_to_finish(); // Previous probe already ended.
	}
	if (acp_probe_runner.is_null()) {
		acp_probe_runner.instantiate();
	}
	probe_agent = p_agent_id;
	probe_bin = bin;
	probe_args = args;
	probe_env.clear();
	if (!path_value.is_empty()) {
		probe_env["PATH"] = path_value;
	}
	probe_cwd = p_cwd.is_empty() ? get_project_dir() : p_cwd;
	if (probe_cwd.is_empty()) {
		probe_cwd = OS::get_singleton()->get_user_data_dir();
	}
	acp_probing.set();
	acp_probe_thread.start(&LagoonicRuntimeHost::_acp_probe_main, this);
}

void LagoonicRuntimeHost::_acp_probe_main(void *p_userdata) {
	LagoonicRuntimeHost *self = static_cast<LagoonicRuntimeHost *>(p_userdata);
	const Array options = self->acp_probe_runner->probe_options(self->probe_bin, self->probe_args, self->probe_cwd, self->probe_env);
	callable_mp(self, &LagoonicRuntimeHost::_on_acp_probe_finished).call_deferred(self->probe_agent, options);
}

void LagoonicRuntimeHost::_on_acp_probe_finished(const String &p_agent_id, const Array &p_options) {
	acp_probing.clear();
	if (acp_probe_thread.is_started()) {
		acp_probe_thread.wait_to_finish();
	}
	if (p_options.is_empty()) {
		return;
	}
	Dictionary event;
	event["type"] = String("acp_options");
	event["agent"] = p_agent_id;
	event["options"] = p_options;
	_emit_event(event);
}

Array LagoonicRuntimeHost::_acp_mcp_servers(const String &dir, const String &path_value, bool p_with_crew) const {
	// lagoonic-mcp gives the agent the Godot tools, like OpenCode's config.
	Array mcp_servers;
	const Vector<String> mcp_command = LagoonicMcpLocator::command();
	if (mcp_command.is_empty()) {
		WARN_PRINT("Lagoonic: lagoonic-mcp was not found, so the agent has no Godot tools.");
	} else {
		Array mcp_env;
		const char *env_names[3] = { "GODOT_PATH", "LAGOONIC_PLATFORM", "PATH" };
		const String env_values[3] = { OS::get_singleton()->get_executable_path(), "desktop", path_value };
		for (int i = 0; i < 3; i++) {
			Dictionary entry;
			entry["name"] = String(env_names[i]);
			entry["value"] = env_values[i];
			mcp_env.push_back(entry);
		}
		Array mcp_args;
		for (int i = 1; i < mcp_command.size(); i++) {
			mcp_args.push_back(mcp_command[i]);
		}
		mcp_args.push_back("--project");
		mcp_args.push_back(dir);
		Dictionary server;
		server["name"] = String("lagoonic");
		server["command"] = mcp_command[0];
		server["args"] = mcp_args;
		server["env"] = mcp_env;
		mcp_servers.push_back(server);
	}
	// The boss also gets the crew tools (start_worker, list_workers) over HTTP.
	state_mutex.lock();
	const String crew_url = crew_url_cache;
	const String crew_auth = crew_auth_cache;
	state_mutex.unlock();
	if (p_with_crew && !crew_url.is_empty() && !crew->crew_worker_names().is_empty()) {
		Array headers;
		Dictionary auth;
		auth["name"] = String("Authorization");
		auth["value"] = crew_auth;
		headers.push_back(auth);
		Dictionary crew_entry;
		crew_entry["type"] = String("http");
		crew_entry["name"] = String(LagoonicCrewServer::CREW_SERVER_NAME);
		crew_entry["url"] = crew_url;
		crew_entry["headers"] = headers;
		mcp_servers.push_back(crew_entry);
	}
	return mcp_servers;
}

String LagoonicRuntimeHost::_boss_prompt(const String &p_text) const {
	// With workers, the boss learns how to delegate (prepended like the
	// desktop bridge did for ACP bosses).
	const Vector<String> names = crew->crew_worker_names();
	state_mutex.lock();
	const bool has_crew = !crew_url_cache.is_empty();
	state_mutex.unlock();
	if (names.is_empty() || !has_crew) {
		return p_text;
	}
	return LagoonicCrewOrchestrator::boss_instruction(names, LagoonicCrewOrchestrator::MAX_CREW_TASKS) + "\n\n" + p_text;
}

void LagoonicRuntimeHost::_append_boss_instruction(Array &r_parts) const {
	// OpenCode boss: hidden from the chat history (synthetic), so only the
	// boss reads it; workers run in their own sessions without it.
	const Vector<String> names = crew->crew_worker_names();
	state_mutex.lock();
	const bool has_crew = !crew_url_cache.is_empty();
	state_mutex.unlock();
	if (names.is_empty() || !has_crew) {
		return;
	}
	Dictionary part;
	part["type"] = String("text");
	part["text"] = LagoonicCrewOrchestrator::boss_instruction(names, LagoonicCrewOrchestrator::MAX_CREW_TASKS);
	part["synthetic"] = true;
	r_parts.push_back(part);
}

String LagoonicRuntimeHost::acp_wake_boss(const String &p_boss_id, const String &p_session_id, const String &p_wake_text) {
	// Crew round thread: the wake-up is a normal boss turn, run here so the
	// orchestrator's loop sees the tasks the boss queues from the reports.
	if (acp_busy.is_set()) {
		return "The boss is still answering; send a message to continue.";
	}
	if (acp_runner.is_null()) {
		return "The boss is not connected; send a message to continue.";
	}
	String bin, path_value, error;
	PackedStringArray args;
	if (!_resolve_acp(p_boss_id, bin, args, path_value, error)) {
		return error;
	}
	state_mutex.lock();
	const String dir = project_dir;
	const Dictionary choices = acp_choices;
	state_mutex.unlock();
	Dictionary env;
	if (!path_value.is_empty()) {
		env["PATH"] = path_value;
	}
	acp_busy.set();
	acp_runner->set_chosen_options(choices);
	acp_runner->set_requested_mode(acp_mode == 2 ? String("plan") : (acp_mode == 1 ? String("default") : String()));
	const String session = p_session_id.is_empty() ? ("acp-" + p_boss_id) : p_session_id;
	const Dictionary result = acp_runner->run_turn(bin, args, dir, env, _acp_mcp_servers(dir, path_value, true),
			_boss_prompt(p_wake_text), Array(), acp_mode == 0, false, session);
	acp_busy.clear();
	if (!bool(result.get("ok", false))) {
		const String message = String(result.get("error", ""));
		return message.is_empty() ? String::utf8("El jefe no respondió a los informes.") : message;
	}
	return String();
}

String LagoonicRuntimeHost::acp_worker_turn(const String &p_worker_id, const String &p_prompt, const Ref<LagoonicAcpRunner> &p_runner, String &r_error) {
	// Crew round thread: one silent, autonomous turn of a worker agent; its
	// text is the report. Workers get lagoonic-mcp but no crew tools.
	String bin, path_value;
	PackedStringArray args;
	if (!_resolve_acp(p_worker_id, bin, args, path_value, r_error)) {
		return String();
	}
	const String dir = get_project_dir();
	Dictionary env;
	if (!path_value.is_empty()) {
		env["PATH"] = path_value;
	}
	const Dictionary result = p_runner->run_turn(bin, args, dir, env, _acp_mcp_servers(dir, path_value, false),
			p_prompt, Array(), true, true, "crew-" + p_worker_id);
	if (!bool(result.get("ok", false))) {
		r_error = String(result.get("error", ""));
		if (r_error.is_empty()) {
			r_error = "The worker did not finish its task.";
		}
		return String();
	}
	return String(result.get("text", ""));
}

// ---------------------------------------------------------- crew server

void LagoonicRuntimeHost::_ensure_crew_server() {
	state_mutex.lock();
	const bool wanted = !crew_workers.is_empty();
	state_mutex.unlock();
	if (!wanted) {
		return;
	}
	if (crew_server.is_null()) {
		crew_server.instantiate();
		crew_server->set_tools(crew.ptr());
	}
	if (!crew_server->is_running()) {
		if (crew_server->start() != OK) {
			_emit_error("Could not start the crew server; the boss works alone this time.");
			return;
		}
		state_mutex.lock();
		crew_url_cache = crew_server->get_url();
		crew_auth_cache = crew_server->get_authorization();
		state_mutex.unlock();
	}
	if (!crew_poll_hooked && SceneTree::get_singleton() != nullptr) {
		// The server answers on the main thread, every frame.
		SceneTree::get_singleton()->connect(SNAME("process_frame"), callable_mp(this, &LagoonicRuntimeHost::_poll_crew_server));
		crew_poll_hooked = true;
	}
}

void LagoonicRuntimeHost::_poll_crew_server() {
	if (crew_server.is_valid()) {
		crew_server->poll();
	}
}

Error LagoonicRuntimeHost::_send_acp(const String &p_boss_id, const String &p_session_id, const String &p_text, int p_mode) {
	const LagoonicCodingAgents::AgentInfo *agent = LagoonicCodingAgents::get_agent(p_boss_id);
	if (agent == nullptr || agent->acp == nullptr) {
		_emit_error(vformat(String::utf8("%s no se puede usar en el chat."), p_boss_id));
		return ERR_UNAVAILABLE;
	}
	const String name = String::utf8(agent->name);
	if (acp_busy.is_set()) {
		_emit_error(vformat(String::utf8("%s todavía está respondiendo."), name));
		return ERR_BUSY;
	}
	if (acp_thread.is_started()) {
		acp_thread.wait_to_finish(); // Previous turn already ended.
	}
	state_mutex.lock();
	const String dir = project_dir;
	String session = p_session_id.is_empty() ? current_session : p_session_id;
	const Dictionary choices = acp_choices;
	state_mutex.unlock();
	if (dir.is_empty()) {
		_emit_error("No project directory: open a project first.");
		return ERR_INVALID_PARAMETER;
	}
	if (session.is_empty()) {
		session = "acp-" + p_boss_id;
	}

	String adapter, path_value, resolve_error;
	PackedStringArray adapter_args;
	if (!_resolve_acp(p_boss_id, adapter, adapter_args, path_value, resolve_error)) {
		_emit_error(resolve_error);
		return ERR_FILE_NOT_FOUND;
	}

	_ensure_crew_server();
	const Array mcp_servers = _acp_mcp_servers(dir, path_value, true);

	if (acp_runner.is_null()) {
		acp_runner.instantiate();
		acp_runner->connect(SNAME("acp_event"), callable_mp(this, &LagoonicRuntimeHost::_on_acp_event), CONNECT_DEFERRED);
		acp_runner->connect(SNAME("acp_permission"), callable_mp(this, &LagoonicRuntimeHost::_on_acp_permission), CONNECT_DEFERRED);
	}
	acp_bin = adapter;
	acp_args = adapter_args;
	acp_agent = p_boss_id;
	acp_runner->set_chosen_options(choices);
	// Lagoonic's mode picks the agent's own mode when it has one:
	// Preguntar antes -> "default" (asks first), Solo planificar -> "plan".
	acp_runner->set_requested_mode(p_mode == 2 ? String("plan") : (p_mode == 1 ? String("default") : String()));
	acp_env.clear();
	if (!path_value.is_empty()) {
		acp_env["PATH"] = path_value;
	}
	acp_mcp_servers = mcp_servers;
	acp_cwd = dir;
	acp_prompt = _boss_prompt(p_text);
	acp_session = session;
	acp_auto_approve = (p_mode == 0);
	acp_mode = p_mode;
	acp_busy.set();
	acp_thread.start(&LagoonicRuntimeHost::_acp_thread_main, this);
	return OK;
}

void LagoonicRuntimeHost::_acp_thread_main(void *p_userdata) {
	LagoonicRuntimeHost *self = static_cast<LagoonicRuntimeHost *>(p_userdata);
	const Dictionary result = self->acp_runner->run_turn(self->acp_bin, self->acp_args, self->acp_cwd, self->acp_env,
			self->acp_mcp_servers, self->acp_prompt, Array(), self->acp_auto_approve, false, self->acp_session);
	callable_mp(self, &LagoonicRuntimeHost::_on_acp_finished).call_deferred(result);
}

void LagoonicRuntimeHost::_on_acp_event(const Dictionary &p_event) {
	Dictionary event = p_event;
	if (String(event.get("type", "")) == "acp_options") {
		event["agent"] = acp_agent;
	}
	if (String(event.get("type", "")) == "permission") {
		Dictionary request = event.get("request", Dictionary());
		if (!request.has("sessionId")) {
			request["sessionId"] = String(request.get("session_id", acp_session));
			event["request"] = request;
		}
	}
	_emit_event(event);
}

void LagoonicRuntimeHost::_on_acp_permission(const Dictionary &p_request) {
	Dictionary request = p_request;
	request["sessionId"] = String(request.get("session_id", acp_session));
	Dictionary event;
	event["type"] = String("permission");
	event["session_id"] = request["sessionId"];
	event["request"] = request;
	_emit_event(event);
}

void LagoonicRuntimeHost::_on_acp_finished(const Dictionary &p_result) {
	acp_busy.clear();
	if (acp_thread.is_started()) {
		acp_thread.wait_to_finish();
	}
	if (!bool(p_result.get("ok", false))) {
		String message = String(p_result.get("error", ""));
		if (message.is_empty()) {
			message = String::utf8("El agente terminó sin responder.");
		}
		_emit_error(message);
		Dictionary idle;
		idle["type"] = String("busy");
		idle["session_id"] = acp_session;
		idle["busy"] = false;
		_emit_event(idle);
		return;
	}
	// Boss turn ended: run the tasks it queued (same hook as OpenCode's
	// turn_finished frame).
	if (!crew->is_in_progress()) {
		crew->maybe_run(acp_session);
	}
}

Mutex *LagoonicRuntimeHost::get_client_mutex() const {
	return &client_mutex;
}

void LagoonicRuntimeHost::set_opencode_bin(const String &p_bin) {
	if (!p_bin.strip_edges().is_empty()) {
		state_mutex.lock();
		opencode_bin = p_bin.strip_edges();
		state_mutex.unlock();
	}
}

String LagoonicRuntimeHost::get_opencode_bin() const {
	state_mutex.lock();
	const String bin = opencode_bin;
	state_mutex.unlock();
	return bin;
}

void LagoonicRuntimeHost::_on_sse_frame(const Dictionary &p_frame) {
	// Already on the main thread (client re-emits via call_deferred).
	const Dictionary result = mapper->lagoonic_feed(p_frame);
	const Array events = result.get("events", Array());
	for (int i = 0; i < events.size(); i++) {
		if (events[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		Dictionary event = events[i];
		// normalize: the agent dock reads request["sessionId"] (camelCase)
		// while the mapper emits "session_id". Carry both; harmless either
		// way and removed once the FIX owner aligns the contract.
		if (String(event.get("type", "")) == "permission") {
			Dictionary request = event.get("request", Dictionary());
			if (!request.has("sessionId")) {
				request["sessionId"] = String(request.get("session_id", get_session()));
				event["request"] = request;
			}
		}
		_emit_event(event);
	}
	const String failure = String(result.get("failure", ""));
	if (!failure.is_empty()) {
		_emit_error(failure);
	}
	if (bool(result.get("turn_finished", false))) {
		// Boss turn ended: run queued crew tasks, unless the round thread
		// is already going (wake-up turns) or the turn failed.
		if (failure.is_empty() && !crew->is_in_progress()) {
			state_mutex.lock();
			const String session = current_session;
			state_mutex.unlock();
			crew->maybe_run(session);
		}
	}
}

void LagoonicRuntimeHost::_on_sse_closed(const String &p_reason) {
	_emit_status("stream-closed:" + p_reason.left(80));
}

void LagoonicRuntimeHost::_on_session_recreated(const String &p_old_id, const String &p_new_id) {
	if (p_new_id.is_empty()) {
		return;
	}
	state_mutex.lock();
	const bool mine = !current_session.is_empty() && (p_old_id.is_empty() || p_old_id == current_session);
	if (mine) {
		current_session = p_new_id;
	}
	state_mutex.unlock();
	if (mine) {
		mapper->lagoonic_configure(p_new_id);
		_emit_status("session:" + p_new_id);
	}
}

void LagoonicRuntimeHost::_on_unexpected_exit(int p_exit_code, const String &p_hint) {
	// Emitted synchronously by whoever called check_health(); the host is
	// only driven from the main thread, so emitting here is safe.
	(void)p_exit_code;
	state_mutex.lock();
	server_down = true;
	const String session = current_session;
	state_mutex.unlock();
	if (client.is_valid() && client->lagoonic_is_subscribed()) {
		client->lagoonic_unsubscribe_events();
	}
	crew->cancel();
	Dictionary event;
	event["type"] = String("error");
	event["session_id"] = session;
	event["message"] = p_hint.is_empty() ? String("OpenCode server exited unexpectedly; it restarts on the next message.") : p_hint;
	_emit_event(event);
	_emit_status("down");
}

void LagoonicRuntimeHost::_crew_emit_chat(const String &p_session_id, const String &p_text) {
	// Crew worker line, shaped like the mapper's message events so the
	// chat renders it as a normal assistant bubble.
	Dictionary message;
	message["id"] = "crew-" + itos(OS::get_singleton()->get_ticks_msec());
	message["role"] = String("assistant");
	message["text"] = p_text;
	Dictionary event;
	event["type"] = String("message");
	event["session_id"] = p_session_id;
	event["message"] = message;
	_emit_event(event);
}

void LagoonicRuntimeHost::_crew_emit_error(const String &p_session_id, const String &p_message) {
	Dictionary event;
	event["type"] = String("error");
	event["session_id"] = p_session_id;
	event["message"] = p_message;
	_emit_event(event);
}
