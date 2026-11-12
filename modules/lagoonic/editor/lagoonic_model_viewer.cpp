/**************************************************************************/
/*  lagoonic_model_viewer.cpp                                             */
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


#include "lagoonic_model_viewer.h"

#include "core/input/input_event.h"
#include "core/object/callable_mp.h"
#include "core/io/resource_loader.h"
#include "editor/themes/editor_scale.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/visual_instance_3d.h"
#include "scene/3d/world_environment.h"
#include "scene/animation/animation_player.h"
#include "scene/main/viewport.h"
#include "scene/resources/environment.h"
#include "scene/resources/packed_scene.h"

bool LagoonicModelViewer::can_show(const String &p_res_path) {
	const String ext = p_res_path.get_extension().to_lower();
	return ext == "glb" || ext == "gltf" || ext == "fbx" || ext == "obj" || ext == "blend";
}

static void _lagoonic_collect_aabb(Node *p_node, AABB &r_aabb, bool &r_has) {
	VisualInstance3D *vi = Object::cast_to<VisualInstance3D>(p_node);
	if (vi != nullptr && vi->is_visible_in_tree()) {
		const AABB box = vi->get_global_transform().xform(vi->get_aabb());
		if (box.size.length() > 0.0001f) {
			if (r_has) {
				r_aabb.merge_with(box);
			} else {
				r_aabb = box;
				r_has = true;
			}
		}
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_lagoonic_collect_aabb(p_node->get_child(i), r_aabb, r_has);
	}
}

static AnimationPlayer *_lagoonic_find_player(Node *p_node) {
	if (AnimationPlayer *ap = Object::cast_to<AnimationPlayer>(p_node)) {
		return ap;
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		if (AnimationPlayer *ap = _lagoonic_find_player(p_node->get_child(i))) {
			return ap;
		}
	}
	return nullptr;
}

void LagoonicModelViewer::_load() {
	loaded = true;
	Ref<Resource> res = ResourceLoader::load(path);
	Node3D *model = nullptr;
	Ref<PackedScene> scene = res;
	if (scene.is_valid()) {
		Node *inst = scene->instantiate();
		model = Object::cast_to<Node3D>(inst);
		if (model == nullptr && inst != nullptr) {
			memdelete(inst);
		}
	}
	if (model == nullptr) {
		hide();
		return;
	}
	pivot->add_child(model);
	AABB box;
	bool has = false;
	_lagoonic_collect_aabb(model, box, has);
	if (!has) {
		box = AABB(Vector3(-0.5, -0.5, -0.5), Vector3(1, 1, 1));
	}
	// Centra el modelo en el pivote para que gire sobre si mismo.
	model->set_position(-box.get_center());
	center = Vector3();
	distance = MAX(0.5f, box.size.length() * 1.15f);
	if (AnimationPlayer *ap = _lagoonic_find_player(model)) {
		LocalVector<StringName> names;
		ap->get_animation_list(&names);
		for (const StringName &n : names) {
			if (String(n) != "RESET") {
				Ref<Animation> anim = ap->get_animation(n);
				if (anim.is_valid()) {
					anim->set_loop_mode(Animation::LOOP_LINEAR);
				}
				ap->play(n);
				break;
			}
		}
	}
	_update_camera();
}

void LagoonicModelViewer::_update_camera() {
	pivot->set_rotation(Vector3(0, yaw, 0));
	const Vector3 offset = Vector3(0, -Math::sin(pitch), Math::cos(pitch)) * distance;
	camera->set_position(center + offset);
	camera->look_at(center, Vector3(0, 1, 0));
	camera->set_near(MAX(0.01f, distance * 0.01f));
	camera->set_far(distance * 20.0f);
}

void LagoonicModelViewer::gui_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid()) {
		if (mb->get_button_index() == MouseButton::LEFT) {
			dragging = mb->is_pressed();
			idle_time = 0.0f;
			accept_event();
		} else if (mb->is_pressed() && (mb->get_button_index() == MouseButton::WHEEL_UP || mb->get_button_index() == MouseButton::WHEEL_DOWN)) {
			distance *= mb->get_button_index() == MouseButton::WHEEL_UP ? 0.9f : 1.1f;
			_update_camera();
			accept_event();
		}
	}
	Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_valid() && dragging) {
		yaw += mm->get_relative().x * 0.01f;
		pitch = CLAMP(pitch - mm->get_relative().y * 0.01f, -1.3f, 1.3f);
		idle_time = 0.0f;
		_update_camera();
		accept_event();
	}
}

void LagoonicModelViewer::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			callable_mp(this, &LagoonicModelViewer::_load).call_deferred();
			set_process_internal(true);
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			if (!is_visible_in_tree()) {
				break;
			}
			// Gira solo hasta que el usuario lo toca (y vuelve tras 3 s).
			idle_time += get_process_delta_time();
			if (loaded && !dragging && idle_time > 3.0f) {
				yaw += get_process_delta_time() * 0.6f;
				_update_camera();
			}
		} break;
	}
}

LagoonicModelViewer::LagoonicModelViewer() {
	set_stretch(true);
	set_custom_minimum_size(Size2(0, 300) * EDSCALE);
	set_mouse_filter(MOUSE_FILTER_STOP);
	set_tooltip_text(String::utf8("Arrastra para girar · rueda para acercar"));
	idle_time = 10.0f;

	viewport = memnew(SubViewport);
	viewport->set_use_own_world_3d(true);
	viewport->set_transparent_background(true);
	viewport->set_msaa_3d(Viewport::MSAA_4X);
	viewport->set_update_mode(SubViewport::UPDATE_WHEN_VISIBLE);
	add_child(viewport);

	WorldEnvironment *we = memnew(WorldEnvironment);
	Ref<Environment> env;
	env.instantiate();
	env->set_background(Environment::BG_CLEAR_COLOR);
	env->set_ambient_source(Environment::AMBIENT_SOURCE_COLOR);
	env->set_ambient_light_color(Color(0.75, 0.78, 0.85));
	env->set_ambient_light_energy(0.6);
	env->set_tonemapper(Environment::TONE_MAPPER_FILMIC);
	we->set_environment(env);
	viewport->add_child(we);

	DirectionalLight3D *key = memnew(DirectionalLight3D);
	key->set_rotation(Vector3(-0.8, 0.6, 0));
	key->set_param(Light3D::PARAM_ENERGY, 1.2);
	viewport->add_child(key);
	DirectionalLight3D *fill = memnew(DirectionalLight3D);
	fill->set_rotation(Vector3(-0.3, -2.4, 0));
	fill->set_param(Light3D::PARAM_ENERGY, 0.5);
	viewport->add_child(fill);

	pivot = memnew(Node3D);
	viewport->add_child(pivot);
	camera = memnew(Camera3D);
	camera->set_fov(40);
	viewport->add_child(camera);
	camera->set_current(true);
}
