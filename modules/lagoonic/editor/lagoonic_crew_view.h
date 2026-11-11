/**************************************************************************/
/*  lagoonic_crew_view.h                                              */
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

// Tarjeta "Equipo Crew" del panel agentico Lagoonic.
//
// Muestra el jefe (chat agent actual) y los workers seleccionables del
// equipo, mas la lista de tareas con su estado. Es un VBoxContainer puro:
// no habla con el puente; el LagoonicChatView (pestana Chat) lo contiene y
// el LagoonicDock historico lo alimenta con setters desde los AgentEvent
// `todos` / `onChatAgent` del contrato (contract.ts).
//
// Eventos salientes (via set_event_handler, y senal Godot si la clase se
// registra algun dia en ClassDB):
//   worker_toggled(agent_id: String, enabled: bool)

#include "scene/gui/box_container.h"

#include "core/templates/hash_map.h"

class CheckButton;
class Label;

class LagoonicCrewView : public VBoxContainer {
	GDCLASS(LagoonicCrewView, VBoxContainer);

public:
	enum TaskState {
		TASK_QUEUED,
		TASK_RUNNING,
		TASK_SUCCEEDED,
		TASK_FAILED,
	};

private:
	struct TaskRow {
		HBoxContainer *box = nullptr;
		Label *dot = nullptr;
		Label *text = nullptr;
	};

	Label *boss_label = nullptr;
	HBoxContainer *workers_row = nullptr;
	VBoxContainer *tasks_box = nullptr;
	Label *empty_label = nullptr;
	Control *hide_target = nullptr; // Se oculta entero cuando no hay tareas.

	Callable event_callback;
	HashMap<StringName, int> signal_argc;
	HashMap<String, CheckButton *> worker_checks;
	HashMap<String, TaskRow> task_rows;

	void _fire(const StringName &p_event, const Variant &p_a = Variant(), const Variant &p_b = Variant());
	void _on_worker_toggled(bool p_pressed, const String &p_agent_id);

	static TaskState _state_from_string(const String &p_state);
	static String _state_label(TaskState p_state);
	static Color _state_color(TaskState p_state);

protected:
	static void _bind_methods();

public:
	void set_hide_when_empty(Control *p_target) { hide_target = p_target; }
	// Canal de eventos sin registro en ClassDB: el dock conecta un unico
	// Callable (event: StringName, a: Variant, b: Variant).
	void set_event_handler(const Callable &p_handler) { event_callback = p_handler; }

	void set_boss_name(const String &p_name);
	// Array de Dictionary {id: String, name: String, enabled: bool}.
	void set_workers(const Array &p_workers);
	// Array de Dictionary {id: String, text: String, state: String}
	// donde state es queued/running/succeeded/failed (se aceptan tambien
	// pending/in_progress/completed/cancelled del TodoState del contrato).
	void set_tasks(const Array &p_tasks);
	void update_task_state(const String &p_task_id, const String &p_state);
	void clear_tasks();

	int get_worker_count() const;
	int get_task_count() const;

	LagoonicCrewView();
};
