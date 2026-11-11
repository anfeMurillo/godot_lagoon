/**************************************************************************/
/*  lagoonic_opencode_client.cpp                                      */
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

#include "lagoonic_opencode_client.h"

#include "core/io/json.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "core/variant/callable.h"

// ---------------------------------------------------------------------------
// Local helpers (no engine API risk: hand-rolled base64 / query encoding so
// this file does not depend on Marshalls or String::http_escape).
// ---------------------------------------------------------------------------

static String _lagoonic_base64_encode(const PackedByteArray &p_data) {
	static const char *s_alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	const int n = p_data.size();
	if (n <= 0) {
		return String();
	}
	const uint8_t *r = p_data.ptr();
	String out;
	for (int i = 0; i < n; i += 3) {
		const uint32_t a = r[i];
		const uint32_t b = (i + 1 < n) ? r[i + 1] : 0;
		const uint32_t c = (i + 2 < n) ? r[i + 2] : 0;
		const uint32_t triple = (a << 16) | (b << 8) | c;
		out += String::chr(s_alphabet[(triple >> 18) & 0x3F]);
		out += String::chr(s_alphabet[(triple >> 12) & 0x3F]);
		out += (i + 1 < n) ? String::chr(s_alphabet[(triple >> 6) & 0x3F]) : String("=");
		out += (i + 2 < n) ? String::chr(s_alphabet[triple & 0x3F]) : String("=");
	}
	return out;
}

// Percent-encodes a query value (keeps unreserved chars only, so `/` in a
// workspace path becomes %2F). Operates on UTF-8 bytes, so non-ASCII paths
// survive the round-trip.
static String _lagoonic_encode_query(const String &p_text) {
	static const char *s_hex = "0123456789ABCDEF";
	const PackedByteArray bytes = p_text.to_utf8_buffer();
	String out;
	for (int i = 0; i < bytes.size(); i++) {
		const uint8_t c = bytes[i];
		const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
				(c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
		if (unreserved) {
			out += String::chr(c);
		} else {
			out += String::chr('%');
			out += String::chr(s_hex[(c >> 4) & 0xF]);
			out += String::chr(s_hex[c & 0xF]);
		}
	}
	return out;
}

static String _lagoonic_dict_string(const Dictionary &p_dict, const StringName &p_key, const String &p_default = String()) {
	if (!p_dict.has(p_key)) {
		return p_default;
	}
	const Variant v = p_dict.get(p_key, Variant());
	return (v.get_type() == Variant::STRING) ? String(v) : p_default;
}

static int64_t _lagoonic_dict_int(const Dictionary &p_dict, const StringName &p_key, int64_t p_default = 0) {
	if (!p_dict.has(p_key)) {
		return p_default;
	}
	const Variant v = p_dict.get(p_key, Variant());
	switch (v.get_type()) {
		case Variant::INT:
			return int64_t(v);
		case Variant::FLOAT:
			return int64_t(double(v));
		default:
			return p_default;
	}
}

// Splits "http://127.0.0.1:4096[/prefix]" into host / port / path prefix.
static void _lagoonic_split_base_url(const String &p_base_url, String &r_host, int &r_port, bool &r_use_tls, String &r_prefix) {
	String rest = p_base_url.strip_edges();
	r_use_tls = rest.begins_with("https://");
	if (rest.begins_with("http://")) {
		rest = rest.substr(7);
	} else if (r_use_tls) {
		rest = rest.substr(8);
	}
	while (rest.ends_with("/")) {
		rest = rest.substr(0, rest.length() - 1);
	}
	const int slash = rest.find("/");
	String hostport = (slash < 0) ? rest : rest.substr(0, slash);
	r_prefix = (slash < 0) ? String() : rest.substr(slash);
	const int colon = hostport.rfind(":");
	if (colon >= 0) {
		r_host = hostport.substr(0, colon);
		r_port = hostport.substr(colon + 1).to_int();
	} else {
		r_host = hostport;
		r_port = r_use_tls ? 443 : 80;
	}
	if (r_host.is_empty()) {
		r_host = "127.0.0.1";
	}
}

// ---------------------------------------------------------------- lifecycle

LagoonicOpenCodeClient::LagoonicOpenCodeClient() {
}

LagoonicOpenCodeClient::~LagoonicOpenCodeClient() {
	lagoonic_unsubscribe_events();
}

void LagoonicOpenCodeClient::_bind_methods() {
	ClassDB::bind_method(D_METHOD("lagoonic_setup", "base_url", "password"), &LagoonicOpenCodeClient::lagoonic_setup);
	ClassDB::bind_method(D_METHOD("lagoonic_set_auth_mode", "mode"), &LagoonicOpenCodeClient::lagoonic_set_auth_mode);
	ClassDB::bind_method(D_METHOD("lagoonic_get_auth_mode"), &LagoonicOpenCodeClient::lagoonic_get_auth_mode);
	ClassDB::bind_method(D_METHOD("lagoonic_get_base_url"), &LagoonicOpenCodeClient::lagoonic_get_base_url);

	ClassDB::bind_method(D_METHOD("lagoonic_health"), &LagoonicOpenCodeClient::lagoonic_health);
	ClassDB::bind_method(D_METHOD("lagoonic_version"), &LagoonicOpenCodeClient::lagoonic_version);
	ClassDB::bind_method(D_METHOD("lagoonic_list_sessions", "directory"), &LagoonicOpenCodeClient::lagoonic_list_sessions);
	ClassDB::bind_method(D_METHOD("lagoonic_create_session", "directory", "title"), &LagoonicOpenCodeClient::lagoonic_create_session, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("lagoonic_list_messages", "session_id", "directory"), &LagoonicOpenCodeClient::lagoonic_list_messages);
	ClassDB::bind_method(D_METHOD("lagoonic_list_providers", "directory"), &LagoonicOpenCodeClient::lagoonic_list_providers);
	ClassDB::bind_method(D_METHOD("lagoonic_prompt_parts_async", "session_id", "directory", "parts", "provider_id", "model_id", "agent"), &LagoonicOpenCodeClient::lagoonic_prompt_parts_async, DEFVAL(String()));

	ClassDB::bind_method(D_METHOD("lagoonic_respond_permission", "session_id", "directory", "permission_id", "reply"), &LagoonicOpenCodeClient::lagoonic_respond_permission);
	ClassDB::bind_method(D_METHOD("lagoonic_abort", "session_id", "directory"), &LagoonicOpenCodeClient::lagoonic_abort);
	ClassDB::bind_method(D_METHOD("lagoonic_revert", "session_id", "directory", "message_id"), &LagoonicOpenCodeClient::lagoonic_revert);
	ClassDB::bind_method(D_METHOD("lagoonic_diff", "session_id", "directory"), &LagoonicOpenCodeClient::lagoonic_diff);
	ClassDB::bind_method(D_METHOD("lagoonic_diff_full", "session_id", "directory"), &LagoonicOpenCodeClient::lagoonic_diff_full);
	ClassDB::bind_method(D_METHOD("lagoonic_delete_session", "directory", "session_id"), &LagoonicOpenCodeClient::lagoonic_delete_session);

	ClassDB::bind_method(D_METHOD("lagoonic_is_not_found_error", "message"), &LagoonicOpenCodeClient::lagoonic_is_not_found_error);

	ClassDB::bind_method(D_METHOD("lagoonic_subscribe_events", "directory", "callback"), &LagoonicOpenCodeClient::lagoonic_subscribe_events);
	ClassDB::bind_method(D_METHOD("lagoonic_unsubscribe_events"), &LagoonicOpenCodeClient::lagoonic_unsubscribe_events);
	ClassDB::bind_method(D_METHOD("lagoonic_is_subscribed"), &LagoonicOpenCodeClient::lagoonic_is_subscribed);

	// Internal fan-out for the SSE thread (call_deferred can only invoke
	// bound methods, so these are bound but documented as internal).
	ClassDB::bind_method(D_METHOD("_lagoonic_emit_frame", "frame"), &LagoonicOpenCodeClient::_lagoonic_emit_frame);
	ClassDB::bind_method(D_METHOD("_lagoonic_emit_closed", "reason"), &LagoonicOpenCodeClient::_lagoonic_emit_closed);

	ADD_SIGNAL(MethodInfo("lagoonic_sse_frame", PropertyInfo(Variant::DICTIONARY, "frame")));
	ADD_SIGNAL(MethodInfo("lagoonic_sse_closed", PropertyInfo(Variant::STRING, "reason")));
	ADD_SIGNAL(MethodInfo("lagoonic_session_recreated", PropertyInfo(Variant::STRING, "old_session_id"), PropertyInfo(Variant::STRING, "new_session_id")));
}

void LagoonicOpenCodeClient::lagoonic_setup(const String &p_base_url, const String &p_password) {
	lagoonic_base_url = p_base_url.strip_edges();
	if (lagoonic_base_url.is_empty()) {
		lagoonic_base_url = "http://127.0.0.1:4096";
	}
	lagoonic_password = p_password;
}

void LagoonicOpenCodeClient::lagoonic_set_auth_mode(const String &p_mode) {
	const String mode = p_mode.strip_edges().to_lower();
	lagoonic_auth_mode = (mode == "bearer") ? "bearer" : "basic";
}

String LagoonicOpenCodeClient::lagoonic_get_auth_mode() const {
	return lagoonic_auth_mode;
}

String LagoonicOpenCodeClient::lagoonic_get_base_url() const {
	return lagoonic_base_url;
}

// ------------------------------------------------------------------ requests

String LagoonicOpenCodeClient::_lagoonic_auth_header() const {
	if (lagoonic_auth_mode == "bearer") {
		return "Authorization: Bearer " + lagoonic_password;
	}
	// Verified against the pinned 1.19.5 server: HTTP Basic with username
	// `opencode` and the OPENCODE_SERVER_PASSWORD value.
	const PackedByteArray raw = ("opencode:" + lagoonic_password).to_utf8_buffer();
	return "Authorization: Basic " + _lagoonic_base64_encode(raw);
}

String LagoonicOpenCodeClient::_lagoonic_with_directory(const String &p_path, const String &p_directory) {
	return p_path + "?directory=" + _lagoonic_encode_query(p_directory);
}

LagoonicOpenCodeClient::LagoonicHttpResult LagoonicOpenCodeClient::_lagoonic_http_request(HTTPClient::Method p_method, const String &p_path_query, const String &p_body, bool p_is_sse) {
	LagoonicHttpResult result;

	String host;
	int port = 80;
	bool use_tls = false;
	String prefix;
	_lagoonic_split_base_url(lagoonic_base_url, host, port, use_tls, prefix);
	const String target = prefix + p_path_query;

	Ref<HTTPClient> cli(HTTPClient::create());
	if (cli.is_null()) {
		result.error = "could not create http client";
		return result;
	}

	const uint64_t t0 = Time::get_singleton()->get_ticks_msec();
	const uint64_t budget = (lagoonic_timeout_ms > 1000) ? (uint64_t)lagoonic_timeout_ms : 1000;
	const uint64_t deadline = t0 + budget;

	Ref<TLSOptions> tls_options;
	if (use_tls) {
		tls_options = TLSOptions::client();
	}
	if (cli->connect_to_host(host, port, tls_options) != OK) {
		result.error = "could not resolve " + host;
		return result;
	}
	while (cli->get_status() == HTTPClient::STATUS_CONNECTING || cli->get_status() == HTTPClient::STATUS_RESOLVING) {
		cli->poll();
		if (Time::get_singleton()->get_ticks_msec() > deadline) {
			result.error = "connect timed out";
			cli->close();
			return result;
		}
		OS::get_singleton()->delay_usec(5000);
	}
	if (cli->get_status() != HTTPClient::STATUS_CONNECTED) {
		result.error = "could not connect";
		cli->close();
		return result;
	}

	Vector<String> headers;
	headers.push_back(_lagoonic_auth_header());
	headers.push_back(p_is_sse ? "Accept: text/event-stream" : "Accept: application/json");
	if (!p_body.is_empty()) {
		headers.push_back("Content-Type: application/json");
	}
	const PackedByteArray body_bytes = p_body.to_utf8_buffer();
	if (cli->request(p_method, target, headers, body_bytes.is_empty() ? nullptr : body_bytes.ptr(), body_bytes.size()) != OK) {
		result.error = "request failed";
		cli->close();
		return result;
	}
	while (cli->get_status() == HTTPClient::STATUS_REQUESTING) {
		cli->poll();
		if (Time::get_singleton()->get_ticks_msec() > deadline) {
			result.error = "request timed out";
			cli->close();
			return result;
		}
		OS::get_singleton()->delay_usec(5000);
	}
	if (!cli->has_response()) {
		result.error = "no response";
		cli->close();
		return result;
	}
	result.code = cli->get_response_code();

	PackedByteArray data;
	int empty_reads = 0;
	while (cli->get_status() == HTTPClient::STATUS_BODY) {
		cli->poll();
		if (Time::get_singleton()->get_ticks_msec() > deadline) {
			result.error = "body timed out";
			cli->close();
			return result;
		}
		const PackedByteArray chunk = cli->read_response_body_chunk();
		if (chunk.size() > 0) {
			data.append_array(chunk);
			empty_reads = 0;
			// NOTE: the SSE path never uses this helper's body loop (the
			// persistent reader lives in _lagoonic_sse_loop); this branch
			// only serves finite JSON responses.
			if (!p_is_sse) {
				const int64_t want = cli->get_response_body_length();
				if (want >= 0 && data.size() >= want) {
					break;
				}
			}
		} else {
			if (++empty_reads > 2000) {
				break; // ~2 s of silence on a finite body: take what we have.
			}
			OS::get_singleton()->delay_usec(1000);
		}
	}
	cli->close();

	result.body = data.is_empty() ? String() : String::utf8((const char *)data.ptr(), data.size());
	result.ok = (result.code >= 200 && result.code <= 299);
	if (!result.ok && result.error.is_empty()) {
		result.error = "HTTP " + String::num_int64(result.code);
	}
	return result;
}

bool LagoonicOpenCodeClient::lagoonic_health() {
	const LagoonicHttpResult r = _lagoonic_http_request(HTTPClient::METHOD_GET, "/global/health");
	if (!r.ok) {
		return false;
	}
	const Variant parsed = JSON::parse_string(r.body);
	if (parsed.get_type() != Variant::DICTIONARY) {
		return false;
	}
	const Variant healthy = Dictionary(parsed).get("healthy", Variant());
	return healthy.get_type() == Variant::BOOL && bool(healthy);
}

String LagoonicOpenCodeClient::lagoonic_version() {
	const LagoonicHttpResult r = _lagoonic_http_request(HTTPClient::METHOD_GET, "/global/health");
	if (!r.ok) {
		return String();
	}
	const Variant parsed = JSON::parse_string(r.body);
	if (parsed.get_type() != Variant::DICTIONARY) {
		return String();
	}
	return _lagoonic_dict_string(Dictionary(parsed), "version");
}

Array LagoonicOpenCodeClient::lagoonic_list_sessions(const String &p_directory) {
	Array out;
	const LagoonicHttpResult r = _lagoonic_http_request(HTTPClient::METHOD_GET, _lagoonic_with_directory("/session", p_directory));
	if (!r.ok) {
		return out;
	}
	const Variant parsed = JSON::parse_string(r.body);
	if (parsed.get_type() != Variant::ARRAY) {
		return out;
	}
	for (int i = 0; i < Array(parsed).size(); i++) {
		const Variant entry = Array(parsed)[i];
		if (entry.get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary info = entry;
		const String id = _lagoonic_dict_string(info, "id");
		if (id.is_empty()) {
			continue;
		}
		Dictionary row;
		row["id"] = id;
		String title = _lagoonic_dict_string(info, "title");
		row["title"] = title.is_empty() ? String("Untitled") : title;
		int64_t updated = 0;
		const Variant time_v = info.get("time", Variant());
		if (time_v.get_type() == Variant::DICTIONARY) {
			const Dictionary time = time_v;
			updated = _lagoonic_dict_int(time, "updated", _lagoonic_dict_int(time, "created", 0));
		}
		row["updated_at"] = updated;
		out.push_back(row);
	}
	return out;
}

String LagoonicOpenCodeClient::lagoonic_create_session(const String &p_directory, const String &p_title) {
	Dictionary payload;
	if (!p_title.strip_edges().is_empty()) {
		payload["title"] = p_title;
	}
	const LagoonicHttpResult r = _lagoonic_http_request(HTTPClient::METHOD_POST, _lagoonic_with_directory("/session", p_directory), JSON::stringify(payload));
	if (!r.ok) {
		return String();
	}
	const Variant parsed = JSON::parse_string(r.body);
	if (parsed.get_type() != Variant::DICTIONARY) {
		return String();
	}
	return _lagoonic_dict_string(Dictionary(parsed), "id");
}

Array LagoonicOpenCodeClient::lagoonic_list_messages(const String &p_session_id, const String &p_directory) {
	Array out;
	const LagoonicHttpResult r = _lagoonic_http_request(HTTPClient::METHOD_GET, _lagoonic_with_directory("/session/" + _lagoonic_encode_query(p_session_id) + "/message", p_directory));
	if (!r.ok) {
		return out;
	}
	const Variant parsed = JSON::parse_string(r.body);
	if (parsed.get_type() != Variant::ARRAY) {
		return out;
	}
	for (int i = 0; i < Array(parsed).size(); i++) {
		const Variant entry = Array(parsed)[i];
		if (entry.get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary root = entry;
		const Variant info_v = root.get("info", Variant());
		if (info_v.get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary info = info_v;
		const String id = _lagoonic_dict_string(info, "id");
		if (id.is_empty()) {
			continue;
		}
		Dictionary msg;
		msg["id"] = id;
		msg["session_id"] = _lagoonic_dict_string(info, "sessionID");
		const String role = _lagoonic_dict_string(info, "role");
		msg["role"] = (role == "assistant") ? String("assistant") : String("user");
		int64_t created = 0;
		const Variant time_v = info.get("time", Variant());
		if (time_v.get_type() == Variant::DICTIONARY) {
			created = _lagoonic_dict_int(Dictionary(time_v), "created", 0);
		}
		msg["time"] = created;
		Array parts;
		const Variant parts_v = root.get("parts", Variant());
		if (parts_v.get_type() == Variant::ARRAY) {
			for (int j = 0; j < Array(parts_v).size(); j++) {
				const Variant part_v = Array(parts_v)[j];
				if (part_v.get_type() != Variant::DICTIONARY) {
					continue;
				}
				const Dictionary part = part_v;
				Dictionary row;
				row["id"] = _lagoonic_dict_string(part, "id");
				row["session_id"] = _lagoonic_dict_string(part, "sessionID");
				String mid = _lagoonic_dict_string(part, "messageID");
				row["message_id"] = mid.is_empty() ? id : mid;
				row["type"] = _lagoonic_dict_string(part, "type");
				if (part.has("text") && Variant(part.get("text", Variant())).get_type() == Variant::STRING) {
					row["text"] = String(part.get("text", Variant()));
				}
				if (part.has("tool") && Variant(part.get("tool", Variant())).get_type() == Variant::STRING) {
					row["tool"] = String(part.get("tool", Variant()));
				}
				if (part.has("state") && Variant(part.get("state", Variant())).get_type() == Variant::DICTIONARY) {
					row["state"] = part.get("state", Variant());
				}
				parts.push_back(row);
			}
		}
		msg["parts"] = parts;
		out.push_back(msg);
	}
	return out;
}

Array LagoonicOpenCodeClient::lagoonic_list_providers(const String &p_directory) {
	Array out;
	const LagoonicHttpResult r = _lagoonic_http_request(HTTPClient::METHOD_GET, _lagoonic_with_directory("/provider", p_directory));
	if (!r.ok) {
		return out;
	}
	const Variant parsed = JSON::parse_string(r.body);
	if (parsed.get_type() != Variant::DICTIONARY) {
		return out;
	}
	const Variant all_v = Dictionary(parsed).get("all", Variant());
	if (all_v.get_type() != Variant::ARRAY) {
		return out;
	}
	for (int i = 0; i < Array(all_v).size(); i++) {
		const Variant entry = Array(all_v)[i];
		if (entry.get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary provider = entry;
		const String id = _lagoonic_dict_string(provider, "id");
		if (id.is_empty()) {
			continue;
		}
		Dictionary row;
		row["id"] = id;
		String name = _lagoonic_dict_string(provider, "name");
		row["name"] = name.is_empty() ? id : name;
		Array env;
		const Variant env_v = provider.get("env", Variant());
		if (env_v.get_type() == Variant::ARRAY) {
			for (int j = 0; j < Array(env_v).size(); j++) {
				if (Array(env_v)[j].get_type() == Variant::STRING) {
					env.push_back(Array(env_v)[j]);
				}
			}
		}
		row["env"] = env;
		Array models;
		const Variant models_v = provider.get("models", Variant());
		if (models_v.get_type() == Variant::DICTIONARY) {
			const Array keys = Dictionary(models_v).keys();
			for (int j = 0; j < keys.size(); j++) {
				const Variant m_v = Dictionary(models_v).get(keys[j], Variant());
				if (m_v.get_type() != Variant::DICTIONARY) {
					continue;
				}
				const Dictionary m = m_v;
				Dictionary mrow;
				mrow["provider_id"] = id;
				String mid = _lagoonic_dict_string(m, "id");
				mrow["id"] = mid.is_empty() ? String(keys[j]) : mid;
				String mname = _lagoonic_dict_string(m, "name");
				mrow["name"] = mname.is_empty() ? String(mrow["id"]) : mname;
				models.push_back(mrow);
			}
		}
		row["models"] = models;
		out.push_back(row);
	}
	return out;
}

Dictionary LagoonicOpenCodeClient::lagoonic_prompt_parts_async(const String &p_session_id, const String &p_directory, const Array &p_parts, const String &p_provider_id, const String &p_model_id, const String &p_agent) {
	Dictionary result;
	result["ok"] = false;
	result["session_id"] = p_session_id;
	result["recreated"] = false;
	result["error"] = String();

	Dictionary model;
	model["providerID"] = p_provider_id;
	model["modelID"] = p_model_id;
	Dictionary payload;
	payload["parts"] = p_parts;
	payload["model"] = model;
	if (!p_agent.strip_edges().is_empty()) {
		payload["agent"] = p_agent; // "build" or "plan".
	}
	const String body = JSON::stringify(payload);

	auto send_once = [&](const String &p_sid) -> LagoonicHttpResult {
		return _lagoonic_http_request(HTTPClient::METHOD_POST, _lagoonic_with_directory("/session/" + _lagoonic_encode_query(p_sid) + "/prompt_async", p_directory), body);
	};

	LagoonicHttpResult r = send_once(p_session_id);
	if (r.ok) {
		result["ok"] = true;
		return result;
	}
	// Deleted session (404 / not-found): recreate once and retry the prompt.
	// Only the status and a truncated body are reported; never headers.
	if (lagoonic_is_not_found_error(String::num_int64(r.code)) || lagoonic_is_not_found_error(r.body) || lagoonic_is_not_found_error(r.error)) {
		const String fresh = lagoonic_create_session(p_directory, "Restored chat");
		if (!fresh.is_empty()) {
			LagoonicHttpResult retry = send_once(fresh);
			if (retry.ok) {
				result["ok"] = true;
				result["session_id"] = fresh;
				result["recreated"] = true;
				emit_signal(StringName("lagoonic_session_recreated"), p_session_id, fresh);
				return result;
			}
			result["error"] = retry.error.is_empty() ? ("HTTP " + String::num_int64(retry.code)) : retry.error;
			return result;
		}
	}
	result["error"] = r.error.is_empty() ? ("HTTP " + String::num_int64(r.code)) : r.error;
	return result;
}

bool LagoonicOpenCodeClient::lagoonic_respond_permission(const String &p_session_id, const String &p_directory, const String &p_permission_id, const String &p_reply) {
	String reply = p_reply.strip_edges().to_lower();
	if (reply != "once" && reply != "always" && reply != "reject") {
		reply = "reject";
	}
	Dictionary payload;
	payload["response"] = reply;
	const LagoonicHttpResult r = _lagoonic_http_request(HTTPClient::METHOD_POST, _lagoonic_with_directory("/session/" + _lagoonic_encode_query(p_session_id) + "/permissions/" + _lagoonic_encode_query(p_permission_id), p_directory), JSON::stringify(payload));
	return r.ok;
}

bool LagoonicOpenCodeClient::lagoonic_abort(const String &p_session_id, const String &p_directory) {
	const LagoonicHttpResult r = _lagoonic_http_request(HTTPClient::METHOD_POST, _lagoonic_with_directory("/session/" + _lagoonic_encode_query(p_session_id) + "/abort", p_directory));
	return r.ok;
}

bool LagoonicOpenCodeClient::lagoonic_revert(const String &p_session_id, const String &p_directory, const String &p_message_id) {
	Dictionary payload;
	payload["messageID"] = p_message_id;
	const LagoonicHttpResult r = _lagoonic_http_request(HTTPClient::METHOD_POST, _lagoonic_with_directory("/session/" + _lagoonic_encode_query(p_session_id) + "/revert", p_directory), JSON::stringify(payload));
	return r.ok;
}

Array LagoonicOpenCodeClient::lagoonic_diff_full(const String &p_session_id, const String &p_directory) {
	Array out;
	const LagoonicHttpResult r = _lagoonic_http_request(HTTPClient::METHOD_GET, _lagoonic_with_directory("/session/" + _lagoonic_encode_query(p_session_id) + "/diff", p_directory));
	if (!r.ok) {
		return out;
	}
	const Variant parsed = JSON::parse_string(r.body);
	if (parsed.get_type() != Variant::ARRAY) {
		return out;
	}
	for (int i = 0; i < Array(parsed).size(); i++) {
		const Variant entry = Array(parsed)[i];
		if (entry.get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary file = entry;
		String path = _lagoonic_dict_string(file, "file");
		if (path.is_empty()) {
			path = _lagoonic_dict_string(file, "path");
		}
		if (path.is_empty()) {
			continue;
		}
		Dictionary row;
		row["path"] = path;
		Variant add_v = file.get("additions", Variant());
		Variant del_v = file.get("deletions", Variant());
		row["additions"] = (add_v.get_type() == Variant::INT || add_v.get_type() == Variant::FLOAT) ? int64_t(add_v) : int64_t(0);
		row["deletions"] = (del_v.get_type() == Variant::INT || del_v.get_type() == Variant::FLOAT) ? int64_t(del_v) : int64_t(0);
		String status = _lagoonic_dict_string(file, "status");
		row["status"] = (status == "added" || status == "deleted" || status == "modified") ? status : String("modified");
		row["patch"] = _lagoonic_dict_string(file, "patch");
		out.push_back(row);
	}
	return out;
}

Array LagoonicOpenCodeClient::lagoonic_diff(const String &p_session_id, const String &p_directory) {
	const Array full = lagoonic_diff_full(p_session_id, p_directory);
	Array out;
	for (int i = 0; i < full.size(); i++) {
		if (full[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary entry = full[i];
		Dictionary row;
		row["path"] = entry.get("path", Variant());
		row["additions"] = entry.get("additions", Variant());
		row["deletions"] = entry.get("deletions", Variant());
		out.push_back(row);
	}
	return out;
}

bool LagoonicOpenCodeClient::lagoonic_delete_session(const String &p_directory, const String &p_session_id) {
	const LagoonicHttpResult r = _lagoonic_http_request(HTTPClient::METHOD_DELETE, _lagoonic_with_directory("/session/" + _lagoonic_encode_query(p_session_id), p_directory));
	return r.ok;
}

bool LagoonicOpenCodeClient::lagoonic_is_not_found_error(const String &p_message) const {
	if (p_message.contains("404")) {
		return true;
	}
	return p_message.to_lower().contains("not found");
}

// ----------------------------------------------------------------------- SSE

void LagoonicOpenCodeClient::_lagoonic_sse_thread_entry(void *p_userdata) {
	LagoonicOpenCodeClient *self = static_cast<LagoonicOpenCodeClient *>(p_userdata);
	if (self != nullptr) {
		self->_lagoonic_sse_loop();
	}
}

bool LagoonicOpenCodeClient::lagoonic_subscribe_events(const String &p_directory, const Callable &p_callback) {
	{
		MutexLock lock(lagoonic_sse_mutex);
		if (lagoonic_sse_active) {
			return false;
		}
		lagoonic_sse_stop = false;
		lagoonic_sse_active = true;
		lagoonic_sse_directory = p_directory;
		lagoonic_sse_callback = p_callback;
	}
	if (lagoonic_sse_thread.is_started()) {
		lagoonic_sse_thread.wait_to_finish();
	}
	const Thread::ID tid = lagoonic_sse_thread.start(_lagoonic_sse_thread_entry, this);
	if (tid == Thread::UNASSIGNED_ID) {
		MutexLock lock(lagoonic_sse_mutex);
		lagoonic_sse_active = false;
		return false;
	}
	return true;
}

void LagoonicOpenCodeClient::lagoonic_unsubscribe_events() {
	bool was_active = false;
	{
		MutexLock lock(lagoonic_sse_mutex);
		was_active = lagoonic_sse_active;
		lagoonic_sse_stop = true;
	}
	// Join without holding the mutex (the worker only takes it briefly).
	if (lagoonic_sse_thread.is_started()) {
		lagoonic_sse_thread.wait_to_finish();
	}
	{
		MutexLock lock(lagoonic_sse_mutex);
		lagoonic_sse_active = false;
		lagoonic_sse_callback = Callable();
	}
	(void)was_active;
}

bool LagoonicOpenCodeClient::lagoonic_is_subscribed() const {
	MutexLock lock(lagoonic_sse_mutex);
	return lagoonic_sse_active;
}

bool LagoonicOpenCodeClient::_lagoonic_should_stop() const {
	MutexLock lock(lagoonic_sse_mutex);
	return lagoonic_sse_stop;
}

void LagoonicOpenCodeClient::_lagoonic_sleep_interruptible(int64_t p_ms, const LagoonicOpenCodeClient *p_self) {
	int64_t waited = 0;
	while (waited < p_ms && !p_self->_lagoonic_should_stop()) {
		OS::get_singleton()->delay_usec(50000);
		waited += 50;
	}
}

// Persistent reader: GET /event?directory=... with HTTPClient on this worker
// thread. Chunks are split into lines; `data: {...}` payloads become frames.
// The only cross-thread call is call_deferred() into _lagoonic_emit_frame().
void LagoonicOpenCodeClient::_lagoonic_sse_loop() {
	String directory;
	{
		MutexLock lock(lagoonic_sse_mutex);
		directory = lagoonic_sse_directory;
	}

	String host;
	int port = 80;
	bool use_tls = false;
	String prefix;
	_lagoonic_split_base_url(lagoonic_base_url, host, port, use_tls, prefix);
	const String target = prefix + _lagoonic_with_directory("/event", directory);

	while (!_lagoonic_should_stop()) {
		Ref<HTTPClient> cli(HTTPClient::create());
		if (cli.is_null()) {
			_lagoonic_sleep_interruptible(LAGOONIC_SSE_RETRY_MS, this);
			continue;
		}

		Ref<TLSOptions> sse_tls_options;
		if (use_tls) {
			sse_tls_options = TLSOptions::client();
		}
		bool connected = false;
		if (cli->connect_to_host(host, port, sse_tls_options) == OK) {
			const uint64_t t0 = Time::get_singleton()->get_ticks_msec();
			while (cli->get_status() == HTTPClient::STATUS_CONNECTING || cli->get_status() == HTTPClient::STATUS_RESOLVING) {
				cli->poll();
				if (Time::get_singleton()->get_ticks_msec() - t0 > 15000) {
					break;
				}
				OS::get_singleton()->delay_usec(5000);
			}
			if (cli->get_status() == HTTPClient::STATUS_CONNECTED) {
				Vector<String> headers;
				headers.push_back(_lagoonic_auth_header());
				headers.push_back("Accept: text/event-stream");
				if (cli->request(HTTPClient::METHOD_GET, target, headers, nullptr, 0) == OK) {
					uint64_t t1 = Time::get_singleton()->get_ticks_msec();
					while (cli->get_status() == HTTPClient::STATUS_REQUESTING && !_lagoonic_should_stop()) {
						cli->poll();
						if (Time::get_singleton()->get_ticks_msec() - t1 > 15000) {
							break;
						}
						OS::get_singleton()->delay_usec(5000);
					}
					connected = cli->has_response() && cli->get_response_code() >= 200 && cli->get_response_code() <= 299;
				}
			}
		}

		if (connected) {
			// Stream frames until the server closes the connection or an
			// unsubscribe is requested. Work in raw bytes and only decode
			// complete lines so a UTF-8 sequence split across two chunks
			// can never corrupt a frame.
			PackedByteArray pending;
			Array data_lines;
			while (!_lagoonic_should_stop() && (cli->get_status() == HTTPClient::STATUS_BODY || cli->get_status() == HTTPClient::STATUS_CONNECTED)) {
				cli->poll();
				const PackedByteArray chunk = cli->read_response_body_chunk();
				if (chunk.size() == 0) {
					if (cli->get_status() != HTTPClient::STATUS_BODY && cli->get_status() != HTTPClient::STATUS_CONNECTED) {
						break;
					}
					OS::get_singleton()->delay_usec(5000);
					continue;
				}
				pending.append_array(chunk);
				// Find the last complete line in the byte buffer.
				int last_nl = -1;
				for (int i = pending.size() - 1; i >= 0; i--) {
					if (pending[i] == '\n') {
						last_nl = i;
						break;
					}
				}
				if (last_nl < 0) {
					continue;
				}
				const PackedByteArray head = pending.slice(0, last_nl + 1);
				pending = pending.slice(last_nl + 1);
				const PackedStringArray lines = (head.is_empty() ? String() : String::utf8((const char *)head.ptr(), head.size())).split("\n");
				for (int li = 0; li < lines.size(); li++) {
					String line = lines[li];
					if (line.ends_with("\r")) {
						line = line.substr(0, line.length() - 1);
					}
					if (line.begins_with("data:")) {
						data_lines.push_back(line.substr(5).strip_edges());
					} else if (line.strip_edges().is_empty()) {
						String payload;
						for (int di = 0; di < data_lines.size(); di++) {
							if (di > 0) {
								payload += "\n";
							}
							payload += String(data_lines[di]);
						}
						data_lines.clear();
						payload = payload.strip_edges();
						if (!payload.is_empty()) {
							const Variant parsed = JSON::parse_string(payload);
							if (parsed.get_type() == Variant::DICTIONARY) {
								const Dictionary root = parsed;
								if (Variant(root.get("type", Variant())).get_type() == Variant::STRING) {
									Dictionary frame;
									frame["id"] = _lagoonic_dict_string(root, "id");
									frame["type"] = String(root.get("type", Variant()));
									Variant props = root.get("properties", Variant());
									frame["properties"] = (props.get_type() == Variant::DICTIONARY) ? Dictionary(props) : Dictionary();
									call_deferred(StringName("_lagoonic_emit_frame"), frame);
								}
							}
							// One malformed frame never kills the stream.
						}
					}
					// Lines starting with ':' are SSE keep-alive comments.
				}
			}
		}

		cli->close();
		if (_lagoonic_should_stop()) {
			break;
		}
		// The supervisor restarts a dead server; keep retrying while
		// subscribed instead of dropping the subscription.
		_lagoonic_sleep_interruptible(LAGOONIC_SSE_RETRY_MS, this);
	}

	call_deferred(StringName("_lagoonic_emit_closed"), String("unsubscribed"));
}

void LagoonicOpenCodeClient::_lagoonic_emit_frame(const Dictionary &p_frame) {
	emit_signal(StringName("lagoonic_sse_frame"), p_frame);
	Callable callback;
	{
		MutexLock lock(lagoonic_sse_mutex);
		callback = lagoonic_sse_callback;
	}
	if (callback.is_valid()) {
		Variant frame_var = p_frame;
		Array call_args;
		call_args.push_back(frame_var);
		// callv (no el overload virtual de bajo nivel): el template
		// Callable::call(VarArgs...) haría la resolución ambigua con
		// CallError. callv reporta el error en el engine y no rompe el stream.
		callback.callv(call_args);
		// A failing dock callback must not break the stream; the error is
		// reported by the engine itself. No frame contents are logged here.
	}
}

void LagoonicOpenCodeClient::_lagoonic_emit_closed(const String &p_reason) {
	emit_signal(StringName("lagoonic_sse_closed"), p_reason);
}
