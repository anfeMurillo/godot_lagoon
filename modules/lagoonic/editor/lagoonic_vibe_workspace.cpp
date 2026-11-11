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

static const char *LAGOONIC_INITIAL_PROMPT_RES_PATH = "res://.lagoonic/initial_prompt.txt";

void LagoonicVibeWorkspace::_ensure_runtime_host() {
	if (runtime_host.is_valid()) {
		return;
	}
	runtime_host.instantiate();
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
		background_style->set_bg_color(p.bg);
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
}

void LagoonicVibeWorkspace::_on_runtime_status(const String &p_status) {
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
		const String model = (chat != nullptr) ? chat->get_current_model() : String();
		if (runtime_host->ensure_server(cwd, model, Dictionary()) == OK) {
			runtime_host->send_prompt(session, String(p_a), mode, model);
		} else if (chat != nullptr) {
			chat->set_busy(false);
		}
	} else if (p_event == SNAME("abort_requested")) {
		runtime_host->abort();
	} else if (p_event == SNAME("permission_replied")) {
		runtime_host->respond_permission(String(p_a), int(p_b));
	} else if (p_event == SNAME("revert_requested")) {
		runtime_host->revert_message(String(p_a));
	} else if (p_event == SNAME("new_session_requested")) {
		const String sid = runtime_host->create_session(String());
		if (!sid.is_empty() && chat != nullptr) {
			chat->set_current_session(sid);
		}
	} else if (p_event == SNAME("session_selected")) {
		runtime_host->select_session(String(p_a));
	} else if (p_event == SNAME("boss_changed")) {
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
			_apply_palette();
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

	// --- Cabecera superior con LagoonicWaterCaustics (120 px) y barra superpuesta ---
	Control *header_band = memnew(Control);
	header_band->set_custom_minimum_size(Size2(0, WATER_HEADER_HEIGHT));
	header_band->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	root_vb->add_child(header_band);
	header_band->connect(SceneStringName(gui_input), callable_mp_static(&LagoonicWaterCaustics::handle_window_drag).bind(header_band));

	water_caustics = memnew(LagoonicWaterCaustics);
	water_caustics->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	water_caustics->set_custom_minimum_size(Size2(0, WATER_HEADER_HEIGHT));
	water_caustics->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	header_band->add_child(water_caustics);

	MarginContainer *header_margin = memnew(MarginContainer);
	header_margin->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	header_margin->add_theme_constant_override(SNAME("margin_left"), 24);
	header_margin->add_theme_constant_override(SNAME("margin_right"), 24);
	header_margin->add_theme_constant_override(SNAME("margin_top"), 18);
	header_margin->add_theme_constant_override(SNAME("margin_bottom"), 18);
	header_band->add_child(header_margin);

	HBoxContainer *header_bar = memnew(HBoxContainer);
	header_bar->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	header_bar->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	header_bar->add_theme_constant_override(SNAME("separation"), 12);
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
	project_title_label->add_theme_font_size_override(SceneStringName(font_size), 20);
	project_title_label->add_theme_color_override(SNAME("font_color"), Color(0.96f, 0.99f, 1.0f));
	project_title_label->add_theme_color_override(SNAME("font_shadow_color"), Color(0.0f, 0.08f, 0.14f, 0.75f));
	project_title_label->add_theme_constant_override(SNAME("shadow_offset_x"), 0);
	project_title_label->add_theme_constant_override(SNAME("shadow_offset_y"), 1);
	// Centrado sobre todo el ancho (capa encima de la barra), no entre botones.
	project_title_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	project_title_label->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	project_title_label->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	header_margin->add_child(project_title_label);
	_refresh_project_title();

	Control *header_spacer = memnew(Control);
	header_spacer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	header_bar->add_child(header_spacer);

	// Derecha: "▶ Jugar", "■ Parar", "Abrir Editor de Godot".
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

	// --- Cuerpo central (centrado, ancho máx ~920 px) con LagoonicAgentDock ---
	MarginContainer *body_margin = memnew(MarginContainer);
	body_margin->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	body_margin->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	body_margin->add_theme_constant_override(SNAME("margin_left"), 20);
	body_margin->add_theme_constant_override(SNAME("margin_right"), 20);
	body_margin->add_theme_constant_override(SNAME("margin_top"), 16);
	body_margin->add_theme_constant_override(SNAME("margin_bottom"), 20);
	root_vb->add_child(body_margin);

	HBoxContainer *center_hb = memnew(HBoxContainer);
	center_hb->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	center_hb->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	body_margin->add_child(center_hb);

	Control *left_spacer = memnew(Control);
	left_spacer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	center_hb->add_child(left_spacer);

	VBoxContainer *center_column = memnew(VBoxContainer);
	center_column->set_custom_minimum_size(Size2(CENTER_COLUMN_MAX_WIDTH, 0));
	center_column->set_h_size_flags(Control::SIZE_SHRINK_CENTER);
	center_column->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	center_hb->add_child(center_column);

	agent_dock = memnew(LagoonicAgentDock);
	agent_dock->set_theme(LagoonicVibeTheme::build());
	agent_dock->set_minimal(true);
	agent_dock->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	agent_dock->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	center_column->add_child(agent_dock);
	agent_dock->set_event_handler(callable_mp(this, &LagoonicVibeWorkspace::_on_agent_event));

	Control *right_spacer = memnew(Control);
	right_spacer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	center_hb->add_child(right_spacer);

	_ensure_runtime_host();
	agent_dock->hook_set_connection(true);
	_apply_palette();
}

LagoonicVibeWorkspace::~LagoonicVibeWorkspace() {
	if (singleton == this) {
		singleton = nullptr;
	}
}
