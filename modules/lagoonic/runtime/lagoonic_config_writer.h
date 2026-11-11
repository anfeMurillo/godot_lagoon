/**************************************************************************/
/*  lagoonic_config_writer.h                                              */
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

#pragma once

// LagoonicConfigWriter: generates `lagoonic.opencode.json`, passed to the
// OpenCode server with OPENCODE_CONFIG.
//
// C++ port of Core/lagoonic-desktop/src/main/config-writer.ts, adapted to the
// headless engine runtime (no Electron, so no ELECTRON_RUN_AS_NODE):
//   - schema { autoupdate:false, disabled_providers:[claude-agent,opencode],
//     model:{env:LAGOONIC_DEFAULT_MODEL}, provider.lagoonic (placeholder
//     https://api.lagoonic.example/v1), mcp.lagoonic {command:
//     [lagoonic-mcp,--project,dir], env {FAL_KEY,HIGGSFIELD_API_KEY}},
//     mcp.lagoonic-crew {url+auth} optional, permission
//     {edit:allow,bash:ask,webfetch:ask}, instructions [AGENTS.md] }.
//   - Destination: <user_data>/lagoonic-<slug>/lagoonic.opencode.json.
//   - Env placeholders stay literal ({env:...}); real values travel only in
//     the supervisor's process environment. GODOT_PATH defaults to
//     OS::get_executable_path().
//
// build_config() input Dictionary keys (only project_dir is required):
//   project_dir    String     project folder, as OpenCode sees it (required)
//   godot_path     String     Godot binary for lagoonic-mcp (default: our exe)
//   default_model  String     informational; config always references
//                             {env:LAGOONIC_DEFAULT_MODEL}
//   mode           String     "ask" (default) or "auto"
//   lagoonic_base_url String   override the placeholder provider URL
//   custom_base_url   String   adds an OpenAI-compatible "custom" provider
//   ollama_base_url   String   registers "ollama" at <url>/v1
//   ollama_models     PackedStringArray  model ids for the picker
//   asset_env         Dictionary  forwarded as {env:NAME} refs to lagoonic-mcp
//                                (FAL_KEY, HIGGSFIELD_API_KEY)
//   mcp_command       PackedStringArray override of the lagoonic-mcp command
//   crew              Dictionary {url, authorization, instruction} (optional)
//   notes_path        String   appended to instructions (optional)

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/variant/dictionary.h"

class LagoonicConfigWriter : public RefCounted {
	GDCLASS(LagoonicConfigWriter, RefCounted);

public:
	static const char *CONFIG_FILE_NAME;
	static const char *PLACEHOLDER_LAGOONIC_URL;

	PackedStringArray disabled_providers() const;
	PackedStringArray asset_env_vars() const;

	// Risky shell patterns that still ask in ask mode (config-writer.ts
	// ASK_BASH_PATTERNS; OpenCode applies the last matching rule).
	PackedStringArray ask_bash_patterns() const;
	// {"*": "allow", <risky>: "ask", ...} with "*" first.
	Dictionary bash_permission_rules() const;

	// "providerId/modelId", as OpenCode writes it.
	String default_model_ref(const String &p_provider_id, const String &p_model) const;
	// Env block for the server process (values, not {env:} refs).
	Dictionary server_env(const String &p_config_path, const String &p_project_dir,
			const String &p_default_model, const Dictionary &p_asset_env) const;

	// Full lagoonic.opencode.json text (2-space indent + trailing newline).
	// Returns "" and logs an error when project_dir is missing.
	String build_config(const Dictionary &p_input) const;
	// Writes build_config() output to p_destination (dirs created).
	Error write_config(const String &p_destination, const Dictionary &p_input) const;
	// <user_data>/lagoonic-<slug>/lagoonic.opencode.json.
	String default_config_path(const String &p_project_slug) const;

protected:
	static void _bind_methods();
};
