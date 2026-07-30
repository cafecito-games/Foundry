/**************************************************************************/
/*  fs_test_runner.cpp                                                    */
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

#include "fs_test_runner.h"

#include "../foundry_script.h"
#include "../fs_analyzer.h"
#include "../fs_autoload_index.h"
#include "../fs_bytecode_export.h"
#include "../fs_bytecode_loader.h"
#include "../fs_cache.h"
#include "../fs_compiler.h"
#include "../fs_conformance_registry.h"
#include "../fs_parser.h"
#include "../fs_tokenizer_buffer.h"

#include "core/config/project_settings.h"
#include "core/core_globals.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/file_access_pack.h"
#include "core/object/script_diagnostic_capture.h"
#include "core/os/os.h"
#include "core/string/string_builder.h"
#include "core/templates/hash_set.h"
#include "scene/resources/packed_scene.h"

#include "tests/core/config/test_project_settings.h"
#include "tests/test_macros.h"

namespace FSTests {

void init_autoloads() {
	FSAutoloadIndex autoload_index;
	autoload_index.rebuild_from_project_settings();

	// First pass, add the constants so they exist before any script is loaded.
	for (const FSAutoloadIndexEntry &autoload : autoload_index.get_entries()) {
		if (autoload.is_singleton) {
			for (int i = 0; i < ScriptServer::get_language_count(); i++) {
				ScriptLanguage *language = ScriptServer::get_language(i);
				// A reserved named global (e.g. the `godot` reflection namespace) wins over
				// an autoload of the same name, mirroring main.cpp.
				if (language->get_reserved_global_names().has(String(autoload.name))) {
					continue;
				}
				language->add_global_constant(autoload.name, Variant());
			}
		}
	}

	// Second pass, load into global constants.
	for (const FSAutoloadIndexEntry &autoload : autoload_index.get_entries()) {
		if (!autoload.is_singleton) {
			// Skip non-singletons since we don't have a scene tree here anyway.
			continue;
		}

		Node *n = nullptr;
		if (ResourceLoader::get_resource_type(autoload.path) == "PackedScene") {
			// Cache the scene reference before loading it (for cyclic references)
			Ref<PackedScene> scn;
			scn.instantiate();
			scn->set_path(autoload.path);
			scn->reload_from_file();
			ERR_CONTINUE_MSG(scn.is_null(), vformat("Failed to instantiate an autoload, can't load from path: %s.", autoload.path));

			if (scn.is_valid()) {
				n = scn->instantiate();
			}
		} else {
			Ref<Resource> res = ResourceLoader::load(autoload.path);
			ERR_CONTINUE_MSG(res.is_null(), vformat("Failed to instantiate an autoload, can't load from path: %s.", autoload.path));

			Ref<Script> scr = res;
			if (scr.is_valid()) {
				StringName ibt = scr->get_instance_base_type();
				bool valid_type = ClassDB::is_parent_class(ibt, "Node");
				ERR_CONTINUE_MSG(!valid_type, vformat("Failed to instantiate an autoload, script '%s' does not inherit from 'Node'.", autoload.path));

				Object *obj = ClassDB::instantiate(ibt);
				ERR_CONTINUE_MSG(!obj, vformat("Failed to instantiate an autoload, cannot instantiate '%s'.", ibt));

				n = Object::cast_to<Node>(obj);
				n->set_script(scr);
			}
		}

		ERR_CONTINUE_MSG(!n, vformat("Failed to instantiate an autoload, path is not pointing to a scene or a script: %s.", autoload.path));
		n->set_name(autoload.name);

		for (int i = 0; i < ScriptServer::get_language_count(); i++) {
			ScriptLanguage *language = ScriptServer::get_language(i);
			if (language->get_reserved_global_names().has(String(autoload.name))) {
				continue;
			}
			language->add_global_constant(autoload.name, n);
		}
	}
}

// Tracks whether the FoundryScript language has been brought up by `init_language()`
// so the heavy setup can be hoisted to once-per-`TEST_SUITE` (see
// `test_suite_language_fixture.h`) instead of running once per `TEST_CASE`.
static bool language_initialized = false;

// Saved ProjectSettings state from before the first `init_language()` in a cycle.
static String saved_resource_path;
static bool saved_project_loaded = false;
static String saved_app_name;
static bool saved_project_settings = false;

// Resource path established by the first `init_language()` setup in the current cycle.
// Per-test ProjectSettings restore guards can revert `resource_path` while the hoisted
// language stays live; the early-return path below re-applies this root when needed.
static String language_project_path;

// Counts how many times the heavy language setup actually ran. Suite fixtures
// assert this stays at one per suite; it is purely test instrumentation.
static uint64_t init_language_count = 0;

void init_language(const String &p_base_path) {
	// Idempotent so repeated `initialize()` calls within a suite reuse the
	// already-initialized language instead of paying the setup cost each case.
	if (language_initialized) {
		ProjectSettings *settings = ProjectSettings::get_singleton();
		if (!language_project_path.is_empty() &&
				(!settings->is_project_loaded() || settings->get_resource_path() != language_project_path)) {
			const Error err = settings->setup(language_project_path, String(), true);
			if (err) {
				print_line("Could not reload project settings.");
			}
		}
		return;
	}

	saved_resource_path = ProjectSettings::get_singleton()->get_resource_path();
	saved_project_loaded = ProjectSettings::get_singleton()->is_project_loaded();
	saved_app_name = GLOBAL_GET("application/config/name");
	saved_project_settings = true;

	// Setup project settings since it's needed by the languages to get the global scripts.
	// This also sets up the base resource path.
	Error err = ProjectSettings::get_singleton()->setup(p_base_path, String(), true);
	if (err) {
		print_line("Could not load project settings.");
		// Keep going since some scripts still work without this.
	} else {
		language_project_path = ProjectSettings::get_singleton()->get_resource_path();
		// Switching the active project also remaps `user://` to that project's
		// app-userdata directory (keyed on `application/config/name`). The engine
		// creates that directory during `Main::setup()`; mirror it here so tests
		// that write to `user://` after a project switch do not fail because the
		// directory was never created. Without this, whether a `user://` write
		// succeeds depends on an unrelated suite having incidentally created the
		// directory earlier in the run.
		OS::get_singleton()->ensure_user_data_dir();
	}

	// Initialize the language for the test routine.
	FSLanguage::get_singleton()->init();
	init_autoloads();

	language_initialized = true;
	init_language_count++;
}

bool is_fs_language_active() {
	return FSLanguage::get_singleton()->get_reflection_singleton().is_valid();
}

void finish_language() {
	if (!is_fs_language_active()) {
		language_initialized = false;
		language_project_path = String();
		if (saved_project_settings) {
			TestProjectSettingsInternalsAccessor::resource_path() = saved_resource_path;
			TestProjectSettingsInternalsAccessor::project_loaded() = saved_project_loaded;
			ProjectSettings::get_singleton()->set_setting("application/config/name", saved_app_name);
			saved_project_settings = false;
		}
		return;
	}
	FSLanguage::get_singleton()->clear_global_annotations();
	FSLanguage::get_singleton()->finish();
	ScriptServer::global_classes_clear();
	language_initialized = false;
	language_project_path = String();
	if (saved_project_settings) {
		TestProjectSettingsInternalsAccessor::resource_path() = saved_resource_path;
		TestProjectSettingsInternalsAccessor::project_loaded() = saved_project_loaded;
		ProjectSettings::get_singleton()->set_setting("application/config/name", saved_app_name);
		saved_project_settings = false;
	}
}

void reset_language_state() {
	// Per-case isolation seam for suites that share a hoisted language: clear
	// everything a fresh `finish_language()` / `init_language()` pair used to
	// reset between cases, except the stable globals (native classes,
	// singletons, the reflection namespace) that never change case to case.
	if (!language_initialized) {
		return;
	}
	FSLanguage::get_singleton()->clear_global_annotations();
	FSCache::clear();
	ScriptServer::global_classes_clear();
}

bool is_language_initialized() {
	return language_initialized;
}

uint64_t get_init_language_count() {
	return init_language_count;
}

StringName FSTestRunner::test_function_name;

FSTestRunner::FSTestRunner(const String &p_source_dir, bool p_init_language, bool p_print_filenames, bool p_use_binary_tokens, bool p_use_compiled_bytecode) {
	test_function_name = StringName("test");
	do_init_languages = p_init_language;
	print_filenames = p_print_filenames;
	binary_tokens = p_use_binary_tokens;
	compiled_bytecode = p_use_compiled_bytecode;

	source_dir = p_source_dir;
	if (!source_dir.ends_with("/")) {
		source_dir += "/";
	}

	if (do_init_languages) {
		init_language(p_source_dir);
	}

#ifdef DEBUG_ENABLED
	// Set all warning levels to "Warn" in order to test them properly, even the ones that default to error.
	ProjectSettings::get_singleton()->set_setting("debug/foundry_script/warnings/enable", true);
	for (int i = 0; i < (int)FSWarning::WARNING_MAX; i++) {
		if (i == FSWarning::UNTYPED_DECLARATION || i == FSWarning::INFERRED_DECLARATION) {
			// TODO: Add ability for test scripts to specify which warnings to enable/disable for testing.
			continue;
		}
		const String setting_path = FSWarning::get_setting_path_from_code((FSWarning::Code)i);
		ProjectSettings::get_singleton()->set_setting(setting_path, (int)FSWarning::WARN);
	}

	// Force the call, since the language is initialized **before** applying project settings
	// and the `settings_changed` signal is emitted with `call_deferred()`.
	FSParser::update_project_settings();
#endif // DEBUG_ENABLED

	// Enable printing to show results.
	CoreGlobals::print_line_enabled = true;
	CoreGlobals::print_error_enabled = true;
}

FSTestRunner::~FSTestRunner() {
	test_function_name = StringName();
	if (do_init_languages) {
		finish_language();
	}
}

#ifndef DEBUG_ENABLED
static String strip_warnings(const String &p_expected) {
	// On release builds we don't have warnings. Here we remove them from the output before comparison
	// so it doesn't fail just because of difference in warnings.
	String expected_no_warnings;
	for (String line : p_expected.split("\n")) {
		if (line.begins_with("~~ ")) {
			continue;
		}
		expected_no_warnings += line + "\n";
	}
	return expected_no_warnings.strip_edges() + "\n";
}
#endif

int FSTestRunner::run_tests() {
	if (!make_tests()) {
		FAIL("An error occurred while making the tests.");
		return -1;
	}

	if (!generate_class_index()) {
		FAIL("An error occurred while generating class index.");
		return -1;
	}

	int failed = 0;
	for (int i = 0; i < tests.size(); i++) {
		FSTest test = tests[i];
		if (print_filenames) {
			print_line(test.get_source_relative_filepath());
		}
		FSTest::TestResult result = test.run_test();

		String expected = FileAccess::get_file_as_string(test.get_output_file());
#ifndef DEBUG_ENABLED
		expected = strip_warnings(expected);
#endif
		INFO(test.get_source_file());
		if (!result.passed) {
			INFO(expected);
			failed++;
		}

		CHECK_MESSAGE(result.passed, (result.passed ? String() : result.output));
	}

	return failed;
}

bool FSTestRunner::generate_outputs() {
	is_generating = true;

	if (!make_tests()) {
		print_line("Failed to generate a test output.");
		return false;
	}

	if (!generate_class_index()) {
		return false;
	}

	for (int i = 0; i < tests.size(); i++) {
		FSTest test = tests[i];
		if (print_filenames) {
			print_line(test.get_source_relative_filepath());
		} else {
			OS::get_singleton()->print(".");
		}

		bool result = test.generate_output();

		if (!result) {
			print_line("\nCould not generate output for " + test.get_source_file());
			return false;
		}
	}
	print_line("\nGenerated output files for " + itos(tests.size()) + " tests successfully.");

	return true;
}

// Reads the runner directives from a fixture's leading lines. Directives are full-line comments
// (`#debug-only`, `#once-per-process`), one per line at the top of the file; the scan is
// order-independent and stops at the first non-directive line, so a fixture can carry several.
static HashSet<String> read_fixture_directives(const String &p_path) {
	HashSet<String> directives;
	Error open_error = OK;
	Ref<FileAccess> fixture_file(FileAccess::open(p_path, FileAccess::READ, &open_error));
	if (open_error != OK) {
		ERR_PRINT(vformat(R"(Couldn't open test file "%s".)", p_path));
		return directives;
	}
	while (!fixture_file->eof_reached()) {
		const String line = fixture_file->get_line();
		if (line != "#debug-only" && line != "#once-per-process") {
			break;
		}
		directives.insert(line);
	}
	return directives;
}

bool FSTestRunner::make_tests_for_dir(const String &p_dir) {
	Error err = OK;
	Ref<DirAccess> dir(DirAccess::open(p_dir, &err));

	if (err != OK) {
		return false;
	}

	String current_dir = dir->get_current_dir();

	dir->list_dir_begin();
	String next = dir->get_next();

	while (!next.is_empty()) {
		if (dir->current_is_dir()) {
			if (next == "." || next == ".." || next == "completion" || next == "lsp" || next == "refactor" || next == "format") {
				next = dir->get_next();
				continue;
			}
			if (!make_tests_for_dir(current_dir.path_join(next))) {
				return false;
			}
		} else {
			// `*.notest.fs` files are skipped.
			if (next.ends_with(".notest.fs")) {
				next = dir->get_next();
				continue;
			} else if (binary_tokens && next.ends_with(".textonly.fs")) {
				next = dir->get_next();
				continue;
			} else if (next.has_extension("fs")) {
				const HashSet<String> directives = read_fixture_directives(current_dir.path_join(next));
				// `#once-per-process` marks fixtures whose expected output includes engine
				// diagnostics emitted through once-per-process macros (e.g. `ERR_PRINT_ONCE`
				// behind required virtual methods). Only a fixture's first run in a process
				// reproduces them, so the repeated compiled-bytecode pass skips them. The skip is
				// pass-order independent: whichever corpus pass runs the fixture first consumes
				// the once-only diagnostics, so exactly one non-skipping pass can ever match.
				if (compiled_bytecode && directives.has("#once-per-process")) {
					next = dir->get_next();
					continue;
				}
#ifndef DEBUG_ENABLED
				// On release builds, skip tests marked as debug only.
				if (directives.has("#debug-only")) {
					next = dir->get_next();
					continue;
				}
#endif

				String out_file = next.get_basename() + ".out";
				ERR_FAIL_COND_V_MSG(!is_generating && !dir->file_exists(out_file), false, "Could not find output file for " + next);

				if (next.ends_with(".bin.fs")) {
					// Test text mode first.
					FSTest text_test(current_dir.path_join(next), current_dir.path_join(out_file), source_dir);
					text_test.set_use_compiled_bytecode(compiled_bytecode);
					tests.push_back(text_test);
					// Test binary mode even without `--use-binary-tokens`.
					FSTest bin_test(current_dir.path_join(next), current_dir.path_join(out_file), source_dir);
					bin_test.set_tokenizer_mode(FSTest::TOKENIZER_BUFFER);
					bin_test.set_use_compiled_bytecode(compiled_bytecode);
					tests.push_back(bin_test);
				} else {
					FSTest test(current_dir.path_join(next), current_dir.path_join(out_file), source_dir);
					if (binary_tokens) {
						test.set_tokenizer_mode(FSTest::TOKENIZER_BUFFER);
					}
					test.set_use_compiled_bytecode(compiled_bytecode);
					tests.push_back(test);
				}
			}
		}

		next = dir->get_next();
	}

	dir->list_dir_end();

	return true;
}

bool FSTestRunner::make_tests() {
	Error err = OK;
	Ref<DirAccess> dir(DirAccess::open(source_dir, &err));

	ERR_FAIL_COND_V_MSG(err != OK, false, "Could not open specified test directory.");

	source_dir = dir->get_current_dir() + "/"; // Make it absolute path.
	return make_tests_for_dir(dir->get_current_dir());
}

static bool generate_class_index_recursive(const String &p_dir) {
	Error err = OK;
	Ref<DirAccess> dir(DirAccess::open(p_dir, &err));

	if (err != OK) {
		return false;
	}

	String current_dir = dir->get_current_dir();

	dir->list_dir_begin();
	String next = dir->get_next();

	StringName fs_name = FSLanguage::get_singleton()->get_name();
	while (!next.is_empty()) {
		if (dir->current_is_dir()) {
			if (next == "." || next == ".." || next == "completion" || next == "lsp" || next == "refactor" || next == "format") {
				next = dir->get_next();
				continue;
			}
			if (!generate_class_index_recursive(current_dir.path_join(next))) {
				return false;
			}
		} else {
			if (!next.ends_with(".fs")) {
				next = dir->get_next();
				continue;
			}
			String base_type;
			String source_file = current_dir.path_join(next);
			bool is_abstract = false;
			bool is_tool = false;
			bool is_trait = false;
			bool is_enum = false;
			String class_name = FSLanguage::get_singleton()->get_global_class_name(source_file, &base_type, nullptr, &is_abstract, &is_tool, &is_trait, &is_enum);
			if (!class_name.is_empty()) {
				ERR_FAIL_COND_V_MSG(ScriptServer::is_global_class(class_name), false,
						"Class name '" + class_name + "' from " + source_file + " is already used in " + ScriptServer::get_global_class_path(class_name));

				ScriptServer::add_global_class(class_name, base_type, fs_name, source_file, is_abstract, is_tool, is_trait, is_enum);
			}

			// Index custom annotation declarations even for annotation-only files that declare
			// no `class_name`/`trait_name`, so imports and duplicate-identity checks can see them.
			// Mirrors the editor file-system scan, which refreshes the index through the same call.
			FSLanguage::get_singleton()->update_global_class_annotations(source_file, source_file);
		}

		next = dir->get_next();
	}

	dir->list_dir_end();

	return true;
}

// Walk up from `p_dir` to the nearest ancestor containing `project.foundry`,
// falling back to `p_dir` itself if none is found.
static String find_test_project_root(const String &p_dir) {
	String current = p_dir;
	while (true) {
		if (FileAccess::exists(current.path_join("project.foundry"))) {
			return current;
		}
		const String parent = current.get_base_dir();
		if (parent.is_empty() || parent == current) {
			return p_dir;
		}
		current = parent;
	}
}

bool FSTestRunner::generate_class_index() {
	Error err = OK;
	Ref<DirAccess> dir(DirAccess::open(source_dir, &err));

	ERR_FAIL_COND_V_MSG(err != OK, false, "Could not open specified test directory.");

	source_dir = dir->get_current_dir() + "/"; // Make it absolute path.

	// Global classes (`class_name`) are project-global: a fixture in any
	// subdirectory may reference one declared elsewhere in the test project
	// (e.g. `Utils` from `utils.notest.fs` at the scripts root). Index from the
	// project root so generating a subdirectory's outputs still resolves classes
	// declared outside it.
	return generate_class_index_recursive(find_test_project_root(dir->get_current_dir()));
}

FSTest::FSTest(const String &p_source_path, const String &p_output_path, const String &p_base_dir) {
	source_file = p_source_path;
	output_file = p_output_path;
	base_dir = p_base_dir;
	_print_handler.printfunc = print_handler;
	_error_handler.errfunc = error_handler;
}

void FSTestRunner::generate_outputs_for_cmdline() {
	List<String> cmdline_args = OS::get_singleton()->get_cmdline_args();

	String path = "modules/foundry_script/tests/scripts";
	for (List<String>::Element *E = cmdline_args.front(); E; E = E->next()) {
		if (E->get() == "--foundry_script-generate-tests") {
			if (E->next()) {
				path = E->next()->get();
			}
			break;
		}
	}

	// This runs as a `--test` command, so the script language is not initialized
	// yet; have the runner set it up (and tear it down) itself.
	FSTestRunner runner(path, true, cmdline_args.find("--print-filenames") != nullptr);

	if (!runner.generate_outputs()) {
		OS::get_singleton()->set_exit_code(EXIT_FAILURE);
	}
}

void FSTest::enable_stdout() {
	// TODO: this could likely be handled by doctest or `tests/test_macros.h`.
	OS::get_singleton()->set_stdout_enabled(true);
	OS::get_singleton()->set_stderr_enabled(true);
}

void FSTest::disable_stdout() {
	// TODO: this could likely be handled by doctest or `tests/test_macros.h`.
	OS::get_singleton()->set_stdout_enabled(false);
	OS::get_singleton()->set_stderr_enabled(false);
}

void FSTest::print_handler(void *p_this, const String &p_message, bool p_error, bool p_rich) {
	TestResult *result = (TestResult *)p_this;
	result->output += p_message + "\n";
}

void FSTest::error_handler(void *p_this, const char *p_function, const char *p_file, int p_line, const char *p_error, const char *p_explanation, bool p_editor_notify, ErrorHandlerType p_type) {
	ErrorHandlerData *data = (ErrorHandlerData *)p_this;
	FSTest *self = data->self;
	TestResult *result = data->result;

	if (ScriptDiagnosticCapture::has_active_capture()) {
		return;
	}

	result->status = FS_TEST_RUNTIME_ERROR;

	String header = _error_handler_type_string(p_type);

	// Only include the file, line, and function for script errors,
	// otherwise the test outputs changes based on the platform/compiler.
	if (p_type == ERR_HANDLER_SCRIPT) {
		header += vformat(" at %s:%d on %s()",
				String::utf8(p_file).trim_prefix(self->base_dir).replace_char('\\', '/'),
				p_line,
				String::utf8(p_function));
	}

	StringBuilder error_string;
	error_string.append(vformat(">> %s: %s\n", header, String::utf8(p_error)));
	if (strlen(p_explanation) > 0) {
		error_string.append(vformat(">>   %s\n", String::utf8(p_explanation)));
	}

	result->output += error_string.as_string();
}

bool FSTest::check_output(const String &p_output) const {
	Error err = OK;
	String expected = FileAccess::get_file_as_string(output_file, &err);

	ERR_FAIL_COND_V_MSG(err != OK, false, "Error when opening the output file.");

	String got = p_output.strip_edges(); // TODO: may be hacky.
	got += "\n"; // Make sure to insert newline for CI static checks.

#ifndef DEBUG_ENABLED
	expected = strip_warnings(expected);
#endif

	return got == expected;
}

String FSTest::get_text_for_status(FSTest::TestStatus p_status) const {
	switch (p_status) {
		case FS_TEST_OK:
			return "FS_TEST_OK";
		case FS_TEST_LOAD_ERROR:
			return "FS_TEST_LOAD_ERROR";
		case FS_TEST_PARSER_ERROR:
			return "FS_TEST_PARSER_ERROR";
		case FS_TEST_ANALYZER_ERROR:
			return "FS_TEST_ANALYZER_ERROR";
		case FS_TEST_COMPILER_ERROR:
			return "FS_TEST_COMPILER_ERROR";
		case FS_TEST_RUNTIME_ERROR:
			return "FS_TEST_RUNTIME_ERROR";
	}
	return "";
}

#ifdef TOOLS_ENABLED
// Resolves the external references a fixture's serialized bytecode names symbolically. Only the
// top-level fixture script goes through the byte round-trip; its dependencies (preloads, external
// bases, cross-file class references) resolve through the normal FSCache text-compilation path.
// That boundary is what the mode proves: the serialized top-level script runs identically against
// dependencies loaded the ordinary way.
class FSTestBytecodeResolver : public FSBytecodeExternalResolver {
public:
	virtual Ref<Resource> resolve_resource(const String &p_path) override {
		return ResourceLoader::load(p_path);
	}

	virtual Ref<Script> resolve_script(const String &p_path, const String &p_fully_qualified_name, bool &r_is_local_class) override {
		// External references never name a class local to the buffer being loaded; those travel as
		// intra-file class indices and resolve without consulting the resolver.
		r_is_local_class = false;
		Error error = OK;
		Ref<FoundryScript> root_script = FSCache::get_full_script(p_path, error);
		if (error != OK || root_script.is_null()) {
			return Ref<Script>();
		}
		if (p_fully_qualified_name.is_empty() || root_script->get_fully_qualified_name() == p_fully_qualified_name) {
			return root_script;
		}
		return Ref<Script>(root_script->find_class(p_fully_qualified_name));
	}
};

// Rebuilds a runnable script from serialized fixture bytecode onto a fresh FoundryScript carrying
// the fixture path. The fresh script never enters ResourceCache (`set_path_cache` only), so the
// directly compiled original keeps its resource identity and dependency compilations that reach the
// fixture's own path keep resolving, while execution goes through the restored script.
static Error load_fixture_from_bytecode(const Vector<uint8_t> &p_buffer, const String &p_source_file, Ref<FoundryScript> &r_restored) {
	Ref<FoundryScript> restored;
	restored.instantiate();
	restored->set_path_cache(p_source_file);

	FSTestBytecodeResolver resolver;
	FSBytecodeLoader loader;
	loader.set_resolver(&resolver);
	Error error = loader.load_skeleton(p_buffer, restored);
	if (error != OK) {
		return error;
	}
	error = loader.load_full(p_buffer, restored);
	if (error != OK) {
		return error;
	}
	// Publishing scripts with retained static data is the loader caller's job, mirroring what the
	// compiler does on the text path.
	if (loader.get_has_static_data() && !loader.get_annotated_static_unload()) {
		FSCache::add_static_script(restored);
	}
	r_restored = restored;
	return OK;
}
#endif // TOOLS_ENABLED

FSTest::TestResult FSTest::execute_test_code(bool p_is_generating) {
	disable_stdout();

	// Each fixture is an isolated mini-project: drop conformances registered by prior fixtures so
	// cross-file coherence checks only see dependencies resolved within this test.
	FSConformanceRegistry::get_singleton()->clear();

	TestResult result;
	result.status = FS_TEST_OK;
	result.output = String();
	result.passed = false;

	Error err = OK;

	// Create script.
	Ref<FoundryScript> script;
	script.instantiate();
	script->set_path(source_file);
	if (tokenizer_mode == TOKENIZER_TEXT) {
		err = script->load_source_code(source_file);
	} else {
		String code = FileAccess::get_file_as_string(source_file, &err);
		if (!err) {
			Vector<uint8_t> buffer = FSTokenizerBuffer::parse_code_string(code, FSTokenizerBuffer::COMPRESS_ZSTD);
			script->set_binary_tokens_source(buffer);
		}
	}
	if (err != OK) {
		enable_stdout();
		result.status = FS_TEST_LOAD_ERROR;
		result.passed = false;
		ERR_FAIL_V_MSG(result, "\nCould not load source code for: '" + source_file + "'");
	}

	// Test parsing.
	FSParser parser;
	if (tokenizer_mode == TOKENIZER_TEXT) {
		err = parser.parse(script->get_source_code(), source_file, false);
	} else {
		err = parser.parse_binary(script->get_binary_tokens_source(), source_file);
	}
	if (err != OK) {
		enable_stdout();
		result.status = FS_TEST_PARSER_ERROR;
		result.output = get_text_for_status(result.status) + "\n";

		const List<FSParser::ParserError> &errors = parser.get_errors();
		if (!errors.is_empty()) {
			// Only the first error since the following might be cascading.
			result.output += errors.front()->get().message + "\n"; // TODO: line, column?
		}
		if (!p_is_generating) {
			result.passed = check_output(result.output);
		}
		return result;
	}

	// Test type-checking.
	FSAnalyzer analyzer(&parser);
	err = analyzer.analyze();
	if (err != OK) {
		enable_stdout();
		result.status = FS_TEST_ANALYZER_ERROR;
		result.output = get_text_for_status(result.status) + "\n";

		StringBuilder error_string;
		for (const FSParser::ParserError &error : parser.get_errors()) {
			error_string.append(vformat(">> ERROR at line %d: %s\n", error.line, error.message));
		}
		result.output += error_string.as_string();
		if (!p_is_generating) {
			result.passed = check_output(result.output);
		}
		return result;
	}

#ifdef DEBUG_ENABLED
	StringBuilder warning_string;
	for (const FSWarning &warning : parser.get_warnings()) {
		warning_string.append(vformat("~~ WARNING at line %d: (%s) %s\n", warning.start_line, warning.get_name(), warning.get_message()));
	}
	result.output += warning_string.as_string();
#endif

	// Test compiling.
	FSCompiler compiler;
	err = compiler.compile(&parser, script.ptr(), false);
	if (err != OK) {
		enable_stdout();
		result.status = FS_TEST_COMPILER_ERROR;
		result.output = get_text_for_status(result.status) + "\n";
		result.output += compiler.get_error() + "\n";
		if (!p_is_generating) {
			result.passed = check_output(result.output);
		}
		return result;
	}

#ifdef TOOLS_ENABLED
	// Fixtures that failed to parse, analyze, or compile returned above and never reach
	// serialization, so error fixtures take the unchanged text path in this mode too.
	Vector<uint8_t> bytecode_buffer;
	if (use_compiled_bytecode) {
		FSBytecodeExporter exporter;
		err = exporter.serialize(script, bytecode_buffer, parser.get_tree()->annotated_static_unload);
		if (err != OK) {
			enable_stdout();
			result.status = FS_TEST_LOAD_ERROR;
			result.passed = false;
			ERR_FAIL_V_MSG(result, "\nCould not serialize compiled bytecode for: '" + source_file + "'");
		}
	}
#endif // TOOLS_ENABLED

	// `*.norun.fs` files are allowed to not contain a `test()` function (no runtime testing).
	if (source_file.ends_with(".norun.fs")) {
#ifdef TOOLS_ENABLED
		if (use_compiled_bytecode) {
			// Nothing runs here, but the fixture must still survive deserialization and linking.
			// Output handlers are not installed, matching the text path, which never reload()s
			// these fixtures.
			Ref<FoundryScript> restored;
			err = load_fixture_from_bytecode(bytecode_buffer, source_file, restored);
			if (err != OK) {
				enable_stdout();
				result.status = FS_TEST_LOAD_ERROR;
				result.passed = false;
				ERR_FAIL_V_MSG(result, "\nCould not load compiled bytecode for: '" + source_file + "'");
			}
		}
#endif // TOOLS_ENABLED
		enable_stdout();
		result.status = FS_TEST_OK;
		result.output = get_text_for_status(result.status) + "\n" + result.output;
		if (!p_is_generating) {
			result.passed = check_output(result.output);
		}
		return result;
	}

	// Test running.
	const HashMap<StringName, FSFunction *>::ConstIterator test_function_element = script->get_member_functions().find(FSTestRunner::test_function_name);
	if (!test_function_element) {
		enable_stdout();
		result.status = FS_TEST_LOAD_ERROR;
		result.output = "";
		result.passed = false;
		ERR_FAIL_V_MSG(result, "\nCould not find test function on: '" + source_file + "'");
	}

	// Setup output handlers.
	ErrorHandlerData error_data(&result, this);

	_print_handler.userdata = &result;
	_error_handler.userdata = &error_data;
	add_print_handler(&_print_handler);
	add_error_handler(&_error_handler);

#ifdef TOOLS_ENABLED
	// Keeps the directly compiled script alive while the restored script runs: destroying it would
	// clear path-keyed global registrations (e.g. conformance witnesses) that the restored script
	// has just re-registered under the same fixture path.
	Ref<FoundryScript> directly_compiled_script;
	if (use_compiled_bytecode) {
		// The restored script replaces the directly compiled one for execution. Loading happens with
		// the print and error handlers already installed because linking finalizes with the reload()
		// tail (static defaults, then the static initializer), so static-initializer output is
		// captured exactly where the text path captures it during reload().
		Ref<FoundryScript> restored;
		err = load_fixture_from_bytecode(bytecode_buffer, source_file, restored);
		if (err != OK) {
			enable_stdout();
			const String captured_output = result.output;
			result.status = FS_TEST_LOAD_ERROR;
			result.output = "";
			result.passed = false;
			remove_print_handler(&_print_handler);
			remove_error_handler(&_error_handler);
			ERR_FAIL_V_MSG(result, "\nCould not load compiled bytecode for: '" + source_file + "'\n" + captured_output);
		}
		directly_compiled_script = script;
		script = restored;
	} else
#endif // TOOLS_ENABLED
	{
		err = script->reload();
		if (err) {
			enable_stdout();
			result.status = FS_TEST_LOAD_ERROR;
			result.output = "";
			result.passed = false;
			remove_print_handler(&_print_handler);
			remove_error_handler(&_error_handler);
			ERR_FAIL_V_MSG(result, "\nCould not reload script: '" + source_file + "'");
		}
	}

	// Create object instance for test.
	Object *obj = ClassDB::instantiate(script->get_native()->get_name());
	Ref<RefCounted> obj_ref;
	if (obj->is_ref_counted()) {
		obj_ref = Ref<RefCounted>(Object::cast_to<RefCounted>(obj));
	}
	obj->set_script(script);
	FSInstance *instance = static_cast<FSInstance *>(obj->get_script_instance());

	// A script whose head class is abstract can't be instantiated: `set_script` refuses
	// it and leaves the object without a script instance. Report a clean, deterministic
	// runtime error instead of dereferencing a null instance below.
	if (instance == nullptr) {
		enable_stdout();
		remove_print_handler(&_print_handler);
		remove_error_handler(&_error_handler);

		result.status = FS_TEST_RUNTIME_ERROR;
		result.output = get_text_for_status(result.status) + "\n";
		if (script->is_abstract()) {
			result.output += ">> Test couldn't run: the head class is abstract and can't be instantiated.\n";
		} else {
			result.output += ">> Test couldn't run: the script instance couldn't be created.\n";
		}

		if (obj_ref.is_null()) {
			memdelete(obj);
		}

		if (!p_is_generating) {
			result.passed = check_output(result.output);
		}

		FSCache::remove_script(script->get_path());
		return result;
	}

	// Call test function.
	Callable::CallError call_err;
	instance->callp(FSTestRunner::test_function_name, nullptr, 0, call_err);

	// Tear down output handlers.
	remove_print_handler(&_print_handler);
	remove_error_handler(&_error_handler);

	// Check results.
	if (call_err.error != Callable::CallError::CALL_OK) {
		enable_stdout();
		result.status = FS_TEST_LOAD_ERROR;
		result.passed = false;
		ERR_FAIL_V_MSG(result, "\nCould not call test function on: '" + source_file + "'");
	}

	result.output = get_text_for_status(result.status) + "\n" + result.output;
	if (!p_is_generating) {
		result.passed = check_output(result.output);
	}

	if (obj_ref.is_null()) {
		memdelete(obj);
	}

	enable_stdout();

	FSCache::remove_script(script->get_path());

	return result;
}

FSTest::TestResult FSTest::run_test() {
	return execute_test_code(false);
}

bool FSTest::generate_output() {
	TestResult result = execute_test_code(true);
	if (result.status == FS_TEST_LOAD_ERROR) {
		return false;
	}

	Error err = OK;
	Ref<FileAccess> out_file = FileAccess::open(output_file, FileAccess::WRITE, &err);
	if (err != OK) {
		return false;
	}

	String output = result.output.strip_edges(); // TODO: may be hacky.
	output += "\n"; // Make sure to insert newline for CI static checks.

	out_file->store_string(output);

	return true;
}

} // namespace FSTests
