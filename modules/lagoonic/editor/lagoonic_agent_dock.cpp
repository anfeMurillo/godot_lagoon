/**************************************************************************/
/*  lagoonic_agent_dock.cpp                                           */
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

#include "lagoonic_agent_dock.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/tab_container.h"
#include "scene/main/timer.h"
#include "servers/display/display_server.h"
#include "servers/text/text_server.h"

// Catalogo estatico de los 19 agentes (Core/lagoonic-desktop/src/main/
// coding-agents.ts). Los comandos install/run/docs salen de ahi; Lagoonic
// nunca ejecuta instaladores, solo muestra el comando copiable.
struct LagoonicAgentCatalogEntry {
	const char *id;
	const char *name;
	const char *vendor;
	const char *install;
	const char *run;
	const char *docs_url;
	const char *acp; // Adapter ACP para el chat; "" = sin chat.
	bool can_connect; // false = nada que conectar (goose).
};

static const LagoonicAgentCatalogEntry LAGOONIC_AGENT_CATALOG[19] = {
	{ "opencode", "OpenCode", "SST", "npm install -g opencode-ai", "opencode", "https://opencode.ai/docs/", "integrado", true },
	{ "claude", "Claude Code", "Anthropic", "npm install -g @anthropic-ai/claude-code", "claude", "https://docs.anthropic.com/en/docs/claude-code/mcp", "claude-agent-acp", true },
	{ "codex", "Codex CLI", "OpenAI", "npm install -g @openai/codex", "codex", "https://developers.openai.com/codex/mcp", "codex-acp", true },
	{ "antigravity", "Antigravity CLI", "Google", "curl -fsSL https://antigravity.google/cli/install.sh | bash", "agy", "https://antigravity.google/docs", "", true },
	{ "qwen", "Qwen Code", "Alibaba", "npm install -g @qwen-code/qwen-code", "qwen", "https://github.com/QwenLM/qwen-code", "qwen --acp", true },
	{ "kimi", "Kimi Code", "Moonshot AI", "npm install -g @moonshot-ai/kimi-code", "kimi", "https://github.com/MoonshotAI/kimi-cli", "kimi acp", true },
	{ "minimax", "MiniMax Mini-Agent", "MiniMax", "curl -LsSf https://astral.sh/uv/install.sh | sh && uv tool install git+https://github.com/MiniMax-AI/Mini-Agent.git", "mini-agent --workspace .", "https://github.com/MiniMax-AI/Mini-Agent", "", true },
	{ "grok", "Grok Build", "xAI", "curl -fsSL https://x.ai/cli/install.sh | bash", "grok", "https://docs.x.ai/build/overview", "", true },
	{ "kiro", "Kiro CLI", "Amazon", "curl --proto \"=https\" --tlsv1.2 -sSf https://desktop-release.q.us-east-1.amazonaws.com/latest/kirocli-aarch64-apple-darwin.zip -o kirocli.zip && unzip -o kirocli.zip && ./kirocli/install.sh", "kiro-cli", "https://kiro.dev/docs/cli/", "", true },
	{ "deepseek", "Codewhale (DeepSeek TUI)", "DeepSeek", "npm install -g codewhale", "codewhale", "https://github.com/Hmbown/DeepSeek-TUI", "", true },
	{ "kilo", "Kilo Code", "Kilo", "npm install -g @kilocode/cli", "kilo", "https://kilo.ai/docs/automate/mcp/using-in-cli", "", true },
	{ "copilot", "GitHub Copilot CLI", "GitHub (Microsoft)", "npm install -g @github/copilot", "copilot", "https://docs.github.com/en/copilot/how-tos/copilot-cli/customize-copilot/add-mcp-servers", "", true },
	{ "cursor", "Cursor", "Anysphere", "curl https://cursor.com/install -fsS | bash", "agent", "https://cursor.com/docs/cli/mcp", "", true },
	{ "goose", "Goose", "Block", "curl -fsSL https://github.com/aaif-goose/goose/releases/download/stable/download_cli.sh | bash", "goose session", "https://github.com/block/goose", "goose acp", false },
	{ "cline", "Cline", "Cline", "npm install -g cline", "cline", "https://docs.cline.bot/cline-cli/getting-started", "", true },
	{ "amp", "Amp", "Sourcegraph", "npm install -g @sourcegraph/amp", "amp", "https://ampcode.com/manual", "", true },
	{ "droid", "Droid", "Factory", "curl -fsSL https://app.factory.ai/cli | sh", "droid", "https://docs.factory.com/cli/configuration/mcp", "", true },
	{ "mistral", "Mistral Vibe", "Mistral AI", "curl -LsSf https://mistral.ai/vibe/install.sh | bash", "vibe", "https://github.com/mistralai/mistral-vibe", "", true },
	{ "auggie", "Auggie", "Augment", "npm install -g @augmentcode/auggie", "auggie", "https://docs.augmentcode.com/cli/overview", "", true },
};

struct LagoonicProviderCatalogEntry {
	const char *id;
	const char *name;
	bool is_url; // true = campo de texto normal (Ollama), false = secreto.
};

// BYOK del contrato + Ollama local. Las claves viajan solo al guardar
// (setKey) y nunca vuelven (hasKey booleano), igual que en desktop.
static const LagoonicProviderCatalogEntry LAGOONIC_PROVIDER_CATALOG[11] = {
	{ "anthropic", "Anthropic", false },
	{ "openai", "OpenAI", false },
	{ "google", "Google", false },
	{ "openrouter", "OpenRouter", false },
	{ "deepseek", "DeepSeek", false },
	{ "groq", "Groq", false },
	{ "xai", "xAI", false },
	{ "opencode-go", "OpenCode Go", false },
	{ "fal", "fal.ai", false },
	{ "higgsfield", "Higgsfield", false },
	{ "ollama", "Ollama URL", true },
};

void LagoonicAgentDock::_fire(const StringName &p_event, const Variant &p_a, const Variant &p_b) {
	if (event_callback.is_valid()) {
		event_callback.call(p_event, p_a, p_b);
	}
	if (!has_signal(p_event)) {
		return;
	}
	const int argc = signal_argc.has(p_event) ? signal_argc[p_event] : 0;
	if (argc <= 0) {
		emit_signal(p_event);
	} else if (argc == 1) {
		emit_signal(p_event, p_a);
	} else {
		emit_signal(p_event, p_a, p_b);
	}
}

void LagoonicAgentDock::set_event_handler(const Callable &p_handler) {
	event_callback = p_handler;
	if (chat_view != nullptr) {
		chat_view->set_event_handler(callable_mp(this, &LagoonicAgentDock::_on_chat_event));
	}
}

void LagoonicAgentDock::_ensure_agent_row(const String &p_agent_id, const String &p_name) {
	if (agent_rows.has(p_agent_id) || agents_box == nullptr) {
		return;
	}
	AgentRow row;
	row.agent_id = p_agent_id;

	row.box = memnew(VBoxContainer);
	row.box->add_theme_constant_override(SNAME("separation"), 2);
	agents_box->add_child(row.box);

	HBoxContainer *top = memnew(HBoxContainer);
	top->add_theme_constant_override(SNAME("separation"), 6);
	row.box->add_child(top);

	row.title = memnew(Label);
	row.title->set_text(p_name);
	row.title->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	top->add_child(row.title);

	row.status = memnew(Label);
	top->add_child(row.status);

	row.connect_button = memnew(Button);
	row.connect_button->set_text(TTRC("Conectar"));
	row.connect_button->set_tooltip_text(TTRC("Registra lagoonic-mcp en este agente."));
	row.connect_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicAgentDock::_on_agent_connect_button).bind(p_agent_id));
	top->add_child(row.connect_button);

	row.docs_button = memnew(Button);
	row.docs_button->set_text(TTRC("Docs"));
	row.docs_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicAgentDock::_on_agent_docs_button).bind(p_agent_id));
	top->add_child(row.docs_button);

	HBoxContainer *bottom = memnew(HBoxContainer);
	bottom->add_theme_constant_override(SNAME("separation"), 4);
	row.box->add_child(bottom);

	row.install_edit = memnew(LineEdit);
	row.install_edit->set_editable(false);
	row.install_edit->set_select_all_on_focus(true);
	row.install_edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	bottom->add_child(row.install_edit);

	row.copy_button = memnew(Button);
	row.copy_button->set_text(TTRC("Copiar"));
	row.copy_button->set_tooltip_text(TTRC(String::utf8("Copia el comando de instalación al portapapeles.")));
	row.copy_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicAgentDock::_on_agent_copy_button).bind(p_agent_id));
	bottom->add_child(row.copy_button);

	row.chat = memnew(Label);
	row.chat->add_theme_color_override(SNAME("font_color"), Color(0.6, 0.6, 0.6));
	row.box->add_child(row.chat);

	agent_rows[p_agent_id] = row;
}

String LagoonicAgentDock::_agent_status_text(bool p_detected, bool p_installed, bool p_connected) {
	if (p_connected) {
		return TTRC("conectado");
	}
	if (p_installed || p_detected) {
		return TTRC("instalado");
	}
	return TTRC("no instalado");
}

Color LagoonicAgentDock::_agent_status_color(bool p_detected, bool p_installed) {
	if (p_installed || p_detected) {
		return Color(0.35, 0.85, 0.4);
	}
	return Color(0.6, 0.6, 0.6);
}

void LagoonicAgentDock::_refresh_agent_row(AgentRow &p_row, const Dictionary &p_info) {
	const bool detected = bool(p_info.get("detected", false));
	const bool installed = bool(p_info.get("installed", false));
	const bool connected = bool(p_info.get("connected", false));
	const bool has_chat = bool(p_info.get("has_chat", false));
	const bool can_connect = bool(p_info.get("can_connect", p_row.can_connect));
	const String install = String(p_info.get("install", ""));
	const String run = String(p_info.get("run", p_row.run_command));

	p_row.connected = connected;
	p_row.can_connect = can_connect;
	if (!run.is_empty()) {
		p_row.run_command = run;
	}
	if (p_row.status != nullptr) {
		p_row.status->set_text(_agent_status_text(detected, installed, connected));
		p_row.status->add_theme_color_override(SNAME("font_color"),
				connected ? Color(0.35, 0.85, 0.4) : _agent_status_color(detected, installed));
	}
	if (p_row.connect_button != nullptr) {
		p_row.connect_button->set_visible(can_connect);
		p_row.connect_button->set_text(connected ? TTRC("Desconectar") : TTRC("Conectar"));
		p_row.connect_button->set_disabled(!installed && !connected);
	}
	if (p_row.install_edit != nullptr && !install.is_empty()) {
		p_row.install_edit->set_text(install);
		p_row.install_edit->set_tooltip_text(vformat(TTRC(String::utf8("Instalar: %s · Ejecutar: %s")), install, p_row.run_command));
	}
	if (p_row.chat != nullptr) {
		p_row.chat->set_text(has_chat ? TTRC("ACP listo para el chat") : TTRC("Sin adapter ACP"));
		p_row.chat->set_visible(true);
	}
	if (agents_count != nullptr) {
		int known = 0;
		for (const KeyValue<String, AgentRow> &kv : agent_rows) {
			(void)kv;
			known++;
		}
		agents_count->set_text(vformat(TTRC("%d agentes"), known));
	}
}

void LagoonicAgentDock::set_agents(const Array &p_agents) {
	for (int i = 0; i < p_agents.size(); i++) {
		const Dictionary info = p_agents[i];
		const String id = String(info.get("id", ""));
		if (id.is_empty()) {
			continue;
		}
		_ensure_agent_row(id, String(info.get("name", id)));
		_refresh_agent_row(agent_rows[id], info);
	}
}

void LagoonicAgentDock::set_agent_state(const String &p_agent_id, bool p_detected, bool p_installed, bool p_connected) {
	_ensure_agent_row(p_agent_id, p_agent_id);
	Dictionary info;
	info["detected"] = p_detected;
	info["installed"] = p_installed;
	info["connected"] = p_connected;
	_refresh_agent_row(agent_rows[p_agent_id], info);
}

void LagoonicAgentDock::_on_agent_connect_button(const String &p_agent_id) {
	if (!agent_rows.has(p_agent_id)) {
		return;
	}
	const AgentRow &row = agent_rows[p_agent_id];
	_fire(row.connected ? SNAME("agent_disconnect_requested") : SNAME("agent_connect_requested"), p_agent_id);
}

void LagoonicAgentDock::_on_agent_copy_button(const String &p_agent_id) {
	if (!agent_rows.has(p_agent_id)) {
		return;
	}
	DisplayServer *ds = DisplayServer::get_singleton();
	if (ds != nullptr && agent_rows[p_agent_id].install_edit != nullptr) {
		ds->clipboard_set(agent_rows[p_agent_id].install_edit->get_text());
	}
}

void LagoonicAgentDock::_on_agent_docs_button(const String &p_agent_id) {
	_fire(SNAME("agent_docs_requested"), p_agent_id);
}

void LagoonicAgentDock::_on_terminal_pressed() {
	_fire(SNAME("open_terminal_requested"));
}

void LagoonicAgentDock::set_provider_has_key(const String &p_provider_id, bool p_has_key) {
	if (!provider_rows.has(p_provider_id)) {
		return;
	}
	ProviderRow &row = provider_rows[p_provider_id];
	if (row.dot != nullptr) {
		row.dot->add_theme_color_override(SNAME("font_color"),
				p_has_key ? Color(0.35, 0.85, 0.4) : Color(0.6, 0.6, 0.6));
	}
	if (row.key_edit != nullptr) {
		row.key_edit->set_placeholder(p_has_key ? TTRC(String::utf8("●●● guardada (escribe para reemplazar)")) : TTRC(String::utf8("Pegar clave…")));
	}
}

void LagoonicAgentDock::set_provider_test_result(const String &p_text, bool p_ok) {
	if (provider_test_result == nullptr) {
		return;
	}
	provider_test_result->set_text(p_text);
	provider_test_result->add_theme_color_override(SNAME("font_color"),
			p_ok ? Color(0.35, 0.85, 0.4) : Color(0.9, 0.35, 0.3));
}

void LagoonicAgentDock::set_ollama_url(const String &p_url) {
	if (ollama_edit != nullptr) {
		ollama_edit->set_text(p_url);
	}
}

String LagoonicAgentDock::get_ollama_url() const {
	if (ollama_edit != nullptr) {
		return ollama_edit->get_text().strip_edges();
	}
	return String();
}

void LagoonicAgentDock::_on_provider_save(const String &p_provider_id) {
	if (!provider_rows.has(p_provider_id)) {
		return;
	}
	LineEdit *edit = provider_rows[p_provider_id].key_edit;
	if (edit == nullptr) {
		return;
	}
	const String secret = edit->get_text().strip_edges();
	if (secret.is_empty()) {
		return;
	}
	edit->clear(); // Las claves van y no vuelven (contract.ts).
	_fire(SNAME("provider_key_save_requested"), p_provider_id, secret);
}

void LagoonicAgentDock::_on_provider_remove(const String &p_provider_id) {
	_fire(SNAME("provider_key_remove_requested"), p_provider_id);
}

void LagoonicAgentDock::_on_provider_test_pressed() {
	_fire(SNAME("provider_test_requested"));
}

void LagoonicAgentDock::_poll() {
	if (!is_visible_in_tree()) {
		return;
	}
	_fire(SNAME("poll_tick"));
}

static int _lagoonic_tool_state(const String &p_state) {
	const String s = p_state.to_lower();
	if (s == "done" || s == "success" || s == "ok") {
		return LagoonicChatView::TOOL_DONE;
	}
	if (s == "error" || s == "failed") {
		return LagoonicChatView::TOOL_ERROR;
	}
	return LagoonicChatView::TOOL_RUNNING;
}

void LagoonicAgentDock::hook_push_agent_event(const Dictionary &p_event) {
	if (chat_view == nullptr) {
		return;
	}
	const String type = String(p_event.get("type", ""));
	if (type == "message") {
		const Dictionary message = p_event.get("message", Dictionary());
		chat_view->upsert_assistant_message(
				String(message.get("id", "")),
				String(message.get("text", "")));
		const Array tools = message.get("tools", Array());
		for (int i = 0; i < tools.size(); i++) {
			const Dictionary tool = tools[i];
			chat_view->append_tool_row(
					String(message.get("id", "")),
					String(tool.get("id", itos(i))),
					String(tool.get("label", "")),
					_lagoonic_tool_state(String(tool.get("state", "running"))),
					String(tool.get("detail", "")));
		}
	} else if (type == "todos") {
		LagoonicCrewView *crew = chat_view->get_crew_view();
		if (crew != nullptr) {
			crew->set_tasks(p_event.get("todos", Array()));
		}
	} else if (type == "permission") {
		const Dictionary request = p_event.get("request", Dictionary());
		chat_view->show_permission_card(
				String(request.get("id", "")),
				String(request.get("sessionId", "")),
				String(request.get("title", "")),
				String(request.get("detail", "")));
	} else if (type == "busy") {
		chat_view->set_busy(bool(p_event.get("busy", false)));
	} else if (type == "changes") {
		const Array changes = p_event.get("changes", Array());
		PackedStringArray paths;
		int additions = 0;
		int deletions = 0;
		for (int i = 0; i < changes.size(); i++) {
			const Dictionary change = changes[i];
			paths.push_back(String(change.get("path", "")));
			additions += int(change.get("additions", 0));
			deletions += int(change.get("deletions", 0));
		}
		if (paths.is_empty()) {
			chat_view->clear_changes();
		} else {
			chat_view->set_changes(paths, additions, deletions);
		}
	} else if (type == "error") {
		chat_view->show_error(String(p_event.get("message", TTRC("Error desconocido."))));
		chat_view->set_busy(false);
	}
}

void LagoonicAgentDock::hook_set_connection(bool p_connected) {
	runtime_connected = p_connected;
	if (chat_view != nullptr) {
		chat_view->set_connected(p_connected);
	}
	if (terminal_button != nullptr) {
		terminal_button->set_disabled(!p_connected);
	}
}

void LagoonicAgentDock::refresh_icons() {
	if (chat_view != nullptr) {
		chat_view->refresh_icons();
	}
	if (terminal_button != nullptr) {
		terminal_button->set_button_icon(get_editor_theme_icon(SNAME("Terminal")));
	}
}

void LagoonicAgentDock::_notification(int p_notification) {
	switch (p_notification) {
		case NOTIFICATION_ENTER_TREE: {
			refresh_icons();
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			refresh_icons();
		} break;
	}
}

void LagoonicAgentDock::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_event_handler", "handler"), &LagoonicAgentDock::set_event_handler);
	ClassDB::bind_method(D_METHOD("get_chat_view"), &LagoonicAgentDock::get_chat_view);
	ClassDB::bind_method(D_METHOD("set_agents", "agents"), &LagoonicAgentDock::set_agents);
	ClassDB::bind_method(D_METHOD("set_agent_state", "agent_id", "detected", "installed", "connected"), &LagoonicAgentDock::set_agent_state);
	ClassDB::bind_method(D_METHOD("set_provider_has_key", "provider_id", "has_key"), &LagoonicAgentDock::set_provider_has_key);
	ClassDB::bind_method(D_METHOD("set_provider_test_result", "text", "ok"), &LagoonicAgentDock::set_provider_test_result);
	ClassDB::bind_method(D_METHOD("set_ollama_url", "url"), &LagoonicAgentDock::set_ollama_url);
	ClassDB::bind_method(D_METHOD("get_ollama_url"), &LagoonicAgentDock::get_ollama_url);
	ClassDB::bind_method(D_METHOD("hook_set_bridge", "bridge"), &LagoonicAgentDock::hook_set_bridge);
	ClassDB::bind_method(D_METHOD("hook_push_agent_event", "event"), &LagoonicAgentDock::hook_push_agent_event);
	ClassDB::bind_method(D_METHOD("hook_set_connection", "connected"), &LagoonicAgentDock::hook_set_connection);
	ClassDB::bind_method(D_METHOD("refresh_icons"), &LagoonicAgentDock::refresh_icons);

	ADD_SIGNAL(MethodInfo("send_requested", PropertyInfo(Variant::STRING, "text")));
	ADD_SIGNAL(MethodInfo("abort_requested"));
	ADD_SIGNAL(MethodInfo("permission_replied",
			PropertyInfo(Variant::STRING, "request_id"),
			PropertyInfo(Variant::INT, "reply")));
	ADD_SIGNAL(MethodInfo("undo_file_requested", PropertyInfo(Variant::STRING, "path")));
	ADD_SIGNAL(MethodInfo("undo_task_requested"));
	ADD_SIGNAL(MethodInfo("revert_requested", PropertyInfo(Variant::STRING, "message_id")));
	ADD_SIGNAL(MethodInfo("new_session_requested"));
	ADD_SIGNAL(MethodInfo("session_selected", PropertyInfo(Variant::STRING, "session_id")));
	ADD_SIGNAL(MethodInfo("boss_changed", PropertyInfo(Variant::STRING, "agent_id")));
	ADD_SIGNAL(MethodInfo("model_changed", PropertyInfo(Variant::STRING, "model_ref")));
	ADD_SIGNAL(MethodInfo("mode_changed", PropertyInfo(Variant::INT, "mode")));
	ADD_SIGNAL(MethodInfo("worker_toggled",
			PropertyInfo(Variant::STRING, "agent_id"),
			PropertyInfo(Variant::BOOL, "enabled")));
	ADD_SIGNAL(MethodInfo("attachments_dropped", PropertyInfo(Variant::PACKED_STRING_ARRAY, "paths")));
	ADD_SIGNAL(MethodInfo("agent_connect_requested", PropertyInfo(Variant::STRING, "agent_id")));
	ADD_SIGNAL(MethodInfo("agent_disconnect_requested", PropertyInfo(Variant::STRING, "agent_id")));
	ADD_SIGNAL(MethodInfo("agent_docs_requested", PropertyInfo(Variant::STRING, "agent_id")));
	ADD_SIGNAL(MethodInfo("open_terminal_requested"));
	ADD_SIGNAL(MethodInfo("provider_key_save_requested",
			PropertyInfo(Variant::STRING, "provider_id"),
			PropertyInfo(Variant::STRING, "secret")));
	ADD_SIGNAL(MethodInfo("provider_key_remove_requested", PropertyInfo(Variant::STRING, "provider_id")));
	ADD_SIGNAL(MethodInfo("provider_test_requested"));
	ADD_SIGNAL(MethodInfo("poll_tick"));
}

LagoonicAgentDock::LagoonicAgentDock() {
	signal_argc[SNAME("send_requested")] = 1;
	signal_argc[SNAME("abort_requested")] = 0;
	signal_argc[SNAME("permission_replied")] = 2;
	signal_argc[SNAME("undo_file_requested")] = 1;
	signal_argc[SNAME("undo_task_requested")] = 0;
	signal_argc[SNAME("revert_requested")] = 1;
	signal_argc[SNAME("new_session_requested")] = 0;
	signal_argc[SNAME("session_selected")] = 1;
	signal_argc[SNAME("boss_changed")] = 1;
	signal_argc[SNAME("model_changed")] = 1;
	signal_argc[SNAME("mode_changed")] = 1;
	signal_argc[SNAME("worker_toggled")] = 2;
	signal_argc[SNAME("attachments_dropped")] = 1;
	signal_argc[SNAME("agent_connect_requested")] = 1;
	signal_argc[SNAME("agent_disconnect_requested")] = 1;
	signal_argc[SNAME("agent_docs_requested")] = 1;
	signal_argc[SNAME("open_terminal_requested")] = 0;
	signal_argc[SNAME("provider_key_save_requested")] = 2;
	signal_argc[SNAME("provider_key_remove_requested")] = 1;
	signal_argc[SNAME("provider_test_requested")] = 0;
	signal_argc[SNAME("poll_tick")] = 0;

	add_theme_constant_override(SNAME("separation"), 6);

	tabs = memnew(TabContainer);
	tabs->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	tabs->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	add_child(tabs);

	// --- Pestana 1: Chat ---
	chat_view = memnew(LagoonicChatView);
	chat_view->set_event_handler(callable_mp(this, &LagoonicAgentDock::_on_chat_event));
	chat_view->set_name("Chat");
	tabs->add_child(chat_view);
	tabs->set_tab_title(0, TTRC("Chat"));

	// --- Pestana 2: Agentes CLI ---
	VBoxContainer *cli_page = memnew(VBoxContainer);
	cli_page->add_theme_constant_override(SNAME("separation"), 6);
	cli_page->set_name("Agents");
	tabs->add_child(cli_page);
	tabs->set_tab_title(1, TTRC("Agentes CLI"));

	HBoxContainer *cli_header = memnew(HBoxContainer);
	cli_header->add_theme_constant_override(SNAME("separation"), 6);
	cli_page->add_child(cli_header);

	agents_count = memnew(Label);
	agents_count->set_text(TTRC("19 agentes"));
	agents_count->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	cli_header->add_child(agents_count);

	terminal_button = memnew(Button);
	terminal_button->set_text(TTRC("Abrir terminal en proyecto"));
	terminal_button->set_tooltip_text(TTRC("Abre un terminal del sistema en la carpeta del proyecto."));
	terminal_button->set_disabled(true);
	terminal_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicAgentDock::_on_terminal_pressed));
	cli_header->add_child(terminal_button);

	ScrollContainer *agents_scroll = memnew(ScrollContainer);
	agents_scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	agents_scroll->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	cli_page->add_child(agents_scroll);

	agents_box = memnew(VBoxContainer);
	agents_box->add_theme_constant_override(SNAME("separation"), 8);
	agents_box->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	agents_scroll->add_child(agents_box);

	for (int i = 0; i < 19; i++) {
		const LagoonicAgentCatalogEntry &entry = LAGOONIC_AGENT_CATALOG[i];
		_ensure_agent_row(entry.id, entry.name);
		Dictionary info;
		info["detected"] = String(entry.id) == "opencode"; // Integrado: siempre presente.
		info["installed"] = String(entry.id) == "opencode";
		info["connected"] = false;
		info["has_chat"] = String(entry.acp) != "";
		info["install"] = String(entry.install);
		info["run"] = String(entry.run);
		info["docs_url"] = String(entry.docs_url);
		info["can_connect"] = entry.can_connect;
		AgentRow &row = agent_rows[entry.id];
		row.run_command = entry.run;
		if (row.title != nullptr) {
			row.title->set_text(vformat("%s (%s)", entry.name, entry.vendor));
		}
		if (row.chat != nullptr && String(entry.acp) != "") {
			row.chat->set_text(vformat(TTRC("Chat ACP: %s"), entry.acp));
		}
		_refresh_agent_row(row, info);
	}

	// --- Pestana 3: Proveedores ---
	VBoxContainer *providers_page = memnew(VBoxContainer);
	providers_page->add_theme_constant_override(SNAME("separation"), 6);
	providers_page->set_name("Providers");
	tabs->add_child(providers_page);
	tabs->set_tab_title(2, TTRC("Proveedores"));

	ScrollContainer *providers_scroll = memnew(ScrollContainer);
	providers_scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	providers_scroll->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	providers_page->add_child(providers_scroll);

	providers_box = memnew(VBoxContainer);
	providers_box->add_theme_constant_override(SNAME("separation"), 4);
	providers_box->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	providers_scroll->add_child(providers_box);

	for (int i = 0; i < 11; i++) {
		const LagoonicProviderCatalogEntry &entry = LAGOONIC_PROVIDER_CATALOG[i];
		const String pid = entry.id;

		HBoxContainer *row_box = memnew(HBoxContainer);
		row_box->add_theme_constant_override(SNAME("separation"), 4);
		providers_box->add_child(row_box);

		Label *dot = memnew(Label);
		dot->set_text(String::chr(0x25CF)); // ●
		dot->add_theme_color_override(SNAME("font_color"), Color(0.6, 0.6, 0.6));
		row_box->add_child(dot);

		Label *name = memnew(Label);
		name->set_text(entry.name);
		name->set_custom_minimum_size(Size2(110, 0));
		row_box->add_child(name);

		LineEdit *key_edit = memnew(LineEdit);
		key_edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		if (entry.is_url) {
			key_edit->set_placeholder(TTRC("http://127.0.0.1:11434/v1"));
		} else {
			key_edit->set_secret(true);
			key_edit->set_placeholder(TTRC(String::utf8("Pegar clave…")));
		}
		row_box->add_child(key_edit);

		Button *save = memnew(Button);
		save->set_text(TTRC("Guardar"));
		save->set_tooltip_text(vformat(TTRC("Guarda la clave de %s en el vault (LagoonicVault)."), entry.name));
		save->connect(SceneStringName(pressed), callable_mp(this, &LagoonicAgentDock::_on_provider_save).bind(pid));
		row_box->add_child(save);

		ProviderRow row;
		row.box = row_box;
		row.dot = dot;
		row.key_edit = key_edit;
		row.save_button = save;
		if (!entry.is_url) {
			Button *remove = memnew(Button);
			remove->set_text(TTRC("Borrar"));
			remove->set_tooltip_text(vformat(TTRC("Olvida la clave de %s."), entry.name));
			remove->connect(SceneStringName(pressed), callable_mp(this, &LagoonicAgentDock::_on_provider_remove).bind(pid));
			row_box->add_child(remove);
			row.remove_button = remove;
		} else {
			row.remove_button = nullptr;
			ollama_edit = key_edit; // La fila URL es el campo Ollama.
		}
		provider_rows[pid] = row;
	}

	HBoxContainer *test_row = memnew(HBoxContainer);
	test_row->add_theme_constant_override(SNAME("separation"), 6);
	providers_page->add_child(test_row);

	provider_test_button = memnew(Button);
	provider_test_button->set_text(TTRC("Probar GET /provider"));
	provider_test_button->set_tooltip_text(TTRC("Lista proveedores del runtime (GET /provider) y muestra el resultado."));
	provider_test_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicAgentDock::_on_provider_test_pressed));
	test_row->add_child(provider_test_button);

	provider_test_result = memnew(Label);
	provider_test_result->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	provider_test_result->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	test_row->add_child(provider_test_result);

	poll_timer = memnew(Timer);
	poll_timer->set_wait_time(0.5);
	poll_timer->set_autostart(true);
	poll_timer->connect("timeout", callable_mp(this, &LagoonicAgentDock::_poll));
	add_child(poll_timer);

	hook_set_connection(false);
}

void LagoonicAgentDock::set_minimal(bool p_minimal) {
	if (tabs != nullptr) {
		tabs->set_tabs_visible(!p_minimal);
		if (p_minimal) {
			tabs->set_current_tab(0);
		}
	}
	if (chat_view != nullptr) {
		chat_view->set_minimal(p_minimal);
	}
}
