/**************************************************************************/
/*  lagoonic_vibe_workspace.cpp                                           */
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

#include "lagoonic_vibe_workspace.h"

#include "lagoonic_agent_dock.h"
#include "lagoonic_agent_settings.h"
#include "lagoonic_model_catalog.h"
#include "lagoonic_vibe_theme.h"
#include "lagoonic_water_caustics.h"
#include "lagoonic_lily_veil.h"
#include "scene/animation/tween.h"
#include "editor/file_system/editor_file_system.h"
#include "scene/resources/material.h"
#include "scene/resources/shader.h"
#include "scene/resources/style_box.h"
#include "scene/resources/style_box_flat.h"
#include "lagoonic_chat_view.h"
#include "lagoonic_water_caustics.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/gui/editor_title_bar.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/margin_container.h"
#include "scene/main/scene_tree.h"
#include "editor/settings/editor_settings.h"
#include "lagoonic_bento.h"
#include "lagoonic_bento_panels.h"
#include "scene/gui/popup_menu.h"
#include "servers/display/display_server.h"

static const char *LAGOONIC_INITIAL_PROMPT_RES_PATH = "res://.lagoonic/initial_prompt.txt";

void LagoonicVibeWorkspace::_ensure_runtime_host() {
	if (runtime_host.is_valid()) {
		return;
	}
	runtime_host.instantiate();
	runtime_host->set_opencode_bin(LagoonicModelCatalog::opencode_binary());
	runtime_host->connect(SNAME("runtime_event"), callable_mp(this, &LagoonicVibeWorkspace::_on_runtime_event));
	runtime_host->connect(SNAME("runtime_status"), callable_mp(this, &LagoonicVibeWorkspace::_on_runtime_status));
}

void LagoonicVibeWorkspace::_ensure_return_pill() {
	if (return_pill_container != nullptr) {
		return;
	}
	EditorNode *en = EditorNode::get_singleton();
	if (en == nullptr) {
		return;
	}

	// Va dentro de la barra superior del editor (junto a Jugar/Detener de
	// Godot), con el mismo tamano que sus botones: nada flotando encima.
	Control *host = EditorNode::get_title_bar();
	const bool in_title_bar = host != nullptr;
	if (!in_title_bar) {
		host = en->get_gui_base();
	}

	return_pill_container = memnew(PanelContainer);
	return_pill_container->set_name("LagoonicReturnToVibe");
	return_pill_container->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	return_pill_container->add_theme_style_override(SceneStringName(panel), memnew(StyleBoxEmpty));

	return_pill_style.instantiate();
	return_pill_style->set_corner_radius_all(6 * EDSCALE);
	return_pill_style->set_content_margin_individual(10 * EDSCALE, 4 * EDSCALE, 12 * EDSCALE, 4 * EDSCALE);
	return_pill_hover_style = return_pill_style->duplicate();

	return_pill_button = memnew(Button);
	return_pill_button->set_text("Vibe Coding");
	return_pill_button->set_tooltip_text(TTRC(String::utf8("Oculta el editor clásico y vuelve al espacio Vibe Coding de Lagoonic.")));
	return_pill_button->set_focus_mode(Control::FOCUS_NONE);
	return_pill_button->add_theme_constant_override(SNAME("icon_max_width"), 16 * EDSCALE);
	return_pill_button->add_theme_constant_override(SNAME("h_separation"), 6 * EDSCALE);
	return_pill_button->add_theme_style_override(SNAME("normal"), return_pill_style);
	return_pill_button->add_theme_style_override(SceneStringName(hover), return_pill_hover_style);
	return_pill_button->add_theme_style_override(SceneStringName(pressed), return_pill_hover_style);
	return_pill_button->add_theme_style_override(SNAME("focus"), memnew(StyleBoxEmpty));
	return_pill_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicVibeWorkspace::_on_return_to_vibe_pressed));
	return_pill_container->add_child(return_pill_button);

	if (in_title_bar) {
		Control *gap = memnew(Control);
		gap->set_custom_minimum_size(Size2(8 * EDSCALE, 0));
		gap->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		return_pill_container->set_meta("lagoonic_gap", gap);
		host->add_child(gap);
		host->add_child(return_pill_container);
	} else {
		host->add_child(return_pill_container);
		return_pill_container->set_anchors_and_offsets_preset(Control::PRESET_TOP_RIGHT);
		return_pill_container->set_offset(SIDE_LEFT, -170 * EDSCALE);
		return_pill_container->set_offset(SIDE_TOP, 6 * EDSCALE);
		return_pill_container->set_offset(SIDE_RIGHT, -12 * EDSCALE);
	}
	return_pill_container->set_visible(!vibe_mode_active);
}

void LagoonicVibeWorkspace::_refresh_project_title() {
	if (project_title_label == nullptr) {
		return;
	}
	String project_name;
	if (ProjectSettings::get_singleton() != nullptr) {
		project_name = String(GLOBAL_GET("application/config/name")).strip_edges();
	}
	if (project_name.is_empty()) {
		project_name = TTRC("Proyecto Lagoonic");
	}
	project_title_label->set_text(project_name);
}

void LagoonicVibeWorkspace::_refresh_icons() {
	if (agent_dock != nullptr) {
		agent_dock->refresh_icons();
	}
	if (add_panel_button != nullptr && is_inside_tree()) {
		add_panel_button->set_button_icon(get_editor_theme_icon(SNAME("Add")));
	}
}

// ---------------------------------------------------------------- Bento

struct LagoonicBentoType {
	const char *id;
	const char *title;
	const char *icon;
	const char *hint;
};

static const LagoonicBentoType LAGOONIC_BENTOS[] = {
	{ "plan", "Plan", "CheckBox", "Los pasos que sigue el agente" },
	{ "changes", "Cambios", "Edit", "Archivos que ha tocado este chat" },
	{ "files", "Archivos", "Folder", "Explora tu proyecto y menciona archivos" },
	{ "context", "Contexto", "Performance", "Modelo, mensajes y tiempo de este chat" },
};

void LagoonicVibeWorkspace::_update_chat_width() {
	if (chat_center == nullptr) {
		return;
	}
	const float width = chat_center->get_size().x;
	const int pad = int(MAX(18.0f * EDSCALE, (width - CENTER_COLUMN_MAX_WIDTH * EDSCALE) * 0.5f));
	chat_center->add_theme_constant_override(SNAME("margin_left"), pad);
	chat_center->add_theme_constant_override(SNAME("margin_right"), pad);
	chat_center->add_theme_constant_override(SNAME("margin_top"), int(10 * EDSCALE));
	chat_center->add_theme_constant_override(SNAME("margin_bottom"), int(16 * EDSCALE));
}

void LagoonicVibeWorkspace::_update_title_margins() {
	if (top_bar_margin == nullptr || DisplayServer::get_singleton() == nullptr || get_window() == nullptr) {
		return;
	}
	// Con la barra de titulo integrada (macOS), deja sitio a los semaforos.
	const Vector3i safe = DisplayServer::get_singleton()->window_get_safe_title_margins(get_window()->get_window_id());
	top_bar_margin->add_theme_constant_override(SNAME("margin_left"), safe.x > 0 ? safe.x + 8 : 16);
	top_bar_margin->add_theme_constant_override(SNAME("margin_right"), safe.y > 0 ? safe.y + 8 : 16);
}

void LagoonicVibeWorkspace::_on_add_panel_pressed() {
	add_panel_menu->clear();
	for (const LagoonicBentoType &t : LAGOONIC_BENTOS) {
		if (bento_board->get_side(t.id) != nullptr) {
			continue;
		}
		add_panel_menu->add_icon_item(get_editor_theme_icon(StringName(t.icon)), String::utf8(t.title));
		const int idx = add_panel_menu->get_item_count() - 1;
		add_panel_menu->set_item_metadata(idx, String(t.id));
		add_panel_menu->set_item_tooltip(idx, String::utf8(t.hint));
	}
	if (add_panel_menu->get_item_count() == 0) {
		add_panel_menu->add_item(String::utf8("Ya están todos abiertos"));
		add_panel_menu->set_item_disabled(0, true);
	}
	const Rect2 r = add_panel_button->get_screen_rect();
	add_panel_menu->set_position(Point2i(r.position.x, r.position.y + r.size.y + 4));
	add_panel_menu->reset_size();
	add_panel_menu->popup();
}

void LagoonicVibeWorkspace::_on_add_panel_selected(int p_index) {
	const Variant meta = add_panel_menu->get_item_metadata(p_index);
	if (meta.get_type() == Variant::STRING) {
		_open_bento(meta);
	}
}

Array LagoonicVibeWorkspace::_get_changes() {
	_ensure_runtime_host();
	return runtime_host.is_valid() ? runtime_host->get_changes() : Array();
}

void LagoonicVibeWorkspace::_open_bento(const String &p_id, bool p_save) {
	if (bento_board == nullptr || agent_dock == nullptr || bento_board->get_side(p_id) != nullptr) {
		return;
	}
	const LagoonicBentoType *type = nullptr;
	for (const LagoonicBentoType &t : LAGOONIC_BENTOS) {
		if (p_id == t.id) {
			type = &t;
		}
	}
	if (type == nullptr) {
		return;
	}
	LagoonicChatView *chat = agent_dock->get_chat_view();
	LagoonicBento *bento = memnew(LagoonicBento);
	bento->set_bento_id(p_id);
	bento->set_title(String::utf8(type->title));
	bento->set_icon(StringName(type->icon));
	bento->set_close_callback(callable_mp(this, &LagoonicVibeWorkspace::_close_bento));
	bento->set_theme(agent_dock->get_theme());
	if (p_id == "plan") {
		LagoonicPlanPanel *panel = memnew(LagoonicPlanPanel);
		panel->setup(chat);
		bento->set_content(panel);
		chat->set_plan_card_suppressed(true);
	} else if (p_id == "changes") {
		LagoonicChangesPanel *panel = memnew(LagoonicChangesPanel);
		panel->setup(chat, callable_mp(this, &LagoonicVibeWorkspace::_get_changes));
		bento->set_content(panel);
	} else if (p_id == "files") {
		LagoonicFilesPanel *panel = memnew(LagoonicFilesPanel);
		panel->setup(chat);
		bento->set_content(panel);
	} else if (p_id == "context") {
		LagoonicContextPanel *panel = memnew(LagoonicContextPanel);
		panel->setup(chat);
		bento->set_content(panel);
	}
	bento_board->add_side(bento);
	bento->apply_palette();
	if (p_save) {
		_save_bentos();
	}
}

void LagoonicVibeWorkspace::_close_bento(const String &p_id) {
	if (bento_board == nullptr) {
		return;
	}
	bento_board->remove_side(p_id);
	if (p_id == "plan" && agent_dock != nullptr) {
		agent_dock->get_chat_view()->set_plan_card_suppressed(false);
	}
	_save_bentos();
}

void LagoonicVibeWorkspace::_save_bentos() {
	if (EditorSettings::get_singleton() != nullptr && bento_board != nullptr) {
		EditorSettings::get_singleton()->set_project_metadata("lagoonic", "bentos", bento_board->get_side_ids());
		EditorSettings::get_singleton()->save_project_metadata(); // Al momento, no solo al cerrar.
	}
}

void LagoonicVibeWorkspace::_restore_bentos() {
	if (EditorSettings::get_singleton() == nullptr) {
		return;
	}
	const PackedStringArray ids = EditorSettings::get_singleton()->get_project_metadata("lagoonic", "bentos", PackedStringArray());
	for (const String &id : ids) {
		_open_bento(id, false);
	}
}

void LagoonicVibeWorkspace::_consume_initial_prompt() {
	if (initial_prompt_checked) {
		return;
	}
	initial_prompt_checked = true;

	if (!FileAccess::exists(LAGOONIC_INITIAL_PROMPT_RES_PATH)) {
		return;
	}

	Error err = OK;
	const String raw_prompt = FileAccess::get_file_as_string(LAGOONIC_INITIAL_PROMPT_RES_PATH, &err);
	const String abs_path = (ProjectSettings::get_singleton() != nullptr)
			? ProjectSettings::get_singleton()->globalize_path(LAGOONIC_INITIAL_PROMPT_RES_PATH)
			: LAGOONIC_INITIAL_PROMPT_RES_PATH;
	DirAccess::remove_absolute(abs_path);

	const String prompt = raw_prompt.strip_edges();
	if (err != OK || prompt.is_empty()) {
		return;
	}

	send_initial_prompt(prompt);
}

void LagoonicVibeWorkspace::send_initial_prompt(const String &p_prompt) {
	const String prompt = p_prompt.strip_edges();
	if (prompt.is_empty()) {
		return;
	}
	_ensure_runtime_host();
	LagoonicChatView *chat = (agent_dock != nullptr) ? agent_dock->get_chat_view() : nullptr;
	if (chat != nullptr) {
		chat->add_user_message(prompt);
		chat->clear_file_guard();
		chat->clear_error();
		chat->set_busy(true);
	}
	_on_agent_event(SNAME("send_requested"), prompt, Variant());
}

void LagoonicVibeWorkspace::_on_projects_pressed() {
	if (OS::get_singleton() != nullptr) {
		List<String> args;
		args.push_back("--project-manager");
		OS::get_singleton()->create_instance(args);
	}
	if (EditorNode::get_singleton() != nullptr && EditorNode::get_singleton()->get_tree() != nullptr) {
		EditorNode::get_singleton()->get_tree()->quit();
	} else if (get_tree() != nullptr) {
		get_tree()->quit();
	}
}

void LagoonicVibeWorkspace::_on_play_pressed() {
	EditorInterface *ei = EditorInterface::get_singleton();
	if (ei == nullptr) {
		return;
	}
	// Un solo boton: Jugar o Detener segun el estado.
	if (ei->is_playing_scene()) {
		ei->stop_playing_scene();
	} else {
		ei->play_main_scene();
	}
}

void LagoonicVibeWorkspace::_apply_palette() {
	const LagoonicVibeTheme::Palette p = LagoonicVibeTheme::current();
	if (background_style.is_valid()) {
		// Fondo un poco mas oscuro que las tarjetas Bento para que destaquen.
		background_style->set_bg_color(p.light ? p.bg.darkened(0.06f) : p.bg.lerp(Color(0, 0, 0), 0.38f));
	}
	if (chat_title_label != nullptr) {
		chat_title_label->add_theme_color_override(SceneStringName(font_color), p.text);
		chat_title_label->add_theme_color_override(SNAME("font_shadow_color"), Color(p.bg, 0.8f));
	}
	if (play_button_style.is_valid()) {
		play_button_style->set_bg_color(p.accent);
		play_button_hover_style->set_bg_color(p.accent.lightened(0.18f));
	}
	if (ghost_button_hover_style.is_valid()) {
		ghost_button_hover_style->set_bg_color(Color(p.text, 0.08f));
	}
	if (return_pill_style.is_valid()) {
		return_pill_style->set_bg_color(Color(p.accent, 0.16f));
		return_pill_hover_style->set_bg_color(Color(p.accent, 0.30f));
	}
	if (return_pill_button != nullptr) {
		const Color fg = p.light ? p.accent.darkened(0.25f) : p.accent.lerp(Color(1, 1, 1), 0.35f);
		for (const char *n : { "font_color", "font_hover_color", "font_pressed_color", "icon_normal_color", "icon_hover_color", "icon_pressed_color" }) {
			return_pill_button->add_theme_color_override(n, fg);
		}
		if (is_inside_tree()) {
			return_pill_button->set_button_icon(get_editor_theme_icon(SNAME("Godot")));
		}
	}
	LagoonicVibeTheme::apply_to_editor();
	if (play_button != nullptr) {
		for (const char *n : { "font_color", "font_hover_color", "font_pressed_color", "font_focus_color" }) {
			play_button->add_theme_color_override(n, p.on_accent);
		}
	}
	for (Button *b : { projects_button, open_editor_button }) {
		if (b != nullptr) {
			b->add_theme_color_override(SNAME("font_color"), p.text_muted.lerp(p.text, 0.5f));
			b->add_theme_color_override(SNAME("font_hover_color"), p.text);
		}
	}
	if (project_title_label != nullptr) {
		project_title_label->add_theme_color_override(SNAME("font_color"), p.text);
		project_title_label->add_theme_color_override(SNAME("font_shadow_color"), Color(p.bg, 0.75f));
	}
	if (water_caustics != nullptr) {
		water_caustics->set_tint(Color(p.accent, 0.85f));
	}
	if (bento_board != nullptr) {
		bento_board->apply_palette();
	}
	if (agent_dock != nullptr && agent_dock->get_chat_view() != nullptr) {
		agent_dock->get_chat_view()->set_fade_color(LagoonicBento::card_color());
	}
	if (add_panel_button != nullptr) {
		add_panel_button->add_theme_color_override(SNAME("font_color"), p.text_muted.lerp(p.text, 0.5f));
		add_panel_button->add_theme_color_override(SNAME("font_hover_color"), p.text);
		for (const char *n : { "icon_normal_color", "icon_hover_color", "icon_pressed_color" }) {
			add_panel_button->add_theme_color_override(n, p.text_muted.lerp(p.text, 0.5f));
		}
	}
	if (agent_dock != nullptr) {
		}
}

void LagoonicVibeWorkspace::_sync_play_button() {
	EditorInterface *ei = EditorInterface::get_singleton();
	const bool playing = ei != nullptr && ei->is_playing_scene();
	if (playing == was_playing || play_button == nullptr) {
		return;
	}
	was_playing = playing;
	play_button->set_text(playing ? String::utf8("■  Detener") : String::utf8("▶  Jugar"));
}

void LagoonicVibeWorkspace::_on_stop_pressed() {
	if (EditorInterface::get_singleton() != nullptr) {
		EditorInterface::get_singleton()->stop_playing_scene();
	}
}

void LagoonicVibeWorkspace::_on_open_editor_pressed() {
	open_classic_editor();
}

void LagoonicVibeWorkspace::_on_return_to_vibe_pressed() {
	return_to_vibe_coding();
}

void LagoonicVibeWorkspace::set_vibe_mode(bool p_enabled) {
	vibe_mode_active = p_enabled;
	set_visible(vibe_mode_active);
	if (vibe_mode_active) {
		move_to_front();
	}
	_ensure_return_pill();
	if (return_pill_container != nullptr) {
		return_pill_container->set_visible(!vibe_mode_active);
		if (!vibe_mode_active) {
			return_pill_container->move_to_front();
		}
	}
	emit_signal(SNAME("vibe_mode_changed"), vibe_mode_active);
}

// Marea: mascara con borde de ola sobre todo el workspace (clip_children usa
// el alfa del panel para recortar a los hijos). Visible = por debajo del borde.
static const char *LAGOONIC_TIDE_SHADER = R"godot_shader(
shader_type canvas_item;
uniform float edge_px = 0.0;
uniform float amp = 14.0;
uniform vec4 crest_color = vec4(0.16, 0.78, 0.91, 0.8);
varying vec2 local_pos;
void vertex() {
	local_pos = VERTEX;
}
void fragment() {
	float wave = sin(local_pos.x * 0.011 + TIME * 2.2) * amp * 0.6 + sin(local_pos.x * 0.027 - TIME * 1.4) * amp * 0.4;
	float e = edge_px + wave;
	float visible = smoothstep(e - 1.5, e + 1.5, local_pos.y);
	float crest = exp(-pow((local_pos.y - e) / 3.0, 2.0)) * crest_color.a;
	COLOR.rgb = mix(COLOR.rgb, crest_color.rgb, crest);
	COLOR.a *= max(visible, crest);
}
)godot_shader";

float LagoonicVibeWorkspace::_tide_overshoot() const {
	return 30.0f * EDSCALE; // Amplitud de la ola + margen: el borde sale entero.
}

void LagoonicVibeWorkspace::_begin_tide() {
	if (tide_material.is_null()) {
		Ref<Shader> shader;
		shader.instantiate();
		shader->set_code(String::utf8(LAGOONIC_TIDE_SHADER));
		tide_material.instantiate();
		tide_material->set_shader(shader);
	}
	const Color acc = LagoonicVibeTheme::accent();
	tide_material->set_shader_parameter("crest_color", Vector4(acc.r, acc.g, acc.b, 0.8f));
	tide_material->set_shader_parameter("amp", 14.0f * EDSCALE);
	set_material(tide_material);
	set_clip_children_mode(CanvasItem::CLIP_CHILDREN_AND_DRAW);
}

void LagoonicVibeWorkspace::_end_tide() {
	set_material(Ref<Material>());
	set_clip_children_mode(CanvasItem::CLIP_CHILDREN_DISABLED);
}

void LagoonicVibeWorkspace::_set_tide(float p_edge_px) {
	if (tide_material.is_valid()) {
		tide_material->set_shader_parameter("edge_px", p_edge_px);
	}
}

void LagoonicVibeWorkspace::open_classic_editor() {
	if (transitioning || !vibe_mode_active) {
		return;
	}
	if (!LagoonicVibeTheme::animations_enabled() || !is_visible_in_tree()) {
		set_vibe_mode(false);
		return;
	}
	// Chat -> editor: primero se va el contenido, despues la marea baja de
	// arriba abajo y descubre el editor.
	transitioning = true;
	const float h = get_size().y;
	const float over = _tide_overshoot();
	_begin_tide();
	_set_tide(-over);
	Ref<Tween> tween = create_tween();
	if (content_root != nullptr) {
		tween->tween_property(content_root, NodePath("modulate:a"), 0.0f, 0.15)->set_trans(Tween::TRANS_SINE)->set_ease(Tween::EASE_IN);
	}
	tween->tween_method(callable_mp(this, &LagoonicVibeWorkspace::_set_tide), -over, h + over, 0.45)->set_trans(Tween::TRANS_SINE)->set_ease(Tween::EASE_IN_OUT);
	tween->tween_callback(callable_mp(this, &LagoonicVibeWorkspace::_finish_hide));
}

void LagoonicVibeWorkspace::_finish_hide() {
	set_vibe_mode(false);
	_end_tide();
	if (content_root != nullptr) {
		content_root->set_modulate(Color(1, 1, 1, 1));
	}
	transitioning = false;
	callable_mp(this, &LagoonicVibeWorkspace::_pulse_return_button).call_deferred();
}

void LagoonicVibeWorkspace::return_to_vibe_coding() {
	if (transitioning || vibe_mode_active) {
		return;
	}
	if (!LagoonicVibeTheme::animations_enabled()) {
		set_vibe_mode(true);
		return;
	}
	// Editor -> chat: la marea sube desde abajo, tapa el editor y despues
	// aparece el contenido.
	transitioning = true;
	const float h = get_size().y;
	const float over = _tide_overshoot();
	_begin_tide();
	_set_tide(h + over);
	if (content_root != nullptr) {
		content_root->set_modulate(Color(1, 1, 1, 0));
	}
	set_vibe_mode(true);
	Ref<Tween> tween = create_tween();
	tween->tween_method(callable_mp(this, &LagoonicVibeWorkspace::_set_tide), h + over, -over, 0.45)->set_trans(Tween::TRANS_SINE)->set_ease(Tween::EASE_IN_OUT);
	tween->tween_callback(callable_mp(this, &LagoonicVibeWorkspace::_end_tide));
	tween->tween_interval(0.08);
	if (content_root != nullptr) {
		tween->tween_property(content_root, NodePath("modulate:a"), 1.0f, 0.3)->set_trans(Tween::TRANS_CUBIC)->set_ease(Tween::EASE_OUT);
	}
	tween->tween_callback(callable_mp(this, &LagoonicVibeWorkspace::_finish_show));
}

void LagoonicVibeWorkspace::_finish_show() {
	transitioning = false;
}

void LagoonicVibeWorkspace::_pulse_return_button() {
	if (return_pill_button == nullptr || !return_pill_button->is_visible_in_tree()) {
		return;
	}
	// Un latido suave para que se vea a donde fue el chat.
	return_pill_button->set_pivot_offset(return_pill_button->get_size() * 0.5f);
	Ref<Tween> tween = return_pill_button->create_tween();
	tween->tween_property(return_pill_button, NodePath("scale"), Vector2(1.12f, 1.12f), 0.14)->set_trans(Tween::TRANS_SINE)->set_ease(Tween::EASE_OUT);
	tween->tween_property(return_pill_button, NodePath("scale"), Vector2(1.0f, 1.0f), 0.26)->set_trans(Tween::TRANS_SINE)->set_ease(Tween::EASE_IN_OUT);
}

float LagoonicVibeWorkspace::_header_band_px() const {
	return (water_caustics != nullptr && water_caustics->get_size().y > 1.0f) ? water_caustics->get_size().y : (float)WATER_HEADER_HEIGHT;
}

void LagoonicVibeWorkspace::_play_intro() {
	if (intro_played || !is_inside_tree()) {
		return;
	}
	if (intro_veil == nullptr) {
		if (!LagoonicVibeTheme::animations_enabled() || !vibe_mode_active) {
			intro_played = true;
			return;
		}
		// Continua el splash: mismo agua y nenufar, ahora flotando mientras
		// el editor termina de cargar.
		float clock = 0.0f;
		float band = 0.0f;
		LagoonicVibeTheme::load_handoff(clock, band); // Continua la caustica del splash.
		intro_veil = memnew(LagoonicLilyVeil);
		add_child(intro_veil); // Ultimo hijo: encima de todo.
		intro_veil->cover_now(band > 1.0f ? band : _header_band_px(), clock);
		intro_veil->start_bobbing();
		if (content_root != nullptr) {
			content_root->set_modulate(Color(1, 1, 1, 0));
		}
	}
	// Espera a que el editor termine de arrancar (ventana visible, sin
	// escaneo/importacion en curso) y a un par de fotogramas estables.
	EditorFileSystem *efs = EditorFileSystem::get_singleton();
	const bool busy = !is_visible_in_tree() || (efs != nullptr && (efs->is_scanning() || efs->is_importing()));
	intro_wait_frames++;
	if ((busy || intro_wait_frames < 20) && intro_wait_frames < 900) {
		get_tree()->connect(SNAME("process_frame"), callable_mp(this, &LagoonicVibeWorkspace::_play_intro), CONNECT_ONE_SHOT);
		return;
	}
	intro_played = true;
	if (!vibe_mode_active) {
		intro_veil->queue_free();
		intro_veil = nullptr;
		if (content_root != nullptr) {
			content_root->set_modulate(Color(1, 1, 1, 1));
		}
		return;
	}
	intro_veil->reveal(content_root, _header_band_px());
	intro_veil = nullptr; // Se libera sola al terminar.
}

void LagoonicVibeWorkspace::_on_runtime_event(const Dictionary &p_event) {
	if (agent_dock != nullptr) {
		agent_dock->hook_push_agent_event(p_event);
	}
	// Al terminar un turno el servidor ya puso titulo al chat: refresca.
	if (String(p_event.get("type", "")) == "busy" && !bool(p_event.get("busy", true)) && runtime_host.is_valid() && agent_dock != nullptr && agent_dock->get_chat_view() != nullptr) {
		agent_dock->get_chat_view()->set_sessions(runtime_host->list_sessions());
	}
}

void LagoonicVibeWorkspace::_on_runtime_status(const String &p_status) {
	if (p_status.begins_with("session:") && agent_dock != nullptr && agent_dock->get_chat_view() != nullptr) {
		// El host creo o cambio la sesion (p. ej. al primer envio).
		agent_dock->get_chat_view()->set_current_session(p_status.substr(8));
	}
	(void)p_status;
	if (agent_dock != nullptr) {
		// Mantiene la entrada del chat habilitada para que el primer envío
		// o un reintento tras caída levante el servidor perezosamente.
		const bool ready = runtime_host.is_valid();
		agent_dock->hook_set_connection(ready);
	}
}

void LagoonicVibeWorkspace::_on_agent_event(const StringName &p_event, const Variant &p_a, const Variant &p_b) {
	_ensure_runtime_host();
	LagoonicChatView *chat = (agent_dock != nullptr) ? agent_dock->get_chat_view() : nullptr;
	const String cwd = (ProjectSettings::get_singleton() != nullptr)
			? ProjectSettings::get_singleton()->globalize_path("res://")
			: String("res://");

	if (p_event == SNAME("send_requested")) {
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
		runtime_host->set_crew_boss(boss);
		if (boss != "opencode") {
			// Claude Code, Codex... answer through their ACP adapter: no
			// OpenCode server needed for this turn.
			runtime_host->set_project_dir(cwd);
			if (runtime_host->send_prompt(session, String(p_a), mode, model) != OK && chat != nullptr) {
				chat->set_busy(false);
			}
		} else if (runtime_host->ensure_server(cwd, model, LagoonicAgentSettings::secret_env()) == OK) {
			if (!crew_applied) {
				// Configuracion > Equipo: necesita la carpeta del proyecto, que
				// el host conoce tras ensure_server().
				runtime_host->set_crew(boss, LagoonicAgentSettings::crew_workers());
				crew_applied = true;
			}
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
	}
}

void LagoonicVibeWorkspace::_notification(int p_notification) {
	switch (p_notification) {
		case NOTIFICATION_ENTER_TREE: {
			LagoonicVibeTheme::get_notifier()->connect(SNAME("changed"), callable_mp(this, &LagoonicVibeWorkspace::_apply_palette));
			set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
			_refresh_project_title();
			_refresh_icons();
		} break;

		case NOTIFICATION_EXIT_TREE: {
			LagoonicVibeTheme::get_notifier()->disconnect(SNAME("changed"), callable_mp(this, &LagoonicVibeWorkspace::_apply_palette));
		} break;

		case NOTIFICATION_INTERNAL_PROCESS: {
			_sync_play_button();
		} break;

		case NOTIFICATION_READY: {
			set_process_internal(true);
			set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
			_ensure_return_pill();
			_restore_bentos();
			_apply_palette();
			_update_title_margins();
			connect(SceneStringName(resized), callable_mp(this, &LagoonicVibeWorkspace::_update_title_margins));
			callable_mp(this, &LagoonicVibeWorkspace::_play_intro).call_deferred();
			call_deferred(SNAME("_consume_initial_prompt"));
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			static bool refreshing = false;
			if (refreshing) {
				break;
			}
			refreshing = true;
			_refresh_icons();
			refreshing = false;
		} break;
	}
}

void LagoonicVibeWorkspace::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_vibe_mode", "enabled"), &LagoonicVibeWorkspace::set_vibe_mode);
	ClassDB::bind_method(D_METHOD("is_vibe_mode"), &LagoonicVibeWorkspace::is_vibe_mode);
	ClassDB::bind_method(D_METHOD("open_classic_editor"), &LagoonicVibeWorkspace::open_classic_editor);
	ClassDB::bind_method(D_METHOD("return_to_vibe_coding"), &LagoonicVibeWorkspace::return_to_vibe_coding);
	ClassDB::bind_method(D_METHOD("send_initial_prompt", "prompt"), &LagoonicVibeWorkspace::send_initial_prompt);
	ClassDB::bind_method(D_METHOD("_consume_initial_prompt"), &LagoonicVibeWorkspace::_consume_initial_prompt);

	ADD_SIGNAL(MethodInfo("vibe_mode_changed", PropertyInfo(Variant::BOOL, "enabled")));
}

LagoonicVibeWorkspace::LagoonicVibeWorkspace() {
	singleton = this;
	set_name("LagoonicVibeWorkspace");
	set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	set_h_size_flags(Control::SIZE_EXPAND_FILL);
	set_v_size_flags(Control::SIZE_EXPAND_FILL);
	set_mouse_filter(Control::MOUSE_FILTER_STOP);
	set_z_index(50);

	// Fondo sólido opaco para cubrir completamente el editor clásico de Godot.
	background_style.instantiate();
	background_style->set_bg_color(Color(0.04f, 0.09f, 0.14f, 1.0f));
	background_style->set_content_margin_all(0);
	add_theme_style_override(SceneStringName(panel), background_style);

	header_button_style.instantiate();
	header_button_style->set_bg_color(Color(0.04f, 0.16f, 0.25f, 0.82f));
	header_button_style->set_border_color(Color(0.28f, 0.72f, 0.88f, 0.55f));
	header_button_style->set_border_width_all(1);
	header_button_style->set_corner_radius_all(8);
	header_button_style->set_content_margin_individual(12, 6, 12, 6);

	header_button_hover_style.instantiate();
	header_button_hover_style->set_bg_color(Color(0.08f, 0.25f, 0.38f, 0.92f));
	header_button_hover_style->set_border_color(Color(0.42f, 0.84f, 0.96f, 0.85f));
	header_button_hover_style->set_border_width_all(1);
	header_button_hover_style->set_corner_radius_all(8);
	header_button_hover_style->set_content_margin_individual(12, 6, 12, 6);

	play_button_style.instantiate();
	play_button_style->set_bg_color(Color(0.16f, 0.78f, 0.91f));
	play_button_style->set_corner_radius_all(18);
	play_button_style->set_content_margin_individual(18, 7, 18, 7);

	play_button_hover_style = play_button_style->duplicate();
	play_button_hover_style->set_bg_color(Color(0.36f, 0.88f, 0.97f));

	// Botones secundarios: sin borde, solo texto; fondo suave al pasar.
	ghost_button_style.instantiate();
	ghost_button_style->set_bg_color(Color(0, 0, 0, 0));
	ghost_button_style->set_corner_radius_all(18);
	ghost_button_style->set_content_margin_individual(14, 7, 14, 7);
	ghost_button_hover_style = ghost_button_style->duplicate();
	ghost_button_hover_style->set_bg_color(Color(1, 1, 1, 0.08f));

	VBoxContainer *root_vb = memnew(VBoxContainer);
	content_root = root_vb;
	root_vb->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	root_vb->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	root_vb->add_theme_constant_override(SNAME("separation"), 0);
	add_child(root_vb);

	// --- Barra superior fina (como la de OpenCode): Proyectos, titulo,
	// anadir panel, Jugar y Abrir editor. Se puede arrastrar la ventana.
	Control *header_band = memnew(Control);
	header_band->set_custom_minimum_size(Size2(0, TOP_BAR_HEIGHT * EDSCALE));
	header_band->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	root_vb->add_child(header_band);
	header_band->connect(SceneStringName(gui_input), callable_mp_static(&LagoonicWaterCaustics::handle_window_drag).bind(header_band));

	MarginContainer *header_margin = memnew(MarginContainer);
	top_bar_margin = header_margin;
	header_margin->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	header_margin->add_theme_constant_override(SNAME("margin_left"), 16);
	header_margin->add_theme_constant_override(SNAME("margin_right"), 16);
	header_margin->add_theme_constant_override(SNAME("margin_top"), 8);
	header_margin->add_theme_constant_override(SNAME("margin_bottom"), 6);
	header_band->add_child(header_margin);

	HBoxContainer *header_bar = memnew(HBoxContainer);
	header_bar->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	header_bar->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	header_bar->add_theme_constant_override(SNAME("separation"), 8);
	header_margin->add_child(header_bar);

	// Izquierda: "← Proyectos" + título del proyecto actual.
	projects_button = memnew(Button);
	projects_button->set_text(TTRC(String::utf8("← Proyectos")));
	projects_button->set_tooltip_text(TTRC("Vuelve al gestor de proyectos de Lagoonic."));
	projects_button->add_theme_style_override(SNAME("normal"), ghost_button_style);
	projects_button->add_theme_style_override(SceneStringName(hover), ghost_button_hover_style);
	projects_button->add_theme_style_override(SceneStringName(pressed), ghost_button_hover_style);
	projects_button->add_theme_style_override(SNAME("focus"), memnew(StyleBoxEmpty));
	projects_button->add_theme_color_override(SNAME("font_color"), Color(0.78f, 0.88f, 0.94f));
	projects_button->add_theme_color_override(SNAME("font_hover_color"), Color(1, 1, 1));
	projects_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicVibeWorkspace::_on_projects_pressed));
	header_bar->add_child(projects_button);

	project_title_label = memnew(Label);
	project_title_label->add_theme_font_size_override(SceneStringName(font_size), 17);
	project_title_label->add_theme_color_override(SNAME("font_color"), Color(0.96f, 0.99f, 1.0f));
	// Centrado sobre todo el ancho (capa encima de la barra), no entre botones.
	project_title_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	project_title_label->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	project_title_label->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	header_margin->add_child(project_title_label);
	_refresh_project_title();

	Control *header_spacer = memnew(Control);
	header_spacer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	header_spacer->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	header_bar->add_child(header_spacer);

	// Derecha: "+ Panel", "▶ Jugar", "Abrir editor".
	add_panel_button = memnew(Button);
	add_panel_button->set_text(TTRC("Panel"));
	add_panel_button->set_tooltip_text(String::utf8("Añade un panel junto al chat: plan, cambios, archivos o contexto."));
	add_panel_button->add_theme_style_override(SNAME("normal"), ghost_button_style);
	add_panel_button->add_theme_style_override(SceneStringName(hover), ghost_button_hover_style);
	add_panel_button->add_theme_style_override(SceneStringName(pressed), ghost_button_hover_style);
	add_panel_button->add_theme_style_override(SNAME("focus"), memnew(StyleBoxEmpty));
	add_panel_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicVibeWorkspace::_on_add_panel_pressed));
	header_bar->add_child(add_panel_button);

	add_panel_menu = memnew(PopupMenu);
	add_panel_menu->connect(SNAME("index_pressed"), callable_mp(this, &LagoonicVibeWorkspace::_on_add_panel_selected));
	add_child(add_panel_menu);

	play_button = memnew(Button);
	play_button->set_text(String::utf8("▶  Jugar"));
	play_button->set_tooltip_text(TTRC("Prueba el juego. Vuelve a pulsar para detenerlo."));
	play_button->add_theme_style_override(SNAME("normal"), play_button_style);
	play_button->add_theme_style_override(SceneStringName(hover), play_button_hover_style);
	play_button->add_theme_style_override(SceneStringName(pressed), play_button_hover_style);
	play_button->add_theme_style_override(SNAME("focus"), memnew(StyleBoxEmpty));
	for (const char *n : { "font_color", "font_hover_color", "font_pressed_color", "font_focus_color" }) {
		play_button->add_theme_color_override(n, Color(0.02f, 0.10f, 0.14f));
	}
	play_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicVibeWorkspace::_on_play_pressed));
	header_bar->add_child(play_button);

	open_editor_button = memnew(Button);
	open_editor_button->set_text(TTRC("Abrir editor"));
	open_editor_button->set_tooltip_text(TTRC(String::utf8("Muestra el editor clásico de Godot con una píldora flotante para volver a Vibe Coding.")));
	open_editor_button->add_theme_style_override(SNAME("normal"), ghost_button_style);
	open_editor_button->add_theme_style_override(SceneStringName(hover), ghost_button_hover_style);
	open_editor_button->add_theme_style_override(SceneStringName(pressed), ghost_button_hover_style);
	open_editor_button->add_theme_style_override(SNAME("focus"), memnew(StyleBoxEmpty));
	open_editor_button->add_theme_color_override(SNAME("font_color"), Color(0.78f, 0.88f, 0.94f));
	open_editor_button->add_theme_color_override(SNAME("font_hover_color"), Color(1, 1, 1));
	open_editor_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicVibeWorkspace::_on_open_editor_pressed));
	header_bar->add_child(open_editor_button);

	// --- Tablero Bento ---
	MarginContainer *body_margin = memnew(MarginContainer);
	body_margin->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	body_margin->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	body_margin->add_theme_constant_override(SNAME("margin_left"), int(12 * EDSCALE));
	body_margin->add_theme_constant_override(SNAME("margin_right"), int(12 * EDSCALE));
	body_margin->add_theme_constant_override(SNAME("margin_top"), 0);
	body_margin->add_theme_constant_override(SNAME("margin_bottom"), int(12 * EDSCALE));
	root_vb->add_child(body_margin);

	bento_board = memnew(LagoonicBentoBoard);
	body_margin->add_child(bento_board);

	// Tarjeta principal: el chat, con el agua de Lagoonic en su cabecera.
	chat_bento = memnew(LagoonicBento);
	chat_bento->set_bento_id("chat");
	chat_bento->set_closable(false);
	HBoxContainer *chat_header = chat_bento->enable_water_header(CHAT_WATER_HEIGHT);
	chat_header->get_parent()->connect(SceneStringName(gui_input), callable_mp_static(&LagoonicWaterCaustics::handle_window_drag).bind(Object::cast_to<Control>(chat_header->get_parent())));
	water_caustics = chat_bento->get_water();
	bento_board->set_main(chat_bento);

	// El chat queda en una columna centrada de ancho maximo dentro de la
	// tarjeta (se estrecha sola si hay paneles al lado).
	chat_center = memnew(MarginContainer);
	chat_center->connect(SceneStringName(resized), callable_mp(this, &LagoonicVibeWorkspace::_update_chat_width));
	chat_bento->set_content(chat_center);
	chat_bento->set_content_margins(0, 0);

	agent_dock = memnew(LagoonicAgentDock);
	agent_dock->set_theme(LagoonicVibeTheme::build());
	agent_dock->set_minimal(true);
	agent_dock->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	agent_dock->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	chat_center->add_child(agent_dock);
	agent_dock->set_event_handler(callable_mp(this, &LagoonicVibeWorkspace::_on_agent_event));
	LagoonicAgentSettings::apply_to_chat(agent_dock->get_chat_view());

	// La barra del chat (titulo + Chats + nuevo) pasa a la cabecera de agua.
	if (HBoxContainer *session_bar = agent_dock->get_chat_view()->get_session_bar()) {
		session_bar->get_parent()->remove_child(session_bar);
		session_bar->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		session_bar->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
		session_bar->set_theme(agent_dock->get_theme());
		chat_header->add_child(session_bar);
		// Titulo del chat sobre el agua: mas grande y claro.
		chat_title_label = Object::cast_to<Label>(session_bar->get_child(0));
		if (chat_title_label != nullptr) {
			chat_title_label->add_theme_font_size_override(SceneStringName(font_size), int(16 * EDSCALE));
			chat_title_label->add_theme_constant_override(SNAME("shadow_offset_y"), 1);
		}
	}

	_ensure_runtime_host();
	agent_dock->hook_set_connection(true);
	_apply_palette();
}

LagoonicVibeWorkspace::~LagoonicVibeWorkspace() {
	if (singleton == this) {
		singleton = nullptr;
	}
}
