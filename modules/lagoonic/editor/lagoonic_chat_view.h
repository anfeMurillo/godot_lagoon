/**************************************************************************/
/*  lagoonic_chat_view.h                                              */
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

// Vista de chat agentico (pestana "Chat").
//
// Cabecera (selector de sesion + nueva, jefe, modelo, modo), tarjeta
// LagoonicCrewView, stream de mensajes (usuario burbuja + asistente
// RichTextLabel BBCode + tool rows compactas + tarjetas de permiso +
// barra de diffs con deshacer), y entrada TextEdit con drag-drop de
// ficheros/nodos/escenas mas Enviar/Detener.
//
// No habla con el puente directamente: emite eventos (via
// set_event_handler, y senales Godot si la clase se registra algun dia en
// ClassDB) y se alimenta con setters. El LagoonicDock historico traduce
// esos eventos a llamadas del contrato (agent.sessions/*, messages/send,
// abort, respond, revert, changes/undo*) y los AgentEvent entrantes a
// setters (upsert_assistant_message, append_tool_row, show_permission_card,
// set_changes, set_busy...).
//
// Eventos salientes:
//   send_requested(text: String)
//   abort_requested()
//   permission_replied(request_id: String, reply: int)  0 = una vez,
//     1 = siempre, 2 = rechazar
//   undo_file_requested(path: String)
//   undo_task_requested()
//   revert_requested(message_id: String)
//   new_session_requested()
//   session_selected(session_id: String)
//   boss_changed(agent_id: String)
//   model_changed(model_ref: String)
//   mode_changed(mode: int)  0 = Auto, 1 = Ask, 2 = Plan
//   worker_toggled(agent_id: String, enabled: bool)  (re-emitido del Crew)
//   attachments_dropped(paths: PackedStringArray)
//   poll_tick()

#include "lagoonic_crew_view.h"

#include "core/templates/hash_map.h"

class Button;
class Label;
class OptionButton;
class PanelContainer;
class RichTextLabel;
class ScrollContainer;
class TextEdit;
class Timer;
class InputEvent;
class TextureRect;

class LagoonicChatView : public VBoxContainer {
	GDCLASS(LagoonicChatView, VBoxContainer);

public:
	enum AgentMode {
		MODE_AUTO,
		MODE_ASK,
		MODE_PLAN,
	};

	enum PermissionReply {
		REPLY_ONCE,
		REPLY_ALWAYS,
		REPLY_REJECT,
	};

	enum ToolState {
		TOOL_RUNNING,
		TOOL_DONE,
		TOOL_ERROR,
	};

private:
	OptionButton *session_option = nullptr;
	Button *new_session_button = nullptr;
	OptionButton *boss_option = nullptr;
	OptionButton *model_option = nullptr;
	OptionButton *mode_option = nullptr;

	LagoonicCrewView *crew_view = nullptr;

	ScrollContainer *messages_scroll = nullptr;
	VBoxContainer *messages_box = nullptr;

	HBoxContainer *changes_bar = nullptr;
	Label *changes_label = nullptr;
	HBoxContainer *changes_files = nullptr;
	Button *undo_task_button = nullptr;

	Label *file_guard_label = nullptr;
	Label *error_label = nullptr;

	Button *attach_button = nullptr;
	TextEdit *input = nullptr;
	Button *send_button = nullptr;
	Button *stop_button = nullptr;

	Timer *poll_timer = nullptr;
	PanelContainer *crew_card = nullptr;
	VBoxContainer *welcome_box = nullptr;
	Label *busy_label = nullptr;
	HBoxContainer *header_row = nullptr;
	Label *welcome_sub = nullptr;
	Control *suggestions_box = nullptr;
	bool minimal = false;
	bool native_look = false;
	void _apply_native_styles();
	PanelContainer *composer_panel = nullptr;
	void _update_send_state();
	void _on_input_focus(bool p_focused);
	int busy_tick = 0;
	bool connected = false;
	bool busy = false;

	Callable event_callback;
	HashMap<StringName, int> signal_argc;

	PackedStringArray session_ids;
	HashMap<String, VBoxContainer *> message_boxes;
	HashMap<String, RichTextLabel *> assistant_labels;
	HashMap<String, VBoxContainer *> tools_boxes;
	HashMap<String, HBoxContainer *> tool_rows; // Clave "msg_id|tool_id".
	HashMap<String, Label *> tool_dots; // Clave "msg_id|tool_id".
	HashMap<String, PanelContainer *> permission_cards; // Clave request_id.
	PackedStringArray change_paths;

	void _fire(const StringName &p_event, const Variant &p_a = Variant(), const Variant &p_b = Variant());

	VBoxContainer *_ensure_message_box(const String &p_msg_id, bool p_assistant);
	void _scroll_to_bottom();
	static String _escape_brackets(const String &p_text);
	static String _tool_key(const String &p_msg_id, const String &p_tool_id);
	static Color _tool_color(ToolState p_state);

	void _on_crew_event(const StringName &p_event, const Variant &p_a, const Variant &p_b) { _fire(p_event, p_a, p_b); }
	void _on_send_pressed();
	void _on_stop_pressed();
	void _on_attach_pressed();
	void _on_new_session_pressed();
	void _on_session_selected(int p_index);
	void _on_boss_selected(int p_index);
	void _on_model_selected(int p_index);
	void _on_mode_selected(int p_index);
	void _on_permission_button(const String &p_request_id, int p_reply);
	void _on_undo_file_button(const String &p_path);
	void _on_undo_task_pressed();
	void _on_revert_button(const String &p_msg_id);
	void _poll();
	void _on_input_gui_input(const Ref<InputEvent> &p_event);
	void _on_suggestion_pressed(const String &p_text);
	void _set_welcome_visible(bool p_visible);
	void _apply_avatar_icon(TextureRect *p_rect);

protected:
	void _notification(int p_what);
	static void _bind_methods();

	// Acepta ficheros del FileSystemDock ("files") y rutas de nodos
	// ("nodes"); las escenas llegan como ficheros .tscn.
	virtual bool can_drop_data(const Point2 &p_point, const Variant &p_data) const override;
	virtual void drop_data(const Point2 &p_point, const Variant &p_data) override;

public:
	void set_event_handler(const Callable &p_handler) { event_callback = p_handler; }

	void set_sessions(const Array &p_sessions); // Dicts {id, title}.
	void set_current_session(const String &p_session_id);
	String get_current_session() const;

	void set_boss_options(const PackedStringArray &p_agent_ids);
	void set_boss_agent(const String &p_agent_id);
	String get_boss_agent() const;

	void set_model_options(const PackedStringArray &p_model_refs);
	void set_current_model(const String &p_model_ref);
	String get_current_model() const;

	void set_mode(int p_mode);
	int get_mode() const;

	void add_user_message(const String &p_text);
	void upsert_assistant_message(const String &p_msg_id, const String &p_text);
	void append_tool_row(const String &p_msg_id, const String &p_tool_id, const String &p_label, int p_state, const String &p_detail);
	void update_tool_state(const String &p_msg_id, const String &p_tool_id, int p_state, const String &p_detail);

	void show_permission_card(const String &p_request_id, const String &p_session_id, const String &p_title, const String &p_detail);
	void close_permission_card(const String &p_request_id);

	void set_changes(const PackedStringArray &p_paths, int p_additions, int p_deletions);
	void clear_changes();

	void notify_file_changed(const String &p_path);
	void clear_file_guard();
	void show_error(const String &p_text);
	void clear_error();

	void set_busy(bool p_busy);
	bool is_busy() const { return busy; }
	void set_connected(bool p_connected);
	void clear_messages();
	void refresh_icons();

	LagoonicCrewView *get_crew_view() const { return crew_view; }
	// Modo minimo (Vibe Coding): solo saludo, mensajes y caja de texto.
	void set_minimal(bool p_minimal);
	// Aspecto nativo del editor (panel del editor clasico): sin saludo y con
	// la caja de texto como los campos de Godot (p. ej. "Filtrar Archivos").
	void set_native_look(bool p_native);

	LagoonicChatView();
};
