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
#include "editor/editor_main_screen.h"
#include "editor/run/editor_run_bar.h"
#include "editor/gui/editor_title_bar.h"
#include "editor/gui/window_wrapper.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/margin_container.h"
#include "scene/main/scene_tree.h"
#include "editor/settings/editor_settings.h"
#include "lagoonic_bento.h"
#include "lagoonic_bento_panels.h"
#include "lagoonic_game_clip.h"
#include "../runtime/lagoonic_coding_agents.h"
#include "scene/gui/color_rect.h"
#include "scene/gui/panel.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/popup_menu.h"
#include "scene/resources/theme.h"
#include "scene/gui/scroll_container.h"
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
		add_panel_button->set_button_icon(get_editor_theme_icon(SNAME("Panels2")));
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
	{ "team", "Equipo", "Groups", "El líder y los ayudantes que lanza, en vivo" },
	{ "scene", "Escena", "PackedScene", "Las piezas de la escena abierta, como en el editor" },
	{ "inspector", "Propiedades", "Tools", "Cambia a mano los valores del elemento elegido" },
	{ "game", "Juego", "Play", "Tu juego en marcha, aquí mismo" },
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
	// Todos los paneles con su casilla: se abren y cierran desde aqui.
	add_panel_menu->clear();
	for (const LagoonicBentoType &t : LAGOONIC_BENTOS) {
		add_panel_menu->add_icon_check_item(get_editor_theme_icon(StringName(t.icon)), String::utf8(t.title));
		const int idx = add_panel_menu->get_item_count() - 1;
		add_panel_menu->set_item_checked(idx, bento_board->get_side(t.id) != nullptr);
		add_panel_menu->set_item_metadata(idx, String(t.id));
		add_panel_menu->set_item_tooltip(idx, String::utf8(t.hint));
	}
	const Rect2 r = add_panel_button->get_screen_rect();
	add_panel_menu->set_position(Point2i(r.position.x, r.position.y + r.size.y + 4));
	add_panel_menu->reset_size();
	add_panel_menu->popup();
}

void LagoonicVibeWorkspace::_on_add_panel_selected(int p_index) {
	const Variant meta = add_panel_menu->get_item_metadata(p_index);
	if (meta.get_type() != Variant::STRING) {
		return;
	}
	if (bento_board->get_side(meta) != nullptr) {
		_close_bento(meta);
	} else {
		_open_bento(meta);
	}
}

Dictionary LagoonicVibeWorkspace::_get_team() {
	_ensure_runtime_host();
	Dictionary d;
	const String boss = LagoonicAgentSettings::get_boss();
	const LagoonicCodingAgents::AgentInfo *info = LagoonicCodingAgents::get_agent(boss);
	d["boss_name"] = info != nullptr ? String::utf8(info->name) : String("OpenCode");
	d["workers"] = LagoonicAgentSettings::crew_workers();
	d["busy"] = agent_dock != nullptr && agent_dock->get_chat_view() != nullptr && agent_dock->get_chat_view()->is_busy();
	d["now_msec"] = OS::get_singleton()->get_ticks_msec();
	d["tasks"] = (runtime_host.is_valid() && runtime_host->get_crew().is_valid()) ? runtime_host->get_crew()->snapshot() : Array();
	return d;
}

void LagoonicVibeWorkspace::_check_team_started() {
	if (runtime_host.is_null() || runtime_host->get_crew().is_null() || bento_board == nullptr) {
		return;
	}
	// El lider acaba de lanzar un ayudante: se abre el panel para verlo.
	// Si el usuario lo cierra, no vuelve hasta la siguiente tarea nueva.
	const int count = runtime_host->get_crew()->get_task_counter();
	if (count > team_tasks_seen) {
		team_tasks_seen = count;
		_open_bento("team");
	}
}

// La vista de juego integrada del editor (con su barra) vive en la pantalla
// "Game" del editor clasico. Mientras el juego corre en modo Vibe se muda a
// la tarjeta Juego y luego vuelve a su sitio.
static WindowWrapper *_lagoonic_find_game_wrapper(Node *p_from) {
	if (p_from == nullptr) {
		return nullptr;
	}
	if (p_from->get_class() == "GameView") {
		for (Node *n = p_from->get_parent(); n != nullptr; n = n->get_parent()) {
			if (WindowWrapper *w = Object::cast_to<WindowWrapper>(n)) {
				return w;
			}
		}
		return nullptr;
	}
	for (int i = 0; i < p_from->get_child_count(); i++) {
		if (WindowWrapper *w = _lagoonic_find_game_wrapper(p_from->get_child(i))) {
			return w;
		}
	}
	return nullptr;
}

// El editor clasico puede abrir el juego en una ventana flotante al jugar
// (opcion "Ventana flotante al jugar" de la vista de juego). Esa ventana nueva
// saca de la pantalla completa al escritorio. En modo Vibe el juego va en su
// tarjeta: la opcion se apaga mientras dura el modo y luego se restaura.
static PopupMenu *_lagoonic_game_window_menu(Node *p_from) {
	static const int MAKE_FLOATING_ON_PLAY = 7; // GameView::WINDOW_MAKE_FLOATING_ON_PLAY.
	static const int RUN_GAME_EMBEDDED = 6; // GameView::WINDOW_RUN_GAME_EMBEDDED.
	static const int SIZE_MODE_STRETCH = 10; // GameView::WINDOW_SIZE_MODE_STRETCH.
	if (p_from == nullptr) {
		return nullptr;
	}
	if (MenuButton *mb = Object::cast_to<MenuButton>(p_from)) {
		PopupMenu *pm = mb->get_popup();
		// El menu de velocidad numera sus opciones 0, 1, 2... y tambien tiene
		// los ids 6, 7 y 10: el de ventana es el que empieza por el 6.
		if (pm->get_item_count() > 0 && pm->get_item_id(0) == RUN_GAME_EMBEDDED && pm->get_item_index(MAKE_FLOATING_ON_PLAY) >= 0 && pm->get_item_index(SIZE_MODE_STRETCH) >= 0) {
			return pm;
		}
	}
	for (int i = 0; i < p_from->get_child_count(); i++) {
		if (PopupMenu *pm = _lagoonic_game_window_menu(p_from->get_child(i))) {
			return pm;
		}
	}
	return nullptr;
}

void LagoonicVibeWorkspace::_set_game_floating_suppressed(bool p_suppressed) {
	if (EditorNode::get_singleton() == nullptr) {
		return;
	}
	_set_game_fill(p_suppressed); // Aparte de la ventana flotante: cada una guarda lo suyo.
	if (p_suppressed == game_floating_suppressed) {
		return;
	}
	if (game_wrapper == nullptr) {
		game_wrapper = _lagoonic_find_game_wrapper(EditorNode::get_singleton()->get_editor_main_screen()->get_control());
	}
	PopupMenu *pm = _lagoonic_game_window_menu(game_wrapper != nullptr ? game_wrapper->get_wrapped_control() : nullptr);
	if (pm == nullptr) {
		return;
	}
	const int idx = pm->get_item_index(7);
	if (p_suppressed) {
		if (!pm->is_item_checked(idx)) {
			return; // Ya estaba apagada: nada que restaurar despues.
		}
		pm->emit_signal(SNAME("id_pressed"), 7);
		game_floating_suppressed = true;
	} else {
		if (!pm->is_item_checked(idx)) {
			pm->emit_signal(SNAME("id_pressed"), 7);
		}
		game_floating_suppressed = false;
	}
}

// "Estirar para ajustar": el juego se redimensiona al hueco de su tarjeta en
// vez de quedarse con su tamano fijo y dejar bandas vacias alrededor. Se
// restaura el modo que tenia el usuario al salir del modo Vibe.
void LagoonicVibeWorkspace::_set_game_fill(bool p_fill) {
	static const int SIZE_FIXED = 8; // GameView::WINDOW_SIZE_MODE_FIXED.
	static const int SIZE_KEEP_ASPECT = 9;
	static const int SIZE_STRETCH = 10;
	if (game_wrapper == nullptr) {
		game_wrapper = _lagoonic_find_game_wrapper(EditorNode::get_singleton()->get_editor_main_screen()->get_control());
	}
	PopupMenu *pm = _lagoonic_game_window_menu(game_wrapper != nullptr ? game_wrapper->get_wrapped_control() : nullptr);
	if (pm == nullptr) {
		return;
	}
	if (p_fill) {
		if (game_size_mode_before != 0) {
			return;
		}
		int current = SIZE_FIXED;
		for (int id : { SIZE_FIXED, SIZE_KEEP_ASPECT, SIZE_STRETCH }) {
			const int idx = pm->get_item_index(id);
			if (idx >= 0 && pm->is_item_checked(idx)) {
				current = id;
			}
		}
		// Si el editor se cerro estando en Vibe, el modo guardado ya es "estirar"
		// y el del usuario esta apuntado aparte.
		EditorSettings *es = EditorSettings::get_singleton();
		const int saved = es != nullptr ? int(es->get_project_metadata("lagoonic", "game_size_before", 0)) : 0;
		if (current == SIZE_STRETCH && saved >= SIZE_FIXED && saved <= SIZE_KEEP_ASPECT) {
			current = saved;
		}
		game_size_mode_before = current;
		if (es != nullptr) {
			es->set_project_metadata("lagoonic", "game_size_before", current);
		}
		if (pm->is_item_checked(pm->get_item_index(SIZE_STRETCH)) == false) {
			pm->emit_signal(SNAME("id_pressed"), SIZE_STRETCH);
		}
	} else if (game_size_mode_before != 0) {
		const int before = game_size_mode_before;
		game_size_mode_before = 0;
		if (EditorSettings::get_singleton() != nullptr) {
			EditorSettings::get_singleton()->set_project_metadata("lagoonic", "game_size_before", 0);
		}
		if (before != SIZE_STRETCH) {
			pm->emit_signal(SNAME("id_pressed"), before);
		}
	}
}

static Control *_lagoonic_find_embedded_process(Node *p_from) {
	if (p_from == nullptr) {
		return nullptr;
	}
	if (Control *c = Object::cast_to<Control>(p_from)) {
		if (String(c->get_class()).begins_with("EmbeddedProcess")) {
			return c;
		}
	}
	for (int i = 0; i < p_from->get_child_count(); i++) {
		if (Control *c = _lagoonic_find_embedded_process(p_from->get_child(i))) {
			return c;
		}
	}
	return nullptr;
}

void LagoonicVibeWorkspace::_on_run_stopped() {
	_undock_game();
	if (game_clipped) {
		lagoonic_clip_game_layer(get_window() != nullptr ? get_window()->get_window_id() : 0, Rect2(), 0, false);
		game_clipped = false;
	}
}

void LagoonicVibeWorkspace::_fit_game_card() {
	// La tarjeta del juego toma, dentro de su columna, la altura que pide la
	// proporcion del juego para su ancho (asi casi no quedan bandas). Sola en
	// su columna ya lo ocupa todo.
	if (!game_docked || bento_board == nullptr || game_holder == nullptr || bento_board->get_side("game") == nullptr) {
		game_fit_width = 0;
		return;
	}
	LagoonicBento *card = bento_board->get_side("game");
	const float w = game_holder->get_size().x;
	if (w <= 1 || !card->is_visible_in_tree() || Math::abs(w - game_fit_width) < 4) {
		return;
	}
	game_fit_width = w;
	const int vw = GLOBAL_GET("display/window/size/viewport_width");
	const int vh = GLOBAL_GET("display/window/size/viewport_height");
	if (vw <= 0 || vh <= 0) {
		return;
	}
	// Lo que la tarjeta ocupa ademas del juego: cabecera, barra y linea.
	Control *area = _lagoonic_find_embedded_process(game_wrapper != nullptr ? game_wrapper->get_wrapped_control() : nullptr);
	const float extra = area != nullptr ? MAX(0.0f, card->get_size().y - area->get_size().y) : 90 * EDSCALE;
	bento_board->fit_card_height("game", w * float(vh) / float(vw) + extra);
}

void LagoonicVibeWorkspace::_update_game_clip() {
	// Mientras el juego esta en su tarjeta, su capa nativa toma las esquinas
	// de abajo de la tarjeta. Fuera de ella (o ampliado a otra cosa) se suelta.
	Control *area = (game_docked && game_wrapper != nullptr) ? _lagoonic_find_embedded_process(game_wrapper->get_wrapped_control()) : nullptr;
	const bool on = area != nullptr && area->is_visible_in_tree();
	if (!on && !game_clipped) {
		return;
	}
	const Rect2 rect = on ? area->get_global_rect() : Rect2();
	// Cada fotograma mientras dure: la capa del juego aparece un momento
	// despues de entrar en la tarjeta (y otra nueva en cada partida).
	game_clip_rect = rect;
	game_clipped = on;
	// Radio de la tarjeta menos su borde de 1 px.
	lagoonic_clip_game_layer(get_window() != nullptr ? get_window()->get_window_id() : 0, rect, 16 * EDSCALE - 1, on);
}

void LagoonicVibeWorkspace::_dock_game() {
	EditorInterface *ei = EditorInterface::get_singleton();
	if (game_docked || game_holder == nullptr || !vibe_mode_active || ei == nullptr || !ei->is_playing_scene() || EditorNode::get_singleton() == nullptr) {
		return;
	}
	if (game_wrapper == nullptr) {
		game_wrapper = _lagoonic_find_game_wrapper(EditorNode::get_singleton()->get_editor_main_screen()->get_control());
	}
	if (game_wrapper == nullptr || game_wrapper->get_parent() == nullptr) {
		return;
	}
	// El editor clasico saca el juego a una ventana aparte al lanzarlo: en
	// modo Vibe vuelve dentro, a su tarjeta.
	if (game_wrapper->get_window_enabled()) {
		game_wrapper->set_window_enabled(false);
	}
	game_wrapper_home = game_wrapper->get_parent();
	game_wrapper_index = game_wrapper->get_index();
	game_wrapper_home->remove_child(game_wrapper);
	game_wrapper->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	game_wrapper->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	game_scroll->add_child(game_wrapper);
	game_wrapper->set_visible(true);
	game_placeholder->set_visible(false);
	game_docked = true;
	// Linea divisoria entre la barra de herramientas y el juego, sin el
	// espaciado propio de la vista de juego (dejaba una franja vacia arriba).
	if (Control *view = game_wrapper->get_wrapped_control()) {
		view->add_theme_constant_override(SNAME("separation"), 0);
		// La barra de la vista de juego mide ~800 px y no dejaba estrechar su
		// columna: al adelgazar la ventana se recortaba la UI. Aqui se
		// desplaza en horizontal cuando no cabe.
		if (Control *toolbar = Object::cast_to<Control>(view->get_child(0, false))) {
			game_toolbar = toolbar;
			game_toolbar_scroll = memnew(ScrollContainer);
			game_toolbar_scroll->set_vertical_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
			game_toolbar_scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_AUTO);
			game_toolbar_scroll->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			view->remove_child(toolbar);
			view->add_child(game_toolbar_scroll);
			view->move_child(game_toolbar_scroll, 0);
			toolbar->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			game_toolbar_scroll->add_child(toolbar);
		}
		// El fondo del panel del juego asomaba en las esquinas redondeadas
		// (un tono azulado sobre el borde de la tarjeta): transparente aqui.
		for (int i = 0; i < view->get_child_count(false); i++) {
			if (Panel *panel = Object::cast_to<Panel>(view->get_child(i, false))) {
				panel->add_theme_style_override(SceneStringName(panel), memnew(StyleBoxEmpty));
				game_panel = panel;
			}
		}
		game_divider = memnew(ColorRect);
		game_divider->set_custom_minimum_size(Size2(0, MAX(1, int(EDSCALE))));
		game_divider->set_color(LagoonicVibeTheme::current().border);
		game_divider->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		view->add_child(game_divider);
		view->move_child(game_divider, MIN(1, view->get_child_count(false) - 1));
	}
}

void LagoonicVibeWorkspace::_undock_game() {
	if (!game_docked || game_wrapper == nullptr || game_wrapper_home == nullptr || game_scroll == nullptr) {
		return;
	}
	game_docked = false;
	if (game_wrapper->get_wrapped_control() != nullptr) {
		Control *view = game_wrapper->get_wrapped_control();
		view->remove_theme_constant_override(SNAME("separation"));
		// La barra vuelve a su sitio de siempre en la vista de juego.
		if (game_toolbar != nullptr && game_toolbar_scroll != nullptr) {
			game_toolbar_scroll->remove_child(game_toolbar);
			view->add_child(game_toolbar);
			view->move_child(game_toolbar, 0);
			view->remove_child(game_toolbar_scroll);
			memdelete(game_toolbar_scroll);
		}
		game_toolbar = nullptr;
		game_toolbar_scroll = nullptr;
	}
	if (game_panel != nullptr) {
		game_panel->remove_theme_style_override(SceneStringName(panel));
		game_panel = nullptr;
	}
	if (game_divider != nullptr) {
		game_divider->get_parent()->remove_child(game_divider);
		memdelete(game_divider);
		game_divider = nullptr;
	}
	if (game_wrapper->get_parent() != nullptr) {
		game_wrapper->get_parent()->remove_child(game_wrapper);
	}
	game_wrapper_home->add_child(game_wrapper);
	game_wrapper_home->move_child(game_wrapper, MIN(game_wrapper_index, game_wrapper_home->get_child_count() - 1));
	// Visible solo si el editor clasico tiene elegida la pantalla Juego.
	game_wrapper->set_visible(EditorNode::get_singleton()->get_editor_main_screen()->get_selected_index() == EditorMainScreen::EDITOR_GAME);
	if (game_placeholder != nullptr) {
		game_placeholder->set_visible(true);
	}
	if (bento_board != nullptr && bento_board->get_expanded() == "game") {
		bento_board->set_expanded(String());
	}
}

void LagoonicVibeWorkspace::_on_game_started() {
	if (!vibe_mode_active || bento_board == nullptr) {
		return;
	}
	if (bento_board->get_side("game") == nullptr) {
		_open_bento("game", false); // Solo mientras dure esta partida.
		game_auto_opened = true;
		// Lo primero de la columna, para que se vea nada mas lanzar.
		bento_board->move_side_first("game");
	}
	_dock_game();
}

void LagoonicVibeWorkspace::_on_game_stopped() {
	_undock_game();
	if (game_auto_opened) {
		_close_bento("game");
	}
}

void LagoonicVibeWorkspace::_save_layout() {
	// Se llama cada pocos segundos: solo escribe cuando el usuario movio algo.
	if (EditorSettings::get_singleton() == nullptr || bento_board == nullptr || !layout_restored) {
		return;
	}
	const Dictionary layout = bento_board->get_layout();
	if (layout == saved_layout) {
		return;
	}
	saved_layout = layout;
	EditorSettings::get_singleton()->set_project_metadata("lagoonic", "bento_layout", layout);
	EditorSettings::get_singleton()->save_project_metadata();
}

void LagoonicVibeWorkspace::_restore_layout(const Dictionary &p_layout) {
	if (bento_board != nullptr && !p_layout.is_empty()) {
		bento_board->set_layout(p_layout);
	}
	saved_layout = bento_board != nullptr ? bento_board->get_layout() : Dictionary();
	layout_restored = true;
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
	} else if (p_id == "team") {
		LagoonicTeamPanel *panel = memnew(LagoonicTeamPanel);
		panel->setup(callable_mp(this, &LagoonicVibeWorkspace::_get_team));
		bento->set_content(panel);
	} else if (p_id == "scene") {
		bento->set_content(memnew(LagoonicScenePanel));
	} else if (p_id == "inspector") {
		bento->set_content(memnew(LagoonicInspectorPanel));
		bento->set_content_margins(8, 8);
	} else if (p_id == "game") {
		// Donde se aloja la vista de juego del editor mientras el juego corre.
		game_holder = memnew(MarginContainer);
		{
			// Sin marco alrededor del juego (el borde de foco de la vista de
			// juego): en la tarjeta solo hay una linea bajo la barra.
			Ref<Theme> frame_theme;
			frame_theme.instantiate();
			Ref<StyleBoxEmpty> no_frame;
			no_frame.instantiate();
			frame_theme->set_stylebox(SNAME("FocusViewport"), SNAME("EditorStyles"), no_frame);
			game_holder->set_theme(frame_theme);
		}
		game_placeholder = memnew(Label);
		game_placeholder->set_text(String::utf8("Pulsa Jugar y tu juego aparecerá aquí."));
		game_placeholder->set_theme_type_variation(LagoonicVibeTheme::MUTED_LABEL);
		game_placeholder->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		game_placeholder->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
		game_placeholder->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		game_holder->add_child(game_placeholder);
		// Contenedor fijo para la vista de juego. Su barra marca el ancho
		// minimo de la columna; el chat conserva el suyo (ver set_main).
		game_scroll = memnew(ScrollContainer);
		game_scroll->set_vertical_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
		game_scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
		game_holder->add_child(game_scroll);
		bento->set_content(game_holder);
		// El juego llega hasta el borde de la tarjeta (solo deja su linea de 1 px).
		bento->set_content_margins(1, 0);
		game_holder->add_theme_constant_override(SNAME("margin_bottom"), 1);
	}
	bento_board->add_side(bento);
	bento->apply_palette();
	if (p_id == "game") {
		_dock_game(); // Si el juego ya esta en marcha, entra ahora.
	}
	if (p_save) {
		_save_bentos();
	}
}

void LagoonicVibeWorkspace::_close_bento(const String &p_id) {
	if (bento_board == nullptr) {
		return;
	}
	if (p_id == "game") {
		_undock_game(); // Antes de liberar la tarjeta: la vista es del editor.
		game_holder = nullptr;
		game_placeholder = nullptr;
		game_scroll = nullptr;
		game_auto_opened = false;
	}
	bento_board->remove_side(p_id);
	if (p_id == "plan" && agent_dock != nullptr) {
		agent_dock->get_chat_view()->set_plan_card_suppressed(false);
	}
	_save_bentos();
}

void LagoonicVibeWorkspace::_save_bentos() {
	if (EditorSettings::get_singleton() != nullptr && bento_board != nullptr) {
		PackedStringArray ids = bento_board->get_side_ids();
		if (game_auto_opened) {
			ids.erase("game"); // Se abrio sola para esta partida: no se recuerda.
		}
		EditorSettings::get_singleton()->set_project_metadata("lagoonic", "bentos", ids);
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
	// Los tamanos que el usuario dejo, cuando el tablero ya tiene su ancho.
	bento_board->set_order_callback(callable_mp(this, &LagoonicVibeWorkspace::_save_bentos));
	const Variant layout = EditorSettings::get_singleton()->get_project_metadata("lagoonic", "bento_layout", Dictionary());
	callable_mp(this, &LagoonicVibeWorkspace::_restore_layout).call_deferred(layout.get_type() == Variant::DICTIONARY ? Dictionary(layout) : Dictionary());
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
	_set_game_floating_suppressed(true);
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
	if (playing) {
		// Despues de que la vista de juego del editor prepare la partida.
		callable_mp(this, &LagoonicVibeWorkspace::_on_game_started).call_deferred();
	} else {
		_on_game_stopped();
	}
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
	// La vista de juego vuelve al editor clasico, o entra en su tarjeta.
	if (!p_enabled) {
		_undock_game();
	} else {
		callable_mp(this, &LagoonicVibeWorkspace::_dock_game).call_deferred();
	}
	if (is_inside_tree()) {
		_set_game_floating_suppressed(p_enabled);
	}
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
	_check_team_started();
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
		_check_team_started();
		_save_layout();
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
			_set_game_floating_suppressed(false); // Deja la preferencia del editor clasico como estaba.
		} break;

		case NOTIFICATION_INTERNAL_PROCESS: {
			_sync_play_button();
			_update_game_clip();
			_fit_game_card();
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
			callable_mp(this, &LagoonicVibeWorkspace::_set_game_floating_suppressed).call_deferred(vibe_mode_active);
			// Al parar el juego (tambien al salir del editor o volver a
			// Proyectos, que lo paran primero) la vista de juego vuelve a su
			// sitio en ese mismo momento. Si siguiera en la tarjeta, se liberaria
			// con ella y el editor la usaria despues: cierre brusco y el juego
			// quedaba abierto, huerfano.
			if (EditorRunBar::get_singleton() != nullptr) {
				EditorRunBar::get_singleton()->connect(SNAME("stop_pressed"), callable_mp(this, &LagoonicVibeWorkspace::_on_run_stopped));
			}
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
	ClassDB::bind_method(D_METHOD("get_runtime_host"), &LagoonicVibeWorkspace::get_runtime_host);
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
	add_panel_button->set_text(TTRC("Paneles"));
	add_panel_button->set_tooltip_text(String::utf8("Abre o cierra paneles junto al chat: plan, cambios, archivos, contexto y equipo."));
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

	// Con muchas columnas o filas el tablero no se aprieta ni se sale de la
	// pantalla: se desplaza en horizontal y en vertical.
	ScrollContainer *board_scroll = memnew(ScrollContainer);
	board_scroll->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	board_scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	board_scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_AUTO);
	board_scroll->set_vertical_scroll_mode(ScrollContainer::SCROLL_MODE_AUTO);
	body_margin->add_child(board_scroll);
	bento_board = memnew(LagoonicBentoBoard);
	board_scroll->add_child(bento_board);

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
