/**************************************************************************/
/*  lagoonic_event_mapper.h                                           */
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

// Turns the OpenCode 1.x SSE stream of one session into dock events.
//
// C++ port of Core/lagoonic-desktop/src/main/event-mapper.ts (plus
// tool-narration.ts displayToolName/toolLabel/extractToolDetail and
// diff.ts snapshotDiffToFileDiff). Shapes checked against the v1.19.5 tag:
// `server.connected` first, `message.updated` with info {id, sessionID,
// role}, `message.part.updated` with the whole `part {...}`, text parts
// repeated on every update, `message.part.delta`, `session.status`
// {status: {type: busy|idle|retry}}, `session.error`, `todo.updated`,
// `permission.asked` / `permission.replied`. Only assistant text is shown;
// only the unseen suffix of each part is emitted.
//
// Worker waiter (desktop crew): worker sessions are ephemeral and named
// `crew-worker-*`. Their frames arrive on the same /event stream; feed()
// separates them from the main chat (they are counted in
// lagoonic_get_dropped_worker_frames() and produce no chat events) so the
// boss transcript never mixes with worker traffic.
//
// Returned event dictionaries mirror the shared contract AgentEvent with
// snake_case keys for GDScript:
//   {type:"message", session_id, message:{id, role, text, tools:[{id,
//    label, state, detail?}], created_at}}       (message.text is cumulative)
//   {type:"todos", session_id, todos:[{id, text, state}]}
//   {type:"permission", request:{id, session_id, title, detail?}}
//   {type:"busy", session_id, busy}
//   {type:"error", session_id, message}
// feed() returns {events: Array, turn_finished: bool, failure: String}.

#include "core/object/ref_counted.h"

#include "core/string/ustring.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

class LagoonicEventMapper : public RefCounted {
	GDCLASS(LagoonicEventMapper, RefCounted);

public:
	// Ephemeral worker sessions never join the main transcript.
	static constexpr const char *LAGOONIC_WORKER_PREFIX = "crew-worker-";
	// Matches diff.ts DIFF_SIDE_LIMIT (512 KB): sides over this are absent.
	static constexpr int64_t LAGOONIC_DIFF_SIDE_LIMIT = 512 * 1024;

	LagoonicEventMapper();

	void lagoonic_configure(const String &p_session_id);
	void lagoonic_set_session(const String &p_session_id);
	String lagoonic_get_session() const;
	bool lagoonic_is_connected() const;

	// Main entry point: maps one SSE frame {id, type, properties}.
	Dictionary lagoonic_feed(const Dictionary &p_frame);
	// Messages assembled so far, oldest first.
	Array lagoonic_snapshot() const;
	// Clears every accumulated state (call when switching sessions).
	void lagoonic_reset();

	// Parses one SSE data payload into {id, type, properties}; returns an
	// empty Dictionary when the payload is malformed.
	Dictionary lagoonic_parse_frame(const String &p_payload) const;

	// --- tool narration (tool-narration.ts) ---
	// "lagoonic_scene_create" -> "Godot: scene create"; "bash" -> "Bash".
	String lagoonic_display_tool_name(const String &p_raw) const;
	// First readable argument of a tool state (never a secret: only the
	// argument value, same as the TS original shows in chat rows).
	String lagoonic_extract_tool_detail(const Dictionary &p_state) const;
	// "Godot: scene create" + detail -> "Godot: scene create · player.tscn".
	String lagoonic_tool_label(const String &p_raw_tool, const String &p_detail) const;
	bool lagoonic_is_hidden_tool(const String &p_raw_tool) const;

	// --- worker waiter separation ---
	bool lagoonic_is_worker_session(const String &p_session_id) const;
	// True when this frame belongs to a foreign (worker) session and was
	// therefore separated from the main chat.
	bool lagoonic_is_worker_frame(const Dictionary &p_frame) const;
	int64_t lagoonic_get_dropped_worker_frames() const;

	// --- diffs (diff.ts) ---
	// "a\n-b\n c" style unified patch -> {before, after} recovered contents.
	Dictionary lagoonic_parse_unified_patch(const String &p_patch) const;
	// [{file|path, patch, additions, deletions, status}] ->
	// [{path, status, additions, deletions, before?, after?}].
	// `before` is absent for added files, `after` for deleted ones, and
	// either side is absent for binary diffs (empty patch) or sides over
	// 512 KB.
	Array lagoonic_snapshot_diff_to_file_diff(const Array &p_entries) const;

protected:
	static void _bind_methods();

private:
	Dictionary _lagoonic_empty_result() const;
	Dictionary _lagoonic_message_event(const Dictionary &p_message) const;
	Dictionary _lagoonic_ensure_message(const String &p_id, const String &p_role);
	bool _lagoonic_is_this_session(const String &p_id) const;
	static String _lagoonic_frame_session(const Dictionary &p_frame);
	Dictionary _lagoonic_on_message_updated(const Dictionary &p_props);
	Dictionary _lagoonic_on_part_updated(const Dictionary &p_props);
	Dictionary _lagoonic_on_part_delta(const Dictionary &p_props);
	Dictionary _lagoonic_on_tool_part(const String &p_part_id, const Dictionary &p_part, const String &p_message_id);
	Dictionary _lagoonic_on_permission_asked(const Dictionary &p_props);
	static String _lagoonic_tool_detail(const Dictionary &p_state);
	static String _lagoonic_short_name(const String &p_path);
	static String _lagoonic_capitalize(const String &p_word);
	static String _lagoonic_todo_state(const String &p_status);
	static String _lagoonic_error_message(const Dictionary &p_error);

	String lagoonic_session_id;
	bool lagoonic_connected = false;
	bool lagoonic_saw_activity = false;
	int64_t lagoonic_dropped_worker_frames = 0;

	// Set emulation via Dictionary (keys only): assistant/user message ids.
	Dictionary lagoonic_assistant_messages;
	Dictionary lagoonic_user_messages;
	// part_id -> type / message id; part_id -> emitted text length.
	Dictionary lagoonic_part_types;
	Dictionary lagoonic_part_messages;
	Dictionary lagoonic_emitted_text;
	// part_id -> last tool status string.
	Dictionary lagoonic_tool_status;
	// message_id -> message Dictionary; insertion order kept separately.
	Dictionary lagoonic_messages;
	Array lagoonic_message_order;
	// message_id -> Array of tool-row Dictionaries.
	Dictionary lagoonic_tools_by_message;
};
