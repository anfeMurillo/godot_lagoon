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
class MenuButton;
class PanelContainer;
class RichTextLabel;
class ScrollContainer;
class TextEdit;
class Timer;
class InputEvent;
class TextureRect;
class AcceptDialog;
class ConfirmationDialog;

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
	OptionButton *effort_option = nullptr; // Variantes de razonamiento del modelo.
	HBoxContainer *composer_controls = nullptr;
	HashMap<String, PackedStringArray> model_variants; // ref -> variantes.
	String preferred_model; // Se aplica cuando llega la lista de modelos.
	String preferred_effort;
	void _rebuild_effort_options();
	static String _effort_label(const String &p_variant);
	void _on_effort_selected(int p_index);

	// Agente que responde el chat. OpenCode usa el catalogo de modelos;
	// los agentes ACP (Claude Code, Codex...) traen sus propias opciones.
	String chat_boss = "opencode";
	bool acp_mode = false;
	Array oc_models; // Catalogo de OpenCode (set_models), para volver a el.
	String oc_default_ref;
	Array acp_options; // Ver LagoonicAcpRunner::parse_options().
	Dictionary acp_choices; // id de opcion -> valor elegido.
	MenuButton *acp_more_button = nullptr; // Resto de opciones del agente.
	void _render_opencode_models();
	void _render_acp_options();
	Dictionary _acp_option_by_category(const String &p_category) const;
	String _acp_value(const Dictionary &p_option) const;
	void _on_acp_more_id(int p_id, const String &p_option_id);

	LagoonicCrewView *crew_view = nullptr;

	ScrollContainer *messages_scroll = nullptr;
	// Degradado del color de la tarjeta sobre los mensajes de arriba: se
	// desvanecen al pasar bajo la cabecera (solo dentro de una tarjeta Bento).
	Control *fade_overlay = nullptr;
	Ref<class GradientTexture2D> fade_texture;
	Color fade_color = Color(0, 0, 0, 0);
	void _draw_fade();
	VBoxContainer *messages_box = nullptr;

	HBoxContainer *changes_bar = nullptr;
	Label *changes_label = nullptr;
	HBoxContainer *changes_files = nullptr;
	Button *undo_task_button = nullptr;

	Label *file_guard_label = nullptr;
	Label *error_label = nullptr;
	Label *queued_label = nullptr; // "N mensajes en cola" while an ACP agent works.

	Button *attach_button = nullptr;
	// Archivos que se enviaran con el proximo mensaje (rutas absolutas):
	// arrastrados desde Finder/Explorador o desde el panel FileSystem.
	PackedStringArray pending_attachments;
	class HFlowContainer *attachments_row = nullptr;
	void _rebuild_attachments();
	void _remove_attachment(int p_index);
	void _on_os_files_dropped(const PackedStringArray &p_files);
	TextEdit *input = nullptr;
	Button *send_button = nullptr;
	Button *stop_button = nullptr;

	Timer *poll_timer = nullptr;
	PanelContainer *crew_card = nullptr;
	VBoxContainer *welcome_box = nullptr;
	Label *busy_label = nullptr;
	HBoxContainer *header_row = nullptr;
	TextureRect *welcome_logo = nullptr; // Silueta del agente activo.
	String welcome_logo_agent;
	void _update_welcome_logo();
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
	Vector<Callable> listeners;
	void _notify_listeners(const StringName &p_what);
	bool plan_card_suppressed = false;
	int user_message_count = 0;
	HashMap<String, uint64_t> worked_ms; // msg_id -> tiempo trabajado.
	Array sessions_data; // [{id, title, updated_at}] para el dialogo de chats.
	String current_session_id;

	// --- Barra de chat (modo Vibe Coding) + dialogo "Chats del proyecto" ---
	HBoxContainer *session_bar = nullptr;
	Label *session_title_label = nullptr;
	Button *chats_button = nullptr;
	Button *bar_new_button = nullptr;
	PanelContainer *chats_panel = nullptr; // Panel dentro del chat, no ventana.
	VBoxContainer *chats_list = nullptr;
	Label *chats_busy_note = nullptr;
	String pending_delete_id;
	String pending_rename_id;
	void _on_chat_rename_pressed(const String &p_session_id);
	void _on_rename_submitted(const String &p_text);
	class LineEdit *rename_edit = nullptr;
	void _on_chats_pressed();
	void _rebuild_chats_list();
	void _on_chat_row_pressed(const String &p_session_id);
	void _on_chat_delete_pressed(const String &p_session_id);
	void _on_delete_confirmed();
	void _on_delete_cancelled();
	void _close_chats();
	void _update_session_title();

	// --- Tarjeta de plan sobre la caja de texto ---
	PanelContainer *plan_card = nullptr;
	Label *plan_title = nullptr;
	Label *plan_current = nullptr;
	Button *plan_toggle = nullptr;
	VBoxContainer *plan_list = nullptr;
	Array plan_todos;
	void _rebuild_plan();
	void _on_plan_toggle();

	// --- Boton "Lo ultimo" y autodesplazamiento ---
	Button *latest_button = nullptr;
	bool stick_to_bottom = true;
	void _on_scroll_changed(double p_value);
	void _update_latest_button();
	void _on_latest_pressed();

	// --- Actividad del turno ---
	uint64_t turn_start_msec = 0;
	String turn_message_id; // Ultimo mensaje del asistente del turno.
	String last_activity_label;
	HashMap<String, Label *> worked_labels; // msg_id -> "Trabajo durante".
	HashMap<String, Button *> copy_buttons; // msg_id -> "Copiar".
	void _on_copy_pressed(const String &p_msg_id);
	HashMap<String, Button *> activity_headers; // msg_id -> "N pasos".
	HashMap<String, int> activity_counts;
	HashMap<String, String> tool_raw_details; // Solo interno (rutas de medios); nunca se muestra.
	HashMap<String, String> tool_phrases; // Frase sencilla que ve el usuario.
	HashMap<String, Button *> tool_buttons; // Clave "msg_id|tool_id".
	HashMap<String, String> tool_raw_labels; // Clave "msg_id|tool_id".
	static void _on_meta_clicked(const Variant &p_meta);
	void _on_activity_toggle(const String &p_msg_id);
	void _refresh_tool_icon(const String &p_key, int p_state);
	StringName _tool_icon_name(const String &p_label) const;
	static String _format_duration(uint64_t p_msec);
	void _finish_turn(uint64_t p_worked_ms = 0);
	void _set_worked(const String &p_msg_id, uint64_t p_msec);

	// --- Archivos del agente (como AgentOutputCards de movil) ---
	HashMap<String, String> assistant_texts; // msg_id -> texto crudo.
	HashMap<String, VBoxContainer *> media_boxes; // msg_id -> tarjetas.
	PackedStringArray turn_message_ids; // Mensajes del turno en curso.
	enum MediaKind {
		MEDIA_NONE,
		MEDIA_IMAGE,
		MEDIA_AUDIO,
		MEDIA_MODEL,
		MEDIA_VIDEO,
		MEDIA_FILE,
	};
	static MediaKind _media_kind(const String &p_path);
	String _resolve_media(const String &p_token) const;
	void _attach_outputs(const String &p_msg_id, const PackedStringArray &p_source_ids);
	void _add_media_card(VBoxContainer *p_box, const String &p_res_path);
	void _on_media_open(const String &p_res_path);
	void _on_media_show(const String &p_res_path);
	void _on_media_play(const String &p_res_path, ObjectID p_button);
	void _on_media_preview(const String &p_path, const Ref<Texture2D> &p_preview, const Ref<Texture2D> &p_small, ObjectID p_rect);
	class AudioStreamPlayer *media_player = nullptr;
	ObjectID media_play_button;
	HashMap<String, VBoxContainer *> message_boxes;
	HashMap<String, class LagoonicMarkdownView *> assistant_bodies;
	// Narracion intermedia del agente ("Voy a revisar..."): va a la
	// actividad como en movil. Clave "msg_id|text|part_id".
	void _update_activity_header(const String &p_msg_id);
	// "Shell · ps aux | grep …" -> "Revisando el rendimiento" (como
	// ToolNarration de movil): el usuario nunca ve comandos ni rutas.
	static String _narrate_tool(const String &p_label, const String &p_detail);
	// Pasos seguidos con la misma frase se muestran como una sola fila.
	void _collapse_repeats(const String &p_msg_id);
	HashMap<String, VBoxContainer *> tools_boxes;
	HashMap<String, HBoxContainer *> tool_rows; // Clave "msg_id|tool_id".
	HashMap<String, Label *> tool_dots; // Clave "msg_id|tool_id".
	HashMap<String, PanelContainer *> permission_cards; // Clave request_id.
	PackedStringArray change_paths;

	void _fire(const StringName &p_event, const Variant &p_a = Variant(), const Variant &p_b = Variant());

	VBoxContainer *_ensure_message_box(const String &p_msg_id, bool p_assistant);
	void _scroll_to_bottom();
	static String _escape_brackets(const String &p_text);
	static String _inline_markdown(const String &p_line);
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
	// Adds files (absolute or res:// paths) to the next message.
	void add_attachments(const PackedStringArray &p_paths);
	PackedStringArray get_pending_attachments() const { return pending_attachments; }
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

	// Lista de modelos: Array de {ref "proveedor/modelo", name, provider,
	// provider_name, variants: PackedStringArray}. p_default_ref es el que
	// el agente usa si no se elige ninguno.
	void set_models(const Array &p_models, const String &p_default_ref);
	// Variante de esfuerzo ("" = la normal del modelo).
	// Opciones del agente ACP activo, tal como las ofrece, y lo elegido.
	void set_acp_options(const String &p_agent_id, const Array &p_options, const Dictionary &p_choices);
	String get_chat_boss() const { return chat_boss; }
	void set_effort(const String &p_variant);
	String get_effort() const;

	void add_user_message(const String &p_text);
	void upsert_assistant_message(const String &p_msg_id, const String &p_text);
	// Orden real {kind: text|tool, id, text?}: el texto seguido de pasos pasa
	// a la actividad y la burbuja muestra solo la respuesta final.
	void set_assistant_timeline(const String &p_msg_id, const Array &p_timeline);
	void append_tool_row(const String &p_msg_id, const String &p_tool_id, const String &p_label, int p_state, const String &p_detail);
	void update_tool_state(const String &p_msg_id, const String &p_tool_id, int p_state, const String &p_detail);

	void show_permission_card(const String &p_request_id, const String &p_session_id, const String &p_title, const String &p_detail);
	void close_permission_card(const String &p_request_id);

	// Plan del agente: Array de {id, text, state} (pending, in_progress,
	// completed, cancelled). Vacio = oculta la tarjeta.
	void set_todos(const Array &p_todos);
	Array get_todos() const { return plan_todos; }
	// Paneles Bento: avisos "todos", "turn_finished", "session", "cleared".
	void add_listener(const Callable &p_callback) { listeners.push_back(p_callback); }
	// Con el panel Plan abierto, la tarjeta de plan sobre la caja sobra.
	void set_plan_card_suppressed(bool p_suppressed);
	// Datos del panel Contexto: chat, modelo, modo, mensajes, pasos, tiempo.
	Dictionary get_context_info() const;
	// Escribe en la caja de texto (p. ej. "@res://player.png ").
	void insert_in_composer(const String &p_text);
	HBoxContainer *get_session_bar() const { return session_bar; }
	// Color del fondo sobre el que esta el chat (alfa 0 = sin desvanecido).
	void set_fade_color(const Color &p_color);
	// Tarjeta de archivo (imagen, 3D, audio...) como las del chat.
	void add_media_card_to(VBoxContainer *p_box, const String &p_res_path) { _add_media_card(p_box, p_res_path); }
	// Mensaje del historial (al cambiar de chat): sin tocar el estado de
	// trabajo; p_worked_ms > 0 pone "Trabajo durante".
	void add_history_message(const Dictionary &p_message);
	// Llamar cuando el runtime avisa de que el turno termino (busy=false).
	void notify_turn_finished();
	// Markdown del agente -> BBCode de RichTextLabel (publico para tests).
	static String markdown_to_bbcode(const String &p_markdown);

	void set_changes(const PackedStringArray &p_paths, int p_additions, int p_deletions);
	void clear_changes();

	void notify_file_changed(const String &p_path);
	void clear_file_guard();
	void show_error(const String &p_text);
	// Messages waiting for the agent's current step to end (0 hides the note).
	void set_queued_count(int p_count);
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
