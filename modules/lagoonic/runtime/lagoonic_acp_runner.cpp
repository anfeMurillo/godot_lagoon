/**************************************************************************/
/*  lagoonic_acp_runner.cpp                                             */
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

#include "lagoonic_acp_runner.h"
#include "lagoonic_model_match.h"

#include "core/io/json.h"
#include "core/object/class_db.h"
#include "core/os/mutex.h"
#include "core/os/os.h"

#include <stdlib.h>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

// Guards approvals/resolve_permission against a dock thread answering
// while a worker thread pumps I/O.
static Mutex _runner_mutex;

// --- small Variant helpers (never throw on mismatched types) ---

static String _dstr(const Dictionary &p_d, const char *p_key) {
	StringName key(p_key);
	if (!p_d.has(key)) {
		return String();
	}
	Variant v = p_d[key];
	if (v.get_type() == Variant::STRING) {
		return v;
	}
	return String();
}

static Dictionary _ddict(const Dictionary &p_d, const char *p_key) {
	StringName key(p_key);
	if (!p_d.has(key)) {
		return Dictionary();
	}
	Variant v = p_d[key];
	if (v.get_type() == Variant::DICTIONARY) {
		return v;
	}
	return Dictionary();
}

static Array _darr(const Dictionary &p_d, const char *p_key) {
	StringName key(p_key);
	if (!p_d.has(key)) {
		return Array();
	}
	Variant v = p_d[key];
	if (v.get_type() == Variant::ARRAY) {
		return v;
	}
	return Array();
}

static String _cap(const String &p_s, int p_max) {
	if (p_s.length() <= p_max) {
		return p_s;
	}
	return p_s.left(p_max);
}

// ================================================================ mapper

void LagoonicAcpMapper::_bind_methods() {
	ClassDB::bind_static_method("LagoonicAcpMapper", D_METHOD("acp_tool_name", "call"), &LagoonicAcpMapper::acp_tool_name);
	ClassDB::bind_static_method("LagoonicAcpMapper", D_METHOD("acp_tool_detail", "call"), &LagoonicAcpMapper::acp_tool_detail);
	ClassDB::bind_static_method("LagoonicAcpMapper", D_METHOD("pick_acp_option", "params", "approved"), &LagoonicAcpMapper::pick_acp_option);
	ClassDB::bind_static_method("LagoonicAcpMapper", D_METHOD("is_hidden_tool", "name"), &LagoonicAcpMapper::is_hidden_tool);
	ClassDB::bind_static_method("LagoonicAcpMapper", D_METHOD("adapter_install_hint", "adapter_bin"), &LagoonicAcpMapper::adapter_install_hint);

	ClassDB::bind_method(D_METHOD("begin_turn"), &LagoonicAcpMapper::begin_turn);
	ClassDB::bind_method(D_METHOD("set_session_id", "session_id"), &LagoonicAcpMapper::set_session_id);
	ClassDB::bind_method(D_METHOD("feed_update", "update"), &LagoonicAcpMapper::feed_update);
	ClassDB::bind_method(D_METHOD("clear_history"), &LagoonicAcpMapper::clear_history);
	ClassDB::bind_method(D_METHOD("get_text"), &LagoonicAcpMapper::get_text);
	ClassDB::bind_method(D_METHOD("get_tool_count"), &LagoonicAcpMapper::get_tool_count);
	ClassDB::bind_method(D_METHOD("build_message"), &LagoonicAcpMapper::build_message);
	ClassDB::bind_method(D_METHOD("get_history"), &LagoonicAcpMapper::get_history);
	ClassDB::bind_method(D_METHOD("get_worker_report", "max_sentences"), &LagoonicAcpMapper::get_worker_report, DEFVAL(3));
}

LagoonicAcpMapper::LagoonicAcpMapper() {
	session_id = StringName("lagoonic");
}

void LagoonicAcpMapper::set_session_id(const String &p_session_id) {
	if (!p_session_id.is_empty()) {
		session_id = StringName(p_session_id);
	}
}

String LagoonicAcpMapper::acp_tool_name(const Dictionary &p_call) {
	String raw;
	if (p_call.has("_meta")) {
		Dictionary meta = _ddict(p_call, "_meta");
		Dictionary cc = _ddict(meta, "claudeCode");
		raw = _dstr(cc, "toolName");
	}
	if (raw.begins_with("mcp__")) {
		PackedStringArray parts = raw.substr(5).split("__");
		String server = parts.size() > 0 ? parts[0] : String();
		String tool = parts.size() > 0 ? parts[parts.size() - 1].replace("_", " ") : String();
		if (server == "lagoonic") {
			return "Godot: " + tool;
		}
		if (!server.is_empty()) {
			return server.substr(0, 1).to_upper() + server.substr(1) + ": " + tool;
		}
		return tool.is_empty() ? String("Tool") : tool;
	}
	if (!raw.is_empty()) {
		return raw;
	}
	String kind = _dstr(p_call, "kind");
	if (kind == "execute") {
		return "Bash";
	} else if (kind == "read") {
		return "Read";
	} else if (kind == "edit") {
		return "Edit";
	} else if (kind == "delete") {
		return "Edit";
	} else if (kind == "move") {
		return "Edit";
	} else if (kind == "search") {
		return "Grep";
	} else if (kind == "fetch") {
		return "WebFetch";
	} else if (kind == "think") {
		return "Think";
	}
	String title = _dstr(p_call, "title").strip_edges();
	return title.is_empty() ? String("Tool") : _cap(title, 40);
}

String LagoonicAcpMapper::acp_tool_detail(const Dictionary &p_call) {
	static const char *DETAIL_KEYS[] = { "command", "description", "file_path", "filePath", "path", "scenePath", "scene", "pattern", "query", "url", "name" };
	if (p_call.has("rawInput")) {
		Variant v = p_call["rawInput"];
		if (v.get_type() == Variant::DICTIONARY) {
			Dictionary input = v;
			for (size_t i = 0; i < sizeof(DETAIL_KEYS) / sizeof(DETAIL_KEYS[0]); i++) {
				String value = _dstr(input, DETAIL_KEYS[i]).strip_edges();
				if (!value.is_empty()) {
					return _cap(value, 240);
				}
			}
			Array keys = input.keys();
			for (int i = 0; i < keys.size(); i++) {
				if (keys[i].get_type() != Variant::STRING) {
					continue;
				}
				String kstr = keys[i];
				String value = _dstr(input, kstr.utf8().get_data());
				if (!value.strip_edges().is_empty() && value.length() <= 240) {
					return value;
				}
			}
		}
	}
	Array locations = _darr(p_call, "locations");
	if (locations.size() > 0 && locations[0].get_type() == Variant::DICTIONARY) {
		Dictionary first = locations[0];
		String path = _dstr(first, "path");
		if (!path.is_empty()) {
			return _cap(path, 240);
		}
	}
	return _cap(_dstr(p_call, "title"), 240);
}

String LagoonicAcpMapper::pick_acp_option(const Dictionary &p_params, bool p_approved) {
	Array options = _darr(p_params, "options");
	if (options.is_empty()) {
		return String();
	}
	PackedStringArray wanted;
	if (p_approved) {
		wanted.push_back("allow_once");
		wanted.push_back("allow_always");
	} else {
		wanted.push_back("reject_once");
		wanted.push_back("reject_always");
	}
	for (int w = 0; w < wanted.size(); w++) {
		for (int i = 0; i < options.size(); i++) {
			if (options[i].get_type() != Variant::DICTIONARY) {
				continue;
			}
			Dictionary entry = options[i];
			if (_dstr(entry, "kind") == wanted[w]) {
				String id = _dstr(entry, "optionId");
				if (!id.is_empty()) {
					return id;
				}
			}
		}
	}
	return String();
}

bool LagoonicAcpMapper::is_hidden_tool(const String &p_name) {
	String lower = p_name.to_lower();
	return lower == "todowrite" || lower == "todoread";
}

String LagoonicAcpMapper::adapter_install_hint(const String &p_adapter_bin) {
	String bin = p_adapter_bin.strip_edges().to_lower();
	if (bin.ends_with("claude")) {
		return "Install with: npm install -g @anthropic-ai/claude-code";
	}
	if (bin.ends_with("codex")) {
		return "Install with: npm install -g @openai/codex";
	}
	if (bin.ends_with("gemini")) {
		return "Install with: npm install -g @google/gemini-cli";
	}
	return "Install the agent's ACP adapter and ensure it is on PATH";
}

void LagoonicAcpMapper::begin_turn() {
	text.clear();
	tools.clear();
	current_message = Dictionary();
}

Array LagoonicAcpMapper::feed_update(const Dictionary &p_update) {
	String kind = _dstr(p_update, "sessionUpdate");
	if (kind == "agent_message_chunk") {
		Dictionary content = _ddict(p_update, "content");
		String chunk;
		if (_dstr(content, "type") == "text") {
			chunk = _dstr(content, "text");
		}
		if (chunk.is_empty()) {
			return Array();
		}
		text += chunk;
		Array out;
		out.push_back(_message_event());
		return out;
	} else if (kind == "agent_thought_chunk") {
		// No reasoning channel on the dock; the final text carries it.
		return Array();
	} else if (kind == "tool_call") {
		return _on_tool_call(p_update);
	} else if (kind == "tool_call_update") {
		return _on_tool_update(p_update);
	} else if (kind == "usage_update") {
		// How full the model's context window is (ACP: used / size tokens).
		const int64_t used = int64_t(double(p_update.get("used", 0)));
		const int64_t size = int64_t(double(p_update.get("size", 0)));
		if (size <= 0) {
			return Array();
		}
		Dictionary event;
		event["type"] = "context";
		event["session_id"] = String(session_id);
		event["used"] = used;
		event["size"] = size;
		Array out;
		out.push_back(event);
		return out;
	} else if (kind == "plan") {
		Array entries = _darr(p_update, "entries");
		Array todos;
		int n = 0;
		for (int i = 0; i < entries.size(); i++) {
			if (entries[i].get_type() != Variant::DICTIONARY) {
				continue;
			}
			Dictionary item = entries[i];
			String content = _dstr(item, "content").strip_edges();
			if (content.is_empty()) {
				continue;
			}
			String state = _dstr(item, "status").to_lower();
			String mapped = "pending";
			if (state == "completed") {
				mapped = "completed";
			} else if (state == "in_progress") {
				mapped = "in_progress";
			} else if (state == "cancelled" || state == "canceled") {
				mapped = "cancelled";
			}
			Dictionary todo;
			todo["id"] = itos(n++);
			todo["text"] = _cap(content, 160);
			todo["state"] = mapped;
			todos.push_back(todo);
		}
		Dictionary event;
		event["type"] = "todos";
		event["session_id"] = String(session_id);
		event["todos"] = todos;
		Array out;
		out.push_back(event);
		return out;
	}
	return Array();
}

void LagoonicAcpMapper::clear_history() {
	begin_turn();
	message_list.clear();
	message_seq = 0;
}

String LagoonicAcpMapper::get_text() const {
	return text;
}

int LagoonicAcpMapper::get_tool_count() const {
	return tools.size();
}

Dictionary LagoonicAcpMapper::build_message() const {
	return current_message.duplicate(true);
}

Array LagoonicAcpMapper::get_history() const {
	return message_list.duplicate(true);
}

String LagoonicAcpMapper::get_worker_report(int p_max_sentences) const {
	if (p_max_sentences <= 0) {
		return String();
	}
	String t = text;
	t = t.replace(". ", ".\n").replace("! ", "!\n").replace("? ", "?\n");
	PackedStringArray lines = t.split("\n");
	PackedStringArray picked;
	for (int i = 0; i < lines.size() && picked.size() < p_max_sentences; i++) {
		String s = lines[i].strip_edges();
		if (!s.is_empty()) {
			picked.push_back(s);
		}
	}
	return String(" ").join(picked);
}

static String _tool_label(const String &p_name, const String &p_detail) {
	String subject = p_detail.strip_edges();
	if (subject.length() > 200 || subject.contains("\n")) {
		subject = String();
	} else if (subject.contains("/")) {
		subject = subject.get_slice("/", subject.get_slice_count("/") - 1).get_slice(":", 0).left(60);
	} else {
		subject = subject.left(60);
	}
	return subject.is_empty() ? p_name : p_name + String::utf8(" · ") + subject;
}

static void _upsert_tool(Dictionary &p_message, const String &p_id, const String &p_name, const String &p_detail, const String &p_state, const String &p_error_detail) {
	Array tools;
	if (p_message.has("tools")) {
		Variant v = p_message["tools"];
		if (v.get_type() == Variant::ARRAY) {
			tools = v;
		}
	}
	String label = _tool_label(p_name, p_detail);
	String detail = p_error_detail.is_empty() ? p_detail : p_error_detail;
	for (int i = 0; i < tools.size(); i++) {
		if (tools[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		Dictionary entry = tools[i];
		if (_dstr(entry, "id") == p_id) {
			entry["label"] = label;
			entry["state"] = p_state;
			entry["detail"] = detail;
			tools[i] = entry;
			p_message["tools"] = tools;
			return;
		}
	}
	Dictionary entry;
	entry["id"] = p_id;
	entry["label"] = label;
	entry["state"] = p_state;
	entry["detail"] = detail;
	tools.push_back(entry);
	p_message["tools"] = tools;
}

Dictionary LagoonicAcpMapper::_ensure_message() {
	if (current_message.is_empty()) {
		message_seq += 1;
		current_message["id"] = "acp-" + String(session_id) + "-" + itos(message_seq);
		current_message["role"] = "assistant";
		current_message["text"] = text;
		Array tools_arr;
		current_message["tools"] = tools_arr;
		current_message["created_at"] = OS::get_singleton()->get_ticks_msec();
		message_list.push_back(current_message);
	}
	return current_message;
}

Dictionary LagoonicAcpMapper::_message_event() {
	_ensure_message();
	current_message["text"] = text;
	message_list[message_list.size() - 1] = current_message;
	Dictionary event;
	event["type"] = "message";
	event["session_id"] = String(session_id);
	event["message"] = current_message.duplicate(true);
	return event;
}

Array LagoonicAcpMapper::_on_tool_call(const Dictionary &p_update) {
	String status = _dstr(p_update, "status");
	if (status == "completed" || status == "failed") {
		return _on_tool_update(p_update);
	}
	String id = _dstr(p_update, "toolCallId");
	String name = acp_tool_name(p_update);
	if (is_hidden_tool(name)) {
		return Array();
	}
	String detail = acp_tool_detail(p_update);
	if (!id.is_empty()) {
		ToolEntry entry{ name, detail };
		tools.insert(id, entry);
	}
	_ensure_message();
	_upsert_tool(current_message, id.is_empty() ? name + "-" + itos(message_seq) : id, name, detail, "running", String());
	message_list[message_list.size() - 1] = current_message;
	Array out;
	out.push_back(_message_event());
	return out;
}

Array LagoonicAcpMapper::_on_tool_update(const Dictionary &p_update) {
	String id = _dstr(p_update, "toolCallId");
	ToolEntry *known = id.is_empty() ? nullptr : tools.getptr(id);
	String name = (known != nullptr) ? known->name : acp_tool_name(p_update);
	if (is_hidden_tool(name)) {
		return Array();
	}
	String status = _dstr(p_update, "status");
	_ensure_message();
	if (status == "completed" || status == "failed") {
		if (!id.is_empty()) {
			tools.erase(id);
		}
		String title = _dstr(p_update, "title");
		String base = known != nullptr ? known->detail : String();
		bool done = status == "completed";
		String error_detail;
		if (!done) {
			error_detail = "Error: " + (title.is_empty() ? "tool failed" : title);
			error_detail = _cap(error_detail, 240);
		}
		_upsert_tool(current_message, id.is_empty() ? name : id, name, title.is_empty() ? base : title, done ? "done" : "error", error_detail);
		message_list[message_list.size() - 1] = current_message;
		Array out;
		out.push_back(_message_event());
		return out;
	}
	String detail = acp_tool_detail(p_update);
	if (known != nullptr && !detail.is_empty() && detail != known->detail) {
		known->detail = detail;
		_upsert_tool(current_message, id, name, detail, "running", String());
		message_list[message_list.size() - 1] = current_message;
		Array out;
		out.push_back(_message_event());
		return out;
	}
	return Array();
}

// ================================================================ runner

void LagoonicAcpRunner::_bind_methods() {
	ClassDB::bind_method(D_METHOD("spawn", "adapter_bin", "args", "cwd", "env"), &LagoonicAcpRunner::spawn);
	ClassDB::bind_method(D_METHOD("is_running"), &LagoonicAcpRunner::is_running);
	ClassDB::bind_method(D_METHOD("set_resume", "acp_session", "transcript"), &LagoonicAcpRunner::set_resume);
	ClassDB::bind_method(D_METHOD("initialize", "protocol_version", "client_name", "client_version", "timeout_ms"), &LagoonicAcpRunner::initialize, DEFVAL(1), DEFVAL("lagoonic"), DEFVAL("1"), DEFVAL(DEFAULT_TIMEOUT_MS));
	ClassDB::bind_method(D_METHOD("session_new", "cwd", "mcp_servers", "timeout_ms"), &LagoonicAcpRunner::session_new, DEFVAL(DEFAULT_TIMEOUT_MS));
	ClassDB::bind_method(D_METHOD("session_prompt", "acp_session_id", "prompt_text", "prompt_blocks", "timeout_ms"), &LagoonicAcpRunner::session_prompt, DEFVAL(Array()), DEFVAL(0));
	ClassDB::bind_method(D_METHOD("session_cancel", "acp_session_id"), &LagoonicAcpRunner::session_cancel);
	ClassDB::bind_method(D_METHOD("resolve_permission", "approval_id", "approved"), &LagoonicAcpRunner::resolve_permission);
	ClassDB::bind_method(D_METHOD("close"), &LagoonicAcpRunner::close);
	ClassDB::bind_method(D_METHOD("poll", "timeout_ms"), &LagoonicAcpRunner::poll, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("run_codex_turn", "codex_bin", "cwd", "env", "mcp_servers", "prompt", "session_id"), &LagoonicAcpRunner::run_codex_turn, DEFVAL("lagoonic"));
	ClassDB::bind_method(D_METHOD("run_turn", "adapter_bin", "args", "cwd", "env", "mcp_servers", "prompt", "prompt_blocks", "auto_approve", "silent", "session_id"), &LagoonicAcpRunner::run_turn, DEFVAL(Array()), DEFVAL(false), DEFVAL(false), DEFVAL("lagoonic"));

	ClassDB::bind_method(D_METHOD("set_session_id", "session_id"), &LagoonicAcpRunner::set_session_id);
	ClassDB::bind_method(D_METHOD("get_session_id"), &LagoonicAcpRunner::get_session_id);

	ClassDB::bind_method(D_METHOD("set_auto_approve", "auto_approve"), &LagoonicAcpRunner::set_auto_approve);
	ClassDB::bind_method(D_METHOD("get_auto_approve"), &LagoonicAcpRunner::get_auto_approve);
	ClassDB::bind_method(D_METHOD("set_silent", "silent"), &LagoonicAcpRunner::set_silent);
	ClassDB::bind_method(D_METHOD("get_silent"), &LagoonicAcpRunner::get_silent);
	ClassDB::bind_method(D_METHOD("get_acp_session_id"), &LagoonicAcpRunner::get_acp_session_id);
	ClassDB::bind_method(D_METHOD("get_log_tail"), &LagoonicAcpRunner::get_log_tail);
	ClassDB::bind_method(D_METHOD("get_last_error"), &LagoonicAcpRunner::get_last_error);
	ClassDB::bind_method(D_METHOD("get_turn_text"), &LagoonicAcpRunner::get_turn_text);
	ClassDB::bind_method(D_METHOD("get_worker_report", "max_sentences"), &LagoonicAcpRunner::get_worker_report, DEFVAL(3));
	ClassDB::bind_method(D_METHOD("get_mapper"), &LagoonicAcpRunner::get_mapper);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_approve"), "set_auto_approve", "get_auto_approve");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "silent"), "set_silent", "get_silent");

	ADD_SIGNAL(MethodInfo("acp_event", PropertyInfo(Variant::DICTIONARY, "event")));
	ADD_SIGNAL(MethodInfo("acp_permission", PropertyInfo(Variant::DICTIONARY, "request")));
}

LagoonicAcpRunner::LagoonicAcpRunner() {
	mapper.instantiate();
}

LagoonicAcpRunner::~LagoonicAcpRunner() {
	close();
}

void LagoonicAcpRunner::_set_error(const String &p_error) {
	last_error = p_error;
}

void LagoonicAcpRunner::_log_stderr_line(const String &p_line) {
	String trimmed = p_line.strip_edges();
	if (trimmed.is_empty()) {
		return;
	}
	stderr_log.push_back(_cap(trimmed, STDERR_MAX_COLS));
	while (stderr_log.size() > STDERR_MAX_LINES) {
		stderr_log.remove_at(0);
	}
}

// --- process spawning -------------------------------------------------
// Godot's OS::create_process() returns only a pid (no pipes) and
// OS::execute_with_pipe() is one-shot, so the interactive stdio channel
// this protocol needs is implemented here with native pipes:
// pipe/fork/exec on Unix, CreateProcess with anonymous pipes on Windows.

Error LagoonicAcpRunner::spawn(const String &p_adapter_bin, const PackedStringArray &p_args, const String &p_cwd, const Dictionary &p_env) {
	if (running) {
		_set_error(String::utf8("A turn is already running. Cancel it before starting another. / Ya hay un turno en curso. Cancélalo antes de empezar otro."));
		return ERR_BUSY;
	}
	close();
	stopped = false;
	_set_error("");

#ifdef _WIN32
	SECURITY_ATTRIBUTES sa;
	sa.nLength = sizeof(sa);
	sa.lpSecurityDescriptor = nullptr;
	sa.bInheritHandle = TRUE;
	HANDLE h_stdin_r = nullptr, h_stdin_w = nullptr;
	HANDLE h_stdout_r = nullptr, h_stdout_w = nullptr;
	HANDLE h_stderr_r = nullptr, h_stderr_w = nullptr;
	if (!CreatePipe(&h_stdin_r, &h_stdin_w, &sa, 0) ||
			!CreatePipe(&h_stdout_r, &h_stdout_w, &sa, 0) ||
			!CreatePipe(&h_stderr_r, &h_stderr_w, &sa, 0)) {
		_set_error("Could not create pipes for the agent process. / No se pudieron crear los conductos del proceso del agente.");
		return ERR_CANT_CREATE;
	}
	SetHandleInformation(h_stdin_w, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(h_stdout_r, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(h_stderr_r, HANDLE_FLAG_INHERIT, 0);

	auto quote = [](const String &s) -> String {
		if (s.find(" ") < 0 && s.find("\t") < 0 && s.find("\"") < 0) {
			return s;
		}
		return "\"" + s.replace("\"", "\\\"") + "\"";
	};
	String cmdline = quote(p_adapter_bin);
	for (int i = 0; i < p_args.size(); i++) {
		cmdline += " " + quote(p_args[i]);
	}
	Char16String cmd16 = cmdline.utf16();
	std::vector<wchar_t> cmd_buf(cmd16.get_data(), cmd16.get_data() + cmd16.length() + 1);

	// Environment block: current environment + overrides.
	std::vector<wchar_t> env_block;
	{
		LPWCH cur = GetEnvironmentStringsW();
		Vector<String> override_names;
		Array keys = p_env.keys();
		for (int i = 0; i < keys.size(); i++) {
			if (keys[i].get_type() == Variant::STRING) {
				override_names.push_back(String(keys[i]));
			}
		}
		if (cur != nullptr) {
			for (LPWCH p = cur; *p != 0; p += wcslen(p) + 1) {
				String entry = String(p);
				bool skip = false;
				for (int i = 0; i < override_names.size(); i++) {
					if (entry.begins_with(override_names[i] + "=")) {
						skip = true;
						break;
					}
				}
				if (!skip) {
					size_t len = wcslen(p);
					env_block.insert(env_block.end(), p, p + len + 1);
				}
			}
			FreeEnvironmentStringsW(cur);
		}
		for (int i = 0; i < override_names.size(); i++) {
			Variant v = p_env[override_names[i]];
			String kv = override_names[i] + "=" + String(v);
			Char16String kv16 = kv.utf16();
			env_block.insert(env_block.end(), kv16.get_data(), kv16.get_data() + kv16.length() + 1);
		}
		env_block.push_back(0);
	}

	STARTUPINFOW si;
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = h_stdin_r;
	si.hStdOutput = h_stdout_w;
	si.hStdError = h_stderr_w;
	PROCESS_INFORMATION pi;
	ZeroMemory(&pi, sizeof(pi));
	Char16String cwd16 = p_cwd.utf16();
	LPCWSTR cwd_ptr = p_cwd.is_empty() ? nullptr : (LPCWSTR)cwd16.get_data();
	BOOL ok = CreateProcessW(nullptr, cmd_buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
			env_block.data(), cwd_ptr, &si, &pi);
	CloseHandle(h_stdin_r);
	CloseHandle(h_stdout_w);
	CloseHandle(h_stderr_w);
	if (!ok) {
		CloseHandle(h_stdin_w);
		CloseHandle(h_stdout_r);
		CloseHandle(h_stderr_r);
		_set_error("Adapter '" + p_adapter_bin + "' not found or failed to start. Install it and ensure it is on PATH. / Adaptador '" + p_adapter_bin + String::utf8("' no encontrado o no se pudo iniciar. Instálalo y asegúrate de que esté en el PATH. (") + LagoonicAcpMapper::adapter_install_hint(p_adapter_bin) + ")");
		return ERR_CANT_OPEN;
	}
	CloseHandle(pi.hThread);
	proc_handle = (intptr_t)pi.hProcess;
	stdin_write_h = (intptr_t)h_stdin_w;
	stdout_read_h = (intptr_t)h_stdout_r;
	stderr_read_h = (intptr_t)h_stderr_r;
	stdout_eof = false;
	stderr_eof = false;
#else
	int in_p[2] = { -1, -1 }, out_p[2] = { -1, -1 }, err_p[2] = { -1, -1 };
	if (pipe(in_p) != 0 || pipe(out_p) != 0 || pipe(err_p) != 0) {
		if (in_p[0] >= 0) {
			::close(in_p[0]);
			::close(in_p[1]);
		}
		if (out_p[0] >= 0) {
			::close(out_p[0]);
			::close(out_p[1]);
		}
		if (err_p[0] >= 0) {
			::close(err_p[0]);
			::close(err_p[1]);
		}
		_set_error("Could not create pipes for the agent process. / No se pudieron crear los conductos del proceso del agente.");
		return ERR_CANT_CREATE;
	}
	pid_t pid = fork();
	if (pid < 0) {
		::close(in_p[0]);
		::close(in_p[1]);
		::close(out_p[0]);
		::close(out_p[1]);
		::close(err_p[0]);
		::close(err_p[1]);
		_set_error("Could not fork the agent process. / No se pudo crear el proceso del agente.");
		return ERR_CANT_CREATE;
	}
	if (pid == 0) {
		// Child. Its own process group: the adapter starts the agent (the
		// `claude` CLI, ...) and that one starts lagoonic-mcp; closing the
		// turn must end all of them, not only the adapter (see _kill_child).
		setpgid(0, 0);
		dup2(in_p[0], STDIN_FILENO);
		dup2(out_p[1], STDOUT_FILENO);
		dup2(err_p[1], STDERR_FILENO);
		::close(in_p[0]);
		::close(in_p[1]);
		::close(out_p[0]);
		::close(out_p[1]);
		::close(err_p[0]);
		::close(err_p[1]);
		if (!p_cwd.is_empty()) {
			::chdir(p_cwd.utf8().get_data());
		}
		Array keys = p_env.keys();
		for (int i = 0; i < keys.size(); i++) {
			if (keys[i].get_type() != Variant::STRING) {
				continue;
			}
			String k = keys[i];
			String v = p_env[k];
			::setenv(k.utf8().get_data(), v.utf8().get_data(), 1);
		}
		Vector<CharString> storage;
		storage.push_back(p_adapter_bin.utf8());
		for (int i = 0; i < p_args.size(); i++) {
			storage.push_back(p_args[i].utf8());
		}
		std::vector<const char *> argv;
		for (int i = 0; i < storage.size(); i++) {
			argv.push_back(storage[i].get_data());
		}
		argv.push_back(nullptr);
		execvp(storage[0].get_data(), const_cast<char *const *>(argv.data()));
		_exit(127);
	}
	// Parent.
	::close(in_p[0]);
	::close(out_p[1]);
	::close(err_p[1]);
	fcntl(out_p[0], F_SETFL, fcntl(out_p[0], F_GETFL, 0) | O_NONBLOCK);
	fcntl(err_p[0], F_SETFL, fcntl(err_p[0], F_GETFL, 0) | O_NONBLOCK);
	fcntl(in_p[1], F_SETFL, fcntl(in_p[1], F_GETFL, 0) | O_NONBLOCK);
	child_pid = (int)pid;
	stdin_fd = in_p[1];
	stdout_fd = out_p[0];
	stderr_fd = err_p[0];
	stdout_eof = false;
	stderr_eof = false;
#endif
	running = true;
	stdout_buf.clear();
	stderr_buf.clear();
	pending.clear();
	pending_order.clear();
	next_id = 1;
	acp_session_id.clear();
	mapper->set_session_id(session_id);
	mapper->begin_turn();
	return OK;
}

bool LagoonicAcpRunner::is_running() const {
	return running;
}

bool LagoonicAcpRunner::_alive() {
	if (!running) {
		return false;
	}
#ifdef _WIN32
	if (proc_handle == 0) {
		return false;
	}
	DWORD code = 0;
	if (!GetExitCodeProcess((HANDLE)proc_handle, &code)) {
		return false;
	}
	return code == STILL_ACTIVE;
#else
	if (child_pid < 0) {
		return false;
	}
	int status = 0;
	pid_t r = waitpid((pid_t)child_pid, &status, WNOHANG);
	return r == 0; // Still a live child.
#endif
}

void LagoonicAcpRunner::_kill_child() {
#ifdef _WIN32
	if (proc_handle != 0) {
		TerminateProcess((HANDLE)proc_handle, 1);
		CloseHandle((HANDLE)proc_handle);
		proc_handle = 0;
	}
#else
	if (child_pid >= 0) {
		// The whole group: killing only the adapter left the agent working on
		// its own (Stop did not stop it). SIGTERM first so lagoonic-mcp can
		// clean up (override.cfg, games it started), then SIGKILL.
		const pid_t group = (pid_t)child_pid;
		kill(-group, SIGTERM);
		kill(group, SIGTERM);
		for (int i = 0; i < 30 && kill(-group, 0) == 0; i++) {
			int status = 0;
			waitpid(group, &status, WNOHANG);
			OS::get_singleton()->delay_usec(50000);
		}
		kill(-group, SIGKILL);
		kill(group, SIGKILL);
		int status = 0;
		waitpid(group, &status, WNOHANG);
		child_pid = -1;
	}
#endif
}

void LagoonicAcpRunner::_close_fds() {
#ifdef _WIN32
	if (stdin_write_h != 0) {
		CloseHandle((HANDLE)stdin_write_h);
		stdin_write_h = 0;
	}
	if (stdout_read_h != 0) {
		CloseHandle((HANDLE)stdout_read_h);
		stdout_read_h = 0;
	}
	if (stderr_read_h != 0) {
		CloseHandle((HANDLE)stderr_read_h);
		stderr_read_h = 0;
	}
#else
	if (stdin_fd >= 0) {
		::close(stdin_fd);
		stdin_fd = -1;
	}
	if (stdout_fd >= 0) {
		::close(stdout_fd);
		stdout_fd = -1;
	}
	if (stderr_fd >= 0) {
		::close(stderr_fd);
		stderr_fd = -1;
	}
#endif
	stdout_eof = true;
	stderr_eof = true;
}

void LagoonicAcpRunner::close() {
	_kill_child();
	_close_fds();
	if (running) {
		running = false;
		_fail_all(String::utf8("The agent closed the connection. / El agente cerró la conexión."));
	}
	_runner_mutex.lock();
	approvals.clear();
	approval_order.clear();
	approval_params.clear();
	_runner_mutex.unlock();
}

void LagoonicAcpRunner::_on_child_exit() {
	String tail;
	int from = (int)stderr_log.size() > 4 ? (int)stderr_log.size() - 4 : 0;
	for (int i = from; i < stderr_log.size(); i++) {
		if (!tail.is_empty()) {
			tail += " | ";
		}
		tail += stderr_log[i];
	}
	String msg = "The agent stopped.";
	if (!tail.is_empty()) {
		msg += " " + _cap(tail, 400);
	}
	msg += " / El agente se detuvo.";
	_fail_all(msg);
}

// --- I/O pump ---------------------------------------------------------

bool LagoonicAcpRunner::_write_stdin(const char *p_data, size_t p_len) {
	if (p_len == 0) {
		return true;
	}
#ifdef _WIN32
	if (stdin_write_h == 0) {
		return false;
	}
	size_t off = 0;
	while (off < p_len) {
		DWORD wrote = 0;
		size_t rest = p_len - off;
		DWORD chunk = rest > 65536 ? (DWORD)65536 : (DWORD)rest;
		if (!WriteFile((HANDLE)stdin_write_h, p_data + off, chunk, &wrote, nullptr) || wrote == 0) {
			return false;
		}
		off += wrote;
	}
	return true;
#else
	if (stdin_fd < 0) {
		return false;
	}
	size_t off = 0;
	while (off < p_len) {
		ssize_t n = ::write(stdin_fd, p_data + off, p_len - off);
		if (n < 0) {
			if (errno == EAGAIN || errno == EINTR) {
				OS::get_singleton()->delay_usec(1000);
				continue;
			}
			return false;
		}
		if (n == 0) {
			return false;
		}
		off += (size_t)n;
	}
	return true;
#endif
}

// Reads every currently available byte; returns false on EOF/closed.
static bool _read_available_native(intptr_t p_handle, std::string &r_out) {
#ifdef _WIN32
	HANDLE h = (HANDLE)p_handle;
	if (h == nullptr) {
		return false;
	}
	for (;;) {
		DWORD avail = 0;
		if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) {
			return false;
		}
		if (avail == 0) {
			return true;
		}
		char buf[4096];
		DWORD got = 0;
		DWORD want = avail > sizeof(buf) ? (DWORD)sizeof(buf) : avail;
		if (!ReadFile(h, buf, want, &got, nullptr) || got == 0) {
			return false;
		}
		r_out.append(buf, got);
	}
#else
	int fd = (int)p_handle;
	if (fd < 0) {
		return false;
	}
	char buf[4096];
	for (;;) {
		ssize_t n = ::read(fd, buf, sizeof(buf));
		if (n > 0) {
			r_out.append(buf, (size_t)n);
			if ((size_t)n < sizeof(buf)) {
				return true;
			}
			continue;
		}
		if (n == 0) {
			return false; // EOF.
		}
		return true; // EAGAIN / error: no more data right now.
	}
#endif
}

bool LagoonicAcpRunner::_pump_once() {
	if (!running) {
		return false;
	}
#ifdef _WIN32
	if (!_read_available_native(stdout_read_h, stdout_buf)) {
		stdout_eof = true;
	}
	if (!_read_available_native(stderr_read_h, stderr_buf)) {
		stderr_eof = true;
	}
#else
	if (!stdout_eof && !_read_available_native((intptr_t)stdout_fd, stdout_buf)) {
		stdout_eof = true;
	}
	if (!stderr_eof && !_read_available_native((intptr_t)stderr_fd, stderr_buf)) {
		stderr_eof = true;
	}
#endif
	// Stderr lines feed the capped log only.
	size_t pos = 0;
	while ((pos = stderr_buf.find('\n')) != std::string::npos) {
		std::string raw = stderr_buf.substr(0, pos);
		stderr_buf.erase(0, pos + 1);
		if (!raw.empty() && raw.back() == '\r') {
			raw.pop_back();
		}
		_log_stderr_line(String::utf8(raw.c_str(), (int)raw.size()));
	}
	_pump_stdout_lines();
	if ((stdout_eof && stderr_eof) || !_alive()) {
		if (!_alive()) {
			_on_child_exit();
			_kill_child();
			_close_fds();
			running = false;
			return false;
		}
	}
	return running;
}

void LagoonicAcpRunner::_pump_stdout_lines() {
	size_t pos = 0;
	while ((pos = stdout_buf.find('\n')) != std::string::npos) {
		std::string raw = stdout_buf.substr(0, pos);
		stdout_buf.erase(0, pos + 1);
		if (!raw.empty() && raw.back() == '\r') {
			raw.pop_back();
		}
		_handle_line(String::utf8(raw.c_str(), (int)raw.size()));
	}
}

void LagoonicAcpRunner::poll(int p_timeout_ms) {
	uint64_t start = OS::get_singleton()->get_ticks_msec();
	for (;;) {
		if (!_pump_once()) {
			return;
		}
		if (p_timeout_ms <= 0) {
			return;
		}
		if (OS::get_singleton()->get_ticks_msec() - start >= (uint64_t)p_timeout_ms) {
			return;
		}
		OS::get_singleton()->delay_usec(1000);
	}
}

// --- JSON-RPC framing ---------------------------------------------------

void LagoonicAcpRunner::_send(const Dictionary &p_message) {
	String line = JSON::stringify(p_message) + "\n";
	CharString cs = line.utf8();
	_write_stdin(cs.get_data(), (size_t)cs.length());
}

void LagoonicAcpRunner::_reply(const Variant &p_id, const Dictionary &p_result) {
	Dictionary msg;
	msg["jsonrpc"] = "2.0";
	msg["id"] = p_id;
	msg["result"] = p_result;
	_send(msg);
}

void LagoonicAcpRunner::_reply_error(const Variant &p_id, int p_code, const String &p_message) {
	Dictionary err;
	err["code"] = p_code;
	err["message"] = p_message;
	Dictionary msg;
	msg["jsonrpc"] = "2.0";
	msg["id"] = p_id;
	msg["error"] = err;
	_send(msg);
}

void LagoonicAcpRunner::_notify(const String &p_method, const Dictionary &p_params) {
	Dictionary msg;
	msg["jsonrpc"] = "2.0";
	msg["method"] = p_method;
	msg["params"] = p_params;
	_send(msg);
}

Dictionary LagoonicAcpRunner::_call(const String &p_method, const Dictionary &p_params, int p_timeout_ms, bool &r_ok) {
	r_ok = false;
	if (!running) {
		_set_error(String::utf8("No agent process is running. / No hay ningún proceso de agente en curso."));
		Dictionary err;
		err["error"] = last_error;
		return err;
	}
	int64_t id = next_id++;
	PendingCall slot;
	pending.insert(id, slot);
	pending_order.push_back(id);
	Dictionary msg;
	msg["jsonrpc"] = "2.0";
	msg["id"] = id;
	msg["method"] = p_method;
	msg["params"] = p_params;
	_send(msg);

	uint64_t start = OS::get_singleton()->get_ticks_msec();
	for (;;) {
		_pump_once();
		if (stopped) {
			pending.erase(id);
			pending_order.erase(id);
			_set_error("Stopped by user. / Detenido por el usuario.");
			Dictionary err;
			err["error"] = last_error;
			return err;
		}
		if (PendingCall *found = pending.getptr(id)) {
			if (found->done) {
				Dictionary result = found->result;
				String error = found->error;
				bool ok = found->ok;
				pending.erase(id);
				pending_order.erase(id);
				if (ok) {
					r_ok = true;
					return result;
				}
				_set_error(error);
				Dictionary err;
				err["error"] = error;
				return err;
			}
		} else {
			// Failed by _fail_all (process died).
			Dictionary err;
			err["error"] = last_error;
			return err;
		}
		if (p_timeout_ms > 0 && OS::get_singleton()->get_ticks_msec() - start >= (uint64_t)p_timeout_ms) {
			pending.erase(id);
			pending_order.erase(id);
			_set_error(p_method + " timed out. / Tiempo de espera agotado.");
			Dictionary err;
			err["error"] = last_error;
			return err;
		}
		if (!running) {
			Dictionary err;
			err["error"] = last_error.is_empty() ? String("The agent stopped. / El agente se detuvo.") : last_error;
			return err;
		}
		OS::get_singleton()->delay_usec(1000);
	}
}

void LagoonicAcpRunner::_fail_all(const String &p_message) {
	_set_error(p_message);
	for (int i = 0; i < pending_order.size(); i++) {
		if (PendingCall *slot = pending.getptr(pending_order[i])) {
			if (!slot->done) {
				slot->done = true;
				slot->ok = false;
				slot->error = p_message;
			}
		}
	}
	pending.clear();
	pending_order.clear();
}

void LagoonicAcpRunner::_handle_line(const String &p_raw) {
	String line = p_raw.strip_edges();
	if (line.is_empty()) {
		return;
	}
	Variant parsed;
	if (!line.begins_with("{")) {
		_log_stderr_line(line);
		return;
	}
	parsed = JSON::parse_string(line);
	if (parsed.get_type() != Variant::DICTIONARY) {
		_log_stderr_line(line);
		return;
	}
	Dictionary message = parsed;
	String method = _dstr(message, "method");
	bool has_id = message.has("id");
	if (!method.is_empty() && has_id) {
		Variant id = message["id"];
		Dictionary params = _ddict(message, "params");
		_handle_request(id, method, params);
	} else if (!method.is_empty()) {
		Dictionary params = _ddict(message, "params");
		_handle_notification(method, params);
	} else if (has_id) {
		Variant idv = message["id"];
		int64_t id = -1;
		if (idv.get_type() == Variant::INT) {
			id = idv;
		} else if (idv.get_type() == Variant::FLOAT) {
			id = (int64_t)(double)idv;
		}
		if (id < 0) {
			return;
		}
		if (PendingCall *slot = pending.getptr(id)) {
			if (message.has("error") && Variant(message["error"]).get_type() == Variant::DICTIONARY) {
				Dictionary err = message["error"];
				String emsg = _dstr(err, "message");
				// "Internal error" alone hides the cause: adapters put the
				// provider's answer in error.data (codex-acp: data.message,
				// itself JSON like {"error": {"message": "..."}}).
				String detail;
				const Variant data = err.get("data", Variant());
				if (data.get_type() == Variant::STRING) {
					detail = data;
				} else if (data.get_type() == Variant::DICTIONARY) {
					detail = _dstr(Dictionary(data), "message");
				}
				const Variant inner = detail.strip_edges().begins_with("{") ? JSON::parse_string(detail) : Variant();
				if (inner.get_type() == Variant::DICTIONARY) {
					const Dictionary inner_error = _ddict(Dictionary(inner), "error");
					const String inner_message = _dstr(inner_error, "message");
					if (!inner_message.is_empty()) {
						detail = inner_message;
					}
				}
				detail = detail.strip_edges();
				if (!detail.is_empty() && detail != emsg) {
					emsg = emsg.is_empty() ? detail : emsg + ": " + detail;
				}
				slot->done = true;
				slot->ok = false;
				slot->error = emsg.is_empty() ? String("Agent error. / Error del agente.") : emsg;
			} else {
				slot->done = true;
				slot->ok = true;
				slot->result = _ddict(message, "result");
			}
		}
	}
}

void LagoonicAcpRunner::_handle_notification(const String &p_method, const Dictionary &p_params) {
	if (codex_dialect) {
		_handle_codex_notification(p_method, p_params);
		return;
	}
	if (p_method != "session/update") {
		return;
	}
	if (replaying) {
		return; // session/load replays the old turns; the chat already has them.
	}
	String sid = _dstr(p_params, "sessionId");
	if (!sid.is_empty() && sid != acp_session_id) {
		return;
	}
	Dictionary update = _ddict(p_params, "update");
	Array events = mapper->feed_update(update);
	if (silent) {
		return; // Worker turn: onEvent is a no-op; report is captured.
	}
	for (int i = 0; i < events.size(); i++) {
		if (events[i].get_type() == Variant::DICTIONARY) {
			emit_signal("acp_event", events[i]);
		}
	}
}

void LagoonicAcpRunner::_handle_request(const Variant &p_id, const String &p_method, const Dictionary &p_params) {
	if (codex_dialect) {
		_handle_codex_request(p_id, p_method, p_params);
		return;
	}
	if (p_method != "session/request_permission") {
		_reply_error(p_id, -32601, "Method not supported: " + p_method);
		return;
	}
	_handle_permission(p_id, p_params);
}

void LagoonicAcpRunner::_handle_permission(const Variant &p_id, const Dictionary &p_params) {
	if (auto_approve || silent) {
		// Silent worker turns always behave like the autonomous mode.
		String option = LagoonicAcpMapper::pick_acp_option(p_params, true);
		Dictionary outcome;
		if (option.is_empty()) {
			Dictionary cancelled;
			cancelled["outcome"] = "cancelled";
			outcome["outcome"] = cancelled;
		} else {
			Dictionary selected;
			selected["outcome"] = "selected";
			selected["optionId"] = option;
			outcome["outcome"] = selected;
		}
		_reply(p_id, outcome);
		return;
	}
	Dictionary tool_call = _ddict(p_params, "toolCall");
	String name = LagoonicAcpMapper::acp_tool_name(tool_call);
	String detail = LagoonicAcpMapper::acp_tool_detail(tool_call);
	String title = _dstr(tool_call, "title");
	if (title.is_empty()) {
		title = detail;
	}
	if (title.is_empty()) {
		title = "The agent requests permission to continue.";
	}
	String approval_id = _new_approval_id();
	_runner_mutex.lock();
	approvals.insert(approval_id, p_id);
	approval_order.push_back(approval_id);
	approval_params.insert(approval_id, p_params);
	_runner_mutex.unlock();
	Dictionary request;
	request["id"] = approval_id;
	request["session_id"] = session_id;
	request["title"] = title;
	if (!detail.is_empty()) {
		request["detail"] = detail;
	}
	emit_signal("acp_permission", request);
}

String LagoonicAcpRunner::_new_approval_id() {
	uint64_t ticks = OS::get_singleton()->get_ticks_msec();
	return "acp-" + String::num_uint64(ticks) + "-" + itos(next_id);
}

void LagoonicAcpRunner::resolve_permission(const String &p_approval_id, bool p_approved) {
	_runner_mutex.lock();
	Variant *id_ptr = approvals.getptr(p_approval_id);
	Dictionary *params_ptr = approval_params.getptr(p_approval_id);
	Variant id;
	Dictionary params;
	bool found = id_ptr != nullptr;
	if (found) {
		id = *id_ptr;
		if (params_ptr != nullptr) {
			params = *params_ptr;
		}
		approvals.erase(p_approval_id);
		approval_order.erase(p_approval_id);
		approval_params.erase(p_approval_id);
	}
	_runner_mutex.unlock();
	if (!found) {
		return;
	}
	// Same choice the autonomous mode makes: allow_once/allow_always
	// when approved, reject_once/reject_always otherwise, cancelled
	// when the agent offered nothing usable.
	String option = LagoonicAcpMapper::pick_acp_option(params, p_approved);
	if (codex_dialect) {
		// codex app-server answers {decision: accept | decline}.
		Dictionary decision;
		decision["decision"] = option == "accept" ? String("accept") : String("decline");
		_reply(id, decision);
		return;
	}
	Dictionary outcome_inner;
	if (option.is_empty()) {
		outcome_inner["outcome"] = "cancelled";
	} else {
		outcome_inner["outcome"] = "selected";
		outcome_inner["optionId"] = option;
	}
	Dictionary outcome;
	outcome["outcome"] = outcome_inner;
	_reply(id, outcome);
}

// --- public protocol methods ------------------------------------------

Dictionary LagoonicAcpRunner::initialize(int p_protocol_version, const String &p_client_name, const String &p_client_version, int p_timeout_ms) {
	Dictionary fs;
	fs["readTextFile"] = false;
	fs["writeTextFile"] = false;
	Dictionary caps;
	caps["fs"] = fs;
	caps["terminal"] = false;
	Dictionary info;
	info["name"] = p_client_name;
	info["version"] = p_client_version;
	Dictionary params;
	params["protocolVersion"] = p_protocol_version;
	params["clientCapabilities"] = caps;
	params["clientInfo"] = info;
	bool ok = false;
	return _call("initialize", params, p_timeout_ms, ok);
}

void LagoonicAcpRunner::set_resume(const String &p_acp_session, const String &p_transcript) {
	resume_acp_session = p_acp_session;
	resume_transcript = p_transcript;
}

bool LagoonicAcpRunner::session_load(const String &p_acp_session, const String &p_cwd, const Array &p_mcp_servers, int p_timeout_ms) {
	Dictionary params;
	params["sessionId"] = p_acp_session;
	params["cwd"] = p_cwd;
	params["mcpServers"] = p_mcp_servers;
	acp_session_id = p_acp_session;
	replaying = true;
	bool ok = false;
	Dictionary result = _call("session/load", params, p_timeout_ms, ok);
	replaying = false;
	if (!ok) {
		acp_session_id = String();
		return false;
	}
	session_info = result;
	return true;
}

String LagoonicAcpRunner::session_new(const String &p_cwd, const Array &p_mcp_servers, int p_timeout_ms) {
	Dictionary params;
	params["cwd"] = p_cwd;
	params["mcpServers"] = p_mcp_servers;
	bool ok = false;
	Dictionary result = _call("session/new", params, p_timeout_ms, ok);
	if (!ok) {
		return String();
	}
	String sid = _dstr(result, "sessionId");
	if (sid.is_empty()) {
		_set_error(String::utf8("The agent did not start a session. / El agente no inició sesión."));
		return String();
	}
	acp_session_id = sid;
	session_info = result;
	return sid;
}

static Dictionary _option_value(const Dictionary &p_entry) {
	Dictionary v;
	v["value"] = _dstr(p_entry, "value");
	const String name = _dstr(p_entry, "name");
	v["name"] = name.is_empty() ? String(v["value"]) : name;
	v["description"] = _dstr(p_entry, "description");
	return v;
}

Array LagoonicAcpRunner::parse_options(const Dictionary &p_session) {
	Array models_first;
	Array rest;
	bool has_model = false;
	const Array config = _darr(p_session, "configOptions");
	for (int i = 0; i < config.size(); i++) {
		if (config[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary item = config[i];
		const String type = _dstr(item, "type");
		if (!type.is_empty() && type != "select") {
			continue;
		}
		Array values;
		const Array raw = _darr(item, "options");
		for (int j = 0; j < raw.size(); j++) {
			if (raw[j].get_type() != Variant::DICTIONARY) {
				continue;
			}
			const Dictionary entry = raw[j];
			if (entry.has("options")) { // Grouped: {group, name, options[]}.
				const Array grouped = _darr(entry, "options");
				for (int k = 0; k < grouped.size(); k++) {
					if (grouped[k].get_type() == Variant::DICTIONARY) {
						const Dictionary v = _option_value(grouped[k]);
						if (!String(v["value"]).is_empty()) {
							values.push_back(v);
						}
					}
				}
			} else {
				const Dictionary v = _option_value(entry);
				if (!String(v["value"]).is_empty()) {
					values.push_back(v);
				}
			}
		}
		const String id = _dstr(item, "id");
		if (id.is_empty() || values.size() < 2) {
			continue;
		}
		Dictionary option;
		option["id"] = id;
		const String name = _dstr(item, "name");
		option["name"] = name.is_empty() ? id : name;
		option["category"] = _dstr(item, "category");
		option["current"] = _dstr(item, "currentValue");
		option["values"] = values;
		option["via_set_model"] = false;
		if (String(option["category"]) == "model") {
			has_model = true;
			models_first.push_back(option);
		} else {
			rest.push_back(option);
		}
	}
	// Older adapters: {models: {currentModelId, availableModels[]}}.
	const Dictionary models = _ddict(p_session, "models");
	if (!has_model && !models.is_empty()) {
		Array values;
		const Array available = _darr(models, "availableModels");
		for (int i = 0; i < available.size(); i++) {
			if (available[i].get_type() != Variant::DICTIONARY) {
				continue;
			}
			const Dictionary m = available[i];
			const String model_id = _dstr(m, "modelId");
			if (model_id.is_empty()) {
				continue;
			}
			Dictionary v;
			v["value"] = model_id;
			const String name = _dstr(m, "name");
			v["name"] = name.is_empty() ? model_id : name;
			v["description"] = _dstr(m, "description");
			values.push_back(v);
		}
		if (values.size() >= 2) {
			Dictionary option;
			option["id"] = "model";
			option["name"] = "Model";
			option["category"] = "model";
			option["current"] = _dstr(models, "currentModelId");
			option["values"] = values;
			option["via_set_model"] = true;
			models_first.push_back(option);
		}
	}
	models_first.append_array(rest);
	return models_first;
}

static bool _option_offers(const Dictionary &p_option, const String &p_value) {
	const Array values = p_option.get("values", Array());
	for (int i = 0; i < values.size(); i++) {
		if (String(Dictionary(values[i]).get("value", "")) == p_value) {
			return true;
		}
	}
	return false;
}

Array LagoonicAcpRunner::_apply_options(const String &p_acp_session_id, const Array &p_options) {
	Array options = p_options;
	PackedStringArray ids;
	for (int i = 0; i < options.size(); i++) {
		ids.push_back(Dictionary(options[i]).get("id", ""));
	}
	for (int n = 0; n < ids.size(); n++) {
		int index = -1;
		for (int i = 0; i < options.size(); i++) {
			if (String(Dictionary(options[i]).get("id", "")) == ids[n]) {
				index = i;
				break;
			}
		}
		if (index < 0) {
			continue;
		}
		Dictionary option = options[index];
		String wanted = chosen_options.get(ids[n], "");
		if (String(option.get("category", "")) == "mode" && !requested_mode.is_empty()) {
			wanted = requested_mode;
		}
		if (wanted.is_empty() || wanted == String(option.get("current", "")) || !_option_offers(option, wanted)) {
			continue;
		}
		bool ok = false;
		Dictionary params;
		params["sessionId"] = p_acp_session_id;
		Dictionary result;
		if (bool(option.get("via_set_model", false))) {
			params["modelId"] = wanted;
			result = _call("session/set_model", params, DEFAULT_TIMEOUT_MS, ok);
		} else {
			params["configId"] = ids[n];
			params["value"] = wanted;
			result = _call("session/set_config_option", params, DEFAULT_TIMEOUT_MS, ok);
		}
		if (!ok) {
			WARN_PRINT(vformat("Lagoonic: the agent did not accept %s = %s.", ids[n], wanted));
			continue;
		}
		if (result.has("configOptions")) {
			// The agent answers with every option as it is now (a new model
			// can bring other effort levels).
			Dictionary merged = result;
			if (session_info.has("models")) {
				merged["models"] = session_info["models"];
			}
			options = parse_options(merged);
		} else {
			option["current"] = wanted;
			options[index] = option;
		}
	}
	return options;
}

Array LagoonicAcpRunner::probe_options(const String &p_adapter_bin, const PackedStringArray &p_args, const String &p_cwd, const Dictionary &p_env) {
	if (_is_codex_app_server(p_adapter_bin, p_args)) {
		// Codex's real models and their efforts, before the first message.
		const Array models = probe_codex_models(p_adapter_bin, p_cwd, p_env);
		String model;
		String effort;
		for (int i = 0; i < models.size(); i++) {
			const Dictionary m = models[i];
			if (bool(m.get("is_default", false))) {
				model = m.get("value", "");
				effort = m.get("default_effort", "");
			}
		}
		if (model.is_empty() && !models.is_empty()) {
			model = Dictionary(models[0]).get("value", "");
		}
		return _codex_options(models, model, effort);
	}
	Array options;
	const bool was_silent = silent;
	silent = true;
	if (spawn(p_adapter_bin, p_args, p_cwd, p_env) == OK) {
		const Dictionary init_result = initialize(1, "lagoonic", "1", DEFAULT_TIMEOUT_MS);
		if (!init_result.has("error") && !session_new(p_cwd, Array(), DEFAULT_TIMEOUT_MS).is_empty()) {
			options = parse_options(session_info);
		}
	}
	close();
	silent = was_silent;
	return options;
}

void LagoonicAcpRunner::set_chosen_options(const Dictionary &p_choices) {
	chosen_options = p_choices.duplicate();
}

void LagoonicAcpRunner::set_requested_mode(const String &p_mode) {
	requested_mode = p_mode;
}

Dictionary LagoonicAcpRunner::session_prompt(const String &p_acp_session_id, const String &p_prompt_text, const Array &p_prompt_blocks, int p_timeout_ms) {
	// The text always goes first; the blocks (images...) follow it.
	Array prompt;
	if (!p_prompt_text.is_empty()) {
		Dictionary block;
		block["type"] = "text";
		block["text"] = p_prompt_text;
		prompt.push_back(block);
	}
	prompt.append_array(p_prompt_blocks);
	Dictionary params;
	params["sessionId"] = p_acp_session_id;
	params["prompt"] = prompt;

	Dictionary busy;
	busy["type"] = "busy";
	busy["session_id"] = session_id;
	busy["busy"] = true;
	if (!silent) {
		emit_signal("acp_event", busy);
	}

	bool ok = false;
	Dictionary result = _call("session/prompt", params, p_timeout_ms, ok);

	Dictionary idle;
	idle["type"] = "busy";
	idle["session_id"] = session_id;
	idle["busy"] = false;
	if (!silent) {
		emit_signal("acp_event", idle);
	}

	if (!ok) {
		return result; // Already {error:...}.
	}
	String stop = _dstr(result, "stopReason");
	if (stopped || stop == "cancelled") {
		stopped = false;
		_set_error("Stopped by user. / Detenido por el usuario.");
		Dictionary err;
		err["error"] = last_error;
		return err;
	}
	if (stop == "refusal") {
		_set_error(String::utf8("The agent declined to continue. / El agente declinó continuar."));
		Dictionary err;
		err["error"] = last_error;
		return err;
	}
	if (stop == "max_tokens" || stop == "max_turn_requests") {
		_set_error(String::utf8("The agent stopped because it reached a limit. / El agente se detuvo porque alcanzó un límite."));
		Dictionary err;
		err["error"] = last_error;
		return err;
	}
	return result;
}

void LagoonicAcpRunner::session_cancel(const String &p_acp_session_id) {
	stopped = true;
	_runner_mutex.lock();
	for (int i = 0; i < approval_order.size(); i++) {
		if (Variant *id_ptr = approvals.getptr(approval_order[i])) {
			Dictionary cancelled_inner;
			cancelled_inner["outcome"] = "cancelled";
			Dictionary cancelled;
			cancelled["outcome"] = cancelled_inner;
			_reply(*id_ptr, cancelled);
		}
	}
	approvals.clear();
	approval_order.clear();
	approval_params.clear();
	_runner_mutex.unlock();
	if (running && !p_acp_session_id.is_empty()) {
		Dictionary params;
		params["sessionId"] = p_acp_session_id;
		_notify("session/cancel", params);
	}
	close(); // Kill on cancel: one process per turn, always reaped.
}

Dictionary LagoonicAcpRunner::run_turn(const String &p_adapter_bin, const PackedStringArray &p_args, const String &p_cwd, const Dictionary &p_env, const Array &p_mcp_servers, const String &p_prompt, const Array &p_prompt_blocks, bool p_auto_approve, bool p_silent, const String &p_session_id) {
	if (_is_codex_app_server(p_adapter_bin, p_args)) {
		// Codex speaks its own app-server protocol (real model list, newer models).
		return _codex_turn(p_adapter_bin, p_cwd, p_env, p_mcp_servers, p_prompt, p_prompt_blocks, p_auto_approve, p_silent, p_session_id);
	}
	Dictionary out;
	auto_approve = p_auto_approve;
	silent = p_silent;
	session_model = String();
	set_session_id(p_session_id);
	Error err = spawn(p_adapter_bin, p_args, p_cwd, p_env);
	if (err != OK) {
		out["ok"] = false;
		out["error"] = last_error;
		out["log"] = get_log_tail();
		return out;
	}
	mapper->begin_turn();
	bool ok = false;
	Dictionary init_result = initialize(1, "lagoonic", "1", DEFAULT_TIMEOUT_MS);
	ok = !init_result.has("error");
	String sid;
	bool resumed = false;
	String prompt = p_prompt;
	const String resume_session = resume_acp_session;
	const String transcript = resume_transcript;
	resume_acp_session = String();
	resume_transcript = String();
	if (ok) {
		// The adapter starts fresh every turn: carry the conversation over.
		const Dictionary caps = init_result.get("agentCapabilities", Dictionary());
		if (!resume_session.is_empty() && bool(caps.get("loadSession", false)) &&
				session_load(resume_session, p_cwd, p_mcp_servers, DEFAULT_TIMEOUT_MS)) {
			sid = resume_session;
			resumed = true;
		} else {
			sid = session_new(p_cwd, p_mcp_servers, DEFAULT_TIMEOUT_MS);
			ok = !sid.is_empty();
			if (ok && !transcript.is_empty()) {
				prompt = transcript + "\n\n" + p_prompt;
			}
		}
	}
	if (ok) {
		Array options = parse_options(session_info);
		ok = _choose_requested_model(options);
	}
	if (ok) {
		Array options = parse_options(session_info);
		if (requested_mode == "plan") {
			bool offers_plan = false;
			for (int i = 0; i < options.size(); i++) {
				const Dictionary option = options[i];
				if (String(option.get("category", "")) == "mode" && _option_offers(option, "plan")) {
					offers_plan = true;
				}
			}
			if (!offers_plan) {
				_set_error(String::utf8("This agent has no plan mode; switch to Automatic or Ask first. / Este agente no tiene modo de solo planificar; cambia a Automático o Preguntar antes."));
				ok = false;
			}
		}
		if (ok) {
			options = _apply_options(sid, options);
			_set_session_model(describe_model(options));
			if (!silent && !options.is_empty()) {
				Dictionary event;
				event["type"] = "acp_options";
				event["session_id"] = session_id;
				event["options"] = options;
				emit_signal("acp_event", event);
			}
		}
	}
	Dictionary prompt_result;
	if (ok) {
		prompt_result = session_prompt(sid, prompt, p_prompt_blocks, 0);
		ok = !prompt_result.has("error");
		// Instructions added while it worked go as the next prompt of the
		// same session (ACP cannot add to a prompt in flight).
		for (String extra = _take_steer(); ok && !extra.is_empty() && !stopped; extra = _take_steer()) {
			prompt_result = session_prompt(sid, String::utf8("Nuevas indicaciones del coordinador para esta misma tarea:\n") + extra, Array(), 0);
			ok = !prompt_result.has("error");
		}
	}
	String stop_reason = _dstr(prompt_result, "stopReason");
	out["ok"] = ok;
	if (!stop_reason.is_empty()) {
		out["stop_reason"] = stop_reason;
	}
	out["text"] = mapper->get_text();
	out["report"] = mapper->get_worker_report(3);
	out["model"] = session_model;
	out["models"] = offered_models;
	out["acp_session_id"] = sid;
	out["resumed"] = resumed;
	if (!ok) {
		out["error"] = last_error;
	}
	out["log"] = get_log_tail();
	close();
	return out;
}

// --- steering ---------------------------------------------------------

void LagoonicAcpRunner::steer(const String &p_text) {
	if (p_text.strip_edges().is_empty()) {
		return;
	}
	MutexLock lock(steer_mutex);
	steer_queue.push_back(p_text.strip_edges());
}

String LagoonicAcpRunner::_take_steer() {
	MutexLock lock(steer_mutex);
	String text;
	for (const String &t : steer_queue) {
		text += (text.is_empty() ? "" : "\n\n") + t;
	}
	steer_queue.clear();
	return text;
}

// --- model ------------------------------------------------------------

String LagoonicAcpRunner::describe_model(const Array &p_options) {
	String model;
	String effort;
	for (int i = 0; i < p_options.size(); i++) {
		if (p_options[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary option = p_options[i];
		const String category = _dstr(option, "category");
		const String current = _dstr(option, "current");
		if (current.is_empty()) {
			continue;
		}
		// The value's display name when the agent gives one.
		String label = current;
		const Array values = _darr(option, "values");
		for (int v = 0; v < values.size(); v++) {
			if (values[v].get_type() == Variant::DICTIONARY && _dstr(Dictionary(values[v]), "value") == current) {
				const String name = _dstr(Dictionary(values[v]), "name");
				if (!name.is_empty()) {
					label = name;
				}
				break;
			}
		}
		if (category == "model" && model.is_empty()) {
			model = label;
		} else if ((category == "thought_level" || _dstr(option, "id").contains("effort")) && effort.is_empty()) {
			effort = label;
		}
	}
	if (!model.is_empty() && !effort.is_empty()) {
		return model + String::utf8(" · ") + effort;
	}
	return model;
}

bool LagoonicAcpRunner::_choose_requested_model(const Array &p_options) {
	offered_models.clear();
	Dictionary model_option;
	Dictionary effort_option;
	for (int i = 0; i < p_options.size(); i++) {
		const Dictionary option = p_options[i];
		const String category = _dstr(option, "category");
		if (category == "model" && model_option.is_empty()) {
			model_option = option;
		} else if ((category == "thought_level" || _dstr(option, "id").contains("effort")) && effort_option.is_empty()) {
			effort_option = option;
		}
	}
	const Array values = _darr(model_option, "values");
	for (int i = 0; i < values.size(); i++) {
		const Dictionary v = values[i];
		Dictionary row;
		row["value"] = _dstr(v, "value");
		row["name"] = _dstr(v, "name");
		offered_models.push_back(row);
	}
	if (requested_model.strip_edges().is_empty()) {
		return true;
	}
	if (model_option.is_empty()) {
		_set_error("This agent does not let Lagoonic choose its model; it uses its own default. / Este agente no deja elegir su modelo.");
		return false;
	}
	const String value = LagoonicModelMatch::best(offered_models, requested_model);
	if (value.is_empty()) {
		_set_error("\"" + requested_model + "\" is not one of this agent's models: " + LagoonicModelMatch::describe(offered_models) + ".");
		return false;
	}
	chosen_options[_dstr(model_option, "id")] = value;
	Vector<String> words;
	String effort;
	LagoonicModelMatch::split_request(requested_model, words, effort);
	if (!effort.is_empty() && !effort_option.is_empty()) {
		const Array efforts = _darr(effort_option, "values");
		for (int i = 0; i < efforts.size(); i++) {
			const Dictionary e = efforts[i];
			if (_dstr(e, "value").to_lower() == effort || _dstr(e, "name").to_lower() == effort) {
				chosen_options[_dstr(effort_option, "id")] = _dstr(e, "value");
				break;
			}
		}
	}
	return true;
}

Array LagoonicAcpRunner::_codex_model_list() {
	Array out;
	bool ok = false;
	Dictionary params;
	const Dictionary result = _call("model/list", params, DEFAULT_TIMEOUT_MS, ok);
	if (!ok) {
		return out;
	}
	const Array data = _darr(result, "data");
	for (int i = 0; i < data.size(); i++) {
		if (data[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary m = data[i];
		Dictionary row;
		row["value"] = _dstr(m, "id");
		row["name"] = _dstr(m, "displayName");
		Array efforts;
		const Array supported = _darr(m, "supportedReasoningEfforts");
		for (int e = 0; e < supported.size(); e++) {
			if (supported[e].get_type() == Variant::DICTIONARY) {
				efforts.push_back(_dstr(Dictionary(supported[e]), "reasoningEffort"));
			}
		}
		row["efforts"] = efforts;
		row["is_default"] = bool(m.get("isDefault", false));
		row["default_effort"] = _dstr(m, "defaultReasoningEffort");
		out.push_back(row);
	}
	return out;
}

Dictionary LagoonicAcpRunner::probe_codex_rate_limits(const String &p_codex_bin, const String &p_cwd, const Dictionary &p_env) {
	Dictionary out;
	const bool was_silent = silent;
	silent = true;
	codex_dialect = true;
	PackedStringArray args;
	args.push_back("app-server");
	if (spawn(p_codex_bin, args, p_cwd, p_env) == OK) {
		Dictionary client_info;
		client_info["name"] = "lagoonic";
		client_info["version"] = "1";
		Dictionary capabilities;
		capabilities["experimentalApi"] = true;
		Dictionary init_params;
		init_params["clientInfo"] = client_info;
		init_params["capabilities"] = capabilities;
		bool ok = false;
		_call("initialize", init_params, DEFAULT_TIMEOUT_MS, ok);
		if (ok) {
			_notify("initialized", Dictionary());
			const Dictionary account = _call("account/read", Dictionary(), DEFAULT_TIMEOUT_MS, ok);
			if (ok) {
				out["account"] = _ddict(account, "account");
			}
			const Dictionary limits = _call("account/rateLimits/read", Dictionary(), DEFAULT_TIMEOUT_MS, ok);
			if (ok) {
				out["rateLimits"] = _ddict(limits, "rateLimits");
			} else {
				out["error"] = last_error;
			}
		} else {
			out["error"] = last_error;
		}
	} else {
		out["error"] = last_error;
	}
	close();
	codex_dialect = false;
	silent = was_silent;
	return out;
}

Array LagoonicAcpRunner::probe_codex_models(const String &p_codex_bin, const String &p_cwd, const Dictionary &p_env) {
	Array models;
	const bool was_silent = silent;
	silent = true;
	codex_dialect = true;
	PackedStringArray args;
	args.push_back("app-server");
	if (spawn(p_codex_bin, args, p_cwd, p_env) == OK) {
		Dictionary client_info;
		client_info["name"] = "lagoonic";
		client_info["version"] = "1";
		Dictionary capabilities;
		capabilities["experimentalApi"] = true;
		Dictionary init_params;
		init_params["clientInfo"] = client_info;
		init_params["capabilities"] = capabilities;
		bool ok = false;
		_call("initialize", init_params, DEFAULT_TIMEOUT_MS, ok);
		if (ok) {
			_notify("initialized", Dictionary());
			models = _codex_model_list();
		}
	}
	close();
	codex_dialect = false;
	silent = was_silent;
	return models;
}

void LagoonicAcpRunner::_set_session_model(const String &p_model) {
	session_model = p_model;
	if (!p_model.is_empty() && model_callback.is_valid()) {
		model_callback.call(p_model);
	}
}

// --- codex app-server ---------------------------------------------------

void LagoonicAcpRunner::_feed_codex_update(const Dictionary &p_update) {
	if (silent) {
		return;
	}
	const Array events = mapper->feed_update(p_update);
	for (int i = 0; i < events.size(); i++) {
		if (events[i].get_type() == Variant::DICTIONARY) {
			emit_signal("acp_event", events[i]);
		}
	}
}

static Dictionary _codex_tool_update(const Dictionary &p_item, bool p_done) {
	// A codex item (command, MCP tool, file edit, web search) as an ACP
	// tool_call / tool_call_update for the chat.
	const String type = p_item.get("type", "");
	Dictionary update;
	update["sessionUpdate"] = p_done ? "tool_call_update" : "tool_call";
	update["toolCallId"] = String(p_item.get("id", ""));
	String title;
	String kind = "other";
	Dictionary raw;
	if (type == "commandExecution") {
		title = String(p_item.get("command", ""));
		kind = "execute";
		raw["command"] = title;
	} else if (type == "mcpToolCall") {
		title = String(p_item.get("server", "")) + "." + String(p_item.get("tool", ""));
		raw = p_item.get("arguments", Dictionary()).get_type() == Variant::DICTIONARY ? Dictionary(p_item.get("arguments", Dictionary())) : Dictionary();
	} else if (type == "fileChange") {
		title = String::utf8("Editar archivos");
		kind = "edit";
		const Array changes = p_item.get("changes", Array());
		if (!changes.is_empty() && changes[0].get_type() == Variant::DICTIONARY) {
			raw["path"] = Dictionary(changes[0]).get("path", "");
			title = String::utf8("Editar ") + String(raw["path"]).get_file();
		}
	} else if (type == "webSearch") {
		title = String::utf8("Buscar: ") + String(p_item.get("query", ""));
		kind = "fetch";
	} else {
		title = type;
	}
	update["title"] = title;
	update["kind"] = kind;
	update["rawInput"] = raw;
	const String status = p_item.get("status", "");
	update["status"] = !p_done ? String("in_progress") : (status == "failed" || status == "declined" ? String("failed") : String("completed"));
	return update;
}

static bool _codex_is_tool_item(const Dictionary &p_item) {
	const String type = p_item.get("type", "");
	return type == "commandExecution" || type == "mcpToolCall" || type == "fileChange" || type == "webSearch" || type == "dynamicToolCall";
}

void LagoonicAcpRunner::_handle_codex_notification(const String &p_method, const Dictionary &p_params) {
	if (p_method == "item/agentMessage/delta") {
		codex_delta_text += _dstr(p_params, "delta");
		Dictionary content;
		content["type"] = "text";
		content["text"] = _dstr(p_params, "delta");
		Dictionary update;
		update["sessionUpdate"] = "agent_message_chunk";
		update["content"] = content;
		_feed_codex_update(update);
	} else if (p_method == "item/reasoning/summaryTextDelta" || p_method == "item/reasoning/textDelta") {
		Dictionary content;
		content["type"] = "text";
		content["text"] = _dstr(p_params, "delta");
		Dictionary update;
		update["sessionUpdate"] = "agent_thought_chunk";
		update["content"] = content;
		_feed_codex_update(update);
	} else if (p_method == "item/started") {
		const Dictionary item = _ddict(p_params, "item");
		if (_codex_is_tool_item(item)) {
			_feed_codex_update(_codex_tool_update(item, false));
		}
	} else if (p_method == "turn/plan/updated") {
		Array entries;
		const Array plan = _darr(p_params, "plan");
		for (int i = 0; i < plan.size(); i++) {
			const Dictionary step = plan[i];
			const String status = _dstr(step, "status");
			Dictionary entry;
			entry["content"] = _dstr(step, "step");
			entry["status"] = status == "inProgress" ? String("in_progress") : (status == "completed" ? String("completed") : String("pending"));
			entry["priority"] = "medium";
			entries.push_back(entry);
		}
		Dictionary update;
		update["sessionUpdate"] = "plan";
		update["entries"] = entries;
		_feed_codex_update(update);
	} else if (p_method == "item/completed") {
		const Dictionary item = _ddict(p_params, "item");
		if (_codex_is_tool_item(item)) {
			_feed_codex_update(_codex_tool_update(item, true));
		}
		if (_dstr(item, "type") == "agentMessage") {
			const String text = _dstr(item, "text").strip_edges();
			if (!text.is_empty()) {
				codex_final_text += (codex_final_text.is_empty() ? "" : "\n\n") + text;
			}
		}
	} else if (p_method == "thread/tokenUsage/updated") {
		// The last request's tokens are what the context holds now.
		const Dictionary usage = _ddict(p_params, "tokenUsage");
		const Dictionary last = _ddict(usage, "last");
		int64_t used = int64_t(double(last.get("totalTokens", 0)));
		if (used <= 0) {
			used = int64_t(double(last.get("inputTokens", 0))) + int64_t(double(last.get("outputTokens", 0)));
		}
		const Variant window = usage.get("modelContextWindow", Variant());
		if (window.get_type() == Variant::INT || window.get_type() == Variant::FLOAT) {
			Dictionary update;
			update["sessionUpdate"] = "usage_update";
			update["used"] = used;
			update["size"] = int64_t(double(window));
			_feed_codex_update(update);
		}
	} else if (p_method == "turn/completed") {
		const Dictionary turn = _ddict(p_params, "turn");
		codex_turn_status = _dstr(turn, "status");
		// The report is the final answer (phase "final_answer"); the
		// progress notes before it ("Voy a consultar...") are not.
		String final_answer;
		const Array items = _darr(turn, "items");
		for (int i = 0; i < items.size(); i++) {
			if (items[i].get_type() != Variant::DICTIONARY) {
				continue;
			}
			const Dictionary item = items[i];
			if (_dstr(item, "type") == "agentMessage" && _dstr(item, "phase") == "final_answer") {
				const String text = _dstr(item, "text").strip_edges();
				if (!text.is_empty()) {
					final_answer += (final_answer.is_empty() ? "" : "\n\n") + text;
				}
			}
		}
		if (!final_answer.is_empty()) {
			codex_final_text = final_answer;
		}
		const Variant error_v = turn.get("error", Variant());
		if (error_v.get_type() == Variant::DICTIONARY) {
			codex_turn_error = _dstr(Dictionary(error_v), "message");
		} else if (error_v.get_type() == Variant::STRING) {
			codex_turn_error = error_v;
		}
		codex_turn_done = true;
	} else if (p_method == "error") {
		// Retries report here too; the turn's own error wins when it ends.
		const Dictionary error = _ddict(p_params, "error");
		const String message = _dstr(error, "message");
		if (!message.is_empty() && !bool(p_params.get("willRetry", false))) {
			codex_turn_error = message;
		}
	}
}

void LagoonicAcpRunner::_handle_codex_request(const Variant &p_id, const String &p_method, const Dictionary &p_params) {
	(void)p_params;
	// Worker turns run with approvalPolicy "never"; answer any approval
	// that still arrives like the autonomous mode does.
	if (p_method.ends_with("/requestApproval") && !(auto_approve || silent)) {
		// Ask mode: the chat's permission card, as for ACP agents.
		Dictionary tool_call;
		String title = _dstr(p_params, "command");
		if (title.is_empty()) {
			title = p_method.contains("fileChange") ? String::utf8("Codex quiere editar archivos.") : String::utf8("Codex pide permiso para continuar.");
		}
		if (!_dstr(p_params, "reason").is_empty()) {
			title += " (" + _dstr(p_params, "reason") + ")";
		}
		tool_call["title"] = title;
		Array options;
		Dictionary allow;
		allow["optionId"] = "accept";
		allow["name"] = "Allow";
		allow["kind"] = "allow_once";
		options.push_back(allow);
		Dictionary deny;
		deny["optionId"] = "decline";
		deny["name"] = "Decline";
		deny["kind"] = "reject_once";
		options.push_back(deny);
		Dictionary params;
		params["toolCall"] = tool_call;
		params["options"] = options;
		_handle_permission(p_id, params);
	} else if (p_method.ends_with("/requestApproval")) {
		Dictionary result;
		result["decision"] = "accept";
		_reply(p_id, result);
	} else if (p_method == "execCommandApproval" || p_method == "applyPatchApproval") {
		Dictionary result;
		result["decision"] = "approved";
		_reply(p_id, result);
	} else {
		_reply_error(p_id, -32601, "Method not supported: " + p_method);
	}
}

Dictionary LagoonicAcpRunner::run_codex_turn(const String &p_codex_bin, const String &p_cwd, const Dictionary &p_env, const Array &p_mcp_servers, const String &p_prompt, const String &p_session_id) {
	return _codex_turn(p_codex_bin, p_cwd, p_env, p_mcp_servers, p_prompt, Array(), true, true, p_session_id);
}

bool LagoonicAcpRunner::_is_codex_app_server(const String &p_bin, const PackedStringArray &p_args) {
	return !p_args.is_empty() && p_args[0] == "app-server" && p_bin.get_file().get_basename() == "codex";
}

Array LagoonicAcpRunner::_codex_options(const Array &p_models, const String &p_model, const String &p_effort) const {
	// Same shape as parse_options(): the chat's model and effort pickers.
	Array options;
	if (p_models.is_empty()) {
		return options;
	}
	Dictionary model_option;
	model_option["id"] = "model";
	model_option["name"] = "Model";
	model_option["category"] = "model";
	model_option["current"] = p_model;
	Array model_values;
	Array efforts;
	for (int i = 0; i < p_models.size(); i++) {
		const Dictionary m = p_models[i];
		Dictionary v;
		v["value"] = m.get("value", "");
		v["name"] = m.get("name", "");
		model_values.push_back(v);
		if (String(m.get("value", "")) == p_model) {
			efforts = m.get("efforts", Array());
		}
	}
	model_option["values"] = model_values;
	options.push_back(model_option);
	if (!efforts.is_empty()) {
		Dictionary effort_option;
		effort_option["id"] = "effort";
		effort_option["name"] = "Effort";
		effort_option["category"] = "thought_level";
		effort_option["current"] = p_effort;
		Array effort_values;
		for (int i = 0; i < efforts.size(); i++) {
			Dictionary v;
			v["value"] = efforts[i];
			v["name"] = String(efforts[i]).capitalize();
			effort_values.push_back(v);
		}
		effort_option["values"] = effort_values;
		options.push_back(effort_option);
	}
	return options;
}

Dictionary LagoonicAcpRunner::_codex_turn(const String &p_codex_bin, const String &p_cwd, const Dictionary &p_env, const Array &p_mcp_servers, const String &p_prompt, const Array &p_prompt_blocks, bool p_auto_approve, bool p_silent, const String &p_session_id) {
	Dictionary out;
	auto_approve = p_auto_approve;
	silent = p_silent;
	codex_dialect = true;
	codex_turn_done = false;
	codex_turn_status = String();
	codex_turn_error = String();
	codex_delta_text = String();
	codex_final_text = String();
	last_error = String();
	session_model = String();
	set_session_id(p_session_id);
	PackedStringArray args;
	args.push_back("app-server");
	if (spawn(p_codex_bin, args, p_cwd, p_env) != OK) {
		codex_dialect = false;
		out["ok"] = false;
		out["error"] = last_error;
		out["log"] = get_log_tail();
		return out;
	}
	if (!silent) {
		mapper->begin_turn();
	}
	const String resume_thread = resume_acp_session;
	const String transcript = resume_transcript;
	resume_acp_session = String();
	resume_transcript = String();
	bool resumed = false;
	String prompt = p_prompt;

	bool ok = false;
	Dictionary client_info;
	client_info["name"] = "lagoonic";
	client_info["title"] = "Lagoonic";
	client_info["version"] = "1";
	Dictionary capabilities;
	capabilities["experimentalApi"] = true;
	Dictionary init_params;
	init_params["clientInfo"] = client_info;
	init_params["capabilities"] = capabilities;
	_call("initialize", init_params, DEFAULT_TIMEOUT_MS, ok);

	String thread_id;
	String requested_model_value;
	if (ok) {
		_notify("initialized", Dictionary());
		// The ACP mcpServers become Codex config overrides
		// (mcp_servers.<name>.command/args/env or .url/.http_headers).
		Dictionary config;
		for (int i = 0; i < p_mcp_servers.size(); i++) {
			if (p_mcp_servers[i].get_type() != Variant::DICTIONARY) {
				continue;
			}
			const Dictionary server = p_mcp_servers[i];
			const String name = _dstr(server, "name");
			if (name.is_empty()) {
				continue;
			}
			const String prefix = "mcp_servers." + name + ".";
			// Lagoonic's own servers: Codex's auto review must not block them.
			// The crew is always allowed; the Godot tools ask only in Ask mode.
			config[prefix + "default_tools_approval_mode"] = (name == "lagoonic-crew" || p_auto_approve || p_silent) ? String("approve") : String("prompt");
			if (server.has("url")) {
				config[prefix + "url"] = _dstr(server, "url");
				Dictionary headers;
				const Array header_list = _darr(server, "headers");
				for (int h = 0; h < header_list.size(); h++) {
					if (header_list[h].get_type() == Variant::DICTIONARY) {
						const Dictionary header = header_list[h];
						headers[_dstr(header, "name")] = _dstr(header, "value");
					}
				}
				if (!headers.is_empty()) {
					config[prefix + "http_headers"] = headers;
				}
			} else {
				config[prefix + "command"] = _dstr(server, "command");
				config[prefix + "args"] = _darr(server, "args");
				Dictionary env;
				const Array env_list = _darr(server, "env");
				for (int e = 0; e < env_list.size(); e++) {
					if (env_list[e].get_type() == Variant::DICTIONARY) {
						const Dictionary entry = env_list[e];
						env[_dstr(entry, "name")] = _dstr(entry, "value");
					}
				}
				if (!env.is_empty()) {
					config[prefix + "env"] = env;
				}
			}
		}
		// Workers act on their own: no approvals, writes inside the
		// project, network for downloads (assets, packages).
		config["sandbox_workspace_write.network_access"] = true;
		// The model the boss asked for, among the ones Codex offers.
		if (!requested_model.strip_edges().is_empty()) {
			offered_models = _codex_model_list();
			const String value = LagoonicModelMatch::best(offered_models, requested_model);
			if (value.is_empty()) {
				_set_error("\"" + requested_model + "\" is not one of Codex's models: " + LagoonicModelMatch::describe(offered_models) + ".");
				ok = false;
			} else {
				Vector<String> words;
				String effort;
				LagoonicModelMatch::split_request(requested_model, words, effort);
				for (int i = 0; i < offered_models.size(); i++) {
					const Dictionary m = offered_models[i];
					if (String(m.get("value", "")) == value && !effort.is_empty() && Array(m.get("efforts", Array())).has(effort)) {
						config["model_reasoning_effort"] = effort;
					}
				}
				requested_model_value = value;
			}
		}
		// The chat's own picks (model, effort), among what Codex offers.
		if (!silent) {
			if (offered_models.is_empty()) {
				offered_models = _codex_model_list();
			}
			const String chosen_model = chosen_options.get("model", "");
			for (int i = 0; i < offered_models.size() && requested_model_value.is_empty(); i++) {
				if (String(Dictionary(offered_models[i]).get("value", "")) == chosen_model) {
					requested_model_value = chosen_model;
				}
			}
			const String chosen_effort = chosen_options.get("effort", "");
			if (!chosen_effort.is_empty()) {
				config["model_reasoning_effort"] = chosen_effort;
			}
			if (requested_mode == "plan") {
				_set_error(String::utf8("Codex has no plan mode here; switch to Automatic or Ask first. / Codex no tiene modo de solo planificar; cambia a Automático o Preguntar antes."));
				ok = false;
			}
		}
		Dictionary thread_params;
		thread_params["cwd"] = p_cwd;
		if (!requested_model_value.is_empty()) {
			thread_params["model"] = requested_model_value;
		}
		// Automatic (and workers): no approvals. Ask: Codex asks before
		// running commands or editing, and the chat shows the request.
		thread_params["approvalPolicy"] = auto_approve || silent ? "never" : "on-request";
		thread_params["sandbox"] = "workspace-write";
		thread_params["config"] = config;
		Dictionary thread_result;
		if (ok && !resume_thread.is_empty()) {
			// The conversation goes on in the same Codex thread.
			Dictionary resume_params = thread_params.duplicate();
			resume_params["threadId"] = resume_thread;
			bool resume_ok = false;
			thread_result = _call("thread/resume", resume_params, DEFAULT_TIMEOUT_MS, resume_ok);
			resumed = resume_ok;
		}
		if (ok && !resumed) {
			thread_result = _call("thread/start", thread_params, DEFAULT_TIMEOUT_MS, ok);
			if (ok && !transcript.is_empty()) {
				prompt = transcript + "\n\n" + p_prompt;
			}
		}
		if (ok) {
			thread_id = _dstr(_ddict(thread_result, "thread"), "id");
			// Codex states the model and effort it will use for this thread.
			String model = _dstr(thread_result, "model");
			const String effort = _dstr(thread_result, "reasoningEffort");
			if (!model.is_empty() && !effort.is_empty()) {
				model += String::utf8(" · ") + effort;
			}
			_set_session_model(model);
			acp_session_id = thread_id;
			if (!silent) {
				// The chat's pickers: Codex's real models (Sol, Astra, Luna...).
				const Array options = _codex_options(offered_models, _dstr(thread_result, "model"), effort);
				if (!options.is_empty()) {
					Dictionary event;
					event["type"] = "acp_options";
					event["session_id"] = session_id;
					event["options"] = options;
					emit_signal("acp_event", event);
				}
			}
			if (thread_id.is_empty()) {
				_set_error("Codex did not open a conversation. / Codex no abrió la conversación.");
				ok = false;
			}
		}
	}
	if (ok) {
		Array input;
		Dictionary block;
		block["type"] = "text";
		block["text"] = prompt;
		input.push_back(block);
		for (int i = 0; i < p_prompt_blocks.size(); i++) {
			// ACP image blocks become app-server image inputs (data URL).
			const Dictionary extra = p_prompt_blocks[i];
			if (_dstr(extra, "type") == "image") {
				Dictionary image;
				image["type"] = "image";
				image["url"] = "data:" + _dstr(extra, "mimeType") + ";base64," + _dstr(extra, "data");
				input.push_back(image);
			}
		}
		if (!silent) {
			Dictionary busy;
			busy["type"] = "busy";
			busy["session_id"] = session_id;
			busy["busy"] = true;
			emit_signal("acp_event", busy);
		}
		Dictionary turn_params;
		turn_params["threadId"] = thread_id;
		turn_params["input"] = input;
		const Dictionary turn_result = _call("turn/start", turn_params, DEFAULT_TIMEOUT_MS, ok);
		codex_turn_id = _dstr(_ddict(turn_result, "turn"), "id");
	}
	if (ok) {
		// turn/start answers at once; the work ends with turn/completed.
		while (!codex_turn_done && !stopped && running) {
			_pump_once();
			// New instructions join the turn in progress (turn/steer).
			const String extra = _take_steer();
			if (!extra.is_empty() && !codex_turn_id.is_empty()) {
				Array steer_input;
				Dictionary steer_block;
				steer_block["type"] = "text";
				steer_block["text"] = extra;
				steer_input.push_back(steer_block);
				Dictionary steer_params;
				steer_params["threadId"] = thread_id;
				steer_params["expectedTurnId"] = codex_turn_id;
				steer_params["input"] = steer_input;
				bool steer_ok = false;
				_call("turn/steer", steer_params, DEFAULT_TIMEOUT_MS, steer_ok);
				if (!steer_ok) {
					steer(extra); // Retry on the next pass.
				}
			}
			OS::get_singleton()->delay_usec(2000);
		}
		if (stopped) {
			_set_error("Stopped by user. / Detenido por el usuario.");
			ok = false;
		} else if (!codex_turn_done) {
			if (last_error.is_empty()) {
				_set_error("Codex stopped before finishing. / Codex se detuvo antes de terminar.");
			}
			ok = false;
		} else if (codex_turn_status != "completed") {
			_set_error(codex_turn_error.is_empty() ? ("Codex turn " + codex_turn_status + ".") : codex_turn_error);
			ok = false;
		}
	}
	if (!silent) {
		Dictionary idle;
		idle["type"] = "busy";
		idle["session_id"] = session_id;
		idle["busy"] = false;
		emit_signal("acp_event", idle);
	}
	const String text = silent ? (codex_final_text.is_empty() ? codex_delta_text.strip_edges() : codex_final_text) : mapper->get_text();
	out["ok"] = ok;
	out["text"] = text;
	out["report"] = silent ? text : mapper->get_worker_report(3);
	out["model"] = session_model;
	out["models"] = offered_models;
	out["acp_session_id"] = thread_id;
	out["resumed"] = resumed;
	if (!codex_turn_status.is_empty()) {
		out["stop_reason"] = codex_turn_status == "completed" ? String("end_turn") : codex_turn_status;
	}
	if (!ok) {
		out["error"] = last_error;
	}
	out["log"] = get_log_tail();
	close();
	codex_dialect = false;
	return out;
}

// --- accessors --------------------------------------------------------

void LagoonicAcpRunner::set_session_id(const String &p_session_id) {
	if (!p_session_id.is_empty()) {
		session_id = p_session_id;
		mapper->set_session_id(session_id);
	}
}

String LagoonicAcpRunner::get_session_id() const {
	return session_id;
}

void LagoonicAcpRunner::set_auto_approve(bool p_auto_approve) {
	auto_approve = p_auto_approve;
}

bool LagoonicAcpRunner::get_auto_approve() const {
	return auto_approve;
}

void LagoonicAcpRunner::set_silent(bool p_silent) {
	silent = p_silent;
}

bool LagoonicAcpRunner::get_silent() const {
	return silent;
}

String LagoonicAcpRunner::get_acp_session_id() const {
	return acp_session_id;
}

String LagoonicAcpRunner::get_log_tail() const {
	String out;
	for (int i = 0; i < stderr_log.size(); i++) {
		if (i > 0) {
			out += "\n";
		}
		out += stderr_log[i];
	}
	return out;
}

String LagoonicAcpRunner::get_last_error() const {
	return last_error;
}

String LagoonicAcpRunner::get_turn_text() const {
	return mapper->get_text();
}

String LagoonicAcpRunner::get_worker_report(int p_max_sentences) const {
	return mapper->get_worker_report(p_max_sentences);
}

Ref<LagoonicAcpMapper> LagoonicAcpRunner::get_mapper() const {
	return mapper;
}
