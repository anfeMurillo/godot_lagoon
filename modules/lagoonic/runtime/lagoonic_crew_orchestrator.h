/**************************************************************************/
/*  lagoonic_crew_orchestrator.h                                      */
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

// Lagoonic crew orchestrator (C++ port of the crew queue/rounds logic in
// Core/lagoonic-desktop/src/main/bridge.ts: queueCrewTask, runCrew,
// runOpenCodeWorker, runAcpWorker, wakeBoss, maybeRunCrew, cancelCrew,
// failCrew).
//
// One boss agent answers in the chat and hands tasks to worker agents through
// the `lagoonic-crew` MCP tools (see lagoonic_crew_server.h). Workers run one
// at a time after the boss ends its turn; the boss then gets every report as
// its next message (Orca's orchestration model: start workers, end your turn,
// be woken with their results).
//
// State machine (mirrors bridge.ts; one C++ deviation noted below):
//   idle --queue_task--> queued (crewQueue + crewRoundTasks grow, counter++)
//   queued --maybe_run--> running (inProgress=true, round spliced from queue)
//   running --task ok--> succeeded | --report starts with FAILED--> failed
//   running --cancel()/fail()--> cancelled (queued tasks dropped, the running
//     worker finishes silently, no wake-up follows)
//   running --round done--> adoptExternalSince + editor reload, wake boss as
//     a normal boss turn; tasks queued from the reports chain into the next
//     loop iteration on the SAME thread (see below)
//   running --rounds > MAX_CREW_ROUNDS--> error, stop (send a message first)
//   * --fail()--> dead (waiters conceptually reject, no new crew may start)
//
// Deviation from the TS async version: the wake-up does NOT drop inProgress.
// TS sets crewInProgress=false before wakeBoss and lets maybeRunCrew spawn a
// new async round; with real threads that would allow a second thread. Here
// the round thread loops and re-splices after the wake-up, so workers always
// run one-at-a-time and chained rounds start exactly when the wake ends.
//
// Limits: MAX_CREW_TASKS=4 per turn, MAX_CREW_ROUNDS=5, MAX_CREW_REPORT=6000
// chars, MAX_LINE 1 MiB (enforced by the server).
//
// Threads: maybe_run() spawns ONE worker thread that runs the round task by
// task (300 ms between tasks). The thread never touches the editor: every
// side effect goes through LagoonicCrewRunner, whose implementation (in the
// future bridge/dock) marshals back to the main thread as needed. cancel()
// aborts the running worker (OpenCode abort or ACP cancel via the runner);
// fail() additionally poisons the queue so no new crew may start and any
// in-flight round drains without hanging.
//
// This file calls NOTHING existing: every side effect is a LagoonicCrewRunner
// virtual, implemented later by the bridge/dock without editing this file:
//   - emit_chat(session, text): append the "**<worker>** · tarea #N: title"
//     line to the boss chat (assistant bubble).
//   - emit_error(session, message): surface a crew error in the boss chat.
//   - run_worker(...): ONE worker turn. OpenCode id -> ephemeral session
//     with worker_prompt() (deleted afterwards); ACP id -> silent ACP turn
//     (chatter stays out of the chat). Blocking; must return promptly when
//     abort_worker() is called. Returns the worker's report text ("" when the
//     worker said nothing); sets r_error when the turn itself failed.
//   - abort_worker(): kill the running worker session/turn.
//   - adopt_external_since(msec): attribute the round's outside edits.
//   - request_editor_reload(): DIRECT bridge _do_reload call (no flag file),
//     once per round, not once per task.
//   - wake_boss(...): the wake-up as a NORMAL boss turn with wake_message()
//     (OpenCode prompt_parts_async or ACP sendAcpInternal), then maybe_run()
//     again for chained rounds.
//
// Example round with 2 workers (boss session S, project P, boss "OpenCode"):
//   1. Boss turn calls start_worker twice -> queue_task() returns
//      "Queued as task #1 for Pixel... end your turn..." and similarly #2;
//      crewRoundTasks = [#1 queued, #2 queued].
//   2. Boss ends turn -> maybe_run(S): spawns the round thread.
//   3. Thread: emit_chat(S, "**Pixel** · tarea #1: Draw sky") ->
//      run_worker(opencode/pixel, worker_prompt("OpenCode", #1), ...) ->
//      report R1 ("...`sky.gd`..."); 300 ms; emit_chat(S, "**Vox** · tarea
//      #2: ...") -> run_worker(...) -> report R2.
//   4. Thread: rounds=1 (<= 5); adopt_external_since(t0);
//      request_editor_reload() (direct _do_reload, no flag file);
//      wake_boss(P, boss, S, [#1 ok R1, #2 ok R2]) with wake_message() as a
//      normal boss turn ("Reports of 2 task(s)..." bubble on ACP). If that
//      turn queues more tasks, the loop re-splices and runs the chained
//      round on the same thread; otherwise the thread exits and
//      inProgress=false.

#include "core/object/ref_counted.h"
#include "core/os/mutex.h"
#include "core/os/thread.h"
#include "core/templates/hash_set.h"

#include "lagoonic_crew_server.h" // LagoonicCrewTools.

struct LagoonicCrewTask {
	int number = 0;
	String title;
	String spec;
	String worker_name;
	String worker_id; // 'opencode' or the coding agent's id.
	String status = "queued"; // queued|running|succeeded|failed|cancelled.
	String report;
	String model; // The model the worker said it runs ("" until it starts).
	String requested_model; // The model the boss asked for ("" = worker's default).
	uint64_t started_msec = 0; // Ticks when the worker started; 0 while queued.
	uint64_t finished_msec = 0; // Ticks when it ended; 0 while it runs.
};

struct LagoonicCrewWorkerEntry {
	String id;
	String name;
};

// Future bridge/dock side of the crew. Implemented later; this file only
// calls these virtuals, so no existing file needs edits.
class LagoonicCrewRunner {
public:
	virtual void crew_emit_chat(const String &p_session_id, const String &p_text) = 0;
	virtual void crew_emit_error(const String &p_session_id, const String &p_message) = 0;
	// One blocking worker turn. Returns the report ("..." when the worker
	// said nothing); sets r_error when the turn itself failed. Must return
	// promptly after abort_worker().
	virtual String crew_run_worker(const String &p_worker_id, const String &p_model, const String &p_prompt, const String &p_title, int p_number, const String &p_boss_session_id, String &r_error) = 0;
	// The models worker p_worker_id offers, as text; may start fetching them
	// in the background and say so. Main thread.
	virtual String crew_model_catalog(const String &p_worker_id, const String &p_worker_name) = 0;
	virtual void crew_abort_worker() = 0;
	// Stops one running task's worker; returns promptly.
	virtual void crew_abort_task(int p_number) = 0;
	// Gives a running task's worker more instructions; false when it is not running.
	virtual bool crew_message_task(int p_number, const String &p_message) = 0;
	virtual void crew_adopt_external_since(uint64_t p_since_msec) = 0;
	virtual void crew_request_editor_reload() = 0;
	// The wake-up as a normal boss turn; the implementation calls
	// maybe_run() again afterwards so chained rounds can start.
	virtual void crew_wake_boss(const String &p_boss_session_id, const String &p_wake_text, const String &p_bubble, String &r_error) = 0;
	virtual ~LagoonicCrewRunner() {}
};

class LagoonicCrewOrchestrator : public RefCounted, public LagoonicCrewTools {
	GDCLASS(LagoonicCrewOrchestrator, RefCounted);

public:
	static constexpr int MAX_CREW_TASKS = 4;
	static constexpr int MAX_CREW_ROUNDS = 5;
	static constexpr int MAX_CREW_REPORT = 6000;
	static constexpr int WORKER_GAP_MSEC = 300;

	// Exact ports of crew.ts prompt builders.
	static String boss_instruction(const Vector<String> &p_workers, int p_max_tasks);
	static String worker_prompt(const String &p_boss_name, int p_number, const String &p_title, const String &p_spec);
	static String wake_message(const Vector<LagoonicCrewTask> &p_tasks);

private:
	mutable Mutex mutex;
	Thread *run_thread = nullptr;
	bool run_thread_done = false; // Set by the round thread as its last act.

	LagoonicCrewRunner *runner = nullptr; // Not owned; the future bridge/dock.
	String project_path;
	String boss_id; // 'opencode' or the coding agent's id.
	Vector<LagoonicCrewWorkerEntry> workers;

	Vector<LagoonicCrewTask> crew_queue;
	Vector<LagoonicCrewTask> crew_round_tasks;
	String pending_boss_session; // Set by maybe_run(), consumed by the round thread.
	int crew_task_counter = 0;
	int crew_rounds = 0;
	bool crew_in_progress = false;
	bool crew_cancelled = false;
	String fatal_error; // Set by fail(): no new crew may start.
	HashSet<int> stopped_tasks; // Tasks the user or the boss stopped.
	String last_error;

	void _join_finished_thread();
	static void _thread_entry(void *p_userdata);
	void _run_crew(const String &p_boss_session_id);
	// One task of a round, on its own thread (tasks run in parallel).
	struct TaskJob {
		LagoonicCrewOrchestrator *self = nullptr;
		LagoonicCrewTask *task = nullptr;
		String boss_name;
		String boss_session;
		int start_delay_msec = 0;
	};
	static void _task_entry(void *p_userdata);
	void _run_task(TaskJob &p_job);
	bool _sleep_msec(int p_msec) const; // False when the round must stop now.

	static bool _report_is_failure(const String &p_report);
	void _mirror_task(const LagoonicCrewTask &p_task); // Call with the mutex held.

protected:
	static void _bind_methods();

public:
	void set_runner(LagoonicCrewRunner *p_runner) { runner = p_runner; }
	void set_project(const String &p_path, const String &p_boss_id);
	void set_workers(const Array &p_workers); // Array of {id, name} Dictionaries.

	// LagoonicCrewTools (called by the MCP server on the main thread).
	virtual Vector<String> crew_worker_names() const override;
	virtual String crew_start_worker(const String &p_title, const String &p_spec, const String &p_worker, const String &p_model, bool p_from_boss, String &r_error) override;
	virtual String crew_list_models(const String &p_worker) override;
	virtual Vector<Dictionary> crew_list_workers() const override;
	virtual String crew_stop_worker(int p_number, String &r_error) override;
	virtual String crew_clear_workers() override { return clear_finished(); }
	virtual String crew_message_worker(int p_number, const String &p_message, String &r_error) override;
	// Drops the tasks that ended (succeeded, failed, stopped, cancelled) from the list.
	String clear_finished();
	String message_worker(int p_number, const String &p_message); // For scripts and tests.
	// Stops task p_number, or every running/queued task with 0 (panel button
	// or the boss's stop_worker). Returns what happened, or "" with
	// get_last_error().
	String stop_task(int p_number);
	// A worker states its model when it starts (any thread).
	void note_task_model(const String &p_model, int p_number);

	// queue_task returns the message for the boss, or "" and get_last_error().
	String queue_task(const String &p_title, const String &p_spec, const String &p_worker, bool p_from_boss = false, const String &p_model = String());

	// Stops the round: queued tasks are dropped, the running worker is
	// aborted (finishes silently), no wake-up follows.
	void cancel();
	// Server death or runtime close: waiters must not hang, no new crew.
	void fail(const String &p_message);
	void clear_crew();

	// Call when a boss turn ends: starts the round thread when tasks are
	// queued and no round runs. No-op when in progress, empty, or failed.
	void maybe_run(const String &p_boss_session_id);
	bool is_in_progress() const;
	// Every task the boss started in this session, oldest first, for the UI:
	// {number, title, worker_name, worker_id, model, status, report,
	// started_msec, finished_msec}. Times are engine ticks.
	Array snapshot() const;
	String get_boss_id() const;
	bool is_failed() const;
	int get_rounds() const;
	int get_task_counter() const;
	String get_last_error() const;

	LagoonicCrewOrchestrator();
	~LagoonicCrewOrchestrator();
};
