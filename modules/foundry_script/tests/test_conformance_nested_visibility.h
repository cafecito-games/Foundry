/**************************************************************************/
/*  test_conformance_nested_visibility.h                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                              GODOT ENGINE                              */
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

#include "modules/foundry_script/foundry_script.h"
#include "modules/foundry_script/fs_analyzer.h"
#include "modules/foundry_script/fs_cache.h"
#include "modules/foundry_script/fs_conformance_registry.h"
#include "modules/foundry_script/fs_parser.h"
#include "modules/foundry_script/tests/fs_temporary_project_tree.h"

#include "core/io/file_access.h"
#include "core/object/script_language.h"
#include "tests/test_macros.h"

namespace FSTests {

struct NestedConformanceVisibilityResult {
	bool clean = false;
	Vector<String> errors;
	// FSCache owns only a raw parser-map pointer. Keeping this Ref alive makes the second analysis in
	// an order case observe the exact parser, analyzer, and memoized nodes produced by the first.
	Ref<FSParserRef> parser_ref;
};

static NestedConformanceVisibilityResult analyze_nested_conformance_visibility_file(const String &p_path) {
	NestedConformanceVisibilityResult result;
	Error analysis_error = OK;
	result.parser_ref = FSCache::get_parser(p_path, FSParserRef::FULLY_SOLVED, analysis_error);
	if (result.parser_ref.is_null() || result.parser_ref->get_parser() == nullptr) {
		return result;
	}
	for (const FSParser::ParserError &error : result.parser_ref->get_parser()->get_errors()) {
		result.errors.push_back(error.message);
	}
	result.clean = analysis_error == OK && result.errors.is_empty();
	return result;
}

// Exercises an already shared parser through its owning analyzer without advancing the ParserRef's
// sticky cache status. This separates node memoization from a terminal failed raise: B-first leaves
// B at PARSED while its member and errors are owner-defined, then A delegates into that exact tree.
static NestedConformanceVisibilityResult analyze_nested_conformance_visibility_owner(
		const String &p_path, bool p_run_full_analysis) {
	NestedConformanceVisibilityResult result;
	Error analysis_error = OK;
	result.parser_ref = FSCache::get_parser(p_path, FSParserRef::PARSED, analysis_error);
	if (result.parser_ref.is_null() || result.parser_ref->get_parser() == nullptr) {
		return result;
	}
	if (analysis_error == OK) {
		if (p_run_full_analysis) {
			analysis_error = result.parser_ref->get_analyzer()->analyze();
		} else {
			analysis_error = result.parser_ref->get_analyzer()->resolve_inheritance();
			if (analysis_error == OK) {
				analysis_error = result.parser_ref->get_analyzer()->resolve_interface();
			}
		}
	}
	for (const FSParser::ParserError &error : result.parser_ref->get_parser()->get_errors()) {
		result.errors.push_back(error.message);
	}
	result.clean = analysis_error == OK && result.errors.is_empty();
	return result;
}

static String nested_conformance_visibility_errors(const NestedConformanceVisibilityResult &p_result) {
	String messages;
	for (const String &error : p_result.errors) {
		if (!messages.is_empty()) {
			messages += " | ";
		}
		messages += error;
	}
	return messages;
}

static bool nested_conformance_visibility_has_error(
		const NestedConformanceVisibilityResult &p_result, const String &p_expected) {
	for (const String &error : p_result.errors) {
		if (error.contains(p_expected)) {
			return true;
		}
	}
	return false;
}

static bool nested_conformance_visibility_has_exact_error(
		const NestedConformanceVisibilityResult &p_result, const String &p_expected) {
	for (const String &error : p_result.errors) {
		if (error == p_expected) {
			return true;
		}
	}
	return false;
}

static bool nested_conformance_visibility_parser_has_exact_error(
		const Ref<FSParserRef> &p_parser_ref, const String &p_expected) {
	if (p_parser_ref.is_null() || p_parser_ref->get_parser() == nullptr) {
		return false;
	}
	for (const FSParser::ParserError &error : p_parser_ref->get_parser()->get_errors()) {
		if (error.message == p_expected) {
			return true;
		}
	}
	return false;
}

struct NestedConformanceVisibilityFixture {
	struct GlobalClass {
		StringName name;
		String file;
		bool is_trait = false;
	};

	TemporaryProjectTree tree;
	Vector<String> files;
	Vector<GlobalClass> global_classes;

	NestedConformanceVisibilityFixture(const String &p_fixture_name, const Vector<String> &p_files,
			const Vector<GlobalClass> &p_global_classes) :
			tree("foundry_conformance_nested_visibility_" + p_fixture_name),
			files(p_files),
			global_classes(p_global_classes) {
		const String fixture_root = String("modules/foundry_script/tests/fixtures/conformance_nested_visibility")
											.path_join(p_fixture_name);
		for (const String &file : files) {
			Error read_error = OK;
			const String contents = FileAccess::get_file_as_string(fixture_root.path_join(file), &read_error);
			REQUIRE_MESSAGE(read_error == OK, vformat("Cannot read conformance fixture '%s'.", file));
			tree.write_file(file, contents);
		}
		reset();
	}

	~NestedConformanceVisibilityFixture() {
		clear();
	}

	String path(const String &p_file) const {
		return tree.root.path_join(p_file);
	}

	void clear() const {
		FSCache::clear();
		FSConformanceRegistry::get_singleton()->clear();
		for (const GlobalClass &global_class : global_classes) {
			if (ScriptServer::is_global_class(global_class.name)) {
				ScriptServer::remove_global_class(global_class.name);
			}
		}
	}

	void reset() const {
		clear();
		for (const GlobalClass &global_class : global_classes) {
			ScriptServer::add_global_class(global_class.name, "RefCounted", FSLanguage::get_singleton()->get_name(),
					path(global_class.file), false, false, global_class.is_trait, false);
		}
	}
};

TEST_CASE("[Modules][FoundryScript][Conformance] a dependency is resolved under its own conformance visibility") {
	NestedConformanceVisibilityFixture fixture("member",
			{ "project.foundry", "widget.fs", "gadgetlike.fs", "conformance.fs", "b.fs", "a.fs" },
			{ { "RtcvWidget", "widget.fs", false }, { "RtcvGadgetlike", "gadgetlike.fs", true },
					{ "RtcvB", "b.fs", false } });

	for (const bool a_first : { true, false }) {
		CAPTURE(a_first);
		fixture.reset();
		NestedConformanceVisibilityResult a_result;
		NestedConformanceVisibilityResult b_result;
		if (a_first) {
			a_result = analyze_nested_conformance_visibility_file(fixture.path("a.fs"));
			b_result = analyze_nested_conformance_visibility_owner(fixture.path("b.fs"), false);
		} else {
			b_result = analyze_nested_conformance_visibility_owner(fixture.path("b.fs"), true);
			a_result = analyze_nested_conformance_visibility_file(fixture.path("a.fs"));
		}

		REQUIRE(a_result.parser_ref.is_valid());
		REQUIRE(b_result.parser_ref.is_valid());
		const Ref<FSParserRef> *a_dependency =
				a_result.parser_ref->get_parser()->get_depended_parsers().getptr(fixture.path("b.fs"));
		REQUIRE(a_dependency != nullptr);
		CHECK_EQ(a_dependency->ptr(), b_result.parser_ref.ptr());

		CHECK_MESSAGE(nested_conformance_visibility_has_exact_error(a_result,
							  R"(Could not resolve external class member "thing".)"),
				nested_conformance_visibility_errors(a_result));
		CHECK_MESSAGE(nested_conformance_visibility_has_exact_error(b_result,
							  R"(Cannot assign a value of type RtcvWidget to variable "thing" with specified type RtcvGadgetlike.)"),
				nested_conformance_visibility_errors(b_result));
	}
}

TEST_CASE("[Modules][FoundryScript][Conformance] nested foreign interface resolution does not borrow the caller's conformances") {
	NestedConformanceVisibilityFixture fixture("interface",
			{ "project.foundry", "widget.fs", "gadgetlike.fs", "conformance.fs", "b.fs", "a.fs" },
			{ { "RtcviWidget", "widget.fs", false }, { "RtcviGadgetlike", "gadgetlike.fs", true },
					{ "RtcviB", "b.fs", false } });

	for (const bool a_first : { true, false }) {
		CAPTURE(a_first);
		fixture.reset();
		NestedConformanceVisibilityResult a_result;
		NestedConformanceVisibilityResult b_result;
		if (a_first) {
			a_result = analyze_nested_conformance_visibility_file(fixture.path("a.fs"));
			b_result = analyze_nested_conformance_visibility_owner(fixture.path("b.fs"), false);
		} else {
			b_result = analyze_nested_conformance_visibility_owner(fixture.path("b.fs"), true);
			a_result = analyze_nested_conformance_visibility_file(fixture.path("a.fs"));
		}

		REQUIRE(a_result.parser_ref.is_valid());
		REQUIRE(b_result.parser_ref.is_valid());
		const Ref<FSParserRef> *a_dependency =
				a_result.parser_ref->get_parser()->get_depended_parsers().getptr(fixture.path("b.fs"));
		REQUIRE(a_dependency != nullptr);
		CHECK_EQ(a_dependency->ptr(), b_result.parser_ref.ptr());

		const bool reports_relevant_owner_failure =
				nested_conformance_visibility_has_error(a_result, "RtcviB") ||
				nested_conformance_visibility_has_exact_error(
						a_result, R"(Could not resolve external class member "accept".)");
		CHECK_FALSE(a_result.clean);
		CHECK_MESSAGE(reports_relevant_owner_failure,
				nested_conformance_visibility_errors(a_result));
		CHECK_MESSAGE(nested_conformance_visibility_has_exact_error(b_result,
							  R"(Cannot assign a value of type RtcviWidget to parameter "thing" with specified type RtcviGadgetlike.)"),
				nested_conformance_visibility_errors(b_result));
	}
}

TEST_CASE("[Modules][FoundryScript][Conformance] foreign dependents replay an owner interface failure") {
	NestedConformanceVisibilityFixture fixture("interface",
			{ "project.foundry", "widget.fs", "gadgetlike.fs", "conformance.fs", "b.fs", "a.fs", "c.fs" },
			{ { "RtcviWidget", "widget.fs", false }, { "RtcviGadgetlike", "gadgetlike.fs", true },
					{ "RtcviB", "b.fs", false } });

	const NestedConformanceVisibilityResult a_result =
			analyze_nested_conformance_visibility_file(fixture.path("a.fs"));
	const NestedConformanceVisibilityResult c_result =
			analyze_nested_conformance_visibility_file(fixture.path("c.fs"));
	REQUIRE(a_result.parser_ref.is_valid());
	REQUIRE(c_result.parser_ref.is_valid());
	const Ref<FSParserRef> *a_dependency =
			a_result.parser_ref->get_parser()->get_depended_parsers().getptr(fixture.path("b.fs"));
	const Ref<FSParserRef> *c_dependency =
			c_result.parser_ref->get_parser()->get_depended_parsers().getptr(fixture.path("b.fs"));
	REQUIRE(a_dependency != nullptr);
	REQUIRE(c_dependency != nullptr);
	CHECK_EQ(a_dependency->ptr(), c_dependency->ptr());

	CHECK_FALSE(a_result.clean);
	CHECK_FALSE(c_result.clean);
	CHECK_MESSAGE(nested_conformance_visibility_has_error(a_result, R"(Could not resolve class "RtcviB".)"),
			nested_conformance_visibility_errors(a_result));
	CHECK_MESSAGE(nested_conformance_visibility_has_error(c_result, R"(Could not resolve class "RtcviB".)"),
			nested_conformance_visibility_errors(c_result));
	CHECK(nested_conformance_visibility_parser_has_exact_error(*a_dependency,
			R"(Cannot assign a value of type RtcviWidget to parameter "thing" with specified type RtcviGadgetlike.)"));
}

TEST_CASE("[Modules][FoundryScript][Conformance] a cached member failure propagates into its owner interface") {
	NestedConformanceVisibilityFixture fixture("member",
			{ "project.foundry", "widget.fs", "gadgetlike.fs", "conformance.fs", "b.fs", "a.fs", "c.fs" },
			{ { "RtcvWidget", "widget.fs", false }, { "RtcvGadgetlike", "gadgetlike.fs", true },
					{ "RtcvB", "b.fs", false } });

	const NestedConformanceVisibilityResult a_result =
			analyze_nested_conformance_visibility_file(fixture.path("a.fs"));
	const NestedConformanceVisibilityResult c_result =
			analyze_nested_conformance_visibility_file(fixture.path("c.fs"));
	REQUIRE(a_result.parser_ref.is_valid());
	REQUIRE(c_result.parser_ref.is_valid());
	const Ref<FSParserRef> *a_dependency =
			a_result.parser_ref->get_parser()->get_depended_parsers().getptr(fixture.path("b.fs"));
	const Ref<FSParserRef> *c_dependency =
			c_result.parser_ref->get_parser()->get_depended_parsers().getptr(fixture.path("b.fs"));
	REQUIRE(a_dependency != nullptr);
	REQUIRE(c_dependency != nullptr);
	CHECK_EQ(a_dependency->ptr(), c_dependency->ptr());

	CHECK_MESSAGE(nested_conformance_visibility_has_exact_error(
						  a_result, R"(Could not resolve external class member "thing".)"),
			nested_conformance_visibility_errors(a_result));
	CHECK_MESSAGE(nested_conformance_visibility_has_exact_error(c_result, R"(Could not resolve class "RtcvB".)"),
			nested_conformance_visibility_errors(c_result));
	CHECK(nested_conformance_visibility_parser_has_exact_error(*a_dependency,
			R"(Cannot assign a value of type RtcvWidget to variable "thing" with specified type RtcvGadgetlike.)"));
}

TEST_CASE("[Modules][FoundryScript][Conformance] a cached interface failure propagates into its owner body") {
	NestedConformanceVisibilityFixture fixture("interface",
			{ "project.foundry", "widget.fs", "gadgetlike.fs", "conformance.fs", "b.fs", "a.fs", "c.fs" },
			{ { "RtcviWidget", "widget.fs", false }, { "RtcviGadgetlike", "gadgetlike.fs", true },
					{ "RtcviB", "b.fs", false } });

	const NestedConformanceVisibilityResult a_result =
			analyze_nested_conformance_visibility_owner(fixture.path("a.fs"), false);
	const NestedConformanceVisibilityResult c_result =
			analyze_nested_conformance_visibility_owner(fixture.path("c.fs"), true);
	REQUIRE(a_result.parser_ref.is_valid());
	REQUIRE(c_result.parser_ref.is_valid());
	const Ref<FSParserRef> *a_dependency =
			a_result.parser_ref->get_parser()->get_depended_parsers().getptr(fixture.path("b.fs"));
	const Ref<FSParserRef> *c_dependency =
			c_result.parser_ref->get_parser()->get_depended_parsers().getptr(fixture.path("b.fs"));
	REQUIRE(a_dependency != nullptr);
	REQUIRE(c_dependency != nullptr);
	CHECK_EQ(a_dependency->ptr(), c_dependency->ptr());

	CHECK_MESSAGE(nested_conformance_visibility_has_exact_error(a_result, R"(Could not resolve class "RtcviB".)"),
			nested_conformance_visibility_errors(a_result));
	String expected_body_error =
			vformat(R"(Could not resolve class "RtcviB". The class is declared in "%s", which has errors, )",
					fixture.path("b.fs"));
	expected_body_error += R"(the first at line 4: Cannot assign a value of type RtcviWidget )";
	expected_body_error += R"(to parameter "thing" with specified type RtcviGadgetlike.)";
	CHECK_MESSAGE(nested_conformance_visibility_has_exact_error(c_result, expected_body_error),
			nested_conformance_visibility_errors(c_result));
	CHECK(nested_conformance_visibility_parser_has_exact_error(*a_dependency,
			R"(Cannot assign a value of type RtcviWidget to parameter "thing" with specified type RtcviGadgetlike.)"));
}

TEST_CASE("[Modules][FoundryScript][Conformance] foreign dependents replay an owner body failure") {
	NestedConformanceVisibilityFixture fixture("body",
			{ "project.foundry", "widget.fs", "gadgetlike.fs", "conformance.fs", "b.fs", "a.fs", "c.fs" },
			{ { "RtcvbWidget", "widget.fs", false }, { "RtcvbGadgetlike", "gadgetlike.fs", true },
					{ "RtcvbB", "b.fs", false } });

	const NestedConformanceVisibilityResult a_result =
			analyze_nested_conformance_visibility_file(fixture.path("a.fs"));
	const NestedConformanceVisibilityResult c_result =
			analyze_nested_conformance_visibility_file(fixture.path("c.fs"));
	REQUIRE(a_result.parser_ref.is_valid());
	REQUIRE(c_result.parser_ref.is_valid());
	const Ref<FSParserRef> *a_dependency =
			a_result.parser_ref->get_parser()->get_depended_parsers().getptr(fixture.path("b.fs"));
	const Ref<FSParserRef> *c_dependency =
			c_result.parser_ref->get_parser()->get_depended_parsers().getptr(fixture.path("b.fs"));
	REQUIRE(a_dependency != nullptr);
	REQUIRE(c_dependency != nullptr);
	CHECK_EQ(a_dependency->ptr(), c_dependency->ptr());

	CHECK_FALSE(a_result.clean);
	CHECK_FALSE(c_result.clean);
	CHECK_MESSAGE(nested_conformance_visibility_has_error(a_result, R"(Could not resolve class "RtcvbB".)"),
			nested_conformance_visibility_errors(a_result));
	CHECK_MESSAGE(nested_conformance_visibility_has_error(c_result, R"(Could not resolve class "RtcvbB".)"),
			nested_conformance_visibility_errors(c_result));
	CHECK(nested_conformance_visibility_parser_has_exact_error(*a_dependency,
			R"(Cannot return value of type "RtcvbWidget" because the function return type is "RtcvbGadgetlike".)"));
}

TEST_CASE("[Modules][FoundryScript][Conformance] a dependency that loads the conformance itself still resolves") {
	NestedConformanceVisibilityFixture fixture("valid",
			{ "project.foundry", "widget.fs", "gadgetlike.fs", "conformance.fs", "b.fs", "a.fs" },
			{ { "RtcvvWidget", "widget.fs", false }, { "RtcvvGadgetlike", "gadgetlike.fs", true },
					{ "RtcvvB", "b.fs", false } });

	for (const bool a_first : { true, false }) {
		CAPTURE(a_first);
		fixture.reset();
		NestedConformanceVisibilityResult a_result;
		NestedConformanceVisibilityResult b_result;
		if (a_first) {
			a_result = analyze_nested_conformance_visibility_file(fixture.path("a.fs"));
			b_result = analyze_nested_conformance_visibility_file(fixture.path("b.fs"));
		} else {
			b_result = analyze_nested_conformance_visibility_file(fixture.path("b.fs"));
			a_result = analyze_nested_conformance_visibility_file(fixture.path("a.fs"));
		}

		CHECK_MESSAGE(a_result.clean, nested_conformance_visibility_errors(a_result));
		CHECK_MESSAGE(b_result.clean, nested_conformance_visibility_errors(b_result));
	}
}

} // namespace FSTests
