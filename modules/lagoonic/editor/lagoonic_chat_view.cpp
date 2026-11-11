/**************************************************************************/
/*  lagoonic_chat_view.cpp                                            */
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

#include "lagoonic_chat_view.h"

#include "lagoonic_vibe_theme.h"

#include "core/input/input_event.h"
#include "core/os/keyboard.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/flow_container.h"
#include "scene/gui/texture_rect.h"
#include "scene/resources/font.h"
#include "scene/resources/style_box_flat.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/option_button.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/text_edit.h"
#include "scene/main/timer.h"
#include "servers/text/text_server.h"

static const char *LAGOONIC_BOSS_IDS[6] = { "opencode", "claude", "codex", "qwen", "kimi", "goose" };

void LagoonicChatView::_fire(const StringName &p_event, const Variant &p_a, const Variant &p_b) {
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

String LagoonicChatView::_escape_brackets(const String &p_text) {
	// El texto del agente es literal: se escapan corchetes para que un "["
	// sin cerrar no rompa el parseado BBCode del RichTextLabel.
	return p_text.replace("[", "[lb]");
}

String LagoonicChatView::_tool_key(const String &p_msg_id, const String &p_tool_id) {
	return p_msg_id + "|" + p_tool_id;
}

Color LagoonicChatView::_tool_color(ToolState p_state) {
	switch (p_state) {
		case TOOL_DONE:
			return Color(0.35, 0.85, 0.4);
		case TOOL_ERROR:
			return Color(0.9, 0.35, 0.3);
		case TOOL_RUNNING:
		default:
			return Color(0.95, 0.75, 0.25);
	}
}

VBoxContainer *LagoonicChatView::_ensure_message_box(const String &p_msg_id, bool p_assistant) {
	if (message_boxes.has(p_msg_id)) {
		return message_boxes[p_msg_id];
	}
	_set_welcome_visible(false);

	VBoxContainer *box = memnew(VBoxContainer);
	box->add_theme_constant_override(SNAME("separation"), 6);
	messages_box->add_child(box);
	message_boxes[p_msg_id] = box;

	if (p_assistant) {
		HBoxContainer *row = memnew(HBoxContainer);
		row->add_theme_constant_override(SNAME("separation"), 10 * EDSCALE);
		box->add_child(row);

		// Avatar: el nenufar de Lagoonic en un circulo de agua.
		PanelContainer *avatar = memnew(PanelContainer);
		avatar->set_theme_type_variation(LagoonicVibeTheme::AVATAR);
		avatar->set_v_size_flags(Control::SIZE_SHRINK_BEGIN);
		row->add_child(avatar);
		TextureRect *logo = memnew(TextureRect);
		logo->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
		logo->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
		logo->set_custom_minimum_size(Size2(22, 22) * EDSCALE);
		avatar->add_child(logo);
		_apply_avatar_icon(logo);

		VBoxContainer *content = memnew(VBoxContainer);
		content->add_theme_constant_override(SNAME("separation"), 6);
		content->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		row->add_child(content);

		HBoxContainer *header = memnew(HBoxContainer);
		header->add_theme_constant_override(SNAME("separation"), 6);
		content->add_child(header);

		Label *role = memnew(Label);
		role->set_text("Lagoonic");
		role->set_theme_type_variation(LagoonicVibeTheme::ROLE_LABEL);
		role->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		header->add_child(role);

		Button *revert = memnew(Button);
		revert->set_text(TTRC("Deshacer"));
		revert->set_theme_type_variation(LagoonicVibeTheme::GHOST_BUTTON);
		revert->set_tooltip_text(TTRC("Revierte todo desde este mensaje (solo OpenCode)."));
		revert->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChatView::_on_revert_button).bind(p_msg_id));
		header->add_child(revert);

		PanelContainer *bubble = memnew(PanelContainer);
		bubble->set_theme_type_variation(LagoonicVibeTheme::AGENT_BUBBLE);
		bubble->set_visible(false); // Se muestra con el primer texto.
		content->add_child(bubble);

		RichTextLabel *body = memnew(RichTextLabel);
		body->set_use_bbcode(true);
		body->set_fit_content(true);
		body->set_scroll_active(false);
		body->set_selection_enabled(true);
		body->set_custom_minimum_size(Size2(0, 20));
		body->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		bubble->add_child(body);
		assistant_labels[p_msg_id] = body;

		VBoxContainer *tools = memnew(VBoxContainer);
		tools->add_theme_constant_override(SNAME("separation"), 4);
		content->add_child(tools);
		tools_boxes[p_msg_id] = tools;
	}
	return box;
}

void LagoonicChatView::_apply_avatar_icon(TextureRect *p_rect) {
	if (p_rect != nullptr && is_inside_tree()) {
		p_rect->set_texture(get_editor_theme_icon(SNAME("Godot")));
	}
}

void LagoonicChatView::_update_send_state() {
	if (send_button == nullptr) {
		return;
	}
	const bool empty = input == nullptr || input->get_text().strip_edges().is_empty();
	send_button->set_disabled(busy || !connected || empty);
}

void LagoonicChatView::_on_input_focus(bool p_focused) {
	if (native_look) {
		_apply_native_styles();
		return;
	}
	if (composer_panel != nullptr) {
		composer_panel->set_theme_type_variation(p_focused ? LagoonicVibeTheme::COMPOSER_FOCUS : LagoonicVibeTheme::COMPOSER);
	}
}

void LagoonicChatView::set_minimal(bool p_minimal) {
	minimal = p_minimal;
	for (Control *c : { (Control *)header_row, (Control *)welcome_sub, suggestions_box, (Control *)attach_button }) {
		if (c != nullptr) {
			c->set_visible(!minimal);
		}
	}
	if (input != nullptr) {
		input->set_placeholder(minimal ? String::utf8("Describe lo que quieres cambiar…")
									   : String::utf8("Describe lo que quieres cambiar… (Enter envía · Shift+Enter, nueva línea)"));
	}
	if (send_button != nullptr) {
		send_button->set_text(minimal ? String::utf8("↑") : TTRC("Enviar"));
		send_button->set_custom_minimum_size(minimal ? Size2(36, 36) * EDSCALE : Size2());
		send_button->add_theme_font_size_override(SceneStringName(font_size), minimal ? (int)(18 * EDSCALE) : -1);
		if (!minimal) {
			send_button->remove_theme_font_size_override(SceneStringName(font_size));
		}
	}
	if (stop_button != nullptr && send_button != nullptr) {
		stop_button->set_visible(!minimal || busy);
		send_button->set_visible(!minimal || !busy);
	}
}

void LagoonicChatView::_set_welcome_visible(bool p_visible) {
	if (welcome_box != nullptr) {
		welcome_box->set_visible(p_visible && !native_look);
	}
}

void LagoonicChatView::set_native_look(bool p_native) {
	native_look = p_native;
	_set_welcome_visible(message_boxes.is_empty() && permission_cards.is_empty());
	if (composer_panel != nullptr) {
		composer_panel->set_theme_type_variation(p_native ? StringName() : StringName(LagoonicVibeTheme::COMPOSER));
	}
	if (input != nullptr) {
		input->set_theme_type_variation(p_native ? StringName() : StringName(LagoonicVibeTheme::COMPOSER_INPUT));
	}
	if (send_button != nullptr) {
		send_button->set_theme_type_variation(p_native ? StringName() : StringName(LagoonicVibeTheme::SEND_BUTTON));
		send_button->set_flat(p_native);
		if (p_native) {
			send_button->set_text(String());
			send_button->set_custom_minimum_size(Size2());
			send_button->remove_theme_font_size_override(SceneStringName(font_size));
		}
	}
	_apply_native_styles();
}

void LagoonicChatView::_apply_native_styles() {
	if (!native_look || !is_inside_tree()) {
		return;
	}
	// Mismo estilo que los LineEdit del editor: el panel hace de campo y el
	// TextEdit de dentro va sin fondo.
	if (composer_panel != nullptr) {
		const bool focused = input != nullptr && input->has_focus();
		Ref<StyleBox> field = get_theme_stylebox(focused ? SNAME("focus") : SNAME("normal"), SNAME("LineEdit"));
		if (focused) {
			// El "focus" del editor es solo el borde: se dibuja encima del normal.
			Ref<StyleBox> base = get_theme_stylebox(SNAME("normal"), SNAME("LineEdit"));
			Ref<StyleBoxFlat> flat = base;
			Ref<StyleBoxFlat> ring = field;
			if (flat.is_valid() && ring.is_valid()) {
				Ref<StyleBoxFlat> merged = flat->duplicate();
				merged->set_border_color(ring->get_border_color());
				merged->set_border_width_all(MAX(1, ring->get_border_width(SIDE_LEFT)));
				field = merged;
			} else {
				field = base;
			}
		}
		composer_panel->add_theme_style_override(SceneStringName(panel), field);
	}
	if (input != nullptr) {
		Ref<StyleBoxEmpty> empty;
		empty.instantiate();
		empty->set_content_margin_individual(0, 2 * EDSCALE, 0, 2 * EDSCALE);
		for (const char *n : { "normal", "focus", "read_only" }) {
			input->add_theme_style_override(n, empty);
		}
		input->add_theme_color_override(SNAME("background_color"), Color(0, 0, 0, 0));
		input->set_custom_minimum_size(Size2());
	}
	if (send_button != nullptr) {
		send_button->set_button_icon(get_editor_theme_icon(SNAME("ArrowUp")));
	}
}

void LagoonicChatView::_on_suggestion_pressed(const String &p_text) {
	if (input == nullptr) {
		return;
	}
	input->set_text(p_text);
	input->set_caret_line(input->get_line_count() - 1);
	input->set_caret_column(input->get_line(input->get_line_count() - 1).length());
	input->grab_focus();
}

void LagoonicChatView::_on_input_gui_input(const Ref<InputEvent> &p_event) {
	// Enter envia; Shift+Enter hace salto de linea.
	Ref<InputEventKey> k = p_event;
	if (k.is_valid() && k->is_pressed() && !k->is_echo() &&
			(k->get_keycode() == Key::ENTER || k->get_keycode() == Key::KP_ENTER) && !k->is_shift_pressed()) {
		input->accept_event();
		_on_send_pressed();
	}
}

void LagoonicChatView::_scroll_to_bottom() {
	if (messages_scroll == nullptr || messages_box == nullptr || messages_box->get_child_count() == 0) {
		return;
	}
	Control *last = Object::cast_to<Control>(messages_box->get_child(messages_box->get_child_count() - 1));
	if (last != nullptr) {
		messages_scroll->call_deferred(SNAME("ensure_control_visible"), last);
	}
}

void LagoonicChatView::_on_send_pressed() {
	if (input == nullptr || busy || !connected) {
		return;
	}
	const String text = input->get_text().strip_edges();
	if (text.is_empty()) {
		return;
	}
	add_user_message(text);
	input->clear();
	clear_file_guard();
	clear_error();
	set_busy(true);
	_fire(SNAME("send_requested"), text);
}

void LagoonicChatView::_on_stop_pressed() {
	set_busy(false);
	_fire(SNAME("abort_requested"));
}

void LagoonicChatView::_on_attach_pressed() {
	// Adjunta la captura actual del viewport como contexto del mensaje.
	// El dock traduce este evento a `files.pickAttachments` o equivalente.
	_fire(SNAME("attachments_dropped"), PackedStringArray());
}

void LagoonicChatView::_on_new_session_pressed() {
	_fire(SNAME("new_session_requested"));
}

void LagoonicChatView::_on_session_selected(int p_index) {
	if (session_option == nullptr || p_index < 0 || p_index >= session_ids.size()) {
		return;
	}
	_fire(SNAME("session_selected"), session_ids[p_index]);
}

void LagoonicChatView::_on_boss_selected(int p_index) {
	if (boss_option == nullptr || p_index < 0) {
		return;
	}
	const String agent_id = boss_option->get_item_text(p_index);
	_fire(SNAME("boss_changed"), agent_id);
	crew_view->set_boss_name(agent_id);
}

void LagoonicChatView::_on_model_selected(int p_index) {
	if (model_option == nullptr) {
		return;
	}
	_fire(SNAME("model_changed"), model_option->get_item_text(p_index));
}

void LagoonicChatView::_on_mode_selected(int p_index) {
	if (mode_option == nullptr) {
		return;
	}
	_fire(SNAME("mode_changed"), mode_option->get_item_id(p_index));
}

void LagoonicChatView::_on_permission_button(const String &p_request_id, int p_reply) {
	close_permission_card(p_request_id);
	_fire(SNAME("permission_replied"), p_request_id, p_reply);
}

void LagoonicChatView::_on_undo_file_button(const String &p_path) {
	_fire(SNAME("undo_file_requested"), p_path);
}

void LagoonicChatView::_on_undo_task_pressed() {
	_fire(SNAME("undo_task_requested"));
}

void LagoonicChatView::_on_revert_button(const String &p_msg_id) {
	_fire(SNAME("revert_requested"), p_msg_id);
}

void LagoonicChatView::_poll() {
	if (!is_visible_in_tree()) {
		return;
	}
	if (busy && busy_label != nullptr) {
		busy_tick = (busy_tick + 1) % 4;
		busy_label->set_text(String::utf8("Lagoonic está trabajando") + String(".").repeat(busy_tick));
	}
	_fire(SNAME("poll_tick"));
}

bool LagoonicChatView::can_drop_data(const Point2 &p_point, const Variant &p_data) const {
	if (p_data.get_type() != Variant::DICTIONARY) {
		return false;
	}
	const Dictionary data = p_data;
	const String type = String(data.get("type", ""));
	return type == "files" || type == "nodes" || type == "resources";
}

void LagoonicChatView::drop_data(const Point2 &p_point, const Variant &p_data) {
	if (input == nullptr) {
		return;
	}
	const Dictionary data = p_data;
	const String type = String(data.get("type", ""));
	if (type == "files" && data.has("files")) {
		const PackedStringArray files = data["files"];
		if (!files.is_empty()) {
			_fire(SNAME("attachments_dropped"), files);
		}
		return;
	}
	if ((type == "nodes" || type == "resources") && data.has("nodes")) {
		const Array nodes = data["nodes"];
		for (int i = 0; i < nodes.size(); i++) {
			input->insert_text_at_caret("@" + String(nodes[i]) + " ");
		}
		input->grab_focus();
	}
}

void LagoonicChatView::set_sessions(const Array &p_sessions) {
	session_ids.clear();
	if (session_option == nullptr) {
		return;
	}
	session_option->clear();
	for (int i = 0; i < p_sessions.size(); i++) {
		const Dictionary d = p_sessions[i];
		const String id = String(d.get("id", ""));
		if (id.is_empty()) {
			continue;
		}
		session_ids.push_back(id);
		session_option->add_item(String(d.get("title", TTRC(String::utf8("Sin título")))), session_ids.size() - 1);
	}
	if (session_option->get_item_count() > 0) {
		session_option->select(0);
	} else {
		session_option->set_text(String::utf8("Conversación nueva"));
	}
}

void LagoonicChatView::set_current_session(const String &p_session_id) {
	const int idx = session_ids.find(p_session_id);
	if (idx >= 0 && session_option != nullptr) {
		session_option->select(idx);
	}
}

String LagoonicChatView::get_current_session() const {
	if (session_option != nullptr && session_option->get_selected() >= 0 && session_option->get_selected() < session_ids.size()) {
		return session_ids[session_option->get_selected()];
	}
	return String();
}

void LagoonicChatView::set_boss_options(const PackedStringArray &p_agent_ids) {
	if (boss_option == nullptr) {
		return;
	}
	const String current = get_boss_agent();
	boss_option->clear();
	PackedStringArray ids = p_agent_ids;
	if (ids.is_empty()) {
		for (int i = 0; i < 6; i++) {
			ids.push_back(LAGOONIC_BOSS_IDS[i]);
		}
	}
	for (int i = 0; i < ids.size(); i++) {
		boss_option->add_item(ids[i], i);
	}
	set_boss_agent(current.is_empty() ? String("opencode") : current);
}

void LagoonicChatView::set_boss_agent(const String &p_agent_id) {
	if (boss_option == nullptr) {
		return;
	}
	for (int i = 0; i < boss_option->get_item_count(); i++) {
		if (String(boss_option->get_item_text(i)) == p_agent_id) {
			boss_option->select(i);
			crew_view->set_boss_name(p_agent_id);
			return;
		}
	}
}

String LagoonicChatView::get_boss_agent() const {
	if (boss_option != nullptr && boss_option->get_selected() >= 0) {
		return boss_option->get_item_text(boss_option->get_selected());
	}
	return "opencode";
}

void LagoonicChatView::set_model_options(const PackedStringArray &p_model_refs) {
	if (model_option == nullptr) {
		return;
	}
	const String current = get_current_model();
	model_option->clear();
	for (int i = 0; i < p_model_refs.size(); i++) {
		model_option->add_item(p_model_refs[i], i);
	}
	if (!current.is_empty()) {
		set_current_model(current);
	} else if (model_option->get_item_count() > 0) {
		model_option->select(0);
	}
	if (model_option->get_item_count() == 0) {
		model_option->set_text(TTRC("Modelo por defecto"));
	}
}

void LagoonicChatView::set_current_model(const String &p_model_ref) {
	if (model_option == nullptr) {
		return;
	}
	for (int i = 0; i < model_option->get_item_count(); i++) {
		if (String(model_option->get_item_text(i)) == p_model_ref) {
			model_option->select(i);
			return;
		}
	}
}

String LagoonicChatView::get_current_model() const {
	if (model_option != nullptr && model_option->get_selected() >= 0) {
		return model_option->get_item_text(model_option->get_selected());
	}
	return String();
}

void LagoonicChatView::set_mode(int p_mode) {
	if (mode_option != nullptr && p_mode >= MODE_AUTO && p_mode <= MODE_PLAN) {
		mode_option->select(p_mode);
	}
}

int LagoonicChatView::get_mode() const {
	if (mode_option != nullptr && mode_option->get_selected() >= 0) {
		return mode_option->get_item_id(mode_option->get_selected());
	}
	return MODE_AUTO;
}

void LagoonicChatView::add_user_message(const String &p_text) {
	VBoxContainer *box = _ensure_message_box("user_" + itos(message_boxes.size()), false);
	HBoxContainer *row = memnew(HBoxContainer);
	box->add_child(row);

	Control *spacer = memnew(Control);
	spacer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	spacer->set_custom_minimum_size(Size2(80, 0) * EDSCALE);
	row->add_child(spacer);

	PanelContainer *bubble = memnew(PanelContainer);
	bubble->set_theme_type_variation(LagoonicVibeTheme::USER_BUBBLE);
	bubble->set_h_size_flags(Control::SIZE_SHRINK_END);
	row->add_child(bubble);

	Label *text = memnew(Label);
	text->set_text(p_text);
	text->set_theme_type_variation(LagoonicVibeTheme::BUBBLE_LABEL);
	text->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	bubble->add_child(text);

	// La burbuja se ajusta al texto hasta un ancho maximo; despues envuelve.
	const Ref<Font> font = get_theme_font(SceneStringName(font), SNAME("Label"));
	const int font_size = get_theme_font_size(SceneStringName(font_size), SNAME("Label"));
	float width = 40.0f * EDSCALE;
	if (font.is_valid()) {
		for (const String &line : p_text.split("\n")) {
			width = MAX(width, font->get_string_size(line, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size).x + 4.0f);
		}
	}
	text->set_custom_minimum_size(Size2(MIN(width, 520.0f * EDSCALE), 0));
	_scroll_to_bottom();
}

void LagoonicChatView::upsert_assistant_message(const String &p_msg_id, const String &p_text) {
	VBoxContainer *box = _ensure_message_box(p_msg_id, true);
	if (assistant_labels.has(p_msg_id)) {
		RichTextLabel *label = assistant_labels[p_msg_id];
		label->set_text(_escape_brackets(p_text));
		if (Control *bubble = Object::cast_to<Control>(label->get_parent())) {
			bubble->set_visible(!p_text.strip_edges().is_empty());
		}
	}
	set_busy(false);
	_scroll_to_bottom();
}

void LagoonicChatView::append_tool_row(const String &p_msg_id, const String &p_tool_id, const String &p_label, int p_state, const String &p_detail) {
	VBoxContainer *box = _ensure_message_box(p_msg_id, true);
	if (!tools_boxes.has(p_msg_id)) {
		return;
	}
	const String key = _tool_key(p_msg_id, p_tool_id);
	if (tool_rows.has(key)) {
		update_tool_state(p_msg_id, p_tool_id, p_state, p_detail);
		return;
	}
	PanelContainer *chip = memnew(PanelContainer);
	chip->set_theme_type_variation(LagoonicVibeTheme::TOOL_CHIP);
	chip->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
	tools_boxes[p_msg_id]->add_child(chip);

	HBoxContainer *row = memnew(HBoxContainer);
	row->add_theme_constant_override(SNAME("separation"), 6);
	chip->add_child(row);

	Label *dot = memnew(Label);
	dot->set_text(String::chr(0x25CF)); // ●
	dot->add_theme_color_override(SNAME("font_color"), _tool_color((ToolState)p_state));
	row->add_child(dot);

	Label *label = memnew(Label);
	label->set_text(p_label.length() > 90 ? p_label.left(88) + String::utf8("…") : p_label);
	label->set_theme_type_variation(LagoonicVibeTheme::MUTED_LABEL);
	label->set_tooltip_text(p_detail.is_empty() ? p_label : p_detail);
	row->add_child(label);

	tool_rows[key] = row;
	tool_dots[key] = dot;
	_scroll_to_bottom();
	(void)box;
}

void LagoonicChatView::update_tool_state(const String &p_msg_id, const String &p_tool_id, int p_state, const String &p_detail) {
	const String key = _tool_key(p_msg_id, p_tool_id);
	if (tool_dots.has(key)) {
		tool_dots[key]->add_theme_color_override(SNAME("font_color"), _tool_color((ToolState)p_state));
	}
	if (tool_rows.has(key) && !p_detail.is_empty()) {
		Label *label = Object::cast_to<Label>(tool_rows[key]->get_child(1));
		if (label != nullptr) {
			label->set_tooltip_text(p_detail);
		}
	}
}

void LagoonicChatView::show_permission_card(const String &p_request_id, const String &p_session_id, const String &p_title, const String &p_detail) {
	(void)p_session_id;
	if (permission_cards.has(p_request_id)) {
		return;
	}
	_set_welcome_visible(false);
	PanelContainer *card = memnew(PanelContainer);
	card->set_theme_type_variation(LagoonicVibeTheme::PERMISSION_CARD);
	messages_box->add_child(card);
	permission_cards[p_request_id] = card;

	VBoxContainer *vb = memnew(VBoxContainer);
	vb->add_theme_constant_override(SNAME("separation"), 4);
	card->add_child(vb);

	Label *title = memnew(Label);
	title->set_text(vformat(TTRC("Permiso: %s"), p_title));
	title->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	vb->add_child(title);

	if (!p_detail.is_empty()) {
		Label *detail = memnew(Label);
		detail->set_text(p_detail);
		detail->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		detail->set_theme_type_variation(LagoonicVibeTheme::MUTED_LABEL);
		vb->add_child(detail);
	}

	HBoxContainer *buttons = memnew(HBoxContainer);
	buttons->add_theme_constant_override(SNAME("separation"), 6);
	buttons->set_alignment(BoxContainer::ALIGNMENT_END);
	vb->add_child(buttons);

	Button *approve = memnew(Button);
	approve->set_text(TTRC("Aprobar"));
	approve->set_theme_type_variation(LagoonicVibeTheme::SEND_BUTTON);
	approve->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChatView::_on_permission_button).bind(p_request_id, REPLY_ONCE));
	buttons->add_child(approve);

	Button *always = memnew(Button);
	always->set_text(TTRC("Siempre"));
	always->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChatView::_on_permission_button).bind(p_request_id, REPLY_ALWAYS));
	buttons->add_child(always);

	Button *reject = memnew(Button);
	reject->set_text(TTRC("Rechazar"));
	reject->set_theme_type_variation(LagoonicVibeTheme::GHOST_BUTTON);
	reject->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChatView::_on_permission_button).bind(p_request_id, REPLY_REJECT));
	buttons->add_child(reject);

	_scroll_to_bottom();
}

void LagoonicChatView::close_permission_card(const String &p_request_id) {
	if (!permission_cards.has(p_request_id)) {
		return;
	}
	PanelContainer *card = permission_cards[p_request_id];
	permission_cards.erase(p_request_id);
	if (card != nullptr) {
		card->queue_free();
	}
}

void LagoonicChatView::set_changes(const PackedStringArray &p_paths, int p_additions, int p_deletions) {
	change_paths = p_paths;
	if (changes_bar == nullptr || changes_label == nullptr || changes_files == nullptr) {
		return;
	}
	for (int i = changes_files->get_child_count() - 1; i >= 0; i--) {
		changes_files->get_child(i)->queue_free();
	}
	changes_bar->set_visible(!p_paths.is_empty());
	if (p_paths.is_empty()) {
		return;
	}
	changes_label->set_text(vformat(TTRC("Cambios: %d +%d/-%d"), p_paths.size(), p_additions, p_deletions));
	const int shown = MIN(p_paths.size(), 8);
	for (int i = 0; i < shown; i++) {
		Button *file_button = memnew(Button);
		file_button->set_text(p_paths[i].get_file());
		file_button->set_tooltip_text(vformat(TTRC("Deshacer cambios en %s"), p_paths[i]));
		file_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChatView::_on_undo_file_button).bind(p_paths[i]));
		changes_files->add_child(file_button);
	}
}

void LagoonicChatView::clear_changes() {
	change_paths.clear();
	if (changes_bar != nullptr) {
		changes_bar->set_visible(false);
	}
}

void LagoonicChatView::notify_file_changed(const String &p_path) {
	if (file_guard_label == nullptr) {
		return;
	}
	// Guarda file_changed del contrato: el fichero cambio en disco fuera
	// del chat; se avisa en vez de escribir a ciegas.
	file_guard_label->set_text(vformat(TTRC(String::utf8("'%s' cambió en disco. Revisa los cambios antes de enviar.")), p_path));
	file_guard_label->set_visible(true);
}

void LagoonicChatView::clear_file_guard() {
	if (file_guard_label != nullptr) {
		file_guard_label->set_visible(false);
	}
}

void LagoonicChatView::show_error(const String &p_text) {
	if (error_label == nullptr) {
		return;
	}
	error_label->set_text(p_text);
	error_label->set_visible(true);
}

void LagoonicChatView::clear_error() {
	if (error_label != nullptr) {
		error_label->set_visible(false);
	}
}

void LagoonicChatView::set_busy(bool p_busy) {
	busy = p_busy;
	_update_send_state();
	if (stop_button != nullptr) {
		stop_button->set_disabled(!busy);
		if (minimal) {
			stop_button->set_visible(busy);
			send_button->set_visible(!busy);
		}
	}
	if (busy_label != nullptr) {
		busy_label->set_visible(busy);
	}
}

void LagoonicChatView::set_connected(bool p_connected) {
	connected = p_connected;
	set_busy(busy);
}

void LagoonicChatView::clear_messages() {
	for (KeyValue<String, VBoxContainer *> kv : message_boxes) {
		if (kv.value != nullptr) {
			kv.value->queue_free();
		}
	}
	message_boxes.clear();
	assistant_labels.clear();
	tools_boxes.clear();
	tool_rows.clear();
	tool_dots.clear();
	for (KeyValue<String, PanelContainer *> kv : permission_cards) {
		if (kv.value != nullptr) {
			kv.value->queue_free();
		}
	}
	permission_cards.clear();
	_set_welcome_visible(true);
	clear_changes();
	clear_file_guard();
	clear_error();
}

void LagoonicChatView::refresh_icons() {
	_apply_native_styles();
	if (stop_button != nullptr) {
		stop_button->set_button_icon(get_editor_theme_icon(SNAME("Stop")));
	}
	if (attach_button != nullptr) {
		attach_button->set_button_icon(get_editor_theme_icon(SNAME("Camera")));
	}
}

void LagoonicChatView::_notification(int p_notification) {
	switch (p_notification) {
		case NOTIFICATION_ENTER_TREE: {
			refresh_icons();
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			refresh_icons();
		} break;
	}
}

void LagoonicChatView::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_event_handler", "handler"), &LagoonicChatView::set_event_handler);
	ClassDB::bind_method(D_METHOD("set_sessions", "sessions"), &LagoonicChatView::set_sessions);
	ClassDB::bind_method(D_METHOD("set_current_session", "session_id"), &LagoonicChatView::set_current_session);
	ClassDB::bind_method(D_METHOD("get_current_session"), &LagoonicChatView::get_current_session);
	ClassDB::bind_method(D_METHOD("set_boss_options", "agent_ids"), &LagoonicChatView::set_boss_options);
	ClassDB::bind_method(D_METHOD("set_boss_agent", "agent_id"), &LagoonicChatView::set_boss_agent);
	ClassDB::bind_method(D_METHOD("get_boss_agent"), &LagoonicChatView::get_boss_agent);
	ClassDB::bind_method(D_METHOD("set_model_options", "model_refs"), &LagoonicChatView::set_model_options);
	ClassDB::bind_method(D_METHOD("set_current_model", "model_ref"), &LagoonicChatView::set_current_model);
	ClassDB::bind_method(D_METHOD("get_current_model"), &LagoonicChatView::get_current_model);
	ClassDB::bind_method(D_METHOD("set_mode", "mode"), &LagoonicChatView::set_mode);
	ClassDB::bind_method(D_METHOD("get_mode"), &LagoonicChatView::get_mode);
	ClassDB::bind_method(D_METHOD("add_user_message", "text"), &LagoonicChatView::add_user_message);
	ClassDB::bind_method(D_METHOD("upsert_assistant_message", "message_id", "text"), &LagoonicChatView::upsert_assistant_message);
	ClassDB::bind_method(D_METHOD("append_tool_row", "message_id", "tool_id", "label", "state", "detail"), &LagoonicChatView::append_tool_row);
	ClassDB::bind_method(D_METHOD("update_tool_state", "message_id", "tool_id", "state", "detail"), &LagoonicChatView::update_tool_state);
	ClassDB::bind_method(D_METHOD("show_permission_card", "request_id", "session_id", "title", "detail"), &LagoonicChatView::show_permission_card);
	ClassDB::bind_method(D_METHOD("close_permission_card", "request_id"), &LagoonicChatView::close_permission_card);
	ClassDB::bind_method(D_METHOD("set_changes", "paths", "additions", "deletions"), &LagoonicChatView::set_changes);
	ClassDB::bind_method(D_METHOD("clear_changes"), &LagoonicChatView::clear_changes);
	ClassDB::bind_method(D_METHOD("notify_file_changed", "path"), &LagoonicChatView::notify_file_changed);
	ClassDB::bind_method(D_METHOD("clear_file_guard"), &LagoonicChatView::clear_file_guard);
	ClassDB::bind_method(D_METHOD("set_busy", "busy"), &LagoonicChatView::set_busy);
	ClassDB::bind_method(D_METHOD("set_connected", "connected"), &LagoonicChatView::set_connected);
	ClassDB::bind_method(D_METHOD("clear_messages"), &LagoonicChatView::clear_messages);
	ClassDB::bind_method(D_METHOD("refresh_icons"), &LagoonicChatView::refresh_icons);
	ClassDB::bind_method(D_METHOD("get_crew_view"), &LagoonicChatView::get_crew_view);

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
	ADD_SIGNAL(MethodInfo("poll_tick"));
}

LagoonicChatView::LagoonicChatView() {
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
	signal_argc[SNAME("poll_tick")] = 0;

	add_theme_constant_override(SNAME("separation"), 10);

	// --- Cabecera: sesion + jefe + modelo + modo ---
	HBoxContainer *header = memnew(HBoxContainer);
	header->add_theme_constant_override(SNAME("separation"), 4);
	add_child(header);
	header_row = header;

	session_option = memnew(OptionButton);
	session_option->set_custom_minimum_size(Size2(120, 0));
	session_option->set_tooltip_text(TTRC(String::utf8("Sesión de chat activa.")));
	session_option->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	session_option->connect(StringName("item_selected"), callable_mp(this, &LagoonicChatView::_on_session_selected));
	session_option->set_text(String::utf8("Conversación nueva"));
	header->add_child(session_option);

	new_session_button = memnew(Button);
	new_session_button->set_text("+");
	new_session_button->set_tooltip_text(TTRC(String::utf8("Nueva sesión de chat.")));
	new_session_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChatView::_on_new_session_pressed));
	header->add_child(new_session_button);

	boss_option = memnew(OptionButton);
	boss_option->set_tooltip_text(TTRC("Jefe del equipo (agente del chat)."));
	for (int i = 0; i < 6; i++) {
		boss_option->add_item(LAGOONIC_BOSS_IDS[i], i);
	}
	boss_option->connect(StringName("item_selected"), callable_mp(this, &LagoonicChatView::_on_boss_selected));
	header->add_child(boss_option);

	model_option = memnew(OptionButton);
	model_option->set_custom_minimum_size(Size2(140, 0));
	model_option->set_tooltip_text(TTRC("Modelo (de GET /provider + Ollama)."));
	model_option->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	model_option->connect(StringName("item_selected"), callable_mp(this, &LagoonicChatView::_on_model_selected));
	model_option->set_text(TTRC("Modelo por defecto"));
	header->add_child(model_option);

	mode_option = memnew(OptionButton);
	mode_option->set_tooltip_text(TTRC("Modo del agente."));
	mode_option->add_item(TTRC("Auto"), MODE_AUTO);
	mode_option->add_item(TTRC("Ask"), MODE_ASK);
	mode_option->add_item(TTRC("Plan"), MODE_PLAN);
	mode_option->connect(StringName("item_selected"), callable_mp(this, &LagoonicChatView::_on_mode_selected));
	header->add_child(mode_option);

	// --- Tarjeta Equipo Crew ---
	crew_card = memnew(PanelContainer);
	crew_card->set_theme_type_variation(LagoonicVibeTheme::CARD);
	crew_card->set_visible(false); // Solo aparece cuando el equipo tiene tareas.
	add_child(crew_card);

	crew_view = memnew(LagoonicCrewView);
	crew_view->set_event_handler(callable_mp(this, &LagoonicChatView::_on_crew_event));
	crew_view->set_hide_when_empty(crew_card);
	crew_card->add_child(crew_view);

	// --- Stream de mensajes ---
	messages_scroll = memnew(ScrollContainer);
	messages_scroll->set_custom_minimum_size(Size2(0, 200));
	messages_scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	messages_scroll->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	add_child(messages_scroll);

	messages_scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);

	messages_box = memnew(VBoxContainer);
	messages_box->add_theme_constant_override(SNAME("separation"), 16);
	messages_box->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	messages_box->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	messages_scroll->add_child(messages_box);

	// --- Pantalla vacia: saludo + sugerencias ---
	welcome_box = memnew(VBoxContainer);
	welcome_box->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	welcome_box->set_alignment(BoxContainer::ALIGNMENT_CENTER);
	welcome_box->add_theme_constant_override(SNAME("separation"), 10);
	messages_box->add_child(welcome_box);

	Label *welcome_title = memnew(Label);
	welcome_title->set_text(String::utf8("¿Qué hacemos hoy con tu juego?"));
	welcome_title->set_theme_type_variation(LagoonicVibeTheme::TITLE_LABEL);
	welcome_title->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	welcome_title->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	welcome_box->add_child(welcome_title);

	welcome_sub = memnew(Label);
	welcome_sub->set_text(String::utf8("Cuéntaselo a Lagoonic con tus palabras. Él escribe el código y tú lo pruebas con Jugar."));
	welcome_sub->set_theme_type_variation(LagoonicVibeTheme::MUTED_LABEL);
	welcome_sub->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	welcome_sub->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	welcome_box->add_child(welcome_sub);

	HFlowContainer *suggestions = memnew(HFlowContainer);
	suggestions->set_alignment(FlowContainer::ALIGNMENT_CENTER);
	suggestions->add_theme_constant_override(SNAME("h_separation"), 8);
	suggestions->add_theme_constant_override(SNAME("v_separation"), 8);
	welcome_box->add_child(suggestions);
	suggestions_box = suggestions;
	const char *suggestion_texts[] = {
		"Añade un enemigo que me persiga",
		"Haz que el personaje salte más alto",
		"Pon un marcador de puntos",
		"Dale un aspecto más bonito",
	};
	for (const char *s : suggestion_texts) {
		const String text = String::utf8(s);
		Button *chip = memnew(Button);
		chip->set_text(text);
		chip->set_theme_type_variation(LagoonicVibeTheme::CHIP_BUTTON);
		chip->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChatView::_on_suggestion_pressed).bind(text));
		suggestions->add_child(chip);
	}

	// --- Barra de diffs ---
	changes_bar = memnew(HBoxContainer);
	changes_bar->add_theme_constant_override(SNAME("separation"), 4);
	changes_bar->set_visible(false);
	add_child(changes_bar);

	changes_label = memnew(Label);
	changes_bar->add_child(changes_label);

	changes_files = memnew(HBoxContainer);
	changes_files->add_theme_constant_override(SNAME("separation"), 4);
	changes_bar->add_child(changes_files);

	undo_task_button = memnew(Button);
	undo_task_button->set_text(TTRC("Deshacer tarea"));
	undo_task_button->set_tooltip_text(TTRC(String::utf8("Deshace la última tarea del agente (todo desde tu último mensaje).")));
	undo_task_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChatView::_on_undo_task_pressed));
	changes_bar->add_child(undo_task_button);

	// --- Guarda file_changed + errores ---
	file_guard_label = memnew(Label);
	file_guard_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	file_guard_label->add_theme_color_override(SNAME("font_color"), Color(0.95, 0.75, 0.25));
	file_guard_label->set_visible(false);
	add_child(file_guard_label);

	error_label = memnew(Label);
	error_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	error_label->add_theme_color_override(SNAME("font_color"), Color(0.9, 0.35, 0.3));
	error_label->set_visible(false);
	add_child(error_label);

	// --- Indicador de trabajo ---
	busy_label = memnew(Label);
	busy_label->set_theme_type_variation(LagoonicVibeTheme::MUTED_LABEL);
	busy_label->set_text(String::utf8("Lagoonic está trabajando"));
	busy_label->set_visible(false);
	add_child(busy_label);

	// --- Entrada: compositor en pildora ---
	PanelContainer *composer = memnew(PanelContainer);
	composer_panel = composer;
	composer->set_theme_type_variation(LagoonicVibeTheme::COMPOSER);
	add_child(composer);

	HBoxContainer *input_row = memnew(HBoxContainer);
	input_row->add_theme_constant_override(SNAME("separation"), 8);
	composer->add_child(input_row);

	attach_button = memnew(Button);
	attach_button->set_theme_type_variation(LagoonicVibeTheme::GHOST_BUTTON);
	attach_button->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	attach_button->set_tooltip_text(TTRC("Adjuntar captura del viewport."));
	attach_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChatView::_on_attach_pressed));
	input_row->add_child(attach_button);

	input = memnew(TextEdit);
	input->set_custom_minimum_size(Size2(0, 0));
	input->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	input->connect(SceneStringName(text_changed), callable_mp(this, &LagoonicChatView::_update_send_state));
	input->connect(SceneStringName(focus_entered), callable_mp(this, &LagoonicChatView::_on_input_focus).bind(true));
	input->connect(SceneStringName(focus_exited), callable_mp(this, &LagoonicChatView::_on_input_focus).bind(false));
	input->set_theme_type_variation(LagoonicVibeTheme::COMPOSER_INPUT);
	input->set_line_wrapping_mode(TextEdit::LINE_WRAPPING_BOUNDARY);
	input->set_fit_content_height_enabled(true);
	input->connect(SceneStringName(gui_input), callable_mp(this, &LagoonicChatView::_on_input_gui_input));
	input->set_placeholder(String::utf8("Describe lo que quieres cambiar… (Enter envía · Shift+Enter, nueva línea)"));
	input->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	input_row->add_child(input);

	send_button = memnew(Button);
	send_button->set_text(TTRC("Enviar"));
	send_button->set_theme_type_variation(LagoonicVibeTheme::SEND_BUTTON);
	send_button->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	send_button->set_tooltip_text(TTRC(String::utf8("Envía el mensaje al jefe del equipo.")));
	send_button->set_disabled(true);
	send_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChatView::_on_send_pressed));
	input_row->add_child(send_button);

	stop_button = memnew(Button);
	stop_button->set_text(TTRC("Detener"));
	stop_button->set_theme_type_variation(LagoonicVibeTheme::GHOST_BUTTON);
	stop_button->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	stop_button->set_tooltip_text(TTRC("Aborta el turno en curso (jefe + worker)."));
	stop_button->set_disabled(true);
	stop_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChatView::_on_stop_pressed));
	input_row->add_child(stop_button);

	poll_timer = memnew(Timer);
	poll_timer->set_wait_time(0.5);
	poll_timer->set_autostart(true);
	poll_timer->connect("timeout", callable_mp(this, &LagoonicChatView::_poll));
	add_child(poll_timer);

	set_connected(false);
}
