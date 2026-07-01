/**************************************************************************/
/*  register_types.cpp                                                    */
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

#include "register_types.h"

#include "foundry_build_task.h"
#include "foundry_script.h"
#include "fs_cache.h"
#include "fs_parser.h"
#include "fs_reflection.h"
#include "fs_tokenizer_buffer.h"
#include "fs_utility_functions.h"

#ifdef TOOLS_ENABLED
#include "fs_format.h"

#include "editor/fs_build_pipeline_settings.h"
#include "editor/fs_highlighter.h"
#include "editor/fs_migration_wizard_plugin.h"
#include "editor/fs_translation_parser_plugin.h"

#ifndef FOUNDRY_SCRIPT_NO_LSP
#include "language_server/fs_language_server.h"
#endif
#endif // TOOLS_ENABLED

#ifdef TESTS_ENABLED
#include "tests/test_foundry_script.h"
#endif

#include "core/io/file_access.h"
#include "core/io/resource_loader.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_node.h"
#include "editor/export/editor_export.h"
#include "editor/translations/editor_translation_parser.h"

#ifndef FOUNDRY_SCRIPT_NO_LSP
#include "core/config/engine.h"
#endif
#endif // TOOLS_ENABLED

#ifdef TESTS_ENABLED
#include "tests/test_macros.h"
#endif

FSLanguage *script_language_gd = nullptr;
Ref<ResourceFormatLoaderFoundryScript> resource_loader_gd;
Ref<ResourceFormatSaverFoundryScript> resource_saver_gd;
FSCache *fs_cache = nullptr;

#ifdef TOOLS_ENABLED

Ref<FSEditorTranslationParserPlugin> fs_translation_parser_plugin;

class EditorExportFoundryScript : public EditorExportPlugin {
	FOUNDRY_CLASS(EditorExportFoundryScript, EditorExportPlugin);

	static constexpr EditorExportPreset::ScriptExportMode DEFAULT_SCRIPT_MODE = EditorExportPreset::MODE_SCRIPT_BINARY_TOKENS_COMPRESSED;
	EditorExportPreset::ScriptExportMode script_mode = DEFAULT_SCRIPT_MODE;

protected:
	virtual void _export_begin(const HashSet<String> &p_features, bool p_debug, const String &p_path, int p_flags) override {
		script_mode = DEFAULT_SCRIPT_MODE;

		const Ref<EditorExportPreset> &preset = get_export_preset();
		if (preset.is_valid()) {
			script_mode = preset->get_script_export_mode();
		}
	}

	virtual void _export_file(const String &p_path, const String &p_type, const HashSet<String> &p_features) override {
		if (p_path.get_extension() != "fs" || script_mode == EditorExportPreset::MODE_SCRIPT_TEXT) {
			return;
		}

		Vector<uint8_t> file = FileAccess::get_file_as_bytes(p_path);
		if (file.is_empty()) {
			return;
		}

		String source = String::utf8(reinterpret_cast<const char *>(file.ptr()), file.size());
		FSTokenizerBuffer::CompressMode compress_mode = script_mode == EditorExportPreset::MODE_SCRIPT_BINARY_TOKENS_COMPRESSED ? FSTokenizerBuffer::COMPRESS_ZSTD : FSTokenizerBuffer::COMPRESS_NONE;
		file = FSTokenizerBuffer::parse_code_string(source, compress_mode);
		if (file.is_empty()) {
			return;
		}

		add_file(p_path.get_basename() + ".fsc", file, true);
	}

public:
	virtual String get_name() const override { return "FoundryScript"; }
};

static FSMigrationWizardDialog *fs_migration_wizard_dialog = nullptr;
static FSBuildPipelineSettingsDialog *fs_build_pipeline_settings_dialog = nullptr;

static void _open_foundry_script_build_pipeline_settings() {
	if (fs_build_pipeline_settings_dialog) {
		fs_build_pipeline_settings_dialog->popup_settings();
	}
}

static void _open_foundry_script_migration_wizard() {
	if (fs_migration_wizard_dialog) {
		fs_migration_wizard_dialog->popup_wizard();
	}
}

static void _editor_init() {
	Ref<EditorExportFoundryScript> gd_export;
	gd_export.instantiate();
	EditorExport::get_singleton()->add_export_plugin(gd_export);

#ifdef TOOLS_ENABLED
	Ref<FSSyntaxHighlighter> fs_syntax_highlighter;
	fs_syntax_highlighter.instantiate();
	ScriptEditor::get_singleton()->register_syntax_highlighter(fs_syntax_highlighter);

	// The editor entry point for the strict-typing migration wizard: a Project > Tools action that
	// opens the dialog driving the same orchestrator as the `--foundry_script-migrate` headless command.
	fs_migration_wizard_dialog = memnew(FSMigrationWizardDialog);
	EditorNode::get_singleton()->get_gui_base()->add_child(fs_migration_wizard_dialog);
	EditorNode::get_singleton()->add_tool_menu_item(
			TTR("Migrate to Strict Typing..."),
			callable_mp_static(&_open_foundry_script_migration_wizard));

	fs_build_pipeline_settings_dialog = memnew(FSBuildPipelineSettingsDialog);
	EditorNode::get_singleton()->get_gui_base()->add_child(fs_build_pipeline_settings_dialog);
	EditorNode::get_singleton()->add_tool_menu_item(
			TTR("Build Pipeline..."),
			callable_mp_static(&_open_foundry_script_build_pipeline_settings));
#endif

#ifndef FOUNDRY_SCRIPT_NO_LSP
	register_lsp_types();
	FSLanguageServer *lsp_plugin = memnew(FSLanguageServer);
	EditorNode::get_singleton()->add_editor_plugin(lsp_plugin);
	Engine::get_singleton()->add_singleton(Engine::Singleton("FSLanguageProtocol", FSLanguageProtocol::get_singleton()));
#endif // !FOUNDRY_SCRIPT_NO_LSP
}

#endif // TOOLS_ENABLED

void initialize_foundry_script_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SERVERS) {
		FOUNDRY_REGISTER_CLASS(FoundryScript);
		FOUNDRY_REGISTER_CLASS(FoundryBuildTaskConfigSchema);
		FOUNDRY_REGISTER_CLASS(FoundryBuildCommand);
		FOUNDRY_REGISTER_CLASS(FoundryBuildResult);
		FOUNDRY_REGISTER_CLASS(FoundryBuildContext);
		FOUNDRY_REGISTER_CLASS(FoundryBuildTask);
		FOUNDRY_REGISTER_CLASS(FoundryCommandBuildTask);
		FOUNDRY_REGISTER_CLASS(FSTypeParameter);
		FOUNDRY_REGISTER_CLASS(FSSpecializedClassHandle);
		FOUNDRY_REGISTER_CLASS(FSAnnotation);
		FOUNDRY_REGISTER_CLASS(FSMethodDescriptor);
		FOUNDRY_REGISTER_CLASS(FSPropertyDescriptor);
		FOUNDRY_REGISTER_CLASS(FSReflection);
		FOUNDRY_REGISTER_CLASS(FSNamespace);

		script_language_gd = memnew(FSLanguage);
		ScriptServer::register_language(script_language_gd);

		resource_loader_gd.instantiate();
		ResourceLoader::add_resource_format_loader(resource_loader_gd);

		resource_saver_gd.instantiate();
		ResourceSaver::add_resource_format_saver(resource_saver_gd);

		fs_cache = memnew(FSCache);

		FSUtilityFunctions::register_functions();
	}

#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_SERVERS) {
		EditorNode::add_init_callback(_editor_init);

		fs_translation_parser_plugin.instantiate();
		EditorTranslationParser::get_singleton()->add_parser(fs_translation_parser_plugin, EditorTranslationParser::STANDARD);
	} else if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		FOUNDRY_REGISTER_CLASS(FSSyntaxHighlighter);
		FOUNDRY_REGISTER_CLASS(FSMigrationWizardDialog);
		FOUNDRY_REGISTER_CLASS(FSBuildPipelineSettingsDialog);
	}
#endif // TOOLS_ENABLED
}

void uninitialize_foundry_script_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SERVERS) {
		ScriptServer::unregister_language(script_language_gd);

		if (fs_cache) {
			memdelete(fs_cache);
		}

		if (script_language_gd) {
			memdelete(script_language_gd);
		}

		ResourceLoader::remove_resource_format_loader(resource_loader_gd);
		resource_loader_gd.unref();

		ResourceSaver::remove_resource_format_saver(resource_saver_gd);
		resource_saver_gd.unref();

		FSParser::cleanup();
		FSUtilityFunctions::unregister_functions();
	}

#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		EditorTranslationParser::get_singleton()->remove_parser(fs_translation_parser_plugin, EditorTranslationParser::STANDARD);
		fs_translation_parser_plugin.unref();
	}
#endif // TOOLS_ENABLED
}

#ifdef TESTS_ENABLED
void test_tokenizer() {
	FSTests::test(FSTests::TestType::TEST_TOKENIZER);
}

void test_tokenizer_buffer() {
	FSTests::test(FSTests::TestType::TEST_TOKENIZER_BUFFER);
}

void test_parser() {
	FSTests::test(FSTests::TestType::TEST_PARSER);
}

void test_compiler() {
	FSTests::test(FSTests::TestType::TEST_COMPILER);
}

void test_bytecode() {
	FSTests::test(FSTests::TestType::TEST_BYTECODE);
}

void generate_foundry_script_tests() {
	FSTests::FSTestRunner::generate_outputs_for_cmdline();
}

// The canonical formatter and its CLI live under `TOOLS_ENABLED` (the tokenizer
// only records comments there), so these commands must not be referenced in a
// `tests=yes` build that is not also a tools/editor build.
#ifdef TOOLS_ENABLED
void fs_format_command() {
	FSFormatterCLI::run_from_cmdline();
}

void fs_generate_format_tests() {
	FSFormatterCLI::generate_format_tests();
}
#endif // TOOLS_ENABLED

REGISTER_TEST_COMMAND("foundry_script-tokenizer", &test_tokenizer);
REGISTER_TEST_COMMAND("foundry_script-tokenizer-buffer", &test_tokenizer_buffer);
REGISTER_TEST_COMMAND("foundry_script-parser", &test_parser);
REGISTER_TEST_COMMAND("foundry_script-compiler", &test_compiler);
REGISTER_TEST_COMMAND("foundry_script-bytecode", &test_bytecode);
REGISTER_TEST_COMMAND("--foundry_script-generate-tests", &generate_foundry_script_tests);
#ifdef TOOLS_ENABLED
REGISTER_TEST_COMMAND("--foundry_script-format", &fs_format_command);
REGISTER_TEST_COMMAND("--foundry_script-generate-format-tests", &fs_generate_format_tests);
#endif // TOOLS_ENABLED
#endif
