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

#include "lagoonic_markdown_view.h"
#include "lagoonic_model_viewer.h"
#include "lagoonic_vibe_theme.h"

#include "core/input/input_event.h"
#include "core/os/os.h"
#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/image.h"
#include "core/io/resource_loader.h"
#include "editor/docks/filesystem_dock.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/inspector/editor_resource_preview.h"
#include "scene/audio/audio_stream_player.h"
#include "servers/audio/audio_stream.h"
#include "scene/resources/gradient_texture.h"
#include "scene/resources/image_texture.h"
#include "core/os/time.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/scroll_bar.h"
#include "core/os/keyboard.h"
#include "editor/themes/editor_scale.h"
#include "servers/display/display_server.h"
#include "scene/gui/flow_container.h"
#include "scene/gui/texture_rect.h"
#include "scene/resources/font.h"
#include "scene/resources/style_box_flat.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/margin_container.h"
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

// Inline: `codigo`, **negrita**, *cursiva*, [texto](url). Todo lo demas es
// literal (los corchetes se escapan para no romper el BBCode).
String LagoonicChatView::_inline_markdown(const String &p_line) {
	String out;
	const int n = p_line.length();
	int i = 0;
	while (i < n) {
		const char32_t c = p_line[i];
		if (c == '`') {
			const int end = p_line.find("`", i + 1);
			if (end > i + 1) {
				out += "[code]" + _escape_brackets(p_line.substr(i + 1, end - i - 1)) + "[/code]";
				i = end + 1;
				continue;
			}
		} else if (c == '*' && i + 1 < n && p_line[i + 1] == '*') {
			const int end = p_line.find("**", i + 2);
			if (end > i + 2) {
				out += "[b]" + _inline_markdown(p_line.substr(i + 2, end - i - 2)) + "[/b]";
				i = end + 2;
				continue;
			}
		} else if ((c == '*' || c == '_') && i + 1 < n && p_line[i + 1] != ' ' && (i == 0 || p_line[i - 1] == ' ' || c == '*')) {
			const int end = p_line.find(String::chr(c), i + 1);
			if (end > i + 1 && p_line[end - 1] != ' ' && (c == '*' || end + 1 >= n || !is_unicode_letter(p_line[end + 1]))) {
				out += "[i]" + _inline_markdown(p_line.substr(i + 1, end - i - 1)) + "[/i]";
				i = end + 1;
				continue;
			}
		} else if (c == '[') {
			const int mid = p_line.find("](", i + 1);
			const int end = mid > 0 ? p_line.find(")", mid + 2) : -1;
			if (mid > i && end > mid + 2 && p_line.substr(i + 1, mid - i - 1).find("[") < 0) {
				const String url = p_line.substr(mid + 2, end - mid - 2).strip_edges();
				out += "[url=" + url.replace("]", "%5D") + "]" + _escape_brackets(p_line.substr(i + 1, mid - i - 1)) + "[/url]";
				i = end + 1;
				continue;
			}
			out += "[lb]";
			i++;
			continue;
		}
		out += String::chr(c);
		i++;
	}
	return out;
}

String LagoonicChatView::markdown_to_bbcode(const String &p_markdown) {
	const PackedStringArray lines = p_markdown.replace("\r\n", "\n").split("\n");
	PackedStringArray out;
	bool in_code = false;
	for (const String &raw : lines) {
		const String trimmed = raw.strip_edges();
		if (trimmed.begins_with("```")) {
			out.push_back(in_code ? String("[/code][/bgcolor]") : String("[bgcolor=#0000004d][code]"));
			in_code = !in_code;
			continue;
		}
		if (in_code) {
			out.push_back(_escape_brackets(raw));
			continue;
		}
		if (trimmed.is_empty()) {
			out.push_back(String());
			continue;
		}
		int hashes = 0;
		while (hashes < trimmed.length() && hashes < 6 && trimmed[hashes] == '#') {
			hashes++;
		}
		if (hashes > 0 && hashes < trimmed.length() && trimmed[hashes] == ' ') {
			const int size = int((hashes == 1 ? 20 : (hashes == 2 ? 18 : 16)) * EDSCALE);
			out.push_back(vformat("[font_size=%d][b]%s[/b][/font_size]", size, _inline_markdown(trimmed.substr(hashes + 1))));
			continue;
		}
		if (trimmed == "---" || trimmed == "***" || trimmed == "___") {
			out.push_back(String::utf8("[color=#ffffff40]────────────[/color]"));
			continue;
		}
		const int indent = raw.length() - raw.strip_edges(true, false).length();
		const String pad = String(" ").repeat(2 + (indent / 2) * 3);
		if (trimmed.begins_with("- ") || trimmed.begins_with("* ") || trimmed.begins_with("+ ")) {
			String item = trimmed.substr(2);
			String mark = String::utf8("•");
			if (item.begins_with("[ ] ")) {
				mark = String::utf8("☐");
				item = item.substr(4);
			} else if (item.begins_with("[x] ") || item.begins_with("[X] ")) {
				mark = String::utf8("☑");
				item = item.substr(4);
			}
			out.push_back(pad + mark + " " + _inline_markdown(item));
			continue;
		}
		const int dot = trimmed.find(". ");
		if (dot > 0 && dot <= 3 && trimmed.substr(0, dot).is_valid_int()) {
			out.push_back(pad + trimmed.substr(0, dot + 1) + " " + _inline_markdown(trimmed.substr(dot + 2)));
			continue;
		}
		if (trimmed.begins_with(">")) {
			out.push_back("[color=#ffffff99][i]" + _inline_markdown(trimmed.substr(1).strip_edges()) + "[/i][/color]");
			continue;
		}
		out.push_back(_inline_markdown(raw));
	}
	if (in_code) {
		out.push_back("[/code][/bgcolor]");
	}
	return String("\n").join(out);
}

String LagoonicChatView::_format_duration(uint64_t p_msec) {
	const uint64_t secs = MAX<uint64_t>(1, p_msec / 1000);
	if (secs >= 3600) {
		return vformat("%d h %d min", secs / 3600, (secs % 3600) / 60);
	}
	if (secs >= 60) {
		return vformat("%d min %d s", secs / 60, secs % 60);
	}
	return vformat("%d s", secs);
}

StringName LagoonicChatView::_tool_icon_name(const String &p_label) const {
	const String l = p_label.to_lower();
	if (l.begins_with("bash") || l.begins_with("shell") || l.begins_with("terminal") || l.begins_with("run")) {
		return SNAME("Terminal");
	}
	if (l.begins_with("edit") || l.begins_with("write") || l.begins_with("patch") || l.begins_with("multiedit") || l.begins_with("apply")) {
		return SNAME("Edit");
	}
	if (l.begins_with("read") || l.begins_with("view")) {
		return SNAME("File");
	}
	if (l.begins_with("grep") || l.begins_with("glob") || l.begins_with("search") || l.begins_with("find") || l.begins_with("list") || l.begins_with("ls")) {
		return SNAME("Search");
	}
	if (l.begins_with("web") || l.begins_with("fetch")) {
		return SNAME("Load");
	}
	if (l.begins_with("godot") || l.begins_with("lagoonic")) {
		return SNAME("Godot");
	}
	if (l.begins_with("task") || l.begins_with("agent")) {
		return SNAME("Node");
	}
	return SNAME("Tools");
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

		// Actividad (como en movil): cabecera "N pasos" desplegable y, debajo,
		// una fila por herramienta con icono y detalle desplegable.
		Button *activity = memnew(Button);
		activity->set_flat(true);
		activity->set_theme_type_variation(LagoonicVibeTheme::GHOST_BUTTON);
		activity->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
		activity->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
		activity->set_tooltip_text(TTRC("Expandir o contraer actividad."));
		activity->set_visible(false);
		activity->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChatView::_on_activity_toggle).bind(p_msg_id));
		content->add_child(activity);
		activity_headers[p_msg_id] = activity;
		activity_counts[p_msg_id] = 0;

		VBoxContainer *tools = memnew(VBoxContainer);
		tools->add_theme_constant_override(SNAME("separation"), 2);
		content->add_child(tools);
		tools_boxes[p_msg_id] = tools;

		PanelContainer *bubble = memnew(PanelContainer);
		bubble->set_theme_type_variation(LagoonicVibeTheme::AGENT_BUBBLE);
		bubble->set_visible(false); // Se muestra con el primer texto.
		content->add_child(bubble);

		LagoonicMarkdownView *body = memnew(LagoonicMarkdownView);
		bubble->add_child(body);
		assistant_bodies[p_msg_id] = body;

		VBoxContainer *media = memnew(VBoxContainer);
		media->add_theme_constant_override(SNAME("separation"), 8);
		media->set_visible(false);
		content->add_child(media);
		media_boxes[p_msg_id] = media;

		HBoxContainer *footer = memnew(HBoxContainer);
		footer->add_theme_constant_override(SNAME("separation"), int(6 * EDSCALE));
		content->add_child(footer);

		Label *worked = memnew(Label);
		worked->set_theme_type_variation(LagoonicVibeTheme::MUTED_LABEL);
		worked->set_visible(false);
		footer->add_child(worked);
		worked_labels[p_msg_id] = worked;

		// Copiar la respuesta: aparece cuando ya hay texto.
		Button *copy = memnew(Button);
		copy->set_flat(true);
		copy->set_focus_mode(Control::FOCUS_NONE);
		copy->set_text(String::utf8("Copiar"));
		copy->set_tooltip_text(String::utf8("Copia esta respuesta al portapapeles."));
		copy->set_theme_type_variation(LagoonicVibeTheme::MUTED_LABEL);
		copy->add_theme_color_override(SceneStringName(font_color), LagoonicVibeTheme::current().text_muted);
		copy->set_visible(false);
		copy->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChatView::_on_copy_pressed).bind(p_msg_id));
		footer->add_child(copy);
		copy_buttons[p_msg_id] = copy;
	}
	return box;
}

void LagoonicChatView::_on_meta_clicked(const Variant &p_meta) {
	const String url = String(p_meta).strip_edges();
	if (url.begins_with("http://") || url.begins_with("https://")) {
		OS::get_singleton()->shell_open(url);
	}
}

void LagoonicChatView::_on_activity_toggle(const String &p_msg_id) {
	if (!tools_boxes.has(p_msg_id)) {
		return;
	}
	VBoxContainer *tools = tools_boxes[p_msg_id];
	tools->set_visible(!tools->is_visible());
	if (activity_headers.has(p_msg_id)) {
		activity_headers[p_msg_id]->set_button_icon(get_editor_theme_icon(tools->is_visible() ? SNAME("GuiTreeArrowDown") : SNAME("GuiTreeArrowRight")));
	}
}

void LagoonicChatView::_refresh_tool_icon(const String &p_key, int p_state) {
	if (!tool_buttons.has(p_key) || !is_inside_tree()) {
		return;
	}
	Button *b = tool_buttons[p_key];
	// Icono segun lo que hace el paso (nunca la terminal: el usuario no ve
	// comandos).
	const String phrase = tool_phrases.has(p_key) ? tool_phrases[p_key].to_lower() : String();
	StringName icon = SNAME("Tools");
	if (phrase.begins_with("leyendo")) {
		icon = SNAME("File");
	} else if (phrase.begins_with("editando") || phrase.begins_with("escribiendo")) {
		icon = SNAME("Edit");
	} else if (phrase.begins_with("buscando")) {
		icon = SNAME("Search");
	} else if (phrase.contains("archivos") || phrase.contains("carpetas")) {
		icon = SNAME("Folder");
	} else if (phrase.contains("rendimiento")) {
		icon = SNAME("Performance");
	} else if (phrase.contains("probando el juego") || phrase.contains("exportando")) {
		icon = SNAME("Play");
	} else if (phrase.contains("web") || phrase.begins_with("descargando")) {
		icon = SNAME("Load");
	} else if (phrase.contains("escena") || phrase.contains("godot") || phrase.contains("editor") || phrase.contains("propiedades") || phrase.contains("imágenes") || phrase.contains("recursos") || phrase.contains("código")) {
		icon = SNAME("Godot");
	}
	b->set_button_icon(get_editor_theme_icon(icon));
	Color c = _tool_color((ToolState)p_state);
	if (p_state == TOOL_DONE) {
		c = LagoonicVibeTheme::current().text_muted;
	}
	for (const char *n : { "icon_normal_color", "icon_hover_color", "icon_pressed_color", "icon_focus_color", "icon_hover_pressed_color" }) {
		b->add_theme_color_override(n, c);
	}
}

void LagoonicChatView::_on_copy_pressed(const String &p_msg_id) {
	if (!assistant_texts.has(p_msg_id) || DisplayServer::get_singleton() == nullptr) {
		return;
	}
	DisplayServer::get_singleton()->clipboard_set(assistant_texts[p_msg_id].strip_edges());
	if (copy_buttons.has(p_msg_id)) {
		copy_buttons[p_msg_id]->set_text(String::utf8("Copiado"));
	}
}

void LagoonicChatView::_set_worked(const String &p_msg_id, uint64_t p_msec) {
	if (!worked_labels.has(p_msg_id) || p_msec == 0) {
		return;
	}
	Label *l = worked_labels[p_msg_id];
	worked_ms[p_msg_id] = p_msec;
	l->set_text(vformat(String::utf8("Trabajó durante %s"), _format_duration(p_msec)));
	l->set_visible(true);
}

void LagoonicChatView::_finish_turn(uint64_t p_worked_ms) {
	if (turn_start_msec != 0 && !turn_message_id.is_empty()) {
		const uint64_t ms = p_worked_ms > 0 ? p_worked_ms : OS::get_singleton()->get_ticks_msec() - turn_start_msec;
		_set_worked(turn_message_id, ms);
		// Al terminar, la actividad se contrae como en movil.
		if (tools_boxes.has(turn_message_id) && tools_boxes[turn_message_id]->is_visible()) {
			_on_activity_toggle(turn_message_id);
		}
		_attach_outputs(turn_message_id, turn_message_ids);
	}
	turn_message_ids.clear();
	turn_start_msec = 0;
	turn_message_id = String();
	last_activity_label = String();
}

void LagoonicChatView::notify_turn_finished() {
	_finish_turn();
	set_busy(false);
	_notify_listeners(SNAME("turn_finished"));
}

void LagoonicChatView::_notify_listeners(const StringName &p_what) {
	// Los paneles cerrados dejan su aviso colgando: fuera.
	for (int i = listeners.size() - 1; i >= 0; i--) {
		if (!listeners[i].is_valid()) {
			listeners.remove_at(i);
		}
	}
	const Vector<Callable> current = listeners; // Un aviso puede abrir o cerrar paneles.
	for (const Callable &c : current) {
		if (c.is_valid()) {
			c.call(p_what);
		}
	}
}

void LagoonicChatView::set_plan_card_suppressed(bool p_suppressed) {
	plan_card_suppressed = p_suppressed;
	_rebuild_plan();
}

void LagoonicChatView::insert_in_composer(const String &p_text) {
	if (input == nullptr) {
		return;
	}
	input->insert_text_at_caret(p_text);
	input->grab_focus();
	_update_send_state();
}

Dictionary LagoonicChatView::get_context_info() const {
	Dictionary d;
	String title = String::utf8("Chat nuevo");
	int64_t updated = 0;
	for (int i = 0; i < sessions_data.size(); i++) {
		const Dictionary s = sessions_data[i];
		if (String(s.get("id", "")) == current_session_id) {
			title = s.get("title", title);
			updated = s.get("updated_at", 0);
		}
	}
	d["title"] = title;
	d["updated_at"] = updated;
	d["model"] = (model_option != nullptr && model_option->get_selected() >= 0) ? model_option->get_item_text(model_option->get_selected()) : String();
	d["mode"] = (mode_option != nullptr && mode_option->get_selected() >= 0) ? mode_option->get_item_text(mode_option->get_selected()) : String();
	d["effort"] = (effort_option != nullptr && effort_option->is_visible() && effort_option->get_selected() >= 0) ? effort_option->get_item_text(effort_option->get_selected()) : String::utf8("Normal");
	d["user_messages"] = user_message_count;
	d["answers"] = assistant_bodies.size();
	int steps = 0;
	for (const KeyValue<String, int> &kv : activity_counts) {
		steps += kv.value;
	}
	d["steps"] = steps;
	uint64_t total = 0;
	for (const KeyValue<String, uint64_t> &kv : worked_ms) {
		total += kv.value;
	}
	if (busy && turn_start_msec != 0) {
		total += OS::get_singleton()->get_ticks_msec() - turn_start_msec;
	}
	d["worked"] = total > 0 ? _format_duration(total) : String::utf8("—");
	d["busy"] = busy;
	return d;
}

void LagoonicChatView::add_history_message(const Dictionary &p_message) {
	const String role = p_message.get("role", "assistant");
	const String text = p_message.get("text", "");
	if (role == "user") {
		if (!text.strip_edges().is_empty()) {
			add_user_message(text);
		}
		return;
	}
	const String id = p_message.get("id", "");
	if (id.is_empty()) {
		return;
	}
	const bool was_busy = busy;
	upsert_assistant_message(id, text);
	const Array tools = p_message.get("tools", Array());
	for (int i = 0; i < tools.size(); i++) {
		const Dictionary tool = tools[i];
		const String state = tool.get("state", "done");
		append_tool_row(id, String(tool.get("id", itos(i))), String(tool.get("label", "")), state == "error" ? TOOL_ERROR : TOOL_DONE, String(tool.get("detail", "")));
	}
	if (p_message.has("timeline")) {
		set_assistant_timeline(id, p_message.get("timeline", Array()));
	}
	if (tools_boxes.has(id) && tools_boxes[id]->is_visible() && tools_boxes[id]->get_child_count() > 0) {
		_on_activity_toggle(id);
	}
	_set_worked(id, uint64_t(int64_t(p_message.get("worked_ms", 0))));
	_attach_outputs(id, PackedStringArray({ id }));
	last_activity_label = String();
	busy = was_busy;
}

LagoonicChatView::MediaKind LagoonicChatView::_media_kind(const String &p_path) {
	const String ext = p_path.get_extension().to_lower();
	if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "webp" || ext == "bmp" || ext == "svg" || ext == "tga") {
		return MEDIA_IMAGE;
	}
	if (ext == "wav" || ext == "ogg" || ext == "mp3") {
		return MEDIA_AUDIO;
	}
	if (ext == "glb" || ext == "gltf" || ext == "fbx" || ext == "obj" || ext == "blend") {
		return MEDIA_MODEL;
	}
	if (ext == "mp4" || ext == "webm" || ext == "mkv" || ext == "mov" || ext == "ogv") {
		return MEDIA_VIDEO;
	}
	if (ext == "zip" || ext == "apk" || ext == "aab" || ext == "pck" || ext == "exe" || ext == "dmg" || ext == "pdf" || ext == "ttf" || ext == "otf" || ext == "csv" || ext == "html" || ext == "tscn") {
		return MEDIA_FILE;
	}
	return MEDIA_NONE;
}

static String _lagoonic_find_by_name(EditorFileSystemDirectory *p_dir, const String &p_name, int p_depth) {
	if (p_dir == nullptr || p_depth > 12) {
		return String();
	}
	for (int i = 0; i < p_dir->get_file_count(); i++) {
		if (p_dir->get_file(i) == p_name) {
			return p_dir->get_file_path(i);
		}
	}
	for (int i = 0; i < p_dir->get_subdir_count(); i++) {
		const String found = _lagoonic_find_by_name(p_dir->get_subdir(i), p_name, p_depth + 1);
		if (!found.is_empty()) {
			return found;
		}
	}
	return String();
}

String LagoonicChatView::_resolve_media(const String &p_token) const {
	String t = p_token.strip_edges();
	// Ni rutas fuera del proyecto ni carpetas internas (.lagoonic, .godot).
	if (t.is_empty() || t.contains("..") || t.contains("/.") || t.begins_with(".") || _media_kind(t) == MEDIA_NONE) {
		return String();
	}
	const String root = ProjectSettings::get_singleton()->globalize_path("res://");
	if (t.begins_with(root)) {
		t = "res://" + t.substr(root.length());
	} else if (t.begins_with("/")) {
		return String(); // Fuera del proyecto.
	} else if (!t.begins_with("res://")) {
		t = "res://" + t.trim_prefix("./");
	}
	if (FileAccess::exists(t)) {
		return t;
	}
	// El agente a veces nombra solo el fichero ("aron.png"): se busca en el
	// proyecto por nombre.
	EditorFileSystem *efs = EditorFileSystem::get_singleton();
	return efs != nullptr ? _lagoonic_find_by_name(efs->get_filesystem(), t.get_file(), 0) : String();
}

void LagoonicChatView::_attach_outputs(const String &p_msg_id, const PackedStringArray &p_source_ids) {
	if (!media_boxes.has(p_msg_id)) {
		return;
	}
	// Rutas nombradas en la respuesta y en los pasos (leer/escribir) del turno.
	String text;
	for (const String &id : p_source_ids) {
		if (assistant_texts.has(id)) {
			text += assistant_texts[id] + "\n";
		}
		for (const KeyValue<String, String> &kv : tool_raw_labels) {
			if (kv.key.begins_with(id + "|")) {
				text += kv.value + "\n";
				if (tool_raw_details.has(kv.key)) {
					text += tool_raw_details[kv.key] + "\n";
				}
			}
		}
	}
	String cleaned = text;
	for (const char *sep : { "`", "\"", "'", "(", ")", "[", "]", "<", ">", ",", ";", "*", "\t", "\n", "\xc2\xb7" }) {
		cleaned = cleaned.replace(String::utf8(sep), " ");
	}
	PackedStringArray found;
	for (String token : cleaned.split(" ", false)) {
		token = token.strip_edges().trim_suffix(".").trim_suffix(":");
		if (token.find(".") < 0) {
			continue;
		}
		const String path = _resolve_media(token);
		if (!path.is_empty() && !found.has(path)) {
			found.push_back(path);
		}
		if (found.size() >= 8) {
			break;
		}
	}
	VBoxContainer *box = media_boxes[p_msg_id];
	for (int i = box->get_child_count() - 1; i >= 0; i--) {
		box->get_child(i)->queue_free();
	}
	for (const String &path : found) {
		_add_media_card(box, path);
	}
	box->set_visible(!found.is_empty());
	_scroll_to_bottom();
}

void LagoonicChatView::_add_media_card(VBoxContainer *p_box, const String &p_res_path) {
	const MediaKind kind = _media_kind(p_res_path);
	PanelContainer *card = memnew(PanelContainer);
	card->set_theme_type_variation(LagoonicVibeTheme::CARD);
	p_box->add_child(card);
	VBoxContainer *vb = memnew(VBoxContainer);
	vb->add_theme_constant_override(SNAME("separation"), 6);
	card->add_child(vb);

	// Vista previa: la imagen tal cual; para modelos/escenas, la miniatura
	// del editor.
	if (LagoonicModelViewer::can_show(p_res_path)) {
		// Modelo 3D en vivo: gira, se arrastra y anima.
		LagoonicModelViewer *viewer = memnew(LagoonicModelViewer);
		viewer->set_model_path(p_res_path);
		vb->add_child(viewer);
	} else if (kind == MEDIA_IMAGE || kind == MEDIA_MODEL || p_res_path.get_extension() == "tscn") {
		TextureRect *preview = memnew(TextureRect);
		preview->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
		preview->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
		preview->set_custom_minimum_size(Size2(0, 260) * EDSCALE);
		preview->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		vb->add_child(preview);
		bool loaded = false;
		if (kind == MEDIA_IMAGE && p_res_path.get_extension().to_lower() != "svg") {
			Ref<Image> img;
			img.instantiate();
			if (img->load(ProjectSettings::get_singleton()->globalize_path(p_res_path)) == OK && !img->is_empty()) {
				const int max_side = int(720 * EDSCALE);
				if (img->get_width() > max_side || img->get_height() > max_side) {
					const float k = float(max_side) / MAX(img->get_width(), img->get_height());
					img->resize(MAX(1, int(img->get_width() * k)), MAX(1, int(img->get_height() * k)), Image::INTERPOLATE_BILINEAR);
				}
				preview->set_texture(ImageTexture::create_from_image(img));
				preview->set_custom_minimum_size(Size2(0, MIN(320.0f * EDSCALE, float(img->get_height()))));
				loaded = true;
			}
		}
		if (!loaded && EditorResourcePreview::get_singleton() != nullptr) {
			preview->set_custom_minimum_size(Size2(0, 160) * EDSCALE);
			EditorResourcePreview::get_singleton()->queue_resource_preview(p_res_path,
					callable_mp(this, &LagoonicChatView::_on_media_preview).bind(preview->get_instance_id()));
		}
	}

	HBoxContainer *footer = memnew(HBoxContainer);
	footer->add_theme_constant_override(SNAME("separation"), 6);
	vb->add_child(footer);

	TextureRect *icon = memnew(TextureRect);
	icon->set_stretch_mode(TextureRect::STRETCH_KEEP_CENTERED);
	static const char *icons[] = { "File", "Image", "AudioStreamPlayer", "Mesh", "VideoStreamPlayer", "File" };
	icon->set_texture(get_editor_theme_icon(StringName(icons[kind])));
	footer->add_child(icon);

	VBoxContainer *names = memnew(VBoxContainer);
	names->add_theme_constant_override(SNAME("separation"), 0);
	names->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	footer->add_child(names);
	Label *name = memnew(Label);
	name->set_text(p_res_path.get_file());
	name->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	names->add_child(name);
	Label *where = memnew(Label);
	String size_text;
	Ref<FileAccess> f = FileAccess::open(p_res_path, FileAccess::READ);
	if (f.is_valid()) {
		size_text = String::utf8(" · ") + String::humanize_size(f->get_length());
	}
	where->set_text(p_res_path.get_base_dir() + size_text);
	where->set_theme_type_variation(LagoonicVibeTheme::MUTED_LABEL);
	where->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	names->add_child(where);

	if (kind == MEDIA_AUDIO) {
		Button *play = memnew(Button);
		play->set_flat(true);
		play->set_theme_type_variation(LagoonicVibeTheme::GHOST_BUTTON);
		play->set_button_icon(get_editor_theme_icon(SNAME("Play")));
		play->set_tooltip_text(TTRC("Reproducir"));
		play->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
		play->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChatView::_on_media_play).bind(p_res_path, play->get_instance_id()));
		footer->add_child(play);
	}
	Button *show = memnew(Button);
	show->set_text(TTRC("Mostrar"));
	show->set_theme_type_variation(LagoonicVibeTheme::GHOST_BUTTON);
	show->set_tooltip_text(TTRC("Mostrarlo en el panel Sistema de archivos."));
	show->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	show->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChatView::_on_media_show).bind(p_res_path));
	footer->add_child(show);
	Button *open = memnew(Button);
	open->set_text(TTRC("Abrir"));
	open->set_theme_type_variation(LagoonicVibeTheme::GHOST_BUTTON);
	open->set_tooltip_text(TTRC("Abrirlo con la app del sistema."));
	open->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	open->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChatView::_on_media_open).bind(p_res_path));
	footer->add_child(open);
}

void LagoonicChatView::_on_media_preview(const String &p_path, const Ref<Texture2D> &p_preview, const Ref<Texture2D> &p_small, ObjectID p_rect) {
	TextureRect *rect = ObjectDB::get_instance<TextureRect>(p_rect);
	if (rect == nullptr) {
		return;
	}
	if (p_preview.is_valid()) {
		rect->set_texture(p_preview);
	} else {
		rect->hide();
	}
}

void LagoonicChatView::_on_media_open(const String &p_res_path) {
	OS::get_singleton()->shell_open(ProjectSettings::get_singleton()->globalize_path(p_res_path));
}

void LagoonicChatView::_on_media_show(const String &p_res_path) {
	if (FileSystemDock::get_singleton() != nullptr) {
		FileSystemDock::get_singleton()->navigate_to_path(p_res_path);
	}
}

void LagoonicChatView::_on_media_play(const String &p_res_path, ObjectID p_button) {
	if (media_player == nullptr) {
		media_player = memnew(AudioStreamPlayer);
		add_child(media_player);
	}
	Button *prev = ObjectDB::get_instance<Button>(media_play_button);
	if (prev != nullptr) {
		prev->set_button_icon(get_editor_theme_icon(SNAME("Play")));
	}
	const bool same = media_play_button == p_button && media_player->is_playing();
	media_player->stop();
	media_play_button = ObjectID();
	if (same) {
		return;
	}
	Ref<AudioStream> stream = ResourceLoader::load(p_res_path);
	if (stream.is_null()) {
		show_error(vformat(String::utf8("No se pudo reproducir %s."), p_res_path.get_file()));
		return;
	}
	media_player->set_stream(stream);
	media_player->play();
	media_play_button = p_button;
	if (Button *b = ObjectDB::get_instance<Button>(p_button)) {
		b->set_button_icon(get_editor_theme_icon(SNAME("Stop")));
	}
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
	// Solo sigue el final si el usuario no se ha desplazado hacia arriba
	// (entonces aparece "Lo ultimo"). El salto real ocurre en
	// _on_scroll_changed cuando el contenido crece.
	if (messages_scroll == nullptr || !stick_to_bottom) {
		_update_latest_button();
		return;
	}
	ScrollBar *bar = messages_scroll->get_v_scroll_bar();
	bar->call_deferred(SNAME("set_value"), bar->get_max());
}

void LagoonicChatView::_draw_fade() {
	if (fade_color.a <= 0.0f || messages_scroll == nullptr) {
		return;
	}
	// Solo con contenido oculto arriba; crece con el desplazamiento. Deja
	// libre la barra de desplazamiento.
	const float scrolled = float(messages_scroll->get_v_scroll_bar()->get_value());
	const float fade = MIN(56.0f * EDSCALE, scrolled);
	if (fade <= 1.0f) {
		return;
	}
	ScrollBar *bar = messages_scroll->get_v_scroll_bar();
	const float bar_w = bar->is_visible() ? bar->get_size().x : 0.0f;
	fade_overlay->draw_texture_rect(fade_texture, Rect2(0, 0, fade_overlay->get_size().x - bar_w, fade), false, fade_color);
}

void LagoonicChatView::set_fade_color(const Color &p_color) {
	fade_color = p_color;
	if (fade_overlay != nullptr) {
		fade_overlay->queue_redraw();
	}
}

void LagoonicChatView::_on_scroll_changed(double p_value) {
	if (fade_overlay != nullptr) {
		fade_overlay->queue_redraw();
	}
	ScrollBar *bar = messages_scroll->get_v_scroll_bar();
	stick_to_bottom = p_value + bar->get_page() >= bar->get_max() - 24 * EDSCALE;
	_update_latest_button();
}

void LagoonicChatView::_update_latest_button() {
	if (latest_button == nullptr || messages_scroll == nullptr) {
		return;
	}
	ScrollBar *bar = messages_scroll->get_v_scroll_bar();
	const bool can_scroll = bar->get_max() > bar->get_page() + 1;
	latest_button->set_visible(can_scroll && !stick_to_bottom);
}

void LagoonicChatView::_on_latest_pressed() {
	stick_to_bottom = true;
	_scroll_to_bottom();
	_update_latest_button();
}

void LagoonicChatView::_on_send_pressed() {
	if (input == nullptr || busy || !connected) {
		return;
	}
	const String text = input->get_text().strip_edges();
	if (text.is_empty()) {
		return;
	}
	stick_to_bottom = true;
	add_user_message(text);
	input->clear();
	clear_file_guard();
	clear_error();
	set_busy(true);
	_fire(SNAME("send_requested"), text);
}

void LagoonicChatView::_on_stop_pressed() {
	_finish_turn();
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
	preferred_model = get_current_model();
	_rebuild_effort_options();
	_fire(SNAME("model_changed"), preferred_model);
}

void LagoonicChatView::_on_effort_selected(int p_index) {
	preferred_effort = get_effort();
	_fire(SNAME("effort_changed"), preferred_effort);
}

String LagoonicChatView::_effort_label(const String &p_variant) {
	static const char *names[][2] = {
		{ "none", "Sin pensar" },
		{ "minimal", "Esfuerzo mínimo" },
		{ "low", "Esfuerzo bajo" },
		{ "medium", "Esfuerzo medio" },
		{ "high", "Esfuerzo alto" },
		{ "xhigh", "Esfuerzo muy alto" },
		{ "max", "Esfuerzo máximo" },
	};
	for (const auto &n : names) {
		if (p_variant == n[0]) {
			return String::utf8(n[1]);
		}
	}
	return String::utf8("Esfuerzo: ") + p_variant.capitalize();
}

void LagoonicChatView::_rebuild_effort_options() {
	if (effort_option == nullptr) {
		return;
	}
	effort_option->clear();
	const String ref = get_current_model();
	PackedStringArray variants;
	if (model_variants.has(ref)) {
		variants = model_variants[ref];
	}
	effort_option->add_item(String::utf8("Esfuerzo normal"));
	effort_option->set_item_metadata(0, String());
	for (int i = 0; i < variants.size(); i++) {
		effort_option->add_item(_effort_label(variants[i]));
		effort_option->set_item_metadata(effort_option->get_item_count() - 1, variants[i]);
	}
	effort_option->select(0);
	for (int i = 0; i < effort_option->get_item_count(); i++) {
		if (String(effort_option->get_item_metadata(i)) == preferred_effort) {
			effort_option->select(i);
			break;
		}
	}
	effort_option->set_visible(!variants.is_empty());
}

void LagoonicChatView::set_models(const Array &p_models, const String &p_default_ref) {
	if (model_option == nullptr) {
		return;
	}
	model_variants.clear();
	model_option->clear();
	String default_name;
	for (int i = 0; i < p_models.size(); i++) {
		const Dictionary m = p_models[i];
		if (String(m.get("ref", "")) == p_default_ref) {
			default_name = m.get("name", "");
		}
	}
	model_option->add_item(default_name.is_empty() ? String(TTRC("Modelo recomendado")) : vformat(String::utf8("Recomendado · %s"), default_name));
	model_option->set_item_metadata(0, String());
	String last_provider;
	for (int i = 0; i < p_models.size(); i++) {
		const Dictionary m = p_models[i];
		const String ref = m.get("ref", "");
		if (ref.is_empty()) {
			continue;
		}
		const String provider = m.get("provider_name", m.get("provider", ""));
		if (provider != last_provider) {
			model_option->add_separator(provider);
			last_provider = provider;
		}
		model_option->add_item(String(m.get("name", ref)));
		model_option->set_item_metadata(model_option->get_item_count() - 1, ref);
		model_option->set_item_tooltip(model_option->get_item_count() - 1, ref);
		model_variants[ref] = PackedStringArray(m.get("variants", PackedStringArray()));
	}
	set_current_model(preferred_model);
	_rebuild_effort_options();
}

void LagoonicChatView::set_effort(const String &p_variant) {
	preferred_effort = p_variant;
	_rebuild_effort_options();
}

String LagoonicChatView::get_effort() const {
	if (effort_option != nullptr && effort_option->is_visible() && effort_option->get_selected() >= 0) {
		return effort_option->get_item_metadata(effort_option->get_selected());
	}
	return String();
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
		// Como la cabecera en vivo de movil: "Pensando · <paso> · 12 s".
		String text = String::utf8("Pensando");
		if (!last_activity_label.is_empty()) {
			text += String::utf8(" · ") + (last_activity_label.length() > 60 ? last_activity_label.left(58) + String::utf8("…") : last_activity_label);
		}
		if (turn_start_msec != 0) {
			text += String::utf8(" · ") + _format_duration(OS::get_singleton()->get_ticks_msec() - turn_start_msec);
		}
		busy_label->set_text(text + String(".").repeat(busy_tick));
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
	sessions_data.clear();
	if (session_option != nullptr) {
		session_option->clear();
	}
	for (int i = 0; i < p_sessions.size(); i++) {
		const Dictionary d = p_sessions[i];
		const String id = String(d.get("id", ""));
		if (id.is_empty()) {
			continue;
		}
		session_ids.push_back(id);
		sessions_data.push_back(d);
		if (session_option != nullptr) {
			session_option->add_item(String(d.get("title", String::utf8("Sin título"))), session_ids.size() - 1);
		}
	}
	set_current_session(current_session_id);
	_rebuild_chats_list();
}

void LagoonicChatView::set_current_session(const String &p_session_id) {
	const bool changed = current_session_id != p_session_id;
	current_session_id = p_session_id;
	if (changed) {
		callable_mp(this, &LagoonicChatView::_notify_listeners).call_deferred(StringName("session"));
	}
	const int idx = session_ids.find(p_session_id);
	if (session_option != nullptr) {
		if (idx >= 0) {
			session_option->select(idx);
		} else {
			session_option->select(-1);
			session_option->set_text(String::utf8("Conversación nueva"));
		}
	}
	_update_session_title();
	_rebuild_chats_list();
}

String LagoonicChatView::get_current_session() const {
	return current_session_id;
}

void LagoonicChatView::_update_session_title() {
	if (session_title_label == nullptr) {
		return;
	}
	String title = String::utf8("Chat nuevo");
	for (int i = 0; i < sessions_data.size(); i++) {
		const Dictionary d = sessions_data[i];
		if (String(d.get("id", "")) == current_session_id) {
			title = d.get("title", title);
			break;
		}
	}
	session_title_label->set_text(title);
	session_title_label->set_tooltip_text(title);
}

void LagoonicChatView::_on_chats_pressed() {
	if (chats_panel == nullptr) {
		return;
	}
	if (chats_panel->is_visible()) {
		_close_chats();
		return;
	}
	pending_delete_id = String();
	chats_panel->set_visible(true);
	_rebuild_chats_list();
	_fire(SNAME("sessions_requested")); // El host responde con set_sessions().
}

void LagoonicChatView::_rebuild_chats_list() {
	if (chats_list == nullptr || chats_panel == nullptr || !chats_panel->is_visible()) {
		return;
	}
	rename_edit = nullptr;
	for (int i = chats_list->get_child_count() - 1; i >= 0; i--) {
		Node *c = chats_list->get_child(i);
		chats_list->remove_child(c);
		c->queue_free();
	}
	if (chats_busy_note != nullptr) {
		chats_busy_note->set_visible(busy);
	}
	if (sessions_data.is_empty()) {
		Label *empty = memnew(Label);
		empty->set_text(String::utf8("Aún no hay chats en este proyecto."));
		empty->set_theme_type_variation(LagoonicVibeTheme::MUTED_LABEL);
		chats_list->add_child(empty);
		return;
	}
	for (int i = 0; i < sessions_data.size(); i++) {
		const Dictionary d = sessions_data[i];
		const String id = d.get("id", "");
		const bool current = id == current_session_id;

		const String title = d.get("title", String::utf8("Sin título"));
		String subtitle = current ? String::utf8("Chat actual") : String::utf8("Conversación guardada");
		const int64_t updated = d.get("updated_at", 0);
		if (updated > 0) {
			const Dictionary dt = Time::get_singleton()->get_datetime_dict_from_unix_time(updated / 1000 + Time::get_singleton()->get_time_zone_from_system().get("bias", 0).operator int64_t() * 60);
			subtitle += vformat(String::utf8(" · %02d/%02d %02d:%02d"), int(dt["day"]), int(dt["month"]), int(dt["hour"]), int(dt["minute"]));
		}

		HBoxContainer *row = memnew(HBoxContainer);
		row->add_theme_constant_override(SNAME("separation"), 6);
		chats_list->add_child(row);

		if (pending_delete_id == id) {
			// Confirmacion en linea, sin ventana aparte.
			PanelContainer *confirm = memnew(PanelContainer);
			confirm->set_theme_type_variation(LagoonicVibeTheme::PERMISSION_CARD);
			confirm->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			row->add_child(confirm);
			VBoxContainer *cv = memnew(VBoxContainer);
			cv->add_theme_constant_override(SNAME("separation"), 6);
			confirm->add_child(cv);
			Label *q = memnew(Label);
			q->set_text(vformat(String::utf8("¿Borrar «%s»? Se borrarán todos sus mensajes y no se puede deshacer."), title));
			q->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
			q->set_custom_minimum_size(Size2(1, 0));
			cv->add_child(q);
			HBoxContainer *cb = memnew(HBoxContainer);
			cb->set_alignment(BoxContainer::ALIGNMENT_END);
			cb->add_theme_constant_override(SNAME("separation"), 6);
			cv->add_child(cb);
			Button *cancel = memnew(Button);
			cancel->set_text(TTRC("Cancelar"));
			cancel->set_theme_type_variation(LagoonicVibeTheme::GHOST_BUTTON);
			cancel->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChatView::_on_delete_cancelled));
			cb->add_child(cancel);
			Button *confirm_button = memnew(Button);
			confirm_button->set_text(TTRC("Borrar chat"));
			confirm_button->set_theme_type_variation(LagoonicVibeTheme::SEND_BUTTON);
			confirm_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChatView::_on_delete_confirmed));
			cb->add_child(confirm_button);
			continue;
		}

		if (pending_rename_id == id) {
			rename_edit = memnew(LineEdit);
			rename_edit->set_text(title);
			rename_edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			rename_edit->set_placeholder(TTRC("Nombre del chat"));
			rename_edit->connect(SNAME("text_submitted"), callable_mp(this, &LagoonicChatView::_on_rename_submitted));
			row->add_child(rename_edit);
			Button *save = memnew(Button);
			save->set_text(TTRC("Guardar"));
			save->set_theme_type_variation(LagoonicVibeTheme::SEND_BUTTON);
			save->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChatView::_on_rename_submitted).bind(String()));
			row->add_child(save);
			Button *cancel = memnew(Button);
			cancel->set_text(TTRC("Cancelar"));
			cancel->set_theme_type_variation(LagoonicVibeTheme::GHOST_BUTTON);
			cancel->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChatView::_on_delete_cancelled));
			row->add_child(cancel);
			callable_mp((Control *)rename_edit, &Control::grab_focus).call_deferred(false);
			callable_mp(rename_edit, &LineEdit::select_all).call_deferred();
			continue;
		}

		Button *open = memnew(Button);
		open->set_theme_type_variation(LagoonicVibeTheme::CHIP_BUTTON);
		open->set_toggle_mode(true);
		open->set_pressed_no_signal(current);
		open->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
		open->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
		open->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		open->set_focus_mode(Control::FOCUS_NONE);
		open->set_button_icon(get_editor_theme_icon(SNAME("History")));
		open->set_text(title + "\n" + subtitle);
		open->set_tooltip_text(title);
		open->set_disabled(busy && !current);
		open->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChatView::_on_chat_row_pressed).bind(id));
		row->add_child(open);

		Button *ren = memnew(Button);
		ren->set_flat(true);
		ren->set_theme_type_variation(LagoonicVibeTheme::GHOST_BUTTON);
		ren->set_focus_mode(Control::FOCUS_NONE);
		ren->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
		ren->set_button_icon(get_editor_theme_icon(SNAME("Edit")));
		ren->set_tooltip_text(TTRC("Renombrar chat"));
		ren->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChatView::_on_chat_rename_pressed).bind(id));
		row->add_child(ren);

		Button *del = memnew(Button);
		del->set_flat(true);
		del->set_theme_type_variation(LagoonicVibeTheme::GHOST_BUTTON);
		del->set_focus_mode(Control::FOCUS_NONE);
		del->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
		del->set_button_icon(get_editor_theme_icon(SNAME("Remove")));
		del->set_tooltip_text(String::utf8("Borrar chat"));
		del->set_disabled(busy && current);
		del->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChatView::_on_chat_delete_pressed).bind(id));
		row->add_child(del);
	}
}

void LagoonicChatView::_on_chat_row_pressed(const String &p_session_id) {
	if (p_session_id == current_session_id) {
		_close_chats();
		return;
	}
	if (busy) {
		return; // La nota del panel lo explica.
	}
	_close_chats();
	_fire(SNAME("session_selected"), p_session_id);
}

void LagoonicChatView::_on_chat_delete_pressed(const String &p_session_id) {
	pending_rename_id = String();
	pending_delete_id = p_session_id;
	_rebuild_chats_list();
}

void LagoonicChatView::_on_chat_rename_pressed(const String &p_session_id) {
	pending_delete_id = String();
	pending_rename_id = p_session_id;
	_rebuild_chats_list();
}

void LagoonicChatView::_on_rename_submitted(const String &p_text) {
	const String title = (rename_edit != nullptr ? rename_edit->get_text() : p_text).strip_edges();
	const String id = pending_rename_id;
	pending_rename_id = String();
	rename_edit = nullptr;
	if (id.is_empty() || title.is_empty()) {
		_rebuild_chats_list();
		return;
	}
	Array args;
	args.push_back(id);
	args.push_back(title);
	_fire(SNAME("session_rename_requested"), args); // El host responde con set_sessions().
}

void LagoonicChatView::_on_delete_cancelled() {
	pending_delete_id = String();
	pending_rename_id = String();
	rename_edit = nullptr;
	_rebuild_chats_list();
}

void LagoonicChatView::_close_chats() {
	pending_delete_id = String();
	pending_rename_id = String();
	rename_edit = nullptr;
	if (chats_panel != nullptr) {
		chats_panel->set_visible(false);
	}
}

void LagoonicChatView::_on_delete_confirmed() {
	if (pending_delete_id.is_empty()) {
		return;
	}
	const String id = pending_delete_id;
	pending_delete_id = String();
	_fire(SNAME("session_delete_requested"), id);
}

void LagoonicChatView::set_todos(const Array &p_todos) {
	plan_todos = p_todos;
	_rebuild_plan();
	_notify_listeners(SNAME("todos"));
}

void LagoonicChatView::_on_plan_toggle() {
	if (plan_list == nullptr) {
		return;
	}
	plan_list->set_visible(!plan_list->is_visible());
	plan_toggle->set_button_icon(get_editor_theme_icon(plan_list->is_visible() ? SNAME("GuiTreeArrowDown") : SNAME("GuiTreeArrowRight")));
}

void LagoonicChatView::_rebuild_plan() {
	if (plan_card == nullptr) {
		return;
	}
	for (int i = plan_list->get_child_count() - 1; i >= 0; i--) {
		Node *c = plan_list->get_child(i);
		plan_list->remove_child(c);
		c->queue_free();
	}
	int total = 0;
	int done = 0;
	String current;
	String first_pending;
	for (int i = 0; i < plan_todos.size(); i++) {
		const Dictionary t = plan_todos[i];
		const String state = t.get("state", "pending");
		const String text = t.get("text", "");
		if (state == "cancelled") {
			continue;
		}
		total++;
		String mark = String::utf8("○");
		Color color = LagoonicVibeTheme::current().text_muted;
		if (state == "completed") {
			done++;
			mark = String::utf8("✓");
		} else if (state == "in_progress") {
			mark = String::utf8("●");
			color = LagoonicVibeTheme::current().accent;
			if (current.is_empty()) {
				current = text;
			}
		} else if (first_pending.is_empty()) {
			first_pending = text;
		}
		Label *row = memnew(Label);
		row->set_text(mark + "  " + text);
		row->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		row->set_custom_minimum_size(Size2(1, 0));
		row->add_theme_color_override(SceneStringName(font_color), color);
		plan_list->add_child(row);
	}
	if (current.is_empty()) {
		current = first_pending;
	}
	const bool show = total > 0 && done < total && !plan_card_suppressed;
	plan_card->set_visible(show);
	if (!show) {
		return;
	}
	plan_title->set_text(vformat(String::utf8("Plan · %d de %d"), done, total));
	plan_current->set_text(current);
	plan_current->set_visible(!current.is_empty());
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
	preferred_model = p_model_ref;
	for (int i = 0; i < model_option->get_item_count(); i++) {
		if (model_option->is_item_separator(i)) {
			continue;
		}
		const Variant meta = model_option->get_item_metadata(i);
		const String ref = meta.get_type() == Variant::STRING ? String(meta) : String(model_option->get_item_text(i));
		if (ref == p_model_ref) {
			model_option->select(i);
			_rebuild_effort_options();
			return;
		}
	}
	model_option->select(0); // Ese modelo ya no esta: el recomendado.
	_rebuild_effort_options();
}

String LagoonicChatView::get_current_model() const {
	if (model_option != nullptr && model_option->get_selected() >= 0) {
		const Variant meta = model_option->get_item_metadata(model_option->get_selected());
		if (meta.get_type() == Variant::STRING) {
			return meta;
		}
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
	user_message_count++;
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
	if (assistant_bodies.has(p_msg_id)) {
		LagoonicMarkdownView *body = assistant_bodies[p_msg_id];
		body->set_markdown(p_text);
		assistant_texts[p_msg_id] = p_text;
		if (copy_buttons.has(p_msg_id)) {
			copy_buttons[p_msg_id]->set_visible(!p_text.strip_edges().is_empty());
			copy_buttons[p_msg_id]->set_text(String::utf8("Copiar"));
		}
		if (Control *bubble = Object::cast_to<Control>(body->get_parent())) {
			bubble->set_visible(!p_text.strip_edges().is_empty());
		}
	}
	// El fin del turno lo marca el evento busy=false (notify_turn_finished),
	// no el primer texto: el agente puede seguir usando herramientas.
	if (busy) {
		turn_message_id = p_msg_id;
		if (!turn_message_ids.has(p_msg_id)) {
			turn_message_ids.push_back(p_msg_id);
		}
	}
	(void)box;
	_scroll_to_bottom();
}

// --- Narracion de pasos (puerto de ToolNarration.kt de movil) ---

static String _lagoonic_short_file(const String &p_path) {
	const String t = p_path.strip_edges().trim_suffix("/");
	if (t.is_empty() || t.contains(" ") || t.contains("\n") || t.length() > 200) {
		return String();
	}
	String last = t.get_slice("/", t.get_slice_count("/") - 1);
	last = last.get_slice(":", last.get_slice_count(":") - 1);
	return last.left(60);
}

// Primer programa real de una linea de shell: salta `cd … &&`, `VAR=x`,
// `sudo`/`env`.
static String _lagoonic_first_program(const String &p_command) {
	String cmd = p_command;
	for (const char *sep : { "&&", "||", ";", "|" }) {
		cmd = cmd.replace(sep, "\n");
	}
	String segment;
	for (const String &seg : cmd.split("\n", false)) {
		const String t = seg.strip_edges();
		if (t.is_empty()) {
			continue;
		}
		if (segment.is_empty()) {
			segment = t;
		}
		if (!t.begins_with("cd ") && t != "cd") {
			segment = t;
			break;
		}
	}
	for (const String &word : segment.split(" ", false)) {
		if (word.contains("=") || word == "sudo" || word == "env" || word == "time" || word == "nohup" || word == "exec") {
			continue;
		}
		return word.trim_prefix("\"").trim_suffix("\"").trim_prefix("'").trim_suffix("'").to_lower();
	}
	return String();
}

static bool _lagoonic_writes_file(const String &p_command) {
	// `> fichero` o `>> fichero`, pero no `2>/dev/null` ni `&>`.
	for (int i = 0; i < p_command.length(); i++) {
		if (p_command[i] == '>' && (i == 0 || (p_command[i - 1] != '2' && p_command[i - 1] != '&' && p_command[i - 1] != '>'))) {
			const String rest = p_command.substr(i + 1).trim_prefix(">").strip_edges();
			if (!rest.is_empty() && !rest.begins_with("/dev/null") && !rest.begins_with("&")) {
				return true;
			}
		}
	}
	return false;
}

static String _lagoonic_narrate_shell(const String &p_command) {
	const String program = _lagoonic_first_program(p_command);
	const String text = p_command.to_lower();
	auto in = [&](std::initializer_list<const char *> p_set) {
		for (const char *s : p_set) {
			if (program == s) {
				return true;
			}
		}
		return false;
	};
	if (program == "godot" || program.ends_with("/godot") || program.contains("godot.")) {
		return text.contains("--export") ? String::utf8("Exportando el juego") : String::utf8("Probando el juego con Godot");
	}
	if (in({ "top", "ps", "sample", "vm_stat", "pgrep", "lsof", "htop", "pidstat", "spindump" })) {
		return String::utf8("Revisando el rendimiento");
	}
	if (in({ "ls", "tree", "du", "stat", "file", "pwd" })) {
		return String::utf8("Revisando los archivos");
	}
	if (in({ "find", "fd" })) {
		return String::utf8("Buscando archivos");
	}
	if (in({ "grep", "rg", "ag", "egrep" })) {
		return String::utf8("Buscando en el código");
	}
	if (in({ "cat", "head", "tail", "less", "more", "wc", "sed", "awk", "jq", "diff" }) && !_lagoonic_writes_file(p_command)) {
		return String::utf8("Leyendo archivos");
	}
	if (program == "mkdir") {
		return String::utf8("Creando carpetas");
	}
	if (in({ "rm", "rmdir" })) {
		return String::utf8("Borrando archivos");
	}
	if (in({ "mv", "cp" })) {
		return String::utf8("Moviendo archivos");
	}
	if (in({ "touch", "tee" }) || (in({ "echo", "printf", "cat" }) && _lagoonic_writes_file(p_command))) {
		return String::utf8("Escribiendo archivos");
	}
	if (in({ "npm", "pnpm", "yarn", "pip", "pip3", "apt", "apt-get", "bun", "brew" }) && (text.contains(" install") || text.contains(" add "))) {
		return String::utf8("Instalando dependencias");
	}
	if (program == "git") {
		return String::utf8("Usando Git");
	}
	if (in({ "curl", "wget" })) {
		return String::utf8("Descargando");
	}
	if (in({ "gradle", "./gradlew", "gradlew", "make", "cmake", "tsc", "cargo", "scons" })) {
		return String::utf8("Compilando el proyecto");
	}
	if (in({ "python", "python3", "node", "deno", "bash", "sh", "npx", "zsh" })) {
		return String::utf8("Ejecutando un script");
	}
	return String::utf8("Ejecutando un comando");
}

String LagoonicChatView::_narrate_tool(const String &p_label, const String &p_detail) {
	const String sep = String::utf8(" · ");
	const int at = p_label.find(sep);
	const String name = (at >= 0 ? p_label.left(at) : p_label).strip_edges();
	const String rest = at >= 0 ? p_label.substr(at + sep.length()).strip_edges() : String();
	const String lower = name.to_lower();
	// Lo que hay tras el nombre: el comando o la ruta; si falta, el detalle.
	const String subject_src = rest.is_empty() ? p_detail : rest;
	const String file = _lagoonic_short_file(subject_src);
	auto with_file = [&](const String &p_phrase) {
		return file.is_empty() ? p_phrase : p_phrase + sep + file;
	};
	if (lower.begins_with("godot:")) {
		const String action = lower.substr(6).strip_edges();
		if (action == "project create") {
			return String::utf8("Creando el proyecto de Godot");
		}
		if (action == "project import") {
			return String::utf8("Importando recursos");
		}
		if (action == "scene create") {
			return with_file(String::utf8("Creando una escena"));
		}
		if (action == "scene read" || action == "editor scene tree") {
			return with_file(String::utf8("Revisando una escena"));
		}
		if (action == "scene save as" || action == "editor save") {
			return with_file(String::utf8("Guardando la escena"));
		}
		if (action == "node add") {
			return String::utf8("Agregando elementos a la escena");
		}
		if (action == "editor node") {
			return String::utf8("Revisando un elemento de la escena");
		}
		if (action == "node edit" || action == "scene inherit") {
			return String::utf8("Ordenando la escena");
		}
		if (action == "signal") {
			return String::utf8("Conectando las piezas del juego");
		}
		if (action == "game input") {
			return String::utf8("Jugando al juego");
		}
		if (action == "game wait") {
			return String::utf8("Dejando correr el juego");
		}
		if (action == "game screenshot") {
			return String::utf8("Mirando la pantalla del juego");
		}
		if (action == "game tree" || action == "game node" || action == "game exec") {
			return String::utf8("Comprobando qué pasa en el juego");
		}
		if (action == "game monitors" || action == "profiler") {
			return String::utf8("Midiendo el rendimiento del juego");
		}
		if (action == "game stop") {
			return String::utf8("Cerrando el juego");
		}
		if (action == "debug") {
			return String::utf8("Buscando el fallo paso a paso");
		}
		if (action == "input map" || action == "add gamepad support") {
			return String::utf8("Configurando los controles");
		}
		if (action == "project setting" || action == "autoload" || action == "layer names" || action == "plugin") {
			return String::utf8("Ajustando el proyecto");
		}
		if (action == "collision") {
			return String::utf8("Ajustando las colisiones");
		}
		if (action == "tileset" || action == "tilemap") {
			return String::utf8("Dibujando el nivel");
		}
		if (action == "animation" || action == "sprite frames") {
			return String::utf8("Creando animaciones");
		}
		if (action == "particles" || action == "shader") {
			return String::utf8("Creando efectos visuales");
		}
		if (action == "theme") {
			return String::utf8("Dando estilo a la interfaz");
		}
		if (action == "navigation") {
			return String::utf8("Preparando los caminos del nivel");
		}
		if (action == "audio bus") {
			return String::utf8("Ajustando el sonido");
		}
		if (action == "resource" || action == "import options") {
			return with_file(String::utf8("Preparando recursos"));
		}
		if (action == "filesystem") {
			return String::utf8("Ordenando los archivos");
		}
		if (action == "find references") {
			return String::utf8("Buscando dónde se usa un archivo");
		}
		if (action == "class doc" || action == "class list") {
			return String::utf8("Consultando la documentación de Godot");
		}
		if (action == "localization") {
			return String::utf8("Añadiendo idiomas");
		}
		if (action == "godot exec" || action == "editor exec") {
			return String::utf8("Haciendo un ajuste a medida");
		}
		if (action == "editor action" || action == "editor select" || action == "editor open scene" || action == "editor status" || action == "editor reload") {
			return String::utf8("Usando el editor");
		}
		if (action.begins_with("asset")) {
			return String::utf8("Creando recursos para el juego");
		}
		if (action == "node set property") {
			return String::utf8("Ajustando propiedades");
		}
		if (action == "node remove") {
			return String::utf8("Quitando elementos de la escena");
		}
		if (action == "sprite set texture") {
			return String::utf8("Poniendo imágenes");
		}
		if (action == "script validate") {
			return with_file(String::utf8("Revisando el código"));
		}
		if (action.begins_with("game") || action == "editor play" || action == "editor stop" || action == "editor run log") {
			return String::utf8("Probando el juego con Godot");
		}
		if (action == "project set main scene") {
			return with_file(String::utf8("Eligiendo la escena principal"));
		}
		if (action.begins_with("export")) {
			return String::utf8("Exportando el juego");
		}
		if (action == "editor open") {
			return String::utf8("Abriendo el editor");
		}
		if (action == "editor screenshot") {
			return String::utf8("Mirando el editor");
		}
		return String::utf8("Revisando el proyecto de Godot");
	}
	if (lower == "bash" || lower == "shell" || lower == "terminal") {
		return _lagoonic_narrate_shell(rest.is_empty() ? p_detail : rest);
	}
	if (lower == "read" || lower == "view") {
		return with_file(String::utf8("Leyendo"));
	}
	if (lower == "edit" || lower == "write" || lower == "apply_patch" || lower == "apply patch" || lower == "patch" || lower == "multiedit") {
		return with_file(String::utf8("Editando"));
	}
	if (lower == "list" || lower == "ls") {
		return String::utf8("Revisando los archivos");
	}
	if (lower == "glob") {
		return String::utf8("Buscando archivos");
	}
	if (lower == "grep") {
		return String::utf8("Buscando en el código");
	}
	if (lower == "webfetch" || lower == "websearch" || lower == "codesearch" || lower.begins_with("web")) {
		return String::utf8("Consultando en la web");
	}
	if (lower == "task") {
		return String::utf8("Pasando una subtarea a un ayudante");
	}
	if (lower == "skill") {
		return String::utf8("Consultando una guía");
	}
	return String::utf8("Usando ") + name;
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
	const String phrase = _narrate_tool(p_label, p_detail);

	Button *row = memnew(Button);
	row->set_flat(true);
	row->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
	row->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	row->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	row->set_focus_mode(Control::FOCUS_NONE);
	row->set_mouse_filter(Control::MOUSE_FILTER_IGNORE); // Sin detalle tecnico que desplegar.
	row->set_text(phrase);
	row->set_meta(SNAME("phrase"), phrase);
	row->add_theme_color_override(SceneStringName(font_color), LagoonicVibeTheme::current().text_muted);
	row->add_theme_color_override(SNAME("font_disabled_color"), LagoonicVibeTheme::current().text_muted);
	tools_boxes[p_msg_id]->add_child(row);

	tool_rows[key] = nullptr;
	tool_buttons[key] = row;
	tool_raw_details[key] = p_detail;
	tool_raw_labels[key] = p_label;
	tool_phrases[key] = phrase;
	_refresh_tool_icon(key, p_state);

	_collapse_repeats(p_msg_id);
	if (p_state == TOOL_RUNNING) {
		last_activity_label = phrase;
	}
	if (busy) {
		turn_message_id = p_msg_id;
		if (!turn_message_ids.has(p_msg_id)) {
			turn_message_ids.push_back(p_msg_id);
		}
	}
	_scroll_to_bottom();
	(void)box;
}

void LagoonicChatView::_collapse_repeats(const String &p_msg_id) {
	if (!tools_boxes.has(p_msg_id)) {
		return;
	}
	// De cada racha de pasos con la misma frase se ve solo el ultimo (su
	// estado es el mas reciente).
	VBoxContainer *tools = tools_boxes[p_msg_id];
	const int n = tools->get_child_count();
	for (int i = 0; i < n; i++) {
		Control *c = Object::cast_to<Control>(tools->get_child(i));
		if (c == nullptr) {
			continue;
		}
		const String phrase = c->get_meta(SNAME("phrase"), String());
		Control *next = i + 1 < n ? Object::cast_to<Control>(tools->get_child(i + 1)) : nullptr;
		const bool repeated = next != nullptr && String(next->get_meta(SNAME("phrase"), String())) == phrase;
		c->set_visible(!repeated);
	}
	_update_activity_header(p_msg_id);
}

void LagoonicChatView::_update_activity_header(const String &p_msg_id) {
	if (!activity_headers.has(p_msg_id) || !tools_boxes.has(p_msg_id)) {
		return;
	}
	VBoxContainer *tools = tools_boxes[p_msg_id];
	int count = 0;
	for (int i = 0; i < tools->get_child_count(); i++) {
		Control *c = Object::cast_to<Control>(tools->get_child(i));
		if (c != nullptr && c->is_visible()) {
			count++;
		}
	}
	activity_counts[p_msg_id] = count;
	Button *header = activity_headers[p_msg_id];
	header->set_visible(count > 0);
	header->set_text(count == 1 ? String("1 paso") : vformat("%d pasos", count));
	header->set_button_icon(get_editor_theme_icon(tools->is_visible() ? SNAME("GuiTreeArrowDown") : SNAME("GuiTreeArrowRight")));
}

void LagoonicChatView::set_assistant_timeline(const String &p_msg_id, const Array &p_timeline) {
	if (!tools_boxes.has(p_msg_id) || !assistant_bodies.has(p_msg_id)) {
		return;
	}
	int last_tool = -1;
	for (int i = 0; i < p_timeline.size(); i++) {
		if (String(Dictionary(p_timeline[i]).get("kind", "")) == "tool") {
			last_tool = i;
		}
	}
	// La burbuja muestra solo la respuesta final (el texto tras el ultimo
	// paso). La narracion intermedia ("Voy a revisar…") no se muestra: la
	// actividad ya cuenta lo que hace el agente en lenguaje sencillo.
	VBoxContainer *tools = tools_boxes[p_msg_id];
	String final_text;
	String all_text;
	int order = 0;
	for (int i = 0; i < p_timeline.size(); i++) {
		const Dictionary entry = p_timeline[i];
		const String kind = entry.get("kind", "");
		if (kind == "tool") {
			const String key = _tool_key(p_msg_id, entry.get("id", ""));
			if (tool_buttons.has(key) && tool_buttons[key]->get_parent() == tools) {
				tools->move_child(tool_buttons[key], MIN(order, tools->get_child_count() - 1));
				order++;
			}
		} else if (kind == "text") {
			const String text = String(entry.get("text", "")).strip_edges();
			if (text.is_empty()) {
				continue;
			}
			all_text += text + "\n\n";
			if (i > last_tool) {
				final_text += (final_text.is_empty() ? String() : String("\n\n")) + text;
			}
		}
	}
	_collapse_repeats(p_msg_id);

	LagoonicMarkdownView *body = assistant_bodies[p_msg_id];
	body->set_markdown(final_text);
	assistant_texts[p_msg_id] = all_text; // Solo para encontrar archivos.
	if (copy_buttons.has(p_msg_id)) {
		copy_buttons[p_msg_id]->set_visible(!all_text.strip_edges().is_empty());
	}
	if (Control *bubble = Object::cast_to<Control>(body->get_parent())) {
		bubble->set_visible(!final_text.is_empty());
	}
}

void LagoonicChatView::update_tool_state(const String &p_msg_id, const String &p_tool_id, int p_state, const String &p_detail) {
	const String key = _tool_key(p_msg_id, p_tool_id);
	_refresh_tool_icon(key, p_state);
	if (!p_detail.is_empty()) {
		tool_raw_details[key] = p_detail;
	}
	if (p_state == TOOL_RUNNING && tool_phrases.has(key)) {
		last_activity_label = tool_phrases[key];
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

	// Como en movil: icono de aviso, "Revisa esta accion", que pide el
	// agente y las rutas afectadas; Rechazar / Permitir una vez.
	HBoxContainer *hb = memnew(HBoxContainer);
	hb->add_theme_constant_override(SNAME("separation"), 10 * EDSCALE);
	card->add_child(hb);

	TextureRect *icon = memnew(TextureRect);
	icon->set_stretch_mode(TextureRect::STRETCH_KEEP_CENTERED);
	icon->set_v_size_flags(Control::SIZE_SHRINK_BEGIN);
	if (is_inside_tree()) {
		icon->set_texture(get_editor_theme_icon(SNAME("StatusWarning")));
	}
	hb->add_child(icon);

	VBoxContainer *vb = memnew(VBoxContainer);
	vb->add_theme_constant_override(SNAME("separation"), 4);
	vb->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	hb->add_child(vb);

	Label *heading = memnew(Label);
	heading->set_text(String::utf8("Revisa esta acción"));
	heading->set_theme_type_variation(LagoonicVibeTheme::ROLE_LABEL);
	vb->add_child(heading);

	Label *title = memnew(Label);
	title->set_text(p_title);
	title->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	title->set_custom_minimum_size(Size2(1, 0));
	vb->add_child(title);

	if (!p_detail.is_empty()) {
		Label *detail = memnew(Label);
		detail->set_text(p_detail);
		detail->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		detail->set_custom_minimum_size(Size2(1, 0));
		detail->set_theme_type_variation(LagoonicVibeTheme::MUTED_LABEL);
		vb->add_child(detail);
	}

	HBoxContainer *buttons = memnew(HBoxContainer);
	buttons->add_theme_constant_override(SNAME("separation"), 6);
	buttons->set_alignment(BoxContainer::ALIGNMENT_END);
	vb->add_child(buttons);

	Button *reject = memnew(Button);
	reject->set_text(TTRC("Rechazar"));
	reject->set_theme_type_variation(LagoonicVibeTheme::GHOST_BUTTON);
	reject->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChatView::_on_permission_button).bind(p_request_id, REPLY_REJECT));
	buttons->add_child(reject);

	Button *approve = memnew(Button);
	approve->set_text(TTRC("Permitir una vez"));
	approve->set_theme_type_variation(LagoonicVibeTheme::SEND_BUTTON);
	approve->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChatView::_on_permission_button).bind(p_request_id, REPLY_ONCE));
	buttons->add_child(approve);

	stick_to_bottom = true;
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
	if (p_busy && turn_start_msec == 0) {
		turn_start_msec = OS::get_singleton()->get_ticks_msec();
	}
	busy = p_busy;
	if (chats_busy_note != nullptr) {
		_rebuild_chats_list();
	}
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
		if (busy) {
			busy_label->set_text(String::utf8("Pensando"));
		}
	}
}

void LagoonicChatView::set_connected(bool p_connected) {
	connected = p_connected;
	set_busy(busy);
}

void LagoonicChatView::clear_messages() {
	user_message_count = 0;
	worked_ms.clear();
	callable_mp(this, &LagoonicChatView::_notify_listeners).call_deferred(StringName("cleared"));
	for (KeyValue<String, VBoxContainer *> kv : message_boxes) {
		if (kv.value != nullptr) {
			kv.value->queue_free();
		}
	}
	message_boxes.clear();
	assistant_bodies.clear();
	tools_boxes.clear();
	tool_rows.clear();
	tool_dots.clear();
	tool_buttons.clear();
	tool_raw_details.clear();
	tool_phrases.clear();
	tool_raw_labels.clear();
	worked_labels.clear();
	copy_buttons.clear();
	media_boxes.clear();
	assistant_texts.clear();
	turn_message_ids.clear();
	if (media_player != nullptr) {
		media_player->stop();
	}
	activity_headers.clear();
	activity_counts.clear();
	turn_message_id = String();
	turn_start_msec = 0;
	last_activity_label = String();
	stick_to_bottom = true;
	set_todos(Array());
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
	if (chats_button != nullptr) {
		chats_button->set_button_icon(get_editor_theme_icon(SNAME("History")));
	}
	if (bar_new_button != nullptr) {
		bar_new_button->set_button_icon(get_editor_theme_icon(SNAME("Add")));
	}
	if (latest_button != nullptr) {
		latest_button->set_button_icon(get_editor_theme_icon(SNAME("ArrowDown")));
	}
	if (plan_toggle != nullptr && plan_list != nullptr) {
		plan_toggle->set_button_icon(get_editor_theme_icon(plan_list->is_visible() ? SNAME("GuiTreeArrowDown") : SNAME("GuiTreeArrowRight")));
	}
	for (const KeyValue<String, Button *> &kv : tool_buttons) {
		_refresh_tool_icon(kv.key, TOOL_DONE);
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
	ClassDB::bind_method(D_METHOD("set_todos", "todos"), &LagoonicChatView::set_todos);
	ClassDB::bind_method(D_METHOD("add_history_message", "message"), &LagoonicChatView::add_history_message);
	ClassDB::bind_method(D_METHOD("notify_turn_finished"), &LagoonicChatView::notify_turn_finished);
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
	ADD_SIGNAL(MethodInfo("sessions_requested"));
	ADD_SIGNAL(MethodInfo("session_delete_requested", PropertyInfo(Variant::STRING, "session_id")));
	ADD_SIGNAL(MethodInfo("session_rename_requested", PropertyInfo(Variant::ARRAY, "id_and_title")));
	ADD_SIGNAL(MethodInfo("boss_changed", PropertyInfo(Variant::STRING, "agent_id")));
	ADD_SIGNAL(MethodInfo("model_changed", PropertyInfo(Variant::STRING, "model_ref")));
	ADD_SIGNAL(MethodInfo("mode_changed", PropertyInfo(Variant::INT, "mode")));
	ADD_SIGNAL(MethodInfo("effort_changed", PropertyInfo(Variant::STRING, "variant")));
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
	signal_argc[SNAME("sessions_requested")] = 0;
	signal_argc[SNAME("session_delete_requested")] = 1;
	signal_argc[SNAME("session_rename_requested")] = 1;
	signal_argc[SNAME("boss_changed")] = 1;
	signal_argc[SNAME("model_changed")] = 1;
	signal_argc[SNAME("mode_changed")] = 1;
	signal_argc[SNAME("effort_changed")] = 1;
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
	// La gestion de chats vive en la barra de abajo (igual en ambos modos).
	session_option->set_visible(false);
	new_session_button->set_visible(false);

	boss_option = memnew(OptionButton);
	boss_option->set_tooltip_text(TTRC("Jefe del equipo (agente del chat)."));
	for (int i = 0; i < 6; i++) {
		boss_option->add_item(LAGOONIC_BOSS_IDS[i], i);
	}
	boss_option->connect(StringName("item_selected"), callable_mp(this, &LagoonicChatView::_on_boss_selected));
	header->add_child(boss_option);

	// Modo, modelo y esfuerzo viven en el compositor (ver mas abajo).
	mode_option = memnew(OptionButton);
	mode_option->set_tooltip_text(String::utf8("Cómo trabaja el agente."));
	mode_option->add_item(String::utf8("Automático"), MODE_AUTO);
	mode_option->add_item(TTRC("Preguntar antes"), MODE_ASK);
	mode_option->add_item(TTRC("Solo planificar"), MODE_PLAN);
	mode_option->connect(StringName("item_selected"), callable_mp(this, &LagoonicChatView::_on_mode_selected));

	model_option = memnew(OptionButton);
	model_option->set_tooltip_text(TTRC("Modelo"));
	model_option->connect(StringName("item_selected"), callable_mp(this, &LagoonicChatView::_on_model_selected));
	model_option->add_item(TTRC("Modelo recomendado"));
	model_option->set_item_metadata(0, String());

	effort_option = memnew(OptionButton);
	effort_option->set_tooltip_text(String::utf8("Cuánto piensa el modelo antes de responder. Más esfuerzo: mejores respuestas, pero más lentas."));
	effort_option->connect(StringName("item_selected"), callable_mp(this, &LagoonicChatView::_on_effort_selected));

	// --- Barra de chat: titulo + Chats + nuevo (como movil) ---
	session_bar = memnew(HBoxContainer);
	session_bar->add_theme_constant_override(SNAME("separation"), 2);
	add_child(session_bar);

	session_title_label = memnew(Label);
	session_title_label->set_theme_type_variation(LagoonicVibeTheme::ROLE_LABEL);
	session_title_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	session_title_label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	session_title_label->set_mouse_filter(Control::MOUSE_FILTER_PASS);
	session_title_label->set_text(String::utf8("Chat nuevo"));
	session_bar->add_child(session_title_label);

	chats_button = memnew(Button);
	chats_button->set_flat(true);
	chats_button->set_text(TTRC("Chats"));
	chats_button->set_tooltip_text(TTRC("Chats del proyecto"));
	chats_button->set_focus_mode(Control::FOCUS_NONE);
	chats_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChatView::_on_chats_pressed));
	session_bar->add_child(chats_button);

	bar_new_button = memnew(Button);
	bar_new_button->set_flat(true);
	bar_new_button->set_tooltip_text(TTRC("Nuevo chat"));
	bar_new_button->set_focus_mode(Control::FOCUS_NONE);
	bar_new_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChatView::_on_new_session_pressed));
	session_bar->add_child(bar_new_button);

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
	Control *messages_area = memnew(Control);
	messages_area->set_custom_minimum_size(Size2(0, 200));
	messages_area->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	messages_area->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	messages_area->set_clip_contents(true);
	add_child(messages_area);

	{
		// Opaco arriba, transparente abajo; se tine con fade_color.
		Ref<Gradient> g;
		g.instantiate();
		g->set_color(0, Color(1, 1, 1, 1));
		g->set_color(1, Color(1, 1, 1, 0));
		fade_texture.instantiate();
		fade_texture->set_gradient(g);
		fade_texture->set_fill_from(Vector2(0, 0));
		fade_texture->set_fill_to(Vector2(0, 1));
		fade_texture->set_width(4);
		fade_texture->set_height(64);
	}

	messages_scroll = memnew(ScrollContainer);
	messages_scroll->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	messages_area->add_child(messages_scroll);

	fade_overlay = memnew(Control);
	fade_overlay->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	fade_overlay->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	fade_overlay->connect(SceneStringName(draw), callable_mp(this, &LagoonicChatView::_draw_fade));
	messages_area->add_child(fade_overlay);
	messages_scroll->get_v_scroll_bar()->connect(SceneStringName(value_changed), callable_mp(this, &LagoonicChatView::_on_scroll_changed));
	messages_scroll->get_v_scroll_bar()->connect(CoreStringName(changed), callable_mp(this, &LagoonicChatView::_scroll_to_bottom));

	// Boton flotante "Lo ultimo" abajo en el centro.
	VBoxContainer *overlay = memnew(VBoxContainer);
	overlay->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	overlay->set_offset(SIDE_BOTTOM, -10 * EDSCALE);
	overlay->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	messages_area->add_child(overlay);
	Control *overlay_spacer = memnew(Control);
	overlay_spacer->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	overlay_spacer->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	overlay->add_child(overlay_spacer);
	HBoxContainer *overlay_row = memnew(HBoxContainer);
	overlay_row->set_alignment(BoxContainer::ALIGNMENT_CENTER);
	overlay_row->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	overlay->add_child(overlay_row);
	latest_button = memnew(Button);
	latest_button->set_text(String::utf8("Lo último"));
	latest_button->set_theme_type_variation(LagoonicVibeTheme::CHIP_BUTTON);
	latest_button->set_focus_mode(Control::FOCUS_NONE);
	latest_button->set_visible(false);
	latest_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChatView::_on_latest_pressed));
	overlay_row->add_child(latest_button);

	// Panel "Chats del proyecto": cubre los mensajes, con el estilo del chat
	// (no una ventana del sistema).
	chats_panel = memnew(PanelContainer);
	chats_panel->set_theme_type_variation(LagoonicVibeTheme::CARD);
	chats_panel->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	chats_panel->set_visible(false);
	messages_area->add_child(chats_panel);
	{
		VBoxContainer *vb = memnew(VBoxContainer);
		vb->add_theme_constant_override(SNAME("separation"), 10);
		chats_panel->add_child(vb);

		HBoxContainer *top = memnew(HBoxContainer);
		vb->add_child(top);
		Label *title = memnew(Label);
		title->set_text(TTRC("Chats del proyecto"));
		title->set_theme_type_variation(LagoonicVibeTheme::TITLE_LABEL);
		title->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		top->add_child(title);
		Button *new_chat = memnew(Button);
		new_chat->set_text(TTRC("Nuevo chat"));
		new_chat->set_theme_type_variation(LagoonicVibeTheme::SEND_BUTTON);
		new_chat->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
		new_chat->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChatView::_close_chats));
		new_chat->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChatView::_on_new_session_pressed));
		top->add_child(new_chat);
		Button *close = memnew(Button);
		close->set_text(String::utf8("✕"));
		close->set_flat(true);
		close->set_theme_type_variation(LagoonicVibeTheme::GHOST_BUTTON);
		close->set_tooltip_text(TTRC("Cerrar"));
		close->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
		close->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChatView::_close_chats));
		top->add_child(close);

		chats_busy_note = memnew(Label);
		chats_busy_note->set_text(String::utf8("Termina la tarea en curso antes de cambiar de chat."));
		chats_busy_note->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		chats_busy_note->set_custom_minimum_size(Size2(1, 0));
		chats_busy_note->add_theme_color_override(SceneStringName(font_color), Color(0.95, 0.75, 0.25));
		chats_busy_note->set_visible(false);
		vb->add_child(chats_busy_note);

		ScrollContainer *sc = memnew(ScrollContainer);
		sc->set_v_size_flags(Control::SIZE_EXPAND_FILL);
		sc->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
		vb->add_child(sc);

		chats_list = memnew(VBoxContainer);
		chats_list->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		chats_list->add_theme_constant_override(SNAME("separation"), 6);
		sc->add_child(chats_list);
	}

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

	// --- Tarjeta de plan fija sobre la caja de texto ---
	plan_card = memnew(PanelContainer);
	plan_card->set_theme_type_variation(LagoonicVibeTheme::CARD);
	plan_card->set_visible(false);
	add_child(plan_card);
	{
		VBoxContainer *vb = memnew(VBoxContainer);
		vb->add_theme_constant_override(SNAME("separation"), 4);
		plan_card->add_child(vb);
		HBoxContainer *top = memnew(HBoxContainer);
		vb->add_child(top);
		plan_title = memnew(Label);
		plan_title->set_theme_type_variation(LagoonicVibeTheme::ROLE_LABEL);
		plan_title->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		top->add_child(plan_title);
		plan_toggle = memnew(Button);
		plan_toggle->set_flat(true);
		plan_toggle->set_focus_mode(Control::FOCUS_NONE);
		plan_toggle->set_tooltip_text(TTRC("Ver todos los pasos del plan."));
		plan_toggle->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChatView::_on_plan_toggle));
		top->add_child(plan_toggle);
		plan_current = memnew(Label);
		plan_current->set_theme_type_variation(LagoonicVibeTheme::MUTED_LABEL);
		plan_current->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
		plan_current->set_custom_minimum_size(Size2(1, 0));
		vb->add_child(plan_current);
		plan_list = memnew(VBoxContainer);
		plan_list->add_theme_constant_override(SNAME("separation"), 2);
		plan_list->set_visible(false);
		vb->add_child(plan_list);
	}

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

	VBoxContainer *composer_box = memnew(VBoxContainer);
	composer_box->add_theme_constant_override(SNAME("separation"), 6);
	composer->add_child(composer_box);

	HBoxContainer *input_row = memnew(HBoxContainer);
	input_row->add_theme_constant_override(SNAME("separation"), 8);
	composer_box->add_child(input_row);

	// Fila de opciones bajo el texto: modo, modelo, esfuerzo ... enviar.
	composer_controls = memnew(HBoxContainer);
	composer_controls->add_theme_constant_override(SNAME("separation"), 2);
	composer_box->add_child(composer_controls);
	for (OptionButton *ob : { mode_option, model_option, effort_option }) {
		ob->set_flat(true);
		ob->set_focus_mode(Control::FOCUS_NONE);
		ob->set_fit_to_longest_item(false); // Ancho del texto elegido, no del mas largo.
		ob->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
		composer_controls->add_child(ob);
	}
	model_option->set_custom_minimum_size(Size2(0, 0));
	effort_option->set_visible(false);
	Control *controls_spacer = memnew(Control);
	controls_spacer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	controls_spacer->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	composer_controls->add_child(controls_spacer);

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
	composer_controls->add_child(send_button);

	stop_button = memnew(Button);
	stop_button->set_text(TTRC("Detener"));
	stop_button->set_theme_type_variation(LagoonicVibeTheme::GHOST_BUTTON);
	stop_button->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	stop_button->set_tooltip_text(TTRC("Aborta el turno en curso (jefe + worker)."));
	stop_button->set_disabled(true);
	stop_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChatView::_on_stop_pressed));
	composer_controls->add_child(stop_button);

	poll_timer = memnew(Timer);
	poll_timer->set_wait_time(0.5);
	poll_timer->set_autostart(true);
	poll_timer->connect("timeout", callable_mp(this, &LagoonicChatView::_poll));
	add_child(poll_timer);

	set_connected(false);
}
