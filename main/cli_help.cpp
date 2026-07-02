/**************************************************************************/
/*  cli_help.cpp                                                          */
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

#include "cli_help.h"

#include "core/io/json.h"

namespace {

using CommandOption = FoundryCLIHelp::CommandOption;
using CommandSpec = FoundryCLIHelp::CommandSpec;
using NounSpec = FoundryCLIHelp::NounSpec;
using Positional = FoundryCLIHelp::Positional;

const int HELP_OPTION_COLUMN_LENGTH = 36;

const NounSpec NOUNS[] = {
	{ "editor", "Open the editor or the Project Manager." },
	{ "project", "Run, export, or import a project." },
	{ "script", "Format, lint, and migrate Foundry Script code." },
	{ "test", "Run the engine test suites." },
	{ "lsp", "Run the Foundry Script language server." },
	{ "docs", "Generate engine and extension API documentation." },
	{ "extension", "FoundryExtension interface tooling." },
	{ "diagnostics", "Probe rendering and device support." },
};

const char *PROJECT_OPTION_DESCRIPTION = "Project directory containing a project.foundry file.";

const CommandOption EDITOR_OPEN_OPTIONS[] = {
	{ "--project", "dir", PROJECT_OPTION_DESCRIPTION, false },
};

const CommandOption PROJECT_RUN_OPTIONS[] = {
	{ "--project", "dir", PROJECT_OPTION_DESCRIPTION, false },
	{ "--scene", "path", "Path or UID of the scene to start.", false },
	{ "--script", "path", "Run the given script instead of a scene.", false },
	{ "--check-only", nullptr, "Only parse the script for errors and quit.", false },
};

const CommandOption PROJECT_EXPORT_OPTIONS[] = {
	{ "--project", "dir", PROJECT_OPTION_DESCRIPTION, false },
	{ "--preset", "name", "Export preset name from export_presets.cfg.", true },
	{ "--output", "path", "Output path, including the binary filename.", true },
	{ "--mode", "release|debug|pack|patch", "Export mode (default: release).", false },
	{ "--patches", "paths", "Comma-separated list of patch packs (used with --mode patch).", false },
	{ "--install-android-build-template", nullptr, "Install the Android build template before exporting.", false },
};

const CommandOption PROJECT_IMPORT_OPTIONS[] = {
	{ "--project", "dir", PROJECT_OPTION_DESCRIPTION, false },
};

const CommandOption SCRIPT_FORMAT_OPTIONS[] = {
	{ "--project", "dir", PROJECT_OPTION_DESCRIPTION, false },
	{ "--check", nullptr, "Exit non-zero if any file would change; do not write.", false },
	{ "--write", nullptr, "Rewrite formatted files in place.", false },
	{ "--diff", nullptr, "Print a unified diff of pending changes.", false },
};

const CommandOption SCRIPT_LINT_OPTIONS[] = {
	{ "--project", "dir", PROJECT_OPTION_DESCRIPTION, false },
	{ "--format=<json|sarif>", nullptr, "Machine-readable report format.", false },
	{ "--out", "path", "Write the report to a file instead of stdout.", false },
	{ "--fail-on=<error|warning>", nullptr, "Severity threshold for a non-zero exit code.", false },
};

const CommandOption SCRIPT_MIGRATE_OPTIONS[] = {
	{ "--project", "dir", PROJECT_OPTION_DESCRIPTION, true },
	{ "--apply", nullptr, "Commit inferred type annotations to disk (otherwise dry-run).", false },
	{ "--strict", "null,dynamic", "Strict checks to apply to the project, comma-separated.", false },
	{ "--activate-strict", nullptr, "Flip the requested strict project settings.", false },
	{ "--confirm", nullptr, "Confirm the gated strict-settings flip.", false },
	{ "--allow-violations", nullptr, "Allow the strict flip even when violations remain.", false },
	{ "--acknowledge-vcs", nullptr, "Proceed with --apply on an unversioned or dirty tree.", false },
	{ "--follow-up", "path", "Write the manual follow-up punch list to a file.", false },
};

const CommandOption TEST_RUN_OPTIONS[] = {
	{ "--project", "dir", PROJECT_OPTION_DESCRIPTION, false },
	{ "--case", "pattern", "doctest test case filter pattern.", false },
};

const CommandOption LSP_SERVE_OPTIONS[] = {
	{ "--project", "dir", PROJECT_OPTION_DESCRIPTION, false },
	{ "--port", "port", "LSP port. Recommended range [1024, 49151].", false },
};

const CommandOption DOCS_GENERATE_API_OPTIONS[] = {
	{ "--include-docs", nullptr, "Include class documentation in the dump.", false },
};

const CommandOption DOCS_GENERATE_ENGINE_OPTIONS[] = {
	{ "--output", "path", "Output directory (default: current directory).", false },
	{ "--no-docbase", nullptr, "Do not dump the base types.", false },
};

const CommandOption DOCS_GENERATE_SCRIPT_OPTIONS[] = {
	{ "--source", "path", "Directory containing Foundry Script files to document.", true },
	{ "--output", "path", "Output directory (default: current directory).", false },
};

const CommandOption EXTENSION_DUMP_INTERFACE_OPTIONS[] = {
	{ "--format", "header|json", "Interface dump format (default: header).", false },
};

const CommandOption EXTENSION_VALIDATE_API_OPTIONS[] = {
	{ "--input", "path", "Extension API JSON file from a previous engine version.", true },
};

const Positional PATHS_POSITIONAL[] = {
	{ "paths", true, true },
};

const Positional DOCTEST_ARGS_POSITIONAL[] = {
	{ "doctest-args", true, true },
};

#define FOUNDRY_CLI_COUNT(m_array) ((int)(sizeof(m_array) / sizeof((m_array)[0])))

const CommandSpec COMMANDS[] = {
	{ "editor", "open", "Open a project in the editor.", "[--project <dir>]", FoundryCLIHelp::AVAILABILITY_EDITOR, EDITOR_OPEN_OPTIONS, FOUNDRY_CLI_COUNT(EDITOR_OPEN_OPTIONS), nullptr, 0, "foundry editor open --project ." },
	{ "editor", "project-manager", "Open the Project Manager.", "", FoundryCLIHelp::AVAILABILITY_EDITOR, nullptr, 0, nullptr, 0, "foundry editor project-manager" },
	{ "project", "run", "Run a project; arguments after -- go to the project.", "[--project <dir>] [--scene <path>] [--script <path>] [--check-only] [-- <user args...>]", FoundryCLIHelp::AVAILABILITY_RELEASE, PROJECT_RUN_OPTIONS, FOUNDRY_CLI_COUNT(PROJECT_RUN_OPTIONS), nullptr, 0, "foundry project run --project . --scene res://main.tscn -- --difficulty hard" },
	{ "project", "export", "Export a project with a preset.", "[--project <dir>] --preset <name> --output <path> [--mode <release|debug|pack|patch>] [--patches <paths>] [--install-android-build-template]", FoundryCLIHelp::AVAILABILITY_EDITOR, PROJECT_EXPORT_OPTIONS, FOUNDRY_CLI_COUNT(PROJECT_EXPORT_OPTIONS), nullptr, 0, "foundry project export --project . --preset Linux --output build/game.x86_64 --mode release" },
	{ "project", "import", "Import project resources and exit.", "[--project <dir>]", FoundryCLIHelp::AVAILABILITY_EDITOR, PROJECT_IMPORT_OPTIONS, FOUNDRY_CLI_COUNT(PROJECT_IMPORT_OPTIONS), nullptr, 0, "foundry project import --project ." },
	{ "script", "format", "Format Foundry Script files or stdin.", "[--project <dir>] [--check|--write|--diff] [paths...]", FoundryCLIHelp::AVAILABILITY_RELEASE, SCRIPT_FORMAT_OPTIONS, FOUNDRY_CLI_COUNT(SCRIPT_FORMAT_OPTIONS), PATHS_POSITIONAL, FOUNDRY_CLI_COUNT(PATHS_POSITIONAL), "foundry script format --project . --check scripts" },
	{ "script", "lint", "Lint Foundry Script files.", "[--project <dir>] [--format=<json|sarif>] [--out <path>] [--fail-on=<error|warning>] [paths...]", FoundryCLIHelp::AVAILABILITY_RELEASE, SCRIPT_LINT_OPTIONS, FOUNDRY_CLI_COUNT(SCRIPT_LINT_OPTIONS), PATHS_POSITIONAL, FOUNDRY_CLI_COUNT(PATHS_POSITIONAL), "foundry script lint --project . --format=sarif --out reports/foundry-script.sarif scripts" },
	{ "script", "migrate", "Run the Foundry Script strict-typing migration wizard.", "--project <dir> [--apply] [--strict <null,dynamic>] [--activate-strict] [--confirm] [--allow-violations] [--acknowledge-vcs] [--follow-up <path>]", FoundryCLIHelp::AVAILABILITY_EDITOR, SCRIPT_MIGRATE_OPTIONS, FOUNDRY_CLI_COUNT(SCRIPT_MIGRATE_OPTIONS), nullptr, 0, "foundry script migrate --trusted --project . --apply --strict null,dynamic --confirm" },
	{ "test", "run", "Run the engine doctest suites.", "[--project <dir>] [--case <pattern>] [doctest-args...]", FoundryCLIHelp::AVAILABILITY_RELEASE, TEST_RUN_OPTIONS, FOUNDRY_CLI_COUNT(TEST_RUN_OPTIONS), DOCTEST_ARGS_POSITIONAL, FOUNDRY_CLI_COUNT(DOCTEST_ARGS_POSITIONAL), "foundry test run --case \"*FoundryScript*\"" },
	{ "lsp", "serve", "Start the Foundry Script language server.", "[--project <dir>] [--port <port>]", FoundryCLIHelp::AVAILABILITY_EDITOR, LSP_SERVE_OPTIONS, FOUNDRY_CLI_COUNT(LSP_SERVE_OPTIONS), nullptr, 0, "foundry lsp serve --project . --port 6005" },
	{ "docs", "generate-api", "Generate the extension API JSON dump.", "[--include-docs]", FoundryCLIHelp::AVAILABILITY_EDITOR, DOCS_GENERATE_API_OPTIONS, FOUNDRY_CLI_COUNT(DOCS_GENERATE_API_OPTIONS), nullptr, 0, "foundry docs generate-api --include-docs" },
	{ "docs", "generate-engine", "Dump the engine class reference XML.", "[--output <path>] [--no-docbase]", FoundryCLIHelp::AVAILABILITY_EDITOR, DOCS_GENERATE_ENGINE_OPTIONS, FOUNDRY_CLI_COUNT(DOCS_GENERATE_ENGINE_OPTIONS), nullptr, 0, "foundry docs generate-engine --output doc-out" },
	{ "docs", "generate-script", "Generate API reference from Foundry Script sources.", "--source <path> [--output <path>]", FoundryCLIHelp::AVAILABILITY_EDITOR, DOCS_GENERATE_SCRIPT_OPTIONS, FOUNDRY_CLI_COUNT(DOCS_GENERATE_SCRIPT_OPTIONS), nullptr, 0, "foundry docs generate-script --source addons/library" },
	{ "extension", "dump-interface", "Generate FoundryExtension interface files.", "[--format <header|json>]", FoundryCLIHelp::AVAILABILITY_EDITOR, EXTENSION_DUMP_INTERFACE_OPTIONS, FOUNDRY_CLI_COUNT(EXTENSION_DUMP_INTERFACE_OPTIONS), nullptr, 0, "foundry extension dump-interface --format json" },
	{ "extension", "validate-api", "Validate an extension API dump for compatibility.", "--input <path>", FoundryCLIHelp::AVAILABILITY_EDITOR, EXTENSION_VALIDATE_API_OPTIONS, FOUNDRY_CLI_COUNT(EXTENSION_VALIDATE_API_OPTIONS), nullptr, 0, "foundry extension validate-api --input extension_api.json" },
	{ "diagnostics", "render-device-support", "Probe rendering device support.", "", FoundryCLIHelp::AVAILABILITY_RELEASE, nullptr, 0, nullptr, 0, "foundry diagnostics render-device-support" },
	{ "diagnostics", "render-device-create", "Attempt rendering device creation.", "", FoundryCLIHelp::AVAILABILITY_RELEASE, nullptr, 0, nullptr, 0, "foundry diagnostics render-device-create" },
};

const CommandOption GLOBAL_OPTIONS[] = {
	{ "--project", "dir", PROJECT_OPTION_DESCRIPTION, false },
	{ "--json", nullptr, "Machine-readable output where supported; suppresses the startup header.", false },
	{ "--trusted", nullptr, "Allow project-defined build task execution.", false },
	{ "--headless", nullptr, "Headless mode (no display, dummy audio).", false },
	{ "--quiet", nullptr, "Silence stdout messages; errors are still shown.", false },
	{ "--verbose", nullptr, "Verbose stdout mode.", false },
	{ "--no-header", nullptr, "Do not print the engine version header on startup.", false },
	{ "--version", nullptr, "Print the version string and exit.", false },
	{ "-h, --help", nullptr, "Print help; combine with a command for scoped help.", false },
};

bool is_command_in_build(const CommandSpec &p_spec) {
#ifdef TOOLS_ENABLED
	return true;
#else
	return p_spec.availability == FoundryCLIHelp::AVAILABILITY_RELEASE;
#endif
}

bool is_noun_in_build(const String &p_noun) {
	for (const CommandSpec &spec : COMMANDS) {
		if (p_noun == spec.noun && is_command_in_build(spec)) {
			return true;
		}
	}
	return false;
}

String help_title(const String &p_title) {
	return "\n\u001b[1;93m" + p_title + ":\u001b[0m\n";
}

// Two-column row matching the engine's historical help styling: green name
// column with magenta/cyan placeholder coloring, optional red E badge.
String help_item(const String &p_name, const String &p_description, bool p_editor_badge) {
	const String badge = p_editor_badge ? String("\u001b[1;91mE\u001b[0m") : String(" ");
	const String column = p_name.rpad(HELP_OPTION_COLUMN_LENGTH)
								  .replace("[", "\u001b[96m[")
								  .replace("]", "]\u001b[0m")
								  .replace("<", "\u001b[95m<")
								  .replace(">", ">\u001b[0m");
	return "  \u001b[92m" + column + "\u001b[0m " + badge + "  " + p_description + "\n";
}

String editor_badge_legend() {
#ifdef TOOLS_ENABLED
	return "\n  \u001b[1;91mE\u001b[0m  Only available in editor builds.\n";
#else
	return String();
#endif
}

String option_display(const CommandOption &p_option) {
	String display = p_option.flag;
	if (p_option.value_name) {
		display += " <" + String(p_option.value_name) + ">";
	}
	return display;
}

const CommandSpec *find_command(const String &p_noun, const String &p_verb) {
	for (const CommandSpec &spec : COMMANDS) {
		if (p_noun == spec.noun && p_verb == spec.verb) {
			return &spec;
		}
	}
	return nullptr;
}

String usage_line(const CommandSpec &p_spec) {
	String usage = "foundry " + String(p_spec.noun) + " " + String(p_spec.verb);
	if (p_spec.usage_args && p_spec.usage_args[0]) {
		usage += " " + String(p_spec.usage_args);
	}
	return usage;
}

} // namespace

const FoundryCLIHelp::NounSpec *FoundryCLIHelp::get_nouns(int &r_count) {
	r_count = FOUNDRY_CLI_COUNT(NOUNS);
	return NOUNS;
}

const FoundryCLIHelp::CommandSpec *FoundryCLIHelp::get_commands(int &r_count) {
	r_count = FOUNDRY_CLI_COUNT(COMMANDS);
	return COMMANDS;
}

bool FoundryCLIHelp::has_noun(const String &p_noun) {
	for (const NounSpec &noun : NOUNS) {
		if (p_noun == noun.name) {
			return true;
		}
	}
	return false;
}

bool FoundryCLIHelp::has_command(const String &p_noun, const String &p_verb) {
	return find_command(p_noun, p_verb) != nullptr;
}

String FoundryCLIHelp::get_top_help_text(const String &p_binary) {
	String text;
	text += help_title("Usage");
	text += "  " + p_binary + " \u001b[96m<command> <subcommand> [options] [-- user args]\u001b[0m\n";
	text += help_title("Commands");
	for (const NounSpec &noun : NOUNS) {
		if (!is_noun_in_build(noun.name)) {
			continue;
		}
		text += help_item(noun.name, noun.summary, false);
	}
	text += help_title("Global options");
	for (const CommandOption &option : GLOBAL_OPTIONS) {
		text += help_item(option_display(option), option.description, false);
	}
	text += "\nRun 'foundry <command> --help' for command details.\n";
	return text;
}

String FoundryCLIHelp::get_noun_help_text(const String &p_noun) {
	String text;
	text += help_title("Usage");
	text += "  foundry " + p_noun + " \u001b[96m<subcommand> [options]\u001b[0m\n";
	text += help_title("Subcommands");
	bool any_editor_badge = false;
	int rendered_count = 0;
	for (const CommandSpec &spec : COMMANDS) {
		if (p_noun != spec.noun || !is_command_in_build(spec)) {
			continue;
		}
		const bool editor_badge = spec.availability == AVAILABILITY_EDITOR;
		any_editor_badge = any_editor_badge || editor_badge;
		text += help_item(spec.verb, spec.summary, editor_badge);
		rendered_count++;
	}
	if (rendered_count == 0) {
		text += "  (All " + p_noun + " subcommands require an editor build.)\n";
	}
	if (any_editor_badge) {
		text += editor_badge_legend();
	}
	text += "\nRun 'foundry " + p_noun + " <subcommand> --help' for details.\n";
	return text;
}

String FoundryCLIHelp::get_command_help_text(const String &p_noun, const String &p_verb) {
	const CommandSpec *spec = find_command(p_noun, p_verb);
	if (spec == nullptr) {
		return String();
	}
	String text;
	text += "\n\u001b[92mfoundry " + p_noun + " " + p_verb + "\u001b[0m - " + spec->summary;
	if (spec->availability == AVAILABILITY_EDITOR) {
		text += " \u001b[1;91m[editor builds only]\u001b[0m";
	}
	text += "\n";
	text += help_title("Usage");
	text += "  " + usage_line(*spec) + "\n";
	if (spec->option_count > 0) {
		text += help_title("Options");
		for (int i = 0; i < spec->option_count; i++) {
			const CommandOption &option = spec->options[i];
			String description = option.description;
			if (option.required) {
				description += " (required)";
			}
			text += help_item(option_display(option), description, false);
		}
	}
	if (spec->example) {
		text += help_title("Example");
		text += "  " + String(spec->example) + "\n";
	}
	return text;
}

String FoundryCLIHelp::get_scoped_help_text(const String &p_binary, const PackedStringArray &p_scope, bool &r_valid) {
	r_valid = true;
	if (p_scope.is_empty()) {
		return get_top_help_text(p_binary);
	}
	if (!has_noun(p_scope[0])) {
		r_valid = false;
		return get_top_help_text(p_binary);
	}
	if (p_scope.size() == 1) {
		return get_noun_help_text(p_scope[0]);
	}
	if (p_scope.size() == 2 && has_command(p_scope[0], p_scope[1])) {
		return get_command_help_text(p_scope[0], p_scope[1]);
	}
	r_valid = false;
	return get_noun_help_text(p_scope[0]);
}

String FoundryCLIHelp::get_help_json(const PackedStringArray &p_scope) {
	return String("{}");
}
