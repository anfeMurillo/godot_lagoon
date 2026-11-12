/**************************************************************************/
/*  lagoonic_bridge_plugin.cpp                                          */
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

#include "lagoonic_bridge_plugin.h"

#include "lagoonic_dock.h"
#include "lagoonic_vibe_workspace.h"
#include "lagoonic_vibe_theme.h"

#include "../runtime/lagoonic_mcp_locator.h"

#include "core/error/error_macros.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/image.h"
#include "core/io/json.h"
#include "core/io/resource_loader.h"
#include "core/io/stream_peer_tcp.h"
#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "core/object/property_info.h"
#include "core/object/script_language.h"
#include "core/templates/hash_set.h"
#include "core/config/project_settings.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "core/string/ustring.h"
#include "core/version.h"
#include "editor/editor_data.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/script/script_editor_plugin.h"
#include "scene/main/node.h"
#include "scene/resources/style_box_flat.h"
#include "scene/main/viewport.h"
#include "servers/display/display_server.h"

void LagoonicBridgePlugin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_bridge_running"), &LagoonicBridgePlugin::is_bridge_running);
	ClassDB::bind_method(D_METHOD("get_bridge_port"), &LagoonicBridgePlugin::get_bridge_port);
	ClassDB::bind_method(D_METHOD("bridge_play_log_append", "line"), &LagoonicBridgePlugin::bridge_play_log_append);
}

LagoonicBridgePlugin::LagoonicBridgePlugin() {
}

LagoonicBridgePlugin::~LagoonicBridgePlugin() {
	_stop_server();
}

void LagoonicBridgePlugin::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			set_process(true);
			_ensure_dock();
			_install_addon();
			_start_server();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			set_process(false);
			_stop_server();
			_free_dock();
		} break;
		case NOTIFICATION_PROCESS: {
			_poll();
		} break;
	}
}

// ---------------------------------------------------------------- lifecycle

void LagoonicBridgePlugin::_start_server() {
	if (running) {
		return;
	}
	token = _make_token();
	server.instantiate();

	Error err = ERR_CANT_CREATE;
	port = 0;

	// Prefer an ephemeral port; fall back to a fixed range so a client can
	// guess the port even if editor.json is momentarily unavailable.
	err = server->listen(0, IPAddress("127.0.0.1"));
	if (err == OK) {
		port = server->get_local_port();
		if (port <= 0) {
			server->stop();
			err = ERR_CANT_CREATE;
		}
	}
	for (int p = 47111; err != OK && p <= 47130; p++) {
		server->stop();
		err = server->listen(p, IPAddress("127.0.0.1"));
		if (err == OK) {
			port = p;
		}
	}
	if (err != OK) {
		WARN_PRINT("LagoonicBridge: could not bind 127.0.0.1 (tried ephemeral port and 47111-47130). Bridge disabled.");
		server.unref();
		port = 0;
		return;
	}

	running = true;
	last_tick_ms = Time::get_singleton()->get_ticks_msec();
	last_reload_mtime = FileAccess::get_modified_time("res://.lagoonic/editor-reload");
	_write_editor_json();
}

static const char *LAGOONIC_ADDON_CFG = "res://addons/lagoonic/plugin.cfg";

void LagoonicBridgePlugin::_install_addon() {
	// Only for an editor someone is using. Runs without a window (--import,
	// --export-* from scripts and from lagoonic-mcp itself) leave the project
	// alone, unless LAGOONIC_ADDON_INSTALL=1 asks for it (tests).
	if (EditorNode::is_cmdline_mode() && OS::get_singleton()->get_environment("LAGOONIC_ADDON_INSTALL") != "1") {
		return;
	}
	for (const String &arg : OS::get_singleton()->get_cmdline_args()) {
		if (arg.begins_with("--export")) {
			return;
		}
	}
	// Before the first scan, so an updated plugin is the one that loads.
	addon_pending = LagoonicMcpLocator::install_addon(ProjectSettings::get_singleton()->get_resource_path());
}

void LagoonicBridgePlugin::_enable_addon_when_ready() {
	if (!addon_pending) {
		return;
	}
	EditorNode *editor = EditorNode::get_singleton();
	if (editor == nullptr || !editor->is_editor_ready()) {
		return;
	}
	addon_pending = false;
	if (editor->is_addon_plugin_enabled(LAGOONIC_ADDON_CFG)) {
		return;
	}
	// Already listed in project.godot: the editor is still loading it.
	if (ProjectSettings::get_singleton()->has_setting("editor_plugins/enabled")) {
		const PackedStringArray listed = GLOBAL_GET("editor_plugins/enabled");
		if (listed.has(LAGOONIC_ADDON_CFG)) {
			return;
		}
	}
	editor->set_addon_plugin_enabled(LAGOONIC_ADDON_CFG, true, true);
}

// The GDScript plugin writes the same file with its own port once it loads.
bool LagoonicBridgePlugin::_editor_json_is_ours() const {
	const String path = "res://.lagoonic/editor.json";
	if (!FileAccess::exists(path)) {
		return true;
	}
	const Variant parsed = JSON::parse_string(FileAccess::get_file_as_string(path));
	if (parsed.get_type() != Variant::DICTIONARY) {
		return true;
	}
	const Dictionary info = parsed;
	const int their_pid = info.get("pid", 0);
	const int their_port = info.get("port", 0);
	return their_pid != OS::get_singleton()->get_process_id() || their_port == port;
}

void LagoonicBridgePlugin::_stop_server() {
	if (!running && clients.is_empty() && server.is_null()) {
		return;
	}
	// Dropping the Refs closes the underlying sockets; no explicit
	// disconnect dance needed on the main thread.
	clients.clear();
	if (server.is_valid()) {
		if (server->is_listening()) {
			server->stop();
		}
		server.unref();
	}
	running = false;
	_remove_editor_json(); // Needs the port to know the file is ours.
	port = 0;
}

void LagoonicBridgePlugin::_ensure_dock() {
	if (dock != nullptr) {
		return;
	}
	dock = memnew(LagoonicDock);
	add_dock(dock);
	callable_mp(this, &LagoonicBridgePlugin::_mount_vibe_workspace).call_deferred();
}

void LagoonicBridgePlugin::_patch_editor_theme() {
	LagoonicVibeTheme::round_editor_theme(EditorInterface::get_singleton()->get_editor_theme());
}

void LagoonicBridgePlugin::_mount_vibe_workspace() {
	if (vibe_workspace != nullptr || !is_inside_tree()) {
		return;
	}
	Control *base = EditorInterface::get_singleton()->get_base_control();
	if (base == nullptr) {
		return;
	}
	_patch_editor_theme();
	LagoonicVibeTheme::restore_boot_splash_setting(); // Carga terminada.
	base->connect(SceneStringName(theme_changed), callable_mp(this, &LagoonicBridgePlugin::_patch_editor_theme));
	// Pantalla completa por encima del editor clasico.
	vibe_workspace = memnew(LagoonicVibeWorkspace);
	base->add_child(vibe_workspace);
	vibe_workspace->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
}

void LagoonicBridgePlugin::_free_dock() {
	if (dock == nullptr) {
		return;
	}
	remove_dock(dock);
	memdelete(dock);
	dock = nullptr;
	if (vibe_workspace != nullptr) {
		if (vibe_workspace->get_parent()) {
			vibe_workspace->get_parent()->remove_child(vibe_workspace);
		}
		memdelete(vibe_workspace);
		vibe_workspace = nullptr;
	}
}

void LagoonicBridgePlugin::_poll() {
	_enable_addon_when_ready();
	if (!running) {
		return;
	}
	_accept_clients();
	for (int i = clients.size() - 1; i >= 0; i--) {
		_service_client(i);
	}

	uint64_t now_ms = Time::get_singleton()->get_ticks_msec();
	if (now_ms - last_tick_ms >= 1000) {
		last_tick_ms = now_ms;
		_write_editor_state();
		_check_reload_flag();
	}
}

// ---------------------------------------------------------------- transport

void LagoonicBridgePlugin::_accept_clients() {
	if (server.is_null() || !server->is_listening()) {
		return;
	}
	while (server->is_connection_available() && clients.size() < 8) {
		Ref<StreamPeerTCP> conn = server->take_connection();
		if (conn.is_null()) {
			break;
		}
		ClientState st;
		st.peer = conn;
		clients.push_back(st);
	}
}

void LagoonicBridgePlugin::_service_client(int p_idx) {
	if (p_idx < 0 || p_idx >= clients.size()) {
		return;
	}
	// Local copy of the peer: keeps the socket alive even if this client's
	// entry is dropped mid-processing. Element mutation goes through
	// `clients.write[]` (Godot 4 Vector has no mutable operator[]).
	Ref<StreamPeerTCP> peer = clients[p_idx].peer;
	if (peer.is_null() || peer->get_status() != StreamPeerTCP::STATUS_CONNECTED) {
		_close_client(p_idx);
		return;
	}

	int avail = peer->get_available_bytes();
	if (avail > 0) {
		int to_read = avail > 65536 ? 65536 : avail;
		Vector<uint8_t> buf;
		buf.resize(to_read);
		if (peer->get_data(buf.ptrw(), to_read) != OK) {
			_close_client(p_idx);
			return;
		}
		String &pending = clients.write[p_idx].pending;
		pending += String::utf8((const char *)buf.ptr(), to_read);
		if (pending.find("\n") < 0 && (uint64_t)pending.utf8().length() > MAX_LINE_BYTES + 4096) {
			// Never-terminated flood: drop without answering.
			_close_client(p_idx);
			return;
		}
	}

	while (true) {
		if (p_idx >= clients.size()) {
			return; // Dropped while handling a previous line.
		}
		String &pending = clients.write[p_idx].pending;
		int nl = pending.find("\n");
		if (nl < 0) {
			break;
		}
		String line = pending.substr(0, nl);
		pending = pending.substr(nl + 1);
		line = line.trim_suffix("\r");
		if (line.is_empty()) {
			continue;
		}
		if ((uint64_t)line.utf8().length() > MAX_LINE_BYTES) {
			_close_client(p_idx);
			return;
		}
		if (!_handle_line(p_idx, line)) {
			return; // Client was dropped (failed handshake).
		}
	}
}

void LagoonicBridgePlugin::_close_client(int p_idx) {
	if (p_idx < 0 || p_idx >= clients.size()) {
		return;
	}
	clients.remove_at(p_idx);
}

void LagoonicBridgePlugin::_send_line(const Ref<StreamPeerTCP> &p_peer, const Dictionary &p_msg) {
	if (p_peer.is_null() || p_peer->get_status() != StreamPeerTCP::STATUS_CONNECTED) {
		return;
	}
	String text = JSON::stringify(p_msg) + "\n";
	CharString utf8 = text.utf8();
	(void)p_peer->put_data((const uint8_t *)utf8.get_data(), utf8.length());
}

void LagoonicBridgePlugin::_send_result(const Ref<StreamPeerTCP> &p_peer, int p_id, const Variant &p_result) {
	Dictionary msg;
	msg["id"] = p_id;
	msg["result"] = p_result;
	_send_line(p_peer, msg);
}

void LagoonicBridgePlugin::_send_error(const Ref<StreamPeerTCP> &p_peer, int p_id, const String &p_message) {
	Dictionary msg;
	msg["id"] = p_id;
	msg["error"] = p_message;
	_send_line(p_peer, msg);
}

// ---------------------------------------------------------------- protocol

bool LagoonicBridgePlugin::_handle_line(int p_idx, const String &p_line) {
	Variant parsed = JSON::parse_string(p_line);
	if (parsed.get_type() != Variant::DICTIONARY) {
		if (!clients[p_idx].authed) {
			// First line must be the handshake object; anything else is a
			// failed handshake.
			Dictionary resp;
			resp["ok"] = false;
			resp["error"] = "Wrong token.";
			_send_line(clients[p_idx].peer, resp);
			_close_client(p_idx);
			return false;
		}
		_send_error(clients[p_idx].peer, 0, "Invalid JSON.");
		return true;
	}
	Dictionary msg = parsed;
	if (!clients[p_idx].authed) {
		return _handle_handshake(p_idx, msg);
	}
	_dispatch(p_idx, msg);
	return true;
}

bool LagoonicBridgePlugin::_handle_handshake(int p_idx, const Dictionary &p_msg) {
	String t = _dict_string(p_msg, "token", "");
	if (!t.is_empty() && t == token) {
		clients.write[p_idx].authed = true;
		Dictionary resp;
		resp["ok"] = true;
		resp["protocol"] = PROTOCOL_VERSION;
		resp["godot"] = String(GODOT_VERSION_NUMBER);
		_send_line(clients[p_idx].peer, resp);
		return true;
	}
	Dictionary resp;
	resp["ok"] = false;
	resp["error"] = "Wrong token.";
	_send_line(clients[p_idx].peer, resp);
	_close_client(p_idx);
	return false;
}

void LagoonicBridgePlugin::_dispatch(int p_idx, const Dictionary &p_msg) {
	Ref<StreamPeerTCP> peer = clients[p_idx].peer;
	int id = _dict_int(p_msg, "id", 0);
	String method = _dict_string(p_msg, "method", "");
	Dictionary params;
	if (p_msg.has("params") && Variant(p_msg["params"]).get_type() == Variant::DICTIONARY) {
		params = p_msg["params"];
	}

	if (method.is_empty()) {
		_send_error(peer, id, "Missing param: method.");
		return;
	}

	EditorInterface *ei = get_editor_interface();
	if (ei == nullptr && method != "ping") {
		_send_error(peer, id, "Editor not available.");
		return;
	}

	if (method == "ping") {
		Dictionary r;
		r["pong"] = true;
		r["protocol"] = PROTOCOL_VERSION;
		r["godot"] = String(GODOT_VERSION_NUMBER);
		_send_result(peer, id, r);
	} else if (method == "status") {
		_send_result(peer, id, _build_status());
	} else if (method == "scene_tree") {
		String scene = _dict_string(params, "scene", "current");
		int depth = _dict_int(params, "depth", DEFAULT_TREE_DEPTH);
		depth = CLAMP(depth, 1, 32);
		String err;
		Node *root = _resolve_scene_root(scene, err);
		if (root == nullptr) {
			_send_error(peer, id, err);
			return;
		}
		int count = 0;
		bool truncated = false;
		Dictionary tree = _build_tree_dict(root, root, depth, count, truncated);
		Dictionary r;
		r["scene"] = root->get_scene_file_path();
		r["unsaved"] = ei->get_unsaved_scenes().has(root->get_scene_file_path());
		r["tree"] = tree;
		r["truncated"] = truncated;
		_send_result(peer, id, r);
	} else if (method == "node") {
		String scene = _dict_string(params, "scene", "current");
		String node_path = _dict_string(params, "node", ".");
		String err;
		Node *root = _resolve_scene_root(scene, err);
		if (root == nullptr) {
			_send_error(peer, id, err);
			return;
		}
		Node *target = _resolve_scene_node(root, node_path);
		if (target == nullptr) {
			_send_error(peer, id, "Node not found: " + node_path);
			return;
		}
		_send_result(peer, id, _build_node_dict(target, root));
	} else if (method == "reload") {
		Dictionary r;
		if (!_do_reload(r)) {
			_send_error(peer, id, "Reload failed.");
			return;
		}
		_send_result(peer, id, r);
	} else if (method == "open_scene") {
		String scene = _dict_string(params, "scene", "");
		if (scene.is_empty()) {
			_send_error(peer, id, "Missing param: scene.");
			return;
		}
		if (!FileAccess::exists(scene)) {
			_send_error(peer, id, "Scene not found: " + scene);
			return;
		}
		ei->open_scene_from_path(scene);
		_send_result(peer, id, _build_status());
	} else if (method == "select") {
		String node_path = _dict_string(params, "node", "");
		if (node_path.is_empty()) {
			_send_error(peer, id, "Missing param: node.");
			return;
		}
		String err;
		Node *root = _resolve_scene_root("current", err);
		if (root == nullptr) {
			_send_error(peer, id, err);
			return;
		}
		Node *target = _resolve_scene_node(root, node_path);
		if (target == nullptr) {
			_send_error(peer, id, "Node not found: " + node_path);
			return;
		}
		EditorSelection *sel = ei->get_selection();
		if (sel != nullptr) {
			sel->clear();
			sel->add_node(target);
		}
		ei->edit_node(target);
		Dictionary r;
		r["selected"] = node_path;
		_send_result(peer, id, r);
	} else if (method == "screenshot") {
		String view = _dict_string(params, "view", "auto");
		Dictionary r = _do_screenshot(view);
		if (r.has("error")) {
			_send_error(peer, id, r["error"]);
			return;
		}
		_send_result(peer, id, r);
	} else if (method == "play") {
		String scene = _dict_string(params, "scene", "main");
		if (!scene.is_empty() && scene != "main" && scene != "current" && !scene.begins_with("res://")) {
			_send_error(peer, id, "Unknown scene selector: " + scene);
			return;
		}
		if (scene.begins_with("res://") && !FileAccess::exists(scene)) {
			_send_error(peer, id, "Scene not found: " + scene);
			return;
		}
		// Truncate the run log so every `play` starts from a clean file.
		// Full stdout + GDScript stack capture (run_log.gd equivalent) is a later phase.
		_lagoonic_reset_run_log();
		if (scene.is_empty() || scene == "main") {
			ei->play_main_scene();
		} else if (scene == "current") {
			ei->play_current_scene();
		} else {
			ei->play_custom_scene(scene);
		}
		Dictionary r;
		r["playing"] = true;
		r["log"] = "res://.lagoonic/editor-run.log";
		_send_result(peer, id, r);
	} else if (method == "stop") {
		ei->stop_playing_scene();
		Dictionary r;
		r["stopped"] = true;
		_send_result(peer, id, r);
	} else if (method == "save") {
		ei->save_all_scenes();
		_send_result(peer, id, _build_status());
	} else {
		_send_error(peer, id, "Unknown method: " + method);
	}
}

// ---------------------------------------------------------------- helpers

String LagoonicBridgePlugin::_make_token() const {
	Math::randomize();
	static const char *hex = "0123456789abcdef";
	String t;
	for (int i = 0; i < 32; i++) {
		t += String::chr(hex[Math::rand() % 16]);
	}
	return t;
}

void LagoonicBridgePlugin::_write_editor_json() const {
	Dictionary info;
	info["protocol"] = PROTOCOL_VERSION;
	info["port"] = port;
	info["token"] = token;
	info["pid"] = OS::get_singleton()->get_process_id();
	info["godot"] = String(GODOT_VERSION_NUMBER);
	info["started"] = Time::get_singleton()->get_unix_time_from_system();

	Ref<DirAccess> da = DirAccess::open("res://");
	if (da.is_valid()) {
		(void)da->make_dir_recursive(".lagoonic");
	}
	Ref<FileAccess> f = FileAccess::open("res://.lagoonic/editor.json", FileAccess::WRITE);
	if (f.is_valid()) {
		(void)f->store_string(JSON::stringify(info, "  "));
	}
}

void LagoonicBridgePlugin::_remove_editor_json() const {
	if (!_editor_json_is_ours()) {
		return;
	}
	Ref<DirAccess> da = DirAccess::open("res://");
	if (da.is_valid()) {
		da->remove(".lagoonic/editor.json");
	}
}

void LagoonicBridgePlugin::_write_editor_state() {
	EditorInterface *ei = get_editor_interface();
	if (ei == nullptr) {
		return;
	}
	Ref<FileAccess> f = FileAccess::open("res://.lagoonic/editor-state.json", FileAccess::WRITE);
	if (f.is_valid()) {
		(void)f->store_string(JSON::stringify(_build_status()));
	}
}

void LagoonicBridgePlugin::_check_reload_flag() {
	uint64_t mtime = FileAccess::get_modified_time("res://.lagoonic/editor-reload");
	if (mtime == last_reload_mtime) {
		return;
	}
	last_reload_mtime = mtime;
	if (mtime > 0) {
		Dictionary tmp;
		_do_reload(tmp);
	}
}

// Creates (or truncates) res://.lagoonic/editor-run.log at the start of
// every `play` request so each run starts from a clean file.
// NOTE (next phase): full stdout + GDScript stack capture into this file —
// the equivalent of the GDScript run_log.gd — is still TODO. For now the
// file is truncated here and appended via bridge_play_log_append().
void LagoonicBridgePlugin::_lagoonic_reset_run_log() {
	Ref<DirAccess> da = DirAccess::open("res://");
	if (da.is_valid()) {
		(void)da->make_dir_recursive(".lagoonic");
	}
	Ref<FileAccess> f = FileAccess::open("res://.lagoonic/editor-run.log", FileAccess::WRITE);
	if (f.is_valid()) {
		(void)f->store_string("# Lagoonic editor run log\n");
	}
}

void LagoonicBridgePlugin::bridge_play_log_append(const String &p_line) {
	Ref<FileAccess> f = FileAccess::open("res://.lagoonic/editor-run.log", FileAccess::READ_WRITE);
	if (f.is_null()) {
		Ref<DirAccess> da = DirAccess::open("res://");
		if (da.is_valid()) {
			(void)da->make_dir_recursive(".lagoonic");
		}
		f = FileAccess::open("res://.lagoonic/editor-run.log", FileAccess::WRITE_READ);
		if (f.is_null()) {
			return;
		}
	}
	f->seek_end();
	(void)f->store_string(p_line + "\n");
}

// ------------------------------------------------------------ editor queries

Dictionary LagoonicBridgePlugin::_build_status() {
	Dictionary s;
	EditorInterface *ei = get_editor_interface();
	if (ei == nullptr) {
		s["open"] = false;
		return s;
	}

	s["open"] = true;

	bool focused = true;
	if (DisplayServer::get_singleton() != nullptr) {
		focused = DisplayServer::get_singleton()->window_is_focused();
	}
	s["focused"] = focused;

	String dir_name = ProjectSettings::get_singleton()->get_resource_path().get_file();
	Variant cfg_name = ProjectSettings::get_singleton()->get_setting("application/config/name", Variant());
	String project = (cfg_name.get_type() == Variant::STRING) ? String(cfg_name) : dir_name;
	if (project.is_empty()) {
		project = dir_name;
	}
	s["project"] = project;

	Node *root = ei->get_edited_scene_root();
	s["current_scene"] = root != nullptr ? root->get_scene_file_path() : String("");

	PackedStringArray open_scenes = ei->get_open_scenes();
	Array open_arr;
	for (int i = 0; i < open_scenes.size(); i++) {
		open_arr.push_back(open_scenes[i]);
	}
	s["open_scenes"] = open_arr;

	PackedStringArray unsaved_scenes = ei->get_unsaved_scenes();
	Array unsaved_arr;
	for (int i = 0; i < unsaved_scenes.size(); i++) {
		unsaved_arr.push_back(unsaved_scenes[i]);
	}
	s["unsaved_scenes"] = unsaved_arr;

	Array selected_arr;
	EditorSelection *sel = ei->get_selection();
	if (sel != nullptr) {
		TypedArray<Node> selected = sel->get_selected_nodes();
		for (int i = 0; i < selected.size(); i++) {
			Node *n = Object::cast_to<Node>(selected[i]);
			if (n == nullptr) {
				continue;
			}
			if (root != nullptr) {
				selected_arr.push_back(String(root->get_path_to(n)));
			} else {
				selected_arr.push_back(String(n->get_path()));
			}
		}
	}
	s["selected_nodes"] = selected_arr;

	Array scripts_arr;
	ScriptEditor *se = ei->get_script_editor();
	if (se != nullptr) {
		Vector<Ref<Script>> scripts = se->get_open_scripts();
		for (int i = 0; i < scripts.size(); i++) {
			if (scripts[i].is_valid()) {
				scripts_arr.push_back(scripts[i]->get_path());
			}
		}
	}
	s["open_scripts"] = scripts_arr;

	s["playing"] = ei->is_playing_scene();
	return s;
}

Node *LagoonicBridgePlugin::_resolve_scene_root(const String &p_scene, String &r_error) {
	EditorInterface *ei = get_editor_interface();
	if (ei == nullptr) {
		r_error = "Editor not available.";
		return nullptr;
	}
	Node *edited = ei->get_edited_scene_root();
	if (p_scene.is_empty() || p_scene == "current") {
		if (edited == nullptr) {
			r_error = "No scene open.";
		}
		return edited;
	}
	TypedArray<Node> roots = ei->get_open_scene_roots();
	for (int i = 0; i < roots.size(); i++) {
		Node *n = Object::cast_to<Node>(roots[i]);
		if (n != nullptr && n->get_scene_file_path() == p_scene) {
			return n;
		}
	}
	if (edited != nullptr && edited->get_scene_file_path() == p_scene) {
		return edited;
	}
	r_error = "Scene not open: " + p_scene;
	return nullptr;
}

Node *LagoonicBridgePlugin::_resolve_scene_node(Node *p_root, const String &p_node_path) {
	if (p_root == nullptr) {
		return nullptr;
	}
	if (p_node_path.is_empty() || p_node_path == ".") {
		return p_root;
	}
	return p_root->get_node_or_null(NodePath(p_node_path));
}

Dictionary LagoonicBridgePlugin::_build_tree_dict(const Node *p_node, const Node *p_root, int p_depth, int &r_count, bool &r_truncated) const {
	Dictionary d;
	d["name"] = String(p_node->get_name());
	d["type"] = String(p_node->get_class());
	d["path"] = (p_node == p_root) ? String(".") : String(p_root->get_path_to(p_node));

	Variant scr_v = p_node->get_script();
	if (scr_v.get_type() == Variant::OBJECT) {
		const Script *scr = Object::cast_to<Script>(scr_v);
		if (scr != nullptr && !scr->get_path().is_empty()) {
			d["script"] = scr->get_path();
		}
	}
	String inst = p_node->get_scene_file_path();
	if (!inst.is_empty() && p_node != p_root) {
		d["instance"] = inst;
	}

	r_count++;
	if (r_count >= MAX_SCENE_NODES) {
		r_truncated = true;
		return d;
	}
	if (p_depth > 0) {
		Array children;
		// Skip internal nodes so the dump matches the editor scene dock.
		int child_count = p_node->get_child_count(false);
		for (int i = 0; i < child_count; i++) {
			if (r_count >= MAX_SCENE_NODES) {
				r_truncated = true;
				break;
			}
			const Node *child = p_node->get_child(i, false);
			if (child == nullptr) {
				continue;
			}
			children.push_back(_build_tree_dict(child, p_root, p_depth - 1, r_count, r_truncated));
		}
		if (!children.is_empty()) {
			d["children"] = children;
		}
	}
	return d;
}

Dictionary LagoonicBridgePlugin::_build_node_dict(const Node *p_node, const Node *p_root) const {
	Dictionary d;
	d["scene"] = p_root != nullptr ? p_root->get_scene_file_path() : String("");
	d["name"] = String(p_node->get_name());
	d["type"] = String(p_node->get_class());
	if (p_root != nullptr) {
		d["path"] = (p_node == p_root) ? String(".") : String(p_root->get_path_to(p_node));
	} else {
		d["path"] = String(p_node->get_path());
	}

	Variant scr_v = p_node->get_script();
	if (scr_v.get_type() == Variant::OBJECT) {
		const Script *scr = Object::cast_to<Script>(scr_v);
		if (scr != nullptr && !scr->get_path().is_empty()) {
			d["script"] = scr->get_path();
		}
	}
	String inst = p_node->get_scene_file_path();
	if (!inst.is_empty() && p_node != p_root) {
		d["instance"] = inst;
	}

	// Lagoonic: report only non-default values. The ClassDB default cache
	// holds one pristine instance per class; when no default is known
	// (script variables, unregistered properties) fall back to reporting
	// the stored value, capped at MAX_NODE_PROPERTIES as before.
	Dictionary props;
	List<PropertyInfo> plist;
	p_node->get_property_list(&plist);
	int added = 0;
	for (const PropertyInfo &pi : plist) {
		if (added >= MAX_NODE_PROPERTIES) {
			break;
		}
		if (pi.name == StringName("script")) {
			continue;
		}
		if ((pi.usage & PROPERTY_USAGE_STORAGE) == 0) {
			continue;
		}
		if (String(pi.name).begins_with("_")) {
			continue;
		}
		if (!_is_json_safe(pi.type)) {
			continue;
		}
		bool valid = false;
		Variant v = p_node->get(pi.name, &valid);
		if (!valid) {
			continue;
		}
		bool lagoonic_has_default = false;
		Variant lagoonic_default = ClassDB::class_get_default_property_value(p_node->get_class(), pi.name, &lagoonic_has_default);
		if (lagoonic_has_default && v.get_type() == lagoonic_default.get_type()) {
			if (v == lagoonic_default) {
				continue; // Unchanged from the class default: skip.
			}
		}
		props[pi.name] = v;
		added++;
	}
	d["properties"] = props;
	return d;
}

bool LagoonicBridgePlugin::_do_reload(Dictionary &r_result) {
	EditorInterface *ei = get_editor_interface();
	if (ei == nullptr) {
		return false;
	}
	// Public-API-only reload (no EditorNode internals): rescan the
	// filesystem, then reload open scripts whose disk copy changed.
	// Scenes with unsaved edits are never touched; they are reported in
	// conflicts[], keeping the PROTOCOL.md {reloaded[], conflicts[]} shape.
	EditorFileSystem *fs = ei->get_resource_filesystem();
	if (fs != nullptr) {
		fs->scan_changes();
	}
	PackedStringArray unsaved_scenes = ei->get_unsaved_scenes();
	Array conflicts;
	for (int i = 0; i < unsaved_scenes.size(); i++) {
		conflicts.push_back(unsaved_scenes[i]);
	}
	r_result["conflicts"] = conflicts;

	Array lagoonic_reloaded;
	ScriptEditor *lagoonic_se = ei->get_script_editor();
	if (lagoonic_se != nullptr) {
		HashSet<String> lagoonic_unsaved;
		PackedStringArray lagoonic_unsaved_files = lagoonic_se->get_unsaved_files();
		for (int i = 0; i < lagoonic_unsaved_files.size(); i++) {
			lagoonic_unsaved.insert(lagoonic_unsaved_files[i]);
		}
		Vector<Ref<Script>> lagoonic_open = lagoonic_se->get_open_scripts();
		for (int i = 0; i < lagoonic_open.size(); i++) {
			Ref<Script> lagoonic_scr = lagoonic_open[i];
			if (lagoonic_scr.is_null()) {
				continue;
			}
			String lagoonic_path = lagoonic_scr->get_path();
			if (lagoonic_path.is_empty() || lagoonic_scr->is_built_in() || lagoonic_path.contains("::")) {
				continue; // Built-in scene scripts save with their scene; not reloaded here.
			}
			if (lagoonic_unsaved.has(lagoonic_path)) {
				continue; // Protect in-editor edits.
			}
			uint64_t lagoonic_disk_mtime = FileAccess::get_modified_time(lagoonic_path);
			if (lagoonic_disk_mtime == 0) {
				continue;
			}
			uint64_t lagoonic_res_mtime = lagoonic_scr->get_last_modified_time();
			if (lagoonic_res_mtime != 0 && lagoonic_disk_mtime == lagoonic_res_mtime) {
				continue; // Unchanged on disk.
			}
			Ref<Resource> lagoonic_fresh = ResourceLoader::load(lagoonic_path, lagoonic_scr->get_class(), ResourceLoader::CACHE_MODE_IGNORE);
			Ref<Script> lagoonic_fresh_scr = lagoonic_fresh;
			if (lagoonic_fresh_scr.is_null()) {
				continue; // e.g. parse error: keep the current copy.
			}
			lagoonic_scr->set_source_code(lagoonic_fresh_scr->get_source_code());
			if (lagoonic_scr->reload(true) != OK) {
				continue;
			}
			lagoonic_scr->set_last_modified_time(lagoonic_disk_mtime);
			lagoonic_reloaded.push_back(lagoonic_path);
		}
		// NOTE: the open tabs' text buffers and disk baselines are left to
		// the editor's own disk-changed flow (prompt or auto-reload), which
		// syncs the visible CodeEdit via reload_text(). Syncing baselines
		// here would hide that step and leave stale tab text.
	}
	r_result["reloaded"] = lagoonic_reloaded;
	return true;
}

Dictionary LagoonicBridgePlugin::_do_screenshot(const String &p_view) {
	Dictionary r;
	EditorInterface *ei = get_editor_interface();
	if (ei == nullptr) {
		r["error"] = "Editor not available.";
		return r;
	}

	String v = p_view.to_lower();
	String resolved;
	SubViewport *vp = nullptr;
	if (v == "2d") {
		resolved = "2d";
		vp = ei->get_editor_viewport_2d();
	} else if (v == "3d") {
		resolved = "3d";
		vp = ei->get_editor_viewport_3d(0);
	} else if (v == "editor") {
		// TODO: capture the full editor chrome, not just the 2D viewport.
		resolved = "editor";
		vp = ei->get_editor_viewport_2d();
	} else {
		// "auto" (and anything unknown): 3D viewport for Node3D roots,
		// 2D viewport otherwise.
		Node *root = ei->get_edited_scene_root();
		if (root != nullptr && root->is_class("Node3D")) {
			resolved = "3d";
			vp = ei->get_editor_viewport_3d(0);
		} else {
			resolved = "2d";
			vp = ei->get_editor_viewport_2d();
		}
	}

	if (vp == nullptr) {
		r["error"] = "No viewport for view: " + p_view;
		return r;
	}
	Ref<Texture2D> tex = vp->get_texture();
	if (tex.is_null()) {
		r["error"] = "Viewport has no texture.";
		return r;
	}
	Ref<Image> img = tex->get_image();
	if (img.is_null() || img->is_empty()) {
		r["error"] = "Viewport capture failed.";
		return r;
	}

	int w = img->get_width();
	int h = img->get_height();
	if (w <= 0 || h <= 0) {
		r["error"] = "Viewport capture failed.";
		return r;
	}
	if (w > 1280) {
		int nw = 1280;
		int nh = (int)((double)h * (double)nw / (double)w);
		if (nh < 1) {
			nh = 1;
		}
		img->resize(nw, nh, Image::INTERPOLATE_BILINEAR);
		w = nw;
		h = nh;
	}

	Ref<DirAccess> da = DirAccess::open("res://");
	if (da.is_valid()) {
		(void)da->make_dir_recursive(".lagoonic/screenshots");
	}
	String rel = ".lagoonic/screenshots/editor.png";
	if (img->save_png("res://" + rel) != OK) {
		r["error"] = "Could not write screenshot.";
		return r;
	}

	r["file"] = rel;
	r["view"] = resolved;
	r["width"] = w;
	r["height"] = h;
	return r;
}

int LagoonicBridgePlugin::_dict_int(const Dictionary &p_dict, const StringName &p_key, int p_default) {
	if (!p_dict.has(p_key)) {
		return p_default;
	}
	Variant v = p_dict.get(p_key, Variant());
	if (v.get_type() == Variant::NIL) {
		return p_default;
	}
	return int(v);
}

String LagoonicBridgePlugin::_dict_string(const Dictionary &p_dict, const StringName &p_key, const String &p_default) {
	if (!p_dict.has(p_key)) {
		return p_default;
	}
	Variant v = p_dict.get(p_key, Variant());
	if (v.get_type() != Variant::STRING) {
		return p_default;
	}
	return String(v);
}

bool LagoonicBridgePlugin::_is_json_safe(Variant::Type p_type) {
	switch (p_type) {
		case Variant::NIL:
		case Variant::BOOL:
		case Variant::INT:
		case Variant::FLOAT:
		case Variant::STRING:
		case Variant::VECTOR2:
		case Variant::VECTOR2I:
		case Variant::RECT2:
		case Variant::RECT2I:
		case Variant::VECTOR3:
		case Variant::VECTOR3I:
		case Variant::VECTOR4:
		case Variant::VECTOR4I:
		case Variant::COLOR:
		case Variant::ARRAY:
		case Variant::DICTIONARY:
		case Variant::PACKED_BYTE_ARRAY:
		case Variant::PACKED_INT32_ARRAY:
		case Variant::PACKED_INT64_ARRAY:
		case Variant::PACKED_FLOAT32_ARRAY:
		case Variant::PACKED_FLOAT64_ARRAY:
		case Variant::PACKED_STRING_ARRAY:
		case Variant::PACKED_VECTOR2_ARRAY:
		case Variant::PACKED_VECTOR3_ARRAY:
		case Variant::PACKED_COLOR_ARRAY:
			return true;
		default:
			return false;
	}
}
