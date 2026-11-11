/**************************************************************************/
/*  lagoonic_vibe_workspace.h                                             */
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

#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "editor/editor_interface.h"
#include "scene/gui/panel_container.h"
#include "scene/resources/style_box_flat.h"

#include "../runtime/lagoonic_runtime_host.h"
#include "lagoonic_water_caustics.h"

class Button;
class HBoxContainer;
class Label;
class LagoonicAgentDock;
class MarginContainer;
class VBoxContainer;

class LagoonicVibeWorkspace : public PanelContainer {
	GDCLASS(LagoonicVibeWorkspace, PanelContainer);

public:
	static constexpr int WATER_HEADER_HEIGHT = 120;
	static constexpr int CENTER_COLUMN_MAX_WIDTH = 920;

private:
	static inline LagoonicVibeWorkspace *singleton = nullptr;

	Ref<StyleBoxFlat> background_style;
	Ref<StyleBoxFlat> header_button_style;
	Ref<StyleBoxFlat> header_button_hover_style;
	Ref<StyleBoxFlat> play_button_style;
	Ref<StyleBoxFlat> play_button_hover_style;
	Ref<StyleBoxFlat> ghost_button_style;
	Ref<StyleBoxFlat> ghost_button_hover_style;
	bool was_playing = false;
	void _sync_play_button();
	void _apply_palette();

	// Transiciones (inmersion de entrada y marea con el editor clasico).
	Control *content_root = nullptr;
	Ref<ShaderMaterial> tide_material;
	bool transitioning = false;
	bool intro_played = false;
	int intro_wait_frames = 0;
	class LagoonicLilyVeil *intro_veil = nullptr;
	float _tide_overshoot() const;
	void _begin_tide();
	void _end_tide();
	void _set_tide(float p_edge_px);
	void _finish_hide();
	void _finish_show();
	void _play_intro();
	float _header_band_px() const;
	void _pulse_return_button();
	Ref<StyleBoxFlat> return_pill_style;
	Ref<StyleBoxFlat> return_pill_hover_style;

	LagoonicWaterCaustics *water_caustics = nullptr;
	Button *projects_button = nullptr;
	Label *project_title_label = nullptr;
	Button *play_button = nullptr;
	Button *stop_button = nullptr;
	Button *open_editor_button = nullptr;

	LagoonicAgentDock *agent_dock = nullptr;
	Ref<LagoonicRuntimeHost> runtime_host;
	bool crew_applied = false; // Equipo de Configuracion ya pasado al host.

	PanelContainer *return_pill_container = nullptr;
	Button *return_pill_button = nullptr;

	bool vibe_mode_active = true;
	bool initial_prompt_checked = false;

	void _ensure_runtime_host();
	void _ensure_return_pill();
	void _refresh_project_title();
	void _refresh_icons();
	void _consume_initial_prompt();

	void _on_projects_pressed();
	void _on_play_pressed();
	void _on_stop_pressed();
	void _on_open_editor_pressed();
	void _on_return_to_vibe_pressed();

	void _on_agent_event(const StringName &p_event, const Variant &p_a, const Variant &p_b);
	void _on_runtime_event(const Dictionary &p_event);
	void _on_runtime_status(const String &p_status);

protected:
	void _notification(int p_notification);
	static void _bind_methods();

public:
	static LagoonicVibeWorkspace *get_singleton() { return singleton; }

	void set_vibe_mode(bool p_enabled);
	bool is_vibe_mode() const { return vibe_mode_active; }

	void open_classic_editor();
	void return_to_vibe_coding();

	void send_initial_prompt(const String &p_prompt);

	LagoonicAgentDock *get_agent_dock() const { return agent_dock; }
	Ref<LagoonicRuntimeHost> get_runtime_host() const { return runtime_host; }
	LagoonicWaterCaustics *get_water_caustics() const { return water_caustics; }
	Button *get_return_pill_button() const { return return_pill_button; }

	LagoonicVibeWorkspace();
	~LagoonicVibeWorkspace();
};
