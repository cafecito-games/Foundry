/**************************************************************************/
/*  cli_help.h                                                            */
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

#include "core/string/ustring.h"
#include "core/variant/variant.h"

class FoundryCLIHelp {
public:
	enum Availability {
		AVAILABILITY_RELEASE,
		AVAILABILITY_EDITOR,
	};

	struct CommandOption {
		const char *flag = nullptr;
		// Null for boolean flags. "a|b" lists accepted values; the first
		// alternative doubles as a parseable sample value in drift tests.
		const char *value_name = nullptr;
		const char *description = nullptr;
		bool required = false;
		// True when the value is attached with '=' as a single token (e.g. --format=sarif).
		bool equals_form = false;
	};

	struct Positional {
		const char *name = nullptr;
		bool optional = true;
		bool repeats = false;
	};

	struct CommandSpec {
		const char *noun = nullptr;
		const char *verb = nullptr;
		const char *summary = nullptr;
		const char *usage_args = nullptr;
		Availability availability = AVAILABILITY_RELEASE;
		const CommandOption *options = nullptr;
		int option_count = 0;
		const Positional *positionals = nullptr;
		int positional_count = 0;
		const char *example = nullptr;
	};

	struct NounSpec {
		const char *name = nullptr;
		const char *summary = nullptr;
	};

	static const NounSpec *get_nouns(int &r_count);
	static const CommandSpec *get_commands(int &r_count);
	static bool has_noun(const String &p_noun);
	static bool has_command(const String &p_noun, const String &p_verb);
	static bool is_command_in_build(const CommandSpec &p_spec);
	static bool is_noun_in_build(const String &p_noun);

	static String get_top_help_text(const String &p_binary);
	static String get_noun_help_text(const String &p_noun);
	static String get_command_help_text(const String &p_noun, const String &p_verb);
	// Routes `p_scope` ([] | [noun] | [noun, verb]) to the right level.
	// `r_valid` is false when the scope names an unknown noun or verb; the
	// nearest valid level's text is still returned as a fallback.
	static String get_scoped_help_text(const String &p_binary, const PackedStringArray &p_scope, bool &r_valid);
	static String get_help_json(const PackedStringArray &p_scope);
};
