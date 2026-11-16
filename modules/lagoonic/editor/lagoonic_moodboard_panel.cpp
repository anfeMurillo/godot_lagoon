/**************************************************************************/
/*  lagoonic_moodboard_panel.cpp                                          */
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

#include "lagoonic_moodboard_panel.h"

#include "lagoonic_vibe_theme.h"

#include "core/config/project_settings.h"
#include "core/input/input_event.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/image.h"
#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/keyboard.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "core/templates/sort_array.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/themes/editor_scale.h"
#include "lagoonic_chat_view.h"
#include "modules/modules_enabled.gen.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/visual_instance_3d.h"
#include "scene/3d/world_environment.h"
#include "scene/audio/audio_stream_player.h"
#include "scene/main/viewport.h"
#include "scene/resources/audio_stream_wav.h"
#include "scene/resources/environment.h"
#ifdef MODULE_GLTF_ENABLED
#include "modules/gltf/extensions/gltf_document_extension_convert_importer_mesh.h"
#include "modules/gltf/gltf_document.h"
#include "scene/3d/importer_mesh_instance_3d.h"
#endif
#include "core/object/method_bind.h"
#include "scene/gui/button.h"
#include "scene/gui/check_box.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/option_button.h"
#include "scene/gui/popup_menu.h"
#include "scene/main/timer.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/label.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/text_edit.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "scene/resources/font.h"
#include "scene/resources/image_texture.h"
#include "servers/display/display_server.h"

// The motion, as tuned on flyshare.design (raum.js TUNE).
static const float SPIN = 6.0f; // Degrees per second of its own.
static const float DRAG = 0.32f; // Degrees per pixel dragged.
static const float DRAG_PITCH = 0.16f;
static const float FRICTION = 1.4f; // Seconds back to the own spin.
static const float TILT_BACK = 1.6f; // Seconds back to the rest tilt.
static const float PERSP = 3.4f; // Focal length, in radii.
static const float TILT_RING = 0.33f; // 19 degrees.
static const float TILT_SPHERE = 0.12f;
static const float FOG = 0.28f; // Opacity the farthest card loses.
static const float MORPH = 1.25f; // Seconds each card flies.
static const float MORPH_STAG = 0.045f; // Seconds between two cards.
static const float DEAL = 1.3f;
static const float DEAL_STAG = 0.08f;
static const float FOCUS_IN = 0.8f;

static const float D2R = Math::PI / 180.0f;
static const float GOLD = Math::PI * (3.0f - 2.2360679775f);

static float _clampf(float x, float a, float b) {
	return MIN(b, MAX(a, x));
}

// Arrival with mass: smootherstep, slow start and slow landing.
static float _land(float u) {
	u = _clampf(u, 0.0f, 1.0f);
	return u * u * u * (u * (6.0f * u - 15.0f) + 10.0f);
}

static float _hash(int i, int k) {
	const float x = Math::sin(i * 127.1f + k * 311.7f) * 43758.5453f;
	return x - Math::floor(x);
}

static const char *IMAGE_EXTS[] = { "png", "jpg", "jpeg", "webp", "bmp", "tga", "svg", nullptr };
static const char *VIDEO_EXTS[] = { "mp4", "mov", "m4v", "webm", "ogv", "mkv", "avi", "gif", nullptr };
static const char *AUDIO_EXTS[] = { "wav", "ogg", "mp3", "flac", "m4a", "aac", nullptr };
static const char *MODEL_EXTS[] = { "glb", "gltf", "fbx", "obj", nullptr };

static bool _ext_in(const String &p_ext, const char **p_list) {
	for (int i = 0; p_list[i] != nullptr; i++) {
		if (p_ext == p_list[i]) {
			return true;
		}
	}
	return false;
}

// ================================================================ canvas

LagoonicMoodboardCanvas::LagoonicMoodboardCanvas() {
	set_clip_contents(true);
	set_focus_mode(FOCUS_CLICK);
	set_mouse_filter(MOUSE_FILTER_STOP);
	set_v_size_flags(SIZE_EXPAND_FILL);
	set_h_size_flags(SIZE_EXPAND_FILL);
	set_custom_minimum_size(Size2(0, 180 * EDSCALE));
	shadow_box.instantiate();
	shadow_box->set_draw_center(false);
	card_box.instantiate();
	edge_box.instantiate();
	edge_box->set_draw_center(false);
}

void LagoonicMoodboardCanvas::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_focus", "index"), &LagoonicMoodboardCanvas::set_focus);
	ClassDB::bind_method(D_METHOD("enter_group", "id"), &LagoonicMoodboardCanvas::enter_group);
	ClassDB::bind_method(D_METHOD("exit_group"), &LagoonicMoodboardCanvas::exit_group);
	ADD_SIGNAL(MethodInfo("board_changed"));
	ADD_SIGNAL(MethodInfo("focus_changed"));
	ADD_SIGNAL(MethodInfo("files_dropped", PropertyInfo(Variant::PACKED_STRING_ARRAY, "files")));
	ADD_SIGNAL(MethodInfo("note_edit_requested", PropertyInfo(Variant::INT, "index")));
	ADD_SIGNAL(MethodInfo("paste_requested"));
	ADD_SIGNAL(MethodInfo("selection_changed"));
	ADD_SIGNAL(MethodInfo("group_changed"));
	ADD_SIGNAL(MethodInfo("model_turn", PropertyInfo(Variant::VECTOR2, "relative")));
	ADD_SIGNAL(MethodInfo("model_zoom", PropertyInfo(Variant::FLOAT, "factor")));
	ADD_SIGNAL(MethodInfo("model_grab", PropertyInfo(Variant::BOOL, "grab")));
}

void LagoonicMoodboardCanvas::_reslot() {
	// Slots go to the cards of the level on view; each group knows its cards.
	members.clear();
	int n = 0;
	for (uint32_t i = 0; i < items.size(); i++) {
		Item &it = items[i];
		if (!it.group.is_empty()) {
			members[it.group].push_back(i);
		}
		if (_visible(it)) {
			it.slot = n++;
		}
	}
	shown_count = n;
	if (!current_group.is_empty() && _group_index(current_group) < 0) {
		current_group = String(); // The group is gone.
		_reslot();
	}
}

int LagoonicMoodboardCanvas::_group_index(const String &p_id) const {
	for (uint32_t i = 0; i < items.size(); i++) {
		if (items[i].kind == "group" && items[i].id == p_id) {
			return i;
		}
	}
	return -1;
}

void LagoonicMoodboardCanvas::enter_group(const String &p_id) {
	const int g = _group_index(p_id);
	if (g < 0 || current_group == p_id) {
		return;
	}
	set_focus(-1);
	clear_selection();
	// The group's cards are dealt out of its stack.
	const Vector3 origin = items[g].rs;
	const float origin_z = items[g].rz;
	current_group = p_id;
	_reslot();
	_layout_grid();
	for (Item &it : items) {
		if (_visible(it)) {
			it.from = Vector3(origin.x, origin.y, origin.z * 0.8f);
			it.from_z = origin_z;
			it.mt = -0.05f - it.slot * DEAL_STAG;
			it.morph_len = DEAL * 0.8f;
			it.rs = it.from;
		}
	}
	emit_signal(SNAME("group_changed"));
	queue_redraw();
}

void LagoonicMoodboardCanvas::exit_group() {
	if (current_group.is_empty()) {
		return;
	}
	set_focus(-1);
	clear_selection();
	const String left = current_group;
	// One level up (groups do not nest yet: the top of the board).
	const int g = _group_index(left);
	current_group = g >= 0 ? items[g].group : String();
	_reslot();
	_layout_grid();
	// Everything comes back out of the stack that was open.
	Vector3 origin(get_size().x * 0.5f, get_size().y * 0.5f, 0.6f);
	float origin_z = 0.0f;
	if (g >= 0) {
		_screen(items[g], origin, origin_z);
	}
	for (Item &it : items) {
		if (_visible(it)) {
			const bool stack = it.id == left;
			it.from = stack ? Vector3(origin.x, origin.y, origin.z * 1.3f) : Vector3(origin.x, origin.y, origin.z * 0.7f);
			it.from_z = origin_z + (stack ? -1.0f : 1.0f);
			it.mt = stack ? 0.0f : -0.1f - it.slot * MORPH_STAG;
			it.morph_len = stack ? 0.6f : MORPH;
			it.rs = it.from;
		}
	}
	emit_signal(SNAME("group_changed"));
	queue_redraw();
}

Vector<String> LagoonicMoodboardCanvas::get_selection() const {
	Vector<String> out;
	for (const Item &it : items) {
		if (it.selected) {
			out.push_back(it.id);
		}
	}
	return out;
}

void LagoonicMoodboardCanvas::clear_selection() {
	bool any = false;
	for (Item &it : items) {
		any = any || it.selected;
		it.selected = false;
	}
	if (any) {
		emit_signal(SNAME("selection_changed"));
		queue_redraw();
	}
}

float LagoonicMoodboardCanvas::_side() const {
	const Size2 size = get_size();
	const float n = MAX(1.0f, float(shown_count));
	// Fewer cards, bigger cards; the sphere and the ring keep their room.
	const float k = n <= 6 ? 0.2f : (n <= 14 ? 0.16f : 0.13f);
	return _clampf(MIN(size.x, size.y * 1.3f) * k, 40.0f * EDSCALE, 140.0f * EDSCALE);
}

float LagoonicMoodboardCanvas::_radius() const {
	const Size2 size = get_size();
	return MAX(50.0f * EDSCALE, MIN(size.x * 0.34f, size.y * 0.62f));
}

Vector2 LagoonicMoodboardCanvas::_card_size(const Item &p_item) const {
	// Every card has the same area (a 4:5 card's); only the shape changes.
	const float side = _side();
	const float area = side * side * 1.25f;
	const float ar = _clampf(p_item.aspect, 0.4f, 2.5f);
	return Vector2(Math::sqrt(area * ar), Math::sqrt(area / ar));
}

void LagoonicMoodboardCanvas::_layout_grid() {
	// Same cells at a fixed gap; a wide card takes two. The column count
	// that gives the biggest cells and fits wins; rows are centred.
	const Size2 size = get_size();
	LocalVector<int> shown;
	for (uint32_t i = 0; i < items.size(); i++) {
		if (_visible(items[i])) {
			shown.push_back(i);
		}
	}
	const int n = shown.size();
	if (n == 0 || size.x <= 0 || size.y <= 0) {
		return;
	}
	const float gap = 10.0f * EDSCALE, margin = 14.0f * EDSCALE;
	const float aw = size.x - 2 * margin, ah = size.y - 2 * margin;
	float best_w = -1.0f;
	int best_cols = 1;
	Vector<Vector<int>> best_rows;
	for (int cols = 1; cols <= MAX(1, n * 2); cols++) {
		Vector<Vector<int>> rows;
		rows.push_back(Vector<int>());
		int used = 0;
		LocalVector<int> queue;
		for (int idx : shown) {
			queue.push_back(idx);
		}
		while (!queue.is_empty()) {
			int pick = -1;
			for (uint32_t q = 0; q < queue.size(); q++) {
				const int units = items[queue[q]].aspect > 1.1f && cols > 1 ? 2 : 1;
				if (used + units <= cols) {
					pick = q;
					break;
				}
			}
			if (pick < 0) {
				rows.push_back(Vector<int>());
				used = 0;
				continue;
			}
			const int idx = queue[pick];
			queue.remove_at(pick);
			rows.write[rows.size() - 1].push_back(idx);
			used += items[idx].aspect > 1.1f && cols > 1 ? 2 : 1;
			if (used >= cols && !queue.is_empty()) {
				rows.push_back(Vector<int>());
				used = 0;
			}
		}
		const float w = MIN((aw - (cols - 1) * gap) / cols, (ah - (rows.size() - 1) * gap) / rows.size() / 1.25f);
		if (w > best_w + 0.5f) {
			best_w = w;
			best_cols = cols;
			best_rows = rows;
		}
	}
	(void)best_cols;
	const float w = MAX(8.0f, MIN(best_w, _side() * 1.5f));
	const float h = w * 1.25f;
	const float total_h = best_rows.size() * h + (best_rows.size() - 1) * gap;
	float y = (size.y - total_h) * 0.5f;
	for (const Vector<int> &row : best_rows) {
		int units = 0;
		for (int idx : row) {
			units += items[idx].aspect > 1.1f && best_cols > 1 ? 2 : 1;
		}
		const float rw = units * w + (units - 1) * gap;
		float x = (size.x - rw) * 0.5f;
		for (int idx : row) {
			Item &it = items[idx];
			const bool wide = it.aspect > 1.1f && best_cols > 1;
			const float cw = wide ? 2 * w + gap : w;
			const Vector2 card = _card_size(it);
			it.grid_pos = Vector2(x + cw * 0.5f, y + h * 0.5f);
			it.grid_scale = MIN(cw / card.x, h / card.y);
			x += cw + gap;
		}
		y += h + gap;
	}
}

void LagoonicMoodboardCanvas::_screen(const Item &p_item, Vector3 &r_pos, float &r_z) const {
	const Size2 size = get_size();
	if (view == VIEW_GRID) {
		r_pos = Vector3(p_item.grid_pos.x, p_item.grid_pos.y, p_item.grid_scale);
		r_z = 0.0f;
		return;
	}
	if (view == VIEW_FREE) {
		const Vector2 p = size * 0.5f + pan + p_item.free_pos * zoom;
		r_pos = Vector3(p.x, p.y, zoom);
		r_z = 0.0f;
		return;
	}
	const int n = MAX(1, shown_count);
	const int k = p_item.slot;
	const float R = _radius();
	// A breath of its own height for each card.
	const float bob = 4.0f * EDSCALE * Math::sin(Math::TAU * 0.08f * (1.0f + _hash(k, 4)) * time + Math::TAU * _hash(k, 2));
	Vector3 p;
	if (view == VIEW_SPHERE) {
		// Fibonacci sphere, flattened to fit between the top and the bottom.
		const float r = R * 0.82f;
		const float ry = MIN(r, get_size().y * 0.34f);
		const float y = 1.0f - 2.0f * (k + 0.5f) / n;
		const float rr = Math::sqrt(MAX(0.0f, 1.0f - y * y));
		const float th = k * GOLD;
		p = Vector3(r * rr * Math::sin(th), ry * y + bob, -r * rr * Math::cos(th));
	} else {
		// The ring: evenly spread, the first card in front.
		const float a = Math::TAU * k / n;
		p = Vector3(R * Math::sin(a), bob, -R * Math::cos(a));
	}
	const float cy = Math::cos(yaw), sy = Math::sin(yaw), cp = Math::cos(pitch), sp = Math::sin(pitch);
	const float x1 = p.x * cy + p.z * sy;
	const float z1 = -p.x * sy + p.z * cy;
	const float y2 = p.y * cp - z1 * sp;
	const float z2 = p.y * sp + z1 * cp;
	const float F = PERSP * R;
	const float s = F / (F + z2);
	// A little smaller than in the grid: the shapes need room to turn.
	const float shrink = view == VIEW_SPHERE ? 0.72f : 0.82f;
	r_pos = Vector3(size.x * 0.5f + x1 * s, size.y * 0.5f + y2 * s, s * shrink);
	r_z = z2;
}

Rect2 LagoonicMoodboardCanvas::get_details_rect() const {
	const Size2 size = get_size();
	const float m = 14.0f * EDSCALE;
	if (size.x >= 460.0f * EDSCALE) {
		return Rect2(size.x * 0.62f, m, size.x * 0.38f - m, size.y - 2 * m);
	}
	return Rect2(m, size.y * 0.66f, size.x - 2 * m, size.y * 0.34f - m);
}

Rect2 LagoonicMoodboardCanvas::_focus_rect(const Item &p_item) const {
	const Size2 size = get_size();
	const float m = 14.0f * EDSCALE;
	Rect2 area;
	if (size.x >= 460.0f * EDSCALE) {
		area = Rect2(m, m, size.x * 0.62f - 2 * m, size.y - 2 * m);
	} else {
		area = Rect2(m, m, size.x - 2 * m, size.y * 0.66f - 2 * m);
	}
	const float ar = _clampf(p_item.aspect, 0.2f, 5.0f);
	Vector2 fit(area.size.x, area.size.x / ar);
	if (fit.y > area.size.y) {
		fit = Vector2(area.size.y * ar, area.size.y);
	}
	return Rect2(area.position + (area.size - fit) * 0.5f, fit);
}

Vector2 LagoonicMoodboardCanvas::free_center() const {
	return -pan / MAX(0.01f, zoom);
}

void LagoonicMoodboardCanvas::set_view(View p_view, bool p_animate) {
	if (p_view == view) {
		return;
	}
	if (focus >= 0) {
		set_focus(-1);
	}
	view = p_view;
	if (view == VIEW_GRID) {
		_layout_grid();
	}
	for (Item &it : items) {
		if (p_animate) {
			it.from = it.rs;
			it.from_z = it.rz;
			it.mt = -it.slot * MORPH_STAG;
			it.morph_len = MORPH;
		}
	}
	queue_redraw();
}

void LagoonicMoodboardCanvas::set_focus(int p_index) {
	if (p_index >= int(items.size())) {
		p_index = -1;
	}
	if (focus == p_index) {
		return;
	}
	if (focus >= 0) {
		last_focus = focus;
	}
	focus = p_index;
	emit_signal(SNAME("focus_changed"));
	queue_redraw();
}

void LagoonicMoodboardCanvas::items_changed(int p_new_from) {
	_reslot();
	if (focus >= int(items.size())) {
		focus = -1;
	}
	_layout_grid();
	if (!laid_out) {
		return;
	}
	// Fresh cards fly out from the open card, or from the middle.
	Vector3 origin(get_size().x * 0.5f, get_size().y * 0.5f, 0.2f);
	if (p_new_from >= 0 && p_new_from < int(items.size())) {
		origin = items[p_new_from].rs;
	}
	int j = 0;
	for (Item &it : items) {
		if (it.rs.z == 0.0f && _visible(it)) {
			it.from = origin;
			it.from_z = -1.0f;
			it.mt = -(j++) * DEAL_STAG;
			it.morph_len = 0.9f;
		}
	}
	queue_redraw();
}

void LagoonicMoodboardCanvas::deal() {
	int first = -1;
	for (uint32_t i = 0; i < items.size(); i++) {
		if (_visible(items[i]) && items[i].slot == 0) {
			first = i;
		}
	}
	if (first < 0) {
		return;
	}
	Vector3 lead;
	float lead_z = 0.0f;
	_screen(items[first], lead, lead_z);
	for (Item &it : items) {
		if (!_visible(it)) {
			continue;
		}
		const int k = it.slot;
		it.from = k == 0 ? Vector3(lead.x, lead.y, lead.z * 0.6f) : lead;
		it.from_z = lead_z + 1.0f;
		it.mt = k == 0 ? 0.0f : -0.25f - (k - 1) * DEAL_STAG;
		it.morph_len = k == 0 ? 0.5f : DEAL;
		it.rs = it.from;
	}
}

int LagoonicMoodboardCanvas::_item_at(const Vector2 &p_pos, int p_except) const {
	if (focus >= 0 && focus < int(items.size()) && items[focus].hit.has_point(p_pos)) {
		return focus;
	}
	if (focus >= 0) {
		return -1; // Behind the veil.
	}
	// Front to back: the nearest card under the pointer wins.
	int best = -1;
	float best_z = 1e9f;
	for (uint32_t i = 0; i < items.size(); i++) {
		const Item &it = items[i];
		if (int(i) == p_except || !_visible(it) || it.alpha < 0.05f || !it.hit.has_point(p_pos)) {
			continue;
		}
		const float z = (view == VIEW_FREE || view == VIEW_GRID) ? -float(i) : it.hit_z;
		if (z < best_z) {
			best_z = z;
			best = i;
		}
	}
	return best;
}

String LagoonicMoodboardCanvas::get_tooltip(const Point2 &p_pos) const {
	const int i = _item_at(p_pos);
	if (i < 0 || i == focus) {
		return String();
	}
	const Item &it = items[i];
	if (it.kind == "group") {
		const LocalVector<int> *m = members.getptr(it.id);
		return vformat(String::utf8("%s · %d · tócalo para abrirlo"), it.text, m != nullptr ? int(m->size()) : 0);
	}
	return it.kind == "note" ? it.text.left(200) : it.file;
}

void LagoonicMoodboardCanvas::_step(float p_delta) {
	const float dt = MIN(p_delta, 0.1f);
	time += dt;
	const bool turning = view == VIEW_RING || view == VIEW_SPHERE;
	if (!(pressing && dragged) && turning) {
		const float spin = SPIN * (focus >= 0 ? 0.1f : 1.0f);
		yaw_speed += (spin - yaw_speed) * (1.0f - Math::exp(-dt / FRICTION));
		yaw += yaw_speed * D2R * dt;
		const float rest = view == VIEW_SPHERE ? TILT_SPHERE : TILT_RING;
		pitch += (rest - pitch) * (1.0f - Math::exp(-dt / TILT_BACK));
	}
	// The floor follows the ring's front (which goes left as yaw grows) and
	// the free view's pan.
	if (turning) {
		floor_x -= (yaw - floor_last_yaw) * 1.6f;
	} else if (view == VIEW_FREE) {
		const float f = MAX(1.0f, get_size().y * 0.6f);
		floor_x += (pan.x - floor_last_pan.x) / f * 2.0f;
		floor_z -= (pan.y - floor_last_pan.y) / f * 2.0f;
	}
	floor_last_yaw = yaw;
	floor_last_pan = pan;
	floor_x = Math::fposmod(floor_x, 1.0f);
	floor_z = Math::fposmod(floor_z, 1.0f);
	focus_k = focus >= 0 ? MIN(1.0f, focus_k + dt / FOCUS_IN) : MAX(0.0f, focus_k - dt / FOCUS_IN);
	if (focus < 0 && focus_k <= 0.0f) {
		last_focus = -1;
	}
	const float fe = _land(focus_k);
	const float R = _radius();
	for (uint32_t i = 0; i < items.size(); i++) {
		Item &it = items[i];
		if (!_visible(it)) {
			it.alpha = 0.0f;
			it.hit = Rect2();
			continue;
		}
		Vector3 P;
		float z = 0.0f;
		_screen(it, P, z);
		if (it.mt > -1e8f) {
			// Flying to its place in the new shape.
			it.mt += dt;
			const float mu = it.mt / it.morph_len;
			if (mu >= 1.0f) {
				it.mt = -1e9f;
			} else {
				const float u = _land(MAX(0.0f, mu));
				P = it.from.lerp(P, u);
				z = Math::lerp(it.from_z, z, u);
			}
		}
		const bool lit = hot == int(i) || drop_target == int(i);
		it.hot += ((lit ? (drop_target == int(i) ? 2.0f : 1.0f) : 0.0f) - it.hot) * MIN(1.0f, dt * 12.0f);
		const Vector2 card = _card_size(it);
		float s = P.z * (1.0f + 0.07f * it.hot);
		z -= 40.0f * it.hot;
		Vector2 center(P.x, P.y);
		Vector2 sz = card * s;
		const bool is_focus = int(i) == focus || (focus < 0 && int(i) == last_focus && focus_k > 0.0f);
		float fog = 1.0f;
		if (turning) {
			fog = 1.0f - FOG * _clampf((z + R) / (2.0f * R), 0.0f, 1.0f);
		}
		float alpha = fog;
		if (is_focus) {
			const Rect2 box = _focus_rect(it);
			center = center.lerp(box.get_center(), fe);
			sz = sz.lerp(box.size, fe);
			alpha = Math::lerp(fog, 1.0f, fe);
		} else if (fe > 0.0f) {
			sz *= 1.0f - 0.15f * fe;
			z += 320.0f * fe;
			alpha = fog * (1.0f - 0.9f * fe);
		}
		it.rs = P;
		it.rz = z;
		it.hit = Rect2(center - sz * 0.5f, sz);
		it.hit_z = is_focus ? -1e6f : z;
		it.alpha = alpha;
	}
	queue_redraw();
}

void LagoonicMoodboardCanvas::_draw_card(const Item &p_item, const Rect2 &p_rect, float p_alpha) {
	const LagoonicVibeTheme::Palette p = LagoonicVibeTheme::current();
	const float radius = MAX(2.0f, MIN(p_rect.size.x, p_rect.size.y) * 0.06f);
	// A soft shadow under every card (a cut-out has no card to cast it).
	if (!p_item.transparent) {
		shadow_box->set_corner_radius_all(int(radius));
		shadow_box->set_shadow_color(Color(0, 0, 0, 0.32f * p_alpha));
		shadow_box->set_shadow_size(int(MAX(2.0f, p_rect.size.y * 0.08f)));
		shadow_box->set_shadow_offset(Vector2(0, p_rect.size.y * 0.03f));
		draw_style_box(shadow_box, p_rect);
	}

	if (p_item.kind == "group") {
		// A stack of its first cards, the name on a band at the bottom.
		const LocalVector<int> *m = members.getptr(p_item.id);
		const int count = m != nullptr ? int(m->size()) : 0;
		const int layers = MIN(3, count);
		for (int j = layers - 1; j >= 0; j--) {
			const Item &member = items[(*m)[j]];
			if (member.kind == "group") {
				continue;
			}
			const Vector2 shift(p_rect.size.x * 0.07f * j, -p_rect.size.y * 0.06f * j);
			const Rect2 r(p_rect.position + shift + p_rect.size * 0.04f * j, p_rect.size * (1.0f - 0.08f * j));
			Item face = member;
			face.pinned = false;
			face.selected = false;
			face.hot = 0.0f;
			_draw_card(face, r, p_alpha * (1.0f - 0.22f * j));
		}
		if (layers == 0) {
			card_box->set_corner_radius_all(int(radius));
			card_box->set_bg_color(Color(0.16f, 0.16f, 0.18f, p_alpha));
			draw_style_box(card_box, p_rect);
		}
		const float band = MAX(16.0f, p_rect.size.y * 0.24f);
		const Rect2 strip(p_rect.position.x, p_rect.get_end().y - band, p_rect.size.x, band);
		card_box->set_corner_radius_all(0);
		card_box->set_corner_radius(CORNER_BOTTOM_LEFT, int(radius));
		card_box->set_corner_radius(CORNER_BOTTOM_RIGHT, int(radius));
		card_box->set_bg_color(Color(0.05f, 0.05f, 0.06f, 0.78f * p_alpha));
		draw_style_box(card_box, strip);
		const Ref<Font> font = get_theme_font(SceneStringName(font), SNAME("Label"));
		const int fs = int(_clampf(band * 0.36f, 6.0f, 18.0f * EDSCALE));
		if (font.is_valid() && p_rect.size.x > 30) {
			const float pad = p_rect.size.x * 0.07f;
			const String count_text = itos(count);
			const float cw = font->get_string_size(count_text, HORIZONTAL_ALIGNMENT_LEFT, -1, fs).x;
			const float base_y = strip.position.y + (band + font->get_ascent(fs) - font->get_descent(fs)) * 0.5f;
			draw_string(font, Vector2(strip.position.x + pad, base_y), p_item.text, HORIZONTAL_ALIGNMENT_LEFT, MAX(1.0f, p_rect.size.x - 3 * pad - cw), fs, Color(1, 1, 1, 0.95f * p_alpha), TextServer::JUSTIFICATION_NONE, TextServer::DIRECTION_AUTO, TextServer::ORIENTATION_HORIZONTAL);
			draw_string(font, Vector2(strip.get_end().x - pad - cw, base_y), count_text, HORIZONTAL_ALIGNMENT_LEFT, -1, fs, Color(1, 1, 1, 0.6f * p_alpha), TextServer::JUSTIFICATION_NONE, TextServer::DIRECTION_AUTO, TextServer::ORIENTATION_HORIZONTAL);
		}
	} else if (p_item.kind == "note") {
		card_box->set_corner_radius_all(int(radius));
		card_box->set_bg_color(Color(0.96f, 0.91f, 0.63f, p_alpha));
		draw_style_box(card_box, p_rect);
		const Ref<Font> font = get_theme_font(SceneStringName(font), SNAME("Label"));
		const float base = _card_size(p_item).x;
		const int font_size = int(_clampf(13.0f * EDSCALE * p_rect.size.x / MAX(1.0f, base), 6.0f, 40.0f * EDSCALE));
		if (font.is_valid() && font_size >= 6) {
			const float pad = p_rect.size.x * 0.1f;
			const int lines = MAX(1, int((p_rect.size.y - 2 * pad) / (font->get_height(font_size))));
			draw_multiline_string(font, p_rect.position + Vector2(pad, pad + font->get_ascent(font_size)), p_item.text, HORIZONTAL_ALIGNMENT_LEFT, p_rect.size.x - 2 * pad, font_size, lines, Color(0.29f, 0.24f, 0.04f, p_alpha), TextServer::BREAK_MANDATORY | TextServer::BREAK_WORD_BOUND | TextServer::BREAK_ADAPTIVE);
		}
	} else if (p_item.texture.is_valid()) {
		draw_texture_rect(p_item.texture, p_rect, false, Color(1, 1, 1, p_alpha));
	} else {
		// A video (or an image that could not be read): a dark card.
		card_box->set_corner_radius_all(int(radius));
		card_box->set_bg_color(Color(0.11f, 0.11f, 0.12f, p_alpha));
		draw_style_box(card_box, p_rect);
		const Vector2 c = p_rect.get_center() - Vector2(0, p_rect.size.y * 0.06f);
		const float r = MIN(p_rect.size.x, p_rect.size.y) * 0.16f;
		const Color ink(1, 1, 1, 0.85f * p_alpha);
		if (p_item.kind == "video") {
			draw_colored_polygon({ c + Vector2(-r * 0.6f, -r), c + Vector2(r, 0), c + Vector2(-r * 0.6f, r) }, ink);
		} else if (p_item.kind == "audio") {
			// A waveform of its own (from its id), centred.
			const int bars = 13;
			const float span = p_rect.size.x * 0.62f;
			const float bw = span / (bars * 1.6f);
			for (int b = 0; b < bars; b++) {
				const float h = r * (0.35f + 1.65f * _hash(p_item.id.hash() % 997 + b, 9)) * (1.0f - Math::abs(b - bars * 0.5f) / bars);
				const float x = c.x - span * 0.5f + b * bw * 1.6f;
				draw_rect(Rect2(x, c.y - h, bw, h * 2.0f), Color(p.accent.r, p.accent.g, p.accent.b, 0.9f * p_alpha));
			}
		} else if (p_item.kind == "model") {
			// A cube in outline while its preview renders (or for .obj).
			const Vector2 t(c.x, c.y - r), l(c.x - r * 0.87f, c.y - r * 0.5f), rr(c.x + r * 0.87f, c.y - r * 0.5f), m(c.x, c.y), bl(c.x - r * 0.87f, c.y + r * 0.5f), br(c.x + r * 0.87f, c.y + r * 0.5f), b(c.x, c.y + r);
			const float w = MAX(1.0f, r * 0.1f);
			draw_polyline({ t, rr, br, b, bl, l, t }, ink, w, true);
			draw_polyline({ l, m, rr }, ink, w, true);
			draw_line(m, b, ink, w, true);
		}
		const Ref<Font> font = get_theme_font(SceneStringName(font), SNAME("Label"));
		const int font_size = int(_clampf(p_rect.size.x * 0.09f, 6.0f, 18.0f * EDSCALE));
		if (font.is_valid() && p_rect.size.x > 40) {
			draw_string(font, p_rect.position + Vector2(p_rect.size.x * 0.08f, p_rect.size.y - p_rect.size.y * 0.08f), p_item.file.get_file(), HORIZONTAL_ALIGNMENT_LEFT, p_rect.size.x * 0.84f, font_size, Color(1, 1, 1, 0.7f * p_alpha), TextServer::JUSTIFICATION_NONE, TextServer::DIRECTION_AUTO, TextServer::ORIENTATION_HORIZONTAL);
		}
	}
	if (p_item.selected) {
		// Chosen: a thick accent edge (not on a cut-out) and a check.
		Color edge = p.accent;
		edge.a = p_alpha;
		if (!p_item.transparent) {
			edge_box->set_corner_radius_all(int(radius));
			edge_box->set_border_width_all(int(MAX(2.0f, 3.0f * EDSCALE)));
			edge_box->set_border_color(edge);
			draw_style_box(edge_box, p_rect.grow(2.0f * EDSCALE));
		}
		const float r = CLAMP(MIN(p_rect.size.x, p_rect.size.y) * 0.09f, 5.0f, 11.0f * EDSCALE);
		const Vector2 c = p_rect.position + Vector2(p_rect.size.x - r * 1.4f, r * 1.4f);
		draw_circle(c, r, edge);
		draw_polyline({ c + Vector2(-r * 0.45f, 0), c + Vector2(-r * 0.1f, r * 0.35f), c + Vector2(r * 0.5f, -r * 0.35f) }, p.on_accent, MAX(1.5f, r * 0.25f), true);
	} else if (p_item.transparent) {
		// A cut-out keeps its own outline: pinned shows as a dot.
		if (p_item.pinned) {
			const float r = CLAMP(MIN(p_rect.size.x, p_rect.size.y) * 0.05f, 3.0f, 6.0f * EDSCALE);
			draw_circle(p_rect.position + Vector2(p_rect.size.x - r * 1.5f, r * 1.5f), r, Color(p.accent.r, p.accent.g, p.accent.b, p_alpha));
		}
	} else if (p_item.pinned || p_item.hot > 0.02f) {
		// Pinned to the game's style: an accent edge (and on hover, faint).
		edge_box->set_corner_radius_all(int(radius));
		edge_box->set_border_width_all(int(MAX(1.0f, 2.0f * EDSCALE)));
		Color edge = p.accent;
		edge.a = (p_item.pinned ? 1.0f : 0.45f * p_item.hot) * p_alpha;
		edge_box->set_border_color(edge);
		draw_style_box(edge_box, p_rect.grow(1.0f * EDSCALE));
	}
}

void LagoonicMoodboardCanvas::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_VISIBILITY_CHANGED:
		case NOTIFICATION_ENTER_TREE: {
			set_process_internal(is_visible_in_tree());
		} break;
		case NOTIFICATION_RESIZED: {
			_layout_grid();
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			if (!laid_out && get_size().x > 10 && get_size().y > 10) {
				laid_out = true;
				_reslot();
				_layout_grid();
				deal();
			}
			if (laid_out) {
				_step(float(get_process_delta_time()));
			}
		} break;
		case NOTIFICATION_MOUSE_EXIT: {
			if (!pressing) {
				hot = -1;
			}
		} break;
		case NOTIFICATION_DRAW: {
			if (!laid_out) {
				break;
			}
			_draw_floor();
			// Far to near; in the free view and the grid, in order. The open
			// card goes last, over a veil.
			LocalVector<int> order;
			for (uint32_t i = 0; i < items.size(); i++) {
				if (int(i) != focus && !(focus < 0 && int(i) == last_focus && focus_k > 0.0f)) {
					order.push_back(i);
				}
			}
			struct ByDepth {
				const LocalVector<Item> *items;
				bool operator()(int a, int b) const { return (*items)[a].hit_z > (*items)[b].hit_z; }
			};
			if (view == VIEW_RING || view == VIEW_SPHERE) {
				SortArray<int, ByDepth> sorter;
				sorter.compare.items = &items;
				sorter.sort(order.ptr(), order.size());
			} else if (drag_item >= 0) {
				order.erase(drag_item);
				order.push_back(drag_item);
			}
			for (int i : order) {
				const Item &it = items[i];
				if (it.alpha > 0.01f) {
					_draw_card(it, it.hit, it.alpha);
				}
			}
			const int open = focus >= 0 ? focus : (focus_k > 0.0f ? last_focus : -1);
			if (open >= 0 && open < int(items.size())) {
				const LagoonicVibeTheme::Palette p = LagoonicVibeTheme::current();
				Color veil = p.bg;
				veil.a = 0.55f * _land(focus_k);
				draw_rect(Rect2(Vector2(), get_size()), veil);
				if (live_texture.is_valid() && open == focus && focus_k > 0.9f) {
					draw_texture_rect(live_texture, items[open].hit, false, Color(1, 1, 1, items[open].alpha));
				} else {
					_draw_card(items[open], items[open].hit, items[open].alpha);
				}
			}
		} break;
	}
}

void LagoonicMoodboardCanvas::_draw_floor() {
	// A floor seen from above: screen x = cx + f X / Z, y = horizon + f / Z
	// (eye 1 above it). Z = 1 is the bottom edge; lines fade toward the
	// horizon and toward the sides, like the chat's caustics.
	const Size2 size = get_size();
	if (size.x < 2.0f || size.y < 2.0f) {
		return;
	}
	const float cx = size.x * 0.5f;
	// Looking further down lifts the horizon.
	const float tilt = view == VIEW_GRID ? TILT_RING : pitch;
	const float hy = size.y * _clampf(0.52f - 0.3f * tilt, 0.12f, 0.7f);
	const float f = size.y - hy;
	const float z_far = 14.0f;
	const float g = 0.5f; // Cell, in floor units.
	Color base = LagoonicVibeTheme::current().accent;
	const float strength = view == VIEW_GRID ? 0.16f : 0.3f;
	const float width = MAX(1.0f, EDSCALE);

	auto fade = [&](float p_z, float p_sx) {
		// Near is bright; it melts long before the horizon (measured on the
		// screen, so the lines never pile up there); the middle is the focus.
		const float d = _clampf((1.0f / p_z - 1.0f / z_far) / (1.0f - 1.0f / z_far), 0.0f, 1.0f);
		// Brightest in the middle, still there at the sides' edges.
		const float side = _clampf(Math::abs(p_sx - cx) / (size.x * 0.5f), 0.0f, 1.0f);
		return strength * Math::pow(d, 1.6f) * (1.0f - 0.45f * side * side);
	};

	PackedVector2Array pts;
	PackedColorArray cols;
	const int steps = 10;
	// Lines that run away from the eye: every one meets at (cx, horizon).
	const float x_reach = (size.x * 0.55f) * z_far / f;
	const int n = MIN(80, int(Math::ceil(x_reach / g)) + 1);
	for (int i = -n; i <= n; i++) {
		const float X = (i + floor_x) * g;
		pts.clear();
		cols.clear();
		for (int k = 0; k <= steps; k++) {
			// More samples near the eye, where the fade changes most.
			const float Z = Math::pow(z_far, float(k) / steps);
			const Vector2 p(cx + f * X / Z, hy + f / Z);
			Color c = base;
			c.a = fade(Z, p.x);
			pts.push_back(p);
			cols.push_back(c);
		}
		draw_polyline_colors(pts, cols, width, true);
	}
	// Lines across: closer together toward the horizon.
	for (int k = 0;; k++) {
		const float Z = 1.0f + (k + 1.0f - floor_z) * g;
		// Stop where two lines would be a few pixels apart.
		if (Z > z_far || f * g / (Z * Z) < 6.0f * EDSCALE) {
			break;
		}
		const float y = hy + f / Z;
		pts.clear();
		cols.clear();
		const int across = 16;
		for (int j = 0; j <= across; j++) {
			const float sx = size.x * j / across;
			Color c = base;
			c.a = fade(Z, sx);
			pts.push_back(Vector2(sx, y));
			cols.push_back(c);
		}
		draw_polyline_colors(pts, cols, width, true);
	}
}

void LagoonicMoodboardCanvas::gui_input(const Ref<InputEvent> &p_event) {
	const Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid()) {
		const Vector2 pos = mb->get_position();
		if (mb->get_button_index() == MouseButton::LEFT) {
			if (mb->is_pressed()) {
				grab_focus();
				if (mb->is_double_click()) {
					const int i = _item_at(pos);
					if (i >= 0 && items[i].kind == "note") {
						emit_signal(SNAME("note_edit_requested"), i);
					}
					pressing = false;
					return;
				}
				if (focus >= 0 && live_texture.is_valid() && items[focus].hit.has_point(pos)) {
					// Over the open 3D model: dragging turns it.
					model_grab = true;
					last_pos = pos;
					emit_signal(SNAME("model_grab"), true);
					set_default_cursor_shape(CURSOR_DRAG);
					accept_event();
					return;
				}
				pressing = true;
				dragged = false;
				press_toggle = mb->is_shift_pressed() || mb->is_command_or_control_pressed();
				press_pos = pos;
				last_pos = pos;
				drag_item = (view == VIEW_FREE && focus < 0) ? _item_at(pos) : -1;
			} else if (model_grab) {
				model_grab = false;
				emit_signal(SNAME("model_grab"), false);
			} else if (pressing) {
				pressing = false;
				if (!dragged) {
					// A click: open the card (a group: deal it out), close the
					// open one; with Cmd/Shift, choose it.
					const int i = _item_at(pos);
					if (focus >= 0) {
						set_focus(-1);
					} else if (i >= 0 && press_toggle) {
						items[i].selected = !items[i].selected;
						emit_signal(SNAME("selection_changed"));
						queue_redraw();
					} else if (i >= 0 && items[i].kind == "group") {
						enter_group(items[i].id);
					} else if (i >= 0) {
						set_focus(i);
					}
				} else if (drag_item >= 0) {
					// Dropped on a group: it goes inside.
					if (drop_target >= 0 && items[drag_item].kind != "group") {
						items[drag_item].group = items[drop_target].id;
						items[drag_item].selected = false;
						_reslot();
						_layout_grid();
						emit_signal(SNAME("selection_changed"));
					}
					emit_signal(SNAME("board_changed"));
				}
				drag_item = -1;
				drop_target = -1;
			}
			accept_event();
			return;
		}
		if (mb->is_pressed() && (mb->get_button_index() == MouseButton::WHEEL_UP || mb->get_button_index() == MouseButton::WHEEL_DOWN)) {
			const float dir = mb->get_button_index() == MouseButton::WHEEL_UP ? 1.0f : -1.0f;
			if (focus >= 0 && live_texture.is_valid()) {
				emit_signal(SNAME("model_zoom"), dir > 0 ? 0.9f : 1.0f / 0.9f);
				accept_event();
				return;
			}
			if (view == VIEW_FREE) {
				const float old = zoom;
				zoom = _clampf(zoom * (dir > 0 ? 1.1f : 1.0f / 1.1f), 0.25f, 3.0f);
				// Keep the point under the pointer still.
				const Vector2 anchor = pos - get_size() * 0.5f;
				pan = anchor - (anchor - pan) * (zoom / old);
				queue_redraw();
			} else if (view == VIEW_RING || view == VIEW_SPHERE) {
				yaw_speed = _clampf(yaw_speed + dir * 18.0f * mb->get_factor(), -240.0f, 240.0f);
			}
			accept_event();
			return;
		}
	}

	const Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_valid()) {
		const Vector2 pos = mm->get_position();
		if (model_grab) {
			emit_signal(SNAME("model_turn"), pos - last_pos);
			last_pos = pos;
			accept_event();
			return;
		}
		if (pressing) {
			const Vector2 rel = pos - last_pos;
			last_pos = pos;
			if (!dragged && pos.distance_to(press_pos) > 4.0f * EDSCALE) {
				dragged = true;
			}
			if (dragged && focus < 0) {
				if (view == VIEW_RING || view == VIEW_SPHERE) {
					yaw += rel.x * DRAG * D2R;
					pitch = _clampf(pitch + rel.y * DRAG_PITCH * D2R, -0.4f, 1.2f);
					// The swing goes on after letting go.
					yaw_speed = _clampf(rel.x * DRAG * 60.0f, -240.0f, 240.0f);
				} else if (view == VIEW_FREE) {
					if (drag_item >= 0) {
						items[drag_item].free_pos += rel / MAX(0.01f, zoom);
						const int under = _item_at(pos, drag_item);
						drop_target = (under >= 0 && items[under].kind == "group") ? under : -1;
					} else {
						pan += rel;
					}
				}
			}
		} else {
			const int h = _item_at(pos);
			if (h != hot) {
				hot = h;
				const bool turnable = h >= 0 && h == focus && live_texture.is_valid();
				set_default_cursor_shape(turnable ? CURSOR_DRAG : (h >= 0 ? CURSOR_POINTING_HAND : CURSOR_ARROW));
			}
		}
		return;
	}

	const Ref<InputEventPanGesture> pg = p_event;
	if (pg.is_valid()) {
		if (view == VIEW_FREE) {
			pan -= pg->get_delta() * 12.0f;
		} else if (view == VIEW_RING || view == VIEW_SPHERE) {
			yaw_speed = _clampf(yaw_speed + pg->get_delta().x * 30.0f, -240.0f, 240.0f);
		}
		accept_event();
		return;
	}
	const Ref<InputEventMagnifyGesture> mg = p_event;
	if (mg.is_valid() && focus >= 0 && live_texture.is_valid()) {
		emit_signal(SNAME("model_zoom"), 1.0f / mg->get_factor());
		accept_event();
		return;
	}
	if (mg.is_valid() && view == VIEW_FREE) {
		zoom = _clampf(zoom * mg->get_factor(), 0.25f, 3.0f);
		accept_event();
		return;
	}

	const Ref<InputEventKey> k = p_event;
	if (k.is_valid() && k->is_pressed()) {
		if (k->get_keycode() == Key::ESCAPE) {
			if (focus >= 0) {
				set_focus(-1);
			} else if (!get_selection().is_empty()) {
				clear_selection();
			} else {
				exit_group();
			}
			accept_event();
		} else if (k->get_keycode() == Key::V && k->is_command_or_control_pressed()) {
			emit_signal(SNAME("paste_requested"));
			accept_event();
		}
	}
}

bool LagoonicMoodboardCanvas::can_drop_data(const Point2 &p_point, const Variant &p_data) const {
	const Dictionary d = p_data;
	return String(d.get("type", "")) == "files";
}

void LagoonicMoodboardCanvas::drop_data(const Point2 &p_point, const Variant &p_data) {
	const Dictionary d = p_data;
	const Vector<String> files = d.get("files", Vector<String>());
	PackedStringArray out;
	for (const String &f : files) {
		out.push_back(f);
	}
	emit_signal(SNAME("files_dropped"), out);
}

// ================================================================ panel

// Where the n-th new card lands in the free view: a loose golden spiral, so
// a pile of new references does not stack on one spot.
static Vector2 _spread(int p_n) {
	const float a = p_n * GOLD;
	const float r = 78.0f * Math::sqrt(float(p_n));
	return Vector2(Math::cos(a), Math::sin(a) * 0.7f) * r + Vector2((_hash(p_n, 1) - 0.5f) * 20.0f, (_hash(p_n, 3) - 0.5f) * 20.0f);
}

void LagoonicMoodboardPanel::_bind_methods() {
	ClassDB::bind_method(D_METHOD("add_files", "files"), &LagoonicMoodboardPanel::add_files);
	ClassDB::bind_method(D_METHOD("add_note", "text"), &LagoonicMoodboardPanel::add_note);
}

String LagoonicMoodboardPanel::_unique_file(const String &p_name) const {
	String base = p_name.get_basename().validate_filename();
	if (base.is_empty()) {
		base = "referencia";
	}
	const String ext = p_name.get_extension().to_lower();
	String name = base + (ext.is_empty() ? "" : "." + ext);
	int n = 2;
	while (FileAccess::exists(board_dir.path_join(name))) {
		name = base + "-" + itos(n++) + (ext.is_empty() ? "" : "." + ext);
	}
	return name;
}

void LagoonicMoodboardPanel::_load_texture(LagoonicMoodboardCanvas::Item &r_item) {
	r_item.texture.unref();
	r_item.transparent = false;
	if (r_item.kind == "model") {
		r_item.aspect = 1.0f;
		const String thumb = board_dir.path_join(".thumbs").path_join(r_item.file + ".png");
		Ref<Image> cached;
		cached.instantiate();
		if (FileAccess::exists(thumb) && cached->load(thumb) == OK && !cached->is_empty()) {
			r_item.texture = ImageTexture::create_from_image(cached);
			r_item.transparent = true;
		} else if ((r_item.file.get_extension().to_lower() == "glb" || r_item.file.get_extension().to_lower() == "gltf") && !thumb_queue.has(r_item.id)) {
			thumb_queue.push_back(r_item.id);
		}
		return;
	}
	if (r_item.kind != "image") {
		r_item.aspect = r_item.kind == "note" ? 1.0f : (r_item.kind == "group" ? 0.8f : (r_item.kind == "audio" ? 1.6f : 16.0f / 9.0f));
		return;
	}
	Ref<Image> image;
	image.instantiate();
	if (image->load(board_dir.path_join(r_item.file)) != OK || image->is_empty()) {
		r_item.aspect = 0.8f;
		return;
	}
	r_item.aspect = float(image->get_width()) / MAX(1, image->get_height());
	// A light copy for the board, with rounded corners baked in.
	const int max_side = 640;
	if (MAX(image->get_width(), image->get_height()) > max_side) {
		const float k = float(max_side) / MAX(image->get_width(), image->get_height());
		image->resize(MAX(1, int(image->get_width() * k)), MAX(1, int(image->get_height() * k)), Image::INTERPOLATE_LANCZOS);
	}
	image->decompress();
	image->convert(Image::FORMAT_RGBA8);
	// A cut-out (a sprite with a see-through background) stays as it is:
	// no rounded card around it.
	r_item.transparent = image->detect_alpha() != Image::ALPHA_NONE;
	if (r_item.transparent) {
		image->generate_mipmaps();
		r_item.texture = ImageTexture::create_from_image(image);
		return;
	}
	const int w = image->get_width(), h = image->get_height();
	const float r = MIN(w, h) * 0.06f;
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			const float dx = x + 0.5f < r ? r - (x + 0.5f) : (x + 0.5f > w - r ? (x + 0.5f) - (w - r) : 0.0f);
			const float dy = y + 0.5f < r ? r - (y + 0.5f) : (y + 0.5f > h - r ? (y + 0.5f) - (h - r) : 0.0f);
			if (dx > 0.0f && dy > 0.0f) {
				const float d = Math::sqrt(dx * dx + dy * dy) - r;
				if (d > -1.0f) {
					Color c = image->get_pixel(x, y);
					c.a *= _clampf(-d, 0.0f, 1.0f);
					image->set_pixel(x, y, c);
				}
			}
		}
	}
	image->generate_mipmaps();
	r_item.texture = ImageTexture::create_from_image(image);
}

void LagoonicMoodboardPanel::_load() {
	loaded = true;
	board_dir = ProjectSettings::get_singleton()->globalize_path("res://referencias").trim_suffix("/");
	LocalVector<LagoonicMoodboardCanvas::Item> &items = canvas->get_items();
	items.clear();
	const String path = board_dir.path_join("tablero.json");
	if (FileAccess::exists(path)) {
		const Variant data = JSON::parse_string(FileAccess::get_file_as_string(path));
		const Dictionary d = data;
		const Array list = d.get("items", Array());
		for (int i = 0; i < list.size(); i++) {
			const Dictionary e = list[i];
			LagoonicMoodboardCanvas::Item it;
			it.id = e.get("id", itos(i));
			it.kind = e.get("kind", "image");
			it.file = e.get("file", "");
			it.text = e.get("text", "");
			it.free_pos = Vector2(float(e.get("x", 0.0)), float(e.get("y", 0.0)));
			it.pinned = e.get("pinned", false);
			it.group = e.get("group", "");
			if (it.kind != "note" && it.kind != "group" && !FileAccess::exists(board_dir.path_join(it.file))) {
				continue; // The file was deleted by hand.
			}
			_load_texture(it);
			items.push_back(it);
		}
		const String v = d.get("view", "ring");
		const LagoonicMoodboardCanvas::View view = v == "free" ? LagoonicMoodboardCanvas::VIEW_FREE : (v == "sphere" ? LagoonicMoodboardCanvas::VIEW_SPHERE : (v == "grid" ? LagoonicMoodboardCanvas::VIEW_GRID : LagoonicMoodboardCanvas::VIEW_RING));
		canvas->set_view(view, false);
	}
	canvas->items_changed();
	_update_ui();
}

void LagoonicMoodboardPanel::_save() {
	if (board_dir.is_empty()) {
		return;
	}
	DirAccess::make_dir_recursive_absolute(board_dir);
	// Never imported nor exported: references are for the team, not the game.
	if (!FileAccess::exists(board_dir.path_join(".gdignore"))) {
		Ref<FileAccess> f = FileAccess::open(board_dir.path_join(".gdignore"), FileAccess::WRITE);
	}
	Array list;
	for (const LagoonicMoodboardCanvas::Item &it : canvas->get_items()) {
		Dictionary e;
		e["id"] = it.id;
		e["kind"] = it.kind;
		if (!it.file.is_empty()) {
			e["file"] = it.file;
		}
		if (!it.text.is_empty()) {
			e["text"] = it.text;
		}
		e["x"] = int64_t(Math::round(it.free_pos.x));
		e["y"] = int64_t(Math::round(it.free_pos.y));
		e["pinned"] = it.pinned;
		if (!it.group.is_empty()) {
			e["group"] = it.group;
		}
		list.push_back(e);
	}
	static const char *VIEW_NAMES[] = { "free", "ring", "sphere", "grid" };
	Dictionary d;
	d["version"] = 1;
	d["view"] = VIEW_NAMES[canvas->get_view()];
	d["items"] = list;
	Ref<FileAccess> f = FileAccess::open(board_dir.path_join("tablero.json"), FileAccess::WRITE);
	if (f.is_valid()) {
		f->store_string(JSON::stringify(d, "\t"));
	}
}

void LagoonicMoodboardPanel::add_files(const PackedStringArray &p_files) {
	if (!loaded) {
		_load();
	}
	DirAccess::make_dir_recursive_absolute(board_dir);
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	LocalVector<LagoonicMoodboardCanvas::Item> &items = canvas->get_items();
	const int before = items.size();
	const Vector2 center = canvas->free_center();
	for (int i = 0; i < p_files.size(); i++) {
		String src = p_files[i].strip_edges();
		if (src.begins_with("res://")) {
			src = ProjectSettings::get_singleton()->globalize_path(src);
		}
		const String ext = src.get_extension().to_lower();
		const bool image = _ext_in(ext, IMAGE_EXTS);
		const bool video = _ext_in(ext, VIDEO_EXTS);
		const bool audio = _ext_in(ext, AUDIO_EXTS);
		const bool model = _ext_in(ext, MODEL_EXTS);
		if ((!image && !video && !audio && !model) || !FileAccess::exists(src)) {
			continue;
		}
		String name;
		if (src.get_base_dir() == board_dir) {
			name = src.get_file(); // Already on the board's folder.
		} else {
			name = _unique_file(src.get_file());
			if (da->copy(src, board_dir.path_join(name)) != OK) {
				continue;
			}
		}
		LagoonicMoodboardCanvas::Item it;
		it.id = itos(int64_t(Time::get_singleton()->get_unix_time_from_system() * 1000.0)) + "-" + itos(i);
		it.kind = image ? (ext == "gif" ? "video" : "image") : (audio ? "audio" : (model ? "model" : "video"));
		it.file = name;
		it.group = canvas->get_current_group();
		it.free_pos = center + _spread(items.size());
		_load_texture(it);
		items.push_back(it);
	}
	if (int(items.size()) != before) {
		canvas->items_changed(canvas->get_focus());
		_save();
		_update_ui();
	}
}

void LagoonicMoodboardPanel::add_note(const String &p_text) {
	if (!loaded) {
		_load();
	}
	if (p_text.strip_edges().is_empty()) {
		return;
	}
	LocalVector<LagoonicMoodboardCanvas::Item> &items = canvas->get_items();
	LagoonicMoodboardCanvas::Item it;
	it.id = itos(int64_t(Time::get_singleton()->get_unix_time_from_system() * 1000.0)) + "-n";
	it.kind = "note";
	it.text = p_text.strip_edges();
	it.group = canvas->get_current_group();
	it.aspect = 1.0f;
	it.free_pos = canvas->free_center() + _spread(items.size());
	items.push_back(it);
	canvas->items_changed(canvas->get_focus());
	_save();
	_update_ui();
}

void LagoonicMoodboardPanel::paste_clipboard() {
	DisplayServer *ds = DisplayServer::get_singleton();
	if (ds == nullptr) {
		return;
	}
	if (ds->clipboard_has_image()) {
		const Ref<Image> image = ds->clipboard_get_image();
		if (image.is_valid() && !image->is_empty()) {
			if (!loaded) {
				_load();
			}
			DirAccess::make_dir_recursive_absolute(board_dir);
			const String name = _unique_file("pegada.png");
			if (image->save_png(board_dir.path_join(name)) == OK) {
				PackedStringArray files;
				files.push_back(board_dir.path_join(name));
				add_files(files);
			}
		}
		return;
	}
	const String text = ds->clipboard_get().strip_edges();
	if (text.is_empty()) {
		return;
	}
	// File paths become references; any other text, a note.
	PackedStringArray files;
	for (const String &line : text.split("\n")) {
		const String l = line.strip_edges();
		if ((l.begins_with("/") || l.begins_with("res://")) && FileAccess::exists(l.begins_with("res://") ? ProjectSettings::get_singleton()->globalize_path(l) : l)) {
			files.push_back(l);
		}
	}
	if (!files.is_empty()) {
		add_files(files);
	} else {
		add_note(text);
	}
}

void LagoonicMoodboardPanel::edit_note(int p_index) {
	editing_note = p_index;
	LocalVector<LagoonicMoodboardCanvas::Item> &items = canvas->get_items();
	note_edit->set_text(p_index >= 0 && p_index < int(items.size()) ? items[p_index].text : String());
	note_dialog->set_title(p_index >= 0 ? String::utf8("Editar nota") : String::utf8("Nueva nota"));
	note_dialog->popup_centered(Size2(420, 220) * EDSCALE);
	note_edit->grab_focus();
}

void LagoonicMoodboardPanel::_on_note_confirmed() {
	const String text = note_edit->get_text().strip_edges();
	LocalVector<LagoonicMoodboardCanvas::Item> &items = canvas->get_items();
	if (editing_note >= 0 && editing_note < int(items.size())) {
		if (!text.is_empty()) {
			items[editing_note].text = text;
			_save();
		}
	} else {
		add_note(text);
	}
	editing_note = -1;
	_update_ui();
}

void LagoonicMoodboardPanel::_on_view_pressed(int p_view) {
	canvas->set_view(LagoonicMoodboardCanvas::View(p_view));
	_save();
	_update_ui();
}

void LagoonicMoodboardPanel::_on_add_pressed() {
	file_dialog->popup_file_dialog();
}

void LagoonicMoodboardPanel::_on_note_pressed() {
	edit_note(-1);
}

void LagoonicMoodboardPanel::_on_files_selected(const PackedStringArray &p_files) {
	add_files(p_files);
}

void LagoonicMoodboardPanel::_on_os_files_dropped(const PackedStringArray &p_files) {
	if (!is_visible_in_tree() || DisplayServer::get_singleton() == nullptr) {
		return;
	}
	// Only a drop over the board; elsewhere it is the editor's.
	if (!get_screen_rect().has_point(DisplayServer::get_singleton()->mouse_get_position())) {
		return;
	}
	add_files(p_files);
}

void LagoonicMoodboardPanel::_on_canvas_changed() {
	_save();
	_update_ui();
}

void LagoonicMoodboardPanel::_on_pin_pressed() {
	const int i = canvas->get_focus();
	LocalVector<LagoonicMoodboardCanvas::Item> &items = canvas->get_items();
	if (i >= 0 && i < int(items.size())) {
		items[i].pinned = !items[i].pinned;
		_save();
		_update_ui();
	}
}

void LagoonicMoodboardPanel::_on_edit_pressed() {
	const int i = canvas->get_focus();
	if (i >= 0) {
		edit_note(i);
	}
}

void LagoonicMoodboardPanel::_on_remove_pressed() {
	const int i = canvas->get_focus();
	LocalVector<LagoonicMoodboardCanvas::Item> &items = canvas->get_items();
	if (i < 0 || i >= int(items.size())) {
		return;
	}
	if (items[i].kind == "group") {
		// Undoing a group keeps its cards, one level up.
		for (LagoonicMoodboardCanvas::Item &it : items) {
			if (it.group == items[i].id) {
				it.group = items[i].group;
			}
		}
	} else if (!items[i].file.is_empty()) {
		// Only the board's copy; the original stays where it was.
		DirAccess::remove_absolute(board_dir.path_join(items[i].file));
		DirAccess::remove_absolute(board_dir.path_join(".thumbs").path_join(items[i].file + ".png"));
	}
	canvas->set_focus(-1);
	items.remove_at(i);
	canvas->items_changed();
	_save();
	_update_ui();
}

String LagoonicMoodboardPanel::_new_id(const String &p_suffix) const {
	return itos(int64_t(Time::get_singleton()->get_unix_time_from_system() * 1000.0)) + "-" + p_suffix;
}

Vector<int> LagoonicMoodboardPanel::_targets() const {
	const LocalVector<LagoonicMoodboardCanvas::Item> &items = canvas->get_items();
	Vector<int> picked;
	auto add = [&](int p_index) {
		if (p_index < 0 || picked.has(p_index)) {
			return;
		}
		if (items[p_index].kind == "group") {
			const LocalVector<int> *m = canvas->get_members(items[p_index].id);
			if (m != nullptr) {
				for (int k : *m) {
					if (items[k].kind != "group" && !picked.has(k)) {
						picked.push_back(k);
					}
				}
			}
			return;
		}
		picked.push_back(p_index);
	};
	for (uint32_t i = 0; i < items.size(); i++) {
		if (items[i].selected) {
			add(i);
		}
	}
	if (picked.is_empty() && !canvas->get_current_group().is_empty()) {
		for (uint32_t i = 0; i < items.size(); i++) {
			if (items[i].group == canvas->get_current_group() && items[i].kind != "group") {
				add(i);
			}
		}
	}
	if (picked.is_empty() && canvas->get_focus() >= 0) {
		add(canvas->get_focus());
	}
	return picked;
}

String LagoonicMoodboardPanel::_target_name() const {
	const LocalVector<LagoonicMoodboardCanvas::Item> &items = canvas->get_items();
	String only_group;
	int groups = 0;
	bool loose = false;
	for (const LagoonicMoodboardCanvas::Item &it : items) {
		if (!it.selected) {
			continue;
		}
		if (it.kind == "group") {
			only_group = it.text;
			groups++;
		} else {
			loose = true;
		}
	}
	if (groups == 1 && !loose) {
		return only_group;
	}
	if (groups == 0 && !loose && !canvas->get_current_group().is_empty()) {
		for (const LagoonicMoodboardCanvas::Item &it : items) {
			if (it.kind == "group" && it.id == canvas->get_current_group()) {
				return it.text;
			}
		}
	}
	return String();
}

void LagoonicMoodboardPanel::_on_selection_changed() {
	_update_ui();
}

void LagoonicMoodboardPanel::_on_group_changed() {
	_update_ui();
}

void LagoonicMoodboardPanel::_on_back_pressed() {
	canvas->exit_group();
}

void LagoonicMoodboardPanel::_on_clear_pressed() {
	canvas->clear_selection();
}

void LagoonicMoodboardPanel::_on_group_pressed() {
	name_mode = 0;
	name_dialog->set_title(String::utf8("Nuevo grupo"));
	name_edit->set_text(String());
	name_dialog->popup_centered(Size2(360, 0) * EDSCALE);
	name_edit->grab_focus();
}

void LagoonicMoodboardPanel::_on_rename_pressed() {
	const LocalVector<LagoonicMoodboardCanvas::Item> &items = canvas->get_items();
	for (const LagoonicMoodboardCanvas::Item &it : items) {
		if (it.kind == "group" && it.id == canvas->get_current_group()) {
			name_edit->set_text(it.text);
		}
	}
	name_mode = 1;
	name_dialog->set_title(String::utf8("Renombrar grupo"));
	name_dialog->popup_centered(Size2(360, 0) * EDSCALE);
	name_edit->grab_focus();
	name_edit->select_all();
}

void LagoonicMoodboardPanel::_on_ungroup_pressed() {
	LocalVector<LagoonicMoodboardCanvas::Item> &items = canvas->get_items();
	const String id = canvas->get_current_group();
	for (uint32_t i = 0; i < items.size(); i++) {
		if (items[i].kind == "group" && items[i].id == id) {
			canvas->exit_group();
			const String parent = items[i].group;
			for (LagoonicMoodboardCanvas::Item &it : items) {
				if (it.group == id) {
					it.group = parent;
					it.rs = Vector3(); // They fly out as new cards.
				}
			}
			items.remove_at(i);
			canvas->items_changed();
			_save();
			_update_ui();
			return;
		}
	}
}

void LagoonicMoodboardPanel::_on_name_confirmed() {
	const String name = name_edit->get_text().strip_edges();
	if (name.is_empty()) {
		return;
	}
	LocalVector<LagoonicMoodboardCanvas::Item> &items = canvas->get_items();
	if (name_mode == 1) {
		for (LagoonicMoodboardCanvas::Item &it : items) {
			if (it.kind == "group" && it.id == canvas->get_current_group()) {
				it.text = name;
			}
		}
		_save();
		_update_ui();
		return;
	}
	// A new group, where the chosen cards (or the open one) were.
	LagoonicMoodboardCanvas::Item group;
	group.id = _new_id("g");
	group.kind = "group";
	group.text = name;
	group.aspect = 0.8f;
	group.group = canvas->get_current_group();
	Vector<int> chosen;
	if (name_mode == 2 && canvas->get_focus() >= 0) {
		chosen.push_back(canvas->get_focus());
	} else {
		for (uint32_t i = 0; i < items.size(); i++) {
			if (items[i].selected && items[i].kind != "group") {
				chosen.push_back(i);
			}
		}
	}
	if (!chosen.is_empty()) {
		group.free_pos = items[chosen[0]].free_pos;
		group.rs = items[chosen[0]].rs;
		group.rz = items[chosen[0]].rz;
	} else {
		group.free_pos = canvas->free_center();
	}
	for (int i : chosen) {
		items[i].group = group.id;
		items[i].selected = false;
	}
	canvas->set_focus(-1);
	items.push_back(group);
	canvas->clear_selection();
	canvas->items_changed();
	_save();
	_update_ui();
}

void LagoonicMoodboardPanel::_on_move_about_to_popup() {
	PopupMenu *menu = move_button->get_popup();
	menu->clear();
	const LocalVector<LagoonicMoodboardCanvas::Item> &items = canvas->get_items();
	const int f = canvas->get_focus();
	const String in = f >= 0 && f < int(items.size()) ? items[f].group : String();
	for (uint32_t i = 0; i < items.size(); i++) {
		if (items[i].kind == "group" && items[i].id != in) {
			menu->add_item(items[i].text, i);
		}
	}
	if (menu->get_item_count() > 0) {
		menu->add_separator();
	}
	menu->add_item(String::utf8("Nuevo grupo…"), -2);
	if (!in.is_empty()) {
		menu->add_item(String::utf8("Sacar del grupo"), -3);
	}
}

void LagoonicMoodboardPanel::_on_move_selected(int p_id) {
	LocalVector<LagoonicMoodboardCanvas::Item> &items = canvas->get_items();
	const int f = canvas->get_focus();
	if (f < 0 || f >= int(items.size())) {
		return;
	}
	if (p_id == -2) {
		name_mode = 2;
		name_dialog->set_title(String::utf8("Nuevo grupo"));
		name_edit->set_text(String());
		name_dialog->popup_centered(Size2(360, 0) * EDSCALE);
		name_edit->grab_focus();
		return;
	}
	if (p_id == -3) {
		items[f].group = String();
	} else if (p_id >= 0 && p_id < int(items.size())) {
		items[f].group = items[p_id].id;
	}
	canvas->set_focus(-1);
	canvas->items_changed();
	_save();
	_update_ui();
}

void LagoonicMoodboardPanel::_on_send_pressed() {
	// The references go to the chat as attachments; notes, as text. The user
	// writes what to do with them and sends.
	if (chat == nullptr) {
		return;
	}
	const LocalVector<LagoonicMoodboardCanvas::Item> &items = canvas->get_items();
	const Vector<int> picked = _targets();
	PackedStringArray files;
	PackedStringArray notes;
	for (int i : picked) {
		const LagoonicMoodboardCanvas::Item &it = items[i];
		if (it.kind == "note") {
			notes.push_back("- " + it.text);
		} else if (!it.file.is_empty()) {
			files.push_back(board_dir.path_join(it.file));
		}
	}
	if (files.is_empty() && notes.is_empty()) {
		return;
	}
	chat->add_attachments(files);
	const String name = _target_name();
	String text = name.is_empty() ? String::utf8("Referencias del tablero") : vformat(String::utf8("Referencias del tablero (%s)"), name);
	if (!notes.is_empty()) {
		text += ":\n" + String("\n").join(notes) + "\n\n";
	} else {
		text += ". ";
	}
	chat->insert_in_composer(text);
	canvas->clear_selection();
}

void LagoonicMoodboardPanel::_on_generate_pressed() {
	const Vector<int> picked = _targets();
	const LocalVector<LagoonicMoodboardCanvas::Item> &items = canvas->get_items();
	int images = 0;
	int notes = 0;
	for (int i : picked) {
		images += items[i].kind == "image" ? 1 : 0;
		notes += items[i].kind == "note" ? 1 : 0;
	}
	String refs = vformat(String::utf8("Referencias: %d imágenes"), images);
	if (notes > 0) {
		refs += vformat(String::utf8(" y %d notas"), notes);
	}
	const String name = _target_name();
	if (!name.is_empty()) {
		refs += String::utf8(" · grupo «") + name + String::utf8("»");
	}
	generate_refs->set_text(refs + String::utf8(". Lo generado vuelve a este tablero."));
	generate_dialog->popup_centered(Size2(480, 0) * EDSCALE);
	generate_prompt->grab_focus();
}

void LagoonicMoodboardPanel::_on_generate_confirmed() {
	// The leader generates with the provider's tool (it holds the keys and
	// the tools); the request goes to the chat ready to send, so the user
	// sees what will be paid for before it runs.
	if (chat == nullptr) {
		return;
	}
	const String prompt = generate_prompt->get_text().strip_edges();
	if (prompt.is_empty()) {
		return;
	}
	static const char *TOOLS[] = {
		"asset_image_fal (model flux-schnell)",
		"asset_image_fal (model recraft-v3)",
		"asset_image_higgsfield",
		"asset_pixel_art (Retro Diffusion; pass the reference images in referenceImages)",
		"asset_sound_fal",
	};
	const int provider = CLAMP(generate_provider->get_selected(), 0, 4);
	const LocalVector<LagoonicMoodboardCanvas::Item> &items = canvas->get_items();
	const Vector<int> picked = _targets();
	PackedStringArray files;
	PackedStringArray res_paths;
	PackedStringArray notes;
	for (int i : picked) {
		const LagoonicMoodboardCanvas::Item &it = items[i];
		if (it.kind == "note") {
			notes.push_back("- " + it.text);
		} else if (!it.file.is_empty()) {
			files.push_back(board_dir.path_join(it.file));
			res_paths.push_back("res://referencias/" + it.file);
		}
	}
	const String name = _target_name();
	String text = String::utf8("Genera: ") + prompt + "\n\n";
	text += String::utf8("Usa ") + TOOLS[provider] + ".";
	if (!res_paths.is_empty()) {
		text += String::utf8(" Toma como referencia estas imágenes del tablero: ") + String(", ").join(res_paths) + ".";
	}
	if (!notes.is_empty()) {
		text += String::utf8("\nNotas de estilo:\n") + String("\n").join(notes);
	}
	if (generate_style->is_pressed()) {
		text += String::utf8("\nRespeta el estilo fijado del juego.");
	}
	text += String::utf8("\nGuarda el resultado en assets/ y copia también cada archivo generado a res://referencias/ para que aparezca en el tablero");
	text += name.is_empty() ? String(".") : String::utf8(" (en el grupo «") + name + String::utf8("»).");
	inbox_group = canvas->get_current_group();
	if (inbox_group.is_empty() && !name.is_empty()) {
		for (const LagoonicMoodboardCanvas::Item &it : items) {
			if (it.kind == "group" && it.text == name) {
				inbox_group = it.id;
			}
		}
	}
	chat->add_attachments(files);
	chat->insert_in_composer(text);
	generate_prompt->set_text(String());
	canvas->clear_selection();
}

void LagoonicMoodboardPanel::_scan_folder() {
	// Files that land in the board's folder (generated by an agent, copied by
	// hand) join the board, in the group the last request asked for.
	if (!is_visible_in_tree() || board_dir.is_empty() || !DirAccess::exists(board_dir)) {
		return;
	}
	LocalVector<LagoonicMoodboardCanvas::Item> &items = canvas->get_items();
	HashSet<String> known;
	for (const LagoonicMoodboardCanvas::Item &it : items) {
		if (!it.file.is_empty()) {
			known.insert(it.file);
		}
	}
	const PackedStringArray files = DirAccess::get_files_at(board_dir);
	PackedStringArray fresh;
	for (const String &f : files) {
		const String ext = f.get_extension().to_lower();
		if (!known.has(f) && (_ext_in(ext, IMAGE_EXTS) || _ext_in(ext, VIDEO_EXTS) || _ext_in(ext, AUDIO_EXTS) || _ext_in(ext, MODEL_EXTS))) {
			fresh.push_back(board_dir.path_join(f));
		}
	}
	if (fresh.is_empty()) {
		return;
	}
	const int before = items.size();
	add_files(fresh);
	if (!inbox_group.is_empty() && canvas->get_current_group() != inbox_group) {
		for (uint32_t i = before; i < items.size(); i++) {
			items[i].group = inbox_group;
		}
		canvas->items_changed();
		_save();
	}
}

void LagoonicMoodboardPanel::_on_listen_pressed() {
	if (player->is_playing()) {
		player->stop();
		_update_ui();
		return;
	}
	const int i = canvas->get_focus();
	const LocalVector<LagoonicMoodboardCanvas::Item> &items = canvas->get_items();
	if (i < 0 || i >= int(items.size())) {
		return;
	}
	const String path = board_dir.path_join(items[i].file);
	const String ext = path.get_extension().to_lower();
	Ref<AudioStream> stream;
	if (ext == "wav") {
		stream = AudioStreamWAV::load_from_file(path, Dictionary());
	}
	if (ext == "ogg" || ext == "mp3") {
		// Through ClassDB: the codec modules' headers need their libraries.
		MethodBind *load = ClassDB::get_method(ext == "ogg" ? StringName("AudioStreamOggVorbis") : StringName("AudioStreamMP3"), SNAME("load_from_file"));
		if (load != nullptr) {
			const Variant arg = path;
			const Variant *args[1] = { &arg };
			Callable::CallError err;
			stream = load->call(nullptr, args, 1, err);
		}
	}
	if (stream.is_valid()) {
		player->set_stream(stream);
		player->play();
	}
	_update_ui();
}

static void _thumb_aabb(Node *p_node, AABB &r_aabb, bool &r_any) {
	if (VisualInstance3D *vi = Object::cast_to<VisualInstance3D>(p_node)) {
		const AABB box = vi->get_global_transform().xform(vi->get_aabb());
		if (box.size != Vector3()) {
			r_aabb = r_any ? r_aabb.merge(box) : box;
			r_any = true;
		}
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_thumb_aabb(p_node->get_child(i), r_aabb, r_any);
	}
}

// A model file (GLB/GLTF) as a scene that draws, or null.
static Node *_load_model_scene(const String &p_path) {
	Node *scene = nullptr;
	const String ext = p_path.get_extension().to_lower();
#ifdef MODULE_GLTF_ENABLED
	if (ext == "glb" || ext == "gltf") {
		Ref<GLTFDocument> doc;
		doc.instantiate();
		Ref<GLTFState> state;
		state.instantiate();
		if (doc->append_from_file(p_path, state) == OK) {
			scene = doc->generate_scene(state);
		}
		// In the editor the scene keeps import meshes, which do not draw:
		// turn them into real meshes.
		if (scene != nullptr) {
			LocalVector<ImporterMeshInstance3D *> found;
			LocalVector<Node *> stack;
			stack.push_back(scene);
			while (!stack.is_empty()) {
				Node *n = stack[stack.size() - 1];
				stack.remove_at(stack.size() - 1);
				if (ImporterMeshInstance3D *imi = Object::cast_to<ImporterMeshInstance3D>(n)) {
					if (imi != scene) {
						found.push_back(imi);
					}
				}
				for (int c = 0; c < n->get_child_count(); c++) {
					stack.push_back(n->get_child(c));
				}
			}
			for (ImporterMeshInstance3D *imi : found) {
				GLTFDocumentExtensionConvertImporterMesh::convert_importer_mesh_instance_3d(imi);
				memdelete(imi);
			}
		}
	}
#endif
	return scene;
}

// Puts a model in a viewport: centred on a pivot (turning the pivot turns
// the model about its middle), a camera that frames it, light, no sky.
static void _stage_model(SubViewport *p_vp, Node *p_scene, ModelStage &r_stage) {
	p_vp->set_transparent_background(true);
	p_vp->set_use_own_world_3d(true);
	p_vp->set_update_mode(SubViewport::UPDATE_ALWAYS);
	p_vp->set_msaa_3d(Viewport::MSAA_4X);
	Node3D *root = memnew(Node3D);
	p_vp->add_child(root);
	Node3D *pivot = memnew(Node3D);
	root->add_child(pivot);
	Node3D *holder = memnew(Node3D);
	pivot->add_child(holder);
	holder->add_child(p_scene);
	AABB box;
	bool any = false;
	_thumb_aabb(p_scene, box, any);
	if (!any) {
		box = AABB(Vector3(-0.5f, -0.5f, -0.5f), Vector3(1, 1, 1));
	}
	holder->set_position(-box.get_center());
	const float radius = MAX(0.01f, box.size.length() * 0.5f);
	Camera3D *camera = memnew(Camera3D);
	camera->set_fov(35.0f);
	const float dist = radius / Math::tan(Math::deg_to_rad(35.0f * 0.5f)) * 1.05f;
	root->add_child(camera);
	camera->look_at_from_position(Vector3(1.0f, 0.75f, 1.35f).normalized() * dist, Vector3(), Vector3(0, 1, 0));
	camera->set_near(MAX(0.001f, radius * 0.02f));
	camera->set_far(dist + radius * 6.0f);
	camera->set_current(true);
	DirectionalLight3D *light = memnew(DirectionalLight3D);
	root->add_child(light);
	light->look_at_from_position(Vector3(-0.6f, 1.0f, 0.8f), Vector3(), Vector3(0, 1, 0));
	Ref<Environment> env;
	env.instantiate();
	env->set_background(Environment::BG_CLEAR_COLOR);
	env->set_ambient_source(Environment::AMBIENT_SOURCE_COLOR);
	env->set_ambient_light_color(Color(1, 1, 1));
	env->set_ambient_light_energy(0.7f);
	WorldEnvironment *we = memnew(WorldEnvironment);
	we->set_environment(env);
	root->add_child(we);
	r_stage.pivot = pivot;
	r_stage.camera = camera;
	r_stage.dist = dist;
	r_stage.radius = radius;
}

// ================================================================ live 3D

void LagoonicMoodboardPanel::_live_open(const String &p_path) {
	if (p_path == live_path && live_vp != nullptr) {
		return;
	}
	_live_close();
	Node *scene = _load_model_scene(p_path);
	if (scene == nullptr) {
		return;
	}
	live_path = p_path;
	live_vp = memnew(SubViewport);
	live_vp->set_size(Size2i(640, 640));
	live_vp->set_disable_input(true);
	add_child(live_vp);
	live_stage = ModelStage();
	_stage_model(live_vp, scene, live_stage);
	live_yaw = 0.0f;
	live_pitch = 0.0f;
	live_zoom = 1.0f;
	live_spin = 0.0f;
	live_idle = 0.0f;
	_live_apply();
	canvas->set_live_texture(live_vp->get_texture());
}

void LagoonicMoodboardPanel::_live_close() {
	canvas->set_live_texture(Ref<Texture2D>());
	if (live_vp != nullptr) {
		live_vp->queue_free();
		live_vp = nullptr;
	}
	live_stage = ModelStage();
	live_path = String();
	live_grab = false;
}

void LagoonicMoodboardPanel::_live_apply() {
	if (live_stage.pivot == nullptr) {
		return;
	}
	live_stage.pivot->set_basis(Basis(Vector3(0, 1, 0), live_yaw) * Basis(Vector3(1, 0, 0), live_pitch));
	live_stage.camera->look_at_from_position(Vector3(1.0f, 0.75f, 1.35f).normalized() * live_stage.dist * live_zoom, Vector3(), Vector3(0, 1, 0));
}

void LagoonicMoodboardPanel::_on_model_turn(const Vector2 &p_relative) {
	// Sideways turns it around, up and down tips it.
	live_yaw += p_relative.x * 0.01f;
	live_pitch = CLAMP(live_pitch + p_relative.y * 0.01f, -1.4f, 1.4f);
	live_spin = p_relative.x * 0.6f;
	live_idle = 0.0f;
	_live_apply();
}

void LagoonicMoodboardPanel::_on_model_zoom(float p_factor) {
	live_zoom = CLAMP(live_zoom * p_factor, 0.3f, 3.0f);
	_live_apply();
}

void LagoonicMoodboardPanel::_on_model_grab(bool p_grab) {
	live_grab = p_grab;
	live_idle = 0.0f;
}

void LagoonicMoodboardPanel::_thumb_step() {
	// One preview at a time: load the model, frame it, render a few frames,
	// keep the picture.
	if (thumb_viewport != nullptr) {
		thumb_frames++;
		if (thumb_frames >= 4) {
			_thumb_finish();
		}
		return;
	}
	if (thumb_queue.is_empty() || !is_visible_in_tree()) {
		return;
	}
	thumb_id = thumb_queue[0];
	thumb_queue.remove_at(0);
	String file;
	for (const LagoonicMoodboardCanvas::Item &it : canvas->get_items()) {
		if (it.id == thumb_id) {
			file = it.file;
		}
	}
	if (file.is_empty()) {
		return;
	}
	Node *scene = _load_model_scene(board_dir.path_join(file));
	if (scene == nullptr) {
		return;
	}
	SubViewport *vp = memnew(SubViewport);
	vp->set_size(Size2i(320, 320));
	add_child(vp);
	ModelStage stage;
	_stage_model(vp, scene, stage);
	thumb_viewport = vp;
	thumb_frames = 0;
}

void LagoonicMoodboardPanel::_thumb_finish() {
	SubViewport *vp = Object::cast_to<SubViewport>(thumb_viewport);
	thumb_viewport = nullptr;
	if (vp == nullptr) {
		return;
	}
	Ref<Image> image = vp->get_texture()->get_image();
	vp->queue_free();
	if (image.is_null() || image->is_empty()) {
		return;
	}
	LocalVector<LagoonicMoodboardCanvas::Item> &items = canvas->get_items();
	for (LagoonicMoodboardCanvas::Item &it : items) {
		if (it.id != thumb_id) {
			continue;
		}
		const String dir = board_dir.path_join(".thumbs");
		DirAccess::make_dir_recursive_absolute(dir);
		image->save_png(dir.path_join(it.file + ".png"));
		image->generate_mipmaps();
		it.texture = ImageTexture::create_from_image(image);
		it.transparent = true;
	}
}

void LagoonicMoodboardPanel::_update_ui() {
	const LocalVector<LagoonicMoodboardCanvas::Item> &items = canvas->get_items();
	for (int v = 0; v < LagoonicMoodboardCanvas::VIEW_COUNT; v++) {
		view_buttons[v]->set_pressed_no_signal(canvas->get_view() == v);
	}
	int pinned = 0;
	int cards = 0;
	int selected = 0;
	int selected_loose = 0;
	for (const LagoonicMoodboardCanvas::Item &it : items) {
		pinned += it.pinned ? 1 : 0;
		cards += it.kind != "group" ? 1 : 0;
		selected += it.selected ? 1 : 0;
		selected_loose += it.selected && it.kind != "group" ? 1 : 0;
	}
	count_label->set_text(cards == 0 ? String() : (pinned > 0 ? vformat(String::utf8("%d · %d en el estilo"), cards, pinned) : itos(cards)));
	empty_label->set_visible(items.is_empty());

	// The action bar: inside a group, and/or with cards chosen.
	const String group_id = canvas->get_current_group();
	String group_name;
	int group_count = 0;
	for (const LagoonicMoodboardCanvas::Item &it : items) {
		if (it.kind == "group" && it.id == group_id) {
			group_name = it.text;
		}
		group_count += (!group_id.is_empty() && it.group == group_id) ? 1 : 0;
	}
	const bool in_group = !group_id.is_empty();
	action_bar->set_visible(in_group || selected > 0);
	back_button->set_visible(in_group);
	if (selected > 0) {
		action_label->set_text(vformat(selected == 1 ? String::utf8("%d elegida") : String::utf8("%d elegidas"), selected));
	} else {
		action_label->set_text(vformat(String::utf8("%s · %d"), group_name, group_count));
	}
	group_button->set_visible(selected_loose > 0);
	rename_button->set_visible(in_group && selected == 0);
	ungroup_button->set_visible(in_group && selected == 0);
	clear_button->set_visible(selected > 0);
	send_button->set_disabled(chat == nullptr);
	generate_button->set_disabled(chat == nullptr);
	send_button->set_tooltip_text(chat == nullptr ? String::utf8("Abre el tablero en el espacio con chat para enviarlo.") : String::utf8("Adjunta estas referencias al mensaje del chat (las notas van como texto)."));

	const int i = canvas->get_focus();
	if (i >= 0 && i < int(items.size())) {
		const LagoonicMoodboardCanvas::Item &it = items[i];
		details_title->set_text(it.kind == "note" ? String::utf8("Nota") : it.file);
		String info;
		if (it.kind == "image" && it.texture.is_valid()) {
			info = String::utf8("Imagen");
		} else if (it.kind == "video") {
			info = String::utf8("Vídeo · se ve como tarjeta, sin reproducir");
		} else if (it.kind == "audio") {
			info = String::utf8("Sonido");
		} else if (it.kind == "model") {
			info = it.texture.is_valid() ? String::utf8("Modelo 3D · arrástralo para girarlo, rueda o pellizca para acercarlo") : String::utf8("Modelo 3D · sin vista previa (solo GLB y GLTF la tienen)");
		} else if (it.kind == "note") {
			info = it.text;
		}
		if (it.pinned) {
			info += String::utf8("\nForma parte del estilo del juego.");
		}
		details_info->set_text(info);
		pin_button->set_text(it.pinned ? String::utf8("Quitar del estilo") : String::utf8("Fijar en el estilo"));
		edit_button->set_visible(it.kind == "note");
		const String ext = it.file.get_extension().to_lower();
		listen_button->set_visible(it.kind == "audio" && (ext == "wav" || ext == "ogg" || ext == "mp3"));
		listen_button->set_text(player->is_playing() ? String::utf8("Parar") : String::utf8("Escuchar"));
	} else if (player != nullptr && player->is_playing()) {
		player->stop();
	}
}

void LagoonicMoodboardPanel::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			add_to_group(SNAME("_lagoonic_os_drop_target"));
			Window *root = get_tree() != nullptr ? get_tree()->get_root() : nullptr;
			if (root != nullptr && !root->is_connected(SNAME("files_dropped"), callable_mp(this, &LagoonicMoodboardPanel::_on_os_files_dropped))) {
				root->connect(SNAME("files_dropped"), callable_mp(this, &LagoonicMoodboardPanel::_on_os_files_dropped));
			}
			set_process_internal(true);
		} break;
		case NOTIFICATION_EXIT_TREE: {
			Window *root = get_tree() != nullptr ? get_tree()->get_root() : nullptr;
			if (root != nullptr && root->is_connected(SNAME("files_dropped"), callable_mp(this, &LagoonicMoodboardPanel::_on_os_files_dropped))) {
				root->disconnect(SNAME("files_dropped"), callable_mp(this, &LagoonicMoodboardPanel::_on_os_files_dropped));
			}
		} break;
		case NOTIFICATION_READY: {
			if (!loaded) {
				_load();
			}
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			_thumb_step();
			{
				// An open 3D model renders live over its card, to turn it.
				const LocalVector<LagoonicMoodboardCanvas::Item> &its = canvas->get_items();
				const int f = canvas->get_focus();
				const bool model = f >= 0 && f < int(its.size()) && its[f].kind == "model" && its[f].texture.is_valid();
				if (model && canvas->get_focus_k() > 0.9f) {
					_live_open(board_dir.path_join(its[f].file));
					if (live_stage.pivot != nullptr && !live_grab) {
						// After letting go it keeps the swing; then it turns by itself.
						const float dt = float(get_process_delta_time());
						live_idle += dt;
						live_spin = Math::lerp(live_spin, live_idle > 2.0f ? 0.4f : 0.0f, MIN(1.0f, dt * 2.0f));
						live_yaw += live_spin * dt;
						_live_apply();
					}
				} else if (live_vp != nullptr) {
					_live_close();
				}
			}
			// The details come in when the open card has almost landed.
			float k = _clampf((canvas->get_focus_k() - 0.72f) / 0.28f, 0.0f, 1.0f);
			k = k * k * (3.0f - 2.0f * k);
			const bool show = canvas->get_focus() >= 0 && k > 0.0f;
			details->set_visible(show);
			if (show) {
				details->set_modulate(Color(1, 1, 1, k));
				const Rect2 r = canvas->get_details_rect();
				details->set_position(r.position);
				details->set_size(r.size);
			}
		} break;
	}
}

LagoonicMoodboardPanel::LagoonicMoodboardPanel() {
	add_theme_constant_override(SNAME("separation"), int(6 * EDSCALE));

	// The bento has no margin of its own: the board (and its floor) reaches
	// the rounded edge; only the bars keep a margin.
	MarginContainer *top = memnew(MarginContainer);
	top->add_theme_constant_override(SNAME("margin_left"), int(8 * EDSCALE));
	top->add_theme_constant_override(SNAME("margin_right"), int(8 * EDSCALE));
	top->add_theme_constant_override(SNAME("margin_top"), int(8 * EDSCALE));
	add_child(top);
	VBoxContainer *bars = memnew(VBoxContainer);
	bars->add_theme_constant_override(SNAME("separation"), int(6 * EDSCALE));
	top->add_child(bars);

	HBoxContainer *bar = memnew(HBoxContainer);
	bar->add_theme_constant_override(SNAME("separation"), int(2 * EDSCALE));
	bars->add_child(bar);
	static const char *VIEW_LABELS[] = { "Libre", "Anillo", "Esfera", "Cuadrícula" };
	static const char *VIEW_TIPS[] = {
		"Un lienzo: arrastra las tarjetas, rueda para acercar.",
		"Un anillo que gira; arrastra o usa la rueda para moverlo.",
		"Una esfera que gira; arrastra para moverla.",
		"Todas en orden, a la vista.",
	};
	// The views as icons (like flyshare's rail): cards thrown on a table, a
	// tilted ring, a sphere of dots, a grid.
	static const char *VIEW_ICONS[] = {
		"<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='none' stroke='#fff' stroke-width='1.3'><rect x='1.5' y='3' width='6' height='7.5' rx='1' transform='rotate(-8 4.5 6.75)'/><rect x='8.5' y='1.5' width='6' height='5' rx='1' transform='rotate(6 11.5 4)'/><rect x='7' y='8.5' width='7' height='6' rx='1'/></svg>",
		"<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='none' stroke='#fff' stroke-width='1.3'><circle cx='8' cy='8' r='6.3'/><ellipse cx='8' cy='8' rx='6.3' ry='2.4'/><path d='M8 1.7v12.6'/></svg>",
		"<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='#fff'><circle cx='8' cy='2' r='1'/><circle cx='4.5' cy='3.4' r='.9'/><circle cx='11.5' cy='3.4' r='.9'/><circle cx='2.4' cy='6.5' r='.8'/><circle cx='6.2' cy='5.8' r='1.1'/><circle cx='9.8' cy='5.8' r='1.1'/><circle cx='13.6' cy='6.5' r='.8'/><circle cx='2.4' cy='9.6' r='.8'/><circle cx='6.2' cy='10.2' r='1.1'/><circle cx='9.8' cy='10.2' r='1.1'/><circle cx='13.6' cy='9.6' r='.8'/><circle cx='4.5' cy='12.6' r='.9'/><circle cx='11.5' cy='12.6' r='.9'/><circle cx='8' cy='14' r='1'/><circle cx='8' cy='8' r='1.2'/></svg>",
		"<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='none' stroke='#fff' stroke-width='1.3'><rect x='1.5' y='3' width='3.6' height='4.2' rx='.8'/><rect x='6.2' y='3' width='3.6' height='4.2' rx='.8'/><rect x='10.9' y='3' width='3.6' height='4.2' rx='.8'/><rect x='1.5' y='8.8' width='3.6' height='4.2' rx='.8'/><rect x='6.2' y='8.8' width='3.6' height='4.2' rx='.8'/><rect x='10.9' y='8.8' width='3.6' height='4.2' rx='.8'/></svg>",
	};
	for (int v = 0; v < LagoonicMoodboardCanvas::VIEW_COUNT; v++) {
		Button *b = memnew(Button);
		Ref<Image> icon;
		icon.instantiate();
		if (icon->load_svg_from_string(VIEW_ICONS[v], EDSCALE) == OK) {
			b->set_button_icon(ImageTexture::create_from_image(icon));
		} else {
			b->set_text(String::utf8(VIEW_LABELS[v]));
		}
		b->set_tooltip_text(String::utf8(VIEW_LABELS[v]) + String::utf8(": ") + String::utf8(VIEW_TIPS[v]));
		b->set_flat(true);
		b->set_toggle_mode(true);
		b->set_focus_mode(FOCUS_NONE);
		b->connect(SceneStringName(pressed), callable_mp(this, &LagoonicMoodboardPanel::_on_view_pressed).bind(v));
		bar->add_child(b);
		view_buttons[v] = b;
	}
	Control *spacer = memnew(Control);
	spacer->set_h_size_flags(SIZE_EXPAND_FILL);
	bar->add_child(spacer);
	count_label = memnew(Label);
	count_label->set_theme_type_variation(LagoonicVibeTheme::MUTED_LABEL);
	bar->add_child(count_label);
	Button *note = memnew(Button);
	note->set_text(String::utf8("Nota"));
	note->set_flat(true);
	note->set_focus_mode(FOCUS_NONE);
	note->set_tooltip_text(String::utf8("Añade una nota: una idea de estilo, una regla, una paleta en palabras."));
	note->connect(SceneStringName(pressed), callable_mp(this, &LagoonicMoodboardPanel::_on_note_pressed));
	bar->add_child(note);
	Button *add = memnew(Button);
	add->set_text(String::utf8("Añadir"));
	add->set_flat(true);
	add->set_focus_mode(FOCUS_NONE);
	add->set_tooltip_text(String::utf8("Elige imágenes o vídeos de tu ordenador. También puedes arrastrarlos aquí o pegar una imagen con ⌘V."));
	add->connect(SceneStringName(pressed), callable_mp(this, &LagoonicMoodboardPanel::_on_add_pressed));
	bar->add_child(add);

	// What to do with a group or the chosen cards.
	action_bar = memnew(HBoxContainer);
	action_bar->add_theme_constant_override(SNAME("separation"), int(2 * EDSCALE));
	action_bar->set_visible(false);
	bars->add_child(action_bar);
	auto action = [&](const String &p_text, const String &p_tip, void (LagoonicMoodboardPanel::*p_method)(), bool p_flat = true) {
		Button *b = memnew(Button);
		b->set_text(p_text);
		b->set_tooltip_text(p_tip);
		b->set_flat(p_flat);
		b->set_focus_mode(FOCUS_NONE);
		b->connect(SceneStringName(pressed), callable_mp(this, p_method));
		action_bar->add_child(b);
		return b;
	};
	back_button = action(String::utf8("‹ Todo"), String::utf8("Vuelve al tablero (Esc)."), &LagoonicMoodboardPanel::_on_back_pressed);
	action_label = memnew(Label);
	action_label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	action_label->set_custom_minimum_size(Size2(1, 0));
	action_label->set_h_size_flags(SIZE_EXPAND_FILL);
	action_bar->add_child(action_label);
	group_button = action(String::utf8("Agrupar"), String::utf8("Junta las tarjetas elegidas en un grupo nuevo."), &LagoonicMoodboardPanel::_on_group_pressed);
	rename_button = action(String::utf8("Renombrar"), String::utf8("Cambia el nombre del grupo."), &LagoonicMoodboardPanel::_on_rename_pressed);
	ungroup_button = action(String::utf8("Deshacer grupo"), String::utf8("Saca las tarjetas al tablero y quita el grupo."), &LagoonicMoodboardPanel::_on_ungroup_pressed);
	send_button = action(String::utf8("Enviar al chat"), String(), &LagoonicMoodboardPanel::_on_send_pressed);
	generate_button = action(String::utf8("Generar…"), String::utf8("Pide al agente una imagen nueva con un proveedor audiovisual, usando estas referencias."), &LagoonicMoodboardPanel::_on_generate_pressed, false);
	clear_button = action(String::utf8("✕"), String::utf8("Deja de elegir (Esc)."), &LagoonicMoodboardPanel::_on_clear_pressed);

	canvas = memnew(LagoonicMoodboardCanvas);
	add_child(canvas);
	canvas->connect(SNAME("selection_changed"), callable_mp(this, &LagoonicMoodboardPanel::_on_selection_changed));
	canvas->connect(SNAME("group_changed"), callable_mp(this, &LagoonicMoodboardPanel::_on_group_changed));
	canvas->connect(SNAME("model_turn"), callable_mp(this, &LagoonicMoodboardPanel::_on_model_turn));
	canvas->connect(SNAME("model_zoom"), callable_mp(this, &LagoonicMoodboardPanel::_on_model_zoom));
	canvas->connect(SNAME("model_grab"), callable_mp(this, &LagoonicMoodboardPanel::_on_model_grab));
	canvas->connect(SNAME("board_changed"), callable_mp(this, &LagoonicMoodboardPanel::_on_canvas_changed));
	canvas->connect(SNAME("focus_changed"), callable_mp(this, &LagoonicMoodboardPanel::_update_ui));
	canvas->connect(SNAME("files_dropped"), callable_mp(this, &LagoonicMoodboardPanel::add_files));
	canvas->connect(SNAME("note_edit_requested"), callable_mp(this, &LagoonicMoodboardPanel::edit_note));
	canvas->connect(SNAME("paste_requested"), callable_mp(this, &LagoonicMoodboardPanel::paste_clipboard));

	empty_label = memnew(Label);
	empty_label->set_text(String::utf8("Arrastra aquí imágenes o vídeos de referencia,\no pega una imagen con ⌘V."));
	empty_label->set_theme_type_variation(LagoonicVibeTheme::MUTED_LABEL);
	empty_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	empty_label->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	empty_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	empty_label->set_anchors_and_offsets_preset(PRESET_FULL_RECT);
	empty_label->set_mouse_filter(MOUSE_FILTER_IGNORE);
	canvas->add_child(empty_label);

	// The open card's details.
	PanelContainer *box = memnew(PanelContainer);
	box->set_theme_type_variation(LagoonicVibeTheme::AGENT_BUBBLE);
	box->set_visible(false);
	canvas->add_child(box);
	details = box;
	VBoxContainer *col = memnew(VBoxContainer);
	col->add_theme_constant_override(SNAME("separation"), int(6 * EDSCALE));
	box->add_child(col);
	details_title = memnew(Label);
	details_title->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	details_title->set_custom_minimum_size(Size2(1, 0));
	col->add_child(details_title);
	details_info = memnew(Label);
	details_info->set_theme_type_variation(LagoonicVibeTheme::MUTED_LABEL);
	details_info->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	details_info->set_custom_minimum_size(Size2(1, 0));
	details_info->set_v_size_flags(SIZE_EXPAND_FILL);
	details_info->set_clip_text(true);
	col->add_child(details_info);
	pin_button = memnew(Button);
	pin_button->set_focus_mode(FOCUS_NONE);
	pin_button->set_tooltip_text(String::utf8("Las referencias fijadas son el estilo del juego."));
	pin_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicMoodboardPanel::_on_pin_pressed));
	col->add_child(pin_button);
	edit_button = memnew(Button);
	edit_button->set_text(String::utf8("Editar nota"));
	edit_button->set_focus_mode(FOCUS_NONE);
	edit_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicMoodboardPanel::_on_edit_pressed));
	col->add_child(edit_button);
	listen_button = memnew(Button);
	listen_button->set_text(String::utf8("Escuchar"));
	listen_button->set_focus_mode(FOCUS_NONE);
	listen_button->set_visible(false);
	listen_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicMoodboardPanel::_on_listen_pressed));
	col->add_child(listen_button);
	player = memnew(AudioStreamPlayer);
	player->connect(SNAME("finished"), callable_mp(this, &LagoonicMoodboardPanel::_update_ui));
	add_child(player);
	move_button = memnew(MenuButton);
	move_button->set_text(String::utf8("Mover a un grupo"));
	move_button->set_flat(false);
	move_button->set_focus_mode(FOCUS_NONE);
	move_button->connect(SNAME("about_to_popup"), callable_mp(this, &LagoonicMoodboardPanel::_on_move_about_to_popup));
	move_button->get_popup()->connect(SceneStringName(id_pressed), callable_mp(this, &LagoonicMoodboardPanel::_on_move_selected));
	col->add_child(move_button);
	remove_button = memnew(Button);
	remove_button->set_text(String::utf8("Quitar del tablero"));
	remove_button->set_flat(true);
	remove_button->set_focus_mode(FOCUS_NONE);
	remove_button->set_tooltip_text(String::utf8("Borra la copia del tablero; el archivo original no se toca."));
	remove_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicMoodboardPanel::_on_remove_pressed));
	col->add_child(remove_button);

	file_dialog = memnew(EditorFileDialog);
	file_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	file_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILES);
	file_dialog->set_title(String::utf8("Añadir referencias"));
	file_dialog->add_filter("*.png,*.jpg,*.jpeg,*.webp,*.bmp,*.tga,*.svg", String::utf8("Imágenes"));
	file_dialog->add_filter("*.mp4,*.mov,*.m4v,*.webm,*.ogv,*.gif", String::utf8("Vídeos"));
	file_dialog->add_filter("*.wav,*.ogg,*.mp3,*.flac,*.m4a,*.aac", String::utf8("Sonidos"));
	file_dialog->add_filter("*.glb,*.gltf,*.fbx,*.obj", String::utf8("Modelos 3D"));
	file_dialog->connect(SNAME("files_selected"), callable_mp(this, &LagoonicMoodboardPanel::_on_files_selected));
	add_child(file_dialog);

	note_dialog = memnew(AcceptDialog);
	note_dialog->set_ok_button_text(String::utf8("Guardar"));
	note_dialog->add_cancel_button(String::utf8("Cancelar"));
	note_edit = memnew(TextEdit);
	note_edit->set_placeholder(String::utf8("Pixel art 16 px, contornos oscuros, sin degradados…"));
	note_edit->set_line_wrapping_mode(TextEdit::LINE_WRAPPING_BOUNDARY);
	note_dialog->add_child(note_edit);
	note_dialog->connect(SceneStringName(confirmed), callable_mp(this, &LagoonicMoodboardPanel::_on_note_confirmed));
	add_child(note_dialog);

	name_dialog = memnew(AcceptDialog);
	name_dialog->set_ok_button_text(String::utf8("Guardar"));
	name_dialog->add_cancel_button(String::utf8("Cancelar"));
	name_edit = memnew(LineEdit);
	name_edit->set_placeholder(String::utf8("Personajes, Escenarios, Interfaz…"));
	name_dialog->add_child(name_edit);
	name_dialog->register_text_enter(name_edit);
	name_dialog->connect(SceneStringName(confirmed), callable_mp(this, &LagoonicMoodboardPanel::_on_name_confirmed));
	add_child(name_dialog);

	generate_dialog = memnew(AcceptDialog);
	generate_dialog->set_title(String::utf8("Generar con referencias"));
	generate_dialog->set_ok_button_text(String::utf8("Preparar en el chat"));
	generate_dialog->add_cancel_button(String::utf8("Cancelar"));
	VBoxContainer *gen = memnew(VBoxContainer);
	gen->add_theme_constant_override(SNAME("separation"), int(8 * EDSCALE));
	generate_dialog->add_child(gen);
	generate_prompt = memnew(TextEdit);
	generate_prompt->set_placeholder(String::utf8("Una torre en ruinas al atardecer, vista lateral…"));
	generate_prompt->set_line_wrapping_mode(TextEdit::LINE_WRAPPING_BOUNDARY);
	generate_prompt->set_custom_minimum_size(Size2(0, 90 * EDSCALE));
	gen->add_child(generate_prompt);
	generate_provider = memnew(OptionButton);
	generate_provider->add_item(String::utf8("Imagen rápida · fal FLUX"));
	generate_provider->add_item(String::utf8("Ilustración y sprites · fal Recraft"));
	generate_provider->add_item(String::utf8("Higgsfield"));
	generate_provider->add_item(String::utf8("Pixel art · Retro Diffusion"));
	generate_provider->add_item(String::utf8("Efecto de sonido · fal ElevenLabs"));
	generate_provider->select(1);
	gen->add_child(generate_provider);
	generate_style = memnew(CheckBox);
	generate_style->set_text(String::utf8("Respetar el estilo fijado del juego"));
	generate_style->set_pressed(true);
	gen->add_child(generate_style);
	generate_refs = memnew(Label);
	generate_refs->set_theme_type_variation(LagoonicVibeTheme::MUTED_LABEL);
	generate_refs->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	generate_refs->set_custom_minimum_size(Size2(1, 0));
	gen->add_child(generate_refs);
	Label *cost = memnew(Label);
	cost->set_theme_type_variation(LagoonicVibeTheme::MUTED_LABEL);
	cost->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	cost->set_custom_minimum_size(Size2(1, 0));
	cost->set_text(String::utf8("El pedido queda escrito en el chat con las referencias adjuntas: revísalo y envíalo. Generar cuesta saldo del proveedor."));
	gen->add_child(cost);
	generate_dialog->connect(SceneStringName(confirmed), callable_mp(this, &LagoonicMoodboardPanel::_on_generate_confirmed));
	add_child(generate_dialog);

	scan_timer = memnew(Timer);
	scan_timer->set_wait_time(2.0);
	scan_timer->set_autostart(true);
	scan_timer->connect("timeout", callable_mp(this, &LagoonicMoodboardPanel::_scan_folder));
	add_child(scan_timer);
}
