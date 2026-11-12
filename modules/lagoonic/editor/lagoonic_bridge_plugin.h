/**************************************************************************/
/*  lagoonic_bridge_plugin.h                                            */
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

// Lagoonic editor bridge (baked into the engine as a C++ module).
//
// Implements the TCP + newline-delimited JSON protocol described in
// Core/godot-addon/PROTOCOL.md so the Lagoonic app / lagoonic-mcp bridge
// works without any GDScript plugin:
//
//   1. On editor start the plugin listens on 127.0.0.1 (ephemeral port,
//      fallback range 47111-47130), generates a random token and writes
//      res://.lagoonic/editor.json {protocol, port, token, pid, godot}.
//   2. Clients connect, send {"token": "<token>"}\n as the first line and
//      receive {"ok":true,"protocol":1,"godot":"4.7.x"} (or an ok:false
//      reply followed by a disconnect on a wrong token).
//   3. Afterwards every line is {"id": n, "method": "<m>", "params": {...}}
//      answered with {"id": n, "result": ...} or {"id": n, "error": "..."}.
//      Lines longer than 1 MiB drop the connection.
//
// Supported methods: ping, status, scene_tree, node, reload, open_scene,
// select, screenshot, play, stop, save. Unknown methods answer
// "Unknown method: <m>".
//
// The plugin polls its sockets from NOTIFICATION_PROCESS, so every editor
// call below runs on the main thread. No threads are used.

#include "core/io/tcp_server.h"
#include "editor/plugins/editor_plugin.h"

class StreamPeerTCP;
class LagoonicDock;
class LagoonicVibeWorkspace;

class LagoonicBridgePlugin : public EditorPlugin {
	GDCLASS(LagoonicBridgePlugin, EditorPlugin);

public:
	// Must stay in sync with Core/godot-addon/PROTOCOL.md.
	static constexpr int32_t PROTOCOL_VERSION = 1;
	static constexpr uint64_t MAX_LINE_BYTES = 1048576; // 1 MiB.
	static constexpr int32_t MAX_SCENE_NODES = 600;
	static constexpr int32_t DEFAULT_TREE_DEPTH = 12;
	static constexpr int32_t MAX_NODE_PROPERTIES = 256;

private:
	struct ClientState {
		Ref<StreamPeerTCP> peer;
		String pending; // Buffered, not yet newline-terminated text.
		bool authed = false;
	};

	Ref<TCPServer> server;
	Vector<ClientState> clients;

	LagoonicDock *dock = nullptr;
	LagoonicVibeWorkspace *vibe_workspace = nullptr;
	void _mount_vibe_workspace();
	void _patch_editor_theme();

	String token;
	int port = 0;
	bool running = false;

	uint64_t last_tick_ms = 0;
	uint64_t last_reload_mtime = 0;

	void _notification(int p_what);

	void _start_server();
	// The GDScript plugin (protocol 2: exec, live edits with undo) shipped with
	// lagoonic-mcp. Copied into the project when the editor opens it and
	// enabled once the editor is ready; this C++ bridge stays as the fallback.
	bool addon_pending = false;
	void _install_addon();
	void _enable_addon_when_ready();
	bool _editor_json_is_ours() const;
	void _stop_server();
	void _ensure_dock();
	void _free_dock();
	void _poll();

	void _accept_clients();
	void _service_client(int p_idx);
	void _close_client(int p_idx);
	void _send_line(const Ref<StreamPeerTCP> &p_peer, const Dictionary &p_msg);
	void _send_result(const Ref<StreamPeerTCP> &p_peer, int p_id, const Variant &p_result);
	void _send_error(const Ref<StreamPeerTCP> &p_peer, int p_id, const String &p_message);

	// Returns false when the line was the (failed) handshake and the client
	// entry may no longer exist (it was dropped). True otherwise.
	bool _handle_line(int p_idx, const String &p_line);
	bool _handle_handshake(int p_idx, const Dictionary &p_msg);
	void _dispatch(int p_idx, const Dictionary &p_msg);

	String _make_token() const;

	void _write_editor_json() const;
	void _remove_editor_json() const;
	void _write_editor_state();
	void _check_reload_flag();
	void _lagoonic_reset_run_log();

	// Editor queries. All run on the main thread.
	Dictionary _build_status();
	Node *_resolve_scene_root(const String &p_scene, String &r_error);
	Node *_resolve_scene_node(Node *p_root, const String &p_node_path);
	Dictionary _build_tree_dict(const Node *p_node, const Node *p_root, int p_depth, int &r_count, bool &r_truncated) const;
	Dictionary _build_node_dict(const Node *p_node, const Node *p_root) const;
	bool _do_reload(Dictionary &r_result);
	Dictionary _do_screenshot(const String &p_view);

	static int _dict_int(const Dictionary &p_dict, const StringName &p_key, int p_default);
	static String _dict_string(const Dictionary &p_dict, const StringName &p_key, const String &p_default);
	static bool _is_json_safe(Variant::Type p_type);

protected:
	static void _bind_methods();

public:
	virtual String get_plugin_name() const override { return "Lagoonic"; }

	bool is_bridge_running() const { return running; }
	int get_bridge_port() const { return port; }

	// Appends one line to res://.lagoonic/editor-run.log (created/truncated
	// by `play`). Full stdout + GDScript stack capture is a later phase.
	void bridge_play_log_append(const String &p_line);

	LagoonicBridgePlugin();
	~LagoonicBridgePlugin();
};
