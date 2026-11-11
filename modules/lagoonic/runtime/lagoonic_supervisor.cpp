/**************************************************************************/
/*  lagoonic_supervisor.cpp                                               */
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

#include "lagoonic_supervisor.h"

#include "core/crypto/crypto.h"
#include "core/crypto/crypto_core.h"
#include "core/error/error_macros.h"
#include "core/io/http_client.h"
#include "core/io/ip_address.h"
#include "core/io/tcp_server.h"
#include "core/object/class_db.h"
#include "core/os/memory.h"
#include "core/os/os.h"

void LagoonicSupervisor::_bind_methods() {
	ClassDB::bind_method(D_METHOD("random_password"), &LagoonicSupervisor::random_password);
	ClassDB::bind_method(D_METHOD("find_free_port"), &LagoonicSupervisor::find_free_port);
	ClassDB::bind_method(D_METHOD("start", "opencode_bin", "config_path", "project_dir", "default_model", "secret_env", "timeout_sec"), &LagoonicSupervisor::start, DEFVAL(60.0));
	ClassDB::bind_method(D_METHOD("stop"), &LagoonicSupervisor::stop);
	ClassDB::bind_method(D_METHOD("is_running"), &LagoonicSupervisor::is_running);
	ClassDB::bind_method(D_METHOD("check_health"), &LagoonicSupervisor::check_health);
	ClassDB::bind_method(D_METHOD("get_port"), &LagoonicSupervisor::get_port);
	ClassDB::bind_method(D_METHOD("get_pid"), &LagoonicSupervisor::get_pid);
	ClassDB::bind_method(D_METHOD("get_password"), &LagoonicSupervisor::get_password);

	ADD_SIGNAL(MethodInfo("unexpected_exit",
			PropertyInfo(Variant::INT, "exit_code"),
			PropertyInfo(Variant::STRING, "hint")));
}

LagoonicSupervisor::LagoonicSupervisor() {}
LagoonicSupervisor::~LagoonicSupervisor() {
	if (started && !stopping) {
		stop();
	}
}

String LagoonicSupervisor::random_password() {
	Crypto *crypto = Crypto::create();
	ERR_FAIL_NULL_V_MSG(crypto, "", "LagoonicSupervisor: no Crypto backend for random password.");
	PackedByteArray raw = crypto->generate_random_bytes(24);
	memdelete(crypto);
	ERR_FAIL_COND_V_MSG(raw.size() != 24, "", "LagoonicSupervisor: could not generate random password.");
	String b64 = CryptoCore::b64_encode_str(raw.ptr(), (size_t)raw.size());
	// base64url without padding (matches Node randomBytes().toString('base64url')).
	b64 = b64.replace("+", "-").replace("/", "_");
	while (b64.ends_with("=")) {
		b64 = b64.substr(0, b64.length() - 1);
	}
	return b64;
}

int LagoonicSupervisor::find_free_port() {
	Ref<TCPServer> server;
	server.instantiate();
	ERR_FAIL_COND_V_MSG(server.is_null(), 0, "LagoonicSupervisor: no TCPServer backend.");
	Error err = server->listen(0, IPAddress("127.0.0.1"));
	ERR_FAIL_COND_V_MSG(err != OK, 0, "LagoonicSupervisor: could not bind a free port.");
	// No stop() on TCPServer: dropping the Ref closes the listen socket.
	// The port is free again when we return (TOCTOU documented in header).
	return server->get_local_port();
}

Error LagoonicSupervisor::start(const String &p_opencode_bin, const String &p_config_path,
		const String &p_project_dir, const String &p_default_model,
		const Dictionary &p_secret_env, double p_timeout_sec) {
	ERR_FAIL_COND_V_MSG(p_opencode_bin.is_empty(), ERR_INVALID_PARAMETER, "LagoonicSupervisor: opencode binary path must not be blank.");
	ERR_FAIL_COND_V_MSG(p_config_path.is_empty(), ERR_INVALID_PARAMETER, "LagoonicSupervisor: config path must not be blank.");
	ERR_FAIL_COND_V_MSG(p_project_dir.is_empty(), ERR_INVALID_PARAMETER, "LagoonicSupervisor: project dir must not be blank.");

	stop();
	stopping = false;

	int free_port = find_free_port();
	ERR_FAIL_COND_V_MSG(free_port <= 0, ERR_CANT_CREATE, "LagoonicSupervisor: no free port available.");
	String pw = random_password();
	ERR_FAIL_COND_V_MSG(pw.is_empty(), ERR_CANT_CREATE, "LagoonicSupervisor: no random password available.");

	OS *os = OS::get_singleton();
	// Install env for the child, remembering previous values to restore.
	Dictionary env;
	env["OPENCODE_SERVER_PASSWORD"] = pw;
	env["OPENCODE_CONFIG"] = p_config_path;
	env["OPENCODE_DISABLE_AUTOUPDATE"] = "1";
	env["LAGOONIC_PROJECT_DIR"] = p_project_dir;
	env["LAGOONIC_DEFAULT_MODEL"] = p_default_model;
	Array secret_keys = p_secret_env.keys();
	for (int i = 0; i < secret_keys.size(); i++) {
		env[secret_keys[i]] = p_secret_env[secret_keys[i]];
	}
	Dictionary previous;
	Array names = env.keys();
	for (int i = 0; i < names.size(); i++) {
		String name = names[i];
		if (os->has_environment(name)) {
			previous[name] = os->get_environment(name);
		}
		String value = env[name];
		os->set_environment(name, value);
	}

	List<String> args;
	args.push_back("serve");
	args.push_back("--hostname");
	args.push_back("127.0.0.1");
	args.push_back("--port");
	args.push_back(itos(free_port));
	ProcessID child_id = 0;
	Error spawn_err = os->create_process(p_opencode_bin, args, &child_id);

	// Restore our environment immediately after spawn.
	for (int i = 0; i < names.size(); i++) {
		String name = names[i];
		if (previous.has(name)) {
			String old_value = previous[name];
			os->set_environment(name, old_value);
		} else {
			os->unset_environment(name);
		}
	}

	ERR_FAIL_COND_V_MSG(spawn_err != OK, spawn_err, "LagoonicSupervisor: could not spawn opencode.");
	if (child_id == 0) {
		return ERR_CANT_CREATE;
	}

	port = free_port;
	pid = (int64_t)child_id;
	password = pw;
	started = true;
	healthy_once = false;

	// Poll GET /global/health until the deadline (mirrors waitForHealthy).
	uint64_t deadline = os->get_ticks_msec() + (uint64_t)(MAX(1.0, p_timeout_sec) * 1000.0);
	bool alive_seen = false;
	bool died_early = false;
	for (;;) {
		if (!os->is_process_running((ProcessID)child_id)) {
			died_early = true;
			break;
		}
		if (probe_once()) {
			alive_seen = true;
			break;
		}
		if (os->get_ticks_msec() >= deadline) {
			break;
		}
		os->delay_usec(250000);
	}
	if (died_early) {
		started = false;
		pid = 0;
		port = 0;
		ERR_FAIL_V_MSG(ERR_CANT_CREATE, "LagoonicSupervisor: opencode server exited before becoming healthy.");
	}
	if (!alive_seen) {
		stop();
		ERR_FAIL_V_MSG(ERR_TIMEOUT, "LagoonicSupervisor: opencode server did not become healthy.");
	}
	healthy_once = true;
	return OK;
}

void LagoonicSupervisor::stop() {
	stopping = true;
	if (!started || pid == 0) {
		started = false;
		pid = 0;
		port = 0;
		return;
	}
	OS *os = OS::get_singleton();
	if (os == nullptr) { // Engine tearing down; nothing safe left to do.
		started = false;
		pid = 0;
		port = 0;
		return;
	}
#ifdef WINDOWS_ENABLED
	// Whole tree kill, like the TS supervisor's `taskkill /T /F`.
	List<String> args;
	args.push_back("/PID");
	args.push_back(itos((int64_t)pid));
	args.push_back("/T");
	args.push_back("/F");
	os->execute("taskkill", args, nullptr, nullptr, false, nullptr, false);
#else
	os->kill((ProcessID)pid);
#endif
	// Give it up to 3 s to exit.
	uint64_t deadline = os->get_ticks_msec() + 3000;
	while (os->is_process_running((ProcessID)pid) && os->get_ticks_msec() < deadline) {
		os->delay_usec(100000);
	}
	started = false;
	pid = 0;
	port = 0;
}

bool LagoonicSupervisor::is_running() const {
	OS *os = OS::get_singleton();
	return started && !stopping && pid != 0 && os != nullptr &&
			os->is_process_running((ProcessID)pid);
}

bool LagoonicSupervisor::check_health() {
	if (!started || pid == 0) {
		return false;
	}
	OS *os = OS::get_singleton();
	if (os == nullptr) {
		return false;
	}
	if (!os->is_process_running((ProcessID)pid)) {
		if (healthy_once) {
			int code = os->get_process_exit_code((ProcessID)pid);
			started = false;
			pid = 0;
			port = 0;
			emit_unexpected(code, "OpenCode exited unexpectedly.");
		} else {
			started = false;
			pid = 0;
			port = 0;
		}
		return false;
	}
	bool ok = probe_once();
	if (ok) {
		healthy_once = true;
		return true;
	}
	if (healthy_once) {
		// Process alive but unreachable: treat as an unexpected exit so the
		// caller restarts it; the exit code is unknowable here (-1).
		emit_unexpected(-1, "OpenCode stopped answering health checks.");
	}
	return false;
}

int LagoonicSupervisor::get_port() const {
	return port;
}

int64_t LagoonicSupervisor::get_pid() const {
	return pid;
}

String LagoonicSupervisor::get_password() const {
	return password;
}

bool LagoonicSupervisor::probe_once() const {
	if (port <= 0) {
		return false;
	}
	OS *os = OS::get_singleton();
	if (os == nullptr) {
		return false;
	}
	Ref<HTTPClient> client(HTTPClient::create());
	if (client.is_null()) {
		return false;
	}
	if (client->connect_to_host("127.0.0.1", port) != OK) {
		return false;
	}
	uint64_t deadline = os->get_ticks_msec() + 5000;
	while (client->get_status() == HTTPClient::STATUS_CONNECTING ||
			client->get_status() == HTTPClient::STATUS_RESOLVING) {
		if (client->poll() != OK) {
			return false;
		}
		if (os->get_ticks_msec() >= deadline) {
			return false;
		}
		os->delay_usec(10000);
	}
	if (client->get_status() != HTTPClient::STATUS_CONNECTED) {
		return false;
	}
	Vector<String> headers;
	headers.push_back("Authorization: Bearer " + password);
	headers.push_back("Connection: close");
	if (client->request(HTTPClient::METHOD_GET, "/global/health", headers, nullptr, 0) != OK) {
		return false;
	}
	while (client->get_status() == HTTPClient::STATUS_REQUESTING) {
		if (client->poll() != OK) {
			return false;
		}
		if (os->get_ticks_msec() >= deadline) {
			return false;
		}
		os->delay_usec(10000);
	}
	// Any complete HTTP response means the server is up: auth-shape agnostic
	// (a 401 is still a live server).
	bool answered = client->has_response() ||
			client->get_status() == HTTPClient::STATUS_BODY ||
			client->get_status() == HTTPClient::STATUS_CONNECTED;
	if (answered) {
		while (client->get_status() == HTTPClient::STATUS_BODY) {
			if (client->poll() != OK) {
				break;
			}
			client->read_response_body_chunk();
		}
	}
	client->close();
	return answered;
}

void LagoonicSupervisor::emit_unexpected(int p_code, const String &p_hint) {
	emit_signal("unexpected_exit", p_code, p_hint);
}
