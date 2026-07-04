/**************************************************************************/
/*  editor_automation_commands.cpp                                        */
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

#include "editor_automation_commands.h"

#include "core/input/input_event.h"
#include "core/input/shortcut.h"
#include "core/templates/hash_set.h"
#include "core/templates/sort_array.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_node.h"
#include "editor/settings/editor_command_palette.h"
#include "editor/settings/editor_settings.h"
#endif

namespace {

String _read_string(const Dictionary &p_dict, const char *p_key, const String &p_default = String()) {
	if (!p_dict.has(p_key)) {
		return p_default;
	}
	const Variant value = p_dict.get(p_key, Variant());
	if (value.get_type() != Variant::STRING && value.get_type() != Variant::STRING_NAME) {
		return p_default;
	}
	return value;
}

bool _read_bool(const Dictionary &p_dict, const char *p_key, bool p_default) {
	if (!p_dict.has(p_key)) {
		return p_default;
	}
	const Variant value = p_dict.get(p_key, Variant());
	if (value.get_type() != Variant::BOOL) {
		return p_default;
	}
	return value;
}

int _read_int(const Dictionary &p_dict, const char *p_key, int p_default) {
	if (!p_dict.has(p_key)) {
		return p_default;
	}
	const Variant value = p_dict.get(p_key, Variant());
	if (!value.is_num()) {
		return p_default;
	}
	return (int)value;
}

bool _editor_viewport_available() {
#ifdef TOOLS_ENABLED
	return EditorNode::get_singleton() != nullptr && EditorNode::get_singleton()->get_viewport() != nullptr;
#else
	return false;
#endif
}

bool _push_shortcut_to_editor(const Ref<Shortcut> &p_shortcut) {
#ifdef TOOLS_ENABLED
	if (!_editor_viewport_available() || p_shortcut.is_null()) {
		return false;
	}
	Ref<InputEventShortcut> ev;
	ev.instantiate();
	ev->set_shortcut(p_shortcut);
	EditorNode::get_singleton()->get_viewport()->push_input(ev, false);
	return true;
#else
	return false;
#endif
}

} // namespace

String EditorAutomationCommands::_category_from_key(const String &p_key) {
	const int slash = p_key.find_char('/');
	if (slash == -1) {
		return p_key;
	}
	return p_key.substr(0, slash);
}

Dictionary EditorAutomationCommands::_entry_from_palette(const String &p_key, const String &p_display_name, const String &p_shortcut_text, const Ref<Shortcut> &p_shortcut) {
	Dictionary entry;
	entry["key"] = p_key;
	entry["label"] = p_display_name;
	entry["category"] = _category_from_key(p_key);
	entry["source"] = "command_palette";
	entry["enabled"] = true;

	String shortcut_text = p_shortcut_text;
	if (shortcut_text == "None") {
		shortcut_text = String();
	}
	if (shortcut_text.is_empty() && p_shortcut.is_valid()) {
		shortcut_text = p_shortcut->get_as_text();
	}
	entry["shortcut_text"] = shortcut_text;

	const bool shortcut_enabled = p_shortcut.is_null() || p_shortcut->has_valid_event();
	entry["enabled"] = shortcut_enabled;

	entry["runnable"] = true;
	entry["runnable_by_run_command"] = true;
	if (!shortcut_enabled) {
		entry["non_runnable_reason"] = "Command shortcut has no assigned key binding.";
		entry["runnable"] = false;
		entry["runnable_by_run_command"] = false;
	}
	return entry;
}

Dictionary EditorAutomationCommands::_entry_from_shortcut(const String &p_key, const Ref<Shortcut> &p_shortcut) {
	Dictionary entry;
	entry["key"] = p_key;
	entry["label"] = p_shortcut.is_valid() ? p_shortcut->get_name() : p_key.get_file();
	entry["category"] = _category_from_key(p_key);
	entry["source"] = "shortcut";

	const bool has_events = p_shortcut.is_valid() && p_shortcut->has_valid_event();
	entry["enabled"] = has_events;
	entry["shortcut_text"] = p_shortcut.is_valid() ? p_shortcut->get_as_text() : String();

	const bool viewport_available = _editor_viewport_available();
	entry["runnable"] = has_events && viewport_available;
	entry["runnable_by_run_command"] = entry["runnable"];

	if (!has_events) {
		entry["non_runnable_reason"] = "Shortcut has no assigned key binding.";
	} else if (!viewport_available) {
		entry["non_runnable_reason"] = "Editor viewport is not available for shortcut dispatch.";
	}
	return entry;
}

bool EditorAutomationCommands::_matches_filter(const Dictionary &p_entry, const String &p_query, const String &p_category, bool p_runnable_only) {
	if (p_runnable_only && !(bool)p_entry.get("runnable_by_run_command", false)) {
		return false;
	}
	if (!p_category.is_empty() && String(p_entry.get("category", String())) != p_category) {
		return false;
	}
	if (p_query.is_empty()) {
		return true;
	}

	const String key = p_entry.get("key", String());
	const String label = p_entry.get("label", String());
	const String query = p_query.to_lower();
	return key.to_lower().contains(query) || label.to_lower().contains(query) || query.is_subsequence_ofn(key.to_lower()) || query.is_subsequence_ofn(label.to_lower());
}

float EditorAutomationCommands::_score_match(const String &p_query, const String &p_key, const String &p_label) {
	if (p_query.is_empty()) {
		return 0.0f;
	}

	const String query = p_query.to_lower();
	const String key = p_key.to_lower();
	const String label = p_label.to_lower();

	if (key == query || label == query) {
		return 1.0f;
	}
	if (key.begins_with(query) || label.begins_with(query)) {
		return 0.95f;
	}
	if (key.contains(query) || label.contains(query)) {
		return 0.85f;
	}
	if (query.is_subsequence_ofn(key) || query.is_subsequence_ofn(label)) {
		return 0.75f;
	}
	return 0.0f;
}

Dictionary EditorAutomationCommands::list_commands(const Dictionary &p_args) {
	Dictionary result;
	result["ok"] = true;

#ifdef TOOLS_ENABLED
	const String query = _read_string(p_args, "query");
	const String category = _read_string(p_args, "category");
	const bool runnable_only = _read_bool(p_args, "runnable_only", false);
	int limit = _read_int(p_args, "limit", 0);

	HashSet<String> palette_keys;
	Array commands;

	EditorCommandPalette *palette = EditorCommandPalette::get_singleton();
	if (palette != nullptr) {
		List<String> palette_list;
		palette->get_actions_list(&palette_list);
		for (const String &key : palette_list) {
			palette_keys.insert(key);
			String display_name;
			String shortcut_text;
			Ref<Shortcut> shortcut;
			if (!palette->get_command_details(key, &display_name, &shortcut_text, &shortcut)) {
				continue;
			}
			const Dictionary entry = _entry_from_palette(key, display_name, shortcut_text, shortcut);
			if (_matches_filter(entry, query, category, runnable_only)) {
				commands.push_back(entry);
			}
		}
	}

	EditorSettings *settings = EditorSettings::get_singleton();
	if (settings != nullptr) {
		List<String> shortcut_list;
		settings->get_shortcut_list(&shortcut_list);
		for (const String &key : shortcut_list) {
			if (palette_keys.has(key)) {
				continue;
			}
			const Ref<Shortcut> shortcut = settings->get_shortcut(key);
			const Dictionary entry = _entry_from_shortcut(key, shortcut);
			if (_matches_filter(entry, query, category, runnable_only)) {
				commands.push_back(entry);
			}
		}
	}

	if (limit > 0 && commands.size() > limit) {
		Array limited;
		for (int i = 0; i < limit; i++) {
			limited.push_back(commands[i]);
		}
		commands = limited;
		result["truncated"] = true;
	} else {
		result["truncated"] = false;
	}

	result["commands"] = commands;
	result["count"] = commands.size();
#else
	result["commands"] = Array();
	result["count"] = 0;
	result["truncated"] = false;
#endif

	return result;
}

Array EditorAutomationCommands::suggest_commands(const String &p_query, int p_limit) {
	Array suggestions;
	if (p_limit <= 0) {
		p_limit = 10;
	}

	const Dictionary listed = list_commands();
	const Array commands = listed.get("commands", Array());

	struct ScoredEntry {
		Dictionary entry;
		float score = 0.0f;
	};

	struct ScoredEntryComparator {
		_FORCE_INLINE_ bool operator()(const ScoredEntry &p_a, const ScoredEntry &p_b) const {
			if (p_a.score == p_b.score) {
				return String(p_a.entry.get("key", String())) < String(p_b.entry.get("key", String()));
			}
			return p_a.score > p_b.score;
		}
	};

	Vector<ScoredEntry> scored;
	scored.resize(commands.size());
	for (int i = 0; i < commands.size(); i++) {
		const Dictionary entry = commands[i];
		ScoredEntry scored_entry;
		scored_entry.entry = entry;
		scored_entry.score = _score_match(p_query, entry.get("key", String()), entry.get("label", String()));
		scored.write[i] = scored_entry;
	}

	SortArray<ScoredEntry, ScoredEntryComparator> sorter;
	sorter.sort(scored.ptrw(), scored.size());

	for (int i = 0; i < scored.size() && suggestions.size() < p_limit; i++) {
		if (scored[i].score <= 0.0f) {
			continue;
		}
		suggestions.push_back(scored[i].entry.get("key", String()));
	}
	return suggestions;
}

Dictionary EditorAutomationCommands::execute(const String &p_command) {
	Dictionary result;
	result["command"] = p_command;

	if (p_command.is_empty()) {
		result["ok"] = false;
		result["kind"] = "invalid_parameter";
		result["message"] = "run_command requires a non-empty 'command'.";
		return result;
	}

#ifdef TOOLS_ENABLED
	EditorCommandPalette *palette = EditorCommandPalette::get_singleton();
	if (palette != nullptr && palette->has_command(p_command)) {
		String display_name;
		String shortcut_text;
		Ref<Shortcut> shortcut;
		palette->get_command_details(p_command, &display_name, &shortcut_text, &shortcut);
		if (shortcut.is_valid() && !shortcut->has_valid_event()) {
			result["ok"] = false;
			result["kind"] = "disabled_command";
			result["message"] = vformat("Command '%s' is disabled because its shortcut has no assigned key binding.", p_command);
			result["candidates"] = suggest_commands(p_command);
			return result;
		}

		palette->execute_command(p_command);
		result["ok"] = true;
		result["route"] = "command_palette";
		return result;
	}

	EditorSettings *settings = EditorSettings::get_singleton();
	if (settings != nullptr && settings->has_shortcut(p_command)) {
		const Ref<Shortcut> shortcut = settings->get_shortcut(p_command);
		if (shortcut.is_null() || !shortcut->has_valid_event()) {
			result["ok"] = false;
			result["kind"] = "disabled_command";
			result["message"] = vformat("Shortcut '%s' has no assigned key binding.", p_command);
			result["candidates"] = suggest_commands(p_command);
			return result;
		}
		if (!_push_shortcut_to_editor(shortcut)) {
			result["ok"] = false;
			result["kind"] = "unavailable";
			result["message"] = vformat("Shortcut '%s' cannot run because the editor viewport is not available.", p_command);
			result["candidates"] = suggest_commands(p_command);
			return result;
		}

		result["ok"] = true;
		result["route"] = "shortcut";
		return result;
	}

	result["ok"] = false;
	result["kind"] = "unknown_command";
	result["message"] = vformat("Unknown command '%s'.", p_command);
	result["suggestions"] = suggest_commands(p_command);
	result["candidates"] = result["suggestions"];
	return result;
#else
	result["ok"] = false;
	result["kind"] = "unavailable";
	result["message"] = "Command execution is only available in editor builds.";
	return result;
#endif
}
