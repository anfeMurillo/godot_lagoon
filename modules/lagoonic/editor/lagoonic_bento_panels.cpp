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
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "core/io/json.h"
#include "editor/docks/filesystem_dock.h"
#include "editor/editor_data.h"
#include "editor/editor_node.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/scene/scene_tree_editor.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/themes/editor_scale.h"
#include "lagoonic_agent_logos.gen.h"
#include "scene/gui/button.h"
#include "scene/resources/image_texture.h"
#include "core/templates/sort_array.h"
#include "editor/settings/editor_settings.h"
#include "scene/gui/graph_edit.h"
#include "scene/gui/graph_node.h"
#include "scene/gui/color_rect.h"
#include "scene/gui/flow_container.h"
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

void LagoonicContextCube::set_free(float p_free, bool p_known, const Color &p_color) {
	target = CLAMP(p_free, 0.0f, 1.0f);
	if (known != p_known || color != p_color) {
		known = p_known;
		color = p_color;
		queue_redraw();
	}
}

void LagoonicContextCube::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_VISIBILITY_CHANGED:
		case NOTIFICATION_ENTER_TREE: {
			set_process(is_visible_in_tree());
		} break;
		case NOTIFICATION_MOUSE_ENTER: {
			hovered = true;
		} break;
		case NOTIFICATION_MOUSE_EXIT: {
			hovered = false;
		} break;
		case NOTIFICATION_PROCESS: {
			const float delta = float(get_process_delta_time());
			time += delta;
			// The level glides to its value, cube by cube.
			shown = Math::move_toward(shown, target, delta * 0.6f);
			// It turns toward the mouse; idle, it sways slowly.
			float want = Math::sin(time * 0.5f) * 0.22f;
			if (hovered && get_size().x > 0) {
				want = CLAMP((get_local_mouse_position().x / get_size().x - 0.5f) * 1.2f, -0.6f, 0.6f);
			}
			yaw = Math::lerp(yaw, want, MIN(1.0f, delta * 4.0f));
			queue_redraw();
		} break;
		case NOTIFICATION_RESIZED:
		case NOTIFICATION_THEME_CHANGED: {
			queue_redraw();
		} break;
		case NOTIFICATION_DRAW: {
			const Size2 size = get_size();
			if (size.x <= 0 || size.y <= 0) {
				break;
			}
			const int n = size.x >= 150 * EDSCALE ? 5 : 4;
			const float half = n * 0.5f;
			// Camera: turned 45 degrees plus the yaw, looking down ~35 degrees.
			const float turn = Math::PI * 0.25f + yaw;
			const float ct = Math::cos(turn), st = Math::sin(turn);
			const float sp = 0.577f, cp = 0.8165f;
			// Fit: the cube spans at most n*sqrt(2) wide and n*(sp*sqrt(2)+cp) tall.
			const float scale = MIN(size.x / (n * 1.45f), size.y * 0.86f / (n * (sp * 1.42f + cp))) * 0.92f;
			const float bob = Math::sin(time * 1.3f) * 3.0f * EDSCALE;
			const Vector2 center(size.x * 0.5f, size.y * 0.46f + bob);
			// World (x, y, z up), centered on the cube; returns screen point.
			auto rot = [&](const Vector3 &p) {
				return Vector3(p.x * ct - p.y * st, p.x * st + p.y * ct, p.z);
			};
			auto project = [&](const Vector3 &p) {
				const Vector3 r = rot(p - Vector3(half, half, half));
				return center + Vector2(r.x, r.y * sp - r.z * cp) * scale;
			};
			const Vector3 view(0.0f, cp, sp); // Toward the viewer, in turned space.
			const Vector3 light = Vector3(-0.45f, 0.35f, 0.82f).normalized();

			const LagoonicVibeTheme::Palette p = LagoonicVibeTheme::current();
			const Color edge = p.surface;
			const Color ghost = p.border;

			// Soft shadow on the floor, smaller when the cube floats up.
			{
				const float r = n * scale * 0.78f * (1.0f - bob / (60.0f * EDSCALE));
				const Vector2 c(size.x * 0.5f, size.y * 0.46f + (half * cp + n * 0.72f * sp) * scale + 6.0f * EDSCALE);
				for (int ring = 4; ring >= 1; ring--) {
					const float k = ring / 4.0f;
					Vector<Vector2> ellipse;
					for (int i = 0; i < 32; i++) {
						const float a = Math::TAU * i / 32.0f;
						ellipse.push_back(c + Vector2(Math::cos(a) * r * k, Math::sin(a) * r * k * 0.28f));
					}
					draw_colored_polygon(ellipse, Color(0, 0, 0, 0.10f));
				}
			}

			const int free_cubes = known ? int(Math::round(shown * n * n * n)) : 0;
			auto is_free = [&](int x, int y, int z) {
				if (x < 0 || y < 0 || z < 0 || x >= n || y >= n || z >= n) {
					return false;
				}
				return (z * n + x) * n + y < free_cubes;
			};
			// The six faces: normal and corners (unit cube offsets).
			static const Vector3i normals[6] = { Vector3i(0, 0, 1), Vector3i(0, 0, -1), Vector3i(1, 0, 0), Vector3i(-1, 0, 0), Vector3i(0, 1, 0), Vector3i(0, -1, 0) };
			static const Vector3 corners[6][4] = {
				{ Vector3(0, 0, 1), Vector3(1, 0, 1), Vector3(1, 1, 1), Vector3(0, 1, 1) },
				{ Vector3(0, 0, 0), Vector3(0, 1, 0), Vector3(1, 1, 0), Vector3(1, 0, 0) },
				{ Vector3(1, 0, 0), Vector3(1, 1, 0), Vector3(1, 1, 1), Vector3(1, 0, 1) },
				{ Vector3(0, 0, 0), Vector3(0, 0, 1), Vector3(0, 1, 1), Vector3(0, 1, 0) },
				{ Vector3(0, 1, 0), Vector3(0, 1, 1), Vector3(1, 1, 1), Vector3(1, 1, 0) },
				{ Vector3(0, 0, 0), Vector3(1, 0, 0), Vector3(1, 0, 1), Vector3(0, 0, 1) },
			};
			bool face_visible[6];
			float face_light[6];
			for (int f = 0; f < 6; f++) {
				const Vector3 nr = rot(Vector3(normals[f]));
				face_visible[f] = nr.dot(view) > 0.02f;
				face_light[f] = 0.52f + 0.48f * MAX(0.0f, nr.dot(light));
			}
			// Back to front by depth toward the viewer.
			struct Cell {
				Vector3i c;
				float depth;
			};
			LocalVector<Cell> cells;
			for (int z = 0; z < n; z++) {
				for (int y = 0; y < n; y++) {
					for (int x = 0; x < n; x++) {
						const Vector3 r = rot(Vector3(x + 0.5f - half, y + 0.5f - half, z + 0.5f - half));
						cells.push_back({ Vector3i(x, y, z), r.y * cp + r.z * sp });
					}
				}
			}
			struct ByDepth {
				bool operator()(const Cell &a, const Cell &b) const { return a.depth < b.depth; }
			};
			SortArray<Cell, ByDepth> sorter;
			sorter.sort(cells.ptr(), cells.size());

			for (const Cell &cell : cells) {
				const Vector3i c = cell.c;
				const bool solid = is_free(c.x, c.y, c.z);
				for (int f = 0; f < 6; f++) {
					if (!face_visible[f]) {
						continue;
					}
					const Vector3i nb = c + normals[f];
					Vector<Vector2> quad;
					for (int k = 0; k < 4; k++) {
						quad.push_back(project(Vector3(c) + corners[f][k]));
					}
					if (solid) {
						if (is_free(nb.x, nb.y, nb.z)) {
							continue; // Hidden by its neighbor.
						}
						Color shade = color * face_light[f];
						shade.a = 1.0f;
						if (f == 0) {
							shade = shade.lightened(0.08f); // The top catches the light.
						}
						draw_colored_polygon(quad, shade);
						quad.push_back(quad[0]);
						draw_polyline(quad, edge, 1.0f, true);
					} else {
						// A used cube: only the big cube's outer faces, as a
						// faint wireframe of what the window holds.
						const bool outer = nb.x < 0 || nb.y < 0 || nb.z < 0 || nb.x >= n || nb.y >= n || nb.z >= n;
						if (outer) {
							quad.push_back(quad[0]);
							draw_polyline(quad, ghost, 1.0f, true);
						}
					}
				}
			}
		} break;
	}
}

LagoonicContextCube::LagoonicContextCube() {
	set_custom_minimum_size(Size2(0, 150 * EDSCALE));
	set_v_size_flags(Control::SIZE_EXPAND_FILL);
	set_mouse_filter(Control::MOUSE_FILTER_PASS);
}

void LagoonicContextCube::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_free", "free", "known", "color"), &LagoonicContextCube::set_free);
}


static String _context_tokens(int64_t p_tokens) {
	if (p_tokens >= 1000000) {
		return String::num(double(p_tokens) / 1000000.0, 1) + "M";
	}
	if (p_tokens >= 1000) {
		return itos(int64_t(Math::round(double(p_tokens) / 1000.0))) + "k";
	}
	return itos(p_tokens);
}

void LagoonicContextPanel::_refresh() {
	if (chat == nullptr || !is_visible_in_tree()) {
		return;
	}
	const Dictionary d = chat->get_context_info();
	const LagoonicVibeTheme::Palette p = LagoonicVibeTheme::current();
	const int64_t used = d.get("context_used", 0);
	const int64_t size = d.get("context_size", 0);
	const bool known = size > 0;
	const int64_t free_tokens = known ? MAX<int64_t>(0, size - used) : 0;
	const float free = known ? float(double(free_tokens) / double(size)) : 0.0f;
	const Color amber = Color::html("#f0b45a");
	const Color red = Color::html("#f07070");
	const Color state = !known ? p.border : (free < 0.15f ? red : (free < 0.4f ? amber : p.accent));
	cube->set_free(free, known, state);

	percent->set_text(known ? itos(int(Math::round(free * 100.0f))) + String::utf8("% libre") : String::utf8("—"));
	tokens->set_text(known ? vformat(String::utf8("%s de %s libres"), _context_tokens(free_tokens), _context_tokens(size)) : String());
	tokens->set_visible(known);

	String note_text;
	Color note_color = p.text_muted;
	const int64_t compacted = d.get("context_compacted_at", 0);
	const int steps = d.get("steps", 0);
	if (!known) {
		note_text = String::utf8("Aparecerá cuando el agente responda. Algunos agentes no comparten su contexto.");
	} else if (compacted > 0 && int64_t(Time::get_singleton()->get_unix_time_from_system()) - compacted < 30) {
		note_text = String::utf8("Resumió la conversación para hacer sitio");
		note_color = p.accent;
	} else if (free < 0.15f) {
		note_text = String::utf8("Casi lleno: conviene empezar un chat nuevo");
		note_color = red;
	} else if (free < 0.4f) {
		note_text = String::utf8("Pronto resumirá lo antiguo");
		note_color = amber;
	} else if (int64_t(d.get("context_growth", 0)) > 0) {
		const int64_t left = free_tokens / int64_t(d.get("context_growth", 1));
		note_text = left > 50 ? String::utf8("Más de 50 respuestas por delante") : vformat(String::utf8("≈ %d respuestas más"), int(left));
	} else {
		note_text = String::utf8("Mucho sitio por delante");
	}
	note->set_text(note_text);
	note->add_theme_color_override(SceneStringName(font_color), note_color);
	note->set_visible(!note_text.is_empty());


	// The rest of the chat's data, on hover.
	String tip = String(d.get("title", "")) + "\n";
	tip += String::utf8("Estado: ") + (bool(d.get("busy", false)) ? String::utf8("trabajando") : String::utf8("listo")) + "\n";
	tip += String::utf8("Modelo: ") + String(d.get("model", "")) + "\n";
	tip += String::utf8("Modo: ") + String(d.get("mode", "")) + String::utf8(" · ") + String(d.get("effort", "")) + "\n";
	tip += vformat(String::utf8("Tus mensajes: %d · Respuestas: %d · Pasos: %d"), int(d.get("user_messages", 0)), int(d.get("answers", 0)), steps) + "\n";
	tip += String::utf8("Tiempo trabajando: ") + String(d.get("worked", "")) + "\n";
	if (known) {
		tip += vformat(String::utf8("Contexto: %s usados de %s"), _context_tokens(used), _context_tokens(size)) + "\n";
	}
	tip += String::utf8("Proyecto: ") + ProjectSettings::get_singleton()->globalize_path("res://").trim_suffix("/").get_file();
	cube->set_tooltip_text(tip);
}

void LagoonicContextPanel::_bind_methods() {
	ClassDB::bind_method(D_METHOD("setup", "chat"), &LagoonicContextPanel::setup);
}

LagoonicContextPanel::LagoonicContextPanel() {
	add_theme_constant_override(SNAME("separation"), int(4 * EDSCALE));
	cube = memnew(LagoonicContextCube);
	add_child(cube);
	percent = memnew(Label);
	percent->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	percent->add_theme_font_size_override(SceneStringName(font_size), int(26 * EDSCALE));
	add_child(percent);
	tokens = memnew(Label);
	tokens->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	add_child(tokens);
	note = _bento_muted(String());
	note->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	add_child(note);
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

// Logo of a coding agent (editor/agent_logos, baked in by SCsub), or null.
static Ref<Texture2D> _team_logo(const String &p_agent_id) {
	static HashMap<String, Ref<Texture2D>> cache;
	if (const Ref<Texture2D> *hit = cache.getptr(p_agent_id)) {
		return *hit;
	}
	Ref<Texture2D> texture;
	for (int i = 0; LAGOONIC_AGENT_LOGOS[i].id != nullptr; i++) {
		if (p_agent_id != LAGOONIC_AGENT_LOGOS[i].id) {
			continue;
		}
		Vector<uint8_t> bytes;
		bytes.resize(LAGOONIC_AGENT_LOGOS[i].size);
		memcpy(bytes.ptrw(), LAGOONIC_AGENT_LOGOS[i].data, LAGOONIC_AGENT_LOGOS[i].size);
		Ref<Image> image;
		image.instantiate();
		if (image->load_png_from_buffer(bytes) == OK) {
			texture = ImageTexture::create_from_image(image);
		}
		break;
	}
	cache[p_agent_id] = texture;
	return texture;
}

// The agent's logo as a silhouette in the theme's text color, like the
// editor's icons; the initial of its name when there is no logo.
static Control *_team_badge(const String &p_agent_id, const String &p_name, float p_px) {
	const int px = int(p_px * EDSCALE);
	const Color tint = LagoonicVibeTheme::current().text;
	const Ref<Texture2D> logo = _team_logo(p_agent_id);
	if (logo.is_valid()) {
		TextureRect *image = memnew(TextureRect);
		image->set_texture(logo);
		image->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
		image->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
		image->set_custom_minimum_size(Size2(px, px));
		image->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
		image->set_self_modulate(tint);
		image->set_tooltip_text(p_name);
		return image;
	}
	Label *initial = memnew(Label);
	initial->set_text(p_name.strip_edges().left(1).to_upper());
	initial->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	initial->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	initial->set_custom_minimum_size(Size2(px, px));
	initial->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	initial->add_theme_color_override(SceneStringName(font_color), tint);
	initial->add_theme_font_size_override(SceneStringName(font_size), MAX(8, int(px * 0.75f)));
	initial->set_tooltip_text(p_name);
	return initial;
}

static String _team_boss_state(const Dictionary &p_team);

static bool _team_task_live(const String &p_status) {
	return p_status == "running" || p_status == "queued";
}

static Color _team_status_color(const String &p_status, const LagoonicVibeTheme::Palette &p) {
	if (p_status == "running") {
		return p.accent.lerp(Color(1, 1, 1), 0.2f);
	}
	if (p_status == "succeeded") {
		return Color(0.4, 0.82, 0.5);
	}
	if (p_status == "failed") {
		return Color(0.95, 0.45, 0.45);
	}
	if (p_status == "stopped" || p_status == "cancelled") {
		return Color(0.95, 0.7, 0.35);
	}
	return p.text_muted;
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
	if (status == "stopped") {
		return String::utf8("Detenida");
	}
	if (status == "cancelled") {
		return String::utf8("Cancelada");
	}
	return String::utf8("En cola");
}

// ---- Orbitas: constelacion con el lider al centro.

LagoonicTeamOrbit::LagoonicTeamOrbit() {
	set_clip_contents(true);
	set_mouse_filter(MOUSE_FILTER_STOP);
	set_process_internal(true);
}

void LagoonicTeamOrbit::set_team(const Dictionary &p_team) {
	team = p_team;
	queue_redraw();
}

void LagoonicTeamOrbit::set_selected_task(int p_number) {
	selected_task = p_number;
	queue_redraw();
}

bool LagoonicTeamOrbit::_animating() const {
	const Array tasks = team.get("tasks", Array());
	for (int i = 0; i < tasks.size(); i++) {
		if (String(Dictionary(tasks[i]).get("status", "")) == "running") {
			return true;
		}
	}
	return false;
}

String LagoonicTeamOrbit::_hit_tooltip(const Point2 &p_pos) const {
	for (int i = hits.size() - 1; i >= 0; i--) {
		if (hits[i].pos.distance_to(p_pos) <= hits[i].radius + 3 * EDSCALE) {
			return hits[i].tooltip;
		}
	}
	return String();
}

String LagoonicTeamOrbit::get_tooltip(const Point2 &p_pos) const {
	return _hit_tooltip(p_pos);
}

void LagoonicTeamOrbit::gui_input(const Ref<InputEvent> &p_event) {
	const Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_null() || !mb->is_pressed() || mb->get_button_index() != MouseButton::LEFT) {
		return;
	}
	int picked = 0;
	for (int i = hits.size() - 1; i >= 0; i--) {
		if (hits[i].task > 0 && hits[i].pos.distance_to(mb->get_position()) <= hits[i].radius + 3 * EDSCALE) {
			picked = hits[i].task;
			break;
		}
	}
	selected_task = picked;
	queue_redraw();
	if (on_select.is_valid()) {
		on_select.call(picked);
	}
	accept_event();
}

// Un nodo: disco con el logo del agente en el color del tema y un anillo que
// dice el estado (un arco que gira mientras trabaja).
static void _orbit_node(Control *p_canvas, const Vector2 &p_pos, float p_radius, const String &p_agent_id, const String &p_name, const Color &p_ring, bool p_running, bool p_selected, float p_time) {
	const LagoonicVibeTheme::Palette p = LagoonicVibeTheme::current();
	p_canvas->draw_circle(p_pos, p_radius, p.bg.lerp(p.surface, 0.6f), true, -1, true);
	const float ring_w = MAX(1.5f, p_radius * 0.12f);
	if (p_running) {
		p_canvas->draw_arc(p_pos, p_radius, 0, Math::TAU, 48, Color(p_ring, 0.25f), ring_w, true);
		const float start = p_time * 2.4f;
		p_canvas->draw_arc(p_pos, p_radius, start, start + Math::PI * 0.7f, 24, p_ring, ring_w, true);
	} else {
		p_canvas->draw_arc(p_pos, p_radius, 0, Math::TAU, 48, p_ring, ring_w, true);
	}
	if (p_selected) {
		p_canvas->draw_arc(p_pos, p_radius + ring_w * 2.2f, 0, Math::TAU, 48, Color(p.text, 0.7f), MAX(1.0f, ring_w * 0.6f), true);
	}
	const Ref<Texture2D> logo = _team_logo(p_agent_id);
	const float side = p_radius * 1.15f;
	if (logo.is_valid()) {
		p_canvas->draw_texture_rect(logo, Rect2(p_pos - Vector2(side, side) * 0.5f, Vector2(side, side)), false, p.text);
	} else {
		const Ref<Font> font = p_canvas->get_theme_font(SceneStringName(font), SNAME("Label"));
		const int size = MAX(8, int(p_radius * 0.9f));
		const String initial = p_name.strip_edges().left(1).to_upper();
		const Vector2 text_size = font->get_string_size(initial, HORIZONTAL_ALIGNMENT_LEFT, -1, size);
		p_canvas->draw_string(font, p_pos + Vector2(-text_size.x * 0.5f, size * 0.35f), initial, HORIZONTAL_ALIGNMENT_LEFT, -1, size, p.text);
	}
}

void LagoonicTeamOrbit::_notification(int p_what) {
	if (p_what == NOTIFICATION_INTERNAL_PROCESS) {
		if (is_visible_in_tree() && _animating()) {
			time += get_process_delta_time();
			queue_redraw();
		}
		return;
	}
	if (p_what != NOTIFICATION_DRAW) {
		return;
	}
	hits.clear();
	const LagoonicVibeTheme::Palette p = LagoonicVibeTheme::current();
	const Vector2 size = get_size();
	const Vector2 center = size * 0.5f;
	const float span = MIN(size.x, size.y);
	if (span <= 0) {
		return;
	}
	const uint64_t now = team.get("now_msec", 0);
	const Array workers = team.get("workers", Array());
	const Array tasks = team.get("tasks", Array());

	// Agentes: los ayudantes elegidos y quien tenga tareas.
	Vector<String> agent_ids;
	HashMap<String, String> agent_names;
	for (int i = 0; i < workers.size(); i++) {
		const Dictionary w = workers[i];
		const String id = w.get("id", "");
		if (!id.is_empty() && !agent_ids.has(id)) {
			agent_ids.push_back(id);
			agent_names[id] = w.get("name", id);
		}
	}
	for (int i = 0; i < tasks.size(); i++) {
		const Dictionary t = tasks[i];
		const String id = t.get("worker_id", "");
		if (!id.is_empty() && !agent_ids.has(id)) {
			agent_ids.push_back(id);
			agent_names[id] = t.get("worker_name", id);
		}
	}

	const float boss_r = span * 0.075f;
	const float agent_r = span * 0.05f;
	const float task_r = span * 0.028f;
	const float orbit_r = span * 0.27f;
	const float sat_r = span * 0.12f;
	const Color line = Color(p.accent, 0.35f);

	// Orbita de los agentes (gira muy despacio mientras hay trabajo).
	draw_arc(center, orbit_r, 0, Math::TAU, 96, Color(p.text, 0.10f), 1.0f * EDSCALE, true);
	const float spin = time * 0.05f;

	struct Placed {
		Vector2 pos;
		String id;
	};
	Vector<Placed> agents;
	for (int a = 0; a < agent_ids.size(); a++) {
		const float angle = -Math::PI * 0.5f + Math::TAU * a / MAX(1, agent_ids.size()) + spin;
		Placed placed;
		placed.pos = center + Vector2(Math::cos(angle), Math::sin(angle)) * orbit_r;
		placed.id = agent_ids[a];
		agents.push_back(placed);
		draw_line(center, placed.pos, line, 1.2f * EDSCALE, true);
	}

	// Satelites: las tareas de cada agente en su propia orbita.
	for (const Placed &agent : agents) {
		Vector<Dictionary> own;
		for (int i = 0; i < tasks.size(); i++) {
			const Dictionary t = tasks[i];
			if (String(t.get("worker_id", "")) == agent.id) {
				own.push_back(t);
			}
		}
		if (own.is_empty()) {
			continue;
		}
		draw_arc(agent.pos, sat_r, 0, Math::TAU, 64, Color(p.text, 0.08f), 1.0f * EDSCALE, true);
		// Las tareas miran hacia fuera de la constelacion.
		const float outward = (agent.pos - center).angle();
		bool any_running = false;
		for (const Dictionary &t : own) {
			any_running = any_running || String(t.get("status", "")) == "running";
		}
		const float drift = any_running ? time * 0.35f : 0.0f;
		for (int k = 0; k < own.size(); k++) {
			const Dictionary t = own[k];
			const float spread = Math::PI * 1.2f;
			const float angle = own.size() == 1 ? outward + drift : outward - spread * 0.5f + spread * k / (own.size() - 1) + drift;
			const Vector2 pos = agent.pos + Vector2(Math::cos(angle), Math::sin(angle)) * sat_r;
			const String status = t.get("status", "queued");
			const int number = t.get("number", 0);
			draw_line(agent.pos, pos, Color(line, 0.25f), 1.0f * EDSCALE, true);
			_orbit_node(this, pos, task_r, agent.id, agent_names[agent.id], _team_status_color(status, p), status == "running", number == selected_task, time);
			Hit hit;
			hit.pos = pos;
			hit.radius = task_r;
			hit.task = number;
			String tip = vformat(String::utf8("Tarea %d · %s\n%s · %s"), number, String(t.get("title", "")), agent_names[agent.id], LagoonicTeamPanel::_status_text(t, now));
			const String model = t.get("model", "");
			if (!model.is_empty()) {
				tip += String::utf8("\nModelo: ") + model;
			}
			hit.tooltip = tip;
			hits.push_back(hit);
		}
	}

	// Agentes encima de sus lineas.
	for (const Placed &agent : agents) {
		bool busy = false;
		String model;
		int count = 0;
		for (int i = 0; i < tasks.size(); i++) {
			const Dictionary t = tasks[i];
			if (String(t.get("worker_id", "")) != agent.id) {
				continue;
			}
			count++;
			busy = busy || _team_task_live(t.get("status", ""));
			if (model.is_empty()) {
				model = t.get("model", "");
			}
		}
		_orbit_node(this, agent.pos, agent_r, agent.id, agent_names[agent.id], busy ? _team_status_color("running", p) : Color(p.text, 0.35f), false, false, time);
		Hit hit;
		hit.pos = agent.pos;
		hit.radius = agent_r;
		hit.tooltip = agent_names[agent.id] + (model.is_empty() ? String() : String::utf8("\nModelo: ") + model) + "\n" + (count == 0 ? String::utf8("Disponible") : vformat(String::utf8("%d tarea(s)"), count));
		hits.push_back(hit);
	}

	// El lider, al centro.
	const String boss_name = team.get("boss_name", "OpenCode");
	_orbit_node(this, center, boss_r, team.get("boss_id", "opencode"), boss_name, p.accent, false, false, time);
	Hit boss;
	boss.pos = center;
	boss.radius = boss_r;
	boss.tooltip = String::utf8("Líder · ") + boss_name + "\n" + _team_boss_state(team);
	hits.push_back(boss);
}

void LagoonicTeamPanel::_notification(int p_what) {
	if (p_what == NOTIFICATION_THEME_CHANGED) {
		// Logos y colores siguen al tema: se repinta todo.
		signature = String();
		callable_mp(this, &LagoonicTeamPanel::_refresh).call_deferred();
	}
}

void LagoonicTeamPanel::_bind_methods() {
	ClassDB::bind_method(D_METHOD("setup", "provider", "stopper"), &LagoonicTeamPanel::setup, DEFVAL(Callable()));
}

void LagoonicTeamPanel::setup(const Callable &p_provider, const Callable &p_stopper) {
	provider = p_provider;
	stopper = p_stopper;
	callable_mp(this, &LagoonicTeamPanel::_refresh).call_deferred();
}

void LagoonicTeamPanel::_set_view(int p_view) {
	view = View(CLAMP(p_view, 0, 2));
	list_scroll->set_visible(view == VIEW_LIST);
	graph->set_visible(view == VIEW_GRAPH);
	orbit->set_visible(view == VIEW_ORBIT);
	center_button->set_visible(view == VIEW_GRAPH);
	_update_actions_row();
	if (view == VIEW_LIST) {
		details->set_visible(false);
	}
	signature = String(); // La otra vista se construye desde cero.
	layout_signature = String();
	_refresh();
}

void LagoonicTeamPanel::_apply_view_setting() {
	String saved = "orbit";
	EditorSettings *es = EditorSettings::get_singleton();
	if (es != nullptr && es->has_setting("lagoonic/team/view")) {
		saved = es->get_setting("lagoonic/team/view");
	}
	const View wanted = saved == "graph" ? VIEW_GRAPH : (saved == "list" ? VIEW_LIST : VIEW_ORBIT);
	if (wanted != view || (!list_scroll->is_visible() && !graph->is_visible() && !orbit->is_visible())) {
		_set_view(int(wanted));
	}
}

void LagoonicTeamPanel::_on_orbit_selected(int p_number) {
	selected_task = p_number;
	orbit->set_selected_task(p_number);
	_update_details();
}

void LagoonicTeamPanel::_on_stop(int p_number) {
	if (stopper.is_valid()) {
		stopper.call(p_number);
	}
	signature = String(); // Fuerza reconstruir con el nuevo estado.
	_refresh();
}

void LagoonicTeamPanel::_on_report_toggled(int p_number) {
	if (open_reports.has(p_number)) {
		open_reports.erase(p_number);
	} else {
		open_reports.insert(p_number);
	}
	signature = String();
	_refresh();
}

void LagoonicTeamPanel::_on_node_selected(Node *p_node) {
	if (p_node == nullptr) {
		return;
	}
	const String name = p_node->get_name();
	selected_task = name.begins_with("task_") ? name.trim_prefix("task_").to_int() : 0;
	_update_details();
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
	last_team = team;
	// Firma de lo que cambia la vista (no el tiempo que corre).
	String sig = String(team.get("boss_name", "")) + "|" + itos(int(bool(team.get("busy", false)))) + "|";
	const Array workers = team.get("workers", Array());
	for (int i = 0; i < workers.size(); i++) {
		sig += String(Dictionary(workers[i]).get("id", "")) + ",";
	}
	const Array tasks = team.get("tasks", Array());
	for (int i = 0; i < tasks.size(); i++) {
		const Dictionary t = tasks[i];
		sig += "|" + itos(int(t.get("number", 0))) + ":" + String(t.get("status", "")) + ":" + String(t.get("model", ""));
	}
	if (sig != signature) {
		signature = sig;
		_rebuild(team);
	} else {
		_update_times(team);
	}
	if (view != VIEW_LIST) {
		_update_details();
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
	status_labels.clear();
	const Array workers = p_team.get("workers", Array());
	const Array tasks = p_team.get("tasks", Array());
	int running = 0;
	int queued = 0;
	int done = 0;
	for (int i = 0; i < tasks.size(); i++) {
		const String status = Dictionary(tasks[i]).get("status", "");
		if (status == "running") {
			running++;
		} else if (status == "queued") {
			queued++;
		} else {
			done++;
		}
	}
	if (workers.is_empty() && tasks.is_empty()) {
		summary->set_text(String::utf8("Tu líder trabaja solo. Añade ayudantes en Configuración › Equipo y verás aquí cada tarea que les reparta."));
	} else if (tasks.is_empty()) {
		summary->set_text(String::utf8("Cuando el líder reparta una tarea a un ayudante, aparecerá aquí."));
	} else if (running + queued > 0) {
		PackedStringArray parts;
		if (running > 0) {
			parts.push_back(vformat(String::utf8("%d trabajando a la vez"), running));
		}
		if (queued > 0) {
			parts.push_back(vformat(String::utf8("%d en cola"), queued));
		}
		if (done > 0) {
			parts.push_back(done == 1 ? String::utf8("1 terminada") : vformat(String::utf8("%d terminadas"), done));
		}
		summary->set_text(String::utf8(" · ").join(parts));
	} else {
		summary->set_text(done == 1 ? String::utf8("1 tarea repartida, ya terminada") : vformat(String::utf8("%d tareas repartidas, todas terminadas"), done));
	}
	stop_all->set_visible(running + queued > 0 && stopper.is_valid());
	clear_done->set_visible(done > 0 && stopper.is_valid());
	_update_actions_row();
	if (view == VIEW_GRAPH) {
		_rebuild_graph(p_team);
	} else if (view == VIEW_ORBIT) {
		orbit->set_team(p_team);
	} else {
		_rebuild_list(p_team);
	}
}

static String _team_boss_state(const Dictionary &p_team) {
	const Array tasks = p_team.get("tasks", Array());
	for (int i = 0; i < tasks.size(); i++) {
		if (_team_task_live(Dictionary(tasks[i]).get("status", ""))) {
			return String::utf8("Esperando a su equipo");
		}
	}
	return bool(p_team.get("busy", false)) ? String::utf8("Trabajando") : String::utf8("Listo");
}

static Button *_team_stop_button() {
	Button *stop = memnew(Button);
	stop->set_text(String::utf8("Detener"));
	stop->set_flat(true);
	stop->set_focus_mode(Control::FOCUS_NONE);
	stop->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
	stop->set_tooltip_text(String::utf8("Detiene esta tarea. Lo que ya escribió se queda en el proyecto."));
	return stop;
}

// ---- Vista en lista (por defecto): el lider arriba y sus tareas colgando.

void LagoonicTeamPanel::_rebuild_list(const Dictionary &p_team) {
	for (int i = list->get_child_count() - 1; i >= 0; i--) {
		Node *c = list->get_child(i);
		list->remove_child(c);
		c->queue_free();
	}
	const LagoonicVibeTheme::Palette p = LagoonicVibeTheme::current();
	const uint64_t now = p_team.get("now_msec", 0);
	const Array workers = p_team.get("workers", Array());
	const Array tasks = p_team.get("tasks", Array());

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
	HBoxContainer *boss_row = memnew(HBoxContainer);
	boss_row->add_theme_constant_override(SNAME("separation"), int(10 * EDSCALE));
	boss_card->add_child(boss_row);
	boss_row->add_child(_team_badge(p_team.get("boss_id", ""), p_team.get("boss_name", "OpenCode"), 30));
	VBoxContainer *boss_box = memnew(VBoxContainer);
	boss_box->add_theme_constant_override(SNAME("separation"), int(2 * EDSCALE));
	boss_box->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	boss_row->add_child(boss_box);
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
	boss_state->set_text(_team_boss_state(p_team));
	boss_box->add_child(boss_state);

	HashSet<String> busy_workers;
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
			if (_team_task_live(status)) {
				busy_workers.insert(t.get("worker_id", ""));
			}
			const Color color = _team_status_color(status, p);
			String mark = String::utf8("○");
			if (status == "running") {
				mark = String::utf8("●");
			} else if (status == "succeeded") {
				mark = String::utf8("✓");
			} else if (status == "failed") {
				mark = String::utf8("!");
			} else if (status == "stopped" || status == "cancelled") {
				mark = String::utf8("■");
			}
			PanelContainer *card = memnew(PanelContainer);
			{
				Ref<StyleBoxFlat> sb;
				sb.instantiate();
				sb->set_bg_color(Color(p.text, status == "running" ? 0.07f : 0.04f));
				sb->set_border_color(status == "running" ? Color(color, 0.5f) : Color(p.border, 0.6f));
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
			dot->add_theme_color_override(SceneStringName(font_color), color);
			head->add_child(dot);
			head->add_child(_team_badge(t.get("worker_id", ""), t.get("worker_name", ""), 18));
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

			const String model = t.get("model", "");
			if (!model.is_empty()) {
				box->add_child(_bento_muted(String::utf8("Modelo: ") + model));
			}

			HBoxContainer *foot = memnew(HBoxContainer);
			box->add_child(foot);
			Label *state = memnew(Label);
			state->set_text(_status_text(t, now));
			state->set_theme_type_variation(LagoonicVibeTheme::MUTED_LABEL);
			state->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			if (status != "queued") {
				state->add_theme_color_override(SceneStringName(font_color), color);
			}
			foot->add_child(state);
			status_labels[number] = state;
			if (_team_task_live(status) && stopper.is_valid()) {
				Button *stop = _team_stop_button();
				stop->connect(SceneStringName(pressed), callable_mp(this, &LagoonicTeamPanel::_on_stop).bind(number));
				foot->add_child(stop);
			}

			const String report = String(t.get("report", "")).strip_edges();
			if (!report.is_empty() && !_team_task_live(status)) {
				const bool open = open_reports.has(number);
				Button *toggle = memnew(Button);
				toggle->set_flat(true);
				toggle->set_focus_mode(Control::FOCUS_NONE);
				toggle->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
				toggle->set_text(open ? String::utf8("Ocultar lo que contó") : String::utf8("Ver lo que contó al líder"));
				toggle->connect(SceneStringName(pressed), callable_mp(this, &LagoonicTeamPanel::_on_report_toggled).bind(number));
				box->add_child(toggle);
				if (open) {
					box->add_child(_bento_muted(report.length() > 1500 ? report.substr(0, 1500) + String::utf8("…") : report));
				}
			}
		}
	}

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

// ---- Vista en grafo: lider -> agentes -> tareas.

void LagoonicTeamPanel::_update_details() {
	Dictionary task;
	const Array tasks = last_team.get("tasks", Array());
	for (int i = 0; i < tasks.size(); i++) {
		if (int(Dictionary(tasks[i]).get("number", 0)) == selected_task) {
			task = tasks[i];
			break;
		}
	}
	if (task.is_empty() || view == VIEW_LIST) {
		details->set_visible(false);
		return;
	}
	details->set_visible(true);
	const String status = task.get("status", "queued");
	const String model = task.get("model", "");
	details_title->set_text(vformat(String::utf8("Tarea %d · %s"), int(task.get("number", 0)), String(task.get("title", ""))));
	String meta = String(task.get("worker_name", "")) + String::utf8(" · ") + _status_text(task, uint64_t(last_team.get("now_msec", 0)));
	if (!model.is_empty()) {
		meta += String::utf8(" · Modelo: ") + model;
	}
	details_meta->set_text(meta);
	const String report = String(task.get("report", "")).strip_edges();
	details_report->set_text(report.is_empty() ? String::utf8("Aún no ha contado nada al líder.") : (report.length() > 1500 ? report.substr(0, 1500) + String::utf8("…") : report));
	details_stop->set_visible(_team_task_live(status) && stopper.is_valid());
}

static GraphNode *_team_node(const String &p_name, const String &p_title, const Color &p_accent, bool p_highlight, const String &p_agent_id = String(), const String &p_agent_name = String()) {
	GraphNode *node = memnew(GraphNode);
	node->set_name(p_name);
	node->set_title(p_title);
	node->set_resizable(false);
	node->set_custom_minimum_size(Size2(220 * EDSCALE, 0));
	// El borde dice el estado, como los nodos del editor de Godot.
	Ref<StyleBoxFlat> sb;
	sb.instantiate();
	const LagoonicVibeTheme::Palette p = LagoonicVibeTheme::current();
	sb->set_bg_color(Color(p.text, p_highlight ? 0.08f : 0.05f));
	sb->set_border_color(Color(p_accent, p_highlight ? 0.85f : 0.45f));
	sb->set_border_width_all(p_highlight ? 2 : 1);
	sb->set_corner_radius_individual(0, 0, int(8 * EDSCALE), int(8 * EDSCALE));
	sb->set_content_margin_individual(10 * EDSCALE, 6 * EDSCALE, 10 * EDSCALE, 8 * EDSCALE);
	node->add_theme_style_override(SceneStringName(panel), sb);
	Ref<StyleBoxFlat> bar;
	bar.instantiate();
	bar->set_bg_color(Color(p_accent, p_highlight ? 0.35f : 0.18f));
	bar->set_corner_radius_individual(int(8 * EDSCALE), int(8 * EDSCALE), 0, 0);
	bar->set_content_margin_individual(10 * EDSCALE, 4 * EDSCALE, 10 * EDSCALE, 4 * EDSCALE);
	node->add_theme_style_override(SNAME("titlebar"), bar);
	node->add_theme_style_override(SNAME("titlebar_selected"), bar);
	if (!p_agent_id.is_empty()) {
		// El logo del agente, delante del titulo.
		HBoxContainer *titlebar = node->get_titlebar_hbox();
		Control *badge = _team_badge(p_agent_id, p_agent_name, 16);
		titlebar->add_child(badge);
		titlebar->move_child(badge, 0);
		titlebar->add_theme_constant_override(SNAME("separation"), int(6 * EDSCALE));
	}
	return node;
}

static Label *_team_line(const String &p_text, bool p_muted) {
	Label *l = memnew(Label);
	l->set_text(p_text);
	if (p_muted) {
		l->set_theme_type_variation(LagoonicVibeTheme::MUTED_LABEL);
	}
	l->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	l->set_custom_minimum_size(Size2(200 * EDSCALE, 0));
	return l;
}

void LagoonicTeamPanel::_rebuild_graph(const Dictionary &p_team) {
	graph->clear_connections();
	// Lo que el usuario movio a mano se respeta mientras los nodos sean los mismos.
	HashMap<String, Vector2> kept;
	for (int i = graph->get_child_count() - 1; i >= 0; i--) {
		GraphNode *node = Object::cast_to<GraphNode>(graph->get_child(i));
		if (node != nullptr) {
			kept[node->get_name()] = node->get_position_offset();
			graph->remove_child(node);
			node->queue_free();
		}
	}
	const LagoonicVibeTheme::Palette p = LagoonicVibeTheme::current();
	const uint64_t now = p_team.get("now_msec", 0);
	const Array workers = p_team.get("workers", Array());
	const Array tasks = p_team.get("tasks", Array());

	Vector<String> agent_ids;
	HashMap<String, String> agent_names;
	for (int i = 0; i < workers.size(); i++) {
		const Dictionary w = workers[i];
		const String id = w.get("id", "");
		if (!id.is_empty() && !agent_ids.has(id)) {
			agent_ids.push_back(id);
			agent_names[id] = w.get("name", id);
		}
	}
	for (int i = 0; i < tasks.size(); i++) {
		const Dictionary t = tasks[i];
		const String id = t.get("worker_id", "");
		if (!id.is_empty() && !agent_ids.has(id)) {
			agent_ids.push_back(id);
			agent_names[id] = t.get("worker_name", id);
		}
	}

	String layout = "boss";
	GraphNode *boss = _team_node("boss", String::utf8("Líder"), p.accent, true, p_team.get("boss_id", ""), p_team.get("boss_name", "OpenCode"));
	boss->add_child(_team_line(p_team.get("boss_name", "OpenCode"), false));
	boss->add_child(_team_line(_team_boss_state(p_team), true));
	boss->set_slot(0, false, 0, Color(), true, 0, p.accent);
	graph->add_child(boss);

	for (const String &agent : agent_ids) {
		// Las tareas de este agente, la mas reciente arriba.
		Vector<Dictionary> own;
		for (int i = tasks.size() - 1; i >= 0; i--) {
			const Dictionary t = tasks[i];
			if (String(t.get("worker_id", "")) == agent) {
				own.push_back(t);
			}
		}
		bool agent_busy = false;
		String model;
		for (const Dictionary &t : own) {
			agent_busy = agent_busy || _team_task_live(t.get("status", ""));
			if (model.is_empty()) {
				model = t.get("model", "");
			}
		}
		const String agent_node = "agent_" + agent.validate_node_name();
		GraphNode *worker = _team_node(agent_node, agent_names[agent], agent_busy ? _team_status_color("running", p) : p.text_muted, agent_busy, agent, agent_names[agent]);
		worker->add_child(_team_line(model.is_empty() ? String::utf8("Modelo: lo dice al empezar") : String::utf8("Modelo: ") + model, true));
		worker->add_child(_team_line(own.is_empty() ? String::utf8("Disponible") : vformat(String::utf8("%d tarea(s)"), own.size()), true));
		worker->set_slot(0, true, 0, p.accent, !own.is_empty(), 0, p.accent);
		graph->add_child(worker);
		graph->connect_node("boss", 0, agent_node, 0);
		layout += "," + agent_node;

		for (const Dictionary &t : own) {
			const int number = t.get("number", 0);
			const String status = t.get("status", "queued");
			const Color color = _team_status_color(status, p);
			const String task_node = "task_" + itos(number);
			GraphNode *node = _team_node(task_node, vformat(String::utf8("Tarea %d"), number), color, status == "running" || number == selected_task, agent, agent_names[agent]);
			node->add_child(_team_line(t.get("title", ""), false));
			Label *state = _team_line(_status_text(t, now), true);
			state->add_theme_color_override(SceneStringName(font_color), color);
			node->add_child(state);
			status_labels[number] = state;
			const String task_model = t.get("model", "");
			if (!task_model.is_empty() && task_model != model) {
				node->add_child(_team_line(String::utf8("Modelo: ") + task_model, true));
			}
			if (_team_task_live(status) && stopper.is_valid()) {
				Button *stop = _team_stop_button();
				stop->connect(SceneStringName(pressed), callable_mp(this, &LagoonicTeamPanel::_on_stop).bind(number));
				node->add_child(stop);
			}
			node->set_slot(0, true, 0, p.accent, false, 0, Color());
			graph->add_child(node);
			graph->connect_node(agent_node, 0, task_node, 0);
			layout += "," + task_node;
		}
	}

	if (layout != layout_signature) {
		// Nodos nuevos: se recolocan con su tamano real y se centra todo
		// cuando ya tienen tamano (un frame despues).
		layout_signature = layout;
		callable_mp(this, &LagoonicTeamPanel::_layout_graph).call_deferred();
	} else {
		for (int i = 0; i < graph->get_child_count(); i++) {
			GraphNode *node = Object::cast_to<GraphNode>(graph->get_child(i));
			if (node != nullptr && kept.has(node->get_name())) {
				node->set_position_offset(kept[node->get_name()]);
			}
		}
	}
}

void LagoonicTeamPanel::_layout_graph() {
	// Columnas lider -> agentes -> tareas con su tamano real: cada tarea
	// debajo de la anterior con aire, cada agente frente a sus tareas y el
	// lider a la mitad de sus agentes.
	const float col_gap = 90 * EDSCALE;
	const float task_gap = 28 * EDSCALE;
	const float group_gap = 56 * EDSCALE;
	GraphNode *boss = Object::cast_to<GraphNode>(graph->get_node_or_null(NodePath("boss")));
	if (boss == nullptr) {
		return;
	}
	float col_width[3] = { boss->get_combined_minimum_size().x, 0, 0 };
	Vector<GraphNode *> agents;
	HashMap<GraphNode *, Vector<GraphNode *>> agent_tasks;
	List<GraphEdit::Connection> unused;
	(void)unused;
	for (int i = 0; i < graph->get_child_count(); i++) {
		GraphNode *node = Object::cast_to<GraphNode>(graph->get_child(i));
		if (node == nullptr) {
			continue;
		}
		const String name = node->get_name();
		if (name.begins_with("agent_")) {
			agents.push_back(node);
			agent_tasks[node] = Vector<GraphNode *>();
			col_width[1] = MAX(col_width[1], node->get_combined_minimum_size().x);
		} else if (name.begins_with("task_")) {
			col_width[2] = MAX(col_width[2], node->get_combined_minimum_size().x);
		}
	}
	// Las tareas siguen a su agente en el orden en que se anadieron.
	GraphNode *current = nullptr;
	for (int i = 0; i < graph->get_child_count(); i++) {
		GraphNode *node = Object::cast_to<GraphNode>(graph->get_child(i));
		if (node == nullptr) {
			continue;
		}
		const String name = node->get_name();
		if (name.begins_with("agent_")) {
			current = node;
		} else if (name.begins_with("task_") && current != nullptr) {
			agent_tasks[current].push_back(node);
		}
	}
	const float x_agent = col_width[0] + col_gap;
	const float x_task = x_agent + col_width[1] + col_gap;
	float y = 0;
	float first_center = -1;
	float last_center = 0;
	for (GraphNode *agent : agents) {
		const float agent_h = agent->get_combined_minimum_size().y;
		float tasks_h = 0;
		for (GraphNode *task : agent_tasks[agent]) {
			tasks_h += task->get_combined_minimum_size().y;
		}
		tasks_h += MAX(0, agent_tasks[agent].size() - 1) * task_gap;
		const float group_h = MAX(agent_h, tasks_h);
		// El agente, centrado frente a su grupo de tareas.
		agent->set_position_offset(Vector2(x_agent, y + (group_h - agent_h) * 0.5f));
		const float center = y + group_h * 0.5f;
		if (first_center < 0) {
			first_center = center;
		}
		last_center = center;
		float task_y = y + (group_h - tasks_h) * 0.5f;
		for (GraphNode *task : agent_tasks[agent]) {
			task->set_position_offset(Vector2(x_task, task_y));
			task_y += task->get_combined_minimum_size().y + task_gap;
		}
		y += group_h + group_gap;
	}
	const float boss_h = boss->get_combined_minimum_size().y;
	const float boss_center = agents.is_empty() ? boss_h * 0.5f : (first_center + last_center) * 0.5f;
	boss->set_position_offset(Vector2(0, boss_center - boss_h * 0.5f));
	_center_graph();
}

void LagoonicTeamPanel::_center_graph() {
	// Encaja el grafo en el panel (hasta el 100 %) y lo centra en ambos ejes.
	Rect2 bounds;
	bool first = true;
	for (int i = 0; i < graph->get_child_count(); i++) {
		GraphNode *node = Object::cast_to<GraphNode>(graph->get_child(i));
		if (node == nullptr) {
			continue;
		}
		const Rect2 r(node->get_position_offset(), node->get_combined_minimum_size().max(node->get_size()));
		bounds = first ? r : bounds.merge(r);
		first = false;
	}
	const Vector2 view = graph->get_size();
	if (first || view.x <= 0 || view.y <= 0) {
		return;
	}
	const Vector2 room = view - Vector2(48, 48) * EDSCALE;
	const float zoom = CLAMP(MIN(room.x / bounds.size.x, room.y / bounds.size.y), 0.5f, 1.0f);
	graph->set_zoom(zoom);
	graph->set_scroll_offset((bounds.get_center() * zoom) - view * 0.5f);
}

void LagoonicTeamPanel::_update_actions_row() {
	if (actions_row != nullptr && center_button != nullptr && stop_all != nullptr && clear_done != nullptr) {
		actions_row->set_visible(center_button->is_visible() || stop_all->is_visible() || clear_done->is_visible());
	}
}

LagoonicTeamPanel::LagoonicTeamPanel() {
	add_theme_constant_override(SNAME("separation"), int(8 * EDSCALE));

	// The summary takes the whole width; the buttons go on their own row
	// and wrap, so a narrow bento never squeezes the text into one column.
	summary = _bento_muted(String());
	add_child(summary);
	HFlowContainer *top = memnew(HFlowContainer);
	top->add_theme_constant_override(SNAME("h_separation"), int(6 * EDSCALE));
	top->add_theme_constant_override(SNAME("v_separation"), int(6 * EDSCALE));
	add_child(top);
	actions_row = top;
	center_button = memnew(Button);
	center_button->set_text(String::utf8("Centrar"));
	center_button->set_flat(true);
	center_button->set_tooltip_text(String::utf8("Recoloca y centra el grafo."));
	center_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicTeamPanel::_layout_graph));
	top->add_child(center_button);
	stop_all = memnew(Button);
	stop_all->set_text(String::utf8("Detener todo"));
	stop_all->set_tooltip_text(String::utf8("Detiene todas las tareas en marcha o en cola."));
	stop_all->set_visible(false);
	stop_all->connect(SceneStringName(pressed), callable_mp(this, &LagoonicTeamPanel::_on_stop).bind(0));
	top->add_child(stop_all);
	clear_done = memnew(Button);
	clear_done->set_text(String::utf8("Limpiar terminadas"));
	clear_done->set_tooltip_text(String::utf8("Quita del equipo las tareas que ya terminaron, fallaron o se detuvieron. Sus archivos no se tocan."));
	clear_done->set_visible(false);
	clear_done->connect(SceneStringName(pressed), callable_mp(this, &LagoonicTeamPanel::_on_stop).bind(-1));
	top->add_child(clear_done);

	list = memnew(VBoxContainer);
	list->add_theme_constant_override(SNAME("separation"), int(8 * EDSCALE));
	list_scroll = _bento_scroll(list);
	add_child(list_scroll);

	graph = memnew(GraphEdit);
	graph->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	graph->set_custom_minimum_size(Size2(0, 220 * EDSCALE));
	graph->set_show_menu(false);
	graph->set_show_grid_buttons(false);
	graph->set_show_minimap_button(false);
	graph->set_show_arrange_button(false);
	graph->set_minimap_enabled(false);
	graph->set_zoom_min(0.4);
	graph->set_connection_lines_curvature(0.6);
	graph->set_connection_lines_thickness(2 * EDSCALE);
	graph->connect("node_selected", callable_mp(this, &LagoonicTeamPanel::_on_node_selected));
	// Al cambiar el tamano del panel, el grafo se vuelve a centrar.
	graph->connect(SceneStringName(resized), callable_mp(this, &LagoonicTeamPanel::_center_graph));
	add_child(graph);

	orbit = memnew(LagoonicTeamOrbit);
	orbit->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	orbit->set_custom_minimum_size(Size2(0, 260 * EDSCALE));
	orbit->set_on_select(callable_mp(this, &LagoonicTeamPanel::_on_orbit_selected));
	add_child(orbit);

	details = memnew(PanelContainer);
	details->set_visible(false);
	{
		const LagoonicVibeTheme::Palette p = LagoonicVibeTheme::current();
		Ref<StyleBoxFlat> sb;
		sb.instantiate();
		sb->set_bg_color(Color(p.text, 0.04f));
		sb->set_border_color(Color(p.border, 0.6f));
		sb->set_border_width_all(1);
		sb->set_corner_radius_all(int(8 * EDSCALE));
		sb->set_content_margin_individual(10 * EDSCALE, 8 * EDSCALE, 10 * EDSCALE, 8 * EDSCALE);
		details->add_theme_style_override(SceneStringName(panel), sb);
	}
	add_child(details);
	VBoxContainer *details_box = memnew(VBoxContainer);
	details_box->add_theme_constant_override(SNAME("separation"), int(4 * EDSCALE));
	details->add_child(details_box);
	HBoxContainer *details_head = memnew(HBoxContainer);
	details_box->add_child(details_head);
	details_title = memnew(Label);
	details_title->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	details_title->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	details_title->set_custom_minimum_size(Size2(1, 0));
	details_head->add_child(details_title);
	details_stop = memnew(Button);
	details_stop->set_text(String::utf8("Detener"));
	details_stop->connect(SceneStringName(pressed), callable_mp(this, &LagoonicTeamPanel::_on_stop_selected));
	details_head->add_child(details_stop);
	details_meta = _bento_muted(String());
	details_box->add_child(details_meta);
	details_report = memnew(Label);
	details_report->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	details_report->set_custom_minimum_size(Size2(1, 0));
	ScrollContainer *report_scroll = _bento_scroll(details_report);
	report_scroll->set_custom_minimum_size(Size2(0, 90 * EDSCALE));
	report_scroll->set_v_size_flags(Control::SIZE_FILL);
	details_box->add_child(report_scroll);

	timer = memnew(Timer);
	timer->set_wait_time(0.5);
	timer->set_autostart(true);
	timer->connect("timeout", callable_mp(this, &LagoonicTeamPanel::_refresh));
	add_child(timer);

	// La vista sale de Configuracion > Temas (Orbitas por defecto) y cambia
	// en cuanto el usuario la cambia alli.
	list_scroll->set_visible(false);
	graph->set_visible(false);
	orbit->set_visible(false);
	view = VIEW_ORBIT;
	_apply_view_setting();
	if (EditorSettings::get_singleton() != nullptr) {
		EditorSettings::get_singleton()->connect(SNAME("settings_changed"), callable_mp(this, &LagoonicTeamPanel::_apply_view_setting));
	}
}

// ---------------------------------------------------------------- Uso

static constexpr uint64_t USAGE_REFRESH_MSEC = 5 * 60 * 1000;

void LagoonicUsagePanel::_bind_methods() {
	ClassDB::bind_method(D_METHOD("setup", "provider", "refresher"), &LagoonicUsagePanel::setup);
}

void LagoonicUsagePanel::setup(const Callable &p_provider, const Callable &p_refresher) {
	provider = p_provider;
	refresher = p_refresher;
	_on_refresh_pressed();
}

void LagoonicUsagePanel::_on_refresh_pressed() {
	last_refresh_msec = OS::get_singleton()->get_ticks_msec();
	if (refresher.is_valid()) {
		refresher.call();
	}
	_refresh();
}

static String _usage_resets_text(const Dictionary &p_window) {
	const int64_t at = p_window.get("resets_at", 0);
	if (at > 0) {
		const int64_t left = at - int64_t(Time::get_singleton()->get_unix_time_from_system());
		if (left <= 0) {
			return String::utf8("Se reinicia ya");
		}
		if (left < 3600) {
			return vformat(String::utf8("Se reinicia en %d min"), int(left / 60) + 1);
		}
		if (left < 86400) {
			return vformat(String::utf8("Se reinicia en %d h %d min"), int(left / 3600), int((left % 3600) / 60));
		}
		return vformat(String::utf8("Se reinicia en %d d %d h"), int(left / 86400), int((left % 86400) / 3600));
	}
	const String text = p_window.get("resets_text", "");
	return text.is_empty() ? String() : String::utf8("Se reinicia ") + text;
}

void LagoonicUsagePanel::_refresh() {
	if (!provider.is_valid() || !is_visible_in_tree()) {
		return;
	}
	if (OS::get_singleton()->get_ticks_msec() - last_refresh_msec > USAGE_REFRESH_MSEC) {
		_on_refresh_pressed();
		return;
	}
	const Variant got = provider.call();
	if (got.get_type() != Variant::DICTIONARY) {
		return;
	}
	const Dictionary data = got;
	const bool reading = data.get("reading", false);
	refresh_button->set_disabled(reading);
	refresh_button->set_button_icon(get_editor_theme_icon(SNAME("Reload")));
	const Dictionary usage = data.get("usage", Dictionary());
	// Los textos de reinicio cambian con el tiempo: se repinta cada minuto.
	const String sig = JSON::stringify(usage) + itos(int(Time::get_singleton()->get_unix_time_from_system() / 60));
	if (sig != signature) {
		signature = sig;
		_rebuild(usage);
	}
}

static Color _usage_color(double p_remaining, const LagoonicVibeTheme::Palette &p) {
	return p_remaining <= 10 ? Color(0.95, 0.45, 0.45) : (p_remaining <= 30 ? Color(0.95, 0.7, 0.35) : p.accent);
}

LagoonicUsageRings::LagoonicUsageRings() {
	set_custom_minimum_size(Size2(120, 120) * EDSCALE);
	set_mouse_filter(MOUSE_FILTER_PASS);
}

void LagoonicUsageRings::set_remaining(int p_kind, double p_remaining) {
	if (p_kind >= 0 && p_kind < 3) {
		remaining[p_kind] = p_remaining < 0 ? -1 : CLAMP(p_remaining, 0.0, 100.0);
		queue_redraw();
	}
}

void LagoonicUsageRings::_notification(int p_what) {
	if (p_what != NOTIFICATION_DRAW) {
		return;
	}
	const LagoonicVibeTheme::Palette p = LagoonicVibeTheme::current();
	const Vector2 size = get_size();
	const Vector2 center = size * 0.5f;
	const float span = MIN(size.x, size.y);
	// Exterior ancho (sesion), luego semanal y mensual, mas finos.
	const float widths[3] = { span * 0.12f, span * 0.075f, span * 0.055f };
	const float gap = span * 0.035f;
	float outer = span * 0.5f - 1.0f;
	for (int k = 0; k < 3; k++) {
		const float radius = outer - widths[k] * 0.5f;
		if (remaining[k] >= 0) {
			draw_arc(center, radius, 0, Math::TAU, 96, Color(p.text, 0.09f), widths[k], true);
			const float start = -Math::PI * 0.5f;
			const float sweep = float(Math::TAU * remaining[k] / 100.0);
			if (sweep > 0.01f) {
				draw_arc(center, radius, start, start + sweep, MAX(8, int(96 * remaining[k] / 100.0)), _usage_color(remaining[k], p), widths[k], true);
			}
		}
		outer -= widths[k] + gap;
	}
	// En el centro, lo que queda de la sesion (o de lo primero que haya);
	// sin datos, un anillo tenue y una raya.
	double shown = -1;
	for (int k = 0; k < 3 && shown < 0; k++) {
		shown = remaining[k];
	}
	if (shown < 0) {
		draw_arc(center, span * 0.5f - 1.0f - widths[0] * 0.5f, 0, Math::TAU, 96, Color(p.text, 0.06f), widths[0], true);
	}
	{
		const Ref<Font> font = get_theme_font(SceneStringName(font), SNAME("Label"));
		const int font_size = MAX(10, int(span * 0.18f));
		const String text = shown < 0 ? String::utf8("—") : itos(int(Math::round(shown))) + "%";
		const Vector2 text_size = font->get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size);
		draw_string(font, center + Vector2(-text_size.x * 0.5f, font_size * 0.35f), text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, shown < 0 ? p.text_muted : p.text);
	}
}

void LagoonicUsagePanel::_rebuild(const Dictionary &p_usage) {
	for (int i = list->get_child_count() - 1; i >= 0; i--) {
		Node *c = list->get_child(i);
		list->remove_child(c);
		c->queue_free();
	}
	const LagoonicVibeTheme::Palette p = LagoonicVibeTheme::current();
	if (p_usage.is_empty()) {
		summary->set_text(String::utf8("Leyendo…"));
		return;
	}
	summary->set_text(String::utf8("Anillo exterior: sesión · interior: semana · más adentro: mes"));
	// Los proveedores de un solo juego de anillos van juntos en una fila; los
	// que tienen varios grupos (Antigravity) ocupan la suya.
	Array ids = p_usage.keys();
	Array compact_ids;
	Array wide_ids;
	for (int i = 0; i < ids.size(); i++) {
		const Array windows = Dictionary(p_usage[ids[i]]).get("windows", Array());
		Vector<String> groups;
		for (int w = 0; w < windows.size(); w++) {
			const String g = Dictionary(windows[w]).get("group", "");
			if (!groups.has(g)) {
				groups.push_back(g);
			}
		}
		(groups.size() > 1 ? wide_ids : compact_ids).push_back(ids[i]);
	}
	ids = compact_ids.duplicate(); // Array is shared by reference: copy before appending.
	ids.append_array(wide_ids);
	HFlowContainer *compact_row = nullptr;
	for (int i = 0; i < ids.size(); i++) {
		const String id = ids[i];
		const Dictionary entry = p_usage[id];
		const String name = entry.get("name", id);
		const bool compact = compact_ids.has(id);
		if (i > 0 && !compact) {
			ColorRect *rule = memnew(ColorRect);
			rule->set_color(Color(p.text, 0.06f));
			rule->set_custom_minimum_size(Size2(0, MAX(1, int(EDSCALE))));
			list->add_child(rule);
		}
		// Bloque del proveedor: en la fila compartida, o en el panel entero.
		VBoxContainer *block_box = memnew(VBoxContainer);
		block_box->add_theme_constant_override(SNAME("separation"), int(8 * EDSCALE));
		if (compact) {
			if (compact_row == nullptr) {
				compact_row = memnew(HFlowContainer);
				compact_row->add_theme_constant_override(SNAME("h_separation"), int(28 * EDSCALE));
				compact_row->add_theme_constant_override(SNAME("v_separation"), int(14 * EDSCALE));
				list->add_child(compact_row);
			}
			block_box->set_custom_minimum_size(Size2(130 * EDSCALE, 0));
			compact_row->add_child(block_box);
		} else {
			list->add_child(block_box);
		}
		HBoxContainer *head = memnew(HBoxContainer);
		head->add_theme_constant_override(SNAME("separation"), int(6 * EDSCALE));
		block_box->add_child(head);
		head->add_child(_team_badge(id, name, 14));
		Label *title = memnew(Label);
		title->set_text(name);
		title->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		head->add_child(title);
		// Sin el nombre del plan: solo cuenta lo que queda (va en el tooltip).
		const String plan = entry.get("plan", "");
		if (!plan.is_empty()) {
			title->set_tooltip_text(name + String::utf8(" · plan ") + plan);
			title->set_mouse_filter(Control::MOUSE_FILTER_PASS);
		}

		HFlowContainer *rings_row = memnew(HFlowContainer);
		rings_row->add_theme_constant_override(SNAME("h_separation"), int(18 * EDSCALE));
		rings_row->add_theme_constant_override(SNAME("v_separation"), int(10 * EDSCALE));
		block_box->add_child(rings_row);

		// Un juego de anillos por grupo de modelos (Antigravity tiene varios).
		const Array windows = entry.get("windows", Array());
		Vector<String> groups;
		for (int w = 0; w < windows.size(); w++) {
			const String g = Dictionary(windows[w]).get("group", "");
			if (!groups.has(g)) {
				groups.push_back(g);
			}
		}
		if (groups.is_empty()) {
			// Sin datos: un anillo vacio; el motivo, al pasar el raton.
			LagoonicUsageRings *rings = memnew(LagoonicUsageRings);
			rings->set_custom_minimum_size(Size2(84, 84) * EDSCALE);
			rings->set_tooltip_text(entry.get("note", String::utf8("Este agente no informa su uso.")));
			rings_row->add_child(rings);
			continue;
		}
		for (const String &group : groups) {
			VBoxContainer *block = memnew(VBoxContainer);
			block->add_theme_constant_override(SNAME("separation"), int(2 * EDSCALE));
			rings_row->add_child(block);
			LagoonicUsageRings *rings = memnew(LagoonicUsageRings);
			rings->set_custom_minimum_size(Size2(84, 84) * EDSCALE);
			rings->set_h_size_flags(Control::SIZE_SHRINK_CENTER);
			block->add_child(rings);
			if (!group.is_empty()) {
				Label *group_label = memnew(Label);
				group_label->set_text(group);
				group_label->set_theme_type_variation(LagoonicVibeTheme::MUTED_LABEL);
				group_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
				group_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
				group_label->set_custom_minimum_size(Size2(96 * EDSCALE, 0));
				block->add_child(group_label);
			}
			String tooltip = group.is_empty() ? name : group;
			static const char *kind_names[3] = { "Sesión", "Semana", "Mes" };
			for (int w = 0; w < windows.size(); w++) {
				const Dictionary window = windows[w];
				if (String(window.get("group", "")) != group) {
					continue;
				}
				const String kind = window.get("kind", "");
				const int kk = kind == "weekly" ? 1 : (kind == "monthly" ? 2 : 0);
				const double left = 100.0 - CLAMP(double(window.get("used_percent", 0)), 0.0, 100.0);
				rings->set_remaining(kk, left);
				const String resets = _usage_resets_text(window);
				tooltip += "\n" + String::utf8(kind_names[kk]) + ": " + itos(int(Math::round(left))) + String::utf8(" % libre") + (resets.is_empty() ? String() : String::utf8(" · ") + resets.to_lower());
			}
			rings->set_tooltip_text(tooltip);
		}
	}
}

LagoonicUsagePanel::LagoonicUsagePanel() {
	add_theme_constant_override(SNAME("separation"), int(10 * EDSCALE));
	HBoxContainer *top = memnew(HBoxContainer);
	add_child(top);
	summary = _bento_muted(String());
	summary->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	top->add_child(summary);
	refresh_button = memnew(Button);
	refresh_button->set_flat(true);
	refresh_button->set_focus_mode(Control::FOCUS_NONE);
	refresh_button->set_tooltip_text(String::utf8("Volver a preguntar a cada agente cuánto le queda."));
	refresh_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicUsagePanel::_on_refresh_pressed));
	top->add_child(refresh_button);
	list = memnew(VBoxContainer);
	list->add_theme_constant_override(SNAME("separation"), int(10 * EDSCALE));
	add_child(_bento_scroll(list));
	timer = memnew(Timer);
	timer->set_wait_time(1.0);
	timer->set_autostart(true);
	timer->connect("timeout", callable_mp(this, &LagoonicUsagePanel::_refresh));
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
