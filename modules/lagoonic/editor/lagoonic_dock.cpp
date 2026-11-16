/**************************************************************************/
/*  lagoonic_dock.cpp                                                   */
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

#include "lagoonic_dock.h"

#include "lagoonic_agent_dock.h"
#include "lagoonic_agent_settings.h"
#include "lagoonic_model_catalog.h"
#include "lagoonic_vibe_theme.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/time.h"
#include "editor/settings/editor_command_palette.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/rich_text_label.h"
#include "scene/main/timer.h"

// Nombre del singleton de Engine que expone el modulo puente
// (modules/lagoonic). Solo acoplamiento por nombre: sin includes.
// NOTA: no usar `static const StringName` a ambito de fichero (ni SNAME
// ahi): se construiria antes de StringName::setup() y dispara
// `ERROR: Condition "!configured" is true`. Estatico de funcion = init
// perezosa, seguro.
static const StringName &lagoonic_bridge_singleton() {
	static StringName s("LagoonicBridge");
	return s;
}

Object *LagoonicDock::_get_bridge() const {
	Engine *engine = Engine::get_singleton();
	if (engine == nullptr || !engine->has_singleton(lagoonic_bridge_singleton())) {
		return nullptr;
	}
	return engine->get_singleton_object(lagoonic_bridge_singleton());
}

void LagoonicDock::_ensure_bridge_signal(Object *p_bridge) {
	if (bridge_signal_hooked || p_bridge == nullptr) {
		return;
	}
	// La senal es opcional: si el puente no la define, el dock sigue
	// funcionando por sondeo (poll) + comandos.
	if (p_bridge->has_signal(SNAME("activity_message"))) {
		p_bridge->connect(SNAME("activity_message"), callable_mp(this, &LagoonicDock::log_activity));
		bridge_signal_hooked = true;
	}
}

void LagoonicDock::_poll_bridge() {
	if (!is_visible_in_tree()) {
		return;
	}
	Object *bridge = _get_bridge();
	if (bridge == nullptr) {
		bridge_signal_hooked = false;
		set_connection_state(STATE_BRIDGE_MISSING);
		return;
	}
	_ensure_bridge_signal(bridge);
	bool connected = false;
	if (bridge->has_method(SNAME("is_runtime_connected"))) {
		connected = bool(bridge->call(SNAME("is_runtime_connected")));
	}
	set_connection_state(connected ? STATE_CONNECTED : STATE_DISCONNECTED);
}

void LagoonicDock::set_connection_state(ConnectionState p_state) {
	if (connection_state == p_state) {
		return;
	}
	connection_state = p_state;
	_refresh_status_ui();
}

void LagoonicDock::_refresh_status_ui() {
	if (status_value == nullptr) {
		return;
	}
	switch (connection_state) {
		case STATE_CONNECTED:
			status_value->set_text(TTRC("Conectado"));
			status_value->add_theme_color_override(SNAME("font_color"), Color(0.35, 0.85, 0.4));
			break;
		case STATE_DISCONNECTED:
			status_value->set_text(TTRC("Desconectado"));
			status_value->add_theme_color_override(SNAME("font_color"), Color(0.95, 0.75, 0.25));
			break;
		case STATE_BRIDGE_MISSING:
		default:
			status_value->set_text(TTRC("Puente no disponible"));
			status_value->add_theme_color_override(SNAME("font_color"), Color(0.9, 0.35, 0.3));
			break;
	}
	const bool can_command = (connection_state == STATE_CONNECTED);
	if (status_row != nullptr) {
		// The bridge only drives Play/Stop/Photo (hidden: the editor has
		// them); the chat talks to the runtime host and never needs it.
		status_row->set_visible(false);
	}
	if (play_button != nullptr) {
		play_button->set_disabled(!can_command);
	}
	if (stop_button != nullptr) {
		stop_button->set_disabled(!can_command);
	}
	if (snapshot_button != nullptr) {
		snapshot_button->set_disabled(!can_command);
	}
}

void LagoonicDock::_refresh_icons() {
	if (play_button != nullptr) {
		play_button->set_button_icon(get_editor_theme_icon(SNAME("Play")));
	}
	if (stop_button != nullptr) {
		stop_button->set_button_icon(get_editor_theme_icon(SNAME("Stop")));
	}
	if (snapshot_button != nullptr) {
		snapshot_button->set_button_icon(get_editor_theme_icon(SNAME("Camera")));
	}
	if (agent_dock != nullptr) {
		agent_dock->refresh_icons();
	}
}

void LagoonicDock::_send_bridge_command(const StringName &p_method, const String &p_log_ok) {
	Object *bridge = _get_bridge();
	if (bridge == nullptr) {
		log_activity(TTRC("Puente Lagoonic no disponible (modulo modules/lagoonic no registrado)."));
		return;
	}
	if (!bridge->has_method(p_method)) {
		log_activity(vformat(TTRC("El puente no implementa '%s'."), String(p_method)));
		return;
	}
	bridge->call(p_method);
	log_activity(p_log_ok);
}

void LagoonicDock::_on_reconnect_pressed() {
	Object *bridge = _get_bridge();
	if (bridge == nullptr) {
		log_activity(TTRC("Sin puente: reintentando deteccion del singleton 'LagoonicBridge'..."));
		_poll_bridge();
		return;
	}
	if (bridge->has_method(SNAME("connect_runtime"))) {
		bridge->call(SNAME("connect_runtime"));
		log_activity(TTRC("Solicitud de conexion enviada al puente."));
	} else {
		log_activity(TTRC("El puente no implementa 'connect_runtime'."));
	}
	_poll_bridge();
}

void LagoonicDock::_on_play_pressed() {
	_send_bridge_command(SNAME("play"), TTRC("Orden Jugar enviada."));
}

void LagoonicDock::_on_stop_pressed() {
	_send_bridge_command(SNAME("stop"), TTRC("Orden Parar enviada."));
}

void LagoonicDock::_on_snapshot_pressed() {
	_send_bridge_command(SNAME("capture_screenshot"), TTRC("Orden Foto enviada."));
}

void LagoonicDock::_on_clear_pressed() {
	clear_activity();
}

void LagoonicDock::_ensure_runtime_host() {
	if (runtime_host.is_valid()) {
		return;
	}
	runtime_host.instantiate();
	runtime_host->set_opencode_bin(LagoonicModelCatalog::opencode_binary());
	runtime_host->connect(SNAME("runtime_event"), callable_mp(this, &LagoonicDock::_on_runtime_event));
	runtime_host->connect(SNAME("runtime_status"), callable_mp(this, &LagoonicDock::_on_runtime_status));
}

void LagoonicDock::_on_runtime_event(const Dictionary &p_event) {
	if (agent_dock != nullptr) {
		agent_dock->hook_push_agent_event(p_event);
	}
	// Al terminar un turno el servidor ya puso titulo al chat: refresca.
	if (String(p_event.get("type", "")) == "busy" && !bool(p_event.get("busy", true)) && runtime_host.is_valid() && agent_dock != nullptr && agent_dock->get_chat_view() != nullptr) {
		agent_dock->get_chat_view()->set_sessions(runtime_host->list_sessions());
	}
}

void LagoonicDock::_on_runtime_status(const String &p_status) {
	if (p_status.begins_with("session:") && agent_dock != nullptr && agent_dock->get_chat_view() != nullptr) {
		// El host creo o cambio la sesion (p. ej. al primer envio).
		agent_dock->get_chat_view()->set_current_session(p_status.substr(8));
	}
	if (agent_dock != nullptr) {
		// Like the vibe workspace: the input stays enabled so the first
		// message (or a retry after a crash) starts the agent lazily.
		agent_dock->hook_set_connection(runtime_host.is_valid());
	}
	if (!p_status.begins_with("adopt:") && p_status != "reload-requested") {
		log_activity("runtime:" + p_status);
	}
}

void LagoonicDock::_on_agent_event(const StringName &p_event, const Variant &p_a, const Variant &p_b) {
	_ensure_runtime_host();
	LagoonicChatView *chat = (agent_dock != nullptr) ? agent_dock->get_chat_view() : nullptr;
	const String cwd = ProjectSettings::get_singleton()->globalize_path("res://");
	if (p_event == SNAME("send_requested")) {
		// Files dropped on the chat go with this message.
		runtime_host->set_next_attachments(p_b.get_type() == Variant::PACKED_STRING_ARRAY ? PackedStringArray(p_b) : PackedStringArray());
		const String session = (chat != nullptr) ? chat->get_current_session() : String();
		const int mode = (chat != nullptr) ? chat->get_mode() : 0;
		String model = (chat != nullptr) ? chat->get_current_model() : String();
		if (model.is_empty()) {
			model = LagoonicAgentSettings::get_model(); // Configuracion > Agentes.
		}
		// Configuracion > Equipo is the source of truth for the boss; the
		// chat picker writes there too (boss_changed below).
		const String boss = LagoonicAgentSettings::get_boss();
		if (chat != nullptr) {
			chat->set_boss_agent(boss);
		}
		// The team (boss + workers from Configuracion > Equipo) goes to the
		// host before each message, so changes apply right away.
		runtime_host->set_project_dir(cwd);
		const Array workers = LagoonicAgentSettings::crew_workers();
		runtime_host->set_crew(boss, workers);
		bool opencode_worker = false;
		for (int i = 0; i < workers.size(); i++) {
			opencode_worker = opencode_worker || String(Dictionary(workers[i]).get("id", "")) == "opencode";
		}
		if (boss != "opencode") {
			// Claude Code, Codex... answer through their ACP adapter; OpenCode
			// only starts when it works as one of the workers.
			if (opencode_worker) {
				runtime_host->ensure_server(cwd, model, LagoonicAgentSettings::secret_env());
			}
			if (runtime_host->send_prompt(session, String(p_a), mode, model) != OK && chat != nullptr) {
				chat->set_busy(false);
			}
		} else if (runtime_host->ensure_server(cwd, model, LagoonicAgentSettings::secret_env()) == OK) {
			runtime_host->send_prompt(session, String(p_a), mode, model, (chat != nullptr) ? chat->get_effort() : LagoonicAgentSettings::get_effort());
		} else if (chat != nullptr) {
			chat->set_busy(false);
		}
	} else if (p_event == SNAME("abort_requested")) {
		runtime_host->abort();
	} else if (p_event == SNAME("permission_replied")) {
		runtime_host->respond_permission(String(p_a), int(p_b));
	} else if (p_event == SNAME("revert_requested")) {
		runtime_host->revert_message(String(p_a));
	} else if (agent_dock != nullptr && agent_dock->handle_session_event(runtime_host, p_event, p_a, cwd, LagoonicAgentSettings::secret_env(), (chat != nullptr && !chat->get_current_model().is_empty()) ? chat->get_current_model() : LagoonicAgentSettings::get_model())) {
		// Chats del proyecto: nuevo, cambiar, borrar, listar.
	} else if (p_event == SNAME("mode_changed")) {
		LagoonicAgentSettings::set_mode(int(p_a)); // El chat recuerda la ultima eleccion.
	} else if (p_event == SNAME("model_changed")) {
		LagoonicAgentSettings::set_model(String(p_a));
	} else if (p_event == SNAME("effort_changed")) {
		LagoonicAgentSettings::set_effort(String(p_a));
	} else if (p_event == SNAME("boss_changed")) {
		LagoonicAgentSettings::set_boss(String(p_a)); // Same choice as Configuracion > Equipo.
		runtime_host->set_crew_boss(String(p_a));
	} else if (p_event == SNAME("worker_toggled")) {
		runtime_host->set_crew_worker(String(p_a), bool(p_b));
	} else if (p_event == SNAME("poll_tick")) {
		runtime_host->poll_connection();
	} else if (p_event == SNAME("provider_key_save_requested")) {
		// Mismo almacen que Configuracion > Agentes.
		const bool ok = LagoonicAgentSettings::set_provider_key(String(p_a), String(p_b)) == OK;
		if (agent_dock != nullptr) {
			agent_dock->set_provider_has_key(String(p_a), LagoonicAgentSettings::has_provider_key(String(p_a)));
		}
		log_activity(ok ? String::utf8("Clave guardada.") : String::utf8("No se pudo guardar la clave."));
	} else if (p_event == SNAME("provider_key_remove_requested")) {
		LagoonicAgentSettings::remove_provider_key(String(p_a));
		if (agent_dock != nullptr) {
			agent_dock->set_provider_has_key(String(p_a), false);
		}
	} else if (p_event == SNAME("undo_file_requested") || p_event == SNAME("undo_task_requested")) {
		log_activity(String::utf8("Deshacer aún no soportado por el runtime (solo revert por mensaje)."));
	} else {
		// provider_*, agent_connect/disconnect/docs, open_terminal,
		// attachments_dropped, model/mode_changed: territorio Vault/puente
		// o estado local del chat; se conserva el log.
		String detail;
		if (p_a.get_type() == Variant::STRING) {
			detail = String(p_a).strip_edges().left(160);
		}
		String line = "agent:" + String(p_event);
		if (!detail.is_empty()) {
			line += " " + detail;
		}
		log_activity(line);
	}
}

void LagoonicDock::log_activity(const String &p_text) {
	if (activity_log == nullptr) {
		return;
	}
	if (activity_lines >= ACTIVITY_MAX_LINES) {
		activity_log->clear();
		activity_lines = 0;
	}
	String timestamp;
	if (Time::get_singleton() != nullptr) {
		timestamp = Time::get_singleton()->get_time_string_from_system();
	}
	activity_log->append_text(vformat("[%s] %s\n", timestamp, p_text));
	activity_lines += 1;
}

void LagoonicDock::clear_activity() {
	if (activity_log != nullptr) {
		activity_log->clear();
	}
	activity_lines = 0;
}

void LagoonicDock::_apply_palette() {
	// Sin tema propio: aqui manda el tema del editor, que ya sigue la
	// paleta de Lagoonic (LagoonicVibeTheme::apply_to_editor).
	if (agent_dock != nullptr) {
		agent_dock->refresh_icons();
	}
}

void LagoonicDock::_notification(int p_notification) {
	switch (p_notification) {
		case NOTIFICATION_ENTER_TREE: {
			LagoonicVibeTheme::get_notifier()->connect(SNAME("changed"), callable_mp(this, &LagoonicDock::_apply_palette));
			_apply_palette();
			_refresh_icons();
			_refresh_status_ui();
		} break;

		case NOTIFICATION_EXIT_TREE: {
			LagoonicVibeTheme::get_notifier()->disconnect(SNAME("changed"), callable_mp(this, &LagoonicDock::_apply_palette));
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			_refresh_icons();
		} break;
	}
}

void LagoonicDock::_bind_methods() {
	ClassDB::bind_method(D_METHOD("log_activity", "text"), &LagoonicDock::log_activity);
	ClassDB::bind_method(D_METHOD("clear_activity"), &LagoonicDock::clear_activity);
}

LagoonicDock::LagoonicDock() {
	singleton = this;
	set_name(TTRC("Lagoonic"));
	set_icon_name(SNAME("Godot"));
	set_dock_shortcut(ED_SHORTCUT_AND_COMMAND("docks/open_lagoonic", TTRC("Open Lagoonic Dock")));
	set_default_slot(EditorDock::DOCK_SLOT_RIGHT_BL);

	VBoxContainer *main_vb = memnew(VBoxContainer);
	main_vb->add_theme_constant_override(SNAME("separation"), 6);
	add_child(main_vb);

	// --- Estado de conexion ---
	HBoxContainer *status_hb = memnew(HBoxContainer);
	status_hb->add_theme_constant_override(SNAME("separation"), 6);
	main_vb->add_child(status_hb);
	status_row = status_hb;

	status_label = memnew(Label);
	status_label->set_text(TTRC("Estado:"));
	status_hb->add_child(status_label);
	status_label->set_visible(false);


	status_value = memnew(Label);
	status_value->set_text(TTRC("Puente no disponible"));
	status_value->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	status_hb->add_child(status_value);

	reconnect_button = memnew(Button);
	reconnect_button->set_text(TTRC("Reconectar"));
	reconnect_button->set_tooltip_text(TTRC("Reintenta detectar el puente Lagoonic y conectar con el runtime."));
	reconnect_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicDock::_on_reconnect_pressed));
	status_hb->add_child(reconnect_button);

	// --- Controles: Jugar / Parar / Foto ---
	HBoxContainer *controls_hb = memnew(HBoxContainer);
	controls_hb->add_theme_constant_override(SNAME("separation"), 6);
	main_vb->add_child(controls_hb);
	controls_hb->set_visible(false); // Jugar/Parar ya estan en la barra del editor.

	play_button = memnew(Button);
	play_button->set_text(TTRC("Jugar"));
	play_button->set_tooltip_text(TTRC("Pide al puente Lagoonic iniciar la sesion de juego."));
	play_button->set_disabled(true);
	play_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicDock::_on_play_pressed));
	controls_hb->add_child(play_button);

	stop_button = memnew(Button);
	stop_button->set_text(TTRC("Parar"));
	stop_button->set_tooltip_text(TTRC("Pide al puente Lagoonic detener la sesion de juego."));
	stop_button->set_disabled(true);
	stop_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicDock::_on_stop_pressed));
	controls_hb->add_child(stop_button);

	snapshot_button = memnew(Button);
	snapshot_button->set_text(TTRC("Foto"));
	snapshot_button->set_tooltip_text(TTRC("Pide al puente Lagoonic capturar una foto del runtime."));
	snapshot_button->set_disabled(true);
	snapshot_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicDock::_on_snapshot_pressed));
	controls_hb->add_child(snapshot_button);

	// --- Actividad ---
	HBoxContainer *activity_header = memnew(HBoxContainer);
	main_vb->add_child(activity_header);
	activity_header->set_visible(false);

	Label *activity_title = memnew(Label);
	activity_title->set_text(TTRC("Actividad"));
	activity_title->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	activity_header->add_child(activity_title);

	clear_button = memnew(Button);
	clear_button->set_text(TTRC("Limpiar"));
	clear_button->set_tooltip_text(TTRC("Limpia el area de actividad."));
	clear_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicDock::_on_clear_pressed));
	activity_header->add_child(clear_button);

	activity_log = memnew(RichTextLabel);
	activity_log->set_custom_minimum_size(Size2(0, 120));
	activity_log->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	activity_log->set_scroll_follow(true);
	activity_log->set_selection_enabled(true);
	main_vb->add_child(activity_log);
	activity_log->set_visible(false); // Registro interno; no hace falta a la vista.

	// Panel agentico (Chat/Crew, Agentes CLI, Proveedores). Vive dentro del
	// propio dock: un solo programa en PC, sin ventana externa.
	agent_dock = memnew(LagoonicAgentDock);
	agent_dock->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	agent_dock->set_custom_minimum_size(Size2(0, 320));
	main_vb->add_child(agent_dock);
	agent_dock->set_minimal(true);
	if (agent_dock->get_chat_view() != nullptr) {
		agent_dock->get_chat_view()->set_native_look(true);
	}
	agent_dock->set_event_handler(callable_mp(this, &LagoonicDock::_on_agent_event));
	LagoonicAgentSettings::apply_to_chat(agent_dock->get_chat_view());
	for (int i = 0; i < LagoonicAgentSettings::PROVIDER_COUNT; i++) {
		const LagoonicAgentSettings::Provider &p = LagoonicAgentSettings::PROVIDERS[i];
		agent_dock->set_provider_has_key(p.id, LagoonicAgentSettings::has_provider_key(p.id));
		if (p.is_url) {
			const String url = LagoonicAgentSettings::get_provider_url(p.id);
			if (!url.is_empty()) {
				agent_dock->set_ollama_url(url);
			}
		}
	}
	_ensure_runtime_host();
	agent_dock->hook_set_connection(true);

	// Sondeo periodico del puente (solo actualiza UI si el dock es visible).
	poll_timer = memnew(Timer);
	poll_timer->set_wait_time(0.5);
	poll_timer->set_autostart(true);
	poll_timer->connect("timeout", callable_mp(this, &LagoonicDock::_poll_bridge));
	add_child(poll_timer);

	_refresh_status_ui();
}

LagoonicDock::~LagoonicDock() {
	singleton = nullptr;
}
