/**************************************************************************/
/*  test_foundry_cli_help.h                                               */
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

#include "main/cli_help.h"
#include "main/cli_parser.h"

#include "tests/test_macros.h"

namespace TestFoundryCLIHelp {

TEST_CASE("[FoundryCLIHelp] Top help lists nouns and omits legacy options") {
	const String text = FoundryCLIHelp::get_top_help_text("foundry");
	int noun_count = 0;
	const FoundryCLIHelp::NounSpec *nouns = FoundryCLIHelp::get_nouns(noun_count);
	for (int i = 0; i < noun_count; i++) {
		CHECK_MESSAGE(text.contains(nouns[i].name), nouns[i].name);
	}
	CHECK(text.contains("--json"));
	CHECK(text.contains("Run 'foundry <command> --help'"));
	CHECK_FALSE(text.contains("--resolution"));
	CHECK_FALSE(text.contains("--fullscreen"));
	CHECK_FALSE(text.contains("--doctool"));
	CHECK_FALSE(text.contains("--export-release"));
}

TEST_CASE("[FoundryCLIHelp] Noun help lists its subcommands") {
	const String text = FoundryCLIHelp::get_noun_help_text("script");
	CHECK(text.contains("format"));
	CHECK(text.contains("lint"));
#ifdef TOOLS_ENABLED
	CHECK(text.contains("migrate"));
#endif
	CHECK(text.contains("Run 'foundry script <subcommand> --help'"));
}

TEST_CASE("[FoundryCLIHelp] Command help documents options and example") {
	const String text = FoundryCLIHelp::get_command_help_text("script", "format");
	CHECK(text.contains("--check"));
	CHECK(text.contains("--write"));
	CHECK(text.contains("--diff"));
	CHECK(text.contains("foundry script format --project . --check scripts"));
}

TEST_CASE("[FoundryCLIHelp] Scoped routing validates nouns and verbs") {
	bool valid = false;

	(void)FoundryCLIHelp::get_scoped_help_text("foundry", PackedStringArray(), valid);
	CHECK(valid);

	PackedStringArray noun_scope;
	noun_scope.push_back("script");
	(void)FoundryCLIHelp::get_scoped_help_text("foundry", noun_scope, valid);
	CHECK(valid);

	PackedStringArray verb_scope = noun_scope;
	verb_scope.push_back("format");
	(void)FoundryCLIHelp::get_scoped_help_text("foundry", verb_scope, valid);
	CHECK(valid);

	PackedStringArray bad_noun;
	bad_noun.push_back("not-a-noun");
	const String fallback_top = FoundryCLIHelp::get_scoped_help_text("foundry", bad_noun, valid);
	CHECK_FALSE(valid);
	CHECK(fallback_top.contains("Global options"));

	PackedStringArray bad_verb = noun_scope;
	bad_verb.push_back("fmt");
	const String fallback_noun = FoundryCLIHelp::get_scoped_help_text("foundry", bad_verb, valid);
	CHECK_FALSE(valid);
	CHECK(fallback_noun.contains("Subcommands"));
}

} // namespace TestFoundryCLIHelp
