/**************************************************************************/
/*  lagoonic_opencode_v2.cpp                                              */
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

#include "lagoonic_opencode_v2.h"

#include "core/variant/variant.h"

static String _v2_str(const Dictionary &p_dict, const String &p_key) {
	const Variant v = p_dict.get(p_key, Variant());
	return v.get_type() == Variant::STRING ? String(v) : String();
}

static Dictionary _v2_dict(const Dictionary &p_dict, const String &p_key) {
	const Variant v = p_dict.get(p_key, Variant());
	return v.get_type() == Variant::DICTIONARY ? Dictionary(v) : Dictionary();
}

static Array _v2_array(const Dictionary &p_dict, const String &p_key) {
	const Variant v = p_dict.get(p_key, Variant());
	return v.get_type() == Variant::ARRAY ? Array(v) : Array();
}

static Dictionary _v2_frame(const String &p_id, const String &p_type, const Dictionary &p_properties) {
	Dictionary frame;
	frame["id"] = p_id;
	frame["type"] = p_type;
	frame["properties"] = p_properties;
	return frame;
}

static Dictionary _v2_status(const String &p_session_id, const String &p_status) {
	Dictionary status;
	status["type"] = p_status;
	Dictionary props;
	props["sessionID"] = p_session_id;
	props["status"] = status;
	return props;
}

static String _v2_text_part_id(const Dictionary &p_data) {
	const Variant ordinal = p_data.get("ordinal", Variant());
	const int64_t n = (ordinal.get_type() == Variant::INT || ordinal.get_type() == Variant::FLOAT) ? int64_t(ordinal) : 0;
	return _v2_str(p_data, "assistantMessageID") + ":text:" + String::num_int64(n);
}

static bool _v2_is_shell(const String &p_action) {
	return p_action == "shell" || p_action == "bash";
}

static Dictionary _v2_part(const String &p_session_id, const String &p_part_id, const String &p_message_id, const String &p_type) {
	Dictionary part;
	part["id"] = p_part_id;
	part["sessionID"] = p_session_id;
	part["messageID"] = p_message_id;
	part["type"] = p_type;
	return part;
}

static Dictionary _v2_tool_frame(const String &p_id, const String &p_session_id, const String &p_tool_id, const String &p_name, const String &p_message_id, const Dictionary &p_state) {
	Dictionary part = _v2_part(p_session_id, p_tool_id, p_message_id, "tool");
	part["tool"] = p_name;
	part["state"] = p_state;
	Dictionary props;
	props["sessionID"] = p_session_id;
	props["part"] = part;
	return _v2_frame(p_id, "message.part.updated", props);
}

String LagoonicV2FrameTranslator::_turn_for(const String &p_session_id, const String &p_assistant_message_id) {
	if (p_assistant_message_id.is_empty()) {
		return String();
	}
	if (message_turn.has(p_assistant_message_id)) {
		return message_turn[p_assistant_message_id];
	}
	if (!turns.has(p_session_id)) {
		// Subscribed in the middle of an execution: start folding from here.
		turns.insert(p_session_id, Turn());
	}
	Turn &turn = turns[p_session_id];
	if (turn.turn_id.is_empty()) {
		turn.turn_id = p_assistant_message_id;
	}
	message_turn.insert(p_assistant_message_id, turn.turn_id);
	return turn.turn_id;
}

LagoonicV2FrameTranslator::ToolCall *LagoonicV2FrameTranslator::_tool_call(const String &p_session_id, const Dictionary &p_data) {
	const String tool_id = _v2_str(p_data, "id");
	if (tool_id.is_empty()) {
		return nullptr;
	}
	if (tools.has(tool_id)) {
		return &tools[tool_id];
	}
	const String turn_id = _turn_for(p_session_id, _v2_str(p_data, "assistantMessageID"));
	if (turn_id.is_empty()) {
		return nullptr;
	}
	ToolCall call;
	call.name = _v2_str(p_data, "name");
	if (call.name.is_empty()) {
		call.name = "tool";
	}
	call.input = _v2_dict(p_data, "input");
	call.message_id = turn_id;
	tools.insert(tool_id, call);
	return &tools[tool_id];
}

void LagoonicV2FrameTranslator::_mark_text(const String &p_session_id) {
	if (turns.has(p_session_id)) {
		turns[p_session_id].has_text = true;
	}
}

void LagoonicV2FrameTranslator::_end_turn(const String &p_session_id) {
	if (!turns.has(p_session_id)) {
		return;
	}
	const String turn_id = turns[p_session_id].turn_id;
	turns.erase(p_session_id);
	if (turn_id.is_empty()) {
		return;
	}
	Vector<String> drop;
	for (const KeyValue<String, String> &kv : message_turn) {
		if (kv.value == turn_id) {
			drop.push_back(kv.key);
		}
	}
	for (const String &key : drop) {
		message_turn.erase(key);
	}
	drop.clear();
	for (const KeyValue<String, ToolCall> &kv : tools) {
		if (kv.value.message_id == turn_id) {
			drop.push_back(kv.key);
		}
	}
	for (const String &key : drop) {
		tools.erase(key);
	}
}

Array LagoonicV2FrameTranslator::translate(const Dictionary &p_envelope) {
	Array out;
	const String type = _v2_str(p_envelope, "type");
	const String id = _v2_str(p_envelope, "id");
	Dictionary data = _v2_dict(p_envelope, "data");
	if (data.is_empty()) {
		data = _v2_dict(p_envelope, "properties");
	}
	const String sid = _v2_str(data, "sessionID");

	if (type == "server.connected") {
		out.push_back(_v2_frame(id, "server.connected", Dictionary()));
	} else if (type == "session.execution.started") {
		turns.insert(sid, Turn());
		out.push_back(_v2_frame(id, "session.status", _v2_status(sid, "busy")));
	} else if (type == "session.retry.scheduled") {
		out.push_back(_v2_frame(id, "session.status", _v2_status(sid, "retry")));
	} else if (type == "session.execution.succeeded" || type == "session.execution.interrupted") {
		_end_turn(sid);
		out.push_back(_v2_frame(id, "session.status", _v2_status(sid, "idle")));
	} else if (type == "session.execution.failed") {
		_end_turn(sid);
		const Dictionary error = _v2_dict(data, "error");
		Dictionary error_data;
		error_data["message"] = _v2_str(error, "message");
		Dictionary mapped;
		const String name = _v2_str(error, "type");
		mapped["name"] = name.is_empty() ? String("Error") : name;
		mapped["data"] = error_data;
		Dictionary props;
		props["sessionID"] = sid;
		props["error"] = mapped;
		out.push_back(_v2_frame(id, "session.error", props));
		out.push_back(_v2_frame(id, "session.status", _v2_status(sid, "idle")));
	} else if (type == "session.step.started") {
		const String turn_id = _turn_for(sid, _v2_str(data, "assistantMessageID"));
		if (!turn_id.is_empty()) {
			Dictionary info;
			info["id"] = turn_id;
			info["sessionID"] = sid;
			info["role"] = "assistant";
			Dictionary props;
			props["sessionID"] = sid;
			props["info"] = info;
			out.push_back(_v2_frame(id, "message.updated", props));
		}
	} else if (type == "session.text.started") {
		const String turn_id = _turn_for(sid, _v2_str(data, "assistantMessageID"));
		if (!turn_id.is_empty()) {
			const String part_id = _v2_text_part_id(data);
			TextBlock block;
			block.prefix = (turns.has(sid) && turns[sid].has_text) ? String("\n\n") : String();
			texts.insert(part_id, block);
			Dictionary part = _v2_part(sid, part_id, turn_id, "text");
			part["text"] = String();
			Dictionary props;
			props["sessionID"] = sid;
			props["part"] = part;
			out.push_back(_v2_frame(id, "message.part.updated", props));
		}
	} else if (type == "session.text.delta") {
		const String turn_id = _turn_for(sid, _v2_str(data, "assistantMessageID"));
		const String delta = _v2_str(data, "delta");
		if (!turn_id.is_empty() && !delta.is_empty()) {
			const String part_id = _v2_text_part_id(data);
			if (!texts.has(part_id)) {
				texts.insert(part_id, TextBlock());
			}
			TextBlock &block = texts[part_id];
			const String chunk = block.sent.is_empty() ? block.prefix + delta : delta;
			block.sent += chunk;
			_mark_text(sid);
			Dictionary props;
			props["sessionID"] = sid;
			props["messageID"] = turn_id;
			props["partID"] = part_id;
			props["field"] = "text";
			props["delta"] = chunk;
			out.push_back(_v2_frame(id, "message.part.delta", props));
		}
	} else if (type == "session.text.ended") {
		const String turn_id = _turn_for(sid, _v2_str(data, "assistantMessageID"));
		const String text = _v2_str(data, "text");
		if (!turn_id.is_empty() && !text.is_empty()) {
			const String part_id = _v2_text_part_id(data);
			const String prefix = texts.has(part_id) ? texts[part_id].prefix : String();
			texts.erase(part_id);
			_mark_text(sid);
			Dictionary part = _v2_part(sid, part_id, turn_id, "text");
			part["text"] = prefix + text;
			Dictionary props;
			props["sessionID"] = sid;
			props["part"] = part;
			out.push_back(_v2_frame(id, "message.part.updated", props));
		}
	} else if (type == "session.tool.input.started") {
		const String turn_id = _turn_for(sid, _v2_str(data, "assistantMessageID"));
		const String tool_id = _v2_str(data, "id");
		if (!turn_id.is_empty() && !tool_id.is_empty()) {
			ToolCall call;
			call.name = _v2_str(data, "name");
			call.message_id = turn_id;
			tools.insert(tool_id, call);
			Dictionary state;
			state["status"] = "pending";
			state["input"] = Dictionary();
			out.push_back(_v2_tool_frame(id, sid, tool_id, call.name, turn_id, state));
		}
	} else if (type == "session.tool.called") {
		ToolCall *call = _tool_call(sid, data);
		if (call != nullptr) {
			const Dictionary input = _v2_dict(data, "input");
			if (!input.is_empty()) {
				call->input = input;
			}
			Dictionary state;
			state["status"] = "running";
			state["input"] = call->input;
			out.push_back(_v2_tool_frame(id, sid, _v2_str(data, "id"), call->name, call->message_id, state));
		}
	} else if (type == "session.tool.success" || type == "session.tool.failed") {
		ToolCall *call = _tool_call(sid, data);
		if (call != nullptr) {
			Dictionary state;
			state["input"] = call->input;
			if (type == "session.tool.success") {
				state["status"] = "completed";
				const String title = _v2_str(_v2_dict(data, "metadata"), "title");
				if (!title.is_empty()) {
					state["title"] = title;
				}
			} else {
				state["status"] = "error";
				const String message = _v2_str(_v2_dict(data, "error"), "message");
				state["error"] = message.is_empty() ? String("tool failed") : message;
			}
			const String name = call->name;
			const String message_id = call->message_id;
			const String tool_id = _v2_str(data, "id");
			tools.erase(tool_id);
			out.push_back(_v2_tool_frame(id, sid, tool_id, name, message_id, state));
		}
	} else if (type == "permission.asked") {
		const String action = _v2_str(data, "action");
		const Array resources = _v2_array(data, "resources");
		Dictionary metadata = _v2_dict(data, "metadata").duplicate();
		const String first = resources.is_empty() ? String() : String(resources[0]);
		if (_v2_str(metadata, "command").is_empty() && _v2_is_shell(action) && !first.is_empty()) {
			metadata["command"] = first;
		}
		if (_v2_str(metadata, "description").is_empty() && !_v2_str(data, "message").is_empty()) {
			metadata["description"] = _v2_str(data, "message");
		}
		Dictionary props;
		props["id"] = _v2_str(data, "id");
		props["sessionID"] = sid;
		props["permission"] = _v2_is_shell(action) ? String("bash") : action;
		props["patterns"] = resources;
		props["metadata"] = metadata;
		out.push_back(_v2_frame(id, "permission.asked", props));
	} else if (type == "permission.replied") {
		Dictionary props;
		props["sessionID"] = sid;
		props["requestID"] = _v2_str(data, "requestID");
		props["reply"] = _v2_str(data, "reply");
		out.push_back(_v2_frame(id, "permission.replied", props));
	}
	// Reasoning, usage, inbox and instruction events have no 1.x counterpart.
	return out;
}

static Dictionary _v2_row(const String &p_id, const String &p_session_id, const String &p_message_id, const String &p_type) {
	Dictionary row;
	row["id"] = p_id;
	row["session_id"] = p_session_id;
	row["message_id"] = p_message_id;
	row["type"] = p_type;
	return row;
}

Array lagoonic_v2_fold_messages(const String &p_session_id, const Array &p_raw) {
	Array out;
	Dictionary turn;
	Array turn_parts;
	bool in_turn = false;
	bool turn_has_text = false;
	auto close_turn = [&]() {
		if (in_turn) {
			turn["parts"] = turn_parts;
			out.push_back(turn);
		}
		in_turn = false;
		turn = Dictionary();
		turn_parts = Array();
	};
	for (int i = 0; i < p_raw.size(); i++) {
		if (p_raw[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary message = p_raw[i];
		const String id = _v2_str(message, "id");
		if (id.is_empty()) {
			continue;
		}
		const String type = _v2_str(message, "type");
		const Variant created_v = _v2_dict(message, "time").get("created", Variant());
		const int64_t created = (created_v.get_type() == Variant::INT || created_v.get_type() == Variant::FLOAT) ? int64_t(created_v) : 0;
		if (type == "user") {
			close_turn();
			String text = _v2_str(message, "text");
			if (text.is_empty()) {
				text = _v2_str(_v2_dict(message, "payload"), "text");
			}
			Dictionary msg;
			msg["id"] = id;
			msg["session_id"] = p_session_id;
			msg["role"] = "user";
			msg["time"] = created;
			Array parts;
			if (!text.is_empty()) {
				Dictionary row = _v2_row(id + ":text", p_session_id, id, "text");
				row["text"] = text;
				parts.push_back(row);
			}
			msg["parts"] = parts;
			out.push_back(msg);
			continue;
		}
		if (type != "assistant") {
			close_turn();
			continue;
		}
		if (!in_turn) {
			in_turn = true;
			turn_has_text = false;
			turn["id"] = id;
			turn["session_id"] = p_session_id;
			turn["role"] = "assistant";
			turn["time"] = created;
		}
		const String turn_id = turn["id"];
		const Array content = _v2_array(message, "content");
		for (int j = 0; j < content.size(); j++) {
			if (content[j].get_type() != Variant::DICTIONARY) {
				continue;
			}
			const Dictionary part = content[j];
			const String part_type = _v2_str(part, "type");
			if (part_type == "text" && !_v2_str(part, "text").is_empty()) {
				Dictionary row = _v2_row(id + ":text:" + itos(j), p_session_id, turn_id, "text");
				row["text"] = (turn_has_text ? String("\n\n") : String()) + _v2_str(part, "text");
				turn_has_text = true;
				turn_parts.push_back(row);
			} else if (part_type == "tool") {
				const Dictionary state = _v2_dict(part, "state");
				String status = _v2_str(state, "status");
				if (status == "failed") {
					status = "error";
				} else if (status.is_empty()) {
					status = "completed";
				}
				Dictionary mapped;
				mapped["status"] = status;
				mapped["input"] = _v2_dict(state, "input");
				const String title = _v2_str(_v2_dict(state, "metadata"), "title");
				if (!title.is_empty()) {
					mapped["title"] = title;
				}
				const Variant error = state.get("error", Variant());
				if (error.get_type() == Variant::STRING) {
					mapped["error"] = error;
				} else if (error.get_type() == Variant::DICTIONARY) {
					const String message_text = _v2_str(Dictionary(error), "message");
					mapped["error"] = message_text.is_empty() ? String("tool failed") : message_text;
				}
				String part_id = _v2_str(part, "id");
				if (part_id.is_empty()) {
					part_id = id + ":tool:" + itos(j);
				}
				Dictionary row = _v2_row(part_id, p_session_id, turn_id, "tool");
				String name = _v2_str(part, "name");
				if (name.is_empty()) {
					name = _v2_str(part, "tool");
				}
				row["tool"] = name.is_empty() ? String("tool") : name;
				row["state"] = mapped;
				turn_parts.push_back(row);
			}
		}
	}
	close_turn();
	return out;
}

Dictionary lagoonic_v2_prompt_body(const Array &p_parts) {
	String text;
	Array files;
	for (int i = 0; i < p_parts.size(); i++) {
		if (p_parts[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary part = p_parts[i];
		const String type = _v2_str(part, "type");
		if (type == "text" && !_v2_str(part, "text").is_empty()) {
			if (!text.is_empty()) {
				text += "\n\n";
			}
			text += _v2_str(part, "text");
		} else if (type == "file") {
			String uri = _v2_str(part, "url");
			if (uri.is_empty()) {
				uri = _v2_str(part, "uri");
			}
			if (uri.is_empty()) {
				continue;
			}
			Dictionary file;
			file["uri"] = uri;
			String name = _v2_str(part, "filename");
			if (name.is_empty()) {
				name = _v2_str(part, "name");
			}
			if (!name.is_empty()) {
				file["name"] = name;
			}
			files.push_back(file);
		}
	}
	Dictionary body;
	body["text"] = text;
	if (!files.is_empty()) {
		body["files"] = files;
	}
	return body;
}
