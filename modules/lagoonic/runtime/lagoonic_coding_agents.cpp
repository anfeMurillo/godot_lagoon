/**************************************************************************/
/*  lagoonic_coding_agents.cpp                                        */
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

#include "lagoonic_coding_agents.h"

#include "core/io/file_access.h"
#include "core/os/os.h"

// id, name, vendor, install, run, registration, built_in, docs_url, acp,
// acp_install, uninstall, config_dir_0, config_dir_1, file_name, key_root,
// entry_kind, cli_style.
const LagoonicCodingAgents::AgentInfo LagoonicCodingAgents::AGENTS[AGENT_COUNT] = {
	{ "opencode", "OpenCode", "SST",
			"npm install -g opencode-ai", "opencode",
			REG_JSON, true, "https://opencode.ai/docs/",
			nullptr, nullptr, nullptr,
			".config", "opencode", "opencode.json", "mcp",
			ENTRY_LOCAL, CLI_ADD_DASHDASH },
	{ "claude", "Claude Code", "Anthropic",
			"npm install -g @anthropic-ai/claude-code", "claude",
			REG_JSON, false, "https://docs.anthropic.com/en/docs/claude-code/mcp",
			"claude-agent-acp", "npm install -g @agentclientprotocol/claude-agent-acp", "npm uninstall -g @anthropic-ai/claude-code",
			nullptr, nullptr, ".claude.json", "mcpServers",
			ENTRY_STDIO, CLI_ADD_DASHDASH },
	{ "codex", "Codex CLI", "OpenAI",
			"npm install -g @openai/codex", "codex",
			REG_CODEX_TOML, false, "https://developers.openai.com/codex/mcp",
			"codex app-server", nullptr, "npm uninstall -g @openai/codex",
			nullptr, nullptr, nullptr, nullptr,
			ENTRY_STDIO, CLI_ADD_DASHDASH },
	{ "antigravity", "Antigravity CLI", "Google",
			"curl -fsSL https://antigravity.google/cli/install.sh | bash", "agy",
			REG_CLI, false, "https://antigravity.google/docs",
			"agy acp", nullptr, "rm -f ~/.local/bin/agy",
			nullptr, nullptr, nullptr, nullptr,
			ENTRY_STDIO, CLI_ADD_DASHDASH,
			"agy acp --dangerously-skip-permissions", nullptr },
	{ "qwen", "Qwen Code", "Alibaba",
			"npm install -g @qwen-code/qwen-code", "qwen",
			REG_JSON, false, "https://github.com/QwenLM/qwen-code",
			"qwen --acp", nullptr, "npm uninstall -g @qwen-code/qwen-code",
			".qwen", nullptr, "settings.json", "mcpServers",
			ENTRY_STDIO, CLI_ADD_DASHDASH },
	{ "kimi", "Kimi Code", "Moonshot AI",
			"npm install -g @moonshot-ai/kimi-code", "kimi",
			REG_CLI, false, "https://github.com/MoonshotAI/kimi-cli",
			"kimi acp", nullptr, "npm uninstall -g @moonshot-ai/kimi-code",
			nullptr, nullptr, nullptr, nullptr,
			ENTRY_STDIO, CLI_ADD_TRANSPORT_STDIO,
			nullptr, "KIMI_CODE_NO_AUTO_UPDATE=1" },
	{ "minimax", "MiniMax Mini-Agent", "MiniMax",
			"curl -LsSf https://astral.sh/uv/install.sh | sh && uv tool install git+https://github.com/MiniMax-AI/Mini-Agent.git", "mini-agent --workspace .",
			REG_JSON, false, "https://github.com/MiniMax-AI/Mini-Agent",
			nullptr, nullptr, "uv tool uninstall mini-agent",
			".mini-agent", "config", "mcp.json", "mcpServers",
			ENTRY_STDIO_TYPED, CLI_ADD_DASHDASH },
	{ "grok", "Grok Build", "xAI",
			"curl -fsSL https://x.ai/cli/install.sh | bash", "grok",
			REG_PROJECT_JSON, false, "https://docs.x.ai/build/overview",
			"grok agent stdio", nullptr, "rm -f ~/.local/bin/grok && rm -rf ~/.grok/bin",
			nullptr, nullptr, ".mcp.json", "mcpServers",
			ENTRY_STDIO, CLI_ADD_DASHDASH,
			"grok agent --always-approve stdio", nullptr },
	{ "kiro", "Kiro CLI", "Amazon",
			"curl --proto \"=https\" --tlsv1.2 -sSf https://desktop-release.q.us-east-1.amazonaws.com/latest/kirocli-aarch64-apple-darwin.zip -o kirocli.zip && unzip -o kirocli.zip && ./kirocli/install.sh", "kiro-cli",
			REG_JSON, false, "https://kiro.dev/docs/cli/",
			nullptr, nullptr, "rm -f ~/.local/bin/kiro-cli ~/.local/bin/kiro-cli-chat ~/.local/bin/kiro-cli-term",
			".kiro", "settings", "mcp.json", "mcpServers",
			ENTRY_STDIO, CLI_ADD_DASHDASH },
	{ "deepseek", "Codewhale (DeepSeek TUI)", "DeepSeek",
			"npm install -g codewhale", "codewhale",
			REG_JSON, false, "https://github.com/Hmbown/DeepSeek-TUI",
			nullptr, nullptr, "npm uninstall -g codewhale",
			".codewhale", nullptr, "mcp.json", "mcpServers",
			ENTRY_STDIO, CLI_ADD_DASHDASH },
	{ "kilo", "Kilo Code", "Kilo",
			"npm install -g @kilocode/cli", "kilo",
			REG_JSON, false, "https://kilo.ai/docs/automate/mcp/using-in-cli",
			nullptr, nullptr, "npm uninstall -g @kilocode/cli",
			".config", "kilo", "kilo.json", "mcp",
			ENTRY_LOCAL, CLI_ADD_DASHDASH },
	{ "copilot", "GitHub Copilot CLI", "GitHub (Microsoft)",
			"npm install -g @github/copilot", "copilot",
			REG_PROJECT_JSON, false, "https://docs.github.com/en/copilot/how-tos/copilot-cli/customize-copilot/add-mcp-servers",
			"copilot --acp", nullptr, "npm uninstall -g @github/copilot",
			nullptr, nullptr, ".mcp.json", "mcpServers",
			ENTRY_STDIO_TOOLS, CLI_ADD_DASHDASH },
	{ "cursor", "Cursor", "Anysphere",
			"curl https://cursor.com/install -fsS | bash", "agent",
			REG_JSON, false, "https://cursor.com/docs/cli/mcp",
			nullptr, nullptr, "rm -rf ~/.local/bin/agent ~/.local/bin/cursor-agent ~/.local/share/cursor-agent",
			".cursor", nullptr, "mcp.json", "mcpServers",
			ENTRY_STDIO, CLI_ADD_DASHDASH },
	{ "goose", "Goose", "Block",
			"curl -fsSL https://github.com/aaif-goose/goose/releases/download/stable/download_cli.sh | bash", "goose session",
			REG_NONE, false, "https://github.com/block/goose",
			"goose acp", nullptr, "rm -f ~/.local/bin/goose",
			nullptr, nullptr, nullptr, nullptr,
			ENTRY_STDIO, CLI_ADD_DASHDASH },
	{ "cline", "Cline", "Cline",
			"npm install -g cline", "cline",
			REG_CLI, false, "https://docs.cline.bot/cline-cli/getting-started",
			nullptr, nullptr, "npm uninstall -g cline",
			nullptr, nullptr, nullptr, nullptr,
			ENTRY_STDIO, CLI_ADD_YES_DASHDASH },
	{ "amp", "Amp", "Sourcegraph",
			"npm install -g @sourcegraph/amp", "amp",
			REG_CLI, false, "https://ampcode.com/manual",
			nullptr, nullptr, "npm uninstall -g @sourcegraph/amp",
			nullptr, nullptr, nullptr, nullptr,
			ENTRY_STDIO, CLI_ADD_DASHDASH },
	{ "droid", "Droid", "Factory",
			"curl -fsSL https://app.factory.ai/cli | sh", "droid",
			REG_JSON, false, "https://docs.factory.com/cli/configuration/mcp",
			nullptr, nullptr, "rm -f ~/.local/bin/droid && rm -rf ~/.factory/bin",
			".factory", nullptr, "mcp.json", "mcpServers",
			ENTRY_STDIO_TYPED, CLI_ADD_DASHDASH },
	{ "mistral", "Mistral Vibe", "Mistral AI",
			"curl -LsSf https://mistral.ai/vibe/install.sh | bash", "vibe",
			REG_VIBE_TOML, false, "https://github.com/mistralai/mistral-vibe",
			nullptr, nullptr, "rm -f ~/.local/bin/vibe",
			nullptr, nullptr, nullptr, nullptr,
			ENTRY_STDIO, CLI_ADD_DASHDASH },
	{ "auggie", "Auggie", "Augment",
			"npm install -g @augmentcode/auggie", "auggie",
			REG_CLI, false, "https://docs.augmentcode.com/cli/overview",
			nullptr, nullptr, "npm uninstall -g @augmentcode/auggie",
			nullptr, nullptr, nullptr, nullptr,
			ENTRY_STDIO, CLI_ADD_COMMAND },
	// Orca's other agents (stablyai/orca agent catalog). REG_MANUAL: the
	// agent has no documented, safe way to add an MCP server from here.
	{ "gemini", "Gemini CLI", "Google",
			"npm install -g @google/gemini-cli", "gemini",
			REG_JSON, false, "https://github.com/google-gemini/gemini-cli",
			nullptr, nullptr, "npm uninstall -g @google/gemini-cli",
			".gemini", nullptr, "settings.json", "mcpServers",
			ENTRY_STDIO, CLI_ADD_DASHDASH },
	{ "crush", "Crush", "Charm",
			"npm install -g @charmland/crush", "crush",
			REG_JSON, false, "https://github.com/charmbracelet/crush",
			nullptr, nullptr, "npm uninstall -g @charmland/crush",
			".config", "crush", "crush.json", "mcp",
			ENTRY_STDIO_TYPED, CLI_ADD_DASHDASH },
	{ "jcode", "Jcode", "1jehuang",
			"curl -fsSL https://jcode.sh/install | bash", "jcode",
			REG_JSON, false, "https://github.com/1jehuang/jcode",
			nullptr, nullptr, nullptr,
			".jcode", nullptr, "mcp.json", "mcpServers",
			ENTRY_STDIO, CLI_ADD_DASHDASH },
	{ "qoder", "Qoder CLI", "Alibaba",
			"npm install -g @qoder-ai/qodercli", "qodercli",
			REG_CLI, false, "https://docs.qoder.com/cli/mcp-servers",
			nullptr, nullptr, "npm uninstall -g @qoder-ai/qodercli",
			nullptr, nullptr, nullptr, nullptr,
			ENTRY_STDIO, CLI_ADD_DASHDASH },
	{ "qoder-cn", "Qoder CLI China", "Alibaba",
			"open https://docs.qoder.cn/cli/overview", "qoderclicn",
			REG_CLI, false, "https://docs.qoder.cn/cli/overview",
			nullptr, nullptr, nullptr,
			nullptr, nullptr, nullptr, nullptr,
			ENTRY_STDIO, CLI_ADD_DASHDASH },
	{ "command-code", "Command Code", "Command Code",
			"npm install -g command-code", "command-code",
			REG_CLI, false, "https://commandcode.ai/docs/mcp",
			nullptr, nullptr, "npm uninstall -g command-code",
			nullptr, nullptr, nullptr, nullptr,
			ENTRY_STDIO, CLI_ADD_DASHDASH },
	{ "devin", "Devin CLI", "Cognition",
			"curl -fsSL https://cli.devin.ai/install.sh | bash", "devin",
			REG_CLI, false, "https://docs.devin.ai/cli/reference/commands",
			nullptr, nullptr, nullptr,
			nullptr, nullptr, nullptr, nullptr,
			ENTRY_STDIO, CLI_ADD_DASHDASH },
	{ "codebuddy", "CodeBuddy Code", "Tencent",
			"npm install -g @tencent-ai/codebuddy-code", "codebuddy",
			REG_CLI, false, "https://www.codebuddy.ai/cli",
			nullptr, nullptr, "npm uninstall -g @tencent-ai/codebuddy-code",
			nullptr, nullptr, nullptr, nullptr,
			ENTRY_STDIO, CLI_ADD_DASHDASH },
	{ "openclaude", "OpenClaude", "Gitlawb",
			"npm install -g @gitlawb/openclaude", "openclaude",
			REG_CLI, false, "https://openclaude.gitlawb.com/",
			nullptr, nullptr, "npm uninstall -g @gitlawb/openclaude",
			nullptr, nullptr, nullptr, nullptr,
			ENTRY_STDIO, CLI_ADD_DASHDASH },
	{ "omp", "OMP (Oh My Pi)", "can1357",
			"npm install -g @oh-my-pi/pi-coding-agent", "omp",
			REG_MANUAL, false, "https://omp.sh",
			"omp acp", nullptr, "npm uninstall -g @oh-my-pi/pi-coding-agent",
			nullptr, nullptr, nullptr, nullptr,
			ENTRY_STDIO, CLI_ADD_DASHDASH,
			nullptr, "PI_NO_PTY=1" },
	{ "pi", "Pi", "Mario Zechner",
			"npm install -g @mariozechner/pi-coding-agent", "pi",
			REG_MANUAL, false, "https://pi.dev",
			nullptr, nullptr, "npm uninstall -g @mariozechner/pi-coding-agent",
			nullptr, nullptr, nullptr, nullptr,
			ENTRY_STDIO, CLI_ADD_DASHDASH },
	{ "aider", "Aider", "Aider",
			"python3 -m pip install aider-install && aider-install", "aider",
			REG_MANUAL, false, "https://aider.chat/docs/",
			nullptr, nullptr, nullptr,
			nullptr, nullptr, nullptr, nullptr,
			ENTRY_STDIO, CLI_ADD_DASHDASH },
	{ "continue", "Continue CLI", "Continue",
			"npm install -g @continuedev/cli", "cn",
			REG_MANUAL, false, "https://docs.continue.dev/cli/quickstart",
			nullptr, nullptr, "npm uninstall -g @continuedev/cli",
			nullptr, nullptr, nullptr, nullptr,
			ENTRY_STDIO, CLI_ADD_DASHDASH },
	{ "codebuff", "Codebuff", "Codebuff",
			"npm install -g codebuff", "codebuff",
			REG_MANUAL, false, "https://www.codebuff.com/docs/help/quick-start",
			nullptr, nullptr, "npm uninstall -g codebuff",
			nullptr, nullptr, nullptr, nullptr,
			ENTRY_STDIO, CLI_ADD_DASHDASH },
	{ "freebuff", "Freebuff", "Codebuff",
			"npm install -g freebuff", "freebuff",
			REG_MANUAL, false, "https://freebuff.com/cli",
			nullptr, nullptr, "npm uninstall -g freebuff",
			nullptr, nullptr, nullptr, nullptr,
			ENTRY_STDIO, CLI_ADD_DASHDASH },
	{ "openclaw", "OpenClaw", "OpenClaw",
			"npm install -g openclaw", "openclaw",
			REG_MANUAL, false, "https://github.com/openclaw/openclaw",
			nullptr, nullptr, "npm uninstall -g openclaw",
			nullptr, nullptr, nullptr, nullptr,
			ENTRY_STDIO, CLI_ADD_DASHDASH },
	{ "autohand", "Autohand Code", "Autohand",
			"npm install -g autohand-cli", "autohand",
			REG_MANUAL, false, "https://autohand.ai/cli/",
			nullptr, nullptr, "npm uninstall -g autohand-cli",
			nullptr, nullptr, nullptr, nullptr,
			ENTRY_STDIO, CLI_ADD_DASHDASH },
	{ "mimo", "MiMo Code", "Xiaomi",
			"npm install -g @mimo-ai/cli", "mimo",
			REG_MANUAL, false, "https://mimo.xiaomi.com/coder",
			nullptr, nullptr, "npm uninstall -g @mimo-ai/cli",
			nullptr, nullptr, nullptr, nullptr,
			ENTRY_STDIO, CLI_ADD_DASHDASH },
	{ "muse", "Muse Code", "Meta",
			"curl -fsSL https://dev.meta.ai/install.sh | bash", "muse --trust-workspace",
			REG_MANUAL, false, "https://dev.meta.ai/docs/muse-code",
			nullptr, nullptr, nullptr,
			nullptr, nullptr, nullptr, nullptr,
			ENTRY_STDIO, CLI_ADD_DASHDASH },
	{ "hermes", "Hermes Agent", "Nous Research",
			"curl -fsSL https://hermes-agent.nousresearch.com/install.sh | bash", "hermes --tui",
			REG_MANUAL, false, "https://hermes-agent.nousresearch.com/docs/",
			nullptr, nullptr, nullptr,
			nullptr, nullptr, nullptr, nullptr,
			ENTRY_STDIO, CLI_ADD_DASHDASH },
	{ "prime-agent", "Prime Agent", "Prime Intellect",
			"curl -fsSL https://app.primeintellect.ai/prime-agent/install.sh | sh", "prime-agent",
			REG_MANUAL, false, "https://github.com/PrimeIntellect-ai/prime-agent",
			nullptr, nullptr, nullptr,
			nullptr, nullptr, nullptr, nullptr,
			ENTRY_STDIO, CLI_ADD_DASHDASH },
	{ "ante", "Ante", "Antigma Labs",
			"curl -fsSL https://ante.run/install.sh | bash", "ante",
			REG_MANUAL, false, "https://github.com/AntigmaLabs/ante-preview",
			nullptr, nullptr, nullptr,
			nullptr, nullptr, nullptr, nullptr,
			ENTRY_STDIO, CLI_ADD_DASHDASH },
	{ "trae", "Trae CLI", "ByteDance",
			"sh -c \"$(curl -L https://lf-cdn.trae.com.cn/obj/trae-com-cn/trae-cli/install.sh)\"", "traecli",
			REG_MANUAL, false, "https://docs.trae.cn/cli_get-started-with-trae-cli",
			nullptr, nullptr, nullptr,
			nullptr, nullptr, nullptr, nullptr,
			ENTRY_STDIO, CLI_ADD_DASHDASH },
	{ "zcode", "ZCode", "Z.ai",
			"open https://zcode.z.ai/en/docs/install", "zcode",
			REG_MANUAL, false, "https://zcode.z.ai/en/docs",
			nullptr, nullptr, nullptr,
			nullptr, nullptr, nullptr, nullptr,
			ENTRY_STDIO, CLI_ADD_DASHDASH },
	{ "rovo", "Rovo Dev", "Atlassian",
			"brew tap atlassian/homebrew-acli && brew install acli", "acli rovodev run",
			REG_MANUAL, false, "https://support.atlassian.com/rovo/docs/install-and-run-rovo-dev-cli-on-your-device/",
			nullptr, nullptr, nullptr,
			nullptr, nullptr, nullptr, nullptr,
			ENTRY_STDIO, CLI_ADD_DASHDASH },
	{ "dsh", "DeepSeek Harness", "DeepSeek Harness",
			"npm install -g dsh-tui", "dsh-tui .",
			REG_MANUAL, false, "https://www.npmjs.com/package/dsh-tui",
			nullptr, nullptr, "npm uninstall -g dsh-tui",
			nullptr, nullptr, nullptr, nullptr,
			ENTRY_STDIO, CLI_ADD_DASHDASH },
};

const char *LagoonicCodingAgents::CHAT_AGENT_IDS[CHAT_AGENT_COUNT] = {
	"opencode", "claude", "codex", "antigravity", "qwen", "kimi", "goose", "grok", "copilot", "omp"
};

const LagoonicCodingAgents::AgentInfo *LagoonicCodingAgents::get_agent(const String &p_id) {
	for (int i = 0; i < AGENT_COUNT; i++) {
		if (p_id == AGENTS[i].id) {
			return &AGENTS[i];
		}
	}
	return nullptr;
}

const LagoonicCodingAgents::AgentInfo *LagoonicCodingAgents::get_agent_at(int p_index) {
	if (p_index < 0 || p_index >= AGENT_COUNT) {
		return nullptr;
	}
	return &AGENTS[p_index];
}

bool LagoonicCodingAgents::is_chat_agent(const String &p_id) {
	for (int i = 0; i < CHAT_AGENT_COUNT; i++) {
		if (p_id == CHAT_AGENT_IDS[i]) {
			return true;
		}
	}
	return false;
}

String LagoonicCodingAgents::agent_binary(const AgentInfo &p_agent) {
	Vector<String> parts = String(p_agent.run).split_spaces();
	return parts.is_empty() ? String() : parts[0];
}

String LagoonicCodingAgents::acp_binary(const AgentInfo &p_agent) {
	if (p_agent.acp == nullptr || p_agent.acp[0] == '\0') {
		return String();
	}
	Vector<String> parts = String(p_agent.acp).split_spaces();
	return parts.is_empty() ? String() : parts[0];
}

bool LagoonicCodingAgents::is_windows() {
	return OS::get_singleton()->get_name() == "Windows";
}

String LagoonicCodingAgents::home_dir() {
	OS *os = OS::get_singleton();
	String home = os->get_environment("HOME");
	if (!home.is_empty()) {
		return home;
	}
	return os->get_environment("USERPROFILE");
}

String LagoonicCodingAgents::npm_global_prefix() {
	List<String> args;
	args.push_back("prefix");
	args.push_back("-g");
	String out;
	int exit_code = -1;
	Error err = OS::get_singleton()->execute("npm", args, &out, &exit_code);
	if (err != OK || exit_code != 0) {
		return String();
	}
	return out.strip_edges();
}

void LagoonicCodingAgents::push_unique(Vector<String> &r_dirs, const String &p_dir) {
	if (p_dir.is_empty() || r_dirs.has(p_dir)) {
		return;
	}
	r_dirs.push_back(p_dir);
}

Vector<String> LagoonicCodingAgents::host_search_dirs(const String &p_npm_prefix) {
	Vector<String> dirs;
	const bool windows = is_windows();
	const String delimiter = windows ? ";" : ":";
	const String path_value = OS::get_singleton()->get_environment("PATH");
	const Vector<String> chunks = path_value.split(delimiter, true);
	for (int i = 0; i < chunks.size(); i++) {
		push_unique(dirs, chunks[i].strip_edges());
	}
	const String home = home_dir();
	if (windows) {
		if (!home.is_empty()) {
			push_unique(dirs, home.path_join(".local").path_join("bin"));
		}
		const String appdata = OS::get_singleton()->get_environment("APPDATA");
		if (!appdata.is_empty()) {
			push_unique(dirs, appdata.path_join("npm"));
		}
		if (!p_npm_prefix.is_empty()) {
			push_unique(dirs, p_npm_prefix);
		}
	} else {
		static const char *extra_dirs[] = {
			".local/bin", "/opt/homebrew/bin", "/usr/local/bin", ".npm-global/bin"
		};
		for (int i = 0; i < 4; i++) {
			String dir = String(extra_dirs[i]);
			if (dir.begins_with("/")) {
				push_unique(dirs, dir);
			} else if (!home.is_empty()) {
				push_unique(dirs, home.path_join(dir));
			} else {
				push_unique(dirs, dir);
			}
		}
		if (!p_npm_prefix.is_empty()) {
			push_unique(dirs, p_npm_prefix.path_join("bin"));
		}
	}
	return dirs;
}

Vector<String> LagoonicCodingAgents::candidate_names(const String &p_bin) {
	Vector<String> names;
	names.push_back(p_bin);
	if (!is_windows()) {
		return names;
	}
	String pathext = OS::get_singleton()->get_environment("PATHEXT");
	if (pathext.is_empty()) {
		pathext = ".COM;.EXE;.BAT;.CMD";
	}
	const Vector<String> exts = pathext.split(";", true);
	const String lower_bin = p_bin.to_lower();
	for (int i = 0; i < exts.size(); i++) {
		String ext = exts[i].strip_edges();
		if (ext.is_empty()) {
			continue;
		}
		if (!ext.begins_with(".")) {
			ext = "." + ext;
		}
		ext = ext.to_lower();
		if (!lower_bin.ends_with(ext)) {
			names.push_back(p_bin + ext);
		}
	}
	return names;
}

bool LagoonicCodingAgents::is_on_host(const String &p_bin, const Vector<String> &p_dirs) {
	if (p_bin.is_empty()) {
		return false;
	}
	const Vector<String> names = candidate_names(p_bin);
	for (int i = 0; i < p_dirs.size(); i++) {
		for (int j = 0; j < names.size(); j++) {
			if (FileAccess::exists(p_dirs[i].path_join(names[j]))) {
				return true;
			}
		}
	}
	return false;
}

bool LagoonicCodingAgents::is_agent_installed(const AgentInfo &p_agent, const Vector<String> &p_dirs) {
	if (p_agent.built_in) {
		return true;
	}
	return is_on_host(agent_binary(p_agent), p_dirs);
}

bool LagoonicCodingAgents::is_chat_ready(const AgentInfo &p_agent, const Vector<String> &p_dirs) {
	if (p_agent.built_in) {
		return true;
	}
	const String adapter = acp_binary(p_agent);
	return !adapter.is_empty() && is_on_host(adapter, p_dirs);
}

String LagoonicCodingAgents::cli_wrapper_name() {
	return is_windows() ? "lagoonic-mcp.cmd" : "lagoonic-mcp.sh";
}

String LagoonicCodingAgents::cli_wrapper_content(const String &p_command, const Vector<String> &p_args, const Dictionary &p_env) {
	if (is_windows()) {
		Vector<String> lines;
		lines.push_back("@echo off");
		const Array keys = p_env.keys();
		for (int i = 0; i < keys.size(); i++) {
			const String key = keys[i];
			const String value = p_env[key];
			lines.push_back("set \"" + key + "=" + value + "\"");
		}
		String run = "\"" + p_command + "\"";
		for (int i = 0; i < p_args.size(); i++) {
			run += " \"" + p_args[i] + "\"";
		}
		lines.push_back(run + " %*");
		return String("\r\n").join(lines) + "\r\n";
	}
	Vector<String> assignments;
	const Array keys = p_env.keys();
	for (int i = 0; i < keys.size(); i++) {
		const String key = keys[i];
		const String value = p_env[key];
		assignments.push_back(key + "=" + shell_quote(value));
	}
	String line = String(" ").join(assignments);
	line += " exec " + shell_quote(p_command);
	for (int i = 0; i < p_args.size(); i++) {
		line += " " + shell_quote(p_args[i]);
	}
	line += " \"$@\"";
	Vector<String> lines;
	lines.push_back("#!/bin/sh");
	lines.push_back("set -u");
	lines.push_back(line);
	return String("\n").join(lines) + "\n";
}

Vector<String> LagoonicCodingAgents::cli_add_argv(const AgentInfo &p_agent, const String &p_wrapper) {
	Vector<String> argv;
	argv.push_back(agent_binary(p_agent));
	argv.push_back("mcp");
	argv.push_back("add");
	switch (p_agent.cli_style) {
		case CLI_ADD_TRANSPORT_STDIO:
			argv.push_back("--transport");
			argv.push_back("stdio");
			argv.push_back("lagoonic");
			argv.push_back("--");
			argv.push_back(p_wrapper);
			break;
		case CLI_ADD_YES_DASHDASH:
			argv.push_back("lagoonic");
			argv.push_back("--yes");
			argv.push_back("--");
			argv.push_back(p_wrapper);
			break;
		case CLI_ADD_COMMAND:
			argv.push_back("lagoonic");
			argv.push_back("--command");
			argv.push_back(p_wrapper);
			break;
		case CLI_ADD_DASHDASH:
		default:
			argv.push_back("lagoonic");
			argv.push_back("--");
			argv.push_back(p_wrapper);
			break;
	}
	return argv;
}

Vector<String> LagoonicCodingAgents::cli_remove_argv(const AgentInfo &p_agent) {
	Vector<String> argv;
	argv.push_back(agent_binary(p_agent));
	argv.push_back("mcp");
	argv.push_back("remove");
	argv.push_back("lagoonic");
	return argv;
}

Vector<String> LagoonicCodingAgents::mcp_args_for_project(const Vector<String> &p_args, const String &p_project_dir) {
	Vector<String> out;
	for (int i = 0; i + 1 < p_args.size(); i++) {
		out.push_back(p_args[i]);
	}
	out.push_back(p_project_dir);
	return out;
}

Dictionary LagoonicCodingAgents::json_entry(const AgentInfo &p_agent, const String &p_command, const Vector<String> &p_args, const String &p_project_dir, const Dictionary &p_env) {
	Dictionary entry;
	const Vector<String> args = mcp_args_for_project(p_args, p_project_dir);
	if (p_agent.entry_kind == ENTRY_LOCAL) {
		Array command;
		command.push_back(p_command);
		for (int i = 0; i < args.size(); i++) {
			command.push_back(args[i]);
		}
		entry["type"] = "local";
		entry["command"] = command;
		entry["enabled"] = true;
		entry["environment"] = p_env.duplicate();
		return entry;
	}
	if (p_agent.entry_kind == ENTRY_STDIO_TYPED) {
		entry["type"] = "stdio";
	}
	entry["command"] = p_command;
	Array arg_list;
	for (int i = 0; i < args.size(); i++) {
		arg_list.push_back(args[i]);
	}
	entry["args"] = arg_list;
	entry["env"] = p_env.duplicate();
	if (p_agent.entry_kind == ENTRY_STDIO_TOOLS) {
		Array tools;
		tools.push_back("*");
		entry["tools"] = tools;
	}
	return entry;
}

String LagoonicCodingAgents::shell_quote(const String &p_value) {
	return "'" + p_value.replace("'", "'\\''") + "'";
}

String LagoonicCodingAgents::json_quote(const String &p_value) {
	CharString bytes = p_value.utf8();
	String out = "\"";
	for (int i = 0; i < bytes.length(); i++) {
		const char c = bytes[i];
		switch (c) {
			case '"':
				out += "\\\"";
				break;
			case '\\':
				out += "\\\\";
				break;
			case '\n':
				out += "\\n";
				break;
			case '\r':
				out += "\\r";
				break;
			case '\t':
				out += "\\t";
				break;
			case '\b':
				out += "\\b";
				break;
			case '\f':
				out += "\\f";
				break;
			default:
				if (c >= 0 && c < 0x20) {
					const char hex[] = "0123456789abcdef";
					out += String("\\u00") + String::chr(hex[(c >> 4) & 0xF]) + String::chr(hex[c & 0xF]);
				} else {
					out += String::chr((unsigned char)c);
				}
				break;
		}
	}
	out += "\"";
	return out;
}
