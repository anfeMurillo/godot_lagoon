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
#include "editor/file_system/editor_file_system.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/button.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
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
