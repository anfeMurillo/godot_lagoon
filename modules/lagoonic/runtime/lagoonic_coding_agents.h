/**************************************************************************/
/*  lagoonic_coding_agents.h                                          */
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

// Native port of Core/lagoonic-desktop/src/main/coding-agents.ts.
//
// Static registry of the 45 coding agents (Orca's catalog) Lagoonic can hand a project to,
// plus host detection (PATH + install dirs + npm global bin, PATHEXT on
// Windows) and the CLI wrapper-script builders. Mirrors the desktop flow:
// registration never replaces an agent config, and Lagoonic never reads the
// agents' credentials.
//
// NOTE (build wiring, handled outside these files): the module SCsub only
// globs top-level "*.cpp". To compile this directory add
//   env_lagoonic.add_source_files(env.modules_sources, "runtime/*.cpp")
// to Engine/godot/modules/lagoonic/SCsub.

#include "core/string/ustring.h"
#include "core/templates/list.h"
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"

class LagoonicCodingAgents {
public:
	static constexpr int AGENT_COUNT = 45;
	static constexpr int CHAT_AGENT_COUNT = 10;

	// How lagoonic-mcp is registered with one agent. Mirrors McpRegistration.
	enum RegistrationKind {
		REG_JSON, // Merge a user-level JSON config (never replaces it).
		REG_PROJECT_JSON, // Merge the project-level .mcp.json file.
		REG_CODEX_TOML, // Replace/append only the lagoonic section of ~/.codex/config.toml.
		REG_VIBE_TOML, // Append/remove only the lagoonic block of ~/.vibe/config.toml.
		REG_CLI, // Register through the agent's own `mcp add` via a wrapper script.
		REG_NONE, // The run command already loads lagoonic-mcp; nothing to connect.
		REG_MANUAL, // No safe automatic way: the user adds lagoonic-mcp by hand (docs_url).
	};

	// Shape of the JSON object written under the agent's key.
	enum JsonEntryKind {
		ENTRY_LOCAL, // {"type":"local","command":[...],"enabled":true,"environment":{...}}
		ENTRY_STDIO, // {"command":...,"args":[...],"env":{...}}
		ENTRY_STDIO_TYPED, // ENTRY_STDIO plus "type":"stdio" (mini-agent, droid).
		ENTRY_STDIO_TOOLS, // ENTRY_STDIO plus "tools":["*"] (copilot).
	};

	// Which `mcp add` argv shape the agent's own CLI expects.
	enum CliAddStyle {
		CLI_ADD_DASHDASH, // <bin> mcp add lagoonic -- <wrapper> (antigravity, amp)
		CLI_ADD_TRANSPORT_STDIO, // <bin> mcp add --transport stdio lagoonic -- <wrapper> (kimi)
		CLI_ADD_YES_DASHDASH, // <bin> mcp add lagoonic --yes -- <wrapper> (cline)
		CLI_ADD_COMMAND, // <bin> mcp add lagoonic --command <wrapper> (auggie)
	};

	struct AgentInfo {
		const char *id;
		const char *name;
		const char *vendor;
		const char *install; // Shell command, shown copyable; Lagoonic never runs it.
		const char *run; // Command that starts the agent.
		RegistrationKind registration;
		bool built_in; // True for OpenCode (installed and wired by Lagoonic itself).
		const char *docs_url;
		const char *acp; // Adapter command for the chat, or nullptr.
		const char *acp_install; // Installs the adapter, or nullptr.
		const char *uninstall; // Removal command, or nullptr.
		// REG_JSON only:
		const char *config_dir_0; // First user-config subdir under home, or nullptr.
		const char *config_dir_1; // Second subdir, or nullptr.
		const char *file_name; // JSON file name, or nullptr.
		const char *key_root; // "mcp" or "mcpServers", or nullptr.
		JsonEntryKind entry_kind;
		// REG_CLI only:
		CliAddStyle cli_style;
		// Chat command for autonomous turns (crew workers), when the agent
		// needs a flag to act without asking (Orca's fullAccess args), or nullptr.
		const char *acp_auto = nullptr;
		// One NAME=value set for the chat adapter (Orca's spec env), or nullptr.
		const char *acp_env = nullptr;
	};

	// Static table of the agents: Lagoonic's first 19, then the rest of Orca's catalog.
	static const AgentInfo AGENTS[AGENT_COUNT];

	// Agents whose adapter speaks Agent Client Protocol over stdio in the chat.
	static const char *CHAT_AGENT_IDS[CHAT_AGENT_COUNT];

	static const AgentInfo *get_agent(const String &p_id);
	static const AgentInfo *get_agent_at(int p_index);
	static bool is_chat_agent(const String &p_id);

	// Executable that tells whether the agent is installed (first word of run).
	static String agent_binary(const AgentInfo &p_agent);
	// Executable that tells whether the chat adapter is installed ("" when none).
	static String acp_binary(const AgentInfo &p_agent);

	// ---- Native host detection (mirrors DetectionEnv / hostSearchDirs) ----

	static bool is_windows();
	static String home_dir();
	// Resolved `npm prefix -g`; "" when npm is missing or fails. Resolve once
	// and pass to host_search_dirs() (spawns one blocking `npm` call).
	static String npm_global_prefix();
	static Vector<String> host_search_dirs(const String &p_npm_prefix);
	static Vector<String> candidate_names(const String &p_bin);
	static bool is_on_host(const String &p_bin, const Vector<String> &p_dirs);
	static bool is_agent_installed(const AgentInfo &p_agent, const Vector<String> &p_dirs);
	static bool is_chat_ready(const AgentInfo &p_agent, const Vector<String> &p_dirs);

	// ---- CLI wrapper scripts (mirrors cliWrapperName / cliWrapperContent) ----
	// Native port: the wrapper sets the native env (GODOT_PATH +
	// LAGOONIC_PLATFORM=desktop). ELECTRON_RUN_AS_NODE only exists in the
	// Electron desktop build and is intentionally not emitted here.

	static String cli_wrapper_name();
	// p_args must already carry the project dir (see mcp_args_for_project()).
	static String cli_wrapper_content(const String &p_command, const Vector<String> &p_args, const Dictionary &p_env);

	// Full argv for the agent's own `mcp add` / `mcp remove` (bin first).
	static Vector<String> cli_add_argv(const AgentInfo &p_agent, const String &p_wrapper);
	static Vector<String> cli_remove_argv(const AgentInfo &p_agent);

	// ---- JSON entry builders (mirrors localEntry / stdioEntry) ----
	// Returns a copy of p_args with the trailing placeholder replaced by the
	// project dir (mirrors mcpArgs: [...args.slice(0, -1), projectDir]).
	static Vector<String> mcp_args_for_project(const Vector<String> &p_args, const String &p_project_dir);
	static Dictionary json_entry(const AgentInfo &p_agent, const String &p_command, const Vector<String> &p_args, const String &p_project_dir, const Dictionary &p_env);

	static String shell_quote(const String &p_value);
	static String json_quote(const String &p_value);

private:
	LagoonicCodingAgents() {}
	static void push_unique(Vector<String> &r_dirs, const String &p_dir);
};
