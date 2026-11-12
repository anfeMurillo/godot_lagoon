/**************************************************************************/
/*  lagoonic_event_mapper.cpp                                         */
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

#include "lagoonic_event_mapper.h"

#include "core/io/json.h"
#include "core/object/class_db.h"
#include "core/os/time.h"

// ---------------------------------------------------------------- helpers

static String _lagoonic_str(const Variant &p_v) {
	return (p_v.get_type() == Variant::STRING) ? String(p_v) : String();
}

static String _lagoonic_dict_str(const Dictionary &p_dict, const StringName &p_key) {
	if (!p_dict.has(p_key)) {
		return String();
	}
	return _lagoonic_str(p_dict.get(p_key, Variant()));
}

static Dictionary _lagoonic_dict_dict(const Dictionary &p_dict, const StringName &p_key) {
	if (!p_dict.has(p_key)) {
		return Dictionary();
	}
	const Variant v = p_dict.get(p_key, Variant());
	return (v.get_type() == Variant::DICTIONARY) ? Dictionary(v) : Dictionary();
}

// ---------------------------------------------------------------- lifecycle

LagoonicEventMapper::LagoonicEventMapper() {
}

void LagoonicEventMapper::_bind_methods() {
	ClassDB::bind_method(D_METHOD("lagoonic_configure", "session_id"), &LagoonicEventMapper::lagoonic_configure);
	ClassDB::bind_method(D_METHOD("lagoonic_set_session", "session_id"), &LagoonicEventMapper::lagoonic_set_session);
	ClassDB::bind_method(D_METHOD("lagoonic_get_session"), &LagoonicEventMapper::lagoonic_get_session);
	ClassDB::bind_method(D_METHOD("lagoonic_is_connected"), &LagoonicEventMapper::lagoonic_is_connected);

	ClassDB::bind_method(D_METHOD("lagoonic_feed", "frame"), &LagoonicEventMapper::lagoonic_feed);
	ClassDB::bind_method(D_METHOD("lagoonic_snapshot"), &LagoonicEventMapper::lagoonic_snapshot);
	ClassDB::bind_method(D_METHOD("lagoonic_reset"), &LagoonicEventMapper::lagoonic_reset);
	ClassDB::bind_method(D_METHOD("lagoonic_parse_frame", "payload"), &LagoonicEventMapper::lagoonic_parse_frame);

	ClassDB::bind_method(D_METHOD("lagoonic_display_tool_name", "raw_tool"), &LagoonicEventMapper::lagoonic_display_tool_name);
	ClassDB::bind_method(D_METHOD("lagoonic_extract_tool_detail", "state"), &LagoonicEventMapper::lagoonic_extract_tool_detail);
	ClassDB::bind_method(D_METHOD("lagoonic_tool_label", "raw_tool", "detail"), &LagoonicEventMapper::lagoonic_tool_label);
	ClassDB::bind_method(D_METHOD("lagoonic_is_hidden_tool"), &LagoonicEventMapper::lagoonic_is_hidden_tool);

	ClassDB::bind_method(D_METHOD("lagoonic_is_worker_session", "session_id"), &LagoonicEventMapper::lagoonic_is_worker_session);
	ClassDB::bind_method(D_METHOD("lagoonic_is_worker_frame", "frame"), &LagoonicEventMapper::lagoonic_is_worker_frame);
	ClassDB::bind_method(D_METHOD("lagoonic_get_dropped_worker_frames"), &LagoonicEventMapper::lagoonic_get_dropped_worker_frames);

	ClassDB::bind_method(D_METHOD("lagoonic_parse_unified_patch", "patch"), &LagoonicEventMapper::lagoonic_parse_unified_patch);
	ClassDB::bind_method(D_METHOD("lagoonic_snapshot_diff_to_file_diff", "entries"), &LagoonicEventMapper::lagoonic_snapshot_diff_to_file_diff);
}

void LagoonicEventMapper::lagoonic_configure(const String &p_session_id) {
	lagoonic_set_session(p_session_id);
}

void LagoonicEventMapper::lagoonic_set_session(const String &p_session_id) {
	if (p_session_id != lagoonic_session_id) {
		lagoonic_session_id = p_session_id;
		lagoonic_reset();
	}
}

String LagoonicEventMapper::lagoonic_get_session() const {
	return lagoonic_session_id;
}

bool LagoonicEventMapper::lagoonic_is_connected() const {
	return lagoonic_connected;
}

void LagoonicEventMapper::lagoonic_reset() {
	lagoonic_connected = false;
	lagoonic_saw_activity = false;
	lagoonic_dropped_worker_frames = 0;
	lagoonic_assistant_messages.clear();
	lagoonic_user_messages.clear();
	lagoonic_part_types.clear();
	lagoonic_part_messages.clear();
	lagoonic_emitted_text.clear();
	lagoonic_tool_status.clear();
	lagoonic_messages.clear();
	lagoonic_message_order.clear();
	lagoonic_tools_by_message.clear();
}

Dictionary LagoonicEventMapper::lagoonic_parse_frame(const String &p_payload) const {
	const Variant parsed = JSON::parse_string(p_payload);
	if (parsed.get_type() != Variant::DICTIONARY) {
		return Dictionary();
	}
	const Dictionary root = parsed;
	if (_lagoonic_str(root.get("type", Variant())).is_empty()) {
		return Dictionary(); // Malformed OpenCode event.
	}
	Dictionary frame;
	frame["id"] = _lagoonic_dict_str(root, "id");
	frame["type"] = _lagoonic_str(root.get("type", Variant()));
	const Variant props = root.get("properties", Variant());
	frame["properties"] = (props.get_type() == Variant::DICTIONARY) ? Dictionary(props) : Dictionary();
	return frame;
}

// -------------------------------------------------------------------- feed

Dictionary LagoonicEventMapper::_lagoonic_empty_result() const {
	Dictionary out;
	out["events"] = Array();
	out["turn_finished"] = false;
	out["failure"] = String();
	return out;
}

Dictionary LagoonicEventMapper::_lagoonic_message_event(const Dictionary &p_message) const {
	Dictionary event;
	event["type"] = String("message");
	event["session_id"] = lagoonic_session_id;
	// The dock keeps the whole message so far; a dropped event cannot
	// corrupt it (same contract as TS messageEvent()).
	Dictionary copy = p_message.duplicate(true);
	event["message"] = copy;
	return event;
}

Dictionary LagoonicEventMapper::_lagoonic_ensure_message(const String &p_id, const String &p_role) {
	if (lagoonic_messages.has(p_id)) {
		return lagoonic_messages[p_id];
	}
	Dictionary message;
	message["id"] = p_id;
	message["role"] = p_role;
	message["text"] = String();
	message["tools"] = Array();
	message["created_at"] = int64_t(Time::get_singleton()->get_unix_time_from_system());
	lagoonic_messages[p_id] = message;
	lagoonic_message_order.push_back(p_id);
	return message;
}

// Orden real de la respuesta: fragmentos de texto y pasos tal como llegan.
// La vista pone el texto seguido de un paso como narracion en la actividad
// y deja en la burbuja solo el texto final.
static void _lagoonic_timeline_add(Dictionary &r_message, const String &p_kind, const String &p_id, const String &p_text) {
	Array timeline = r_message.get("timeline", Array());
	for (int i = 0; i < timeline.size(); i++) {
		Dictionary entry = timeline[i];
		if (String(entry.get("id", "")) == p_id && String(entry.get("kind", "")) == p_kind) {
			if (!p_text.is_empty()) {
				entry["text"] = String(entry.get("text", "")) + p_text;
				timeline[i] = entry;
				r_message["timeline"] = timeline;
			}
			return;
		}
	}
	Dictionary entry;
	entry["kind"] = p_kind;
	entry["id"] = p_id;
	if (p_kind == "text") {
		entry["text"] = p_text;
	}
	timeline.push_back(entry);
	r_message["timeline"] = timeline;
}

bool LagoonicEventMapper::_lagoonic_is_this_session(const String &p_id) const {
	return p_id.is_empty() || p_id == lagoonic_session_id;
}

// Session routing for one frame: explicit properties.sessionID wins,
// falling back to part/info session ids like the TS handlers do.
String LagoonicEventMapper::_lagoonic_frame_session(const Dictionary &p_frame) {
	const Dictionary props = _lagoonic_dict_dict(p_frame, "properties");
	String sid = _lagoonic_dict_str(props, "sessionID");
	if (!sid.is_empty()) {
		return sid;
	}
	const Dictionary part = _lagoonic_dict_dict(props, "part");
	sid = _lagoonic_dict_str(part, "sessionID");
	if (!sid.is_empty()) {
		return sid;
	}
	const Dictionary info = _lagoonic_dict_dict(props, "info");
	return _lagoonic_dict_str(info, "sessionID");
}

Dictionary LagoonicEventMapper::lagoonic_feed(const Dictionary &p_frame) {
	Dictionary out = _lagoonic_empty_result();
	Array events = out["events"];

	const String type = _lagoonic_dict_str(p_frame, "type");
	if (type.is_empty()) {
		return out;
	}
	const Dictionary props = _lagoonic_dict_dict(p_frame, "properties");

	if (type == "server.connected") {
		lagoonic_connected = true;
		return out;
	}

	// Separate foreign sessions (crew workers) from the main chat before
	// any handler runs. server.connected carries no session and always
	// passes (handled above).
	const String frame_sid = _lagoonic_frame_session(p_frame);
	if (!frame_sid.is_empty() && frame_sid != lagoonic_session_id) {
		if (lagoonic_is_worker_session(frame_sid)) {
			lagoonic_dropped_worker_frames++;
		}
		return out;
	}

	if (type == "message.updated") {
		return _lagoonic_on_message_updated(props);
	} else if (type == "message.part.updated") {
		return _lagoonic_on_part_updated(props);
	} else if (type == "message.part.delta") {
		return _lagoonic_on_part_delta(props);
	} else if (type == "session.status") {
		if (!_lagoonic_is_this_session(_lagoonic_dict_str(props, "sessionID"))) {
			return out;
		}
		const Dictionary status = _lagoonic_dict_dict(props, "status");
		const String state = _lagoonic_dict_str(status, "type");
		if (state == "busy" || state == "retry") {
			lagoonic_saw_activity = true;
			Dictionary event;
			event["type"] = String("busy");
			event["session_id"] = lagoonic_session_id;
			event["busy"] = true;
			events.push_back(event);
		} else if (state == "idle") {
			if (lagoonic_saw_activity) {
				Dictionary event;
				event["type"] = String("busy");
				event["session_id"] = lagoonic_session_id;
				event["busy"] = false;
				events.push_back(event);
			}
			out["events"] = events;
			out["turn_finished"] = lagoonic_saw_activity;
			return out;
		}
		out["events"] = events;
		return out;
	} else if (type == "session.idle") {
		if (!_lagoonic_is_this_session(_lagoonic_dict_str(props, "sessionID"))) {
			return out;
		}
		if (lagoonic_saw_activity) {
			Dictionary event;
			event["type"] = String("busy");
			event["session_id"] = lagoonic_session_id;
			event["busy"] = false;
			events.push_back(event);
		}
		out["events"] = events;
		out["turn_finished"] = lagoonic_saw_activity;
		return out;
	} else if (type == "session.error") {
		const String at = _lagoonic_dict_str(props, "sessionID");
		if (!at.is_empty() && at != lagoonic_session_id) {
			return out;
		}
		const Dictionary error = _lagoonic_dict_dict(props, "error");
		if (error.is_empty()) {
			return out;
		}
		const String message = _lagoonic_error_message(error);
		Dictionary event;
		event["type"] = String("error");
		event["session_id"] = lagoonic_session_id;
		event["message"] = message;
		events.push_back(event);
		out["events"] = events;
		out["turn_finished"] = true;
		out["failure"] = message;
		return out;
	} else if (type == "todo.updated") {
		if (!_lagoonic_is_this_session(_lagoonic_dict_str(props, "sessionID"))) {
			return out;
		}
		const Variant todos_v = props.get("todos", Variant());
		if (todos_v.get_type() != Variant::ARRAY) {
			return out;
		}
		Array items;
		const Array todos = todos_v;
		for (int i = 0; i < todos.size(); i++) {
			if (todos[i].get_type() != Variant::DICTIONARY) {
				continue;
			}
			const Dictionary todo = todos[i];
			const String text = _lagoonic_dict_str(todo, "content").strip_edges();
			if (text.is_empty()) {
				continue;
			}
			Dictionary item;
			item["id"] = String::num_int64(items.size());
			item["text"] = text.left(160);
			item["state"] = _lagoonic_todo_state(_lagoonic_dict_str(todo, "status"));
			items.push_back(item);
		}
		lagoonic_saw_activity = true;
		Dictionary event;
		event["type"] = String("todos");
		event["session_id"] = lagoonic_session_id;
		event["todos"] = items;
		events.push_back(event);
		out["events"] = events;
		return out;
	} else if (type == "permission.asked") {
		return _lagoonic_on_permission_asked(props);
	} else if (type == "permission.replied") {
		return out;
	}
	return out;
}

Array LagoonicEventMapper::lagoonic_snapshot() const {
	Array out;
	for (int i = 0; i < lagoonic_message_order.size(); i++) {
		const Variant key = lagoonic_message_order[i];
		if (lagoonic_messages.has(key)) {
			out.push_back(Dictionary(lagoonic_messages[key]).duplicate(true));
		}
	}
	return out;
}

Dictionary LagoonicEventMapper::_lagoonic_on_message_updated(const Dictionary &p_props) {
	Dictionary out = _lagoonic_empty_result();
	const Dictionary info = _lagoonic_dict_dict(p_props, "info");
	if (info.is_empty()) {
		return out;
	}
	const String session = _lagoonic_dict_str(info, "sessionID");
	if (!session.is_empty() && !_lagoonic_is_this_session(session)) {
		const String fallback = _lagoonic_dict_str(p_props, "sessionID");
		if (!fallback.is_empty() && !_lagoonic_is_this_session(fallback)) {
			return out;
		}
	}
	const String id = _lagoonic_dict_str(info, "id");
	if (id.is_empty()) {
		return out;
	}
	const String role = _lagoonic_dict_str(info, "role");
	if (role == "assistant") {
		lagoonic_assistant_messages[id] = true;
		lagoonic_saw_activity = true;
		_lagoonic_ensure_message(id, "assistant");
	} else if (role == "user") {
		lagoonic_user_messages[id] = true;
	}
	return out;
}

Dictionary LagoonicEventMapper::_lagoonic_on_part_updated(const Dictionary &p_props) {
	Dictionary out = _lagoonic_empty_result();
	Array events = out["events"];
	const Dictionary part = _lagoonic_dict_dict(p_props, "part");
	if (part.is_empty()) {
		return out;
	}
	if (!_lagoonic_is_this_session(_lagoonic_dict_str(part, "sessionID")) ||
			!_lagoonic_is_this_session(_lagoonic_dict_str(p_props, "sessionID"))) {
		return out;
	}
	const String part_id = _lagoonic_dict_str(part, "id");
	const String message_id = _lagoonic_dict_str(part, "messageID");
	const String part_type = _lagoonic_dict_str(part, "type");
	if (!part_id.is_empty()) {
		lagoonic_part_types[part_id] = part_type;
		if (!message_id.is_empty()) {
			lagoonic_part_messages[part_id] = message_id;
		}
	}
	if (!message_id.is_empty() && lagoonic_user_messages.has(message_id)) {
		return out; // Never show user parts.
	}
	if (part_type == "text") {
		const String full = _lagoonic_dict_str(part, "text");
		if (full.is_empty() || message_id.is_empty()) {
			return out;
		}
		const int64_t already = lagoonic_emitted_text.has(part_id) ? int64_t(lagoonic_emitted_text[part_id]) : 0;
		if (full.length() <= already) {
			return out;
		}
		lagoonic_emitted_text[part_id] = int64_t(full.length());
		lagoonic_saw_activity = true;
		Dictionary message = _lagoonic_ensure_message(message_id, "assistant");
		message["text"] = String(message["text"]) + full.substr(already);
		_lagoonic_timeline_add(message, "text", part_id, full.substr(already));
		lagoonic_messages[message_id] = message;
		events.push_back(_lagoonic_message_event(message));
		out["events"] = events;
		return out;
	} else if (part_type == "reasoning") {
		if (!_lagoonic_dict_str(part, "text").strip_edges().is_empty()) {
			lagoonic_saw_activity = true;
		}
		return out;
	} else if (part_type == "tool") {
		if (message_id.is_empty()) {
			return out;
		}
		return _lagoonic_on_tool_part(part_id, part, message_id);
	} else if (part_type == "step-start" || part_type == "step_start") {
		lagoonic_saw_activity = true;
		return out;
	}
	return out;
}

Dictionary LagoonicEventMapper::_lagoonic_on_part_delta(const Dictionary &p_props) {
	Dictionary out = _lagoonic_empty_result();
	Array events = out["events"];
	if (!_lagoonic_is_this_session(_lagoonic_dict_str(p_props, "sessionID"))) {
		return out;
	}
	const String part_id = _lagoonic_dict_str(p_props, "partID");
	String message_id = _lagoonic_dict_str(p_props, "messageID");
	if (message_id.is_empty() && lagoonic_part_messages.has(part_id)) {
		message_id = String(lagoonic_part_messages[part_id]);
	}
	if (!message_id.is_empty() && lagoonic_user_messages.has(message_id)) {
		return out;
	}
	const String field = _lagoonic_dict_str(p_props, "field");
	if (!field.is_empty() && field != "text") {
		return out;
	}
	String text;
	const Variant delta = p_props.get("delta", Variant());
	if (delta.get_type() == Variant::STRING) {
		text = String(delta);
	} else if (delta.get_type() == Variant::DICTIONARY) {
		text = _lagoonic_dict_str(Dictionary(delta), "text");
	}
	if (text.is_empty() || message_id.is_empty()) {
		return out;
	}
	if (lagoonic_part_types.has(part_id) && String(lagoonic_part_types[part_id]) == "reasoning") {
		return out;
	}
	lagoonic_saw_activity = true;
	const int64_t previous = lagoonic_emitted_text.has(part_id) ? int64_t(lagoonic_emitted_text[part_id]) : 0;
	lagoonic_emitted_text[part_id] = previous + int64_t(text.length());
	Dictionary message = _lagoonic_ensure_message(message_id, "assistant");
	message["text"] = String(message["text"]) + text;
	_lagoonic_timeline_add(message, "text", part_id, text);
	lagoonic_messages[message_id] = message;
	events.push_back(_lagoonic_message_event(message));
	out["events"] = events;
	return out;
}

Dictionary LagoonicEventMapper::_lagoonic_on_tool_part(const String &p_part_id, const Dictionary &p_part, const String &p_message_id) {
	Dictionary out = _lagoonic_empty_result();
	Array events = out["events"];
	lagoonic_saw_activity = true;
	const String raw = _lagoonic_dict_str(p_part, "tool");
	// The plan card shows todowrite/todoread; a row for them would only
	// repeat the plan.
	if (lagoonic_is_hidden_tool(raw)) {
		return out;
	}
	const Dictionary state = _lagoonic_dict_dict(p_part, "state");
	const String status = _lagoonic_dict_str(state, "status");
	const String previous = lagoonic_tool_status.has(p_part_id) ? String(lagoonic_tool_status[p_part_id]) : String();
	if (previous == status) {
		return out;
	}
	lagoonic_tool_status[p_part_id] = status;
	const String title = _lagoonic_dict_str(state, "title");
	const String detail = _lagoonic_tool_detail(state);
	const String label = lagoonic_tool_label(raw, detail.is_empty() ? title : detail);
	// Pending parts are recorded (so the running update triggers) but emit
	// no row yet, mirroring the TS mapper.
	if (status == "pending") {
		return out;
	}
	Dictionary message = _lagoonic_ensure_message(p_message_id, "assistant");
	Array rows;
	if (lagoonic_tools_by_message.has(p_message_id)) {
		rows = lagoonic_tools_by_message[p_message_id];
	}
	const String row_state = (status == "completed") ? String("done") : (status == "error" ? String("error") : String("running"));
	bool found = false;
	for (int i = 0; i < rows.size(); i++) {
		Dictionary row = rows[i];
		if (_lagoonic_dict_str(row, "id") == p_part_id) {
			row["label"] = label;
			row["state"] = row_state;
			if (status == "error") {
				String err = _lagoonic_dict_str(state, "error");
				row["detail"] = ("Error: " + (err.is_empty() ? String("tool failed") : err)).left(240);
			} else {
				row["detail"] = detail.is_empty() ? title : detail;
			}
			rows[i] = row;
			found = true;
			break;
		}
	}
	if (!found) {
		Dictionary row;
		row["id"] = p_part_id;
		row["label"] = label;
		row["state"] = row_state;
		if (status == "running") {
			row["detail"] = detail.is_empty() ? title : detail;
		} else if (status == "error") {
			String err = _lagoonic_dict_str(state, "error");
			row["detail"] = ("Error: " + (err.is_empty() ? String("tool failed") : err)).left(240);
		} else {
			const String d = title.is_empty() ? detail : title;
			row["detail"] = d.is_empty() ? String("Done") : d;
		}
		rows.push_back(row);
		_lagoonic_timeline_add(message, "tool", p_part_id, String());
	}
	lagoonic_tools_by_message[p_message_id] = rows;
	message["tools"] = rows.duplicate(true);
	lagoonic_messages[p_message_id] = message;
	events.push_back(_lagoonic_message_event(message));
	out["events"] = events;
	return out;
}

Dictionary LagoonicEventMapper::_lagoonic_on_permission_asked(const Dictionary &p_props) {
	Dictionary out = _lagoonic_empty_result();
	Array events = out["events"];
	if (!_lagoonic_is_this_session(_lagoonic_dict_str(p_props, "sessionID"))) {
		return out;
	}
	const String id = _lagoonic_dict_str(p_props, "id");
	if (id.is_empty()) {
		return out;
	}
	const String permission = _lagoonic_dict_str(p_props, "permission");
	const String perm = permission.is_empty() ? String("tool") : permission;
	String pattern_first;
	const Variant patterns_v = p_props.get("patterns", Variant());
	if (patterns_v.get_type() == Variant::ARRAY && !Array(patterns_v).is_empty()) {
		pattern_first = _lagoonic_str(Array(patterns_v)[0]);
	}
	const Dictionary metadata = _lagoonic_dict_dict(p_props, "metadata");
	const String command = _lagoonic_dict_str(metadata, "command");
	const String description = _lagoonic_dict_str(metadata, "description");
	String detail = description.is_empty() ? command : description;
	if (detail.is_empty()) {
		detail = pattern_first;
	}
	String title = "Allow " + perm;
	if (!command.is_empty()) {
		title += String::utf8(" · ") + command.strip_edges().left(80);
	}
	title += "?";
	Dictionary request;
	request["id"] = id;
	request["session_id"] = lagoonic_session_id;
	request["title"] = title;
	if (!detail.is_empty()) {
		request["detail"] = detail;
	}
	Dictionary event;
	event["type"] = String("permission");
	event["request"] = request;
	events.push_back(event);
	out["events"] = events;
	return out;
}

// ------------------------------------------------------- tool narration

bool LagoonicEventMapper::lagoonic_is_hidden_tool(const String &p_raw_tool) const {
	const String name = p_raw_tool.strip_edges().to_lower();
	return name == "todowrite" || name == "todoread";
}

String LagoonicEventMapper::lagoonic_display_tool_name(const String &p_raw) const {
	if (p_raw.is_empty()) {
		return String("Tool");
	}
	if (p_raw.begins_with("lagoonic_")) {
		return "Godot: " + p_raw.substr(9).replace("_", " ");
	}
	const int under = p_raw.find("_");
	if (under > 0) {
		return _lagoonic_capitalize(p_raw.substr(0, under)) + ": " + p_raw.substr(under + 1).replace("_", " ");
	}
	return _lagoonic_capitalize(p_raw);
}

String LagoonicEventMapper::lagoonic_extract_tool_detail(const Dictionary &p_state) const {
	return _lagoonic_tool_detail(p_state);
}

String LagoonicEventMapper::_lagoonic_tool_detail(const Dictionary &p_state) {
	const Dictionary input = _lagoonic_dict_dict(p_state, "input");
	if (input.is_empty()) {
		return String();
	}
	static const char *s_keys[] = { "command", "description", "filePath", "path", "scenePath", "scene", "pattern", "query", "url", "name" };
	for (const char *key : s_keys) {
		const String value = _lagoonic_dict_str(input, StringName(key)).strip_edges();
		if (!value.is_empty()) {
			return value.left(240);
		}
	}
	// MCP tools (lagoonic-mcp) use their own argument names: show the first
	// short text argument.
	const Array keys = input.keys();
	for (int i = 0; i < keys.size(); i++) {
		const String value = _lagoonic_str(input.get(keys[i], Variant())).strip_edges();
		if (!value.is_empty() && value.length() <= 240) {
			return value;
		}
	}
	return String();
}

String LagoonicEventMapper::lagoonic_tool_label(const String &p_raw_tool, const String &p_detail) const {
	const String name = lagoonic_display_tool_name(p_raw_tool);
	// Una ruta se acorta al nombre del fichero; un comando o una frase se
	// muestran enteros (compactados), si no "2>/dev/null | grep" quedaria
	// como "null | grep".
	String detail = p_detail.strip_edges();
	const int nl = detail.find("\n");
	if (nl >= 0) {
		detail = detail.left(nl).strip_edges() + String::utf8(" …");
	}
	if (detail.contains(" ")) {
		while (detail.contains("  ")) {
			detail = detail.replace("  ", " ");
		}
		return detail.is_empty() ? name : (name + String::utf8(" · ") + (detail.length() > 90 ? detail.left(88) + String::utf8("…") : detail));
	}
	const String subject = _lagoonic_short_name(detail);
	return subject.is_empty() ? name : (name + String::utf8(" · ") + subject);
}

// `res://scenes/player.tscn` -> `player.tscn` (max 60 chars).
String LagoonicEventMapper::_lagoonic_short_name(const String &p_path) {
	const String trimmed = p_path.strip_edges().trim_suffix("/");
	if (trimmed.is_empty() || trimmed.contains("\n") || trimmed.length() > 200) {
		return String();
	}
	const PackedStringArray slash_parts = trimmed.split("/");
	String last = slash_parts[slash_parts.size() - 1];
	const PackedStringArray colon_parts = last.split(":");
	last = colon_parts[colon_parts.size() - 1];
	return last.left(60);
}

String LagoonicEventMapper::_lagoonic_capitalize(const String &p_word) {
	if (p_word.is_empty()) {
		return p_word;
	}
	return p_word.substr(0, 1).to_upper() + p_word.substr(1);
}

String LagoonicEventMapper::_lagoonic_todo_state(const String &p_status) {
	const String s = p_status.strip_edges().to_lower();
	if (s == "in_progress") {
		return String("in_progress");
	}
	if (s == "completed") {
		return String("completed");
	}
	if (s == "cancelled" || s == "canceled") {
		return String("cancelled");
	}
	return String("pending");
}

String LagoonicEventMapper::_lagoonic_error_message(const Dictionary &p_error) {
	const Dictionary data = _lagoonic_dict_dict(p_error, "data");
	String message = _lagoonic_dict_str(data, "message");
	if (message.is_empty()) {
		message = _lagoonic_dict_str(p_error, "message");
	}
	if (message.is_empty()) {
		message = _lagoonic_dict_str(p_error, "name");
	}
	return message.is_empty() ? String("OpenCode reported an error") : message;
}

// ------------------------------------------------- worker waiter separation

bool LagoonicEventMapper::lagoonic_is_worker_session(const String &p_session_id) const {
	return p_session_id.begins_with(LAGOONIC_WORKER_PREFIX);
}

bool LagoonicEventMapper::lagoonic_is_worker_frame(const Dictionary &p_frame) const {
	const String sid = _lagoonic_frame_session(p_frame);
	return !sid.is_empty() && sid != lagoonic_session_id && lagoonic_is_worker_session(sid);
}

int64_t LagoonicEventMapper::lagoonic_get_dropped_worker_frames() const {
	return lagoonic_dropped_worker_frames;
}

// ------------------------------------------------------------------- diffs

Dictionary LagoonicEventMapper::lagoonic_parse_unified_patch(const String &p_patch) const {
	String before;
	String after;
	bool before_first = true;
	bool after_first = true;
	const PackedStringArray lines = p_patch.split("\n");
	for (int i = 0; i < lines.size(); i++) {
		const String line = lines[i];
		if (line.begins_with("--- ") || line.begins_with("+++ ") || line.begins_with("@@") ||
				line.begins_with("diff ") || line.begins_with("index ") || line.begins_with("Binary ")) {
			continue;
		}
		if (line == "\\ No newline at end of file") {
			continue;
		}
		if (line.is_empty()) {
			continue;
		}
		const char marker = line[0];
		const String content = line.substr(1);
		if (marker == ' ') {
			before += (before_first ? String() : String("\n")) + content;
			before_first = false;
			after += (after_first ? String() : String("\n")) + content;
			after_first = false;
		} else if (marker == '-') {
			before += (before_first ? String() : String("\n")) + content;
			before_first = false;
		} else if (marker == '+') {
			after += (after_first ? String() : String("\n")) + content;
			after_first = false;
		}
	}
	Dictionary out;
	out["before"] = before;
	out["after"] = after;
	return out;
}

Array LagoonicEventMapper::lagoonic_snapshot_diff_to_file_diff(const Array &p_entries) const {
	Array out;
	for (int i = 0; i < p_entries.size(); i++) {
		if (p_entries[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary entry = p_entries[i];
		String path = _lagoonic_dict_str(entry, "file");
		if (path.is_empty()) {
			path = _lagoonic_dict_str(entry, "path");
		}
		if (path.is_empty()) {
			continue;
		}
		String status = _lagoonic_dict_str(entry, "status");
		if (status != "added" && status != "deleted" && status != "modified") {
			status = "modified";
		}
		const String patch = _lagoonic_dict_str(entry, "patch");
		const bool binary = patch.is_empty();
		Dictionary diff;
		diff["path"] = path;
		diff["status"] = status;
		const Variant add_v = entry.get("additions", Variant());
		const Variant del_v = entry.get("deletions", Variant());
		diff["additions"] = (add_v.get_type() == Variant::INT || add_v.get_type() == Variant::FLOAT) ? int64_t(add_v) : int64_t(0);
		diff["deletions"] = (del_v.get_type() == Variant::INT || del_v.get_type() == Variant::FLOAT) ? int64_t(del_v) : int64_t(0);
		if (!binary) {
			const Dictionary sides = lagoonic_parse_unified_patch(patch);
			const String before = _lagoonic_dict_str(sides, "before");
			const String after = _lagoonic_dict_str(sides, "after");
			if (status != "added" && before.to_utf8_buffer().size() <= LAGOONIC_DIFF_SIDE_LIMIT) {
				diff["before"] = before;
			}
			if (status != "deleted" && after.to_utf8_buffer().size() <= LAGOONIC_DIFF_SIDE_LIMIT) {
				diff["after"] = after;
			}
		}
		out.push_back(diff);
	}
	return out;
}
