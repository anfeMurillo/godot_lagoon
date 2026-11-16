/**************************************************************************/
/*  lagoonic_model_match.h                                                */
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

// Turns the model the boss asks for ("hydrogen high", "gpt-6-luna",
// "muse spark") into one model the worker offers, plus an effort level.
// Candidates are {value, name} Dictionaries; the value is what the agent
// expects, the name what people read.

#include "core/string/ustring.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

namespace LagoonicModelMatch {

inline bool is_effort_word(const String &p_word) {
	static const char *words[] = { "minimal", "low", "medium", "high", "xhigh", "max", "ultra", nullptr };
	for (int i = 0; words[i] != nullptr; i++) {
		if (p_word == words[i]) {
			return true;
		}
	}
	return false;
}

// Lower case, with every separator turned into one space.
inline String normalize(const String &p_text) {
	String out = p_text.to_lower();
	static const char *seps[] = { "-", "_", "/", ".", ":", "(", ")", ",", nullptr };
	for (int i = 0; seps[i] != nullptr; i++) {
		out = out.replace(seps[i], " ");
	}
	out = out.replace(String::utf8("·"), " ");
	while (out.contains("  ")) {
		out = out.replace("  ", " ");
	}
	return " " + out.strip_edges() + " ";
}

// Splits "hydrogen high" into the model words and the effort ("high").
inline void split_request(const String &p_request, Vector<String> &r_words, String &r_effort) {
	r_words.clear();
	r_effort = String();
	const Vector<String> parts = normalize(p_request).strip_edges().split(" ", false);
	for (const String &part : parts) {
		if (is_effort_word(part)) {
			r_effort = part;
		} else {
			r_words.push_back(part);
		}
	}
}

inline bool _has_all(const String &p_haystack, const Vector<String> &p_words) {
	for (const String &w : p_words) {
		// Whole words first ("sol" must not match "solar"); digits may join ("6" in "6 1").
		if (!p_haystack.contains(" " + w + " ") && !p_haystack.contains(" " + w)) {
			return false;
		}
	}
	return !p_words.is_empty();
}

// Best candidate value for the request, or "" when none fits. Models that put
// the effort in the id ("gemini-4-hydrogen-high") match it there first.
inline String best(const Array &p_candidates, const String &p_request) {
	Vector<String> words;
	String effort;
	split_request(p_request, words, effort);
	const String exact = p_request.strip_edges().to_lower();
	// 1) Exact value or name.
	for (int i = 0; i < p_candidates.size(); i++) {
		const Dictionary c = p_candidates[i];
		if (String(c.get("value", "")).to_lower() == exact || String(c.get("name", "")).to_lower() == exact) {
			return c.get("value", "");
		}
	}
	// 2) Every word (effort included), then 3) every model word. Shortest wins:
	// "luna" picks "gpt-6-luna" before "gpt-6-luna-preview".
	for (int pass = 0; pass < 2; pass++) {
		Vector<String> wanted = words;
		if (pass == 0) {
			if (effort.is_empty()) {
				continue;
			}
			wanted.push_back(effort);
		}
		String found;
		for (int i = 0; i < p_candidates.size(); i++) {
			const Dictionary c = p_candidates[i];
			const String value = c.get("value", "");
			const String hay = normalize(value + " " + String(c.get("name", "")));
			if (_has_all(hay, wanted) && (found.is_empty() || value.length() < found.length())) {
				found = value;
			}
		}
		if (!found.is_empty()) {
			return found;
		}
	}
	return String();
}

// "value (name), ..." for error messages and list_models.
inline String describe(const Array &p_candidates, int p_max = 60) {
	String out;
	for (int i = 0; i < p_candidates.size() && i < p_max; i++) {
		const Dictionary c = p_candidates[i];
		const String value = c.get("value", "");
		const String name = c.get("name", "");
		out += (i > 0 ? ", " : "") + value + ((name.is_empty() || name == value) ? String() : " (" + name + ")");
	}
	if (p_candidates.size() > p_max) {
		out += vformat(", ... (%d more)", p_candidates.size() - p_max);
	}
	return out;
}

} // namespace LagoonicModelMatch
