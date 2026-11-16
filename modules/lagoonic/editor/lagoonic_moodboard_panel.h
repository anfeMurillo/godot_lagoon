/**************************************************************************/
/*  lagoonic_moodboard_panel.h                                            */
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

// Bento "Referencias": the game's mood board. Images, videos and notes in
// res://referencias/ (with a .gdignore: never imported nor exported), laid
// out as a free canvas, a ring, a sphere or a grid.
//
// The ring and the sphere follow flyshare.design's "raum.js": every card has
// a place (x, y, z) in a shape that turns (yaw) and tilts (pitch); one
// projection s = F / (F + z) gives position, size and drawing order. Cards
// stay upright; depth is only size, overlap and fog. Switching shapes flies
// every card from where it is, staggered, with a smootherstep landing.

#include "core/templates/local_vector.h"
#include "scene/gui/box_container.h"
#include "scene/resources/style_box_flat.h"
#include "scene/resources/texture.h"

class AcceptDialog;
class Camera3D;
class Node3D;
class SubViewport;
class AudioStreamPlayer;
class Button;
class EditorFileDialog;
class CheckBox;
class Label;
class LagoonicChatView;
class MenuButton;
class OptionButton;
class Timer;
class LineEdit;
class TextEdit;

struct ModelStage {
	Node3D *pivot = nullptr;
	Camera3D *camera = nullptr;
	float dist = 1.0f;
	float radius = 1.0f;
};

class LagoonicMoodboardCanvas : public Control {
	GDCLASS(LagoonicMoodboardCanvas, Control);

public:
	enum View {
		VIEW_FREE,
		VIEW_RING,
		VIEW_SPHERE,
		VIEW_GRID,
		VIEW_COUNT,
	};

	struct Item {
		String id;
		String kind; // image, video, note, group
		String file; // Inside the board folder.
		String text; // A note's text, or a group's name.
		String group; // The group it is in ("" = the top of the board).
		bool selected = false;
		// A cut-out (PNG with transparency, a 3D model's preview): drawn
		// alone, with no card, shadow or frame around it.
		bool transparent = false;
		Vector2 free_pos; // Free view, in board units.
		bool pinned = false; // Part of the game's style.
		Ref<Texture2D> texture;
		float aspect = 0.8f; // Width / height.

		// Animation (screen space).
		Vector3 rs; // Current x, y and scale.
		float rz = 0.0f;
		Vector3 from;
		float from_z = 0.0f;
		float mt = -1e9f; // Morph time; < -1e8: still.
		float morph_len = 1.25f;
		float hot = 0.0f;
		int slot = 0;
		Vector2 grid_pos;
		float grid_scale = 1.0f;
		Rect2 hit; // Last drawn rectangle.
		float hit_z = 0.0f;
		float alpha = 1.0f;
	};

private:
	LocalVector<Item> items;
	View view = VIEW_RING;
	bool laid_out = false;
	Ref<Texture2D> live_texture;
	bool model_grab = false;

	float yaw = 0.0f;
	float yaw_speed = 6.0f; // Degrees per second.
	float pitch = 0.33f;
	float time = 0.0f;

	Vector2 pan;
	float zoom = 1.0f;

	// Groups: the board shows one level at a time; a group is a stack of
	// its first cards, and touching it deals them out (and back).
	String current_group;
	HashMap<String, LocalVector<int>> members;
	int shown_count = 0;
	bool _visible(const Item &p_item) const { return p_item.group == current_group; }
	void _reslot();
	int _group_index(const String &p_id) const;

	int hot = -1;
	int focus = -1;
	int last_focus = -1;
	float focus_k = 0.0f;

	// Pointer.
	bool pressing = false;
	bool dragged = false;
	bool press_toggle = false; // Cmd/Shift: choose instead of open.
	int drag_item = -1;
	int drop_target = -1; // Free view: the group a dragged card would go into.
	Vector2 press_pos;
	Vector2 last_pos;

	float _side() const;
	float _radius() const;
	Vector2 _card_size(const Item &p_item) const;
	void _layout_grid();
	void _screen(const Item &p_item, Vector3 &r_pos, float &r_z) const;
	Rect2 _focus_rect(const Item &p_item) const;
	int _item_at(const Vector2 &p_pos, int p_except = -1) const;
	void _draw_card(const Item &p_item, const Rect2 &p_rect, float p_alpha);
	void _step(float p_delta);
	// The floor: lines that run to a vanishing point in the middle and fade
	// with distance. It slides as the ring turns, or as the free view pans.
	float floor_x = 0.0f;
	float floor_z = 0.0f;
	float floor_last_yaw = 0.0f;
	Vector2 floor_last_pan;
	void _draw_floor();
	Ref<StyleBoxFlat> shadow_box;
	Ref<StyleBoxFlat> card_box;
	Ref<StyleBoxFlat> edge_box;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	virtual void gui_input(const Ref<InputEvent> &p_event) override;
	virtual bool can_drop_data(const Point2 &p_point, const Variant &p_data) const override;
	virtual void drop_data(const Point2 &p_point, const Variant &p_data) override;
	virtual String get_tooltip(const Point2 &p_pos) const override;

	LocalVector<Item> &get_items() { return items; }
	View get_view() const { return view; }
	void set_view(View p_view, bool p_animate = true);
	int get_focus() const { return focus; }
	float get_focus_k() const { return focus_k; }
	void set_focus(int p_index);
	// New cards fly out from the centre (or the focused card).
	void items_changed(int p_new_from = -1);
	// First appearance: the cards come out from behind the first one.
	void deal();
	Vector2 free_center() const;
	// Where the open card's details go (beside it, or under it when narrow).
	Rect2 get_details_rect() const;
	String get_current_group() const { return current_group; }
	// An open 3D model shows this live render instead of its preview, and
	// dragging over it turns the model (signals model_turn / model_zoom).
	void set_live_texture(const Ref<Texture2D> &p_texture) {
		live_texture = p_texture;
		queue_redraw();
	}
	void enter_group(const String &p_id);
	void exit_group();
	// The ids of the chosen cards (Cmd/Shift + click).
	Vector<String> get_selection() const;
	void clear_selection();
	const LocalVector<int> *get_members(const String &p_group) const { return members.getptr(p_group); }

	LagoonicMoodboardCanvas();
};

class LagoonicMoodboardPanel : public VBoxContainer {
	GDCLASS(LagoonicMoodboardPanel, VBoxContainer);

	LagoonicMoodboardCanvas *canvas = nullptr;
	Button *view_buttons[LagoonicMoodboardCanvas::VIEW_COUNT] = {};
	Label *count_label = nullptr;
	Label *empty_label = nullptr;
	EditorFileDialog *file_dialog = nullptr;
	AcceptDialog *note_dialog = nullptr;
	TextEdit *note_edit = nullptr;
	int editing_note = -1;

	// The card's details, over the board while one is open.
	Control *details = nullptr;
	Label *details_title = nullptr;
	Label *details_info = nullptr;
	Button *pin_button = nullptr;
	Button *edit_button = nullptr;
	Button *remove_button = nullptr;
	MenuButton *move_button = nullptr;

	// Inside a group, or with cards chosen: what to do with them.
	HBoxContainer *action_bar = nullptr;
	Button *back_button = nullptr;
	Label *action_label = nullptr;
	Button *group_button = nullptr;
	Button *rename_button = nullptr;
	Button *ungroup_button = nullptr;
	Button *send_button = nullptr;
	Button *generate_button = nullptr;
	Button *clear_button = nullptr;

	AcceptDialog *name_dialog = nullptr;
	LineEdit *name_edit = nullptr;
	int name_mode = 0; // 0 new group from the selection, 1 rename, 2 new group for the open card.

	AcceptDialog *generate_dialog = nullptr;
	TextEdit *generate_prompt = nullptr;
	OptionButton *generate_provider = nullptr;
	CheckBox *generate_style = nullptr;
	Label *generate_refs = nullptr;
	String inbox_group; // Where generated images that show up go.
	Timer *scan_timer = nullptr;

	LagoonicChatView *chat = nullptr;
	// The open 3D model, live: drag to turn it, wheel to come closer.
	SubViewport *live_vp = nullptr;
	ModelStage live_stage;
	String live_path;
	float live_yaw = 0.0f;
	float live_pitch = 0.0f;
	float live_zoom = 1.0f;
	float live_spin = 0.0f;
	float live_idle = 0.0f;
	bool live_grab = false;
	void _live_open(const String &p_path);
	void _live_close();
	void _live_apply();
	void _on_model_turn(const Vector2 &p_relative);
	void _on_model_zoom(float p_factor);
	void _on_model_grab(bool p_grab);

	// 3D models: a preview rendered once (cached in .thumbs/).
	LocalVector<String> thumb_queue; // Item ids.
	Node *thumb_viewport = nullptr;
	String thumb_id;
	int thumb_frames = 0;
	void _thumb_step();
	void _thumb_finish();

	// Audio: listen to a sound card.
	AudioStreamPlayer *player = nullptr;
	Button *listen_button = nullptr;
	void _on_listen_pressed();

	String board_dir; // Absolute, without the trailing slash.
	bool loaded = false;

	void _load();
	void _save();
	void _load_texture(LagoonicMoodboardCanvas::Item &r_item);
	String _unique_file(const String &p_name) const;
	void _on_view_pressed(int p_view);
	void _on_add_pressed();
	void _on_note_pressed();
	void _on_note_confirmed();
	void _on_files_selected(const PackedStringArray &p_files);
	void _on_os_files_dropped(const PackedStringArray &p_files);
	void _on_canvas_changed();
	void _on_pin_pressed();
	void _on_edit_pressed();
	void _on_remove_pressed();
	void _update_ui();
	void _on_selection_changed();
	void _on_group_changed();
	void _on_back_pressed();
	void _on_group_pressed();
	void _on_rename_pressed();
	void _on_ungroup_pressed();
	void _on_name_confirmed();
	void _on_move_about_to_popup();
	void _on_move_selected(int p_id);
	void _on_send_pressed();
	void _on_generate_pressed();
	void _on_generate_confirmed();
	void _on_clear_pressed();
	void _scan_folder();
	// The cards an action works on: the chosen ones, or else the open group;
	// groups count as their cards.
	Vector<int> _targets() const;
	String _target_name() const;
	String _new_id(const String &p_suffix) const;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	// Copies files (absolute or res://) into the board and adds them.
	void add_files(const PackedStringArray &p_files);
	void add_note(const String &p_text);
	void paste_clipboard();
	void edit_note(int p_index);
	// The chat that "Enviar al chat" and "Generar" fill.
	void setup(LagoonicChatView *p_chat) { chat = p_chat; }

	LagoonicMoodboardPanel();
};
