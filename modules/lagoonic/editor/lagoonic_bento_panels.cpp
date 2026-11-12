/**************************************************************************/
/*  lagoonic_bento_panels.cpp                                             */
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


#include "lagoonic_bento_panels.h"

#include "lagoonic_chat_view.h"
#include "lagoonic_vibe_theme.h"

#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "core/os/time.h"
#include "editor/docks/filesystem_dock.h"
#include "editor/editor_data.h"
#include "editor/editor_node.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/scene/scene_tree_editor.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/button.h"
#include "scene/gui/color_rect.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/progress_bar.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/texture_rect.h"
#include "scene/gui/tree.h"
#include "scene/main/timer.h"
#include "scene/resources/style_box_flat.h"

static Label *_bento_muted(const String &p_text) {
	Label *l = memnew(Label);
	l->set_text(p_text);
	l->set_theme_type_variation(LagoonicVibeTheme::MUTED_LABEL);
	l->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	l->set_custom_minimum_size(Size2(1, 0));
	return l;
}

static ScrollContainer *_bento_scroll(Control *p_child) {
	ScrollContainer *sc = memnew(ScrollContainer);
	sc->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	sc->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	p_child->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	sc->add_child(p_child);
	return sc;
}

static String _bento_res_path(const String &p_path) {
	String p = p_path.strip_edges();
	if (p.begins_with("res://")) {
		return p;
	}
	const String root = ProjectSettings::get_singleton()->globalize_path("res://");
	if (p.begins_with(root)) {
		return "res://" + p.substr(root.length());
	}
	return "res://" + p.trim_prefix("./").trim_prefix("/");
}

// ---------------------------------------------------------------- Plan

void LagoonicPlanPanel::setup(LagoonicChatView *p_chat) {
	chat = p_chat;
	chat->add_listener(callable_mp(this, &LagoonicPlanPanel::_on_chat));
	_rebuild();
}

void LagoonicPlanPanel::_on_chat(const StringName &p_what) {
	if (p_what == SNAME("todos") || p_what == SNAME("cleared") || p_what == SNAME("session")) {
		_rebuild();
	}
}

void LagoonicPlanPanel::_rebuild() {
	for (int i = list->get_child_count() - 1; i >= 0; i--) {
		Node *c = list->get_child(i);
		list->remove_child(c);
		c->queue_free();
	}
	const Array todos = chat != nullptr ? chat->get_todos() : Array();
	const LagoonicVibeTheme::Palette p = LagoonicVibeTheme::current();
	int total = 0;
	int done = 0;
	for (int i = 0; i < todos.size(); i++) {
		const Dictionary t = todos[i];
		const String state = t.get("state", "pending");
		String mark = String::utf8("○");
		Color color = p.text_muted;
		Color text_color = p.text;
		if (state == "completed") {
			mark = String::utf8("✓");
			color = Color(0.4, 0.82, 0.5);
			text_color = p.text_muted;
			done++;
			total++;
		} else if (state == "in_progress") {
			mark = String::utf8("●");
			color = p.accent.lerp(Color(1, 1, 1), 0.2f);
			total++;
		} else if (state == "cancelled") {
			mark = String::utf8("×");
			text_color = p.text_muted;
		} else {
			total++;
		}
		HBoxContainer *row = memnew(HBoxContainer);
		row->add_theme_constant_override(SNAME("separation"), int(10 * EDSCALE));
		list->add_child(row);
		Label *m = memnew(Label);
		m->set_text(mark);
		m->set_v_size_flags(Control::SIZE_SHRINK_BEGIN);
		m->set_custom_minimum_size(Size2(14 * EDSCALE, 0));
		m->add_theme_color_override(SceneStringName(font_color), color);
		row->add_child(m);
		Label *l = memnew(Label);
		l->set_text(t.get("text", ""));
		l->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		l->set_custom_minimum_size(Size2(1, 0));
		l->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		l->add_theme_color_override(SceneStringName(font_color), text_color);
		row->add_child(l);
	}
	progress->set_visible(total > 0);
	progress->set_max(MAX(total, 1));
	progress->set_value(done);
	if (total == 0) {
		summary->set_text(String::utf8("Cuando el agente planifique una tarea, verás aquí sus pasos."));
	} else if (done == total) {
		summary->set_text(vformat(String::utf8("Plan completado · %d pasos"), total));
	} else {
		summary->set_text(vformat(String::utf8("%d de %d pasos hechos"), done, total));
	}
}

LagoonicPlanPanel::LagoonicPlanPanel() {
	add_theme_constant_override(SNAME("separation"), int(10 * EDSCALE));
	summary = _bento_muted(String());
	add_child(summary);
	progress = memnew(ProgressBar);
	progress->set_show_percentage(false);
	progress->set_custom_minimum_size(Size2(0, 6 * EDSCALE));
	{
		const LagoonicVibeTheme::Palette p = LagoonicVibeTheme::current();
		Ref<StyleBoxFlat> bg;
		bg.instantiate();
		bg->set_bg_color(Color(p.text, 0.08f));
		bg->set_corner_radius_all(int(3 * EDSCALE));
		Ref<StyleBoxFlat> fill = bg->duplicate();
		fill->set_bg_color(p.accent);
		progress->add_theme_style_override(SNAME("background"), bg);
		progress->add_theme_style_override(SNAME("fill"), fill);
	}
	add_child(progress);
	list = memnew(VBoxContainer);
	list->add_theme_constant_override(SNAME("separation"), int(8 * EDSCALE));
	add_child(_bento_scroll(list));
}

// ---------------------------------------------------------------- Cambios

void LagoonicChangesPanel::setup(LagoonicChatView *p_chat, const Callable &p_provider) {
	chat = p_chat;
	provider = p_provider;
	chat->add_listener(callable_mp(this, &LagoonicChangesPanel::_on_chat));
}

void LagoonicChangesPanel::_on_chat(const StringName &p_what) {
	if (p_what == SNAME("turn_finished") || p_what == SNAME("session") || p_what == SNAME("cleared")) {
		refresh();
	}
}

void LagoonicChangesPanel::_on_show(const String &p_path) {
	if (FileSystemDock::get_singleton() != nullptr) {
		FileSystemDock::get_singleton()->navigate_to_path(p_path);
	}
}

void LagoonicChangesPanel::refresh() {
	for (int i = list->get_child_count() - 1; i >= 0; i--) {
		Node *c = list->get_child(i);
		list->remove_child(c);
		c->queue_free();
	}
	const Array rows = provider.is_valid() ? Array(provider.call()) : Array();
	const LagoonicVibeTheme::Palette p = LagoonicVibeTheme::current();
	int adds = 0;
	int dels = 0;
	for (int i = 0; i < rows.size(); i++) {
		const Dictionary r = rows[i];
		const String path = _bento_res_path(r.get("path", ""));
		if (path.contains("/.")) {
			continue; // Carpetas internas (.godot, .lagoonic).
		}
		const int a = r.get("additions", 0);
		const int d = r.get("deletions", 0);
		adds += a;
		dels += d;

		HBoxContainer *row = memnew(HBoxContainer);
		row->add_theme_constant_override(SNAME("separation"), int(8 * EDSCALE));
		list->add_child(row);
		TextureRect *icon = memnew(TextureRect);
		icon->set_stretch_mode(TextureRect::STRETCH_KEEP_CENTERED);
		icon->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
		const StringName type = EditorFileSystem::get_singleton() != nullptr ? StringName(EditorFileSystem::get_singleton()->get_file_type(path)) : StringName();
		icon->set_texture(get_editor_theme_icon(type != StringName() && has_theme_icon(type, SNAME("EditorIcons")) ? type : SNAME("File")));
		row->add_child(icon);
		VBoxContainer *names = memnew(VBoxContainer);
		names->add_theme_constant_override(SNAME("separation"), 0);
		names->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		row->add_child(names);
		Label *name = memnew(Label);
		name->set_text(path.get_file());
		name->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
		names->add_child(name);
		Label *dir = memnew(Label);
		dir->set_text(path.get_base_dir());
		dir->set_theme_type_variation(LagoonicVibeTheme::MUTED_LABEL);
		dir->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
		names->add_child(dir);
		Label *plus = memnew(Label);
		plus->set_text("+" + itos(a));
		plus->add_theme_color_override(SceneStringName(font_color), Color(0.4, 0.82, 0.5));
		row->add_child(plus);
		Label *minus = memnew(Label);
		minus->set_text(String::utf8("−") + itos(d));
		minus->add_theme_color_override(SceneStringName(font_color), Color(0.92, 0.42, 0.42));
		row->add_child(minus);
		Button *show = memnew(Button);
		show->set_flat(true);
		show->set_focus_mode(Control::FOCUS_NONE);
		show->set_button_icon(get_editor_theme_icon(SNAME("Search")));
		show->set_tooltip_text(TTRC("Mostrarlo en el panel Sistema de archivos."));
		show->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChangesPanel::_on_show).bind(path));
		row->add_child(show);
	}
	const int files = list->get_child_count();
	if (files == 0) {
		summary->set_text(String::utf8("Aún no hay cambios en este chat. Cuando el agente toque tu juego, verás aquí qué archivos cambió."));
	} else {
		summary->set_text(vformat(String::utf8("%d %s · +%d −%d líneas"), files, files == 1 ? "archivo" : "archivos", adds, dels));
	}
	(void)p;
}

void LagoonicChangesPanel::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY) {
		callable_mp(this, &LagoonicChangesPanel::refresh).call_deferred();
	}
	if (p_what == NOTIFICATION_THEME_CHANGED && refresh_button != nullptr) {
		refresh_button->set_button_icon(get_editor_theme_icon(SNAME("Reload")));
	}
}

LagoonicChangesPanel::LagoonicChangesPanel() {
	add_theme_constant_override(SNAME("separation"), int(10 * EDSCALE));
	HBoxContainer *top = memnew(HBoxContainer);
	add_child(top);
	summary = _bento_muted(String());
	summary->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	top->add_child(summary);
	refresh_button = memnew(Button);
	refresh_button->set_flat(true);
	refresh_button->set_focus_mode(Control::FOCUS_NONE);
	refresh_button->set_tooltip_text(TTRC("Actualizar"));
	refresh_button->set_v_size_flags(Control::SIZE_SHRINK_BEGIN);
	refresh_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicChangesPanel::refresh));
	top->add_child(refresh_button);
	list = memnew(VBoxContainer);
	list->add_theme_constant_override(SNAME("separation"), int(8 * EDSCALE));
	add_child(_bento_scroll(list));
}

// ---------------------------------------------------------------- Archivos

void LagoonicFilesPanel::setup(LagoonicChatView *p_chat) {
	chat = p_chat;
}

void LagoonicFilesPanel::_queue_rebuild() {
	if (!rebuild_queued) {
		rebuild_queued = true;
		callable_mp(this, &LagoonicFilesPanel::_rebuild).call_deferred();
	}
}

static void _bento_fill_tree(Control *p_owner, Tree *p_tree, TreeItem *p_parent, EditorFileSystemDirectory *p_dir, const String &p_filter, int p_depth) {
	if (p_dir == nullptr || p_depth > 16) {
		return;
	}
	const Ref<Texture2D> folder_icon = p_owner->get_editor_theme_icon(SNAME("Folder"));
	for (int i = 0; i < p_dir->get_subdir_count(); i++) {
		EditorFileSystemDirectory *sub = p_dir->get_subdir(i);
		// La pieza interna de Lagoonic no es parte del juego del usuario.
		const String sub_path = sub->get_path();
		if (sub_path == "res://addons/lagoonic/" || (sub_path == "res://addons/" && sub->get_file_count() == 0 && sub->get_subdir_count() == 1 && sub->get_subdir(0)->get_name() == "lagoonic")) {
			continue;
		}
		if (p_filter.is_empty()) {
			TreeItem *item = p_tree->create_item(p_parent);
			item->set_text(0, sub->get_name());
			item->set_icon(0, folder_icon);
			item->set_icon_modulate(0, LagoonicVibeTheme::current().accent.lerp(Color(1, 1, 1), 0.35f));
			item->set_selectable(0, false);
			item->set_collapsed(true);
			_bento_fill_tree(p_owner, p_tree, item, sub, p_filter, p_depth + 1);
		} else {
			_bento_fill_tree(p_owner, p_tree, p_parent, sub, p_filter, p_depth + 1);
		}
	}
	for (int i = 0; i < p_dir->get_file_count(); i++) {
		const String name = p_dir->get_file(i);
		if (!p_filter.is_empty() && !name.containsn(p_filter)) {
			continue;
		}
		TreeItem *item = p_tree->create_item(p_parent);
		item->set_text(0, name);
		const StringName type = p_dir->get_file_type(i);
		item->set_icon(0, p_owner->get_editor_theme_icon(p_owner->has_theme_icon(type, SNAME("EditorIcons")) ? type : SNAME("File")));
		item->set_metadata(0, p_dir->get_file_path(i));
		if (!p_filter.is_empty()) {
			item->set_tooltip_text(0, p_dir->get_file_path(i));
		}
	}
}

void LagoonicFilesPanel::_rebuild() {
	rebuild_queued = false;
	if (!is_inside_tree()) {
		return;
	}
	tree->clear();
	TreeItem *root = tree->create_item();
	EditorFileSystem *efs = EditorFileSystem::get_singleton();
	if (efs != nullptr) {
		_bento_fill_tree(this, tree, root, efs->get_filesystem(), search->get_text().strip_edges(), 0);
	}
}

void LagoonicFilesPanel::_on_selected() {
	TreeItem *item = tree->get_selected();
	if (item == nullptr || item->get_metadata(0).get_type() != Variant::STRING) {
		return;
	}
	selected_path = item->get_metadata(0);
	for (int i = preview->get_child_count() - 1; i >= 0; i--) {
		Node *c = preview->get_child(i);
		preview->remove_child(c);
		c->queue_free();
	}
	if (chat != nullptr) {
		chat->add_media_card_to(preview, selected_path);
	}
	Button *mention = memnew(Button);
	mention->set_text(String::utf8("Mencionar en el chat"));
	mention->set_theme_type_variation(LagoonicVibeTheme::CHIP_BUTTON);
	mention->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
	mention->set_tooltip_text(String::utf8("Añade este archivo a tu mensaje para que el agente sepa de cuál hablas."));
	mention->connect(SceneStringName(pressed), callable_mp(this, &LagoonicFilesPanel::_on_mention));
	preview->add_child(mention);
	preview->set_visible(true);
}

void LagoonicFilesPanel::_on_mention() {
	if (chat != nullptr && !selected_path.is_empty()) {
		chat->insert_in_composer(selected_path + " ");
	}
}

void LagoonicFilesPanel::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			if (EditorFileSystem::get_singleton() != nullptr) {
				EditorFileSystem::get_singleton()->connect(SNAME("filesystem_changed"), callable_mp(this, &LagoonicFilesPanel::_queue_rebuild));
			}
			search->set_right_icon(get_editor_theme_icon(SNAME("Search")));
			_queue_rebuild();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (EditorFileSystem::get_singleton() != nullptr) {
				EditorFileSystem::get_singleton()->disconnect(SNAME("filesystem_changed"), callable_mp(this, &LagoonicFilesPanel::_queue_rebuild));
			}
		} break;
	}
}

LagoonicFilesPanel::LagoonicFilesPanel() {
	add_theme_constant_override(SNAME("separation"), int(8 * EDSCALE));
	search = memnew(LineEdit);
	search->set_placeholder(String::utf8("Buscar archivo…"));
	search->set_clear_button_enabled(true);
	search->connect(SceneStringName(text_changed), callable_mp(this, &LagoonicFilesPanel::_queue_rebuild).unbind(1));
	add_child(search);
	tree = memnew(Tree);
	tree->set_hide_root(true);
	tree->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	tree->set_custom_minimum_size(Size2(0, 120 * EDSCALE));
	tree->add_theme_style_override(SceneStringName(panel), memnew(StyleBoxEmpty));
	tree->add_theme_style_override(SNAME("focus"), memnew(StyleBoxEmpty));
	tree->connect(SNAME("item_selected"), callable_mp(this, &LagoonicFilesPanel::_on_selected));
	add_child(tree);
	preview = memnew(VBoxContainer);
	preview->add_theme_constant_override(SNAME("separation"), int(6 * EDSCALE));
	preview->set_visible(false);
	add_child(preview);
}

// ---------------------------------------------------------------- Contexto

void LagoonicContextPanel::setup(LagoonicChatView *p_chat) {
	chat = p_chat;
	callable_mp(this, &LagoonicContextPanel::_refresh).call_deferred();
}

void LagoonicContextPanel::_add_field(const String &p_key, const String &p_label) {
	VBoxContainer *cell = memnew(VBoxContainer);
	cell->add_theme_constant_override(SNAME("separation"), int(2 * EDSCALE));
	cell->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	grid->add_child(cell);
	Label *l = memnew(Label);
	l->set_text(p_label);
	l->set_theme_type_variation(LagoonicVibeTheme::MUTED_LABEL);
	cell->add_child(l);
	Label *v = memnew(Label);
	v->set_text(String::utf8("—"));
	v->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	v->set_custom_minimum_size(Size2(1, 0));
	cell->add_child(v);
	values[p_key] = v;
}

void LagoonicContextPanel::_refresh() {
	if (chat == nullptr || !is_visible_in_tree()) {
		return;
	}
	const Dictionary d = chat->get_context_info();
	values["title"]->set_text(d.get("title", ""));
	values["title"]->set_tooltip_text(d.get("title", ""));
	values["state"]->set_text(bool(d.get("busy", false)) ? String::utf8("Trabajando…") : String::utf8("Listo"));
	values["model"]->set_text(d.get("model", ""));
	values["mode"]->set_text(d.get("mode", ""));
	values["effort"]->set_text(d.get("effort", ""));
	values["user"]->set_text(itos(int(d.get("user_messages", 0))));
	values["answers"]->set_text(itos(int(d.get("answers", 0))));
	values["steps"]->set_text(itos(int(d.get("steps", 0))));
	values["worked"]->set_text(d.get("worked", ""));
	const int64_t updated = d.get("updated_at", 0);
	if (updated > 0) {
		const Dictionary dt = Time::get_singleton()->get_datetime_dict_from_unix_time(updated / 1000 + int64_t(Time::get_singleton()->get_time_zone_from_system().get("bias", 0)) * 60);
		values["updated"]->set_text(vformat("%02d/%02d %02d:%02d", int(dt["day"]), int(dt["month"]), int(dt["hour"]), int(dt["minute"])));
	} else {
		values["updated"]->set_text(String::utf8("—"));
	}
	values["project"]->set_text(ProjectSettings::get_singleton()->globalize_path("res://").trim_suffix("/").get_file());
	// Las celdas son estrechas: el texto entero queda al pasar el raton.
	for (const KeyValue<String, Label *> &kv : values) {
		kv.value->set_tooltip_text(kv.value->get_text());
		kv.value->set_mouse_filter(Control::MOUSE_FILTER_PASS);
	}
}

LagoonicContextPanel::LagoonicContextPanel() {
	add_theme_constant_override(SNAME("separation"), int(8 * EDSCALE));
	grid = memnew(GridContainer);
	grid->set_columns(2);
	grid->add_theme_constant_override(SNAME("h_separation"), int(16 * EDSCALE));
	grid->add_theme_constant_override(SNAME("v_separation"), int(14 * EDSCALE));
	add_child(_bento_scroll(grid));
	_add_field("title", "Chat");
	_add_field("state", "Estado");
	_add_field("model", "Modelo");
	_add_field("mode", "Modo");
	_add_field("effort", "Esfuerzo");
	_add_field("worked", "Tiempo trabajando");
	_add_field("user", "Tus mensajes");
	_add_field("answers", "Respuestas");
	_add_field("steps", "Pasos");
	_add_field("updated", String::utf8("Última actividad"));
	_add_field("project", "Proyecto");
	timer = memnew(Timer);
	timer->set_wait_time(1.0);
	timer->set_autostart(true);
	timer->connect("timeout", callable_mp(this, &LagoonicContextPanel::_refresh));
	add_child(timer);
}

// ---------------------------------------------------------------- Equipo

static String _team_duration(uint64_t p_msec) {
	const uint64_t secs = p_msec / 1000;
	if (secs < 60) {
		return vformat("%d s", int(secs));
	}
	if (secs < 3600) {
		return vformat("%d min %d s", int(secs / 60), int(secs % 60));
	}
	return vformat("%d h %d min", int(secs / 3600), int((secs % 3600) / 60));
}

String LagoonicTeamPanel::_status_text(const Dictionary &p_task, uint64_t p_now) {
	const String status = p_task.get("status", "queued");
	const uint64_t started = p_task.get("started_msec", 0);
	const uint64_t finished = p_task.get("finished_msec", 0);
	if (status == "running") {
		return String::utf8("Trabajando · ") + _team_duration(p_now > started ? p_now - started : 0);
	}
	if (status == "succeeded") {
		return finished > started ? String::utf8("Terminó en ") + _team_duration(finished - started) : String::utf8("Terminó");
	}
	if (status == "failed") {
		return String::utf8("No pudo terminar");
	}
	if (status == "cancelled") {
		return String::utf8("Cancelada");
	}
	return String::utf8("En cola");
}

void LagoonicTeamPanel::setup(const Callable &p_provider) {
	provider = p_provider;
	callable_mp(this, &LagoonicTeamPanel::_refresh).call_deferred();
}

void LagoonicTeamPanel::_on_report_toggled(int p_number) {
	if (open_reports.has(p_number)) {
		open_reports.erase(p_number);
	} else {
		open_reports.insert(p_number);
	}
	signature = String(); // Fuerza reconstruir.
	_refresh();
}

void LagoonicTeamPanel::_refresh() {
	if (!provider.is_valid() || !is_visible_in_tree()) {
		return;
	}
	const Variant got = provider.call();
	if (got.get_type() != Variant::DICTIONARY) {
		return;
	}
	const Dictionary team = got;
	// Firma de lo que cambia la estructura (no el tiempo que corre).
	String sig = String(team.get("boss_name", "")) + "|" + itos(int(bool(team.get("busy", false)))) + "|";
	const Array workers = team.get("workers", Array());
	for (int i = 0; i < workers.size(); i++) {
		sig += String(Dictionary(workers[i]).get("id", "")) + ",";
	}
	const Array tasks = team.get("tasks", Array());
	for (int i = 0; i < tasks.size(); i++) {
		const Dictionary t = tasks[i];
		sig += "|" + itos(int(t.get("number", 0))) + ":" + String(t.get("status", ""));
	}
	if (sig != signature) {
		signature = sig;
		_rebuild(team);
	} else {
		_update_times(team);
	}
}

void LagoonicTeamPanel::_update_times(const Dictionary &p_team) {
	const uint64_t now = p_team.get("now_msec", 0);
	const Array tasks = p_team.get("tasks", Array());
	for (int i = 0; i < tasks.size(); i++) {
		const Dictionary t = tasks[i];
		if (String(t.get("status", "")) != "running") {
			continue;
		}
		Label **label = status_labels.getptr(int(t.get("number", 0)));
		if (label != nullptr) {
			(*label)->set_text(_status_text(t, now));
		}
	}
}

void LagoonicTeamPanel::_rebuild(const Dictionary &p_team) {
	for (int i = list->get_child_count() - 1; i >= 0; i--) {
		Node *c = list->get_child(i);
		list->remove_child(c);
		c->queue_free();
	}
	status_labels.clear();
	const LagoonicVibeTheme::Palette p = LagoonicVibeTheme::current();
	const uint64_t now = p_team.get("now_msec", 0);
	const Array workers = p_team.get("workers", Array());
	const Array tasks = p_team.get("tasks", Array());
	const bool busy = p_team.get("busy", false);
	const Color green = Color(0.4, 0.82, 0.5);
	const Color red = Color(0.95, 0.45, 0.45);
	const Color active = p.accent.lerp(Color(1, 1, 1), 0.2f);

	int running = 0;
	int queued = 0;
	int done = 0;
	HashSet<String> busy_workers;
	for (int i = 0; i < tasks.size(); i++) {
		const Dictionary t = tasks[i];
		const String status = t.get("status", "");
		if (status == "running") {
			running++;
			busy_workers.insert(t.get("worker_id", ""));
		} else if (status == "queued") {
			queued++;
			busy_workers.insert(t.get("worker_id", ""));
		} else {
			done++;
		}
	}
	if (workers.is_empty() && tasks.is_empty()) {
		summary->set_text(String::utf8("Tu líder trabaja solo. Añade ayudantes en Configuración › Equipo y verás aquí cada tarea que les reparta."));
	} else if (tasks.is_empty()) {
		summary->set_text(String::utf8("Cuando el líder reparta una tarea a un ayudante, aparecerá aquí enlazada a él."));
	} else if (running + queued > 0) {
		PackedStringArray parts;
		if (running > 0) {
			parts.push_back(vformat(String::utf8("%d trabajando"), running));
		}
		if (queued > 0) {
			parts.push_back(vformat(String::utf8("%d en cola"), queued));
		}
		if (done > 0) {
			parts.push_back(vformat(String::utf8("%d terminadas"), done));
		}
		summary->set_text(String::utf8(" · ").join(parts));
	} else {
		summary->set_text(done == 1 ? String::utf8("1 tarea repartida, ya terminada") : vformat(String::utf8("%d tareas repartidas, todas terminadas"), done));
	}

	// --- Lider ---
	PanelContainer *boss_card = memnew(PanelContainer);
	{
		Ref<StyleBoxFlat> sb;
		sb.instantiate();
		sb->set_bg_color(Color(p.accent, 0.12f));
		sb->set_border_color(Color(p.accent, 0.45f));
		sb->set_border_width_all(1);
		sb->set_corner_radius_all(int(10 * EDSCALE));
		sb->set_content_margin_individual(12 * EDSCALE, 8 * EDSCALE, 12 * EDSCALE, 8 * EDSCALE);
		boss_card->add_theme_style_override(SceneStringName(panel), sb);
	}
	list->add_child(boss_card);
	VBoxContainer *boss_box = memnew(VBoxContainer);
	boss_box->add_theme_constant_override(SNAME("separation"), int(2 * EDSCALE));
	boss_card->add_child(boss_box);
	Label *boss_caption = memnew(Label);
	boss_caption->set_text(String::utf8("Líder"));
	boss_caption->set_theme_type_variation(LagoonicVibeTheme::MUTED_LABEL);
	boss_box->add_child(boss_caption);
	Label *boss_name = memnew(Label);
	boss_name->set_text(p_team.get("boss_name", "OpenCode"));
	boss_name->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	boss_name->set_custom_minimum_size(Size2(1, 0));
	boss_box->add_child(boss_name);
	Label *boss_state = memnew(Label);
	boss_state->set_theme_type_variation(LagoonicVibeTheme::MUTED_LABEL);
	if (running + queued > 0) {
		boss_state->set_text(String::utf8("Esperando a su equipo"));
	} else if (busy) {
		boss_state->set_text(String::utf8("Trabajando"));
	} else {
		boss_state->set_text(String::utf8("Listo"));
	}
	boss_box->add_child(boss_state);

	// --- Tareas, colgando del lider por una linea ---
	if (!tasks.is_empty()) {
		HBoxContainer *branch = memnew(HBoxContainer);
		branch->add_theme_constant_override(SNAME("separation"), int(10 * EDSCALE));
		list->add_child(branch);
		MarginContainer *line_margin = memnew(MarginContainer);
		line_margin->add_theme_constant_override(SNAME("margin_left"), int(14 * EDSCALE));
		branch->add_child(line_margin);
		ColorRect *line = memnew(ColorRect);
		line->set_color(Color(p.accent, 0.45f));
		line->set_custom_minimum_size(Size2(MAX(1, int(2 * EDSCALE)), 0));
		line->set_v_size_flags(Control::SIZE_EXPAND_FILL);
		line_margin->add_child(line);
		VBoxContainer *cards = memnew(VBoxContainer);
		cards->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		cards->add_theme_constant_override(SNAME("separation"), int(8 * EDSCALE));
		branch->add_child(cards);

		// Lo mas reciente arriba: es lo que el usuario quiere ver.
		for (int i = tasks.size() - 1; i >= 0; i--) {
			const Dictionary t = tasks[i];
			const String status = t.get("status", "queued");
			const int number = t.get("number", 0);
			Color dot_color = p.text_muted;
			String mark = String::utf8("○");
			if (status == "running") {
				dot_color = active;
				mark = String::utf8("●");
			} else if (status == "succeeded") {
				dot_color = green;
				mark = String::utf8("✓");
			} else if (status == "failed") {
				dot_color = red;
				mark = String::utf8("!");
			} else if (status == "cancelled") {
				mark = String::utf8("×");
			}
			PanelContainer *card = memnew(PanelContainer);
			{
				Ref<StyleBoxFlat> sb;
				sb.instantiate();
				sb->set_bg_color(Color(p.text, status == "running" ? 0.07f : 0.04f));
				sb->set_border_color(status == "running" ? Color(active, 0.5f) : Color(p.border, 0.6f));
				sb->set_border_width_all(1);
				sb->set_corner_radius_all(int(10 * EDSCALE));
				sb->set_content_margin_individual(10 * EDSCALE, 8 * EDSCALE, 10 * EDSCALE, 8 * EDSCALE);
				card->add_theme_style_override(SceneStringName(panel), sb);
			}
			cards->add_child(card);
			VBoxContainer *box = memnew(VBoxContainer);
			box->add_theme_constant_override(SNAME("separation"), int(3 * EDSCALE));
			card->add_child(box);

			HBoxContainer *head = memnew(HBoxContainer);
			head->add_theme_constant_override(SNAME("separation"), int(8 * EDSCALE));
			box->add_child(head);
			Label *dot = memnew(Label);
			dot->set_text(mark);
			dot->set_custom_minimum_size(Size2(14 * EDSCALE, 0));
			dot->add_theme_color_override(SceneStringName(font_color), dot_color);
			head->add_child(dot);
			Label *who = memnew(Label);
			who->set_text(t.get("worker_name", ""));
			who->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			who->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
			who->set_custom_minimum_size(Size2(1, 0));
			head->add_child(who);
			Label *num = memnew(Label);
			num->set_text(vformat(String::utf8("Tarea %d"), number));
			num->set_theme_type_variation(LagoonicVibeTheme::MUTED_LABEL);
			head->add_child(num);

			Label *title = memnew(Label);
			title->set_text(t.get("title", ""));
			title->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
			title->set_custom_minimum_size(Size2(1, 0));
			box->add_child(title);

			Label *state = memnew(Label);
			state->set_text(_status_text(t, now));
			state->set_theme_type_variation(LagoonicVibeTheme::MUTED_LABEL);
			if (status == "failed") {
				state->add_theme_color_override(SceneStringName(font_color), red);
			}
			box->add_child(state);
			status_labels[number] = state;

			const String report = String(t.get("report", "")).strip_edges();
			if (!report.is_empty() && (status == "succeeded" || status == "failed")) {
				const bool open = open_reports.has(number);
				Button *toggle = memnew(Button);
				toggle->set_flat(true);
				toggle->set_focus_mode(Control::FOCUS_NONE);
				toggle->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
				toggle->set_text(open ? String::utf8("Ocultar lo que contó") : String::utf8("Ver lo que contó al líder"));
				toggle->connect(SceneStringName(pressed), callable_mp(this, &LagoonicTeamPanel::_on_report_toggled).bind(number));
				box->add_child(toggle);
				if (open) {
					Label *text = _bento_muted(report.length() > 1500 ? report.substr(0, 1500) + String::utf8("…") : report);
					box->add_child(text);
				}
			}
		}
	}

	// --- Ayudantes sin tarea ahora mismo ---
	PackedStringArray idle;
	for (int i = 0; i < workers.size(); i++) {
		const Dictionary w = workers[i];
		if (!busy_workers.has(String(w.get("id", "")))) {
			idle.push_back(w.get("name", ""));
		}
	}
	if (!idle.is_empty()) {
		list->add_child(_bento_muted(String::utf8("Disponibles: ") + String(", ").join(idle)));
	}
}

LagoonicTeamPanel::LagoonicTeamPanel() {
	add_theme_constant_override(SNAME("separation"), int(10 * EDSCALE));
	summary = _bento_muted(String());
	add_child(summary);
	list = memnew(VBoxContainer);
	list->add_theme_constant_override(SNAME("separation"), int(8 * EDSCALE));
	add_child(_bento_scroll(list));
	timer = memnew(Timer);
	timer->set_wait_time(0.5);
	timer->set_autostart(true);
	timer->connect("timeout", callable_mp(this, &LagoonicTeamPanel::_refresh));
	add_child(timer);
}

// ---------------------------------------------------------------- Escena

void LagoonicScenePanel::_on_node_selected() {
	Node *node = tree->get_selected();
	if (node != nullptr && EditorNode::get_singleton() != nullptr) {
		// Igual que en el editor clasico: el inspector pasa a este elemento.
		EditorNode::get_singleton()->push_item(node);
	}
}

void LagoonicScenePanel::_refresh() {
	if (!is_visible_in_tree() || EditorNode::get_singleton() == nullptr) {
		return;
	}
	Node *root = EditorNode::get_singleton()->get_edited_scene();
	const String scene = root != nullptr ? root->get_scene_file_path() : String("-");
	if (scene == shown_scene) {
		return;
	}
	shown_scene = scene;
	if (root == nullptr) {
		summary->set_text(String::utf8("No hay ninguna escena abierta. Pídele una al agente y verás aquí sus piezas."));
	} else {
		summary->set_text(scene.is_empty() ? String::utf8("Escena nueva, sin guardar") : scene.get_file());
	}
	tree->set_visible(root != nullptr);
}

LagoonicScenePanel::LagoonicScenePanel() {
	add_theme_constant_override(SNAME("separation"), int(8 * EDSCALE));
	summary = _bento_muted(String());
	add_child(summary);
	tree = memnew(SceneTreeEditor(false, true, false));
	tree->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	tree->set_custom_minimum_size(Size2(0, 120 * EDSCALE));
	if (EditorNode::get_singleton() != nullptr) {
		tree->set_editor_selection(EditorNode::get_singleton()->get_editor_selection());
	}
	tree->connect(SNAME("node_selected"), callable_mp(this, &LagoonicScenePanel::_on_node_selected));
	add_child(tree);
	timer = memnew(Timer);
	timer->set_wait_time(0.5);
	timer->set_autostart(true);
	timer->connect("timeout", callable_mp(this, &LagoonicScenePanel::_refresh));
	add_child(timer);
}

// ---------------------------------------------------------------- Propiedades

void LagoonicInspectorPanel::_on_selection_changed() {
	Node *node = nullptr;
	if (EditorNode::get_singleton() != nullptr) {
		const List<Node *> selected = EditorNode::get_singleton()->get_editor_selection()->get_top_selected_node_list();
		if (!selected.is_empty()) {
			node = selected.front()->get();
		}
	}
	const ObjectID id = node != nullptr ? node->get_instance_id() : ObjectID();
	if (id == shown && (node != nullptr) == inspector->is_visible()) {
		return;
	}
	shown = id;
	inspector->edit(node);
	inspector->set_visible(node != nullptr);
	search->set_visible(node != nullptr);
	target_row->set_visible(node != nullptr);
	hint->set_visible(node == nullptr);
	if (node != nullptr) {
		target_label->set_text(String(node->get_name()) + String::utf8("  ·  ") + node->get_class());
		target_label->set_tooltip_text(target_label->get_text());
		const StringName type = node->get_class();
		target_icon->set_texture(get_editor_theme_icon(has_theme_icon(type, SNAME("EditorIcons")) ? type : SNAME("Node")));
	}
}

void LagoonicInspectorPanel::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			if (EditorNode::get_singleton() != nullptr) {
				EditorNode::get_singleton()->get_editor_selection()->connect(SNAME("selection_changed"), callable_mp(this, &LagoonicInspectorPanel::_on_selection_changed));
			}
			search->set_right_icon(get_editor_theme_icon(SNAME("Search")));
			callable_mp(this, &LagoonicInspectorPanel::_on_selection_changed).call_deferred();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (EditorNode::get_singleton() != nullptr) {
				EditorNode::get_singleton()->get_editor_selection()->disconnect(SNAME("selection_changed"), callable_mp(this, &LagoonicInspectorPanel::_on_selection_changed));
			}
		} break;
	}
}

LagoonicInspectorPanel::LagoonicInspectorPanel() {
	add_theme_constant_override(SNAME("separation"), int(8 * EDSCALE));
	hint = _bento_muted(String::utf8("Elige un elemento en el panel Escena para ver y cambiar sus propiedades. Cada cambio se puede deshacer."));
	add_child(hint);
	target_row = memnew(HBoxContainer);
	target_row->add_theme_constant_override(SNAME("separation"), int(8 * EDSCALE));
	target_row->set_visible(false);
	add_child(target_row);
	target_icon = memnew(TextureRect);
	target_icon->set_stretch_mode(TextureRect::STRETCH_KEEP_CENTERED);
	target_row->add_child(target_icon);
	target_label = memnew(Label);
	target_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	target_label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	target_label->set_custom_minimum_size(Size2(1, 0));
	target_label->set_mouse_filter(Control::MOUSE_FILTER_PASS);
	target_row->add_child(target_label);
	search = memnew(LineEdit);
	search->set_placeholder(String::utf8("Buscar propiedad…"));
	search->set_clear_button_enabled(true);
	search->set_visible(false);
	add_child(search);
	inspector = memnew(EditorInspector);
	inspector->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	inspector->set_custom_minimum_size(Size2(0, 140 * EDSCALE));
	inspector->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	inspector->set_use_doc_hints(true);
	inspector->set_use_folding(true);
	inspector->set_use_filter(true);
	inspector->register_text_enter(search);
	inspector->set_autoclear(true);
	inspector->set_visible(false);
	add_child(inspector);
}
