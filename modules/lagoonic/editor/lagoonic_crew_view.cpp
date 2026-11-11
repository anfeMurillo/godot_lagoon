/**************************************************************************/
/*  lagoonic_crew_view.cpp                                            */
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

#include "lagoonic_crew_view.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/gui/check_button.h"
#include "scene/gui/label.h"

void LagoonicCrewView::_fire(const StringName &p_event, const Variant &p_a, const Variant &p_b) {
	if (event_callback.is_valid()) {
		event_callback.call(p_event, p_a, p_b);
	}
	if (!has_signal(p_event)) {
		return;
	}
	const int argc = signal_argc.has(p_event) ? signal_argc[p_event] : 0;
	if (argc <= 0) {
		emit_signal(p_event);
	} else if (argc == 1) {
		emit_signal(p_event, p_a);
	} else {
		emit_signal(p_event, p_a, p_b);
	}
}

void LagoonicCrewView::_on_worker_toggled(bool p_pressed, const String &p_agent_id) {
	_fire(SNAME("worker_toggled"), p_agent_id, p_pressed);
}

LagoonicCrewView::TaskState LagoonicCrewView::_state_from_string(const String &p_state) {
	const String s = p_state.to_lower();
	if (s == "running" || s == "in_progress") {
		return TASK_RUNNING;
	}
	if (s == "succeeded" || s == "completed") {
		return TASK_SUCCEEDED;
	}
	if (s == "failed" || s == "cancelled" || s == "canceled") {
		return TASK_FAILED;
	}
	return TASK_QUEUED; // queued / pending / desconocido.
}

String LagoonicCrewView::_state_label(TaskState p_state) {
	switch (p_state) {
		case TASK_RUNNING:
			return TTRC("en curso");
		case TASK_SUCCEEDED:
			return TTRC("completada");
		case TASK_FAILED:
			return TTRC("fallida");
		case TASK_QUEUED:
		default:
			return TTRC("en cola");
	}
}

Color LagoonicCrewView::_state_color(TaskState p_state) {
	switch (p_state) {
		case TASK_RUNNING:
			return Color(0.95, 0.75, 0.25);
		case TASK_SUCCEEDED:
			return Color(0.35, 0.85, 0.4);
		case TASK_FAILED:
			return Color(0.9, 0.35, 0.3);
		case TASK_QUEUED:
		default:
			return Color(0.6, 0.6, 0.6);
	}
}

void LagoonicCrewView::set_boss_name(const String &p_name) {
	if (boss_label == nullptr) {
		return;
	}
	boss_label->set_text(vformat(TTRC("Jefe: %s"), p_name.is_empty() ? TTRC("OpenCode") : p_name));
}

void LagoonicCrewView::set_workers(const Array &p_workers) {
	for (KeyValue<String, CheckButton *> kv : worker_checks) {
		if (kv.value != nullptr) {
			kv.value->queue_free();
		}
	}
	worker_checks.clear();

	if (workers_row == nullptr) {
		return;
	}
	for (int i = 0; i < p_workers.size(); i++) {
		const Dictionary d = p_workers[i];
		const String agent_id = String(d.get("id", ""));
		if (agent_id.is_empty()) {
			continue;
		}
		const String agent_name = String(d.get("name", agent_id));
		const bool enabled = bool(d.get("enabled", false));

		CheckButton *check = memnew(CheckButton);
		check->set_text(agent_name);
		check->set_tooltip_text(vformat(TTRC("Incluir a %s como worker del equipo."), agent_name));
		check->set_pressed_no_signal(enabled);
		check->connect(StringName("toggled"), callable_mp(this, &LagoonicCrewView::_on_worker_toggled).bind(agent_id));
		workers_row->add_child(check);
		worker_checks[agent_id] = check;
	}
}

void LagoonicCrewView::set_tasks(const Array &p_tasks) {
	clear_tasks();
	if (tasks_box == nullptr || empty_label == nullptr) {
		return;
	}
	empty_label->set_visible(p_tasks.is_empty());
	if (hide_target != nullptr) {
		hide_target->set_visible(!p_tasks.is_empty());
	}
	for (int i = 0; i < p_tasks.size(); i++) {
		const Dictionary d = p_tasks[i];
		const String task_id = String(d.get("id", itos(i)));
		const String text = String(d.get("text", ""));
		const TaskState state = _state_from_string(String(d.get("state", "queued")));

		HBoxContainer *row = memnew(HBoxContainer);
		row->add_theme_constant_override(SNAME("separation"), 4);
		tasks_box->add_child(row);

		Label *dot = memnew(Label);
		dot->set_text(String::chr(0x25CF)); // ●
		dot->add_theme_color_override(SNAME("font_color"), _state_color(state));
		row->add_child(dot);

		Label *label = memnew(Label);
		label->set_text(vformat(String::utf8("%s — %s"), text, _state_label(state)));
		label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		label->set_clip_text(true);
		label->set_tooltip_text(text);
		row->add_child(label);

		TaskRow stored;
		stored.box = row;
		stored.dot = dot;
		stored.text = label;
		task_rows[task_id] = stored;
	}
}

void LagoonicCrewView::update_task_state(const String &p_task_id, const String &p_state) {
	if (!task_rows.has(p_task_id)) {
		return;
	}
	const TaskState state = _state_from_string(p_state);
	TaskRow &row = task_rows[p_task_id];
	if (row.dot != nullptr) {
		row.dot->add_theme_color_override(SNAME("font_color"), _state_color(state));
	}
	if (row.text != nullptr) {
		String full = row.text->get_tooltip_text();
		if (full.is_empty()) {
			full = row.text->get_text();
		}
		row.text->set_text(vformat(String::utf8("%s — %s"), full, _state_label(state)));
	}
}

void LagoonicCrewView::clear_tasks() {
	for (KeyValue<String, TaskRow> kv : task_rows) {
		if (kv.value.box != nullptr) {
			kv.value.box->queue_free();
		}
	}
	task_rows.clear();
	if (empty_label != nullptr) {
		empty_label->set_visible(true);
	}
	if (hide_target != nullptr) {
		hide_target->set_visible(false);
	}
}

int LagoonicCrewView::get_worker_count() const {
	return worker_checks.size();
}

int LagoonicCrewView::get_task_count() const {
	return task_rows.size();
}

void LagoonicCrewView::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_event_handler", "handler"), &LagoonicCrewView::set_event_handler);
	ClassDB::bind_method(D_METHOD("set_boss_name", "name"), &LagoonicCrewView::set_boss_name);
	ClassDB::bind_method(D_METHOD("set_workers", "workers"), &LagoonicCrewView::set_workers);
	ClassDB::bind_method(D_METHOD("set_tasks", "tasks"), &LagoonicCrewView::set_tasks);
	ClassDB::bind_method(D_METHOD("update_task_state", "task_id", "state"), &LagoonicCrewView::update_task_state);
	ClassDB::bind_method(D_METHOD("clear_tasks"), &LagoonicCrewView::clear_tasks);

	ADD_SIGNAL(MethodInfo("worker_toggled",
			PropertyInfo(Variant::STRING, "agent_id"),
			PropertyInfo(Variant::BOOL, "enabled")));
}

LagoonicCrewView::LagoonicCrewView() {
	signal_argc[SNAME("worker_toggled")] = 2;

	Label *title = memnew(Label);
	title->set_text(TTRC("Equipo Crew"));
	title->set_theme_type_variation("LagoonicRoleLabel");
	add_child(title);

	HBoxContainer *boss_row = memnew(HBoxContainer);
	boss_row->add_theme_constant_override(SNAME("separation"), 6);
	add_child(boss_row);

	boss_label = memnew(Label);
	boss_label->set_text(vformat(TTRC("Jefe: %s"), TTRC("OpenCode")));
	boss_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	boss_row->add_child(boss_label);

	workers_row = memnew(HBoxContainer);
	workers_row->add_theme_constant_override(SNAME("separation"), 8);
	add_child(workers_row);

	tasks_box = memnew(VBoxContainer);
	tasks_box->add_theme_constant_override(SNAME("separation"), 2);
	add_child(tasks_box);

	empty_label = memnew(Label);
	empty_label->set_text(TTRC("Sin tareas de momento."));
	empty_label->add_theme_color_override(SNAME("font_color"), Color(0.6, 0.6, 0.6));
	tasks_box->add_child(empty_label);
}
