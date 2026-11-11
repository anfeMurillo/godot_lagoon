/**************************************************************************/
/*  lagoonic_water_caustics.h                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                            LAGOONIC ENGINE                             */
/**************************************************************************/

#pragma once

#include "scene/gui/control.h"
#include "scene/resources/material.h"
#include "scene/resources/shader.h"

class LagoonicWaterCaustics : public Control {
	GDCLASS(LagoonicWaterCaustics, Control);

public:
	static constexpr float DEFAULT_BAND_HEIGHT = 150.0f;
	static constexpr float STILL_TIME = 4.2f;

private:
	Ref<Shader> caustics_shader;
	Ref<ShaderMaterial> caustics_material;

	Color tint = Color(0.0f, 0.612f, 0.878f, 0.85f); // #009ce0 Lagoonic water
	float band_height = DEFAULT_BAND_HEIGHT;
	bool animated = true;
	float fill = 0.0f;
	Color fill_color = Color(0.05f, 0.12f, 0.18f, 1.0f);
	float pattern_band = 0.0f;
	float manual_clock = -1.0f; // < 0: usa TIME del motor.

	void _ensure_shader_material();
	void _sync_shader_parameters();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_tint(const Color &p_tint);
	Color get_tint() const;

	void set_band_height(float p_height);
	float get_band_height() const;

	// Transiciones de pantalla (inmersion / marea).
	void set_fill(float p_fill);
	float get_fill() const { return fill; }
	void set_fill_color(const Color &p_color);
	// Alto de la franja de causticas aunque el control ocupe mas.
	void set_pattern_band(float p_px);
	float get_pattern_band() const { return pattern_band; }
	// Anima con un reloj propio que empieza en p_time (continuidad entre procesos).
	void start_manual_clock(float p_time);
	float get_manual_clock() const { return manual_clock; }

	void set_animated(bool p_animated);
	bool is_animated() const;

	LagoonicWaterCaustics();
	~LagoonicWaterCaustics() override = default;

	// Arrastre de ventana para cabeceras que tapan la barra de titulo de
	// Godot (Project Manager / editor con extend_to_title). Conectar a la
	// senal gui_input de la franja superior.
	static void handle_window_drag(const Ref<InputEvent> &p_event, Control *p_area);
};
