/**************************************************************************/
/*  lagoonic_crew_server.h                                            */
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

// Lagoonic crew MCP server (C++ port of Core/lagoonic-desktop/src/main/crew.ts).
//
// The MCP side of the crew: the boss agent's MCP client talks JSON-RPC 2.0
// over Streamable HTTP (POST /mcp) and this server answers with a JSON
// string. Notifications and responses get HTTP 202 with an empty body.
//
// Protocol (mirrors the desktop CrewMcpServer):
//   1. Listen on 127.0.0.1, ephemeral port. Every request must carry the
//      random 24-byte bearer token as `Authorization: Bearer <token>`.
//   2. POST /mcp with a JSON-RPC 2.0 message. Non-POST -> 405, bad auth ->
//      401, oversize/unparseable body -> 400 (and the connection is dropped).
//   3. Replies are {jsonrpc:"2.0", id, result:{content:[{type:"text",
//      text}], isError}} or {jsonrpc:"2.0", id, error:{code, message}}.
//      Unknown methods answer -32601; handler failures answer -32603.
//   4. Requests without an `id` (or without a string `method`) are
//      notifications/responses: HTTP 202, no body.
//
// Tools:
//   - start_worker(task, title?, worker?): queues one self-contained task for
//     a worker. Workers start after the boss ends its turn, run one at a
//     time, and their reports come back as the boss's next message.
//   - list_workers: the worker tasks of the current round and their status.
//
// Threading: none inside this class. The owner polls from the main thread
// (same pattern as LagoonicBridgePlugin::NOTIFICATION_PROCESS), so every
// tool call below runs on the main thread. Bodies longer than 1 MiB drop
// the connection.
//
// Build wiring (NOT done here: this file must not edit anything existing):
//   - Add `runtime/*.cpp` to Engine/godot/modules/lagoonic/SCsub so these
//     sources compile (one line next to the existing `editor/*.cpp` glob).
//   - The future bridge/dock creates one server per crew session, implements
//     LagoonicCrewTools on top of the orchestrator, polls it, and calls
//     stop() on runtime close. Server death must call orchestrator fail() so
//     waiters never hang (see lagoonic_crew_orchestrator.h).

#include "core/io/tcp_server.h"
#include "core/object/ref_counted.h"

class StreamPeerTCP;

class LagoonicCrewTools {
public:
	// Names of the workers the boss can pick; the first one is the default.
	virtual Vector<String> crew_worker_names() const = 0;
	// Queues a task for `p_worker` (empty = default). Returns the message
	// for the boss, or "" and sets `r_error` when the task is refused.
	// `p_from_boss`: the call carries the boss-only token (an ACP boss), so it
	// can never come from a worker, even while one is running.
	virtual String crew_start_worker(const String &p_title, const String &p_spec, const String &p_worker, const String &p_model, bool p_from_boss, String &r_error) = 0;
	// The models a worker offers ("" = every worker), as text for the boss.
	virtual String crew_list_models(const String &p_worker) = 0;
	// Stops task p_number (0 = every running/queued task). Returns the
	// message for the boss, or "" and sets r_error.
	virtual String crew_stop_worker(int p_number, String &r_error) = 0;
	// Removes finished, failed and stopped tasks from the list. Returns the message.
	virtual String crew_clear_workers() = 0;
	// Adds instructions to task p_number while it runs (or before it starts).
	virtual String crew_message_worker(int p_number, const String &p_message, String &r_error) = 0;
	// One Dictionary per round task: {number, status, worker_name, title}.
	virtual Vector<Dictionary> crew_list_workers() const = 0;
	virtual ~LagoonicCrewTools() {}
};

class LagoonicCrewServer : public RefCounted {
	GDCLASS(LagoonicCrewServer, RefCounted);

public:
	static constexpr const char *CREW_SERVER_NAME = "lagoonic-crew";
	static constexpr uint64_t MAX_LINE_BYTES = 1048576; // 1 MiB.

private:
	struct ClientState {
		Ref<StreamPeerTCP> peer;
		Vector<uint8_t> pending; // Raw bytes of the not yet fully received HTTP request.
		bool responded = false;
	};

	Ref<TCPServer> server;
	Vector<ClientState> clients;
	LagoonicCrewTools *tools = nullptr; // Not owned; the orchestrator.

	String token; // Shared: OpenCode's config, read by its boss and its workers.
	String boss_token; // Only ACP bosses (Claude Code, Codex...) get this one.
	mutable bool request_from_boss = false; // Set per request on the main thread.
	int port = 0;
	bool running = false;

	void _accept_clients();
	void _service_client(int p_idx);
	void _close_client(int p_idx);
	bool _request_complete(const Vector<uint8_t> &p_pending, int &r_header_end, int &r_total_len) const;
	void _answer(int p_idx, int p_status, const String &p_status_text, const String &p_body, const String &p_content_type);
	void _handle_request(int p_idx, const String &p_raw);

	Dictionary _parse_http_request(const String &p_raw, String &r_method, String &r_path, Dictionary &r_headers, String &r_body) const;
	String _make_token();

	// Pure JSON-RPC layer, no sockets. Returns "" for notifications (HTTP 202).
	Dictionary _call_tool(const Dictionary &p_params) const;
	Array _build_tool_list() const;
	static Dictionary _text_result(const String &p_text, bool p_is_error);
	static String _json_result(const Variant &p_id, const Dictionary &p_value);
	static String _json_error(const Variant &p_id, int p_code, const String &p_message);
	static String _dict_string(const Dictionary &p_dict, const StringName &p_key, const String &p_default);

protected:
	static void _bind_methods();

public:
	void set_tools(LagoonicCrewTools *p_tools) { tools = p_tools; }

	// Pure handler, exposed for tests: JSON-RPC request -> reply, or "".
	String handle_crew_message(const Dictionary &p_message) const;

	bool is_running() const { return running; }
	int get_port() const { return port; }
	String get_url() const;
	String get_authorization() const;
	String get_boss_authorization() const;

	Error start();
	void stop();
	void poll(); // Call from the main thread, every process tick.

	LagoonicCrewServer();
	~LagoonicCrewServer();
};
