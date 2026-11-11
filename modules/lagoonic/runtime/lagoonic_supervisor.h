/**************************************************************************/
/*  lagoonic_supervisor.h                                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             LAGOONIC ENGINE                            */
/*                   https://github.com/lagoonic/engine                   */
/**************************************************************************/
/* Copyright (c) 2026 Lagoonic contributors.                              */
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
/* License: MIT, same as the Godot Engine (see LICENSE.txt upstream).    */
/**************************************************************************/

#pragma once

// LagoonicSupervisor: owns the `opencode serve` child process.
//
// C++ port of Core/lagoonic-desktop/src/main/supervisor.ts:
//   opencode serve --hostname 127.0.0.1 --port <free>
// with a random 24-byte base64url OPENCODE_SERVER_PASSWORD, polled via
// GET /global/health with a 60 s timeout.
//
// Environment: OS::create_process() has no per-spawn env block, so start()
// installs OPENCODE_SERVER_PASSWORD / OPENCODE_CONFIG /
// OPENCODE_DISABLE_AUTOUPDATE / LAGOONIC_PROJECT_DIR / LAGOONIC_DEFAULT_MODEL
// plus the secret env map (provider and asset keys from LagoonicVault) into
// the current process, spawns, then restores the previous values. The child
// inherits them; nothing is written to disk.
//
// Stopping: Windows kills the whole tree with `taskkill /PID <pid> /T /F`.
// POSIX uses OS::kill(pid) (SIGTERM to the server pid): Godot's
// OS::create_process() does not setsid(2), so the child shares our process
// group and a negative-pid group kill would also signal ourselves. A full
// group kill on POSIX is future work once spawning can create a session;
// document this instead of risking self-termination.
//
// Liveness: OS::create_process() is fire-and-forget (no exit callback), so
// unexpected exits are observed through OS::is_process_running() /
// OS::get_process_exit_code() plus check_health(): after the server was
// healthy once, a dead process or a failed probe emits
// `unexpected_exit(exit_code, hint)` (-1 when the code is unknowable) and the
// caller restarts on next message, like the TS supervisor's onUnexpectedExit
// hook.
//
// Signal:
//   unexpected_exit(exit_code: int, hint: String)

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/variant/dictionary.h"

class LagoonicSupervisor : public RefCounted {
	GDCLASS(LagoonicSupervisor, RefCounted);

	bool started = false;
	bool stopping = false;
	bool healthy_once = false;
	int port = 0;
	int64_t pid = 0;
	String password;

protected:
	static void _bind_methods();

public:
	// 24 random bytes, base64url without padding (matches TS randomPassword()).
	String random_password();
	// Binds a TCPServer to 127.0.0.1:0 and returns the assigned port.
	// TOCTOU: the port is freed before the server binds it (same as the TS
	// findFreePort()); a collision surfaces as a failed health poll.
	int find_free_port();

	// Spawns `p_opencode_bin serve --hostname 127.0.0.1 --port <free>` and
	// blocks (up to p_timeout_sec, default 60) polling GET /global/health.
	// p_secret_env maps env names (e.g. ANTHROPIC_API_KEY) to values; values
	// are never logged. Returns OK once healthy. On failure the child is
	// stopped and an error returned.
	Error start(const String &p_opencode_bin, const String &p_config_path,
			const String &p_project_dir, const String &p_default_model,
			const Dictionary &p_secret_env, double p_timeout_sec = 60.0);
	void stop();
	bool is_running() const;

	// Single GET /global/health probe. Updates state; if the server was
	// healthy before and now answers nothing, emits unexpected_exit.
	bool check_health();

	int get_port() const;
	int64_t get_pid() const;
	// Needed by the HTTP client to talk to the server; never log it.
	String get_password() const;

	LagoonicSupervisor();
	~LagoonicSupervisor();

private:
	bool probe_once() const;
	void emit_unexpected(int p_code, const String &p_hint);
};
