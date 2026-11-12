/**************************************************************************/
/*  lagoonic_opencode_v2.h                                                */
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

// OpenCode 2.x ("v2 API") helpers for LagoonicOpenCodeClient.
//
// C++ port of Core/lagoonic-desktop/src/main/opencode-v2-events.ts and the
// pure helpers of opencode-client-v2.ts, checked against OpenCode 2.2.7:
//   - LagoonicV2FrameTranslator turns `GET /api/event` envelopes
//     {id, type, data} into the 1.x frames {id, type, properties} that
//     LagoonicEventMapper reads. 2.x writes one assistant message per model
//     step; every step of one execution folds into the first message id,
//     as 1.x showed one message per turn.
//   - lagoonic_v2_fold_messages() folds `GET /api/session/:id/message` the
//     same way and returns the client's 1.x message rows.
//   - lagoonic_v2_prompt_body() turns 1.x prompt parts into {text, files}.

#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

class LagoonicV2FrameTranslator {
public:
	// Returns 0..n 1.x frames (Dictionary {id, type, properties}).
	Array translate(const Dictionary &p_envelope);

private:
	struct Turn {
		String turn_id;
		bool has_text = false;
	};
	struct ToolCall {
		String name;
		Dictionary input;
		String message_id;
	};
	struct TextBlock {
		String prefix;
		String sent;
	};

	String _turn_for(const String &p_session_id, const String &p_assistant_message_id);
	ToolCall *_tool_call(const String &p_session_id, const Dictionary &p_data);
	void _end_turn(const String &p_session_id);
	void _mark_text(const String &p_session_id);

	HashMap<String, Turn> turns;
	HashMap<String, String> message_turn;
	HashMap<String, ToolCall> tools;
	HashMap<String, TextBlock> texts;
};

// `GET /api/session/:id/message` items (oldest first) -> 1.x message rows
// {id, session_id, role, time, parts: [{id, session_id, message_id, type,
// text?, tool?, state?}]}.
Array lagoonic_v2_fold_messages(const String &p_session_id, const Array &p_raw);

// 1.x prompt parts ([{type: "text", text}, {type: "file", url, filename}])
// -> 2.x prompt body {text, files?: [{uri, name?}]}.
Dictionary lagoonic_v2_prompt_body(const Array &p_parts);
