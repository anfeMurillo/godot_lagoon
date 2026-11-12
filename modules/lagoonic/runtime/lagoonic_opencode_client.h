/**************************************************************************/
/*  lagoonic_opencode_client.h                                        */
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

// Lagoonic OpenCode HTTP + SSE client (Godot 4.7 C++ module).
//
// C++ port of Core/lagoonic-desktop/src/main/opencode-client.ts, verified
// against the pinned OpenCode 1.x server (`opencode serve`, v1.19.5):
// auth is HTTP Basic with username `opencode` and the
// OPENCODE_SERVER_PASSWORD value; `GET /global/health` returns
// {healthy, version}; `POST /session` with {title} creates a session;
// `POST /session/:id/prompt_async` takes {parts, model:{providerID,
// modelID}, agent?}; `GET /event` streams {id, type, properties} envelopes.
// Every instance endpoint takes a `?directory=` query for workspace routing.
//
// Threading model (see the .cpp for details):
//   - All request/response methods are synchronous and blocking, with a
//     60 s budget each. Call them from a worker thread, never from the
//     editor main thread.
//   - lagoonic_subscribe_events() starts a persistent SSE reader on a
//     secondary Thread (GET /event?directory=...). Parsed frames are
//     re-emitted on the main thread via call_deferred as the
//     `lagoonic_sse_frame` signal plus the stored Callable.
//   - No secrets are ever printed: error strings contain only the HTTP
//     status and a truncated body, never headers.
//
// Name coordination (plan Lagoonic PC, Agente 4/10): Agente 3 owns
// lagoonic_vault / lagoonic_supervisor / lagoonic_config_writer; this file
// only introduces lagoonic_opencode_client / lagoonic_event_mapper symbols.

#include "core/object/ref_counted.h"
#include "core/templates/hash_map.h"

#include "core/io/http_client.h"
#include "core/os/mutex.h"
#include "core/os/thread.h"
#include "core/string/ustring.h"
#include "core/variant/array.h"
#include "core/variant/callable.h"
#include "core/variant/dictionary.h"

class LagoonicOpenCodeClient : public RefCounted {
	GDCLASS(LagoonicOpenCodeClient, RefCounted);

public:
	// Budget for every blocking HTTP round-trip (connect + request + body).
	static constexpr int64_t LAGOONIC_HTTP_TIMEOUT_MS = 60000;
	// Delay between SSE reconnect attempts while subscribed.
	static constexpr int64_t LAGOONIC_SSE_RETRY_MS = 2000;

	LagoonicOpenCodeClient();
	~LagoonicOpenCodeClient();

	// Base URL is always http://127.0.0.1:<port> (the supervisor picks a
	// free port). The password is the random OPENCODE_SERVER_PASSWORD value;
	// it is only ever sent in the Authorization header, never logged.
	void lagoonic_setup(const String &p_base_url, const String &p_password);
	// "basic" (default, verified against 1.19.5: `opencode:<password>`) or
	// "bearer" (`Bearer <password>`). Anything else falls back to "basic".
	void lagoonic_set_auth_mode(const String &p_mode);
	String lagoonic_get_auth_mode() const;
	String lagoonic_get_base_url() const;

	// GET /global/health -> true when {healthy: true} (1.x), or GET /api/info
	// answers with a version (2.x).
	bool lagoonic_health();
	// True when the server speaks the OpenCode 2.x API (`opencode serve`,
	// "v2 API", checked against 2.2.7). Probed once with GET /api/info: 2.x
	// answers JSON, 1.x has no such route. LAGOONIC_OPENCODE_API=v1|v2 skips
	// the probe. Every method below routes to the matching API, and 2.x
	// events are translated to the 1.x frames LagoonicEventMapper reads
	// (see lagoonic_opencode_v2.h).
	bool lagoonic_is_api_v2();
	// GET /global/health -> version string ("" when unknown).
	String lagoonic_version();

	// GET /session?directory= -> [{id, title, updated_at}].
	Array lagoonic_list_sessions(const String &p_directory);
	// POST /session?directory= {title} -> new session id ("" on error).
	String lagoonic_create_session(const String &p_directory, const String &p_title = String());
	// GET /session/:id/message?directory= -> [{id, session_id, role, time,
	// parts: [{id, session_id, message_id, type, text, tool, state}]}].
	Array lagoonic_list_messages(const String &p_session_id, const String &p_directory);
	// GET /provider?directory= -> [{id, name, env: [String],
	// models: [{provider_id, id, name}]}.
	Array lagoonic_list_providers(const String &p_directory);

	// POST /session/:id/prompt_async?directory= {parts, model, agent?}.
	// parts: [{type, text, ...}] (text parts carry {type:"text", text:...}).
	// model: provider_id + model_id; agent: "" (default), "build" or "plan".
	// Returns {ok: bool, session_id: String, recreated: bool, error: String}.
	// When the server answers 404 / not-found (deleted session), a
	// replacement session is created once and the prompt is retried with it;
	// `lagoonic_session_recreated(old_id, new_id)` is emitted in that case.
	Dictionary lagoonic_prompt_parts_async(const String &p_session_id, const String &p_directory, const Array &p_parts, const String &p_provider_id, const String &p_model_id, const String &p_agent = String(), const String &p_variant = String());

	// POST /session/:id/permissions/:permissionID?directory= {response}.
	// reply: "once" | "always" | "reject".
	bool lagoonic_respond_permission(const String &p_session_id, const String &p_directory, const String &p_permission_id, const String &p_reply);
	// POST /session/:id/abort?directory=.
	bool lagoonic_abort(const String &p_session_id, const String &p_directory);
	// POST /session/:id/revert?directory= {messageID}.
	bool lagoonic_revert(const String &p_session_id, const String &p_directory, const String &p_message_id);
	// GET /session/:id/diff?directory= -> [{path, additions, deletions}].
	Array lagoonic_diff(const String &p_session_id, const String &p_directory);
	// Same source, full entries for LagoonicEventMapper::
	// lagoonic_snapshot_diff_to_file_diff -> [{path, additions, deletions,
	// status, patch}].
	Array lagoonic_diff_full(const String &p_session_id, const String &p_directory);
	// DELETE /session/:id?directory=.
	bool lagoonic_delete_session(const String &p_directory, const String &p_session_id);
	// PATCH titulo de la sesion.
	bool lagoonic_rename_session(const String &p_directory, const String &p_session_id, const String &p_title);

	// True when a message/HTTP status means "the server no longer knows this
	// session" (caller is expected to recreate it). Matches HTTP 404 and
	// "not found" (case-insensitive), mirroring isNotFoundError() in TS.
	bool lagoonic_is_not_found_error(const String &p_message) const;

	// Starts the persistent SSE reader (GET /event?directory=...) on a
	// secondary thread. Every `data: {...}` payload is parsed into
	// {id, type, properties} and re-emitted on the main thread via
	// call_deferred: signal `lagoonic_sse_frame(frame)` plus p_callback.
	// Returns false when already subscribed.
	bool lagoonic_subscribe_events(const String &p_directory, const Callable &p_callback);
	void lagoonic_unsubscribe_events();
	bool lagoonic_is_subscribed() const;

protected:
	static void _bind_methods();

private:
	// Result of one blocking HTTP round-trip. `body` is the response text
	// (truncated for errors only at the log site, never here with secrets).
	struct LagoonicHttpResult {
		bool ok = false;
		int code = 0;
		String body;
		String error;
	};

	LagoonicHttpResult _lagoonic_http_request(HTTPClient::Method p_method, const String &p_path_query, const String &p_body = String(), bool p_is_sse = false);
	String _lagoonic_auth_header() const;
	static String _lagoonic_with_directory(const String &p_path, const String &p_directory);
	// 2.x routing: `location[directory]` query (same as the
	// x-opencode-directory header) plus any extra `&key=value` pairs.
	static String _lagoonic_v2_path(const String &p_path, const String &p_directory, const String &p_extra_query = String());
	// `{data}` of a 2.x response body (the body itself when unwrapped).
	static Variant _lagoonic_v2_data(const String &p_body);
	Array _lagoonic_v2_list_providers(const String &p_directory);

	// SSE worker entry point (runs on lagoonic_sse_thread). Never touches
	// the scene tree; the only cross-thread call is call_deferred().
	void _lagoonic_sse_loop();
	static void _lagoonic_sse_thread_entry(void *p_userdata);
	// Main-thread fan-out, invoked via call_deferred from the SSE thread.
	// Bound in _bind_methods (call_deferred requires a bound method).
	void _lagoonic_emit_frame(const Dictionary &p_frame);
	void _lagoonic_emit_closed(const String &p_reason);
	bool _lagoonic_should_stop() const;
	static void _lagoonic_sleep_interruptible(int64_t p_ms, const LagoonicOpenCodeClient *p_self);

	String lagoonic_base_url = "http://127.0.0.1:4096";
	String lagoonic_password;
	String lagoonic_auth_mode = "basic";
	int64_t lagoonic_timeout_ms = LAGOONIC_HTTP_TIMEOUT_MS;
	// 0 = not probed yet, 1 = OpenCode 1.x, 2 = OpenCode 2.x.
	mutable Mutex lagoonic_api_mutex;
	int lagoonic_api = 0;
	// Last model / agent selected per session on a 2.x server, so a prompt
	// only switches them when they change (2.x prompts carry neither).
	HashMap<String, String> lagoonic_v2_model;
	HashMap<String, String> lagoonic_v2_agent;

	Thread lagoonic_sse_thread;
	mutable Mutex lagoonic_sse_mutex;
	bool lagoonic_sse_stop = false;
	bool lagoonic_sse_active = false;
	String lagoonic_sse_directory;
	Callable lagoonic_sse_callback;
};
