/**************************************************************************/
/*  lagoonic_model_viewer.h                                               */
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

// Visor 3D en vivo para el chat (modelos .glb/.gltf y escenas 3D), como el
// ModelViewer de movil: gira solo, se arrastra para rotar, la rueda acerca
// y reproduce la primera animacion si la hay.

#include "scene/gui/subviewport_container.h"

class SubViewport;
class Node3D;
class Camera3D;
class InputEvent;

class LagoonicModelViewer : public SubViewportContainer {
	GDCLASS(LagoonicModelViewer, SubViewportContainer);

	SubViewport *viewport = nullptr;
	Node3D *pivot = nullptr;
	Camera3D *camera = nullptr;
	String path;
	bool loaded = false;
	bool dragging = false;
	float yaw = 0.0f;
	float pitch = -0.25f;
	float distance = 3.0f;
	float idle_time = 0.0f;
	Vector3 center;

	void _load();
	void _update_camera();

protected:
	void _notification(int p_what);
	virtual void gui_input(const Ref<InputEvent> &p_event) override;

public:
	// True si el recurso es una escena con algo 3D que mostrar.
	static bool can_show(const String &p_res_path);
	void set_model_path(const String &p_res_path) { path = p_res_path; }

	LagoonicModelViewer();
};
