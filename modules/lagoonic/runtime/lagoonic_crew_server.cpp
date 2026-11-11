/**************************************************************************/
/*  lagoonic_crew_server.cpp                                          */
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

#include "lagoonic_crew_server.h"

#include "core/io/json.h"
#include "core/io/stream_peer_tcp.h"
#include "core/math/math_funcs.h"
#include "core/object/class_db.h"

void LagoonicCrewServer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_running"), &LagoonicCrewServer::is_running);
	ClassDB::bind_method(D_METHOD("get_port"), &LagoonicCrewServer::get_port);
	ClassDB::bind_method(D_METHOD("get_url"), &LagoonicCrewServer::get_url);
	ClassDB::bind_method(D_METHOD("get_authorization"), &LagoonicCrewServer::get_authorization);
	ClassDB::bind_method(D_METHOD("start"), &LagoonicCrewServer::start);
	ClassDB::bind_method(D_METHOD("stop"), &LagoonicCrewServer::stop);
	ClassDB::bind_method(D_METHOD("poll"), &LagoonicCrewServer::poll);
}

LagoonicCrewServer::LagoonicCrewServer() {
}

LagoonicCrewServer::~LagoonicCrewServer() {
	stop();
}

String LagoonicCrewServer::get_url() const {
	return "http://127.0.0.1:" + String::num_int64(port) + "/mcp";
}

String LagoonicCrewServer::get_authorization() const {
	return "Bearer " + token;
}

// ---------------------------------------------------------------- lifecycle

Error LagoonicCrewServer::start() {
	if (running) {
		return OK;
	}
	token = _make_token();
	server.instantiate();
	Error err = server->listen(0, IPAddress("127.0.0.1"));
	if (err != OK) {
		server.unref();
		return err;
	}
	port = server->get_local_port();
	if (port <= 0) {
		server->stop();
		server.unref();
		port = 0;
		return ERR_CANT_CREATE;
	}
	running = true;
	return OK;
}

void LagoonicCrewServer::stop() {
	// Dropping the Refs closes the underlying sockets; no explicit
	// disconnect dance needed on the main thread.
	clients.clear();
	if (server.is_valid()) {
		if (server->is_listening()) {
			server->stop();
		}
		server.unref();
	}
	running = false;
	port = 0;
}

void LagoonicCrewServer::poll() {
	if (!running) {
		return;
	}
	_accept_clients();
	for (int i = clients.size() - 1; i >= 0; i--) {
		_service_client(i);
	}
}

String LagoonicCrewServer::_make_token() {
	// 24 random bytes as hex (48 chars), like randomBytes(24).toString('hex').
	Math::randomize();
	static const char *hex = "0123456789abcdef";
	String t;
	for (int i = 0; i < 48; i++) {
		t += String::chr(hex[Math::rand() % 16]);
	}
	return t;
}

// ---------------------------------------------------------------- transport

void LagoonicCrewServer::_accept_clients() {
	if (server.is_null() || !server->is_listening()) {
		return;
	}
	while (server->is_connection_available() && clients.size() < 8) {
		Ref<StreamPeerTCP> conn = server->take_connection();
		if (conn.is_null()) {
			break;
		}
		ClientState st;
		st.peer = conn;
		clients.push_back(st);
	}
}

void LagoonicCrewServer::_service_client(int p_idx) {
	if (p_idx < 0 || p_idx >= clients.size()) {
		return;
	}
	// Local copy of the peer: keeps the socket alive even if this client's
	// entry is dropped mid-processing. Element mutation goes through
	// `clients.write[]` (Godot 4 Vector has no mutable operator[]).
	Ref<StreamPeerTCP> peer = clients[p_idx].peer;
	if (peer.is_null() || peer->get_status() != StreamPeerTCP::STATUS_CONNECTED) {
		_close_client(p_idx);
		return;
	}

	int avail = peer->get_available_bytes();
	if (avail > 0) {
		int to_read = avail > 65536 ? 65536 : avail;
		Vector<uint8_t> buf;
		buf.resize(to_read);
		if (peer->get_data(buf.ptrw(), to_read) != OK) {
			_close_client(p_idx);
			return;
		}
		String &pending = clients.write[p_idx].pending;
		pending += String::utf8((const char *)buf.ptr(), to_read);
		if ((uint64_t)pending.length() > MAX_LINE_BYTES + 4096) {
			// Never-completing flood: drop without answering.
			_close_client(p_idx);
			return;
		}
	}

	if (p_idx >= clients.size()) {
		return;
	}
	int header_end = -1;
	int total_len = -1;
	if (!_request_complete(clients[p_idx].pending, header_end, total_len)) {
		return; // Incomplete: wait for more bytes.
	}
	(void)header_end;
	(void)total_len;
	String raw = clients[p_idx].pending;
	_handle_request(p_idx, raw);
}

void LagoonicCrewServer::_close_client(int p_idx) {
	if (p_idx < 0 || p_idx >= clients.size()) {
		return;
	}
	clients.remove_at(p_idx);
}

bool LagoonicCrewServer::_request_complete(const String &p_pending, int &r_header_end, int &r_total_len) const {
	r_header_end = p_pending.find("\r\n\r\n");
	if (r_header_end < 0) {
		return (uint64_t)p_pending.length() > MAX_LINE_BYTES;
	}
	String head = p_pending.substr(0, r_header_end);
	int content_len = 0;
	Vector<String> lines = head.split("\r\n");
	for (int i = 1; i < lines.size(); i++) {
		int colon = lines[i].find(":");
		if (colon < 0) {
			continue;
		}
		if (lines[i].substr(0, colon).strip_edges().to_lower() == "content-length") {
			content_len = lines[i].substr(colon + 1).strip_edges().to_int();
			break;
		}
	}
	if (content_len < 0) {
		content_len = 0;
	}
	r_total_len = r_header_end + 4 + content_len;
	return p_pending.length() >= r_total_len;
}

void LagoonicCrewServer::_answer(int p_idx, int p_status, const String &p_status_text, const String &p_body, const String &p_content_type) {
	if (p_idx < 0 || p_idx >= clients.size()) {
		return;
	}
	Ref<StreamPeerTCP> peer = clients[p_idx].peer;
	if (peer.is_null() || peer->get_status() != StreamPeerTCP::STATUS_CONNECTED) {
		_close_client(p_idx);
		return;
	}
	CharString body_utf8 = p_body.utf8();
	String head = "HTTP/1.1 " + String::num_int64(p_status) + " " + p_status_text + "\r\n" +
			"Content-Type: " + p_content_type + "\r\n" +
			"Content-Length: " + String::num_int64(body_utf8.length()) + "\r\n" +
			"Connection: close\r\n\r\n";
	CharString head_utf8 = head.utf8();
	(void)peer->put_data((const uint8_t *)head_utf8.get_data(), head_utf8.length());
	if (body_utf8.length() > 0) {
		(void)peer->put_data((const uint8_t *)body_utf8.get_data(), body_utf8.length());
	}
	// One request per connection, then close (mirrors the bridge plugin's
	// drop-after-reply simplicity; MCP clients POST per message anyway).
	_close_client(p_idx);
}

Dictionary LagoonicCrewServer::_parse_http_request(const String &p_raw, String &r_method, String &r_path, Dictionary &r_headers, String &r_body) const {
	Dictionary err;
	int header_end = p_raw.find("\r\n\r\n");
	if (header_end < 0) {
		err["error"] = "incomplete";
		return err;
	}
	String head = p_raw.substr(0, header_end);
	r_body = p_raw.substr(header_end + 4);
	Vector<String> lines = head.split("\r\n");
	if (lines.is_empty()) {
		err["error"] = "empty";
		return err;
	}
	Vector<String> req = lines[0].split(" ");
	if (req.size() < 2) {
		err["error"] = "bad-request-line";
		return err;
	}
	r_method = req[0].strip_edges().to_upper();
	r_path = req[1].strip_edges();
	r_headers.clear();
	for (int i = 1; i < lines.size(); i++) {
		int colon = lines[i].find(":");
		if (colon < 0) {
			continue;
		}
		r_headers[lines[i].substr(0, colon).strip_edges().to_lower()] = lines[i].substr(colon + 1).strip_edges();
	}
	return err; // Empty dict = OK.
}

void LagoonicCrewServer::_handle_request(int p_idx, const String &p_raw) {
	String method, path, body;
	Dictionary headers;
	if (!_parse_http_request(p_raw, method, path, headers, body).is_empty()) {
		_answer(p_idx, 400, "Bad Request", "", "text/plain");
		return;
	}
	if ((uint64_t)p_raw.length() > MAX_LINE_BYTES) {
		_answer(p_idx, 400, "Bad Request", "", "text/plain");
		return;
	}
	if (method != "POST") {
		_answer(p_idx, 405, "Method Not Allowed", "", "text/plain");
		return;
	}
	if (path != "/mcp") {
		_answer(p_idx, 404, "Not Found", "", "text/plain");
		return;
	}
	if (_dict_string(headers, "authorization", "") != get_authorization()) {
		_answer(p_idx, 401, "Unauthorized", "", "text/plain");
		return;
	}
	Variant parsed = JSON::parse_string(body);
	if (parsed.get_type() != Variant::DICTIONARY) {
		_answer(p_idx, 400, "Bad Request", "", "text/plain");
		return;
	}
	String reply = handle_crew_message(parsed);
	if (reply.is_empty()) {
		_answer(p_idx, 202, "Accepted", "", "text/plain");
		return;
	}
	_answer(p_idx, 200, "OK", reply, "application/json");
}

// ---------------------------------------------------------------- JSON-RPC

String LagoonicCrewServer::handle_crew_message(const Dictionary &p_message) const {
	if (!p_message.has("id")) {
		return ""; // Notification or response: HTTP 202.
	}
	Variant id = p_message["id"];
	if (!p_message.has("method") || p_message["method"].get_type() != Variant::STRING) {
		return ""; // Response to our (never sent) request: HTTP 202.
	}
	String rpc_method = p_message["method"];
	Dictionary params;
	if (p_message.has("params") && p_message["params"].get_type() == Variant::DICTIONARY) {
		params = p_message["params"];
	}
	if (rpc_method == "initialize") {
		String protocol = _dict_string(params, "protocolVersion", "");
		if (protocol.is_empty()) {
			protocol = "2025-06-18";
		}
		Dictionary tools_cap;
		tools_cap["listChanged"] = false;
		Dictionary caps;
		caps["tools"] = tools_cap;
		Dictionary info;
		info["name"] = CREW_SERVER_NAME;
		info["version"] = "1";
		Dictionary value;
		value["protocolVersion"] = protocol;
		value["capabilities"] = caps;
		value["serverInfo"] = info;
		return _json_result(id, value);
	} else if (rpc_method == "ping") {
		return _json_result(id, Dictionary());
	} else if (rpc_method == "tools/list") {
		Dictionary value;
		value["tools"] = _build_tool_list();
		return _json_result(id, value);
	} else if (rpc_method == "tools/call") {
		// Refusals arrive as a text payload with isError (never as a
		// JSON-RPC error), mirroring crew.ts callTool.
		return _json_result(id, _call_tool(params));
	}
	return _json_error(id, -32601, "Method not found");
}

Array LagoonicCrewServer::_build_tool_list() const {
	Vector<String> workers;
	if (tools != nullptr) {
		workers = tools->crew_worker_names();
	}
	String first = workers.is_empty() ? String() : workers[0];

	Dictionary worker_prop;
	worker_prop["type"] = "string";
	Array worker_enum;
	for (const String &w : workers) {
		worker_enum.push_back(w);
	}
	worker_prop["enum"] = worker_enum;
	worker_prop["description"] = "Which worker does it. Default: " + first + ".";

	Dictionary title_prop;
	title_prop["type"] = "string";
	title_prop["description"] = "Short name of the task, in the user's language, for the chat.";

	Dictionary task_prop;
	task_prop["type"] = "string";
	task_prop["description"] = "The full spec: target, the change, constraints, which files it may edit, and the evidence that proves it is done. The worker sees nothing else.";

	Dictionary start_props;
	start_props["worker"] = worker_prop;
	start_props["title"] = title_prop;
	start_props["task"] = task_prop;
	Dictionary start_schema;
	start_schema["type"] = "object";
	start_schema["properties"] = start_props;
	Array start_required;
	start_required.push_back("task");
	start_schema["required"] = start_required;

	Dictionary start_tool;
	start_tool["name"] = "start_worker";
	start_tool["description"] = "Hand one self-contained task to a worker agent. Workers start after you end your turn, run one at a time, and their reports come back to you as your next message. Start every independent task, then end your turn; never wait or poll.";
	start_tool["inputSchema"] = start_schema;

	Dictionary list_schema;
	list_schema["type"] = "object";
	list_schema["properties"] = Dictionary();
	Dictionary list_tool;
	list_tool["name"] = "list_workers";
	list_tool["description"] = "The worker tasks of this round and their status.";
	list_tool["inputSchema"] = list_schema;

	Array out;
	out.push_back(start_tool);
	out.push_back(list_tool);
	return out;
}

Dictionary LagoonicCrewServer::_call_tool(const Dictionary &p_params) const {
	if (!p_params.has("name") || p_params["name"].get_type() != Variant::STRING) {
		return _text_result("Unknown tool", true);
	}
	String tool_name = p_params["name"];
	Dictionary args;
	if (p_params.has("arguments") && p_params["arguments"].get_type() == Variant::DICTIONARY) {
		args = p_params["arguments"];
	}
	if (tool_name == "start_worker") {
		String spec = _dict_string(args, "task", "").strip_edges();
		if (spec.is_empty()) {
			return _text_result("task is empty: give the worker a self-contained task.", true);
		}
		String first_line = spec.get_slice("\n", 0);
		String title = _dict_string(args, "title", "").strip_edges();
		if (title.is_empty()) {
			title = first_line.substr(0, 60);
		}
		title = title.substr(0, 80);
		String worker = _dict_string(args, "worker", "").strip_edges();
		if (tools == nullptr) {
			return _text_result("No worker agent is set up. Do the task yourself.", true);
		}
		String error;
		String msg = tools->crew_start_worker(title, spec, worker, error);
		if (!error.is_empty()) {
			return _text_result(error, true);
		}
		return _text_result(msg, false);
	} else if (tool_name == "list_workers") {
		String text = "No worker tasks in this round.";
		if (tools != nullptr) {
			Vector<Dictionary> list = tools->crew_list_workers();
			if (!list.is_empty()) {
				String lines;
				for (int i = 0; i < list.size(); i++) {
					if (i > 0) {
						lines += "\n";
					}
					lines += "#" + String::num_int64(int64_t(list[i].get("number", 0))) +
							" [" + String(list[i].get("status", "")) + "] " +
							String(list[i].get("worker_name", "")) + ": " +
							String(list[i].get("title", ""));
				}
				text = lines;
			}
		}
		return _text_result(text, false);
	}
	return _text_result("Unknown tool " + tool_name, true);
}

Dictionary LagoonicCrewServer::_text_result(const String &p_text, bool p_is_error) {
	Dictionary item;
	item["type"] = "text";
	item["text"] = p_text;
	Array content;
	content.push_back(item);
	Dictionary out;
	out["content"] = content;
	out["isError"] = p_is_error;
	return out;
}

String LagoonicCrewServer::_json_result(const Variant &p_id, const Dictionary &p_value) {
	Dictionary out;
	out["jsonrpc"] = "2.0";
	out["id"] = p_id;
	out["result"] = p_value;
	return JSON::stringify(out);
}

String LagoonicCrewServer::_json_error(const Variant &p_id, int p_code, const String &p_message) {
	Dictionary detail;
	detail["code"] = p_code;
	detail["message"] = p_message;
	Dictionary out;
	out["jsonrpc"] = "2.0";
	out["id"] = p_id;
	out["error"] = detail;
	return JSON::stringify(out);
}

String LagoonicCrewServer::_dict_string(const Dictionary &p_dict, const StringName &p_key, const String &p_default) {
	if (!p_dict.has(p_key)) {
		return p_default;
	}
	Variant v = p_dict[p_key];
	if (v.get_type() != Variant::STRING) {
		return p_default;
	}
	return v;
}
