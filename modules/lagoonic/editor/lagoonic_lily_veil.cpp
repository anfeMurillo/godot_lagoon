/**************************************************************************/
/*  lagoonic_lily_veil.cpp                                                */
/**************************************************************************/

#include "lagoonic_lily_veil.h"

#include "lagoonic_lily_data.h"
#include "lagoonic_vibe_theme.h"
#include "lagoonic_water_caustics.h"

#include "core/io/image.h"
#include "core/object/callable_mp.h"
#include "scene/animation/tween.h"
#include "scene/gui/texture_rect.h"
#include "scene/main/viewport.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "scene/resources/image_texture.h"

namespace {

Ref<Texture2D> _lily_texture() {
	static Ref<ImageTexture> texture;
	if (texture.is_null()) {
		Ref<Image> img;
		img.instantiate();
		Vector<uint8_t> bytes;
		bytes.resize(sizeof(lagoonic_lily_png));
		memcpy(bytes.ptrw(), lagoonic_lily_png, sizeof(lagoonic_lily_png));
		if (img->load_png_from_buffer(bytes) == OK) {
			texture = ImageTexture::create_from_image(img);
		}
	}
	return texture;
}

} // namespace

LagoonicLilyVeil::LagoonicLilyVeil() {
	set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	set_mouse_filter(Control::MOUSE_FILTER_STOP); // Sin clics durante la transicion.

	water = memnew(LagoonicWaterCaustics);
	water->set_band_height(1.0f);
	water->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	water->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	add_child(water);

	// Mismo tamano y centro que el splash (Godot lo pinta a tamano real,
	// centrado en la ventana).
	Ref<Texture2D> tex = _lily_texture();
	const Size2 lily_size = tex.is_valid() ? tex->get_size() : Size2(340, 141);
	lily_holder = memnew(Control);
	lily_holder->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	lily_holder->set_anchors_preset(Control::PRESET_CENTER);
	lily_holder->set_offset(SIDE_LEFT, -lily_size.x * 0.5f);
	lily_holder->set_offset(SIDE_TOP, -lily_size.y * 0.5f);
	lily_holder->set_offset(SIDE_RIGHT, lily_size.x * 0.5f);
	lily_holder->set_offset(SIDE_BOTTOM, lily_size.y * 0.5f);
	add_child(lily_holder);

	lily = memnew(TextureRect);
	lily->set_texture(tex);
	lily->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	lily->set_size(lily_size);
	lily->set_pivot_offset(lily_size * 0.5f);
	lily_holder->add_child(lily);
}

void LagoonicLilyVeil::_set_lily_offset(float p_y) {
	if (lily != nullptr) {
		lily->set_position(Point2(0, p_y));
	}
}

void LagoonicLilyVeil::_stop_bobbing() {
	if (bob_tween.is_valid()) {
		bob_tween->kill();
		bob_tween.unref();
	}
}

void LagoonicLilyVeil::cover_now(float p_pattern_band, float p_clock) {
	const LagoonicVibeTheme::Palette p = LagoonicVibeTheme::current();
	pattern_band = p_pattern_band;
	water->set_tint(Color(p.accent, 0.85f));
	water->set_fill_color(LagoonicVibeTheme::water_fill_color());
	water->set_pattern_band(p_pattern_band);
	water->start_manual_clock(p_clock);
	water->set_fill(1.0f);
	lily->set_modulate(Color(1, 1, 1, 1));
	_set_lily_offset(0);
	lily->set_rotation(0);
}

void LagoonicLilyVeil::start_bobbing() {
	_stop_bobbing();
	// Flota: sube y baja unos pixeles con un leve vaiven, sin fin.
	bob_tween = create_tween();
	bob_tween->set_loops(0); // 0 = sin fin.
	bob_tween->tween_method(callable_mp(this, &LagoonicLilyVeil::_set_lily_offset), 0.0f, -6.0f, 0.9)->set_trans(Tween::TRANS_SINE)->set_ease(Tween::EASE_IN_OUT);
	bob_tween->parallel()->tween_property(lily, NodePath("rotation"), Math::deg_to_rad(-1.2f), 0.9)->set_trans(Tween::TRANS_SINE)->set_ease(Tween::EASE_IN_OUT);
	bob_tween->tween_method(callable_mp(this, &LagoonicLilyVeil::_set_lily_offset), -6.0f, 0.0f, 0.9)->set_trans(Tween::TRANS_SINE)->set_ease(Tween::EASE_IN_OUT);
	bob_tween->parallel()->tween_property(lily, NodePath("rotation"), Math::deg_to_rad(1.2f), 0.9)->set_trans(Tween::TRANS_SINE)->set_ease(Tween::EASE_IN_OUT);
}

void LagoonicLilyVeil::reveal(Control *p_content, float p_target_band) {
	_stop_bobbing();
	set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	const float y = lily->get_position().y;
	Ref<Tween> tween = create_tween();
	tween->set_parallel(true);
	// 1) El nenufar se hunde y se desvanece.
	tween->tween_method(callable_mp(this, &LagoonicLilyVeil::_set_lily_offset), y, 22.0f, 0.32)->set_trans(Tween::TRANS_SINE)->set_ease(Tween::EASE_IN);
	tween->tween_property(lily, NodePath("modulate:a"), 0.0f, 0.28)->set_trans(Tween::TRANS_SINE)->set_ease(Tween::EASE_IN);
	tween->tween_property(lily, NodePath("rotation"), 0.0f, 0.32);
	// 2) Mientras se hunde, el agua empieza a retirarse hacia arriba.
	tween->tween_property(water, NodePath("fill"), 0.0f, 0.6)->set_delay(0.2)->set_trans(Tween::TRANS_CUBIC)->set_ease(Tween::EASE_OUT);
	tween->tween_property(water, NodePath("modulate:a"), 0.0f, 0.15)->set_delay(0.65);
	if (p_target_band > 0.0f && !Math::is_equal_approx(p_target_band, pattern_band)) {
		// La franja de la caustica llega a la altura de la pantalla nueva.
		tween->tween_property(water, NodePath("pattern_band"), p_target_band, 0.6)->set_delay(0.2)->set_trans(Tween::TRANS_CUBIC)->set_ease(Tween::EASE_OUT);
	}
	// 3) El contenido aparece un poco despues que el agua.
	if (p_content != nullptr) {
		tween->tween_property(p_content, NodePath("modulate:a"), 1.0f, 0.35)->set_delay(0.32)->set_trans(Tween::TRANS_CUBIC)->set_ease(Tween::EASE_OUT);
	}
	tween->chain()->tween_callback(callable_mp((Node *)this, &Node::queue_free));
}
void LagoonicLilyVeil::cover(float p_pattern_band, const Callable &p_done) {
	_stop_bobbing();
	cover_now(p_pattern_band);
	water->set_fill(0.0f);
	lily->set_modulate(Color(1, 1, 1, 0));
	_set_lily_offset(18.0f);
	Ref<Tween> tween = create_tween();
	// 1) El agua baja y cubre todo.
	tween->tween_property(water, NodePath("fill"), 1.0f, 0.38)->set_trans(Tween::TRANS_SINE)->set_ease(Tween::EASE_IN);
	// 2) El nenufar sale a flote en el centro (donde estara en el splash).
	tween->tween_method(callable_mp(this, &LagoonicLilyVeil::_set_lily_offset), 18.0f, 0.0f, 0.3)->set_trans(Tween::TRANS_CUBIC)->set_ease(Tween::EASE_OUT);
	tween->parallel()->tween_property(lily, NodePath("modulate:a"), 1.0f, 0.25)->set_trans(Tween::TRANS_SINE)->set_ease(Tween::EASE_OUT);
	tween->tween_interval(0.08);
	// Foto del instante final: sera la pantalla de carga del proceso nuevo.
	tween->tween_callback(callable_mp(this, &LagoonicLilyVeil::save_handoff_now));
	if (p_done.is_valid()) {
		tween->tween_callback(p_done);
	}
}

void LagoonicLilyVeil::save_handoff_now() {
	Viewport *vp = get_viewport();
	if (vp == nullptr || vp->get_texture().is_null()) {
		return;
	}
	LagoonicVibeTheme::save_handoff(vp->get_texture()->get_image(), water->get_manual_clock(), pattern_band);
}

void LagoonicLilyVeil::render_snapshot(Node *p_host, const Size2i &p_size, float p_pattern_band) {
	if (p_host == nullptr || !p_host->is_inside_tree() || p_size.x <= 0 || p_size.y <= 0) {
		return;
	}
	SubViewport *svp = memnew(SubViewport);
	svp->set_size(p_size);
	svp->set_disable_3d(true);
	svp->set_transparent_background(false);
	svp->set_update_mode(SubViewport::UPDATE_ALWAYS);
	LagoonicLilyVeil *veil = memnew(LagoonicLilyVeil);
	svp->add_child(veil);
	// En una subvista no hay Control padre: tamano fijo en vez de anclas.
	veil->set_anchors_and_offsets_preset(Control::PRESET_TOP_LEFT);
	veil->set_position(Point2());
	veil->set_size(Size2(p_size));
	p_host->add_child(svp);
	veil->cover_now(p_pattern_band, 0.0f);
	_finish_snapshot(svp, p_pattern_band, 3);
}

void LagoonicLilyVeil::_finish_snapshot(Object *p_viewport, float p_band, int p_frames_left) {
	SubViewport *svp = Object::cast_to<SubViewport>(p_viewport);
	if (svp == nullptr || !svp->is_inside_tree()) {
		return;
	}
	if (p_frames_left > 0) {
		// Unos fotogramas para que la subvista se dibuje.
		svp->get_tree()->connect(SNAME("process_frame"), callable_mp_static(&LagoonicLilyVeil::_finish_snapshot).bind(p_viewport, p_band, p_frames_left - 1), CONNECT_ONE_SHOT);
		return;
	}
	LagoonicVibeTheme::save_handoff(svp->get_texture()->get_image(), 0.0f, p_band);
	svp->queue_free();
}
