/**************************************************************************/
/*  lagoonic_water_caustics.cpp                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                            LAGOONIC ENGINE                             */
/**************************************************************************/

#include "lagoonic_water_caustics.h"
#include "servers/display/display_server.h"
#include "scene/main/window.h"
#include "core/input/input_event.h"

#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "core/os/time.h"
#include "editor/themes/editor_scale.h"

static const char *LAGOONIC_WATER_CAUSTICS_SHADER_CODE = R"godot_shader(
shader_type canvas_item;

uniform vec2 size = vec2(800.0, 150.0);
uniform float unit = 0.33333334;
uniform vec4 tint = vec4(0.0, 0.612, 0.878, 0.85);
uniform bool animated = true;
uniform float still_time = 4.2;
// Transiciones: fill 0 = solo la franja; 1 = toda el area cubierta de agua.
// El borde del agua es una ola (dos senos) con una cresta brillante.
uniform float fill = 0.0;
uniform vec4 fill_color = vec4(0.05, 0.12, 0.18, 1.0);
uniform float band_px = 0.0;
uniform float wave_amp = 14.0;

float hash(vec2 p) {
	return fract(sin(p.x * 127.1 + p.y * 311.7) * 43758.5453);
}

float pattern_alpha(vec2 frag, vec2 size_px, float t) {
	float width = size_px.x * unit;
	float depth = frag.y / max(size_px.y, 1.0);
	float proximity = 1.0 - depth;
	float fade = pow(min(1.0, proximity * 2.2), 1.8);
	if (fade <= 0.003) {
		return 0.0;
	}
	float spread = 0.18 + proximity * 0.82;
	float away = (1.0 / spread - 1.0) * 1.2;
	float across = (frag.x * unit - width * 0.5) / max(width * 0.22 * spread, 0.0001);
	float u = across * 0.866 - away * 0.5;
	float v = across * 0.5 + away * 0.866;
	u += sin(v * 2.1 + t * 0.35) * 0.20;
	v += sin(u * 2.5 - t * 0.29) * 0.17;
	u += t * 0.035;
	vec2 cell = floor(vec2(u, v));
	float first = 9.0;
	float second = 9.0;
	for (int j = -1; j <= 1; j++) {
		for (int i = -1; i <= 1; i++) {
			vec2 c = cell + vec2(float(i), float(j));
			float seed = hash(c);
			vec2 site = c + 0.5 + vec2(
				(seed - 0.5) * 0.7 + sin(t * 0.24 + seed * 9.0) * 0.09,
				(hash(vec2(c.y + 17.0, c.x - 8.0)) - 0.5) * 0.7
			);
			float d = distance(vec2(u, v), site);
			if (d < first) {
				second = first;
				first = d;
			} else if (d < second) {
				second = d;
			}
		}
	}
	float edge = second - first;
	float per_px = unit / max(width * 0.22 * spread, 0.0001);
	float line = (2.0 + proximity * 2.8) * per_px;
	float aa = per_px * 1.5;
	float solid = clamp((line + aa - edge) / max(aa, 0.0001), 0.0, 1.0);
	float detail = clamp((proximity - 0.25) * 4.0, 0.0, 1.0);
	solid *= detail;
	float e = edge / 0.19;
	float halo = exp(-e * e) * 0.05;
	float shimmer = 0.86 + sin(u * 4.0 + v * 3.0 + t * 0.6) * 0.14;
	return clamp((solid * 0.9 + halo) * fade * shimmer * (0.55 + proximity * 0.45), 0.0, 1.0);
}

void fragment() {
	vec2 fallback_size = vec2(1.0 / max(SCREEN_PIXEL_SIZE.x, 0.0001), 150.0);
	vec2 resolved_size = (size.x > 1.0 && size.y > 1.0) ? size : fallback_size;
	vec2 base = UV * resolved_size;
	float t = animated ? TIME : still_time;
	// El dibujo de causticas siempre mide la franja, aunque el control crezca.
	vec2 pattern_size = vec2(resolved_size.x, band_px > 1.0 ? band_px : resolved_size.y);
	float alpha = pattern_alpha(base + vec2(-0.25, -0.25), pattern_size, t)
		+ pattern_alpha(base + vec2(0.25, -0.25), pattern_size, t)
		+ pattern_alpha(base + vec2(-0.25, 0.25), pattern_size, t)
		+ pattern_alpha(base + vec2(0.25, 0.25), pattern_size, t);
	alpha *= 0.25;
	float a = tint.a * alpha;
	if (fill <= 0.0005) {
		COLOR = vec4(tint.rgb, a);
	} else {
		float wave = sin(base.x * 0.011 + t * 2.2) * wave_amp * 0.6 + sin(base.x * 0.027 - t * 1.4) * wave_amp * 0.4;
		float edge = fill * (resolved_size.y + wave_amp * 2.0) - wave_amp + wave;
		float water = 1.0 - smoothstep(edge - 1.5, edge + 1.5, base.y);
		float crest = exp(-pow((base.y - edge) / 3.0, 2.0)) * step(fill, 0.999) * 0.7;
		// Agua mas clara junto a la ola y mas profunda hacia arriba.
		float near_edge = smoothstep(edge - 180.0, edge, base.y) * step(fill, 0.999);
		vec3 body = mix(fill_color.rgb, mix(fill_color.rgb, tint.rgb, 0.18), near_edge);
		vec3 rgb = mix(body, tint.rgb, a);
		rgb = mix(rgb, tint.rgb, crest);
		float out_a = max(max(water * fill_color.a, a), crest * tint.a);
		COLOR = vec4(rgb, out_a);
	}
}
)godot_shader";

void LagoonicWaterCaustics::_ensure_shader_material() {
	if (caustics_shader.is_null()) {
		caustics_shader.instantiate();
		caustics_shader->set_code(String::utf8(LAGOONIC_WATER_CAUSTICS_SHADER_CODE));
	}
	if (caustics_material.is_null()) {
		caustics_material.instantiate();
		caustics_material->set_shader(caustics_shader);
		set_material(caustics_material);
	}
	_sync_shader_parameters();
}

void LagoonicWaterCaustics::_sync_shader_parameters() {
	if (caustics_material.is_null()) {
		return;
	}
	Size2 current_size = get_size();
	if (current_size.x <= 1.0f) {
		current_size.x = 800.0f;
	}
	if (current_size.y <= 1.0f) {
		current_size.y = band_height;
	}
	caustics_material->set_shader_parameter("size", current_size);
	caustics_material->set_shader_parameter("unit", 1.0f / 3.0f);
	caustics_material->set_shader_parameter("tint", Vector4(tint.r, tint.g, tint.b, tint.a));
	caustics_material->set_shader_parameter("animated", animated && manual_clock < 0.0f);
	caustics_material->set_shader_parameter("still_time", manual_clock >= 0.0f ? manual_clock : STILL_TIME);
	caustics_material->set_shader_parameter("fill", fill);
	caustics_material->set_shader_parameter("fill_color", Vector4(fill_color.r, fill_color.g, fill_color.b, fill_color.a));
	caustics_material->set_shader_parameter("band_px", pattern_band);
	caustics_material->set_shader_parameter("wave_amp", 14.0f * (EDSCALE > 0.0f ? EDSCALE : 1.0f));
}

void LagoonicWaterCaustics::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE:
		case NOTIFICATION_READY: {
			_ensure_shader_material();
			set_process(animated || manual_clock >= 0.0f);
			queue_redraw();
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			_ensure_shader_material();
			queue_redraw();
		} break;

		case NOTIFICATION_RESIZED: {
			_sync_shader_parameters();
			queue_redraw();
		} break;

		case NOTIFICATION_VISIBILITY_CHANGED: {
			set_process((animated || manual_clock >= 0.0f) && is_visible_in_tree());
			if (is_visible_in_tree()) {
				queue_redraw();
			}
		} break;

		case NOTIFICATION_PROCESS: {
			if (manual_clock >= 0.0f) {
				// Reloj propio: el mismo dibujo en dos procesos distintos (la
				// foto del splash y el agua del chat continuan sin salto).
				manual_clock += (float)get_process_delta_time();
				if (caustics_material.is_valid()) {
					caustics_material->set_shader_parameter("still_time", manual_clock);
				}
				queue_redraw();
			} else if (animated && is_visible_in_tree()) {
				queue_redraw();
			}
		} break;

		case NOTIFICATION_DRAW: {
			_ensure_shader_material();
			const Size2 draw_size = get_size();
			if (draw_size.x > 0.0f && draw_size.y > 0.0f) {
				draw_rect(Rect2(Point2(), draw_size), Color(1.0f, 1.0f, 1.0f, 1.0f));
			}
		} break;
	}
}

void LagoonicWaterCaustics::set_tint(const Color &p_tint) {
	if (tint == p_tint) {
		return;
	}
	tint = p_tint;
	_sync_shader_parameters();
	queue_redraw();
}

Color LagoonicWaterCaustics::get_tint() const {
	return tint;
}

void LagoonicWaterCaustics::set_band_height(float p_height) {
	const float clamped = MAX(1.0f, p_height);
	if (Math::is_equal_approx(band_height, clamped)) {
		return;
	}
	band_height = clamped;
	set_custom_minimum_size(Size2(0.0f, band_height));
	_sync_shader_parameters();
	queue_redraw();
}

float LagoonicWaterCaustics::get_band_height() const {
	return band_height;
}

void LagoonicWaterCaustics::set_animated(bool p_animated) {
	if (animated == p_animated) {
		return;
	}
	animated = p_animated;
	if (is_inside_tree()) {
		set_process((animated || manual_clock >= 0.0f) && is_visible_in_tree());
	}
	_sync_shader_parameters();
	queue_redraw();
}

bool LagoonicWaterCaustics::is_animated() const {
	return animated;
}

void LagoonicWaterCaustics::start_manual_clock(float p_time) {
	manual_clock = MAX(0.0f, p_time);
	_sync_shader_parameters();
	set_process(true);
	queue_redraw();
}

void LagoonicWaterCaustics::set_fill(float p_fill) {
	fill = CLAMP(p_fill, 0.0f, 1.0f);
	if (caustics_material.is_valid()) {
		caustics_material->set_shader_parameter("fill", fill);
	}
	queue_redraw();
}

void LagoonicWaterCaustics::set_fill_color(const Color &p_color) {
	fill_color = p_color;
	_sync_shader_parameters();
}

void LagoonicWaterCaustics::set_pattern_band(float p_px) {
	pattern_band = p_px;
	_sync_shader_parameters();
}

void LagoonicWaterCaustics::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_pattern_band", "px"), &LagoonicWaterCaustics::set_pattern_band);
	ClassDB::bind_method(D_METHOD("get_pattern_band"), &LagoonicWaterCaustics::get_pattern_band);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "pattern_band"), "set_pattern_band", "get_pattern_band");
	ClassDB::bind_method(D_METHOD("set_fill", "fill"), &LagoonicWaterCaustics::set_fill);
	ClassDB::bind_method(D_METHOD("get_fill"), &LagoonicWaterCaustics::get_fill);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fill", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_fill", "get_fill");

	ClassDB::bind_method(D_METHOD("set_tint", "tint"), &LagoonicWaterCaustics::set_tint);
	ClassDB::bind_method(D_METHOD("get_tint"), &LagoonicWaterCaustics::get_tint);

	ClassDB::bind_method(D_METHOD("set_band_height", "height"), &LagoonicWaterCaustics::set_band_height);
	ClassDB::bind_method(D_METHOD("get_band_height"), &LagoonicWaterCaustics::get_band_height);

	ClassDB::bind_method(D_METHOD("set_animated", "animated"), &LagoonicWaterCaustics::set_animated);
	ClassDB::bind_method(D_METHOD("is_animated"), &LagoonicWaterCaustics::is_animated);

	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "tint"), "set_tint", "get_tint");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "band_height", PROPERTY_HINT_RANGE, "1,1024,1,or_greater,suffix:px"), "set_band_height", "get_band_height");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "animated"), "set_animated", "is_animated");
}

LagoonicWaterCaustics::LagoonicWaterCaustics() {
	set_custom_minimum_size(Size2(0.0f, DEFAULT_BAND_HEIGHT));
	set_h_size_flags(Control::SIZE_EXPAND_FILL);
	set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	_ensure_shader_material();
}

void LagoonicWaterCaustics::handle_window_drag(const Ref<InputEvent> &p_event, Control *p_area) {
	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_null() || p_area == nullptr || mb->get_button_index() != MouseButton::LEFT || !mb->is_pressed()) {
		return;
	}
	Window *w = Object::cast_to<Window>(p_area->get_viewport());
	if (w == nullptr) {
		return;
	}
	DisplayServer *ds = DisplayServer::get_singleton();
	if (mb->is_double_click()) {
		if (ds->window_maximize_on_title_dbl_click()) {
			w->set_mode(w->get_mode() == Window::MODE_MAXIMIZED ? Window::MODE_WINDOWED : Window::MODE_MAXIMIZED);
		} else if (ds->window_minimize_on_title_dbl_click()) {
			w->set_mode(Window::MODE_MINIMIZED);
		}
		p_area->accept_event();
		return;
	}
	if (ds->has_feature(DisplayServerEnums::FEATURE_WINDOW_DRAG)) {
		ds->window_start_drag(w->get_window_id());
		p_area->accept_event();
	}
}
