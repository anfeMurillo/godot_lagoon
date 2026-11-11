/**************************************************************************/
/*  lagoonic_external_agents.cpp                                      */
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

#include "lagoonic_external_agents.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"

#ifndef WINDOWS_ENABLED
#include <sys/stat.h>
#endif

static const char *CODEX_SECTION = "[mcp_servers.lagoonic]";
static const char *VIBE_HEADER = "[[mcp_servers]]";
static const char *VIBE_TRANSPORT_LINE = "transport = \"stdio\"";

LagoonicMcpTarget LagoonicExternalAgents::make_mcp_target(const String &p_project_dir, const String &p_godot_path, const String &p_mcp_entry) {
	LagoonicMcpTarget target;
	target.command = OS::get_singleton()->get_executable_path();
	if (!p_mcp_entry.is_empty()) {
		target.args.push_back(p_mcp_entry);
	}
	target.args.push_back("--project");
	target.args.push_back(p_project_dir);
	target.env["GODOT_PATH"] = p_godot_path;
	target.env["LAGOONIC_PLATFORM"] = "desktop";
	return target;
}

String LagoonicExternalAgents::user_config_path(const Vector<String> &p_parts) {
	const String home = LagoonicCodingAgents::home_dir();
	Vector<String> parts = p_parts;
	if (LagoonicCodingAgents::is_windows() && !parts.is_empty() && parts[0] == ".config") {
		parts.remove_at(0);
		String path = home.path_join("AppData").path_join("Roaming");
		for (int i = 0; i < parts.size(); i++) {
			path = path.path_join(parts[i]);
		}
		return path;
	}
	String path = home;
	for (int i = 0; i < parts.size(); i++) {
		path = path.path_join(parts[i]);
	}
	return path;
}

String LagoonicExternalAgents::json_config_path(const LagoonicCodingAgents::AgentInfo &p_agent) {
	Vector<String> parts;
	if (p_agent.config_dir_0 != nullptr) {
		parts.push_back(String(p_agent.config_dir_0));
	}
	if (p_agent.config_dir_1 != nullptr) {
		parts.push_back(String(p_agent.config_dir_1));
	}
	parts.push_back(String(p_agent.file_name));
	return user_config_path(parts);
}

String LagoonicExternalAgents::project_config_path(const String &p_project_dir) {
	return p_project_dir.path_join(".mcp.json");
}

String LagoonicExternalAgents::codex_config_path() {
	return LagoonicCodingAgents::home_dir().path_join(".codex").path_join("config.toml");
}

String LagoonicExternalAgents::vibe_config_path() {
	return LagoonicCodingAgents::home_dir().path_join(".vibe").path_join("config.toml");
}

String LagoonicExternalAgents::wrapper_path(const String &p_project_dir) {
	return p_project_dir.path_join(".lagoonic").path_join(LagoonicCodingAgents::cli_wrapper_name());
}

bool LagoonicExternalAgents::read_json_file(const String &p_path, Dictionary &r_out, String &r_error) {
	r_out.clear();
	if (!FileAccess::exists(p_path)) {
		return true;
	}
	Error err = OK;
	const String text = FileAccess::get_file_as_string(p_path, &err);
	if (err != OK) {
		r_error = "Could not read " + p_path + "; left untouched";
		return false;
	}
	const Variant parsed = JSON::parse_string(text);
	if (parsed.get_type() != Variant::DICTIONARY) {
		r_error = "Could not parse " + p_path + "; left untouched";
		return false;
	}
	r_out = parsed;
	return true;
}

bool LagoonicExternalAgents::write_json_file(const String &p_path, const Dictionary &p_value, String &r_error) {
	const String dir = p_path.get_base_dir();
	if (!dir.is_empty()) {
		DirAccess::make_dir_recursive_absolute(dir);
	}
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
	if (file.is_null()) {
		r_error = "Could not write " + p_path;
		return false;
	}
	file->store_string(JSON::stringify(p_value, "  ") + "\n");
	return true;
}

bool LagoonicExternalAgents::read_text_file(const String &p_path, String &r_out) {
	if (!FileAccess::exists(p_path)) {
		r_out = "";
		return true;
	}
	Error err = OK;
	r_out = FileAccess::get_file_as_string(p_path, &err);
	return err == OK;
}

bool LagoonicExternalAgents::write_text_file(const String &p_path, const String &p_text, String &r_error) {
	const String dir = p_path.get_base_dir();
	if (!dir.is_empty()) {
		DirAccess::make_dir_recursive_absolute(dir);
	}
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
	if (file.is_null()) {
		r_error = "Could not write " + p_path;
		return false;
	}
	file->store_string(p_text);
	return true;
}

int LagoonicExternalAgents::run_agent_command(const Vector<String> &p_argv, const String &p_cwd, int &r_exit_code) {
	r_exit_code = -1;
	if (p_argv.is_empty()) {
		return ERR_INVALID_PARAMETER;
	}
	List<String> args;
	for (int i = 1; i < p_argv.size(); i++) {
		args.push_back(p_argv[i]);
	}
	// Best effort: inherits the fork's environment. A non-zero exit means the
	// agent refused our server; the caller turns that into a friendly error.
	return OS::get_singleton()->execute(p_argv[0], args, nullptr, &r_exit_code);
}

bool LagoonicExternalAgents::is_toml_header(const String &p_trimmed_line) {
	return p_trimmed_line.begins_with("[") && p_trimmed_line.ends_with("]");
}

String LagoonicExternalAgents::collapse_blank_lines(const String &p_text) {
	String out = p_text;
	while (out.contains("\n\n\n")) {
		out = out.replace("\n\n\n", "\n\n");
	}
	return out;
}

static String _vibe_name_line() {
	return String("name = ") + LagoonicCodingAgents::json_quote(LagoonicExternalAgents::MCP_NAME);
}

static String _vibe_block(const LagoonicMcpTarget &p_mcp, const String &p_project_dir) {
	const Vector<String> args = LagoonicCodingAgents::mcp_args_for_project(p_mcp.args, p_project_dir);
	Vector<String> words;
	words.push_back(p_mcp.command);
	for (int i = 0; i < args.size(); i++) {
		words.push_back(args[i]);
	}
	Vector<String> lines;
	lines.push_back(VIBE_HEADER);
	lines.push_back(_vibe_name_line());
	lines.push_back(VIBE_TRANSPORT_LINE);
	lines.push_back(String("command = ") + LagoonicCodingAgents::json_quote(String(" ").join(words)));
	return String("\n").join(lines);
}

String LagoonicExternalAgents::merge_codex_toml(const String &p_existing, const LagoonicMcpTarget &p_mcp, const String &p_project_dir) {
	const Vector<String> args = LagoonicCodingAgents::mcp_args_for_project(p_mcp.args, p_project_dir);
	Vector<String> quoted_args;
	for (int i = 0; i < args.size(); i++) {
		quoted_args.push_back(LagoonicCodingAgents::json_quote(args[i]));
	}
	Vector<String> env_pairs;
	const Array keys = p_mcp.env.keys();
	for (int i = 0; i < keys.size(); i++) {
		const String key = keys[i];
		const String value = p_mcp.env[key];
		env_pairs.push_back(LagoonicCodingAgents::json_quote(key) + " = " + LagoonicCodingAgents::json_quote(value));
	}
	Vector<String> block;
	block.push_back(CODEX_SECTION);
	block.push_back(String("command = ") + LagoonicCodingAgents::json_quote(p_mcp.command));
	block.push_back("args = [" + String(", ").join(quoted_args) + "]");
	block.push_back("env = { " + String(", ").join(env_pairs) + " }");
	const String block_text = String("\n").join(block);

	const Vector<String> lines = p_existing.split("\n", true);
	int start = -1;
	for (int i = 0; i < lines.size(); i++) {
		if (lines[i].strip_edges() == CODEX_SECTION) {
			start = i;
			break;
		}
	}
	if (start == -1) {
		String prefix = (p_existing.ends_with("\n") || p_existing.is_empty()) ? p_existing : p_existing + "\n";
		String out = prefix;
		if (!prefix.is_empty() && !prefix.ends_with("\n\n")) {
			out += "\n";
		}
		return out + block_text + "\n";
	}
	int end = lines.size();
	for (int i = start + 1; i < lines.size(); i++) {
		if (is_toml_header(lines[i].strip_edges())) {
			end = i;
			break;
		}
	}
	Vector<String> next;
	for (int i = 0; i < start; i++) {
		next.push_back(lines[i]);
	}
	next.push_back(block_text);
	for (int i = end; i < lines.size(); i++) {
		next.push_back(lines[i]);
	}
	return collapse_blank_lines(String("\n").join(next)).strip_edges() + "\n";
}

String LagoonicExternalAgents::remove_codex_toml(const String &p_existing) {
	const Vector<String> lines = p_existing.split("\n", true);
	int start = -1;
	for (int i = 0; i < lines.size(); i++) {
		if (lines[i].strip_edges() == CODEX_SECTION) {
			start = i;
			break;
		}
	}
	if (start == -1) {
		return p_existing;
	}
	int end = lines.size();
	for (int i = start + 1; i < lines.size(); i++) {
		if (is_toml_header(lines[i].strip_edges())) {
			end = i;
			break;
		}
	}
	Vector<String> next;
	for (int i = 0; i < start; i++) {
		next.push_back(lines[i]);
	}
	for (int i = end; i < lines.size(); i++) {
		next.push_back(lines[i]);
	}
	return collapse_blank_lines(String("\n").join(next)).strip_edges() + "\n";
}

String LagoonicExternalAgents::merge_vibe_toml(const String &p_existing, const LagoonicMcpTarget &p_mcp, const String &p_project_dir) {
	const Vector<String> lines = p_existing.split("\n", true);
	for (int i = 0; i < lines.size(); i++) {
		if (lines[i].strip_edges() == _vibe_name_line()) {
			return p_existing;
		}
	}
	String prefix = (p_existing.ends_with("\n") || p_existing.is_empty()) ? p_existing : p_existing + "\n";
	String out = prefix;
	if (!prefix.is_empty() && !prefix.ends_with("\n\n")) {
		out += "\n";
	}
	return out + _vibe_block(p_mcp, p_project_dir) + "\n";
}

String LagoonicExternalAgents::remove_vibe_toml(const String &p_existing) {
	const Vector<String> lines = p_existing.split("\n", true);
	int first_header = -1;
	for (int i = 0; i < lines.size(); i++) {
		if (lines[i].strip_edges() == VIBE_HEADER) {
			first_header = i;
			break;
		}
	}
	if (first_header == -1) {
		return p_existing;
	}
	// Walk every [[mcp_servers]] block; drop only the one naming lagoonic.
	Vector<String> kept;
	bool dropped = false;
	int i = 0;
	// Preamble before the first header is always kept verbatim.
	while (i < first_header) {
		kept.push_back(lines[i]);
		i++;
	}
	while (i < lines.size()) {
		if (lines[i].strip_edges() == VIBE_HEADER) {
			int end = i + 1;
			while (end < lines.size() && lines[end].strip_edges() != VIBE_HEADER) {
				end++;
			}
			bool is_lagoonic = false;
			for (int k = i; k < end; k++) {
				if (lines[k].strip_edges() == _vibe_name_line()) {
					is_lagoonic = true;
					break;
				}
			}
			if (is_lagoonic) {
				dropped = true;
			} else {
				for (int k = i; k < end; k++) {
					kept.push_back(lines[k]);
				}
			}
			i = end;
		} else {
			kept.push_back(lines[i]);
			i++;
		}
	}
	if (!dropped) {
		return p_existing;
	}
	return collapse_blank_lines(String("\n").join(kept)).strip_edges() + "\n";
}

bool LagoonicExternalAgents::get_key_path(const Dictionary &p_root, const String &p_key_root, Variant &r_out) {
	if (!p_root.has(p_key_root)) {
		return false;
	}
	const Variant sub = p_root[p_key_root];
	if (sub.get_type() != Variant::DICTIONARY) {
		return false;
	}
	const Dictionary inner = sub;
	if (!inner.has(MCP_NAME)) {
		return false;
	}
	r_out = inner[MCP_NAME];
	return true;
}

void LagoonicExternalAgents::set_key_path(Dictionary &r_root, const String &p_key_root, const Variant &p_value) {
	Dictionary inner;
	if (r_root.has(p_key_root) && ((Variant)r_root[p_key_root]).get_type() == Variant::DICTIONARY) {
		inner = r_root[p_key_root];
	}
	inner[MCP_NAME] = p_value;
	r_root[p_key_root] = inner;
}

bool LagoonicExternalAgents::remove_key_path(Dictionary &r_root, const String &p_key_root) {
	if (!r_root.has(p_key_root)) {
		return false;
	}
	if (((Variant)r_root[p_key_root]).get_type() != Variant::DICTIONARY) {
		return false;
	}
	Dictionary inner = r_root[p_key_root];
	if (!inner.has(MCP_NAME)) {
		return false;
	}
	inner.erase(MCP_NAME);
	r_root[p_key_root] = inner;
	return true;
}

bool LagoonicExternalAgents::can_connect(const LagoonicCodingAgents::AgentInfo &p_agent, bool p_has_project) {
	switch (p_agent.registration) {
		case LagoonicCodingAgents::REG_JSON:
		case LagoonicCodingAgents::REG_CODEX_TOML:
		case LagoonicCodingAgents::REG_VIBE_TOML:
			return true;
		case LagoonicCodingAgents::REG_PROJECT_JSON:
		case LagoonicCodingAgents::REG_CLI:
			return p_has_project;
		case LagoonicCodingAgents::REG_NONE:
			return false;
	}
	return false;
}

bool LagoonicExternalAgents::has_config(const LagoonicCodingAgents::AgentInfo &p_agent, const String &p_project_dir) {
	switch (p_agent.registration) {
		case LagoonicCodingAgents::REG_JSON:
			return FileAccess::exists(json_config_path(p_agent));
		case LagoonicCodingAgents::REG_PROJECT_JSON:
			return !p_project_dir.is_empty() && FileAccess::exists(project_config_path(p_project_dir));
		case LagoonicCodingAgents::REG_CODEX_TOML:
			return FileAccess::exists(codex_config_path());
		case LagoonicCodingAgents::REG_VIBE_TOML:
			return FileAccess::exists(vibe_config_path());
		case LagoonicCodingAgents::REG_CLI:
		case LagoonicCodingAgents::REG_NONE:
			return false;
	}
	return false;
}

bool LagoonicExternalAgents::is_connected(const LagoonicCodingAgents::AgentInfo &p_agent, const String &p_project_dir) {
	String error;
	switch (p_agent.registration) {
		case LagoonicCodingAgents::REG_JSON: {
			const String path = json_config_path(p_agent);
			if (!FileAccess::exists(path)) {
				return false;
			}
			Dictionary root;
			if (!read_json_file(path, root, error)) {
				return false;
			}
			if (String(p_agent.id) == "deepseek") {
				Variant out;
				if (get_key_path(root, "mcpServers", out) || get_key_path(root, "servers", out)) {
					return true;
				}
				return false;
			}
			Variant out;
			return get_key_path(root, String(p_agent.key_root), out);
		}
		case LagoonicCodingAgents::REG_PROJECT_JSON: {
			if (p_project_dir.is_empty()) {
				return false;
			}
			const String path = project_config_path(p_project_dir);
			if (!FileAccess::exists(path)) {
				return false;
			}
			Dictionary root;
			if (!read_json_file(path, root, error)) {
				return false;
			}
			Variant out;
			return get_key_path(root, String(p_agent.key_root), out);
		}
		case LagoonicCodingAgents::REG_CODEX_TOML: {
			const String path = codex_config_path();
			if (!FileAccess::exists(path)) {
				return false;
			}
			String text;
			if (!read_text_file(path, text)) {
				return false;
			}
			const Vector<String> lines = text.split("\n", true);
			for (int i = 0; i < lines.size(); i++) {
				if (lines[i].strip_edges() == CODEX_SECTION) {
					return true;
				}
			}
			return false;
		}
		case LagoonicCodingAgents::REG_VIBE_TOML: {
			const String path = vibe_config_path();
			if (!FileAccess::exists(path)) {
				return false;
			}
			String text;
			if (!read_text_file(path, text)) {
				return false;
			}
			const Vector<String> lines = text.split("\n", true);
			for (int i = 0; i < lines.size(); i++) {
				if (lines[i].strip_edges() == _vibe_name_line()) {
					return true;
				}
			}
			return false;
		}
		case LagoonicCodingAgents::REG_CLI:
		case LagoonicCodingAgents::REG_NONE:
			// The agent owns its config; Lagoonic cannot read it back.
			return false;
	}
	return false;
}

Array LagoonicExternalAgents::list_agents(const String &p_project_dir) {
	Array out;
	const String npm_prefix = LagoonicCodingAgents::npm_global_prefix();
	const Vector<String> dirs = LagoonicCodingAgents::host_search_dirs(npm_prefix);
	const bool has_project = !p_project_dir.is_empty();
	for (int i = 0; i < LagoonicCodingAgents::AGENT_COUNT; i++) {
		const LagoonicCodingAgents::AgentInfo &agent = *LagoonicCodingAgents::get_agent_at(i);
		const bool installed = LagoonicCodingAgents::is_agent_installed(agent, dirs);
		Dictionary entry;
		entry["id"] = String(agent.id);
		entry["name"] = String(agent.name);
		entry["vendor"] = String(agent.vendor);
		entry["built_in"] = agent.built_in;
		entry["detected"] = installed || has_config(agent, p_project_dir);
		entry["installed"] = installed;
		entry["connected"] = is_connected(agent, p_project_dir);
		entry["docs_url"] = String(agent.docs_url);
		entry["install"] = String(agent.install);
		entry["run"] = String(agent.run);
		entry["has_chat"] = LagoonicCodingAgents::is_chat_ready(agent, dirs);
		entry["chat_install"] = agent.acp_install != nullptr ? String(agent.acp_install) : String();
		entry["can_connect"] = can_connect(agent, has_project);
		out.push_back(entry);
	}
	return out;
}

bool LagoonicExternalAgents::connect_agent(const String &p_id, const String &p_project_dir, const LagoonicMcpTarget &p_mcp, String &r_error) {
	const LagoonicCodingAgents::AgentInfo *agent = LagoonicCodingAgents::get_agent(p_id);
	if (agent == nullptr) {
		r_error = "Unknown coding agent: " + p_id;
		return false;
	}
	const String dir = p_project_dir;
	switch (agent->registration) {
		case LagoonicCodingAgents::REG_JSON: {
			const String path = json_config_path(*agent);
			Dictionary root;
			if (!read_json_file(path, root, r_error)) {
				return false;
			}
			set_key_path(root, String(agent->key_root),
					LagoonicCodingAgents::json_entry(*agent, p_mcp.command, p_mcp.args, dir, p_mcp.env));
			return write_json_file(path, root, r_error);
		}
		case LagoonicCodingAgents::REG_PROJECT_JSON: {
			if (p_project_dir.is_empty()) {
				r_error = String("Open a project first to connect ") + String(agent->name) + "; its config lives in the project folder.";
				return false;
			}
			const String path = project_config_path(p_project_dir);
			Dictionary root;
			if (!read_json_file(path, root, r_error)) {
				return false;
			}
			set_key_path(root, String(agent->key_root),
					LagoonicCodingAgents::json_entry(*agent, p_mcp.command, p_mcp.args, p_project_dir, p_mcp.env));
			return write_json_file(path, root, r_error);
		}
		case LagoonicCodingAgents::REG_CODEX_TOML: {
			const String path = codex_config_path();
			String existing;
			if (!read_text_file(path, existing)) {
				r_error = "Could not read " + path;
				return false;
			}
			return write_text_file(path, merge_codex_toml(existing, p_mcp, dir), r_error);
		}
		case LagoonicCodingAgents::REG_VIBE_TOML: {
			const String path = vibe_config_path();
			String existing;
			if (!read_text_file(path, existing)) {
				r_error = "Could not read " + path;
				return false;
			}
			return write_text_file(path, merge_vibe_toml(existing, p_mcp, dir), r_error);
		}
		case LagoonicCodingAgents::REG_CLI: {
			if (p_project_dir.is_empty()) {
				r_error = String("Open a project first to connect ") + String(agent->name) + "; its wrapper script lives in the project folder.";
				return false;
			}
			const String wrapper = wrapper_path(p_project_dir);
			const Vector<String> args = LagoonicCodingAgents::mcp_args_for_project(p_mcp.args, p_project_dir);
			if (!write_text_file(wrapper,
						LagoonicCodingAgents::cli_wrapper_content(p_mcp.command, args, p_mcp.env),
						r_error)) {
				return false;
			}
#ifndef WINDOWS_ENABLED
			::chmod(wrapper.utf8().get_data(), 0755);
#endif
			int exit_code = -1;
			if (run_agent_command(LagoonicCodingAgents::cli_add_argv(*agent, wrapper), p_project_dir, exit_code) != OK) {
				r_error = String("Cannot run ") + LagoonicCodingAgents::agent_binary(*agent);
				return false;
			}
			if (exit_code != 0) {
				r_error = String(agent->name) + " refused the lagoonic server (exit " + itos(exit_code) + ").";
				return false;
			}
			return true;
		}
		case LagoonicCodingAgents::REG_NONE:
			r_error = String(agent->name) + " loads lagoonic-mcp on its own; nothing to connect.";
			return false;
	}
	r_error = "Unknown registration for " + p_id;
	return false;
}

bool LagoonicExternalAgents::disconnect_agent(const String &p_id, const String &p_project_dir, String &r_error) {
	const LagoonicCodingAgents::AgentInfo *agent = LagoonicCodingAgents::get_agent(p_id);
	if (agent == nullptr) {
		r_error = "Unknown coding agent: " + p_id;
		return false;
	}
	switch (agent->registration) {
		case LagoonicCodingAgents::REG_JSON: {
			const String path = json_config_path(*agent);
			if (!FileAccess::exists(path)) {
				return true;
			}
			Dictionary root;
			if (!read_json_file(path, root, r_error)) {
				return false;
			}
			bool removed = false;
			if (String(agent->id) == "deepseek") {
				removed = remove_key_path(root, "mcpServers") || remove_key_path(root, "servers");
			} else {
				removed = remove_key_path(root, String(agent->key_root));
			}
			if (!removed) {
				return true;
			}
			return write_json_file(path, root, r_error);
		}
		case LagoonicCodingAgents::REG_PROJECT_JSON: {
			if (!p_project_dir.is_empty()) {
				const String path = project_config_path(p_project_dir);
				if (FileAccess::exists(path)) {
					Dictionary root;
					if (!read_json_file(path, root, r_error)) {
						return false;
					}
					if (remove_key_path(root, String(agent->key_root)) && !write_json_file(path, root, r_error)) {
						return false;
					}
				}
			}
			return true;
		}
		case LagoonicCodingAgents::REG_CODEX_TOML: {
			const String path = codex_config_path();
			if (FileAccess::exists(path)) {
				String text;
				if (!read_text_file(path, text)) {
					r_error = "Could not read " + path;
					return false;
				}
				if (!write_text_file(path, remove_codex_toml(text), r_error)) {
					return false;
				}
			}
			return true;
		}
		case LagoonicCodingAgents::REG_VIBE_TOML: {
			const String path = vibe_config_path();
			if (FileAccess::exists(path)) {
				String text;
				if (!read_text_file(path, text)) {
					r_error = "Could not read " + path;
					return false;
				}
				if (!write_text_file(path, remove_vibe_toml(text), r_error)) {
					return false;
				}
			}
			return true;
		}
		case LagoonicCodingAgents::REG_CLI: {
			int exit_code = -1;
			if (run_agent_command(LagoonicCodingAgents::cli_remove_argv(*agent), p_project_dir, exit_code) != OK) {
				r_error = String("Cannot run ") + LagoonicCodingAgents::agent_binary(*agent);
				return false;
			}
			if (exit_code != 0) {
				r_error = String(agent->name) + " refused to disconnect (exit " + itos(exit_code) + ").";
				return false;
			}
			return true;
		}
		case LagoonicCodingAgents::REG_NONE:
			return true;
	}
	return true;
}

LagoonicConnectReport LagoonicExternalAgents::connect_installed(const String &p_project_dir, const LagoonicMcpTarget &p_mcp) {
	LagoonicConnectReport report;
	const String npm_prefix = LagoonicCodingAgents::npm_global_prefix();
	const Vector<String> dirs = LagoonicCodingAgents::host_search_dirs(npm_prefix);
	for (int i = 0; i < LagoonicCodingAgents::AGENT_COUNT; i++) {
		const LagoonicCodingAgents::AgentInfo &agent = *LagoonicCodingAgents::get_agent_at(i);
		if (!LagoonicCodingAgents::is_agent_installed(agent, dirs)) {
			continue;
		}
		if (!can_connect(agent, true)) {
			continue;
		}
		if (is_connected(agent, p_project_dir)) {
			continue;
		}
		if (agent.registration == LagoonicCodingAgents::REG_CLI ||
				agent.registration == LagoonicCodingAgents::REG_NONE) {
			continue;
		}
		String error;
		if (connect_agent(String(agent.id), p_project_dir, p_mcp, error)) {
			report.connected.push_back(String(agent.id));
		} else {
			report.failed[String(agent.id)] = error;
		}
	}
	return report;
}

String LagoonicExternalAgents::godot_minimum_version() {
	return String::num_int64(GODOT_MIN_MAJOR) + "." + String::num_int64(GODOT_MIN_MINOR);
}

Vector<String> LagoonicExternalAgents::engine_godot_names() {
	Vector<String> names;
	if (LagoonicCodingAgents::is_windows()) {
		names.push_back("godot.windows.editor.x86_64.exe");
		names.push_back("godot.windows.editor.arm64.exe");
		names.push_back("godot.windows.editor.double.x86_64.exe");
		names.push_back("godot.exe");
	} else if (OS::get_singleton()->get_name() == "macOS") {
		names.push_back("godot.macos.editor.arm64");
		names.push_back("godot.macos.editor.x86_64");
		names.push_back("godot.macos.editor.universal");
		names.push_back("godot.macos.editor.double.arm64");
		names.push_back("godot.macos.editor.double.x86_64");
		names.push_back("godot");
	} else {
		names.push_back("godot.linuxbsd.editor.x86_64");
		names.push_back("godot.linuxbsd.editor.arm64");
		names.push_back("godot.linuxbsd.editor.double.x86_64");
		names.push_back("godot");
	}
	return names;
}

String LagoonicExternalAgents::normalize_godot_path(const String &p_raw) {
	String trimmed = p_raw.strip_edges();
	while (trimmed.ends_with("/")) {
		trimmed = trimmed.substr(0, trimmed.length() - 1);
	}
	if (trimmed.to_lower().ends_with(".app")) {
		return trimmed.path_join("Contents").path_join("MacOS").path_join("Godot");
	}
	return trimmed;
}

String LagoonicExternalAgents::bundled_godot_bin(const String &p_runtime_root) {
	if (LagoonicCodingAgents::is_windows()) {
		return p_runtime_root.path_join("godot").path_join("godot.exe");
	}
	if (OS::get_singleton()->get_name() == "macOS") {
		return p_runtime_root.path_join("godot").path_join("Godot.app").path_join("Contents").path_join("MacOS").path_join("Godot");
	}
	return p_runtime_root.path_join("godot").path_join("godot");
}

void LagoonicExternalAgents::collect_existing(const Vector<String> &p_candidates, Vector<String> &r_out) {
	for (int i = 0; i < p_candidates.size(); i++) {
		if (!p_candidates[i].is_empty() && !r_out.has(p_candidates[i])) {
			r_out.push_back(p_candidates[i]);
		}
	}
}

Vector<String> LagoonicExternalAgents::collect_godot_candidates(const String &p_bundled) {
	Vector<String> ordered;
	// 1. env: LAGOONIC_GODOT_BIN wins when set (file, .app bundle, or folder).
	const String env_raw = OS::get_singleton()->get_environment(GODOT_BIN_ENV).strip_edges();
	if (!env_raw.is_empty()) {
		const String base = normalize_godot_path(env_raw);
		Vector<String> env_candidates;
		env_candidates.push_back(base);
		const Vector<String> names = engine_godot_names();
		for (int i = 0; i < names.size(); i++) {
			env_candidates.push_back(base.path_join(names[i]));
		}
		collect_existing(env_candidates, ordered);
	}
	// 2. engine: our compiled build next to the running fork (Engine/godot/bin
	// layout in development, <exe>/bin once packaged).
	const String exe_dir = OS::get_singleton()->get_executable_path().get_base_dir();
	const Vector<String> engine_names = engine_godot_names();
	Vector<String> engine_candidates;
	for (int i = 0; i < engine_names.size(); i++) {
		engine_candidates.push_back(exe_dir.path_join(engine_names[i]));
		engine_candidates.push_back(exe_dir.path_join("bin").path_join(engine_names[i]));
	}
	collect_existing(engine_candidates, ordered);
	// 3. bundled: the official Godot shipped with Lagoonic.
	if (!p_bundled.is_empty()) {
		ordered.push_back(p_bundled);
	}
	// 4. system: official Godot on PATH, plus the usual macOS bundle.
	const String npm_prefix = LagoonicCodingAgents::npm_global_prefix();
	const Vector<String> dirs = LagoonicCodingAgents::host_search_dirs(npm_prefix);
	const String exe = LagoonicCodingAgents::is_windows() ? "godot.exe" : "godot";
	Vector<String> system_candidates;
	for (int i = 0; i < dirs.size(); i++) {
		system_candidates.push_back(dirs[i].path_join(exe));
	}
	if (OS::get_singleton()->get_name() == "macOS") {
		system_candidates.push_back("/Applications/Godot.app/Contents/MacOS/Godot");
		const String home = LagoonicCodingAgents::home_dir();
		if (!home.is_empty()) {
			system_candidates.push_back(home.path_join("Applications").path_join("Godot.app").path_join("Contents").path_join("MacOS").path_join("Godot"));
		}
	}
	collect_existing(system_candidates, ordered);
	return ordered;
}

String LagoonicExternalAgents::preferred_godot_bin(const String &p_bundled) {
	const Vector<String> candidates = collect_godot_candidates(p_bundled);
	for (int i = 0; i < candidates.size(); i++) {
		if (FileAccess::exists(candidates[i])) {
			return candidates[i];
		}
	}
	return p_bundled;
}

String LagoonicExternalAgents::godot_version_output(const String &p_bin) {
	if (p_bin.is_empty() || !FileAccess::exists(p_bin)) {
		return String();
	}
	List<String> args;
	args.push_back("--version");
	String out;
	int exit_code = -1;
	if (OS::get_singleton()->execute(p_bin, args, &out, &exit_code) != OK || exit_code != 0) {
		return String();
	}
	return out.strip_edges();
}

LagoonicGodotVersion LagoonicExternalAgents::parse_godot_version(const String &p_output) {
	LagoonicGodotVersion version;
	const CharString bytes = p_output.utf8();
	const int len = bytes.length();
	int i = 0;
	while (i < len && (bytes[i] < '0' || bytes[i] > '9')) {
		i++;
	}
	if (i >= len) {
		return version;
	}
	int major = 0;
	while (i < len && bytes[i] >= '0' && bytes[i] <= '9') {
		major = major * 10 + (bytes[i] - '0');
		i++;
	}
	if (i >= len || bytes[i] != '.') {
		return version;
	}
	i++;
	if (i >= len || bytes[i] < '0' || bytes[i] > '9') {
		return version;
	}
	int minor = 0;
	while (i < len && bytes[i] >= '0' && bytes[i] <= '9') {
		minor = minor * 10 + (bytes[i] - '0');
		i++;
	}
	int patch = 0;
	if (i < len && bytes[i] == '.') {
		i++;
		while (i < len && bytes[i] >= '0' && bytes[i] <= '9') {
			patch = patch * 10 + (bytes[i] - '0');
			i++;
		}
	}
	version.major = major;
	version.minor = minor;
	version.patch = patch;
	version.valid = true;
	return version;
}

bool LagoonicExternalAgents::meets_godot_minimum(const LagoonicGodotVersion &p_version) {
	if (p_version.major != GODOT_MIN_MAJOR) {
		return p_version.major > GODOT_MIN_MAJOR;
	}
	return p_version.minor >= GODOT_MIN_MINOR;
}

int LagoonicExternalAgents::is_supported_godot_version(const String &p_output) {
	const LagoonicGodotVersion version = parse_godot_version(p_output);
	if (!version.valid) {
		return 0;
	}
	return meets_godot_minimum(version) ? 1 : -1;
}

String LagoonicExternalAgents::missing_godot_message(const Vector<String> &p_tried) {
	String places = "its usual places";
	if (!p_tried.is_empty()) {
		Vector<String> first;
		for (int i = 0; i < p_tried.size() && i < 4; i++) {
			first.push_back(p_tried[i]);
		}
		places = String(", ").join(first);
	}
	return "Lagoonic could not find a Godot engine (looked in " + places + "). " +
			"Install the official Godot " + godot_minimum_version() + " or newer and it will be used as before, " +
			"or point Lagoonic at your own build with " + String(GODOT_BIN_ENV) + "=/path/to/godot. [runtime_unavailable]";
}

String LagoonicExternalAgents::too_old_godot_message(const String &p_found, const String &p_bin) {
	return "That Godot is too old (version " + p_found + ", Lagoonic needs " + godot_minimum_version() +
			" or newer) at " + p_bin + ". Install the official Godot " + godot_minimum_version() +
			", or set " + String(GODOT_BIN_ENV) + " to a newer build. [runtime_unavailable]";
}
