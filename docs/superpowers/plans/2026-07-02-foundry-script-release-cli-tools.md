# Foundry Script Release CLI Tools Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship `--foundry_script-format` and `--foundry_script-lint` as supported headless CLI features in release editor builds without requiring `tests=yes`.

**Architecture:** Keep formatter behavior in `FSFormatterCLI`, but invoke it from normal `main/main.cpp` command-line tool flow instead of the unit-test command path. Add a focused `modules/foundry_script/fs_lint.{h,cpp}` module for lint option parsing, file collection, parser/analyzer diagnostic collection, JSON/SARIF serialization, output writing, and exit-code decisions. `main/main.cpp` only detects, preserves, and dispatches the two commands.

**Tech Stack:** Godot/Foundry C++ core APIs, Foundry Script `FSParser` and `FSAnalyzer`, `JSON::stringify`, `FileAccess`, `DirAccess`, doctest through `tests/test_macros.h`, SCons LinuxBSD editor builds.

---

## Scope Check

This plan covers two CLI commands, but they share one release command-line integration path and one CI-oriented Foundry Script tooling surface. They belong in one plan because the main startup changes must handle both together and the release-build verification is shared.

## File Structure

- Modify `main/main.cpp`: include lint/format headers, document help entries, preserve command arguments through setup, dispatch commands from `Main::start()`, and stop treating format as a test command.
- Modify `modules/foundry_script/register_types.cpp`: stop registering `--foundry_script-format` as a unit-test command; keep `--foundry_script-generate-format-tests`.
- Create `modules/foundry_script/fs_lint.h`: public lint data model and CLI helper declarations.
- Create `modules/foundry_script/fs_lint.cpp`: lint implementation, serialization, file traversal, command-line runner.
- Create `modules/foundry_script/tests/test_lint.h`: focused C++ tests for lint parsing, collection, diagnostics, serialization, and exit behavior.
- Modify `modules/foundry_script/tests/test_format.h`: add a small regression that format remains parsed by `FSFormatterCLI` and no longer relies on test registration for supported behavior.

## Task 1: Add Lint Data Model And Option Parsing

**Files:**
- Create: `modules/foundry_script/fs_lint.h`
- Create: `modules/foundry_script/fs_lint.cpp`
- Create: `modules/foundry_script/tests/test_lint.h`

- [ ] **Step 1: Write the failing option parsing tests**

Add `modules/foundry_script/tests/test_lint.h` with this initial content:

```cpp
/**************************************************************************/
/*  test_lint.h                                                           */
/**************************************************************************/

#pragma once

#include "../fs_lint.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "tests/test_macros.h"

namespace FSTests {

TEST_CASE("[Modules][FoundryScript][Lint] CLI option parsing uses CI defaults") {
	List<String> args;
	args.push_back("--headless");
	args.push_back("--foundry_script-lint");

	String error;
	FSLintCLI::Options options = FSLintCLI::parse_options(args, error);

	CHECK(error.is_empty());
	CHECK_EQ(options.output_format, FSLintCLI::OUTPUT_JSON);
	CHECK_EQ(options.fail_on, FSLintCLI::FAIL_ON_ERROR);
	CHECK(options.output_path.is_empty());
	CHECK(options.paths.is_empty());
}

TEST_CASE("[Modules][FoundryScript][Lint] CLI option parsing accepts SARIF output file and warning threshold") {
	List<String> args;
	args.push_back("--foundry_script-lint");
	args.push_back("--format=sarif");
	args.push_back("--out");
	args.push_back("lint.sarif");
	args.push_back("--fail-on=warning");
	args.push_back("res://scripts");

	String error;
	FSLintCLI::Options options = FSLintCLI::parse_options(args, error);

	CHECK(error.is_empty());
	CHECK_EQ(options.output_format, FSLintCLI::OUTPUT_SARIF);
	CHECK_EQ(options.fail_on, FSLintCLI::FAIL_ON_WARNING);
	CHECK_EQ(options.output_path, "lint.sarif");
	REQUIRE_EQ(options.paths.size(), 1);
	CHECK_EQ(options.paths[0], "res://scripts");
}

TEST_CASE("[Modules][FoundryScript][Lint] CLI option parsing rejects invalid choices") {
	List<String> bad_format;
	bad_format.push_back("--foundry_script-lint");
	bad_format.push_back("--format=xml");
	String format_error;
	FSLintCLI::parse_options(bad_format, format_error);
	CHECK(format_error.contains("Invalid --format value"));

	List<String> bad_fail_on;
	bad_fail_on.push_back("--foundry_script-lint");
	bad_fail_on.push_back("--fail-on=note");
	String fail_on_error;
	FSLintCLI::parse_options(bad_fail_on, fail_on_error);
	CHECK(fail_on_error.contains("Invalid --fail-on value"));

	List<String> missing_out;
	missing_out.push_back("--foundry_script-lint");
	missing_out.push_back("--out");
	String out_error;
	FSLintCLI::parse_options(missing_out, out_error);
	CHECK(out_error.contains("Missing file path after --out"));
}

} // namespace FSTests
```

- [ ] **Step 2: Run the focused tests to verify they fail**

Run:

```bash
python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=yes module_text_server_fb_enabled=yes cache_path="$HOME/.scons_cache" -j$(nproc)
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless --test --test-case="*[Lint]*" --force-colors
```

Expected: compile fails because `modules/foundry_script/fs_lint.h` does not exist, or the lint test fails because `FSLintCLI` is not implemented.

- [ ] **Step 3: Add the minimal lint declarations**

Create `modules/foundry_script/fs_lint.h`:

```cpp
/**************************************************************************/
/*  fs_lint.h                                                             */
/**************************************************************************/

#pragma once

#include "core/error/error_list.h"
#include "core/string/ustring.h"
#include "core/templates/list.h"
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"

class FSLintCLI {
public:
	enum OutputFormat {
		OUTPUT_JSON,
		OUTPUT_SARIF,
	};

	enum FailOn {
		FAIL_ON_ERROR,
		FAIL_ON_WARNING,
	};

	enum Severity {
		SEVERITY_NOTE,
		SEVERITY_WARNING,
		SEVERITY_ERROR,
	};

	struct Range {
		int start_line = 1;
		int start_column = 1;
		int end_line = 1;
		int end_column = 1;
	};

	struct Diagnostic {
		String path;
		String sarif_path;
		Range range;
		Severity severity = SEVERITY_ERROR;
		String source = "foundry_script";
		String rule_id;
		String message;
	};

	struct Options {
		OutputFormat output_format = OUTPUT_JSON;
		FailOn fail_on = FAIL_ON_ERROR;
		String output_path;
		Vector<String> paths;
	};

	struct Result {
		Vector<Diagnostic> diagnostics;
		bool had_command_error = false;
		String command_error;

		int get_exit_code(const Options &p_options) const;
	};

	static Options parse_options(const List<String> &p_cmdline_args, String &r_error);
	static String severity_to_string(Severity p_severity);
	static String sarif_level_for_severity(Severity p_severity);
	static void run_from_cmdline();
};
```

- [ ] **Step 4: Add minimal option parsing implementation**

Create `modules/foundry_script/fs_lint.cpp`:

```cpp
/**************************************************************************/
/*  fs_lint.cpp                                                           */
/**************************************************************************/

#include "fs_lint.h"

#include "core/os/os.h"

#include <stdio.h>
#include <stdlib.h>

FSLintCLI::Options FSLintCLI::parse_options(const List<String> &p_cmdline_args, String &r_error) {
	Options options;
	bool reached_command = false;
	for (const List<String>::Element *element = p_cmdline_args.front(); element; element = element->next()) {
		const String &argument = element->get();
		if (!reached_command) {
			if (argument == "--foundry_script-lint") {
				reached_command = true;
			}
			continue;
		}

		if (argument == "--format=json") {
			options.output_format = OUTPUT_JSON;
		} else if (argument == "--format=sarif") {
			options.output_format = OUTPUT_SARIF;
		} else if (argument.begins_with("--format=")) {
			r_error = "Invalid --format value. Expected json or sarif.";
			return options;
		} else if (argument == "--fail-on=error") {
			options.fail_on = FAIL_ON_ERROR;
		} else if (argument == "--fail-on=warning") {
			options.fail_on = FAIL_ON_WARNING;
		} else if (argument.begins_with("--fail-on=")) {
			r_error = "Invalid --fail-on value. Expected error or warning.";
			return options;
		} else if (argument == "--out") {
			const List<String>::Element *next = element->next();
			if (next == nullptr || next->get().begins_with("-")) {
				r_error = "Missing file path after --out.";
				return options;
			}
			options.output_path = next->get();
			element = next;
		} else {
			options.paths.push_back(argument);
		}
	}

	return options;
}

String FSLintCLI::severity_to_string(Severity p_severity) {
	switch (p_severity) {
		case SEVERITY_ERROR:
			return "error";
		case SEVERITY_WARNING:
			return "warning";
		case SEVERITY_NOTE:
			return "note";
	}
	return "error";
}

String FSLintCLI::sarif_level_for_severity(Severity p_severity) {
	return severity_to_string(p_severity);
}

int FSLintCLI::Result::get_exit_code(const Options &p_options) const {
	if (had_command_error) {
		return 2;
	}
	for (const Diagnostic &diagnostic : diagnostics) {
		if (diagnostic.severity == SEVERITY_ERROR) {
			return 1;
		}
		if (p_options.fail_on == FAIL_ON_WARNING && diagnostic.severity == SEVERITY_WARNING) {
			return 1;
		}
	}
	return 0;
}

void FSLintCLI::run_from_cmdline() {
	String error;
	const Options options = parse_options(OS::get_singleton()->get_cmdline_args(), error);
	OS::get_singleton()->set_exit_code(error.is_empty() ? EXIT_SUCCESS : 2);
}
```

- [ ] **Step 5: Run the focused tests to verify they pass**

Run:

```bash
python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=yes module_text_server_fb_enabled=yes cache_path="$HOME/.scons_cache" -j$(nproc)
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless --test --test-case="*[Lint]*" --force-colors
```

Expected: the three option parsing tests pass.

- [ ] **Step 6: Commit**

```bash
git add modules/foundry_script/fs_lint.h modules/foundry_script/fs_lint.cpp modules/foundry_script/tests/test_lint.h
git commit -m "Add Foundry Script lint CLI option parsing"
```

## Task 2: Add Deterministic Lint File Collection

**Files:**
- Modify: `modules/foundry_script/fs_lint.h`
- Modify: `modules/foundry_script/fs_lint.cpp`
- Modify: `modules/foundry_script/tests/test_lint.h`

- [ ] **Step 1: Write failing collection tests**

Append to `modules/foundry_script/tests/test_lint.h`:

```cpp
TEST_CASE("[Modules][FoundryScript][Lint] File collection recurses deterministically and skips hidden directories") {
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	REQUIRE(da.is_valid());
	const String root = da->get_current_dir().path_join("fs_lint_collect");

	DirAccess::remove_absolute(root.path_join("visible/b.fs"));
	DirAccess::remove_absolute(root.path_join("visible/a.fs"));
	DirAccess::remove_absolute(root.path_join(".godot/cache.fs"));
	DirAccess::remove_absolute(root.path_join("visible/not_script.txt"));
	DirAccess::remove_absolute(root.path_join("visible"));
	DirAccess::remove_absolute(root.path_join(".godot"));
	DirAccess::remove_absolute(root);

	REQUIRE_EQ(DirAccess::make_dir_recursive_absolute(root.path_join("visible")), OK);
	REQUIRE_EQ(DirAccess::make_dir_recursive_absolute(root.path_join(".godot")), OK);
	{
		Ref<FileAccess> file = FileAccess::open(root.path_join("visible/b.fs"), FileAccess::WRITE);
		REQUIRE(file.is_valid());
		file->store_string("func b() -> void:\n\tpass\n");
	}
	{
		Ref<FileAccess> file = FileAccess::open(root.path_join("visible/a.fs"), FileAccess::WRITE);
		REQUIRE(file.is_valid());
		file->store_string("func a() -> void:\n\tpass\n");
	}
	{
		Ref<FileAccess> file = FileAccess::open(root.path_join(".godot/cache.fs"), FileAccess::WRITE);
		REQUIRE(file.is_valid());
		file->store_string("func cached() -> void:\n\tpass\n");
	}
	{
		Ref<FileAccess> file = FileAccess::open(root.path_join("visible/not_script.txt"), FileAccess::WRITE);
		REQUIRE(file.is_valid());
		file->store_string("not a script\n");
	}

	Vector<String> paths;
	paths.push_back(root);
	bool had_error = false;
	const Vector<String> files = FSLintCLI::collect_files(paths, had_error);

	CHECK_FALSE(had_error);
	REQUIRE_EQ(files.size(), 2);
	CHECK(files[0].ends_with("visible/a.fs"));
	CHECK(files[1].ends_with("visible/b.fs"));

	DirAccess::remove_absolute(root.path_join("visible/b.fs"));
	DirAccess::remove_absolute(root.path_join("visible/a.fs"));
	DirAccess::remove_absolute(root.path_join(".godot/cache.fs"));
	DirAccess::remove_absolute(root.path_join("visible/not_script.txt"));
	DirAccess::remove_absolute(root.path_join("visible"));
	DirAccess::remove_absolute(root.path_join(".godot"));
	DirAccess::remove_absolute(root);
}

TEST_CASE("[Modules][FoundryScript][Lint] File collection reports missing paths") {
	Vector<String> paths;
	paths.push_back("missing_lint_path.fs");
	bool had_error = false;
	const Vector<String> files = FSLintCLI::collect_files(paths, had_error);
	CHECK(files.is_empty());
	CHECK(had_error);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=yes module_text_server_fb_enabled=yes cache_path="$HOME/.scons_cache" -j$(nproc)
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless --test --test-case="*[Lint]*" --force-colors
```

Expected: compile fails because `FSLintCLI::collect_files` is not declared.

- [ ] **Step 3: Add the collection API**

Add to the public section of `FSLintCLI` in `modules/foundry_script/fs_lint.h`:

```cpp
	static Vector<String> collect_files(const Vector<String> &p_paths, bool &r_had_error);
```

Add to the private section by first introducing `private:` before `run_from_cmdline()`:

```cpp
private:
	static void collect_files_recursive(const String &p_dir, Vector<String> &r_files, bool &r_had_error);

public:
	static void run_from_cmdline();
```

- [ ] **Step 4: Implement deterministic traversal**

Add includes to `modules/foundry_script/fs_lint.cpp`:

```cpp
#include "core/core_globals.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
```

Add these methods:

```cpp
void FSLintCLI::collect_files_recursive(const String &p_dir, Vector<String> &r_files, bool &r_had_error) {
	Error open_error = OK;
	Ref<DirAccess> dir = DirAccess::open(p_dir, &open_error);
	if (dir.is_null() || open_error != OK) {
		if (CoreGlobals::print_error_enabled) {
			fprintf(stderr, "%s: could not open directory\n", p_dir.utf8().get_data());
		}
		r_had_error = true;
		return;
	}

	dir->set_include_hidden(false);
	if (dir->list_dir_begin() != OK) {
		if (CoreGlobals::print_error_enabled) {
			fprintf(stderr, "%s: could not list directory\n", p_dir.utf8().get_data());
		}
		r_had_error = true;
		return;
	}

	for (String entry = dir->get_next(); !entry.is_empty(); entry = dir->get_next()) {
		if (entry == "." || entry == ".." || dir->current_is_hidden()) {
			continue;
		}
		const String full_path = p_dir.path_join(entry);
		if (dir->current_is_dir()) {
			if (dir->is_link(entry)) {
				continue;
			}
			collect_files_recursive(full_path, r_files, r_had_error);
		} else if (entry.get_extension() == "fs") {
			r_files.push_back(full_path);
		}
	}
	dir->list_dir_end();
}

Vector<String> FSLintCLI::collect_files(const Vector<String> &p_paths, bool &r_had_error) {
	Vector<String> files;
	for (const String &path : p_paths) {
		if (DirAccess::exists(path)) {
			collect_files_recursive(path, files, r_had_error);
		} else if (FileAccess::exists(path)) {
			if (path.get_extension() == "fs") {
				files.push_back(path);
			}
		} else {
			if (CoreGlobals::print_error_enabled) {
				fprintf(stderr, "%s: no such file or directory\n", path.utf8().get_data());
			}
			r_had_error = true;
		}
	}
	files.sort();
	return files;
}
```

- [ ] **Step 5: Run focused tests**

Run:

```bash
python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=yes module_text_server_fb_enabled=yes cache_path="$HOME/.scons_cache" -j$(nproc)
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless --test --test-case="*[Lint]*" --force-colors
```

Expected: lint option and collection tests pass.

- [ ] **Step 6: Commit**

```bash
git add modules/foundry_script/fs_lint.h modules/foundry_script/fs_lint.cpp modules/foundry_script/tests/test_lint.h
git commit -m "Add Foundry Script lint file collection"
```

## Task 3: Collect Parser, Analyzer, And Warning Diagnostics

**Files:**
- Modify: `modules/foundry_script/fs_lint.h`
- Modify: `modules/foundry_script/fs_lint.cpp`
- Modify: `modules/foundry_script/tests/test_lint.h`

- [ ] **Step 1: Write failing diagnostic tests**

Append to `modules/foundry_script/tests/test_lint.h`:

```cpp
TEST_CASE("[Modules][FoundryScript][Lint] Parser errors become lint diagnostics") {
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	REQUIRE(da.is_valid());
	const String path = da->get_current_dir().path_join("lint_parse_error.fs");
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	REQUIRE(file.is_valid());
	file->store_string("func broken(:\n");
	file.unref();

	Vector<String> paths;
	paths.push_back(path);
	FSLintCLI::Options options;
	FSLintCLI::Result result = FSLintCLI::lint_paths(paths, options);

	REQUIRE_EQ(result.diagnostics.size(), 1);
	const FSLintCLI::Diagnostic &diagnostic = result.diagnostics[0];
	CHECK_EQ(diagnostic.rule_id, "parse-error");
	CHECK_EQ(diagnostic.severity, FSLintCLI::SEVERITY_ERROR);
	CHECK_EQ(diagnostic.source, "foundry_script");
	CHECK(diagnostic.message.contains("Expected"));
	CHECK_GE(diagnostic.range.start_line, 1);
	CHECK_GE(diagnostic.range.start_column, 1);

	DirAccess::remove_absolute(path);
}

TEST_CASE("[Modules][FoundryScript][Lint] Analyzer errors become lint diagnostics") {
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	REQUIRE(da.is_valid());
	const String path = da->get_current_dir().path_join("lint_analyzer_error.fs");
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	REQUIRE(file.is_valid());
	file->store_string("func run() -> void:\n\tvar value: int = \"bad\"\n");
	file.unref();

	Vector<String> paths;
	paths.push_back(path);
	FSLintCLI::Options options;
	FSLintCLI::Result result = FSLintCLI::lint_paths(paths, options);

	REQUIRE_FALSE(result.diagnostics.is_empty());
	const FSLintCLI::Diagnostic &diagnostic = result.diagnostics[0];
	CHECK_EQ(diagnostic.rule_id, "analyzer-error");
	CHECK_EQ(diagnostic.severity, FSLintCLI::SEVERITY_ERROR);
	CHECK(diagnostic.message.contains("int") || diagnostic.message.contains("String"));

	DirAccess::remove_absolute(path);
}

#ifdef DEBUG_ENABLED
TEST_CASE("[Modules][FoundryScript][Lint] Warnings become lint diagnostics when warning support is compiled in") {
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	REQUIRE(da.is_valid());
	const String path = da->get_current_dir().path_join("lint_warning.fs");
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	REQUIRE(file.is_valid());
	file->store_string("func run() -> void:\n\tvar unused := 1\n");
	file.unref();

	Vector<String> paths;
	paths.push_back(path);
	FSLintCLI::Options options;
	FSLintCLI::Result result = FSLintCLI::lint_paths(paths, options);

	bool found_warning = false;
	for (const FSLintCLI::Diagnostic &diagnostic : result.diagnostics) {
		if (diagnostic.rule_id == "UNUSED_VARIABLE" || diagnostic.rule_id.to_lower() == "unused_variable") {
			found_warning = diagnostic.severity == FSLintCLI::SEVERITY_WARNING;
		}
	}
	CHECK(found_warning);

	DirAccess::remove_absolute(path);
}
#endif // DEBUG_ENABLED
```

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=yes module_text_server_fb_enabled=yes cache_path="$HOME/.scons_cache" -j$(nproc)
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless --test --test-case="*[Lint]*" --force-colors
```

Expected: compile fails because `FSLintCLI::lint_paths` is not declared.

- [ ] **Step 3: Add the linting API**

Add to `modules/foundry_script/fs_lint.h`:

```cpp
	static Result lint_paths(const Vector<String> &p_paths, const Options &p_options);
```

- [ ] **Step 4: Implement parser/analyzer collection**

Add includes to `modules/foundry_script/fs_lint.cpp`:

```cpp
#include "fs_analyzer.h"
#include "fs_parser.h"

#ifdef DEBUG_ENABLED
#include "fs_warning.h"
#endif
```

Add helper functions above `FSLintCLI::parse_options`:

```cpp
static FSLintCLI::Range make_line_range(const PackedStringArray &p_lines, int p_line, int p_column) {
	FSLintCLI::Range range;
	const int line_count = p_lines.size();
	const int clamped_line = CLAMP(p_line, 1, MAX(1, line_count));
	range.start_line = clamped_line;
	range.start_column = MAX(1, p_column);
	range.end_line = clamped_line;
	if (line_count > 0) {
		const String line_text = p_lines[clamped_line - 1];
		range.end_column = MAX(range.start_column, line_text.strip_edges(false).length() + 1);
	} else {
		range.end_column = range.start_column;
	}
	return range;
}

static FSLintCLI::Diagnostic make_error_diagnostic(const String &p_path, const String &p_rule_id, const FSParser::ParserError &p_error, const PackedStringArray &p_lines) {
	FSLintCLI::Diagnostic diagnostic;
	diagnostic.path = p_path;
	diagnostic.sarif_path = p_path;
	diagnostic.range = make_line_range(p_lines, p_error.line, p_error.column);
	diagnostic.severity = FSLintCLI::SEVERITY_ERROR;
	diagnostic.rule_id = p_rule_id;
	diagnostic.message = p_error.message;
	return diagnostic;
}
```

Add implementation:

```cpp
FSLintCLI::Result FSLintCLI::lint_paths(const Vector<String> &p_paths, const Options &p_options) {
	(void)p_options;
	Result result;
	bool had_collection_error = false;
	const Vector<String> files = collect_files(p_paths, had_collection_error);
	if (had_collection_error) {
		result.had_command_error = true;
		result.command_error = "One or more lint paths could not be read.";
	}

	for (const String &file : files) {
		Error read_error = OK;
		const String source = FileAccess::get_file_as_string(file, &read_error);
		if (read_error != OK) {
			result.had_command_error = true;
			result.command_error = "One or more lint files could not be read.";
			fprintf(stderr, "%s: could not read file\n", file.utf8().get_data());
			continue;
		}

		const PackedStringArray lines = source.split("\n");
		FSParser parser;
		const Error parse_error = parser.parse(source, file, false);
		if (parse_error != OK) {
			for (const FSParser::ParserError &error : parser.get_errors()) {
				result.diagnostics.push_back(make_error_diagnostic(file, "parse-error", error, lines));
			}
			continue;
		}

		FSAnalyzer analyzer(&parser);
		analyzer.analyze();
		for (const FSParser::ParserError &error : parser.get_errors()) {
			result.diagnostics.push_back(make_error_diagnostic(file, "analyzer-error", error, lines));
		}

#ifdef DEBUG_ENABLED
		for (const FSWarning &warning : parser.get_warnings()) {
			Diagnostic diagnostic;
			diagnostic.path = file;
			diagnostic.sarif_path = file;
			diagnostic.range = make_line_range(lines, warning.start_line, 1);
			diagnostic.severity = SEVERITY_WARNING;
			diagnostic.rule_id = warning.get_name();
			diagnostic.message = warning.get_message();
			result.diagnostics.push_back(diagnostic);
		}
#endif
	}

	return result;
}
```

- [ ] **Step 5: Run focused tests**

Run:

```bash
python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=yes module_text_server_fb_enabled=yes cache_path="$HOME/.scons_cache" -j$(nproc)
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless --test --test-case="*[Lint]*" --force-colors
```

Expected: parser, analyzer, and warning tests pass. If the analyzer test does not produce a diagnostic, replace its source with `func run() -> int:\n\treturn \"bad\"\n`, rerun the same focused test command, and keep the rule ID assertion on `analyzer-error`.

- [ ] **Step 6: Commit**

```bash
git add modules/foundry_script/fs_lint.h modules/foundry_script/fs_lint.cpp modules/foundry_script/tests/test_lint.h
git commit -m "Collect Foundry Script lint diagnostics"
```

## Task 4: Add JSON And SARIF Serialization

**Files:**
- Modify: `modules/foundry_script/fs_lint.h`
- Modify: `modules/foundry_script/fs_lint.cpp`
- Modify: `modules/foundry_script/tests/test_lint.h`

- [ ] **Step 1: Write failing serialization tests**

Append to `modules/foundry_script/tests/test_lint.h`:

```cpp
TEST_CASE("[Modules][FoundryScript][Lint] JSON serialization is stable and one-based") {
	FSLintCLI::Diagnostic diagnostic;
	diagnostic.path = "res://scripts/player.fs";
	diagnostic.sarif_path = "scripts/player.fs";
	diagnostic.range.start_line = 12;
	diagnostic.range.start_column = 5;
	diagnostic.range.end_line = 12;
	diagnostic.range.end_column = 20;
	diagnostic.severity = FSLintCLI::SEVERITY_WARNING;
	diagnostic.rule_id = "unused_variable";
	diagnostic.message = "The local variable \"value\" is declared but never used.";

	Vector<FSLintCLI::Diagnostic> diagnostics;
	diagnostics.push_back(diagnostic);
	const String text = FSLintCLI::to_json(diagnostics);
	const Variant parsed = JSON::parse_string(text);

	REQUIRE(parsed.get_type() == Variant::DICTIONARY);
	const Dictionary root = parsed;
	CHECK_EQ(int(root["version"]), 1);
	const Array output_diagnostics = root["diagnostics"];
	REQUIRE_EQ(output_diagnostics.size(), 1);
	const Dictionary first = output_diagnostics[0];
	CHECK_EQ(String(first["path"]), "res://scripts/player.fs");
	CHECK_EQ(String(first["severity"]), "warning");
	CHECK_EQ(String(first["source"]), "foundry_script");
	CHECK_EQ(String(first["ruleId"]), "unused_variable");
	const Dictionary range = first["range"];
	CHECK_EQ(int(range["startLine"]), 12);
	CHECK_EQ(int(range["startColumn"]), 5);
	CHECK_EQ(int(range["endLine"]), 12);
	CHECK_EQ(int(range["endColumn"]), 20);
}

TEST_CASE("[Modules][FoundryScript][Lint] SARIF serialization contains rules results and locations") {
	FSLintCLI::Diagnostic diagnostic;
	diagnostic.path = "res://scripts/player.fs";
	diagnostic.sarif_path = "scripts/player.fs";
	diagnostic.range.start_line = 3;
	diagnostic.range.start_column = 2;
	diagnostic.range.end_line = 3;
	diagnostic.range.end_column = 9;
	diagnostic.severity = FSLintCLI::SEVERITY_ERROR;
	diagnostic.rule_id = "parse-error";
	diagnostic.message = "Expected identifier.";

	Vector<FSLintCLI::Diagnostic> diagnostics;
	diagnostics.push_back(diagnostic);
	const String text = FSLintCLI::to_sarif(diagnostics);
	const Variant parsed = JSON::parse_string(text);

	REQUIRE(parsed.get_type() == Variant::DICTIONARY);
	const Dictionary root = parsed;
	CHECK_EQ(String(root["version"]), "2.1.0");
	CHECK_EQ(String(root["$schema"]), "https://json.schemastore.org/sarif-2.1.0.json");
	const Array runs = root["runs"];
	REQUIRE_EQ(runs.size(), 1);
	const Dictionary run = runs[0];
	const Dictionary tool = run["tool"];
	const Dictionary driver = tool["driver"];
	CHECK_EQ(String(driver["name"]), "Foundry Script Lint");
	const Array rules = driver["rules"];
	REQUIRE_EQ(rules.size(), 1);
	CHECK_EQ(String(Dictionary(rules[0])["id"]), "parse-error");
	const Array results = run["results"];
	REQUIRE_EQ(results.size(), 1);
	const Dictionary result = results[0];
	CHECK_EQ(String(result["ruleId"]), "parse-error");
	CHECK_EQ(String(result["level"]), "error");
	const Array locations = result["locations"];
	const Dictionary physical_location = Dictionary(Dictionary(locations[0])["physicalLocation"]);
	const Dictionary artifact = physical_location["artifactLocation"];
	CHECK_EQ(String(artifact["uri"]), "scripts/player.fs");
	const Dictionary region = physical_location["region"];
	CHECK_EQ(int(region["startLine"]), 3);
	CHECK_EQ(int(region["startColumn"]), 2);
}
```

Add `#include "core/io/json.h"` to `test_lint.h`.

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=yes module_text_server_fb_enabled=yes cache_path="$HOME/.scons_cache" -j$(nproc)
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless --test --test-case="*[Lint]*" --force-colors
```

Expected: compile fails because `to_json` and `to_sarif` are not declared.

- [ ] **Step 3: Add serialization declarations**

Add to `modules/foundry_script/fs_lint.h`:

```cpp
	static Dictionary diagnostic_to_dictionary(const Diagnostic &p_diagnostic);
	static String to_json(const Vector<Diagnostic> &p_diagnostics);
	static String to_sarif(const Vector<Diagnostic> &p_diagnostics);
```

- [ ] **Step 4: Implement JSON and SARIF serialization**

Add includes to `modules/foundry_script/fs_lint.cpp`:

```cpp
#include "core/io/json.h"
#include "core/variant/array.h"
```

Add methods:

```cpp
Dictionary FSLintCLI::diagnostic_to_dictionary(const Diagnostic &p_diagnostic) {
	Dictionary range;
	range["startLine"] = p_diagnostic.range.start_line;
	range["startColumn"] = p_diagnostic.range.start_column;
	range["endLine"] = p_diagnostic.range.end_line;
	range["endColumn"] = p_diagnostic.range.end_column;

	Dictionary diagnostic;
	diagnostic["path"] = p_diagnostic.path;
	diagnostic["range"] = range;
	diagnostic["severity"] = severity_to_string(p_diagnostic.severity);
	diagnostic["source"] = p_diagnostic.source;
	diagnostic["ruleId"] = p_diagnostic.rule_id;
	diagnostic["message"] = p_diagnostic.message;
	return diagnostic;
}

String FSLintCLI::to_json(const Vector<Diagnostic> &p_diagnostics) {
	Array diagnostics;
	for (const Diagnostic &diagnostic : p_diagnostics) {
		diagnostics.push_back(diagnostic_to_dictionary(diagnostic));
	}

	Dictionary root;
	root["version"] = 1;
	root["diagnostics"] = diagnostics;
	return JSON::stringify(root, "\t", true, true) + "\n";
}

String FSLintCLI::to_sarif(const Vector<Diagnostic> &p_diagnostics) {
	Dictionary rules_by_id;
	Array results;

	for (const Diagnostic &diagnostic : p_diagnostics) {
		if (!rules_by_id.has(diagnostic.rule_id)) {
			Dictionary rule;
			rule["id"] = diagnostic.rule_id;
			rule["name"] = diagnostic.rule_id;
			Dictionary short_description;
			short_description["text"] = diagnostic.rule_id;
			rule["shortDescription"] = short_description;
			rules_by_id[diagnostic.rule_id] = rule;
		}

		Dictionary message;
		message["text"] = diagnostic.message;

		Dictionary artifact_location;
		artifact_location["uri"] = diagnostic.sarif_path.is_empty() ? diagnostic.path : diagnostic.sarif_path;

		Dictionary region;
		region["startLine"] = diagnostic.range.start_line;
		region["startColumn"] = diagnostic.range.start_column;
		region["endLine"] = diagnostic.range.end_line;
		region["endColumn"] = diagnostic.range.end_column;

		Dictionary physical_location;
		physical_location["artifactLocation"] = artifact_location;
		physical_location["region"] = region;

		Dictionary location;
		location["physicalLocation"] = physical_location;

		Array locations;
		locations.push_back(location);

		Dictionary result;
		result["ruleId"] = diagnostic.rule_id;
		result["level"] = sarif_level_for_severity(diagnostic.severity);
		result["message"] = message;
		result["locations"] = locations;
		results.push_back(result);
	}

	Array rules;
	Array rule_ids = rules_by_id.keys();
	rule_ids.sort();
	for (const Variant &rule_id : rule_ids) {
		rules.push_back(rules_by_id[rule_id]);
	}

	Dictionary driver;
	driver["name"] = "Foundry Script Lint";
	driver["informationUri"] = "https://github.com/cafecito-games/godot";
	driver["rules"] = rules;

	Dictionary tool;
	tool["driver"] = driver;

	Dictionary run;
	run["tool"] = tool;
	run["results"] = results;

	Array runs;
	runs.push_back(run);

	Dictionary root;
	root["version"] = "2.1.0";
	root["$schema"] = "https://json.schemastore.org/sarif-2.1.0.json";
	root["runs"] = runs;
	return JSON::stringify(root, "\t", true, true) + "\n";
}
```

- [ ] **Step 5: Run focused tests**

Run:

```bash
python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=yes module_text_server_fb_enabled=yes cache_path="$HOME/.scons_cache" -j$(nproc)
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless --test --test-case="*[Lint]*" --force-colors
```

Expected: lint serialization tests pass.

- [ ] **Step 6: Commit**

```bash
git add modules/foundry_script/fs_lint.h modules/foundry_script/fs_lint.cpp modules/foundry_script/tests/test_lint.h
git commit -m "Serialize Foundry Script lint reports"
```

## Task 5: Complete Lint CLI Runner And Exit Behavior

**Files:**
- Modify: `modules/foundry_script/fs_lint.h`
- Modify: `modules/foundry_script/fs_lint.cpp`
- Modify: `modules/foundry_script/tests/test_lint.h`

- [ ] **Step 1: Write failing exit-code and output tests**

Append to `modules/foundry_script/tests/test_lint.h`:

```cpp
TEST_CASE("[Modules][FoundryScript][Lint] Exit code respects failure threshold") {
	FSLintCLI::Diagnostic warning;
	warning.severity = FSLintCLI::SEVERITY_WARNING;
	warning.rule_id = "unused_variable";
	warning.message = "warning";

	FSLintCLI::Diagnostic error;
	error.severity = FSLintCLI::SEVERITY_ERROR;
	error.rule_id = "parse-error";
	error.message = "error";

	FSLintCLI::Options fail_on_error;
	FSLintCLI::Result warning_result;
	warning_result.diagnostics.push_back(warning);
	CHECK_EQ(warning_result.get_exit_code(fail_on_error), 0);

	FSLintCLI::Options fail_on_warning;
	fail_on_warning.fail_on = FSLintCLI::FAIL_ON_WARNING;
	CHECK_EQ(warning_result.get_exit_code(fail_on_warning), 1);

	FSLintCLI::Result error_result;
	error_result.diagnostics.push_back(error);
	CHECK_EQ(error_result.get_exit_code(fail_on_error), 1);

	FSLintCLI::Result command_error;
	command_error.had_command_error = true;
	command_error.command_error = "bad command";
	CHECK_EQ(command_error.get_exit_code(fail_on_error), 2);
}

TEST_CASE("[Modules][FoundryScript][Lint] Report writing stores selected output format") {
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	REQUIRE(da.is_valid());
	const String path = da->get_current_dir().path_join("lint_report.json");
	FSLintCLI::Diagnostic diagnostic;
	diagnostic.path = "res://bad.fs";
	diagnostic.sarif_path = "bad.fs";
	diagnostic.rule_id = "parse-error";
	diagnostic.message = "Expected identifier.";

	FSLintCLI::Options options;
	options.output_format = FSLintCLI::OUTPUT_JSON;
	options.output_path = path;

	FSLintCLI::Result result;
	result.diagnostics.push_back(diagnostic);
	const Error error = FSLintCLI::write_report(options, result);
	CHECK_EQ(error, OK);
	CHECK(FileAccess::exists(path));
	const String text = FileAccess::get_file_as_string(path);
	CHECK(text.contains("\"version\""));
	CHECK(text.contains("\"diagnostics\""));

	DirAccess::remove_absolute(path);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=yes module_text_server_fb_enabled=yes cache_path="$HOME/.scons_cache" -j$(nproc)
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless --test --test-case="*[Lint]*" --force-colors
```

Expected: compile fails because `FSLintCLI::write_report` is not declared.

- [ ] **Step 3: Add report writing API**

Add to `modules/foundry_script/fs_lint.h`:

```cpp
	static Error write_report(const Options &p_options, const Result &p_result);
```

- [ ] **Step 4: Implement report writing and complete `run_from_cmdline`**

Add to `modules/foundry_script/fs_lint.cpp`:

```cpp
Error FSLintCLI::write_report(const Options &p_options, const Result &p_result) {
	const String text = p_options.output_format == OUTPUT_SARIF ? to_sarif(p_result.diagnostics) : to_json(p_result.diagnostics);
	if (p_options.output_path.is_empty()) {
		const CharString utf8 = text.utf8();
		if (utf8.length() > 0) {
			fwrite(utf8.get_data(), 1, utf8.length(), stdout);
		}
		fflush(stdout);
		return OK;
	}

	Ref<FileAccess> file = FileAccess::open(p_options.output_path, FileAccess::WRITE);
	if (file.is_null()) {
		fprintf(stderr, "%s: could not open output file\n", p_options.output_path.utf8().get_data());
		return ERR_CANT_OPEN;
	}
	if (!file->store_string(text)) {
		fprintf(stderr, "%s: could not write output file\n", p_options.output_path.utf8().get_data());
		return ERR_CANT_CREATE;
	}
	return OK;
}

void FSLintCLI::run_from_cmdline() {
	String error;
	Options options = parse_options(OS::get_singleton()->get_cmdline_args(), error);
	if (!error.is_empty()) {
		fprintf(stderr, "foundry_script-lint: %s\n", error.utf8().get_data());
		OS::get_singleton()->set_exit_code(2);
		return;
	}

	if (options.paths.is_empty()) {
		options.paths.push_back(ProjectSettings::get_singleton()->get_resource_path());
	}

	Result result = lint_paths(options.paths, options);
	if (write_report(options, result) != OK) {
		result.had_command_error = true;
	}
	OS::get_singleton()->set_exit_code(result.get_exit_code(options));
}
```

Add `#include "core/config/project_settings.h"` to `fs_lint.cpp`.

- [ ] **Step 5: Run focused tests**

Run:

```bash
python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=yes module_text_server_fb_enabled=yes cache_path="$HOME/.scons_cache" -j$(nproc)
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless --test --test-case="*[Lint]*" --force-colors
```

Expected: all lint tests pass.

- [ ] **Step 6: Commit**

```bash
git add modules/foundry_script/fs_lint.h modules/foundry_script/fs_lint.cpp modules/foundry_script/tests/test_lint.h
git commit -m "Finish Foundry Script lint CLI runner"
```

## Task 6: Wire Formatter And Lint Into Release CLI Startup

**Files:**
- Modify: `main/main.cpp`
- Modify: `modules/foundry_script/register_types.cpp`
- Modify: `modules/foundry_script/tests/test_format.h`

- [ ] **Step 1: Capture the current release-build formatter failure**

Run:

```bash
python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=no module_text_server_fb_enabled=yes cache_path="$HOME/.scons_cache" -j$(nproc)
printf 'func f():\\n\\treturn  1\\n' | ./bin/foundry.linuxbsd.editor.dev.x86_64 --headless --foundry_script-format
```

Expected before the fix: the command exits nonzero and reports that a test command was specified in a binary compiled without unit-test support, or it reaches normal startup without running the formatter.

- [ ] **Step 2: Add includes and help entries**

In `main/main.cpp`, inside `#ifdef MODULE_FOUNDRY_SCRIPT_ENABLED`, add the lint/format include under `#ifdef TOOLS_ENABLED`:

```cpp
#ifdef TOOLS_ENABLED
#include "modules/foundry_script/fs_format.h"
#include "modules/foundry_script/fs_lint.h"
#include "modules/foundry_script/editor/fs_migration_wizard.h"
#endif // TOOLS_ENABLED
```

In `Main::print_help`, under the existing Foundry Script tool options, add:

```cpp
	print_help_option("--foundry_script-format [--write|-w|--check|--diff|-d] [paths...|-]", "Format FoundryScript files, directories, or stdin using the canonical formatter.\n", CLI_OPTION_AVAILABILITY_EDITOR);
	print_help_option("--foundry_script-lint [--format=json|sarif] [--out <path>] [--fail-on=error|warning] [paths...]", "Lint FoundryScript files with parser/analyzer diagnostics and emit JSON or SARIF for CI.\n", CLI_OPTION_AVAILABILITY_EDITOR);
```

- [ ] **Step 3: Stop classifying format as a test command**

In `Main::test_entrypoint`, replace:

```cpp
		const bool is_format_command = strcmp(argv[x], "--foundry_script-format") == 0 ||
				strcmp(argv[x], "--foundry_script-generate-format-tests") == 0;
		if (is_test || is_test_command || is_format_command) {
			test_requested = true;
		}
```

with:

```cpp
		const bool is_format_test_command = strcmp(argv[x], "--foundry_script-generate-format-tests") == 0;
		if (is_test || is_test_command || is_format_test_command) {
			test_requested = true;
		}
```

In `modules/foundry_script/register_types.cpp`, remove the `fs_format_command()` function and remove:

```cpp
REGISTER_TEST_COMMAND("--foundry_script-format", &fs_format_command);
```

Keep `fs_generate_format_tests()` and `REGISTER_TEST_COMMAND("--foundry_script-generate-format-tests", &fs_generate_format_tests);`.

- [ ] **Step 4: Preserve Foundry Script tool args through setup**

In `Main::setup`, near other command-line state locals, add:

```cpp
	bool foundry_script_cli_tool_args = false;
```

At the top of the main argument loop, after `adding_user_args` handling and before ordinary option handling, add this branch:

```cpp
		} else if (foundry_script_cli_tool_args) {
			main_args.push_back(arg);
```

Add command detection under `#if defined(TOOLS_ENABLED) && defined(MODULE_FOUNDRY_SCRIPT_ENABLED)` before `--path` handling:

```cpp
		} else if (arg == "--foundry_script-format" || arg == "--foundry_script-lint") {
			cmdline_tool = true;
			audio_driver = NULL_AUDIO_DRIVER;
			display_driver = NULL_DISPLAY_DRIVER;
			main_args.push_back(arg);
			foundry_script_cli_tool_args = true;
			quit_after = 1;
```

This capture mode intentionally forwards all later tokens before the user-argument separator into `main_args`, so `--format=json`, `--out`, `--fail-on`, and file paths survive `OS::set_cmdline(execpath, main_args, user_args)`.

- [ ] **Step 5: Parse and dispatch commands in `Main::start`**

In `Main::start`, add booleans under the existing `#ifdef MODULE_FOUNDRY_SCRIPT_ENABLED` block:

```cpp
	bool fs_format_requested = false;
	bool fs_lint_requested = false;
```

In the no-argument parsing section under `#ifdef TOOLS_ENABLED` and `#ifdef MODULE_FOUNDRY_SCRIPT_ENABLED`, add:

```cpp
		} else if (E->get() == "--foundry_script-format") {
			fs_format_requested = true;
		} else if (E->get() == "--foundry_script-lint") {
			fs_lint_requested = true;
```

Before main scene resolution and before migration dispatch, add:

```cpp
#if defined(TOOLS_ENABLED) && defined(MODULE_FOUNDRY_SCRIPT_ENABLED)
	if (fs_format_requested) {
		FSFormatterCLI::run_from_cmdline();
		return OS::get_singleton()->get_exit_code();
	}

	if (fs_lint_requested) {
		FSLintCLI::run_from_cmdline();
		return OS::get_singleton()->get_exit_code();
	}
#endif // TOOLS_ENABLED && MODULE_FOUNDRY_SCRIPT_ENABLED
```

- [ ] **Step 6: Add a formatter test note to keep the supported path visible**

Append to `modules/foundry_script/tests/test_format.h` near the existing CLI option tests:

```cpp
	TEST_CASE("[Format] CLI command name remains the supported release formatter entry point") {
		List<String> args;
		args.push_back("--foundry_script-format");
		args.push_back("--check");
		args.push_back("script.fs");

		FSFormatterCLI::Options options = FSFormatterCLI::parse_options(args);
		CHECK_EQ(options.mode, FSFormatterCLI::MODE_CHECK);
		REQUIRE_EQ(options.paths.size(), 1);
		CHECK_EQ(options.paths[0], "script.fs");
	}
```

- [ ] **Step 7: Build and verify release formatter and lint commands**

Run:

```bash
python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=no module_text_server_fb_enabled=yes cache_path="$HOME/.scons_cache" -j$(nproc)
printf 'func f():\\n\\treturn  1\\n' | ./bin/foundry.linuxbsd.editor.dev.x86_64 --headless --foundry_script-format
```

Expected formatter stdout contains:

```foundry_script
func f():
	return 1
```

Run a lint smoke test:

```bash
tmp_project="$(mktemp -d)"
printf 'config_version=5\n' > "$tmp_project/project.foundry"
mkdir -p "$tmp_project/scripts"
printf 'func broken(:\n' > "$tmp_project/scripts/bad.fs"
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless --path "$tmp_project" --foundry_script-lint --format=json --fail-on=error
```

Expected: exit code `1`, stdout is JSON with `"version": 1`, `"ruleId": "parse-error"`, and a `res://scripts/bad.fs` or project-localized path.

- [ ] **Step 8: Commit**

```bash
git add main/main.cpp modules/foundry_script/register_types.cpp modules/foundry_script/tests/test_format.h
git commit -m "Expose Foundry Script tools in release CLI"
```

## Task 7: Full Verification And Release Build Guard

**Files:**
- No source files unless verification exposes a defect in earlier tasks.

- [ ] **Step 1: Run focused Foundry Script lint/format tests**

Run:

```bash
python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=yes module_text_server_fb_enabled=yes cache_path="$HOME/.scons_cache" -j$(nproc)
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless --test --test-case="*[Lint]*" --force-colors
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless --test --test-case="*[Format]*" --force-colors
```

Expected: both filtered test runs report doctest success.

- [ ] **Step 2: Run the full test suite**

Run:

```bash
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless --test --force-colors
```

Expected: output includes `[doctest] Status: SUCCESS!`. If cleanup leak messages make the process exit nonzero after the success summary, record the exact cleanup messages and keep investigating only if doctest reports failures.

- [ ] **Step 3: Run release-style no-test command smoke tests**

Run:

```bash
python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=no module_text_server_fb_enabled=yes cache_path="$HOME/.scons_cache" -j$(nproc)
printf 'func f():\\n\\treturn  1\\n' | ./bin/foundry.linuxbsd.editor.dev.x86_64 --headless --foundry_script-format > /tmp/foundry_format.out
cat /tmp/foundry_format.out
```

Expected:

```foundry_script
func f():
	return 1
```

Run:

```bash
tmp_project="$(mktemp -d)"
printf 'config_version=5\n' > "$tmp_project/project.foundry"
mkdir -p "$tmp_project/scripts"
printf 'func broken(:\n' > "$tmp_project/scripts/bad.fs"
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless --path "$tmp_project" --foundry_script-lint --format=sarif --out "$tmp_project/lint.sarif" --fail-on=error
status=$?
test "$status" -eq 1
test -s "$tmp_project/lint.sarif"
rg '\"version\": \"2.1.0\"|\"ruleId\": \"parse-error\"' "$tmp_project/lint.sarif"
```

Expected: status is `1`, SARIF file exists, and `rg` finds both SARIF version and parse-error result.

- [ ] **Step 4: Run diff and status checks**

Run:

```bash
git diff --check
git status --short
```

Expected: `git diff --check` prints nothing. `git status --short` shows only intentional source changes if the previous commits were not made during task execution.

- [ ] **Step 5: Commit final verification adjustments**

If verification required code or test fixes, commit them:

```bash
git add main/main.cpp modules/foundry_script/fs_lint.h modules/foundry_script/fs_lint.cpp modules/foundry_script/register_types.cpp modules/foundry_script/tests/test_lint.h modules/foundry_script/tests/test_format.h
git commit -m "Verify Foundry Script release CLI tools"
```

If no files changed during verification, do not create an empty commit.
