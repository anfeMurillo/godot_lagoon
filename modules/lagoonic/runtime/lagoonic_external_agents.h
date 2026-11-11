/**************************************************************************/
/*  lagoonic_external_agents.h                                        */
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

// Native port of Core/lagoonic-desktop/src/main/external-agents.ts and
// Core/lagoonic-desktop/src/main/godot-bin.ts.
//
// "Connect to…" for the agent the user already has: registers the bundled
// lagoonic-mcp with any of the 19 coding agents through its own CLI command
// or by merging its config file — never replacing it — and keeps every other
// entry. Lagoonic never touches the agents' credentials; it only adds or
// removes its own `lagoonic` MCP server.
//
// Plus the Godot binary search (preferredGodotBin): LAGOONIC_GODOT_BIN >
// Engine/godot/bin/* > bundled official > system, with --version parsing,
// the 4.7 minimum, and the friendly missing/too-old messages.

#include "lagoonic_coding_agents.h"

#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

struct LagoonicMcpTarget {
	String command;
	Vector<String> args; // [mcp_entry, "--project", project_dir]; last slot is the project.
	Dictionary env; // GODOT_PATH + LAGOONIC_PLATFORM=desktop (native env).
};

struct LagoonicGodotVersion {
	int major = 0;
	int minor = 0;
	int patch = 0;
	bool valid = false;
};

// Outcome of connect_installed(): ids per agent, errors as id -> message.
struct LagoonicConnectReport {
	Vector<String> connected;
	Dictionary failed;
};

class LagoonicExternalAgents {
public:
	static constexpr const char *MCP_NAME = "lagoonic";
	static constexpr const char *GODOT_BIN_ENV = "LAGOONIC_GODOT_BIN";
	static constexpr int GODOT_MIN_MAJOR = 4;
	static constexpr int GODOT_MIN_MINOR = 7;

	// ---- McpTarget (mirrors bridge mcpTarget) ----
	// command = the fork's own executable path (OS::get_executable_path())
	// or an embedded lagoonic-mcp binary; args carry the mcp entry plus
	// --project <dir>; env is the native {GODOT_PATH, LAGOONIC_PLATFORM}.
	static LagoonicMcpTarget make_mcp_target(const String &p_project_dir, const String &p_godot_path, const String &p_mcp_entry = String());

	// ---- Config paths ----
	static String user_config_path(const Vector<String> &p_parts);
	static String json_config_path(const LagoonicCodingAgents::AgentInfo &p_agent);
	static String project_config_path(const String &p_project_dir); // <project>/.mcp.json
	static String codex_config_path(); // ~/.codex/config.toml
	static String vibe_config_path(); // ~/.vibe/config.toml
	static String wrapper_path(const String &p_project_dir); // <project>/.lagoonic/lagoonic-mcp.sh|.cmd

	// ---- JSON merge (never replaces the file) ----
	static bool read_json_file(const String &p_path, Dictionary &r_out, String &r_error);
	static bool write_json_file(const String &p_path, const Dictionary &p_value, String &r_error);

	// ---- TOML merge (only the lagoonic section/block is touched) ----
	static String merge_codex_toml(const String &p_existing, const LagoonicMcpTarget &p_mcp, const String &p_project_dir);
	static String remove_codex_toml(const String &p_existing);
	static String merge_vibe_toml(const String &p_existing, const LagoonicMcpTarget &p_mcp, const String &p_project_dir);
	static String remove_vibe_toml(const String &p_existing);

	// ---- Agent state ----
	static bool can_connect(const LagoonicCodingAgents::AgentInfo &p_agent, bool p_has_project);
	static bool has_config(const LagoonicCodingAgents::AgentInfo &p_agent, const String &p_project_dir);
	static bool is_connected(const LagoonicCodingAgents::AgentInfo &p_agent, const String &p_project_dir);
	// Mirrors ExternalAgentManager.list(): one Dictionary per agent with
	// id/name/vendor/built_in/detected/installed/connected/docs_url/
	// install/run/has_chat/chat_install/can_connect. p_project_dir may be "".
	static Array list_agents(const String &p_project_dir);

	// ---- connect / disconnect (mirrors ExternalAgentManager) ----
	static bool connect_agent(const String &p_id, const String &p_project_dir, const LagoonicMcpTarget &p_mcp, String &r_error);
	static bool disconnect_agent(const String &p_id, const String &p_project_dir, String &r_error);

	// Best-effort registration of lagoonic-mcp with every installed agent
	// that has a file-based MCP config. CLI agents are skipped (registering
	// them would execute the agent's own binary without the user asking).
	// Never fails as a whole: one corrupt config is collected, the rest
	// still connect.
	static LagoonicConnectReport connect_installed(const String &p_project_dir, const LagoonicMcpTarget &p_mcp);

	// ---- Godot binary search (mirrors godot-bin.ts) ----
	static String godot_minimum_version(); // "4.7"
	static Vector<String> engine_godot_names();
	static String normalize_godot_path(const String &p_raw);
	static String bundled_godot_bin(const String &p_runtime_root);
	static Vector<String> collect_godot_candidates(const String &p_bundled);
	static String preferred_godot_bin(const String &p_bundled);
	static String godot_version_output(const String &p_bin); // "" when it cannot run.
	static LagoonicGodotVersion parse_godot_version(const String &p_output);
	static bool meets_godot_minimum(const LagoonicGodotVersion &p_version);
	// +1 supported, 0 unparsable (let through; the binary itself will report), -1 too old.
	static int is_supported_godot_version(const String &p_output);
	static String missing_godot_message(const Vector<String> &p_tried);
	static String too_old_godot_message(const String &p_found, const String &p_bin);

private:
	LagoonicExternalAgents() {}
	static bool get_key_path(const Dictionary &p_root, const String &p_key_root, Variant &r_out);
	static void set_key_path(Dictionary &r_root, const String &p_key_root, const Variant &p_value);
	static bool remove_key_path(Dictionary &r_root, const String &p_key_root);
	static bool read_text_file(const String &p_path, String &r_out);
	static bool write_text_file(const String &p_path, const String &p_text, String &r_error);
	static int run_agent_command(const Vector<String> &p_argv, const String &p_cwd, int &r_exit_code);
	static bool is_toml_header(const String &p_trimmed_line);
	static String collapse_blank_lines(const String &p_text);
	static void collect_existing(const Vector<String> &p_candidates, Vector<String> &r_out);
};
