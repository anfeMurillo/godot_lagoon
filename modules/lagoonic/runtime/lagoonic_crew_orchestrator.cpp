/**************************************************************************/
/*  lagoonic_crew_orchestrator.cpp                                    */
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

#include "lagoonic_crew_orchestrator.h"

#include "core/object/class_db.h"
#include "core/os/memory.h"
#include "core/os/os.h"
#include "core/os/time.h"

void LagoonicCrewOrchestrator::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_project", "path", "boss_id"), &LagoonicCrewOrchestrator::set_project);
	ClassDB::bind_method(D_METHOD("set_workers", "workers"), &LagoonicCrewOrchestrator::set_workers);
	ClassDB::bind_method(D_METHOD("queue_task", "title", "spec", "worker"), &LagoonicCrewOrchestrator::queue_task);
	ClassDB::bind_method(D_METHOD("cancel"), &LagoonicCrewOrchestrator::cancel);
	ClassDB::bind_method(D_METHOD("fail", "message"), &LagoonicCrewOrchestrator::fail);
	ClassDB::bind_method(D_METHOD("clear_crew"), &LagoonicCrewOrchestrator::clear_crew);
	ClassDB::bind_method(D_METHOD("maybe_run", "boss_session_id"), &LagoonicCrewOrchestrator::maybe_run);
	ClassDB::bind_method(D_METHOD("is_in_progress"), &LagoonicCrewOrchestrator::is_in_progress);
	ClassDB::bind_method(D_METHOD("is_failed"), &LagoonicCrewOrchestrator::is_failed);
	ClassDB::bind_method(D_METHOD("get_rounds"), &LagoonicCrewOrchestrator::get_rounds);
	ClassDB::bind_method(D_METHOD("get_last_error"), &LagoonicCrewOrchestrator::get_last_error);
}

LagoonicCrewOrchestrator::LagoonicCrewOrchestrator() {
}

LagoonicCrewOrchestrator::~LagoonicCrewOrchestrator() {
	{
		MutexLock lock(mutex);
		crew_cancelled = true;
		crew_queue.clear(); // The in-flight round's trailing check then finds
		// nothing to chain, so no new thread can spawn on a dying object.
	}
	if (runner != nullptr) {
		runner->crew_abort_worker();
	}
	if (run_thread != nullptr) {
		// The round checks crew_cancelled between tasks and after the
		// blocking worker call, so this join always terminates.
		run_thread->wait_to_finish();
		memdelete(run_thread);
		run_thread = nullptr;
	}
}

// ---------------------------------------------------------------- prompts

String LagoonicCrewOrchestrator::boss_instruction(const Vector<String> &p_workers, int p_max_tasks) {
	String names;
	for (int i = 0; i < p_workers.size(); i++) {
		if (i > 0) {
			names += ", ";
		}
		names += p_workers[i];
	}
	return "You lead a crew in Lagoonic: you plan and review, and worker agents implement. Your workers: " +
			names + " (pick one per task with the worker argument). " +
			"Delegate implementation work with the lagoonic-crew start_worker tool: one self-contained task per call " +
			"(target, change, constraints, files it may edit, how to prove it is done); at most " + String::num_int64(p_max_tasks) + " per turn. " +
			"Start every independent task, then end your turn with one short line to the user; Lagoonic runs the workers " +
			"one at a time and sends you their reports as your next message. Never wait or poll. Do quick things (answers, " +
			"reading a file) yourself. When reports arrive, review them (read the files, run checks), then delegate fixes " +
			"or answer the user.";
}

String LagoonicCrewOrchestrator::worker_prompt(const String &p_boss_name, int p_number, const String &p_title, const String &p_spec) {
	String out;
	out += "You are a worker in Lagoonic. Your coordinator, " + p_boss_name + ", gave you the task below.\n";
	out += "The coordinator cannot see this chat and you cannot ask it questions: make reasonable choices and say which assumptions you made.\n";
	out += "Do only this task, yourself.\n";
	out += "End with a short report of three sentences: what you did, what you found, what is left. If you could not finish, start the report with FAILED:.\n";
	out += "Name every file you created or changed in backticks.\n";
	out += "\n";
	out += "=== TASK #" + String::num_int64(p_number) + ": " + p_title + " ===\n";
	out += p_spec;
	return out;
}

String LagoonicCrewOrchestrator::wake_message(const Vector<LagoonicCrewTask> &p_tasks) {
	String out = "<worker_reports>\n";
	for (int i = 0; i < p_tasks.size(); i++) {
		const LagoonicCrewTask &t = p_tasks[i];
		String title = t.title;
		title = title.replace("\"", "'");
		out += "<report task=\"" + String::num_int64(t.number) + "\" worker=\"" + t.worker_name + "\" title=\"" + title + "\" outcome=\"" + t.status + "\">\n";
		String report = t.report.strip_edges();
		out += (report.is_empty() ? String("(no report)") : report) + "\n";
		out += "</report>\n";
	}
	out += "</worker_reports>\n";
	out += "Review these reports. Verify what matters, then delegate fixes or more work with start_worker, or answer the user.";
	return out;
}

// ---------------------------------------------------------------- setup

void LagoonicCrewOrchestrator::set_project(const String &p_path, const String &p_boss_id) {
	MutexLock lock(mutex);
	project_path = p_path;
	boss_id = p_boss_id;
}

void LagoonicCrewOrchestrator::set_workers(const Array &p_workers) {
	MutexLock lock(mutex);
	workers.clear();
	for (int i = 0; i < p_workers.size(); i++) {
		if (p_workers[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		Dictionary d = p_workers[i];
		LagoonicCrewWorkerEntry e;
		e.id = String(d.get("id", ""));
		e.name = String(d.get("name", ""));
		if (!e.id.is_empty() && !e.name.is_empty()) {
			workers.push_back(e);
		}
	}
}

// ---------------------------------------------------------------- queue

Vector<String> LagoonicCrewOrchestrator::crew_worker_names() const {
	MutexLock lock(mutex);
	Vector<String> out;
	for (const LagoonicCrewWorkerEntry &e : workers) {
		out.push_back(e.name);
	}
	return out;
}

Vector<Dictionary> LagoonicCrewOrchestrator::crew_list_workers() const {
	MutexLock lock(mutex);
	Vector<Dictionary> out;
	for (const LagoonicCrewTask &t : crew_round_tasks) {
		Dictionary d;
		d["number"] = t.number;
		d["status"] = t.status;
		d["worker_name"] = t.worker_name;
		d["title"] = t.title;
		out.push_back(d);
	}
	return out;
}

String LagoonicCrewOrchestrator::crew_start_worker(const String &p_title, const String &p_spec, const String &p_worker, String &r_error) {
	// Same path as queue_task; refusals surface as r_error so the server can
	// answer them as a text payload with isError (never as -32603).
	String msg = queue_task(p_title, p_spec, p_worker);
	if (msg.is_empty()) {
		MutexLock lock(mutex);
		r_error = last_error;
		return "";
	}
	r_error = "";
	return msg;
}

String LagoonicCrewOrchestrator::queue_task(const String &p_title, const String &p_spec, const String &p_worker) {
	MutexLock lock(mutex);
	last_error = "";
	if (!fatal_error.is_empty()) {
		last_error = fatal_error;
		return "";
	}
	if (workers.is_empty()) {
		last_error = "No worker agent is set up. Do the task yourself.";
		return "";
	}
	String want = p_worker.strip_edges();
	const LagoonicCrewWorkerEntry *chosen = nullptr;
	if (want.is_empty()) {
		chosen = &workers[0];
	} else {
		for (const LagoonicCrewWorkerEntry &e : workers) {
			if (e.name.to_lower() == want.to_lower() || e.id.to_lower() == want.to_lower()) {
				chosen = &e;
				break;
			}
		}
	}
	if (chosen == nullptr) {
		String names;
		for (int i = 0; i < workers.size(); i++) {
			if (i > 0) {
				names += ", ";
			}
			names += workers[i].name;
		}
		last_error = "Unknown worker \"" + p_worker + "\". Your workers: " + names + ".";
		return "";
	}
	if (crew_queue.size() >= MAX_CREW_TASKS) {
		last_error = "This turn already has " + String::num_int64(MAX_CREW_TASKS) + " worker tasks. End your turn; you can start more when the reports arrive.";
		return "";
	}
	crew_task_counter += 1;
	LagoonicCrewTask task;
	task.number = crew_task_counter;
	task.title = p_title;
	task.spec = p_spec;
	task.worker_name = chosen->name;
	task.worker_id = chosen->id;
	task.status = "queued";
	crew_queue.push_back(task);
	crew_round_tasks.push_back(task);
	return "Queued as task #" + String::num_int64(task.number) + " for " + chosen->name + ". Start any other independent task, then end your turn: " +
			"Lagoonic runs the workers one at a time after your turn and sends you their reports as your next message. Do not wait or poll.";
}

// ---------------------------------------------------------------- control

void LagoonicCrewOrchestrator::cancel() {
	LagoonicCrewRunner *r = nullptr;
	{
		MutexLock lock(mutex);
		crew_cancelled = true;
		crew_queue.clear();
		r = runner;
	}
	// The running worker finishes silently; no wake-up follows because
	// _run_crew checks crew_cancelled before the wake.
	if (r != nullptr) {
		r->crew_abort_worker();
	}
}

void LagoonicCrewOrchestrator::fail(const String &p_message) {
	LagoonicCrewRunner *r = nullptr;
	{
		MutexLock lock(mutex);
		if (fatal_error.is_empty()) {
			fatal_error = p_message.is_empty() ? "The crew failed." : p_message;
		}
		last_error = fatal_error;
		crew_cancelled = true;
		crew_queue.clear();
		// NOTE: crew_in_progress is left alone: the in-flight round thread
		// clears it when it drains. maybe_run() is already blocked by
		// fatal_error, so no second thread can spawn.
		r = runner;
	}
	// Unblock any worker call stuck in the runner: waiters must not hang,
	// and no new crew may start until the owner rebuilds the orchestrator.
	if (r != nullptr) {
		r->crew_abort_worker();
	}
}

void LagoonicCrewOrchestrator::clear_crew() {
	MutexLock lock(mutex);
	crew_queue.clear();
	crew_round_tasks.clear();
}

// ---------------------------------------------------------------- rounds

void LagoonicCrewOrchestrator::maybe_run(const String &p_boss_session_id) {
	MutexLock lock(mutex);
	if (!fatal_error.is_empty() || crew_in_progress || crew_queue.is_empty()) {
		return;
	}
	_join_finished_thread();
	if (run_thread != nullptr) {
		return; // Should not happen (done flag implies joined above), stay safe.
	}
	pending_boss_session = p_boss_session_id;
	crew_in_progress = true;
	run_thread_done = false;
	run_thread = memnew(Thread);
	Thread::ID tid = run_thread->start(_thread_entry, this);
	if (tid == Thread::UNASSIGNED_ID) {
		memdelete(run_thread);
		run_thread = nullptr;
		crew_in_progress = false;
		last_error = "Could not start the crew thread.";
		return;
	}
}

void LagoonicCrewOrchestrator::_join_finished_thread() {
	// Thread::is_started() stays true until wait_to_finish(), so the round
	// thread raises run_thread_done as its last act and we join on that.
	if (run_thread != nullptr && run_thread_done) {
		run_thread->wait_to_finish();
		memdelete(run_thread);
		run_thread = nullptr;
		run_thread_done = false;
	}
}

void LagoonicCrewOrchestrator::_thread_entry(void *p_userdata) {
	LagoonicCrewOrchestrator *self = static_cast<LagoonicCrewOrchestrator *>(p_userdata);
	String session;
	{
		MutexLock lock(self->mutex);
		session = self->pending_boss_session;
	}
	self->_run_crew(session);
}

// Runs the queued worker tasks one at a time, then wakes the boss with every
// report. Runs on the round thread; every side effect goes through the
// runner, which marshals back to the main thread as needed. Chained rounds
// (tasks the boss queues from the reports) run on this SAME thread: the loop
// re-splices after the wake-up, so workers stay one-at-a-time and no second
// thread can ever exist. crew_in_progress stays true across the wake-up for
// the same reason (unlike the TS async version, real threads need the guard).
void LagoonicCrewOrchestrator::_run_crew(const String &p_boss_session_id) {
	uint64_t round_started_at = Time::get_singleton()->get_ticks_msec();
	String boss_name;
	LagoonicCrewRunner *r = runner; // Set once by the owner before maybe_run().
	{
		MutexLock lock(mutex);
		boss_name = boss_id.is_empty() ? String("OpenCode") : boss_id;
	}

	while (true) {
		Vector<LagoonicCrewTask> round;
		{
			MutexLock lock(mutex);
			if (crew_cancelled || !fatal_error.is_empty() || crew_queue.is_empty()) {
				break;
			}
			// A fresh round starts un-cancelled; aborts and user messages
			// during the round set the latch again.
			crew_cancelled = false;
			round = crew_queue; // Splice: the next turn queues into a fresh list.
			crew_queue.clear();
		}
		Vector<LagoonicCrewTask> results;

		for (int i = 0; i < round.size(); i++) {
			{
				MutexLock lock(mutex);
				if (crew_cancelled || !fatal_error.is_empty()) {
					round.write[i].status = "cancelled";
					continue;
				}
				round.write[i].status = "running";
			}
			if (r != nullptr) {
				r->crew_emit_chat(p_boss_session_id, "**" + round[i].worker_name + String::utf8("** · tarea #") + String::num_int64(round[i].number) + ": " + round[i].title);
			}
			String report;
			bool ok = false;
			if (r != nullptr) {
				String error;
				String prompt = worker_prompt(boss_name, round[i].number, round[i].title, round[i].spec);
				report = r->crew_run_worker(round[i].worker_id, prompt, round[i].title, round[i].number, p_boss_session_id, error);
				if (error.is_empty()) {
					ok = !_report_is_failure(report);
				} else {
					report = error.substr(0, MAX_CREW_REPORT);
					ok = false;
				}
			} else {
				report = "The crew runner is gone.";
				ok = false;
			}
			{
				MutexLock lock(mutex);
				if (crew_cancelled || !fatal_error.is_empty()) {
					round.write[i].status = "cancelled";
					continue;
				}
				round.write[i].status = ok ? "succeeded" : "failed";
				report = report.strip_edges();
				round.write[i].report = report.is_empty() ? "(no report)" : report.substr(0, MAX_CREW_REPORT);
				results.push_back(round[i]);
				// Mirror the round status back into the visible round list.
				for (int k = 0; k < crew_round_tasks.size(); k++) {
					if (crew_round_tasks[k].number == round[i].number) {
						crew_round_tasks.write[k].status = round[i].status;
						crew_round_tasks.write[k].report = round[i].report;
						break;
					}
				}
			}
			if (!_sleep_gap()) {
				MutexLock lock(mutex);
				for (int j = i + 1; j < round.size(); j++) {
					round.write[j].status = "cancelled";
				}
				break;
			}
		}

		bool rounds_exceeded = false;
		{
			MutexLock lock(mutex);
			if (crew_cancelled || !fatal_error.is_empty() || results.is_empty()) {
				break; // No wake-up follows a cancelled round.
			}
			crew_rounds += 1;
			rounds_exceeded = crew_rounds > MAX_CREW_ROUNDS;
		}
		if (rounds_exceeded) {
			if (r != nullptr) {
				r->crew_emit_error(p_boss_session_id, "The crew stopped after " + String::num_int64(MAX_CREW_ROUNDS) + " rounds. Send a message to continue.");
			}
			MutexLock lock(mutex);
			crew_in_progress = false;
			break;
		}
		// Worker writes have no session diff to checkpoint: attribute the
		// round's outside edits to the agent and ping the editor once for
		// the whole round instead of once per task.
		if (r != nullptr) {
			r->crew_adopt_external_since(round_started_at);
			r->crew_request_editor_reload(); // Direct bridge _do_reload, no flag file.
			String wake = wake_message(results);
			String bubble = "Reports of " + String::num_int64(results.size()) + " task(s) for " + boss_name;
			String error;
			r->crew_wake_boss(p_boss_session_id, wake, bubble, error);
			if (!error.is_empty()) {
				r->crew_emit_error(p_boss_session_id, error);
			}
		}
		// Loop: tasks queued from the reports (chained round) run next on
		// this same thread. crew_in_progress never dropped, so a concurrent
		// maybe_run() cannot spawn a second thread.
	}

	{
		MutexLock lock(mutex);
		crew_in_progress = false;
		run_thread_done = true;
	}
}

bool LagoonicCrewOrchestrator::_report_is_failure(const String &p_report) {
	return p_report.strip_edges().begins_with("FAILED");
}

bool LagoonicCrewOrchestrator::_sleep_gap() const {
	// 300 ms between tasks, interruptible by cancel()/fail().
	// (OS only offers delay_usec; 50 ms steps keep abort responsive.)
	const int step_usec = 50000;
	for (int waited = 0; waited < WORKER_GAP_MSEC * 1000; waited += step_usec) {
		OS::get_singleton()->delay_usec(step_usec);
		MutexLock lock(mutex);
		if (crew_cancelled || !fatal_error.is_empty()) {
			return false;
		}
	}
	return true;
}

bool LagoonicCrewOrchestrator::is_in_progress() const {
	MutexLock lock(mutex);
	return crew_in_progress;
}

bool LagoonicCrewOrchestrator::is_failed() const {
	MutexLock lock(mutex);
	return !fatal_error.is_empty();
}

int LagoonicCrewOrchestrator::get_rounds() const {
	MutexLock lock(mutex);
	return crew_rounds;
}

int LagoonicCrewOrchestrator::get_task_counter() const {
	MutexLock lock(mutex);
	return crew_task_counter;
}

String LagoonicCrewOrchestrator::get_last_error() const {
	MutexLock lock(mutex);
	// crew_start_worker funnels through queue_task; copy the refusal out.
	// (queue_task already stored it under the same mutex.)
	return last_error;
}
