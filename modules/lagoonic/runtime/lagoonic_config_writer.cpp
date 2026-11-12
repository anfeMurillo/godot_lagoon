/**************************************************************************/
/*  lagoonic_config_writer.cpp                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             LAGOONIC ENGINE                            */
/*                   https://github.com/lagoonic/engine                   */
/**************************************************************************/
/* Copyright (c) 2026 Lagoonic contributors.                              */
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
/* License: MIT, same as the Godot Engine (see LICENSE.txt upstream).    */
/**************************************************************************/

#include "lagoonic_config_writer.h"

#include "lagoonic_mcp_locator.h"

#include "core/error/error_macros.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/variant/array.h"

const char *LagoonicConfigWriter::CONFIG_FILE_NAME = "lagoonic.opencode.json";
const char *LagoonicConfigWriter::PLACEHOLDER_LAGOONIC_URL = "https://api.lagoonic.example/v1";

void LagoonicConfigWriter::_bind_methods() {
	ClassDB::bind_method(D_METHOD("disabled_providers"), &LagoonicConfigWriter::disabled_providers);
	ClassDB::bind_method(D_METHOD("asset_env_vars"), &LagoonicConfigWriter::asset_env_vars);
	ClassDB::bind_method(D_METHOD("ask_bash_patterns"), &LagoonicConfigWriter::ask_bash_patterns);
	ClassDB::bind_method(D_METHOD("bash_permission_rules"), &LagoonicConfigWriter::bash_permission_rules);
	ClassDB::bind_method(D_METHOD("default_model_ref", "provider_id", "model"), &LagoonicConfigWriter::default_model_ref);
	ClassDB::bind_method(D_METHOD("server_env", "config_path", "project_dir", "default_model", "asset_env"), &LagoonicConfigWriter::server_env, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("build_config", "input"), &LagoonicConfigWriter::build_config);
	ClassDB::bind_method(D_METHOD("write_config", "destination", "input"), &LagoonicConfigWriter::write_config);
	ClassDB::bind_method(D_METHOD("default_config_path", "project_slug"), &LagoonicConfigWriter::default_config_path);
}

PackedStringArray LagoonicConfigWriter::disabled_providers() const {
	PackedStringArray out;
	out.push_back("claude-agent"); // Runs the Claude Code binary underneath (ARCH §4.3).
	// `opencode` (Zen) stays enabled since 2026-11-11: the model picker offers
	// its free models, and the chat must be able to run them (ARCH §4.3).
	return out;
}

PackedStringArray LagoonicConfigWriter::asset_env_vars() const {
	PackedStringArray out;
	out.push_back("FAL_KEY");
	out.push_back("HIGGSFIELD_API_KEY");
	return out;
}

PackedStringArray LagoonicConfigWriter::ask_bash_patterns() const {
	PackedStringArray out;
	const char *patterns[] = {
		"rm *", "rmdir *", "shred *", "dd *", "mkfs*", "truncate *",
		"git push*", "git reset --hard*", "git clean*", "git checkout -- *", "git restore *",
		"sudo *", "su *", "chmod -R *", "chown *",
		"apt *", "apt-get *", "dpkg *",
		"curl *", "wget *", "ssh *", "scp *", "rsync *",
		"kill *", "pkill *", "killall *",
	};
	for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++) {
		out.push_back(String(patterns[i]));
	}
	return out;
}

Dictionary LagoonicConfigWriter::bash_permission_rules() const {
	Dictionary rules;
	rules["*"] = "allow";
	PackedStringArray patterns = ask_bash_patterns();
	for (int i = 0; i < patterns.size(); i++) {
		rules[patterns[i]] = "ask";
	}
	return rules;
}

String LagoonicConfigWriter::default_model_ref(const String &p_provider_id, const String &p_model) const {
	String provider = p_provider_id.strip_edges();
	if (provider.is_empty()) {
		provider = "anthropic";
	}
	return provider + "/" + p_model.strip_edges();
}

Dictionary LagoonicConfigWriter::server_env(const String &p_config_path, const String &p_project_dir,
		const String &p_default_model, const Dictionary &p_asset_env) const {
	Dictionary env;
	env["OPENCODE_CONFIG"] = p_config_path;
	env["OPENCODE_DISABLE_AUTOUPDATE"] = "1";
	env["LAGOONIC_PROJECT_DIR"] = p_project_dir;
	env["LAGOONIC_DEFAULT_MODEL"] = p_default_model;
	env["LAGOONIC_PLATFORM"] = "desktop";
	PackedStringArray assets = asset_env_vars();
	for (int i = 0; i < assets.size(); i++) {
		if (p_asset_env.has(assets[i])) {
			String value = p_asset_env[assets[i]];
			if (!value.is_empty()) {
				env[assets[i]] = value;
			}
		}
	}
	return env;
}

String LagoonicConfigWriter::build_config(const Dictionary &p_input) const {
	String project_dir = p_input.get("project_dir", "");
	ERR_FAIL_COND_V_MSG(project_dir.is_empty(), "", "LagoonicConfigWriter: project_dir must not be blank.");

	String godot_path = p_input.get("godot_path", "");
	if (godot_path.is_empty()) {
		godot_path = OS::get_singleton()->get_executable_path();
	}
	String mode_raw = p_input.get("mode", "ask");
	String mode = mode_raw.to_lower();
	bool auto_mode = (mode == "auto");

	// Providers: lagoonic placeholder always (spec schema); custom/ollama only
	// when configured, so OpenCode never lists them as connected otherwise.
	Dictionary providers;
	{
		String base_url = p_input.get("lagoonic_base_url", PLACEHOLDER_LAGOONIC_URL);
		if (base_url.is_empty()) {
			base_url = PLACEHOLDER_LAGOONIC_URL;
		}
		Dictionary options;
		options["baseURL"] = base_url;
		options["apiKey"] = "{env:LAGOONIC_API_KEY}";
		Dictionary game_coder;
		game_coder["name"] = "Lagoonic Game Coder";
		Dictionary models;
		models["lagoonic-game-coder"] = game_coder;
		Dictionary lagoonic;
		lagoonic["npm"] = "@ai-sdk/openai-compatible";
		lagoonic["name"] = "Lagoonic";
		lagoonic["options"] = options;
		lagoonic["models"] = models;
		providers["lagoonic"] = lagoonic;
	}
	String custom_url = p_input.get("custom_base_url", "");
	if (!custom_url.is_empty()) {
		Dictionary options;
		options["baseURL"] = custom_url;
		options["apiKey"] = "{env:CUSTOM_API_KEY}";
		Dictionary custom;
		custom["npm"] = "@ai-sdk/openai-compatible";
		custom["name"] = "Custom";
		custom["options"] = options;
		providers["custom"] = custom;
	}
	String ollama_url = p_input.get("ollama_base_url", "");
	if (!ollama_url.is_empty()) {
		Dictionary models;
		if (p_input.has("ollama_models")) {
			PackedStringArray ids = p_input["ollama_models"];
			for (int i = 0; i < ids.size(); i++) {
				Dictionary m;
				m["name"] = ids[i];
				models[ids[i]] = m;
			}
		}
		Dictionary options;
		options["baseURL"] = ollama_url;
		Dictionary ollama;
		ollama["npm"] = "@ai-sdk/openai-compatible";
		ollama["name"] = "Ollama (local)";
		ollama["options"] = options;
		ollama["models"] = models;
		providers["ollama"] = ollama;
	}

	// mcp.lagoonic stdio server. Values are {env:} refs; secrets travel in
	// the supervisor's process environment, never in this file.
	Dictionary mcp_env;
	mcp_env["FAL_KEY"] = "{env:FAL_KEY}";
	mcp_env["HIGGSFIELD_API_KEY"] = "{env:HIGGSFIELD_API_KEY}";
	mcp_env["GODOT_PATH"] = godot_path;
	mcp_env["LAGOONIC_PLATFORM"] = "desktop";
	if (p_input.has("asset_env") && Variant(p_input["asset_env"]).get_type() == Variant::DICTIONARY) {
		Dictionary extra = p_input["asset_env"];
		Array keys = extra.keys();
		for (int i = 0; i < keys.size(); i++) {
			if (!mcp_env.has(keys[i])) {
				mcp_env[keys[i]] = extra[keys[i]];
			}
		}
	}
	Array command;
	if (p_input.has("mcp_command")) {
		Array mcp_cmd = p_input["mcp_command"];
		command = mcp_cmd;
	} else {
		// The server packaged with the editor (or a source checkout); as a last
		// resort the bare name, for a server installed later on the PATH.
		const Vector<String> located = LagoonicMcpLocator::command();
		for (const String &part : located) {
			command.push_back(part);
		}
		if (located.is_empty()) {
			WARN_PRINT("Lagoonic: lagoonic-mcp was not found, so the agent has no Godot tools.");
			command.push_back("lagoonic-mcp");
		}
		command.push_back("--project");
		command.push_back("{env:LAGOONIC_PROJECT_DIR}");
	}
	Dictionary lagoonic_mcp;
	lagoonic_mcp["type"] = "local";
	lagoonic_mcp["command"] = command;
	lagoonic_mcp["enabled"] = true;
	lagoonic_mcp["environment"] = mcp_env;
	Dictionary mcp;
	mcp["lagoonic"] = lagoonic_mcp;
	if (p_input.has("crew") && Variant(p_input["crew"]).get_type() == Variant::DICTIONARY) {
		Dictionary crew = p_input["crew"];
		if (crew.has("url")) {
			String crew_auth = crew.get("authorization", "");
			String crew_url = crew["url"];
			Dictionary headers;
			headers["Authorization"] = crew_auth;
			Dictionary crew_server;
			crew_server["type"] = "remote";
			crew_server["url"] = crew_url;
			crew_server["headers"] = headers;
			mcp["lagoonic-crew"] = crew_server;
		}
	}

	Dictionary permission;
	if (auto_mode) {
		permission["*"] = "allow";
	} else {
		permission["edit"] = "allow";
		permission["bash"] = bash_permission_rules();
		permission["webfetch"] = "ask";
	}

	Array instructions;
	instructions.push_back(project_dir + "/AGENTS.md");
	String notes_path = p_input.get("notes_path", "");
	if (!notes_path.is_empty()) {
		instructions.push_back(notes_path);
	}

	Dictionary config;
	config["$schema"] = "https://opencode.ai/config.json";
	config["autoupdate"] = false;
	config["disabled_providers"] = disabled_providers();
	config["model"] = "{env:LAGOONIC_DEFAULT_MODEL}";
	config["provider"] = providers;
	config["mcp"] = mcp;
	config["permission"] = permission;
	if (!auto_mode) {
		// Plan mode guard (mirrors config-writer.ts): the agent's own rules
		// merge after the global ones, so deny explicitly here.
		Dictionary plan_perm;
		plan_perm["edit"] = "deny";
		plan_perm["bash"] = bash_permission_rules();
		Dictionary plan;
		plan["permission"] = plan_perm;
		Dictionary agent;
		agent["plan"] = plan;
		config["agent"] = agent;
	}
	config["instructions"] = instructions;

	return JSON::stringify(config, "  ") + "\n";
}

Error LagoonicConfigWriter::write_config(const String &p_destination, const Dictionary &p_input) const {
	ERR_FAIL_COND_V_MSG(p_destination.is_empty(), ERR_INVALID_PARAMETER, "LagoonicConfigWriter: destination must not be blank.");
	String text = build_config(p_input);
	ERR_FAIL_COND_V_MSG(text.is_empty(), ERR_INVALID_PARAMETER, "LagoonicConfigWriter: invalid config input.");
	Error dir_err = DirAccess::make_dir_recursive_absolute(p_destination.get_base_dir());
	ERR_FAIL_COND_V_MSG(dir_err != OK, dir_err, "LagoonicConfigWriter: could not create config directory.");
	Ref<FileAccess> f = FileAccess::open(p_destination, FileAccess::WRITE);
	ERR_FAIL_COND_V_MSG(f.is_null(), ERR_CANT_CREATE, "LagoonicConfigWriter: could not write config file.");
	f->store_string(text);
	f->close();
	return OK;
}

String LagoonicConfigWriter::default_config_path(const String &p_project_slug) const {
	String slug = p_project_slug.strip_edges();
	ERR_FAIL_COND_V_MSG(slug.is_empty(), "", "LagoonicConfigWriter: project slug must not be blank.");
	return OS::get_singleton()->get_user_data_dir().path_join("lagoonic-" + slug).path_join(CONFIG_FILE_NAME);
}
