/**************************************************************************/
/*  main.cpp                                                              */
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

#include "main.h"

#include "core/config/project_build_pipeline_status.h"
#include "core/config/project_settings.h"
#include "core/core_globals.h"
#include "core/crypto/crypto.h"
#include "core/debugger/engine_debugger.h"
#include "core/extension/extension_api_dump.h"
#include "core/extension/foundry_extension_interface_dump.gen.h"
#include "core/extension/foundry_extension_interface_header_generator.h"
#include "core/extension/foundry_extension_manager.h"
#include "core/input/input.h"
#include "core/input/input_map.h"
#include "core/io/dir_access.h"
#include "core/io/file_access_pack.h"
#include "core/io/file_access_zip.h"
#include "core/io/image.h"
#include "core/io/image_loader.h"
#include "core/io/ip.h"
#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "core/object/message_queue.h"
#include "core/object/script_language.h"
#include "core/object/script_runner.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "core/profiling/profiling.h"
#include "core/register_core_types.h"
#include "core/string/translation_server.h"
#include "core/version.h"
#include "drivers/register_driver_types.h"
#include "main/app_icon.gen.h"
#include "main/cli_help.h"
#include "main/cli_parser.h"
#include "main/main_timer_sync.h"
#include "main/performance.h"
#include "main/splash.gen.h"
#include "main/version_info.h"
#include "modules/register_module_types.h"
#include "platform/register_platform_apis.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "scene/property_list_helper.h"
#include "scene/register_scene_types.h"
#include "scene/resources/packed_scene.h"
#include "scene/theme/theme_db.h"
#include "servers/audio/audio_driver_dummy.h"
#include "servers/audio/audio_server.h"
#include "servers/camera/camera_server.h"
#include "servers/display/accessibility_server.h"
#include "servers/display/display_server.h"
#include "servers/movie_writer/movie_writer.h"
#include "servers/register_server_types.h"
#include "servers/rendering/rendering_server_default.h"
#include "servers/text/text_server.h"
#include "servers/text/text_server_dummy.h"

// 2D
#ifndef NAVIGATION_2D_DISABLED
#include "servers/navigation_2d/navigation_server_2d.h"
#include "servers/navigation_2d/navigation_server_2d_dummy.h"
#endif // NAVIGATION_2D_DISABLED

#ifndef PHYSICS_2D_DISABLED
#include "servers/physics_2d/physics_server_2d.h"
#include "servers/physics_2d/physics_server_2d_dummy.h"
#endif // PHYSICS_2D_DISABLED

// 3D
#ifndef NAVIGATION_3D_DISABLED
#include "servers/navigation_3d/navigation_server_3d.h"
#include "servers/navigation_3d/navigation_server_3d_dummy.h"
#endif // NAVIGATION_3D_DISABLED

#ifndef PHYSICS_3D_DISABLED
#include "servers/physics_3d/physics_server_3d.h"
#include "servers/physics_3d/physics_server_3d_dummy.h"
#endif // PHYSICS_3D_DISABLED

#ifndef XR_DISABLED
#include "servers/xr/xr_server.h"
#endif // XR_DISABLED

#ifdef TESTS_ENABLED
#include "tests/foundry_test_progress.h"
#include "tests/test_main.h"
#endif

#ifdef TOOLS_ENABLED
#include "editor/automation/editor_automation_server.h"
#include "editor/debugger/debug_adapter/debug_adapter_server.h"
#include "editor/debugger/editor_debugger_node.h"
#include "editor/doc/doc_data_class_path.gen.h"
#include "editor/doc/doc_tools.h"
#include "editor/doc/editor_help.h"
#include "editor/editor_node.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/file_system/editor_paths.h"
#include "editor/project_manager/known_project_store.h"
#include "editor/project_manager/startup_router.h"
#include "editor/register_editor_types.h"
#include "editor/settings/editor_settings.h"
#include "editor/tooling/editor_tooling_host.h"
#include "editor/translations/editor_translation.h"

#if defined(TOOLS_ENABLED) && !defined(NO_EDITOR_SPLASH)
#include "main/splash_editor.gen.h"
#endif

#endif // TOOLS_ENABLED

#if defined(STEAMAPI_ENABLED)
#include "main/steam_tracker.h"
#endif

#include "modules/modules_enabled.gen.h" // For mono.

#if defined(MODULE_MONO_ENABLED) && defined(TOOLS_ENABLED)
#include "modules/mono/editor/bindings_generator.h"
#endif

#ifdef MODULE_FOUNDRY_SCRIPT_ENABLED
#include "modules/foundry_script/foundry_script.h"
#include "modules/foundry_script/fs_autoload_index.h"
#include "modules/foundry_script/fs_build_pipeline_runner.h"
#include "modules/foundry_script/fs_inline_eval.h"
#ifdef TOOLS_ENABLED
#include "modules/foundry_script/editor/fs_migration_wizard.h"
#include "modules/foundry_script/fs_format.h"
#include "modules/foundry_script/fs_lint.h"
#include "modules/foundry_script/tests/fs_test_runner.h"
#endif // TOOLS_ENABLED
#if defined(TOOLS_ENABLED) && !defined(FOUNDRY_SCRIPT_NO_LSP)
#include "modules/foundry_script/language_server/fs_language_server.h"
#endif // TOOLS_ENABLED && !FOUNDRY_SCRIPT_NO_LSP
#endif // MODULE_FOUNDRY_SCRIPT_ENABLED

/* Static members */

// Singletons

// Initialized in setup()
static Engine *engine = nullptr;
static ProjectSettings *globals = nullptr;
static Input *input = nullptr;
static InputMap *input_map = nullptr;
static TranslationServer *translation_server = nullptr;
static Performance *performance = nullptr;
static PackedData *packed_data = nullptr;
#ifdef MINIZIP_ENABLED
static ZipArchive *zip_packed_data = nullptr;
#endif
static MessageQueue *message_queue = nullptr;

#if defined(STEAMAPI_ENABLED)
static SteamTracker *steam_tracker = nullptr;
#endif

// Initialized in setup2()
static AudioServer *audio_server = nullptr;
static CameraServer *camera_server = nullptr;
static AccessibilityServer *accessibility_server = nullptr;
static DisplayServer *display_server = nullptr;
static RenderingServer *rendering_server = nullptr;
static TextServerManager *tsman = nullptr;
static ThemeDB *theme_db = nullptr;
#ifndef PHYSICS_2D_DISABLED
static PhysicsServer2DManager *physics_server_2d_manager = nullptr;
static PhysicsServer2D *physics_server_2d = nullptr;
#endif // PHYSICS_2D_DISABLED
#ifndef PHYSICS_3D_DISABLED
static PhysicsServer3DManager *physics_server_3d_manager = nullptr;
static PhysicsServer3D *physics_server_3d = nullptr;
#endif // PHYSICS_3D_DISABLED
#ifndef XR_DISABLED
static XRServer *xr_server = nullptr;
#endif // XR_DISABLED
// We error out if setup2() doesn't turn this true
static bool _start_success = false;

// Drivers

String display_driver = "";
String tablet_driver = "";
String text_driver = "";
String rendering_driver = "";
String rendering_method = "";
static int text_driver_idx = -1;
static int audio_driver_idx = -1;

// Engine config/tools

static AccessibilityServerEnums::AccessibilityMode accessibility_mode = AccessibilityServerEnums::AccessibilityMode::ACCESSIBILITY_AUTO;
static String accessibility_driver_name;
static bool accessibility_mode_set = false;
static bool single_window = false;
static bool editor = false;
static bool cmdline_tool = false;
static String locale;
static String log_file;
static bool show_help = false;
static uint64_t quit_after = 0;
static OS::ProcessID editor_pid = 0;
// Set when an explicit CLI `--project` path could not be applied (e.g. the directory does
// not exist). Lets `script eval` refuse to run against a fallback/ambient project instead
// of silently evaluating outside the project the caller requested.
static bool foundry_cli_project_path_error = false;
#ifdef TOOLS_ENABLED
static bool projectless_editor_shell = false;
static bool found_project = false;
// Set for a genuine interactive editor launch (bare launch or `editor open`) that is
// eligible for projectless startup routing (#1135). Gates recording a successful open
// into the global known-project store so background editor-mode services like
// `lsp serve` do not rewrite the GUI auto-open candidate/recents.
static bool interactive_editor_launch = false;
static bool recovery_mode = false;
static bool auto_build_solutions = false;
static String debug_server_uri;
static bool wait_for_import = false;
static bool restore_editor_window_layout = true;
HashMap<Main::CLIScope, Vector<String>> forwardable_cli_arguments;
#endif
static bool single_threaded_scene = false;

// Display

static DisplayServer::WindowMode window_mode = DisplayServer::WINDOW_MODE_WINDOWED;
static DisplayServer::ScreenOrientation window_orientation = DisplayServer::SCREEN_LANDSCAPE;
static DisplayServer::VSyncMode window_vsync_mode = DisplayServer::VSYNC_ENABLED;
static uint32_t window_flags = 0;
static Size2i window_size = Size2i(1152, 648);

static int init_screen = DisplayServer::SCREEN_PRIMARY;
static bool init_fullscreen = false;
static bool init_maximized = false;
static bool init_windowed = false;
static bool init_always_on_top = false;
static bool init_use_custom_pos = false;
static bool init_use_custom_screen = false;
static Vector2 init_custom_pos;
static int64_t init_embed_parent_window_id = 0;
#ifdef TOOLS_ENABLED
static bool init_display_scale_found = false;
static int init_display_scale = 0;
static bool init_custom_scale_found = false;
static float init_custom_scale = 1.0;
static bool init_expand_to_title = false;
static bool init_expand_to_title_found = false;
#endif
static bool use_custom_res = true;
static bool force_res = false;

// Debug

static bool use_debug_profiler = false;
#ifdef DEBUG_ENABLED
static bool debug_collisions = false;
static bool debug_paths = false;
static bool debug_navigation = false;
static bool debug_avoidance = false;
static bool debug_canvas_item_redraw = false;
static bool debug_mute_audio = false;
#endif
static int max_fps = -1;
static int frame_delay = 0;
static int audio_output_latency = 0;
static bool disable_render_loop = false;
static int fixed_fps = -1;
static MovieWriter *movie_writer = nullptr;
static bool disable_vsync = false;
static bool print_fps = false;
#ifdef TOOLS_ENABLED
static bool editor_pseudolocalization = false;
static bool dump_foundry_extension_interface = false;
static bool dump_foundry_extension_interface_header = false;
static bool dump_extension_api = false;
static bool include_docs_in_extension_api_dump = false;
static bool validate_extension_api = false;
static String validate_extension_api_file;
#endif
static FoundryCLIParser::ParseResult foundry_cli_parse;
bool profile_gpu = false;

// Constants.

static const String NULL_DISPLAY_DRIVER("headless");
static const String EMBEDDED_DISPLAY_DRIVER("embedded");
static const String NULL_AUDIO_DRIVER("Dummy");

/* Helper methods */

bool Main::is_cmdline_tool() {
	return cmdline_tool;
}

#ifdef TOOLS_ENABLED
bool Main::is_projectless_editor_shell() {
	return projectless_editor_shell;
}
#endif

#ifdef TOOLS_ENABLED
const Vector<String> &Main::get_forwardable_cli_arguments(Main::CLIScope p_scope) {
	return forwardable_cli_arguments[p_scope];
}
#endif

static String unescape_cmdline(const String &p_str) {
	return p_str.replace("%20", " ");
}

static String get_full_version_string() {
	String hash = String(FOUNDRY_VERSION_HASH);
	if (!hash.is_empty()) {
		hash = "." + hash.left(9);
	}
	return String(FOUNDRY_VERSION_FULL_BUILD) + hash;
}

#if defined(TOOLS_ENABLED) && defined(MODULE_FOUNDRY_SCRIPT_ENABLED)
static Vector<String> get_files_with_extension(const String &p_root, const String &p_extension) {
	Vector<String> paths;

	Ref<DirAccess> dir = DirAccess::open(p_root);
	if (dir.is_valid()) {
		dir->list_dir_begin();
		String fn = dir->get_next();
		while (!fn.is_empty()) {
			if (!dir->current_is_hidden() && fn != "." && fn != "..") {
				if (dir->current_is_dir()) {
					paths.append_array(get_files_with_extension(p_root.path_join(fn), p_extension));
				} else if (fn.get_extension() == p_extension) {
					paths.append(p_root.path_join(fn));
				}
			}
			fn = dir->get_next();
		}
		dir->list_dir_end();
	}

	return paths;
}
#endif

#ifdef MODULE_FOUNDRY_SCRIPT_ENABLED
static bool run_foundry_build_stage_for_cli(ProjectBuildPipelineConfig::Stage p_stage, const String &p_context) {
	const FoundryBuildPipelineRunner::StageRunResult result = FoundryBuildPipelineRunner::run_stage(p_stage);
	if (result.is_success()) {
		return true;
	}

	FoundryBuildPipelineRunner::print_diagnostics(result.snapshot, p_context);
	if (result.error != OK) {
		ERR_PRINT(vformat("%s returned error %d.", p_context, int(result.error)));
	}
	return false;
}
#endif // MODULE_FOUNDRY_SCRIPT_ENABLED

// FIXME: Could maybe be moved to have less code in main.cpp.
void initialize_physics() {
#ifndef PHYSICS_3D_DISABLED
	/// 3D Physics Server
	physics_server_3d = PhysicsServer3DManager::get_singleton()->new_server(
			GLOBAL_GET(PhysicsServer3DManager::setting_property_name));
	if (!physics_server_3d) {
		// Physics server not found, Use the default physics
		physics_server_3d = PhysicsServer3DManager::get_singleton()->new_default_server();
	}

	// Fall back to dummy if no default server has been registered.
	if (!physics_server_3d) {
		WARN_PRINT(vformat("Falling back to dummy PhysicsServer3D; 3D physics functionality will be disabled. If this is intended, set the %s project setting to Dummy.", PhysicsServer3DManager::setting_property_name));
		physics_server_3d = memnew(PhysicsServer3DDummy);
	}

	// Should be impossible, but make sure it's not null.
	ERR_FAIL_NULL_MSG(physics_server_3d, "Failed to initialize PhysicsServer3D.");
	physics_server_3d->init();
#endif // PHYSICS_3D_DISABLED

#ifndef PHYSICS_2D_DISABLED
	// 2D Physics server
	physics_server_2d = PhysicsServer2DManager::get_singleton()->new_server(
			GLOBAL_GET(PhysicsServer2DManager::get_singleton()->setting_property_name));
	if (!physics_server_2d) {
		// Physics server not found, Use the default physics
		physics_server_2d = PhysicsServer2DManager::get_singleton()->new_default_server();
	}

	// Fall back to dummy if no default server has been registered.
	if (!physics_server_2d) {
		WARN_PRINT(vformat("Falling back to dummy PhysicsServer2D; 2D physics functionality will be disabled. If this is intended, set the %s project setting to Dummy.", PhysicsServer2DManager::setting_property_name));
		physics_server_2d = memnew(PhysicsServer2DDummy);
	}

	// Should be impossible, but make sure it's not null.
	ERR_FAIL_NULL_MSG(physics_server_2d, "Failed to initialize PhysicsServer2D.");
	physics_server_2d->init();
#endif // PHYSICS_2D_DISABLED
}

void finalize_physics() {
#ifndef PHYSICS_3D_DISABLED
	physics_server_3d->finish();
	memdelete(physics_server_3d);
#endif // PHYSICS_3D_DISABLED

#ifndef PHYSICS_2D_DISABLED
	physics_server_2d->finish();
	memdelete(physics_server_2d);
#endif // PHYSICS_2D_DISABLED
}

void finalize_display() {
	rendering_server->finish();
	memdelete(rendering_server);

	memdelete(display_server);
	memdelete(accessibility_server);
}

void initialize_theme_db() {
	theme_db = memnew(ThemeDB);
}

void finalize_theme_db() {
	memdelete(theme_db);
	theme_db = nullptr;
}

//#define DEBUG_INIT
#ifdef DEBUG_INIT
#define MAIN_PRINT(m_txt) print_line(m_txt)
#else
#define MAIN_PRINT(m_txt)
#endif

void Main::print_header(bool p_rich) {
	if (FOUNDRY_VERSION_TIMESTAMP > 0) {
		// Version timestamp available.
		if (p_rich) {
			Engine::get_singleton()->print_header_rich("\u001b[38;5;39m" + String(FOUNDRY_VERSION_NAME) + "\u001b[0m v" + get_full_version_string() + " (" + Time::get_singleton()->get_datetime_string_from_unix_time(FOUNDRY_VERSION_TIMESTAMP, true) + " UTC) - \u001b[4m" + String(FOUNDRY_VERSION_WEBSITE));
		} else {
			Engine::get_singleton()->print_header(String(FOUNDRY_VERSION_NAME) + " v" + get_full_version_string() + " (" + Time::get_singleton()->get_datetime_string_from_unix_time(FOUNDRY_VERSION_TIMESTAMP, true) + " UTC) - " + String(FOUNDRY_VERSION_WEBSITE));
		}
	} else {
		if (p_rich) {
			Engine::get_singleton()->print_header_rich("\u001b[38;5;39m" + String(FOUNDRY_VERSION_NAME) + "\u001b[0m v" + get_full_version_string() + " - \u001b[4m" + String(FOUNDRY_VERSION_WEBSITE));
		} else {
			Engine::get_singleton()->print_header(String(FOUNDRY_VERSION_NAME) + " v" + get_full_version_string() + " - " + String(FOUNDRY_VERSION_WEBSITE));
		}
	}
}

/**
 * Prints a copyright notice in the command-line help with colored text. A newline is
 * automatically added at the end.
 */
void Main::print_help_copyright(const char *p_notice) {
	OS::get_singleton()->print("\u001b[90m%s\u001b[0m\n", p_notice);
}

void Main::print_help(const char *p_binary) {
	print_header(true);
	print_help_copyright("Free and open source software under the terms of the MIT license.");
	print_help_copyright("(c) 2014-present Godot Engine contributors. (c) 2007-present Juan Linietsky, Ariel Manzur.");
	OS::get_singleton()->print("%s", FoundryCLIHelp::get_top_help_text(p_binary).utf8().get_data());
}

#ifdef TESTS_ENABLED
// The order is the same as in `Main::setup()`, only core and some editor types
// are initialized here. This also combines `Main::setup2()` initialization.
Error Main::test_setup() {
	Thread::make_main_thread();
	set_current_thread_safe_for_nodes(true);

	OS::get_singleton()->initialize();

	CoreGlobals::print_ready = true;

	engine = memnew(Engine);

	register_core_types();
	register_core_driver_types();

	packed_data = memnew(PackedData);

	globals = memnew(ProjectSettings);

	register_core_settings(); // Here globals are present.

	translation_server = memnew(TranslationServer);
	tsman = memnew(TextServerManager);

	if (tsman) {
		Ref<TextServerDummy> ts;
		ts.instantiate();
		tsman->add_interface(ts);
	}

#ifndef PHYSICS_3D_DISABLED
	physics_server_3d_manager = memnew(PhysicsServer3DManager);
#endif // PHYSICS_3D_DISABLED
#ifndef PHYSICS_2D_DISABLED
	physics_server_2d_manager = memnew(PhysicsServer2DManager);
#endif // PHYSICS_2D_DISABLED

#ifndef NAVIGATION_2D_DISABLED
	NavigationServer2DManager::initialize_server_manager();
#endif // NAVIGATION_2D_DISABLED
#ifndef NAVIGATION_3D_DISABLED
	NavigationServer3DManager::initialize_server_manager();
#endif // NAVIGATION_3D_DISABLED

	// From `Main::setup2()`.
	register_early_core_singletons();
	initialize_modules(MODULE_INITIALIZATION_LEVEL_CORE);
	register_core_extensions();

	register_core_singletons();

	/** INITIALIZE SERVERS **/
	register_server_types();
#ifndef XR_DISABLED
	XRServer::set_xr_mode(XRServer::XRMODE_OFF); // Skip in tests.
#endif // XR_DISABLED
	initialize_modules(MODULE_INITIALIZATION_LEVEL_SERVERS);
	FoundryExtensionManager::get_singleton()->initialize_extensions(FoundryExtension::INITIALIZATION_LEVEL_SERVERS);

	translation_server->setup(); //register translations, load them, etc.
	if (!locale.is_empty()) {
		translation_server->set_locale(locale);
	}
	translation_server->load_project_translations(translation_server->get_main_domain());
	ResourceLoader::load_translation_remaps(); //load remaps for resources

	// Initialize ThemeDB early so that scene types can register their theme items.
	// Default theme will be initialized later, after modules and ScriptServer are ready.
	initialize_theme_db();

#ifndef NAVIGATION_3D_DISABLED
	NavigationServer3DManager::initialize_server();
#endif // NAVIGATION_3D_DISABLED
#ifndef NAVIGATION_2D_DISABLED
	NavigationServer2DManager::initialize_server();
#endif // NAVIGATION_2D_DISABLED

	register_scene_types();
	register_driver_types();

	register_scene_singletons();

	initialize_modules(MODULE_INITIALIZATION_LEVEL_SCENE);
	FoundryExtensionManager::get_singleton()->initialize_extensions(FoundryExtension::INITIALIZATION_LEVEL_SCENE);

#ifdef TOOLS_ENABLED
	ClassDB::set_current_api(ClassDB::API_EDITOR);
	register_editor_types();

	initialize_modules(MODULE_INITIALIZATION_LEVEL_EDITOR);
	FoundryExtensionManager::get_singleton()->initialize_extensions(FoundryExtension::INITIALIZATION_LEVEL_EDITOR);

	ClassDB::set_current_api(ClassDB::API_CORE);
#endif
	register_platform_apis();

	// Theme needs modules to be initialized so that sub-resources can be loaded.
	theme_db->initialize_theme_noproject();

	ERR_FAIL_COND_V(TextServerManager::get_singleton()->get_interface_count() == 0, ERR_CANT_CREATE);

	/* Use one with the most features available. */
	int max_features = 0;
	for (int i = 0; i < TextServerManager::get_singleton()->get_interface_count(); i++) {
		uint32_t features = TextServerManager::get_singleton()->get_interface(i)->get_features();
		int feature_number = 0;
		while (features) {
			feature_number += features & 1;
			features >>= 1;
		}
		if (feature_number >= max_features) {
			max_features = feature_number;
			text_driver_idx = i;
		}
	}
	if (text_driver_idx >= 0) {
		Ref<TextServer> ts = TextServerManager::get_singleton()->get_interface(text_driver_idx);
		TextServerManager::get_singleton()->set_primary_interface(ts);
		if (ts->has_feature(TextServer::FEATURE_USE_SUPPORT_DATA)) {
			ts->load_support_data("res://" + ts->get_support_data_filename());
		}
	} else {
		ERR_FAIL_V_MSG(ERR_CANT_CREATE, "TextServer: Unable to create TextServer interface.");
	}

	ClassDB::set_current_api(ClassDB::API_NONE);

	_start_success = true;

	return OK;
}

// The order is the same as in `Main::cleanup()`.
void Main::test_cleanup() {
	ERR_FAIL_COND(!_start_success);

	// Printing in the usual way can become problematic during/after cleanup.
	CoreGlobals::print_ready = false;

	for (int i = 0; i < TextServerManager::get_singleton()->get_interface_count(); i++) {
		TextServerManager::get_singleton()->get_interface(i)->cleanup();
	}

	ResourceLoader::remove_custom_loaders();
	ResourceSaver::remove_custom_savers();
	PropertyListHelper::clear_base_helpers();

#ifdef TOOLS_ENABLED
	FoundryExtensionManager::get_singleton()->deinitialize_extensions(FoundryExtension::INITIALIZATION_LEVEL_EDITOR);
	uninitialize_modules(MODULE_INITIALIZATION_LEVEL_EDITOR);
	unregister_editor_types();
#endif

	FoundryExtensionManager::get_singleton()->deinitialize_extensions(FoundryExtension::INITIALIZATION_LEVEL_SCENE);
	uninitialize_modules(MODULE_INITIALIZATION_LEVEL_SCENE);

	unregister_platform_apis();
	unregister_driver_types();
	unregister_scene_types();

	finalize_theme_db();

#ifndef NAVIGATION_2D_DISABLED
	NavigationServer2DManager::finalize_server();
	NavigationServer2DManager::finalize_server_manager();
#endif // NAVIGATION_2D_DISABLED
#ifndef NAVIGATION_3D_DISABLED
	NavigationServer3DManager::finalize_server();
	NavigationServer3DManager::finalize_server_manager();
#endif // NAVIGATION_3D_DISABLED

	FoundryExtensionManager::get_singleton()->deinitialize_extensions(FoundryExtension::INITIALIZATION_LEVEL_SERVERS);
	uninitialize_modules(MODULE_INITIALIZATION_LEVEL_SERVERS);
	unregister_server_types();

	EngineDebugger::deinitialize();
	OS::get_singleton()->finalize();

	if (packed_data) {
		memdelete(packed_data);
	}
	if (translation_server) {
		memdelete(translation_server);
	}
	if (tsman) {
		memdelete(tsman);
	}
#ifndef PHYSICS_3D_DISABLED
	if (physics_server_3d_manager) {
		memdelete(physics_server_3d_manager);
	}
#endif // PHYSICS_3D_DISABLED
#ifndef PHYSICS_2D_DISABLED
	if (physics_server_2d_manager) {
		memdelete(physics_server_2d_manager);
	}
#endif // PHYSICS_2D_DISABLED
	if (globals) {
		memdelete(globals);
	}

	unregister_core_driver_types();
	unregister_core_extensions();
	uninitialize_modules(MODULE_INITIALIZATION_LEVEL_CORE);

	if (engine) {
		memdelete(engine);
	}

	unregister_core_types();

	OS::get_singleton()->finalize_core();
}
#endif

#if defined(TOOLS_ENABLED)
static void apply_foundry_cli_project_path(const String &p_project_path, String &r_project_path) {
	if (p_project_path.is_empty()) {
		return;
	}
#if defined(OVERRIDE_PATH_ENABLED)
	if (OS::get_singleton()->set_cwd(p_project_path) != OK) {
		OS::get_singleton()->printerr("Invalid project path specified: \"%s\", aborting.\n", p_project_path.utf8().get_data());
		foundry_cli_project_path_error = true;
		return;
	}
	r_project_path = p_project_path;
#else
	ERR_PRINT(
			"`--project` was specified on the command line, but this Foundry binary was compiled without support for path overrides. Aborting.\n"
			"To be able to use it, use the `disable_path_overrides=no` SCons option when compiling Foundry.\n");
	foundry_cli_project_path_error = true;
#endif
}

// Passthrough arguments are appended to the engine argument list rather than
// straight to the game argument list: engine-owned runtime options such as
// `--remote-debug` and `--editor-pid` have to be seen by the runtime option
// parser, and anything it does not recognize still falls through to the game
// arguments from there.
static void apply_foundry_cli_invocation(
		const FoundryCLIParser::ParseResult &p_parse,
		String &r_project_path,
		String &r_audio_driver,
		List<String> &r_engine_args,
		bool &r_test_rd_support,
		bool &r_test_rd_creation) {
	using Kind = FoundryCLIParser::CLIInvocation::Kind;
	const FoundryCLIParser::CLIInvocation &inv = p_parse.invocation;

	apply_foundry_cli_project_path(inv.project_path, r_project_path);

	switch (inv.kind) {
		case Kind::NONE:
			break;
		case Kind::EDITOR_OPEN:
			editor = true;
#if defined(TOOLS_ENABLED)
			EditorAutomationServer::apply_cli_options(inv);
#endif
			for (int i = 0; i < inv.passthrough_args.size(); i++) {
				r_engine_args.push_back(inv.passthrough_args[i]);
			}
			break;
		case Kind::PROJECT_RUN:
		case Kind::PROJECT_TEST:
			for (int i = 0; i < inv.passthrough_args.size(); i++) {
				r_engine_args.push_back(inv.passthrough_args[i]);
			}
			break;
		case Kind::PROJECT_EXPORT:
			editor = true;
			cmdline_tool = true;
			wait_for_import = true;
			break;
		case Kind::PROJECT_IMPORT:
			editor = true;
			cmdline_tool = true;
			wait_for_import = true;
			quit_after = 1;
			break;
		case Kind::SCRIPT_FORMAT:
		case Kind::SCRIPT_LINT:
			cmdline_tool = true;
			r_audio_driver = NULL_AUDIO_DRIVER;
			display_driver = NULL_DISPLAY_DRIVER;
			quit_after = 1;
			break;
		case Kind::SCRIPT_MIGRATE:
			r_project_path = inv.project_path;
			cmdline_tool = true;
			r_audio_driver = NULL_AUDIO_DRIVER;
			display_driver = NULL_DISPLAY_DRIVER;
			quit_after = 1;
			break;
		case Kind::SCRIPT_EVAL:
			// Runs a generated ScriptRunner under a live SceneTree, so unlike the other
			// `script` tools it does not quit_after=1: the runner host quits the tree with
			// the snippet's exit code. cmdline_tool keeps a projectless eval from falling
			// back to the project manager; null audio/display keep it headless by default.
			cmdline_tool = true;
			r_audio_driver = NULL_AUDIO_DRIVER;
			display_driver = NULL_DISPLAY_DRIVER;
			break;
		case Kind::TEST_RUN:
		case Kind::TEST_GENERATE_FIXTURES:
		case Kind::TEST_GENERATE_FORMAT_FIXTURES:
			break;
		case Kind::LSP_SERVE:
		case Kind::TOOLING_SERVE:
			// One combined host owns both tooling listeners. `lsp serve` is a deprecated
			// alias that starts the very same host, so both kinds share this setup.
			editor = true;
			cmdline_tool = true;
			r_audio_driver = NULL_AUDIO_DRIVER;
			display_driver = NULL_DISPLAY_DRIVER;
#if defined(TOOLS_ENABLED)
#if defined(MODULE_FOUNDRY_SCRIPT_ENABLED) && !defined(FOUNDRY_SCRIPT_NO_LSP)
			EditorToolingHost::configure(
					inv.lsp_port.is_empty() ? FoundryCLIParser::DEFAULT_LSP_PORT : inv.lsp_port.to_int(),
					inv.dap_port.is_empty() ? FoundryCLIParser::DEFAULT_DAP_PORT : inv.dap_port.to_int());
#else
			// The host contract advertises both services; a build without the language
			// server cannot honor it, so fail before any listener binds.
			OS::get_singleton()->printerr("%s\n", "The tooling host requires the Foundry Script language server, which this build does not include.");
			OS::get_singleton()->print("FOUNDRY_TOOLING_ERROR %s\n", "{\"error\":\"service_unavailable\",\"service\":\"lsp\"}");
			OS::get_singleton()->set_exit_code(EXIT_FAILURE);
			quit_after = 1;
#endif
#endif
			break;
		case Kind::DOCS_GENERATE_API:
			editor = true;
			cmdline_tool = true;
			dump_extension_api = true;
			include_docs_in_extension_api_dump = inv.docs_include_docs;
			print_line(inv.docs_include_docs ? "Dumping Extension API including documentation" : "Dumping Extension API");
			break;
		case Kind::DOCS_GENERATE_ENGINE:
			cmdline_tool = true;
			r_audio_driver = NULL_AUDIO_DRIVER;
			display_driver = NULL_DISPLAY_DRIVER;
			break;
		case Kind::DOCS_GENERATE_SCRIPT:
			cmdline_tool = true;
			quit_after = 1;
			break;
		case Kind::EXTENSION_DUMP_INTERFACE:
			editor = true;
			cmdline_tool = true;
			if (inv.extension_interface_format == "json") {
				dump_foundry_extension_interface = true;
				print_line("Dumping FoundryExtension interface json file");
			} else {
				dump_foundry_extension_interface_header = true;
				print_line("Dumping FoundryExtension interface header file");
			}
			break;
		case Kind::EXTENSION_VALIDATE_API:
			editor = true;
			cmdline_tool = true;
			validate_extension_api = true;
			validate_extension_api_file = inv.extension_validate_input;
			break;
		case Kind::DIAGNOSTICS_RENDER_DEVICE_SUPPORT:
			r_test_rd_support = true;
			break;
		case Kind::DIAGNOSTICS_RENDER_DEVICE_CREATE:
			r_test_rd_creation = true;
			break;
	}
}
#endif // TOOLS_ENABLED

int Main::test_entrypoint(int argc, char *argv[], bool &tests_need_run) {
	const FoundryCLIParser::ParseResult cli_parse = FoundryCLIParser::parse(argc, argv);
	foundry_cli_parse = cli_parse;
	if (!cli_parse.ok) {
		tests_need_run = false;
		return EXIT_SUCCESS;
	}
	if (cli_parse.help_requested) {
		tests_need_run = false;
		return EXIT_SUCCESS;
	}

	using Kind = FoundryCLIParser::CLIInvocation::Kind;
	const Kind kind = cli_parse.invocation.kind;
	if (kind != Kind::TEST_RUN && kind != Kind::TEST_GENERATE_FIXTURES && kind != Kind::TEST_GENERATE_FORMAT_FIXTURES) {
		tests_need_run = false;
		return EXIT_SUCCESS;
	}

	tests_need_run = true;
#ifdef TESTS_ENABLED
	test_setup();

#ifdef MODULE_FOUNDRY_SCRIPT_ENABLED
	bool project_loaded_for_build_pipeline = false;
	const bool old_foundry_build_trusted = ProjectBuildTrustStore::is_cli_trusted_execution();
	const String &test_project_path = cli_parse.invocation.project_path;
	if (!test_project_path.is_empty()) {
		const Error project_err = ProjectSettings::get_singleton()->setup(test_project_path, String(), false, false);
		if (project_err != OK) {
			ERR_PRINT(vformat("Could not load project at path \"%s\" before running tests.", test_project_path));
			test_cleanup();
			return EXIT_FAILURE;
		}
		project_loaded_for_build_pipeline = true;

		ProjectBuildTrustStore::set_cli_trusted_execution(cli_parse.trusted);
		const bool pre_compile_ok = run_foundry_build_stage_for_cli(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE,
				"Foundry pre_compile test stage");
		ProjectBuildTrustStore::set_cli_trusted_execution(old_foundry_build_trusted);
		if (!pre_compile_ok) {
			test_cleanup();
			return EXIT_FAILURE;
		}
		ResourceLoader::add_custom_loaders();
		ResourceSaver::add_custom_savers();
	}
#endif // MODULE_FOUNDRY_SCRIPT_ENABLED

	int status = EXIT_SUCCESS;
	if (kind == Kind::TEST_RUN) {
		Vector<CharString> test_arg_storage;
		Vector<char *> test_argv;
		auto push_test_arg = [&](const String &p_arg) {
			test_arg_storage.push_back(p_arg.utf8());
			test_argv.push_back(const_cast<char *>(test_arg_storage[test_arg_storage.size() - 1].get_data()));
		};
		for (int i = 0; i < cli_parse.global_args.size(); i++) {
			push_test_arg(cli_parse.global_args[i]);
		}
		push_test_arg("--test");
		if (!cli_parse.invocation.test_cases.is_empty()) {
			push_test_arg("--test-case=" + FoundryCLIParser::build_doctest_case_filter(cli_parse.invocation.test_cases));
		}
		for (int i = 0; i < cli_parse.invocation.passthrough_args.size(); i++) {
			push_test_arg(cli_parse.invocation.passthrough_args[i]);
		}
		FoundryTestProgress::configure_from_invocation(cli_parse.invocation);
		status = test_main(test_argv.size(), test_argv.ptrw());
#ifdef MODULE_FOUNDRY_SCRIPT_ENABLED
	} else if (kind == Kind::TEST_GENERATE_FIXTURES) {
#ifdef TOOLS_ENABLED
		const String path = cli_parse.invocation.command_args.is_empty()
				? String("modules/foundry_script/tests/scripts")
				: cli_parse.invocation.command_args[0];
		FSTests::FSTestRunner runner(path, true, cli_parse.invocation.print_filenames);
		if (!runner.generate_outputs()) {
			status = EXIT_FAILURE;
		}
#else
		ERR_PRINT("foundry test generate-fixtures requires an editor build.");
		status = EXIT_FAILURE;
#endif // TOOLS_ENABLED
	} else if (kind == Kind::TEST_GENERATE_FORMAT_FIXTURES) {
#ifdef TOOLS_ENABLED
		const String path = cli_parse.invocation.command_args.is_empty()
				? String("modules/foundry_script/tests/scripts/format")
				: cli_parse.invocation.command_args[0];
		FSFormatterCLI::generate_format_tests(path);
		status = OS::get_singleton()->get_exit_code();
#else
		ERR_PRINT("foundry test generate-format-fixtures requires an editor build.");
		status = EXIT_FAILURE;
#endif // TOOLS_ENABLED
	}
#endif // MODULE_FOUNDRY_SCRIPT_ENABLED

#ifdef MODULE_FOUNDRY_SCRIPT_ENABLED
	if (status == EXIT_SUCCESS && project_loaded_for_build_pipeline) {
		ProjectBuildTrustStore::set_cli_trusted_execution(cli_parse.trusted);
		const bool post_compile_ok = run_foundry_build_stage_for_cli(ProjectBuildPipelineConfig::STAGE_POST_COMPILE,
				"Foundry post_compile test stage");
		ProjectBuildTrustStore::set_cli_trusted_execution(old_foundry_build_trusted);
		if (!post_compile_ok) {
			status = EXIT_FAILURE;
		}
	}
#endif // MODULE_FOUNDRY_SCRIPT_ENABLED
	test_cleanup();
	return status;
#else
	ERR_PRINT(
			"A test command was specified on the command line, but this Foundry binary was compiled without support for unit tests. Aborting.\n"
			"To be able to run unit tests, use the `tests=yes` SCons option when compiling Foundry.\n");
	return EXIT_FAILURE;
#endif
}

/* Engine initialization
 *
 * Consists of several methods that are called by each platform's specific main(argc, argv).
 * To fully understand engine init, one should therefore start from the platform's main and
 * see how it calls into the Main class' methods.
 *
 * The initialization is typically done in 3 steps (with the setup2 step triggered either
 * automatically by setup, or manually in the platform's main).
 *
 * - setup(execpath, argc, argv, p_second_phase) is the main entry point for all platforms,
 *   responsible for the initialization of all low level singletons and core types, and parsing
 *   command line arguments to configure things accordingly.
 *   If p_second_phase is true, it will chain into setup2() (default behavior). This is
 *   disabled on some platforms (Android, iOS) which trigger the second step in their own time.
 *
 * - setup2(p_main_tid_override) registers high level servers and singletons, displays the
 *   boot splash, then registers higher level types (scene, editor, etc.).
 *
 * - start() is the last step and that's where command line tools can run, or the main loop
 *   can be created eventually and the project settings put into action. That's also where
 *   the editor node is created, if relevant.
 *   start() does it own argument parsing for a subset of the command line arguments described
 *   in help, it's a bit messy and should be globalized with the setup() parsing somehow.
 */

Error Main::setup(const char *execpath, int argc, char *argv[], bool p_second_phase) {
	FoundryProfileZone("setup");
	Thread::make_main_thread();
	set_current_thread_safe_for_nodes(true);

	OS::get_singleton()->initialize();

	CoreGlobals::print_ready = true;

#if !defined(OVERRIDE_PATH_ENABLED) && !defined(TOOLS_ENABLED)
	String old_cwd = OS::get_singleton()->get_cwd();
#if defined(MACOS_ENABLED) || defined(APPLE_EMBEDDED_ENABLED)
	String new_cwd = OS::get_singleton()->get_bundle_resource_dir();
	if (new_cwd.is_empty() || !new_cwd.is_absolute_path()) {
		new_cwd = OS::get_singleton()->get_executable_path().get_base_dir();
	}
#else
		String new_cwd = OS::get_singleton()->get_executable_path().get_base_dir();
#endif
	if (!new_cwd.is_empty()) {
		OS::get_singleton()->set_cwd(new_cwd);
	}
#endif

	// Benchmark tracking must be done after `OS::get_singleton()->initialize()` as on some
	// platforms, it's used to set up the time utilities.
	OS::get_singleton()->benchmark_begin_measure("Startup", "Main::Setup");

	engine = memnew(Engine);

	MAIN_PRINT("Main: Initialize CORE");

	register_core_types();
	register_core_driver_types();

	MAIN_PRINT("Main: Initialize Globals");

	input_map = memnew(InputMap);
	globals = memnew(ProjectSettings);

	register_core_settings(); //here globals are present

	translation_server = memnew(TranslationServer);
	performance = memnew(Performance);
	FOUNDRY_REGISTER_CLASS(Performance);
	engine->add_singleton(Engine::Singleton("Performance", performance));

	// Only flush stdout in debug builds by default, as spamming `print()` will
	// decrease performance if this is enabled.
	GLOBAL_DEF_RST("application/run/flush_stdout_on_print", false);
	GLOBAL_DEF_RST("application/run/flush_stdout_on_print.debug", true);

	MAIN_PRINT("Main: Parse CMDLine");

	/* argument parsing and main creation */
	List<String> args;
	List<String> main_args;
	List<String> user_args;
	bool adding_user_args = false;
	bool foundry_script_cli_tool_args = false;
	List<String> platform_args = OS::get_singleton()->get_cmdline_platform_args();
	PackedStringArray raw_cli_args;

	// Add command line arguments.
	for (int i = 0; i < argc; i++) {
		raw_cli_args.push_back(String::utf8(argv[i]));
	}

	String audio_driver = "";
	String project_path = ".";
	String debug_uri = "";
#ifdef TOOLS_ENABLED
	bool test_rd_creation = false;
	bool test_rd_support = false;
#endif
	bool skip_breakpoints = false;
	bool ignore_error_breaks = false;
	String main_pack;
	bool quiet_stdout = false;
	int separate_thread_render = -1; // Tri-state: -1 = not set, 0 = false, 1 = true.

	String remotefs;
	String remotefs_pass;

	Vector<String> breakpoints;
	bool delta_smoothing_override = false;
	bool load_shell_env = false;

	String default_renderer = "";
	String default_renderer_mobile = "";
	String renderer_hints = "";

	packed_data = PackedData::get_singleton();
	if (!packed_data) {
		packed_data = memnew(PackedData);
	}

#ifdef MINIZIP_ENABLED

	//XXX: always get_singleton() == 0x0
	zip_packed_data = ZipArchive::get_singleton();
	//TODO: remove this temporary fix
	if (!zip_packed_data) {
		zip_packed_data = memnew(ZipArchive);
	}

	packed_data->add_pack_source(zip_packed_data);
#endif

	// Exit error code used in the `goto error` conditions.
	// It's returned as the program exit code. ERR_HELP is special cased and handled as success (0).
	Error exit_err = ERR_INVALID_PARAMETER;
	List<String>::Element *I = nullptr;

	const FoundryCLIParser::ParseResult cli_parse = FoundryCLIParser::parse(raw_cli_args);
	foundry_cli_parse = cli_parse;
	if (!cli_parse.ok) {
		OS::get_singleton()->printerr("Foundry CLI error: %s\n", cli_parse.error.utf8().get_data());
		if (!cli_parse.command_path.is_empty() && FoundryCLIHelp::has_noun(cli_parse.command_path[0])) {
			bool scope_valid = false;
			const String scoped_help = FoundryCLIHelp::get_scoped_help_text(execpath, cli_parse.command_path, scope_valid);
			OS::get_singleton()->printerr("%s", scoped_help.utf8().get_data());
		}
		goto error;
	}
	if (cli_parse.version_requested) {
		if (cli_parse.json) {
			OS::get_singleton()->print("%s\n", FoundryVersionInfo::get_json().utf8().get_data());
		} else {
			print_line(get_full_version_string());
		}
		exit_err = ERR_HELP;
		goto error;
	}
	if (cli_parse.help_requested) {
		if (cli_parse.json) {
			OS::get_singleton()->print("%s\n", FoundryCLIHelp::get_help_json(cli_parse.command_path).utf8().get_data());
			exit_err = ERR_HELP;
			goto error;
		}
		bool scope_valid = false;
		const String scoped_help = FoundryCLIHelp::get_scoped_help_text(execpath, cli_parse.command_path, scope_valid);
		if (!scope_valid) {
			OS::get_singleton()->printerr("Unknown command for help: %s\n", String(" ").join(cli_parse.command_path).utf8().get_data());
			OS::get_singleton()->printerr("%s", scoped_help.utf8().get_data());
			goto error;
		}
		if (cli_parse.command_path.is_empty()) {
			if (cli_parse.no_header) {
				OS::get_singleton()->print("%s", FoundryCLIHelp::get_top_help_text(execpath).utf8().get_data());
			} else {
				print_help(execpath);
			}
		} else {
			if (!cli_parse.no_header) {
				print_header(true);
			}
			OS::get_singleton()->print("%s", scoped_help.utf8().get_data());
		}
		exit_err = ERR_HELP;
		goto error;
	}

	for (int i = 0; i < cli_parse.global_args.size(); i++) {
		args.push_back(cli_parse.global_args[i]);
	}
	for (int i = 0; i < cli_parse.user_args.size(); i++) {
		user_args.push_back(cli_parse.user_args[i]);
	}

#ifdef TOOLS_ENABLED
	apply_foundry_cli_invocation(cli_parse, project_path, audio_driver, args, test_rd_support, test_rd_creation);
	if (cli_parse.invocation.kind == FoundryCLIParser::CLIInvocation::SCRIPT_FORMAT ||
			cli_parse.invocation.kind == FoundryCLIParser::CLIInvocation::SCRIPT_LINT) {
		Engine::get_singleton()->_print_header = false;
	}
#endif

	// Add arguments received from macOS LaunchService (URL schemas, file associations).
	for (const String &arg : platform_args) {
		args.push_back(arg);
	}

	I = args.front();

	while (I) {
		I->get() = unescape_cmdline(I->get().strip_edges());
		I = I->next();
	}

	I = args.front();
	while (I) {
		List<String>::Element *N = I->next();

		const String &arg = I->get();

#ifdef MACOS_ENABLED
		// Ignore the process serial number argument passed by macOS Gatekeeper.
		// Otherwise, Godot would try to open a non-existent project on the first start and abort.
		if (arg.begins_with("-psn_")) {
			I = N;
			continue;
		}
#endif

#ifdef TOOLS_ENABLED
		if (arg == "--debug" ||
				arg == "--verbose" ||
				arg == "--disable-crash-handler") {
			forwardable_cli_arguments[CLI_SCOPE_TOOL].push_back(arg);
			forwardable_cli_arguments[CLI_SCOPE_PROJECT].push_back(arg);
		}
		if (arg == "--single-window" || arg == "--editor-pseudolocalization") {
			forwardable_cli_arguments[CLI_SCOPE_TOOL].push_back(arg);
		}
#ifdef MODULE_FOUNDRY_SCRIPT_ENABLED
		if (arg == "--foundry-build-trusted") {
			forwardable_cli_arguments[CLI_SCOPE_TOOL].push_back(arg);
			forwardable_cli_arguments[CLI_SCOPE_PROJECT].push_back(arg);
		}
#endif // MODULE_FOUNDRY_SCRIPT_ENABLED
		if (arg == "--audio-driver" ||
				arg == "--display-driver" ||
				arg == "--rendering-method" ||
				arg == "--rendering-driver" ||
				arg == "--xr-mode" ||
				arg == "-l" ||
				arg == "--language") {
			if (N) {
				forwardable_cli_arguments[CLI_SCOPE_TOOL].push_back(arg);
				forwardable_cli_arguments[CLI_SCOPE_TOOL].push_back(N->get());
			}
		}
		// If gpu is specified, both editor and debug instances started from editor will inherit.
		if (arg == "--gpu-index") {
			if (N) {
				const String &next_arg = N->get();
				forwardable_cli_arguments[CLI_SCOPE_TOOL].push_back(arg);
				forwardable_cli_arguments[CLI_SCOPE_TOOL].push_back(next_arg);
				forwardable_cli_arguments[CLI_SCOPE_PROJECT].push_back(arg);
				forwardable_cli_arguments[CLI_SCOPE_PROJECT].push_back(next_arg);
			}
		}
#endif

		if (adding_user_args) {
			user_args.push_back(arg);
		} else if (foundry_script_cli_tool_args) {
			if (arg == "--" || arg == "++") {
				adding_user_args = true;
			} else if (arg == "--path") {
#if defined(OVERRIDE_PATH_ENABLED)
				if (N) {
					String p = N->get();
					if (OS::get_singleton()->set_cwd(p) != OK) {
						OS::get_singleton()->print("Invalid project path specified: \"%s\", aborting.\n", p.utf8().get_data());
						goto error;
					}
					N = N->next();
				} else {
					OS::get_singleton()->print("Missing relative or absolute path, aborting.\n");
					goto error;
				}
#else
				ERR_PRINT(
						"`--path` was specified on the command line, but this Foundry binary was compiled without support for path overrides. Aborting.\n"
						"To be able to use it, use the `disable_path_overrides=no` SCons option when compiling Foundry.\n");
				goto error;
#endif // defined(OVERRIDE_PATH_ENABLED)
			} else {
				main_args.push_back(arg);
			}
		} else if (arg == "-h" || arg == "--help" || arg == "/?") { // display help

			show_help = true;
			exit_err = ERR_HELP; // Hack to force an early exit in `main()` with a success code.
			goto error;

		} else if (arg == "-v" || arg == "--verbose") { // verbose output

			OS::get_singleton()->_verbose_stdout = true;
		} else if (arg == "-q" || arg == "--quiet") { // quieter output

			quiet_stdout = true;

		} else if (arg == "--no-header") {
			Engine::get_singleton()->_print_header = false;

#ifdef MODULE_FOUNDRY_SCRIPT_ENABLED
		} else if (arg == "--foundry-build-trusted") {
			ProjectBuildTrustStore::set_cli_trusted_execution(true);
#endif // MODULE_FOUNDRY_SCRIPT_ENABLED

		} else if (arg == "--audio-driver") { // audio driver

			if (N) {
				audio_driver = N->get();

				bool found = false;
				for (int i = 0; i < AudioDriverManager::get_driver_count(); i++) {
					if (audio_driver == AudioDriverManager::get_driver(i)->get_name()) {
						found = true;
					}
				}

				if (!found) {
					OS::get_singleton()->print("Unknown audio driver '%s', aborting.\nValid options are ",
							audio_driver.utf8().get_data());

					for (int i = 0; i < AudioDriverManager::get_driver_count(); i++) {
						if (i == AudioDriverManager::get_driver_count() - 1) {
							OS::get_singleton()->print(" and ");
						} else if (i != 0) {
							OS::get_singleton()->print(", ");
						}

						OS::get_singleton()->print("'%s'", AudioDriverManager::get_driver(i)->get_name());
					}

					OS::get_singleton()->print(".\n");

					goto error;
				}

				N = N->next();
			} else {
				OS::get_singleton()->print("Missing audio driver argument, aborting.\n");
				goto error;
			}
		} else if (arg == "--audio-output-latency") {
			if (N) {
				audio_output_latency = N->get().to_int();
				N = N->next();
			} else {
				OS::get_singleton()->print("Missing audio output latency argument, aborting.\n");
				goto error;
			}
		} else if (arg == "--text-driver") {
			if (N) {
				text_driver = N->get();
				N = N->next();
			} else {
				OS::get_singleton()->print("Missing text driver argument, aborting.\n");
				goto error;
			}

		} else if (arg == "--display-driver") { // force video driver

			if (N) {
				display_driver = N->get();

				bool found = false;
				for (int i = 0; i < DisplayServer::get_create_function_count(); i++) {
					if (display_driver == DisplayServer::get_create_function_name(i)) {
						found = true;
					}
				}

				if (!found) {
					OS::get_singleton()->print("Unknown display driver '%s', aborting.\nValid options are ",
							display_driver.utf8().get_data());

					for (int i = 0; i < DisplayServer::get_create_function_count(); i++) {
						if (i == DisplayServer::get_create_function_count() - 1) {
							OS::get_singleton()->print(" and ");
						} else if (i != 0) {
							OS::get_singleton()->print(", ");
						}

						OS::get_singleton()->print("'%s'", DisplayServer::get_create_function_name(i));
					}

					OS::get_singleton()->print(".\n");

					goto error;
				}

				N = N->next();
			} else {
				OS::get_singleton()->print("Missing display driver argument, aborting.\n");
				goto error;
			}
		} else if (arg == "--rendering-method") {
			if (N) {
				rendering_method = N->get();
				N = N->next();
			} else {
				OS::get_singleton()->print("Missing renderer name argument, aborting.\n");
				goto error;
			}
		} else if (arg == "--rendering-driver") {
			if (N) {
				rendering_driver = N->get();
				N = N->next();
			} else {
				OS::get_singleton()->print("Missing rendering driver argument, aborting.\n");
				goto error;
			}
		} else if (arg == "-f" || arg == "--fullscreen") { // force fullscreen
			init_fullscreen = true;
			window_mode = DisplayServer::WINDOW_MODE_FULLSCREEN;
		} else if (arg == "-m" || arg == "--maximized") { // force maximized window
			init_maximized = true;
			window_mode = DisplayServer::WINDOW_MODE_MAXIMIZED;
		} else if (arg == "-w" || arg == "--windowed") { // force windowed window

			init_windowed = true;
		} else if (arg == "--gpu-index") {
			if (N) {
				Engine::singleton->gpu_idx = N->get().to_int();
				N = N->next();
			} else {
				OS::get_singleton()->print("Missing GPU index argument, aborting.\n");
				goto error;
			}
		} else if (arg == "--gpu-validation") {
			Engine::singleton->use_validation_layers = true;
#ifdef DEBUG_ENABLED
		} else if (arg == "--gpu-abort") {
			Engine::singleton->abort_on_gpu_errors = true;
#endif
		} else if (arg == "--generate-spirv-debug-info") {
			Engine::singleton->generate_spirv_debug_info = true;
#if defined(DEBUG_ENABLED) || defined(DEV_ENABLED)
		} else if (arg == "--extra-gpu-memory-tracking") {
			Engine::singleton->extra_gpu_memory_tracking = true;
		} else if (arg == "--accurate-breadcrumbs") {
			Engine::singleton->accurate_breadcrumbs = true;
#endif
		} else if (arg == "--tablet-driver") {
			if (N) {
				tablet_driver = N->get();
				N = N->next();
			} else {
				OS::get_singleton()->print("Missing tablet driver argument, aborting.\n");
				goto error;
			}
		} else if (arg == "--delta-smoothing") {
			if (N) {
				String string = N->get();
				bool recognized = false;
				if (string == "enable") {
					OS::get_singleton()->set_delta_smoothing(true);
					delta_smoothing_override = true;
					recognized = true;
				}
				if (string == "disable") {
					OS::get_singleton()->set_delta_smoothing(false);
					delta_smoothing_override = false;
					recognized = true;
				}
				if (!recognized) {
					OS::get_singleton()->print("Delta-smoothing argument not recognized, aborting.\n");
					goto error;
				}
				N = N->next();
			} else {
				OS::get_singleton()->print("Missing delta-smoothing argument, aborting.\n");
				goto error;
			}
		} else if (arg == "--single-window") { // force single window

			single_window = true;
		} else if (arg == "--accessibility") {
			if (N) {
				String string = N->get();
				if (string == "auto") {
					accessibility_mode = AccessibilityServerEnums::AccessibilityMode::ACCESSIBILITY_AUTO;
					accessibility_mode_set = true;
				} else if (string == "always") {
					accessibility_mode = AccessibilityServerEnums::AccessibilityMode::ACCESSIBILITY_ALWAYS;
					accessibility_mode_set = true;
				} else if (string == "disabled") {
					accessibility_mode = AccessibilityServerEnums::AccessibilityMode::ACCESSIBILITY_DISABLED;
					accessibility_mode_set = true;
				} else {
					OS::get_singleton()->print("Accessibility mode argument not recognized, aborting.\n");
					goto error;
				}
				N = N->next();
			} else {
				OS::get_singleton()->print("Missing accessibility mode argument, aborting.\n");
				goto error;
			}
		} else if (arg == "--accessibility-driver") {
			if (N) {
				String string = N->get();
				accessibility_driver_name = string;
				N = N->next();
			} else {
				OS::get_singleton()->print("Missing accessibility driver argument, aborting.\n");
				goto error;
			}
		} else if (arg == "-t" || arg == "--always-on-top") { // force always-on-top window

			init_always_on_top = true;
		} else if (arg == "--resolution") { // force resolution

			if (N) {
				String vm = N->get();

				if (!vm.contains_char('x')) { // invalid parameter format

					OS::get_singleton()->print("Invalid resolution '%s', it should be e.g. '1280x720'.\n",
							vm.utf8().get_data());
					goto error;
				}

				int w = vm.get_slicec('x', 0).to_int();
				int h = vm.get_slicec('x', 1).to_int();

				if (w <= 0 || h <= 0) {
					OS::get_singleton()->print("Invalid resolution '%s', width and height must be above 0.\n",
							vm.utf8().get_data());
					goto error;
				}

				window_size.width = w;
				window_size.height = h;
				force_res = true;

				N = N->next();
			} else {
				OS::get_singleton()->print("Missing resolution argument, aborting.\n");
				goto error;
			}

		} else if (arg == "--screen") { // set window screen

			if (N) {
				init_screen = N->get().to_int();
				init_use_custom_screen = true;

				N = N->next();
			} else {
				OS::get_singleton()->print("Missing screen argument, aborting.\n");
				goto error;
			}

		} else if (arg == "--position") { // set window position

			if (N) {
				String vm = N->get();

				if (!vm.contains_char(',')) { // invalid parameter format

					OS::get_singleton()->print("Invalid position '%s', it should be e.g. '80,128'.\n",
							vm.utf8().get_data());
					goto error;
				}

				int x = vm.get_slicec(',', 0).to_int();
				int y = vm.get_slicec(',', 1).to_int();

				init_custom_pos = Point2(x, y);
				init_use_custom_pos = true;

				N = N->next();
			} else {
				OS::get_singleton()->print("Missing position argument, aborting.\n");
				goto error;
			}

		} else if (arg == "--headless") { // enable headless mode (no audio, no rendering).

			audio_driver = NULL_AUDIO_DRIVER;
			display_driver = NULL_DISPLAY_DRIVER;

		} else if (arg == "--embedded") { // Enable embedded mode.
#ifdef MACOS_ENABLED
			display_driver = EMBEDDED_DISPLAY_DRIVER;
#else
			OS::get_singleton()->print("--embedded is only supported on macOS, aborting.\n");
			goto error;
#endif
		} else if (arg == "--log-file") { // write to log file

			if (N) {
				log_file = N->get();
				N = N->next();
			} else {
				OS::get_singleton()->print("Missing log file path argument, aborting.\n");
				goto error;
			}
		} else if (arg == "--profiling") { // enable profiling

			use_debug_profiler = true;

		} else if (arg == "-l" || arg == "--language") { // language

			if (N) {
				locale = N->get();
				N = N->next();
			} else {
				OS::get_singleton()->print("Missing language argument, aborting.\n");
				goto error;
			}
		} else if (arg == "--remote-fs") { // remote filesystem

#if defined(DEBUG_ENABLED) || defined(TOOLS_ENABLED)
			if (N) {
				remotefs = N->get();
				N = N->next();
			} else {
				OS::get_singleton()->print("Missing remote filesystem address, aborting.\n");
				goto error;
			}
#else
			ERR_PRINT(
					"`--remote-fs` was specified on the command line, but this Foundry binary was compiled without debug. Aborting.\n"
					"To be able to use it, use the `target=template_debug` SCons option when compiling Foundry.\n");
#endif // defined(DEBUG_ENABLED) || defined (TOOLS_ENABLED)
		} else if (arg == "--remote-fs-password") { // remote filesystem password

#if defined(DEBUG_ENABLED) || defined(TOOLS_ENABLED)
			if (N) {
				remotefs_pass = N->get();
				N = N->next();
			} else {
				OS::get_singleton()->print("Missing remote filesystem password, aborting.\n");
				goto error;
			}
#else
			ERR_PRINT(
					"`--remote-fs-password` was specified on the command line, but this Foundry binary was compiled without debug. Aborting.\n"
					"To be able to use it, use the `target=template_debug` SCons option when compiling Foundry.\n");
			goto error;
#endif // defined(DEBUG_ENABLED) || defined (TOOLS_ENABLED)
		} else if (arg == "--render-thread") { // render thread mode

			if (N) {
				if (N->get() == "safe") {
					separate_thread_render = 0;
				} else if (N->get() == "separate") {
					separate_thread_render = 1;
				} else {
					OS::get_singleton()->print("Unknown render thread mode, aborting.\n");
					OS::get_singleton()->print("Valid options are 'safe' and 'separate'.\n");
					goto error;
				}

				N = N->next();
			} else {
				OS::get_singleton()->print("Missing render thread mode argument, aborting.\n");
				goto error;
			}
#ifdef TOOLS_ENABLED
		} else if (arg == "-e" || arg == "--editor") { // starts editor

			editor = true;
		} else if (arg == "--recovery-mode") { // Enables recovery mode.
			recovery_mode = true;
		} else if (arg == "--debug-server") {
			if (N) {
				debug_server_uri = N->get();
				if (!debug_server_uri.contains("://")) { // wrong address
					OS::get_singleton()->print("Invalid debug server uri. It should be of the form <protocol>://<bind_address>:<port>.\n");
					goto error;
				}
				N = N->next();
			} else {
				OS::get_singleton()->print("Missing remote debug server uri, aborting.\n");
				goto error;
			}
		} else if (arg == "--single-threaded-scene") {
			single_threaded_scene = true;
		} else if (arg == "--build-solutions") { // Build the scripting solution such C#

			auto_build_solutions = true;
			editor = true;
			cmdline_tool = true;
		} else if (arg == "--dump-foundryextension-interface") {
			// Register as an editor instance to use low-end fallback if relevant.
			editor = true;
			cmdline_tool = true;
			dump_foundry_extension_interface_header = true;
			print_line("Dumping FoundryExtension interface header file");
			// Hack. Not needed but otherwise we end up detecting that this should
			// run the project instead of a cmdline tool.
			// Needs full refactoring to fix properly.
			main_args.push_back(arg);
		} else if (arg == "--dump-foundryextension-interface-json") {
			// Register as an editor instance to use low-end fallback if relevant.
			editor = true;
			cmdline_tool = true;
			dump_foundry_extension_interface = true;
			print_line("Dumping FoundryExtension interface json file");
			// Hack. Not needed but otherwise we end up detecting that this should
			// run the project instead of a cmdline tool.
			// Needs full refactoring to fix properly.
			main_args.push_back(arg);
		} else if (arg == "--dump-extension-api") {
			// Register as an editor instance to use low-end fallback if relevant.
			editor = true;
			cmdline_tool = true;
			dump_extension_api = true;
			print_line("Dumping Extension API");
			// Hack. Not needed but otherwise we end up detecting that this should
			// run the project instead of a cmdline tool.
			// Needs full refactoring to fix properly.
			main_args.push_back(arg);
		} else if (arg == "--dump-extension-api-with-docs") {
			// Register as an editor instance to use low-end fallback if relevant.
			editor = true;
			cmdline_tool = true;
			dump_extension_api = true;
			include_docs_in_extension_api_dump = true;
			print_line("Dumping Extension API including documentation");
			// Hack. Not needed but otherwise we end up detecting that this should
			// run the project instead of a cmdline tool.
			// Needs full refactoring to fix properly.
			main_args.push_back(arg);
		} else if (arg == "--validate-extension-api") {
			// Register as an editor instance to use low-end fallback if relevant.
			editor = true;
			cmdline_tool = true;
			validate_extension_api = true;
			// Hack. Not needed but otherwise we end up detecting that this should
			// run the project instead of a cmdline tool.
			// Needs full refactoring to fix properly.
			main_args.push_back(arg);

			if (N) {
				validate_extension_api_file = N->get();

				N = N->next();
			} else {
				OS::get_singleton()->print("Missing file to load argument after --validate-extension-api, aborting.");
				goto error;
			}
		} else if (arg == "--import") {
			editor = true;
			cmdline_tool = true;
			wait_for_import = true;
			quit_after = 1;
		} else if (arg == "--export-release" || arg == "--export-debug" ||
				arg == "--export-pack" || arg == "--export-patch") { // Export project
			// Actually handling is done in start().
			editor = true;
			cmdline_tool = true;
			wait_for_import = true;
			main_args.push_back(arg);
		} else if (arg == "--patches") {
			if (N) {
				// Actually handling is done in start().
				main_args.push_back(arg);
				main_args.push_back(N->get());

				N = N->next();
			} else {
				OS::get_singleton()->print("Missing comma-separated list of patches after --patches, aborting.\n");
				goto error;
			}
		} else if (arg == "--doctool") {
			// Actually handling is done in start().
			cmdline_tool = true;

			// `--doctool` implies `--headless` to avoid spawning an unnecessary window
			// and speed up class reference generation.
			audio_driver = NULL_AUDIO_DRIVER;
			display_driver = NULL_DISPLAY_DRIVER;
			main_args.push_back(arg);
#ifdef MODULE_FOUNDRY_SCRIPT_ENABLED
		} else if (arg == "--foundry_script-docs") {
			if (N) {
				project_path = N->get();
				// Will be handled in start()
				main_args.push_back(arg);
				main_args.push_back(N->get());
				N = N->next();
				// FoundryScript docgen requires Autoloads, but loading those also creates a main loop.
				// This forces main loop to quit without adding more FoundryScript-specific exceptions to setup.
				quit_after = 1;
			} else {
				OS::get_singleton()->print("Missing relative or absolute path to project for --foundry_script-docs, aborting.\n");
				goto error;
			}
		} else if (arg == "--foundry_script-format" || arg == "--foundry_script-lint") {
			cmdline_tool = true;
			audio_driver = NULL_AUDIO_DRIVER;
			display_driver = NULL_DISPLAY_DRIVER;
			Engine::get_singleton()->_print_header = false;
			main_args.push_back(arg);
			foundry_script_cli_tool_args = true;
			quit_after = 1;
		} else if (arg == "--foundry_script-migrate") {
			// Headless strict-typing migration wizard: runs the dry-run report and, when
			// asked, the atomic apply and gated strict activation, then exits. Will be
			// handled in start(); the modifier flags are read there from cmdline_args.
			if (N) {
				project_path = N->get();
				// Run as a command-line tool: this skips the editor/game-launch
				// branches so start() reaches the migration handler, and forces headless
				// since the wizard prints to the console and needs no window.
				cmdline_tool = true;
				audio_driver = NULL_AUDIO_DRIVER;
				display_driver = NULL_DISPLAY_DRIVER;
				main_args.push_back(arg);
				main_args.push_back(N->get());
				N = N->next();
				// The wizard finishes its work in start() and exits; like --foundry_script-docs it
				// does not need a running main loop.
				quit_after = 1;
			} else {
				OS::get_singleton()->print("Missing relative or absolute path to project for --foundry_script-migrate, aborting.\n");
				goto error;
			}
		} else if (arg == "--foundry_script-migrate-follow-up") {
			// Validate the required path argument here (where end-of-command-line is detectable);
			// the value itself is read in start(). Consume the path so it is not reparsed as a
			// standalone option.
			if (N) {
				main_args.push_back(arg);
				main_args.push_back(N->get());
				N = N->next();
			} else {
				OS::get_singleton()->print("Missing file path argument for --foundry_script-migrate-follow-up, aborting.\n");
				goto error;
			}
#endif // MODULE_FOUNDRY_SCRIPT_ENABLED
#endif // TOOLS_ENABLED

		} else if (arg == "--path") { // set path of project to start or edit
#if defined(OVERRIDE_PATH_ENABLED)
			if (N) {
				String p = N->get();
				if (OS::get_singleton()->set_cwd(p) != OK) {
					OS::get_singleton()->print("Invalid project path specified: \"%s\", aborting.\n", p.utf8().get_data());
					goto error;
				}
				N = N->next();
			} else {
				OS::get_singleton()->print("Missing relative or absolute path, aborting.\n");
				goto error;
			}
#else
			ERR_PRINT(
					"`--path` was specified on the command line, but this Foundry binary was compiled without support for path overrides. Aborting.\n"
					"To be able to use it, use the `disable_path_overrides=no` SCons option when compiling Foundry.\n");
			goto error;
#endif // defined(OVERRIDE_PATH_ENABLED)
		} else if (arg == "--quit") { // Auto quit at the end of the first main loop iteration
			quit_after = 1;
#ifdef TOOLS_ENABLED
			wait_for_import = true;
#endif
		} else if (arg == "--quit-after") { // Quit after the given number of iterations
			if (N) {
				quit_after = N->get().to_int();
				N = N->next();
			} else {
				OS::get_singleton()->print("Missing number of iterations, aborting.\n");
				goto error;
			}
		} else if (arg.ends_with("project.foundry")) {
#if defined(OVERRIDE_PATH_ENABLED)
			String path;
			String file = arg;
			int sep = MAX(file.rfind_char('/'), file.rfind_char('\\'));
			if (sep == -1) {
				path = ".";
			} else {
				path = file.substr(0, sep);
			}
			if (OS::get_singleton()->set_cwd(path) == OK) {
				// path already specified, don't override
			} else {
				project_path = path;
			}
#ifdef TOOLS_ENABLED
			editor = true;
#endif
#else
			ERR_PRINT(
					"`project.foundry` path was specified on the command line, but this Foundry binary was compiled without support for path overrides. Aborting.\n"
					"To be able to use it, use the `disable_path_overrides=no` SCons option when compiling Foundry.\n");
			goto error;
#endif // defined(OVERRIDE_PATH_ENABLED)
		} else if (arg == "-b" || arg == "--breakpoints") { // add breakpoints

			if (N) {
				String bplist = N->get();
				breakpoints = bplist.split(",");
				N = N->next();
			} else {
				OS::get_singleton()->print("Missing list of breakpoints, aborting.\n");
				goto error;
			}

		} else if (arg == "--max-fps") { // set maximum rendered FPS

			if (N) {
				max_fps = N->get().to_int();
				N = N->next();
			} else {
				OS::get_singleton()->print("Missing maximum FPS argument, aborting.\n");
				goto error;
			}

		} else if (arg == "--frame-delay") { // force frame delay

			if (N) {
				frame_delay = N->get().to_int();
				N = N->next();
			} else {
				OS::get_singleton()->print("Missing frame delay argument, aborting.\n");
				goto error;
			}

		} else if (arg == "--time-scale") { // force time scale

			if (N) {
				Engine::get_singleton()->set_time_scale(N->get().to_float());
				N = N->next();
			} else {
				OS::get_singleton()->print("Missing time scale argument, aborting.\n");
				goto error;
			}
		} else if (arg == "--main-pack") {
// Note: main-pack is always used on web and can't be disabled.
// Note: main-pack can be used on Android so long as it's located within the 'assets' directory which is in the executable.
#if defined(OVERRIDE_PATH_ENABLED) || defined(WEB_ENABLED) || defined(ANDROID_ENABLED)
			if (N) {
				main_pack = N->get();
				N = N->next();

#if defined(ANDROID_ENABLED) && !defined(OVERRIDE_PATH_ENABLED)
				// Validate that `main_pack` is located within the 'assets' directory.
				Ref<FileAccess> main_pack_fa = FileAccess::create_for_path(main_pack);
				if (main_pack_fa.is_valid()) {
					if (main_pack_fa->get_access_type() != FileAccess::ACCESS_RESOURCES) {
						ERR_PRINT(
								"--main-pack is attempting to load from outside of the executable, but this Foundry binary was compiled without support for path overrides. Aborting.\n"
								"To be able to use it, use the `disable_path_overrides=no` SCons option when compiling Foundry.\n");
						goto error;
					}
				} else {
					ERR_PRINT("Invalid path to main pack file, aborting.\n");
					goto error;
				}
#endif // defined(ANDROID_ENABLED) && !defined(OVERRIDE_PATH_ENABLED)
			} else {
				OS::get_singleton()->print("Missing path to main pack file, aborting.\n");
				goto error;
			}
#else
			ERR_PRINT(
					"`--main-pack` was specified on the command line, but this Foundry binary was compiled without support for path overrides. Aborting.\n"
					"To be able to use it, use the `disable_path_overrides=no` SCons option when compiling Foundry.\n");
			goto error;
#endif // defined(OVERRIDE_PATH_ENABLED) || defined(WEB_ENABLED) || defined(ANDROID_ENABLED)

		} else if (arg == "-d" || arg == "--debug") {
			debug_uri = "local://";
			OS::get_singleton()->_debug_stdout = true;
#if defined(DEBUG_ENABLED)
		} else if (arg == "--debug-collisions") {
			debug_collisions = true;
		} else if (arg == "--debug-paths") {
			debug_paths = true;
		} else if (arg == "--debug-navigation") {
			debug_navigation = true;
		} else if (arg == "--debug-avoidance") {
			debug_avoidance = true;
		} else if (arg == "--debug-canvas-item-redraw") {
			debug_canvas_item_redraw = true;
		} else if (arg == "--debug-stringnames") {
			StringName::set_debug_stringnames(true);
		} else if (arg == "--debug-mute-audio") {
			debug_mute_audio = true;
#endif // defined(DEBUG_ENABLED)
#if defined(TOOLS_ENABLED) && (defined(WINDOWS_ENABLED) || defined(LINUXBSD_ENABLED))
		} else if (arg == "--test-rd-support") {
			test_rd_support = true;
		} else if (arg == "--test-rd-creation") {
			test_rd_creation = true;
#endif // defined(TOOLS_ENABLED) && (defined(WINDOWS_ENABLED) || defined(LINUXBSD_ENABLED))
		} else if (arg == "--remote-debug") {
#if defined(DEBUG_ENABLED) || defined(TOOLS_ENABLED)
			if (N) {
				debug_uri = N->get();
				if (!debug_uri.contains("://")) { // wrong address
					OS::get_singleton()->print(
							"Invalid debug host address, it should be of the form <protocol>://<host/IP>:<port>.\n");
					goto error;
				}
				N = N->next();
			} else {
				OS::get_singleton()->print("Missing remote debug host address, aborting.\n");
				goto error;
			}
#else
			ERR_PRINT(
					"`--remote-debug` was specified on the command line, but this Foundry binary was compiled without debug. Aborting.\n"
					"To be able to use it, use the `target=template_debug` SCons option when compiling Foundry.\n");
			goto error;
#endif // defined(DEBUG_ENABLED) || defined (TOOLS_ENABLED)
		} else if (arg == "--editor-pid") { // not exposed to user
			if (N) {
				editor_pid = N->get().to_int();
				N = N->next();
			} else {
				OS::get_singleton()->print("Missing editor PID argument, aborting.\n");
				goto error;
			}
		} else if (arg == "--disable-render-loop") {
			disable_render_loop = true;
		} else if (arg == "--fixed-fps") {
			if (N) {
				fixed_fps = N->get().to_int();
				N = N->next();
			} else {
				OS::get_singleton()->print("Missing fixed-fps argument, aborting.\n");
				goto error;
			}
		} else if (arg == "--write-movie") {
			if (N) {
				Engine::get_singleton()->set_write_movie_path(N->get());
				N = N->next();
				if (fixed_fps == -1) {
					fixed_fps = 60;
				}
				OS::get_singleton()->_writing_movie = true;
			} else {
				OS::get_singleton()->print("Missing write-movie argument, aborting.\n");
				goto error;
			}
		} else if (arg == "--disable-vsync") {
			disable_vsync = true;
		} else if (arg == "--print-fps") {
			print_fps = true;
#ifdef TOOLS_ENABLED
		} else if (arg == "--editor-pseudolocalization") {
			editor_pseudolocalization = true;
			main_args.push_back(arg);
#endif // TOOLS_ENABLED
		} else if (arg == "--gpu-profile") {
			profile_gpu = true;
		} else if (arg == "--disable-crash-handler") {
			OS::get_singleton()->disable_crash_handler();
		} else if (arg == "--skip-breakpoints") {
			skip_breakpoints = true;
		} else if (I->get() == "--ignore-error-breaks") {
			ignore_error_breaks = true;
#ifndef XR_DISABLED
		} else if (arg == "--xr-mode") {
			if (N) {
				String xr_mode = N->get().to_lower();
				N = N->next();
				if (xr_mode == "default") {
					XRServer::set_xr_mode(XRServer::XRMODE_DEFAULT);
				} else if (xr_mode == "off") {
					XRServer::set_xr_mode(XRServer::XRMODE_OFF);
				} else if (xr_mode == "on") {
					XRServer::set_xr_mode(XRServer::XRMODE_ON);
				} else {
					OS::get_singleton()->print("Unknown --xr-mode argument \"%s\", aborting.\n", xr_mode.ascii().get_data());
					goto error;
				}
			} else {
				OS::get_singleton()->print("Missing --xr-mode argument, aborting.\n");
				goto error;
			}
#endif // XR_DISABLED
		} else if (arg == "--benchmark") {
			OS::get_singleton()->set_use_benchmark(true);
		} else if (arg == "--benchmark-file") {
			if (N) {
				OS::get_singleton()->set_use_benchmark(true);
				String benchmark_file = N->get();
				OS::get_singleton()->set_benchmark_file(benchmark_file);
				N = N->next();
			} else {
				OS::get_singleton()->print("Missing <path> argument for --benchmark-file <path>.\n");
				goto error;
			}
#if defined(TOOLS_ENABLED) && defined(MODULE_FOUNDRY_SCRIPT_ENABLED) && !defined(FOUNDRY_SCRIPT_NO_LSP)
		} else if (arg == "--lsp-port") {
			if (N) {
				int port_override = N->get().to_int();
				if (port_override < 0 || port_override > 65535) {
					OS::get_singleton()->print("<port> argument for --lsp-port <port> must be between 0 and 65535.\n");
					goto error;
				}
				FSLanguageServer::port_override = port_override;
				N = N->next();
			} else {
				OS::get_singleton()->print("Missing <port> argument for --lsp-port <port>.\n");
				goto error;
			}
#endif // TOOLS_ENABLED && MODULE_FOUNDRY_SCRIPT_ENABLED && !FOUNDRY_SCRIPT_NO_LSP
#if defined(TOOLS_ENABLED)
		} else if (arg == "--dap-port") {
			if (N) {
				int port_override = N->get().to_int();
				if (port_override < 0 || port_override > 65535) {
					OS::get_singleton()->print("<port> argument for --dap-port <port> must be between 0 and 65535.\n");
					goto error;
				}
				DebugAdapterServer::port_override = port_override;
				N = N->next();
			} else {
				OS::get_singleton()->print("Missing <port> argument for --dap-port <port>.\n");
				goto error;
			}
#endif // TOOLS_ENABLED
		} else if (arg == "--wid") {
			if (N) {
				init_embed_parent_window_id = N->get().to_int();
				if (init_embed_parent_window_id == 0) {
					OS::get_singleton()->print("<window_id> argument for --wid <window_id> must be different then 0.\n");
					goto error;
				}

				OS::get_singleton()->_embedded_in_editor = true;
				Engine::get_singleton()->set_embedded_in_editor(true);

				N = N->next();
			} else {
				OS::get_singleton()->print("Missing <window_id> argument for --wid <window_id>.\n");
				goto error;
			}

		} else if (arg == "--" || arg == "++") {
			adding_user_args = true;
		} else {
			main_args.push_back(arg);
		}

		I = N;
	}

#if defined(DEBUG_ENABLED) || defined(TOOLS_ENABLED)
	// Network file system needs to be configured before globals, since globals are based on the
	// 'project.foundry' file which will only be available through the network if this is enabled
	if (!remotefs.is_empty()) {
		int port;
		if (remotefs.contains_char(':')) {
			port = remotefs.get_slicec(':', 1).to_int();
			remotefs = remotefs.get_slicec(':', 0);
		} else {
			port = 6010;
		}
		Error err = OS::get_singleton()->setup_remote_filesystem(remotefs, port, remotefs_pass, project_path);

		if (err) {
			OS::get_singleton()->printerr("Could not connect to remotefs: %s:%i.\n", remotefs.utf8().get_data(), port);
			goto error;
		}
	}
#endif // defined(DEBUG_ENABLED) || defined (TOOLS_ENABLED)

#ifdef TOOLS_ENABLED
	// Projectless startup routing (#1135): an editor launch without an explicit,
	// loadable project auto-opens the last valid remembered project instead of dropping
	// straight to the projectless shell. Only bare launches and `editor open` are
	// eligible; command-line tools, runtime/game launches, exports, and the render-device
	// probes are excluded so their semantics are untouched.
	{
		using Kind = FoundryCLIParser::CLIInvocation::Kind;
		const Kind kind = cli_parse.invocation.kind;
		// `editor open` is an explicit editor command (a positional scene there just names a
		// scene to reopen). A command-less launch (NONE) is an editor launch only when it
		// carries no legacy runtime scene/script args — `foundry --script X` and friends
		// share the command-less shape of a bare editor launch but must run as a
		// game/script, not auto-open a remembered project.
		const bool editor_intent_launch = kind == FoundryCLIParser::CLIInvocation::EDITOR_OPEN ||
				(kind == FoundryCLIParser::CLIInvocation::NONE &&
						!StartupRouter::args_request_runtime_launch(main_args));
		// Automation launches (`editor open --project ... --automation`) drive disposable
		// scratch projects from tests and agent workflows; they must never auto-open a
		// remembered project or overwrite the user's GUI recents/auto-open candidate.
		if (editor_intent_launch && !cmdline_tool &&
				!test_rd_support && !test_rd_creation && main_pack.is_empty()) {
			if (!cli_parse.invocation.automation) {
				interactive_editor_launch = true;
			}

			// The user pinned a project when `--project` was given (its value is preserved
			// on the invocation even for `--project .`, which leaves project_path as "."),
			// when a legacy/positional path set project_path directly, or when an explicit
			// `--project` failed to apply. In every such case a remembered project must not
			// be auto-opened. It is valid (setup() will attempt it) unless it failed to apply.
			const bool explicit_requested = !cli_parse.invocation.project_path.is_empty() ||
					project_path != "." || foundry_cli_project_path_error;
			const bool explicit_valid = !foundry_cli_project_path_error;

			KnownProjectStore known_projects(EditorPaths::get_data_dir_path().path_join("known_projects.cfg"));
			known_projects.load();
			const StartupRouter::Decision decision = StartupRouter::resolve_launch(
					explicit_requested, explicit_valid, OS::get_singleton()->get_cwd(), known_projects);
			if (decision.route == StartupRouter::ROUTE_OPEN_REMEMBERED && !cli_parse.invocation.automation) {
				project_path = decision.project_path;
				editor = true;
			} else if (decision.route == StartupRouter::ROUTE_PROJECTLESS_SHELL ||
					(cli_parse.invocation.automation && decision.route == StartupRouter::ROUTE_OPEN_REMEMBERED)) {
				// Automation launches without an explicit project must boot the projectless
				// shell even when a remembered project exists; auto-opening would mutate the
				// user's recents and load an unrelated project from disposable test cwd.
				editor = true;
				projectless_editor_shell = true;
			}
			// The remaining routes (explicit path, cwd project, explicit-invalid) intentionally
			// leave project_path/editor untouched: the explicit or cwd path is loaded by the
			// setup() call below, and an unresolved launch fails that setup() and drops to the
			// projectless editor shell via the no-project fallback below unless
			// projectless_editor_shell was already selected above.
			// The router never returns ROUTE_OPEN_REMEMBERED for an explicit-invalid launch,
			// so an invalid `--project` can never silently auto-open a remembered project;
			// the recording gate below additionally refuses to record the ambient cwd
			// project that Godot's upward search may still load in that error case.
			if (decision.store_modified && !cli_parse.invocation.automation) {
				known_projects.save();
			}
		}
	}
#endif

	OS::get_singleton()->_in_editor = editor;
	if (globals->setup(project_path, main_pack, false, editor) == OK) {
#ifdef TOOLS_ENABLED
		found_project = true;
		if (projectless_editor_shell && ProjectSettings::get_singleton()->is_project_loaded()) {
			projectless_editor_shell = false;
		}
#endif
	} else {
#ifdef TOOLS_ENABLED
		if (!projectless_editor_shell) {
			editor = false;
		}
#else
		String error_msg = "Error: Couldn't load project data at path \"" + (project_path == "." ? OS::get_singleton()->get_cwd() : project_path) + "\". Is the .pck file missing?\n\n";
#if !defined(OVERRIDE_PATH_ENABLED) && !defined(TOOLS_ENABLED)
		String exec_path = OS::get_singleton()->get_executable_path();
		String exec_basename = exec_path.get_file().get_basename();

		if (FileAccess::exists(old_cwd.path_join(exec_basename + ".pck"))) {
			error_msg += "\"" + exec_basename + ".pck\" was found in the current working directory. To be able to load a project from the CWD, use the `disable_path_overrides=no` SCons option when compiling Foundry.\n";
		} else if (FileAccess::exists(old_cwd.path_join("project.foundry"))) {
			error_msg += "\"project.foundry\" was found in the current working directory. To be able to load a project from the CWD, use the `disable_path_overrides=no` SCons option when compiling Foundry.\n";
		} else {
			error_msg += "If you've renamed the executable, the associated .pck file should also be renamed to match the executable's name (without the extension).\n";
		}
#else
		error_msg += "If you've renamed the executable, the associated .pck file should also be renamed to match the executable's name (without the extension).\n";
#endif
		ERR_PRINT(error_msg);

		OS::get_singleton()->alert(error_msg);

		goto error;
#endif
	}

	// Initialize WorkerThreadPool.
	{
#ifdef THREADS_ENABLED
		if (editor) {
			WorkerThreadPool::get_singleton()->init(-1, 0.75);
		} else {
			int worker_threads = GLOBAL_GET("threading/worker_pool/max_threads");
			float low_priority_ratio = GLOBAL_GET("threading/worker_pool/low_priority_thread_ratio");
			WorkerThreadPool::get_singleton()->init(worker_threads, low_priority_ratio);
		}
#else
		WorkerThreadPool::get_singleton()->init(0, 0);
#endif
	}

#ifdef TOOLS_ENABLED
	if (!editor && !found_project && !cmdline_tool) {
		// No project could be loaded and this is an interactive launch: open the
		// projectless editor shell (which presents the startup dialog) instead of a
		// separate Project Manager startup mode, which no longer exists.
		editor = true;
		projectless_editor_shell = true;
	}

	{
		// Synced with https://github.com/baldurk/renderdoc/blob/2b01465c7/renderdoc/driver/vulkan/vk_layer.cpp#L118-L165
		LocalVector<String> layers_to_disable = {
			"DISABLE_RTSS_LAYER", // GH-57937.
			"DISABLE_VULKAN_OBS_CAPTURE", // GH-103800.
			"DISABLE_VULKAN_OW_OBS_CAPTURE", // GH-104154.
			"DISABLE_SAMPLE_LAYER", // GH-104154.
			"DISABLE_GAMEPP_LAYER", // GH-104154.
			"DISABLE_VK_LAYER_TENCENT_wegame_cross_overlay_1", // GH-104154.
			// "NODEVICE_SELECT", // Kept as it's useful - GH-104592.
			"VK_LAYER_bandicam_helper_DEBUG_1", // GH-101480.
			"DISABLE_VK_LAYER_bandicam_helper_1", // GH-101480.
			"DISABLE_VK_LAYER_reshade_1", // GH-70849.
			"DISABLE_VK_LAYER_GPUOpen_GRS", // GH-104154.
			"DISABLE_LAYER", // GH-104154 (fpsmon).
			"DISABLE_MANGOHUD", // GH-57403.
			"DISABLE_VKBASALT",
			"DISABLE_FOSSILIZE", // GH-115139.
		};

#if defined(WINDOWS_ENABLED) || defined(LINUXBSD_ENABLED)
		if (editor || test_rd_support || test_rd_creation) {
#else
			if (editor) {
#endif
			// Disable Vulkan overlays in editor, they cause various issues.
			for (const String &layer_disable : layers_to_disable) {
				OS::get_singleton()->set_environment(layer_disable, "1");
			}
		} else {
			// Re-allow using Vulkan overlays, disabled while using the editor.
			for (const String &layer_disable : layers_to_disable) {
				OS::get_singleton()->unset_environment(layer_disable);
			}
		}
	}
#endif

#if defined(TOOLS_ENABLED) && (defined(WINDOWS_ENABLED) || defined(LINUXBSD_ENABLED))
	if (test_rd_support) {
		// Test Rendering Device creation and exit.

		OS::get_singleton()->set_crash_handler_silent();
		if (OS::get_singleton()->_test_create_rendering_device(display_driver)) {
			exit_err = ERR_HELP;
		} else {
			exit_err = ERR_UNAVAILABLE;
		}
		goto error;
	} else if (test_rd_creation) {
		// Test OpenGL context and Rendering Device simultaneous creation and exit.

		OS::get_singleton()->set_crash_handler_silent();
		if (OS::get_singleton()->_test_create_rendering_device_and_gl(display_driver)) {
			exit_err = ERR_HELP;
		} else {
			exit_err = ERR_UNAVAILABLE;
		}
		goto error;
	}
#endif

#ifdef TOOLS_ENABLED
	if (editor) {
		Engine::get_singleton()->set_editor_hint(true);
		Engine::get_singleton()->set_extension_reloading_enabled(true);
		if (projectless_editor_shell) {
			Engine::get_singleton()->set_projectless_editor_shell_hint(true);
		}

		// Create initialization lock file to detect crashes during startup.
		OS::get_singleton()->create_lock_file();

		if (!init_windowed && !init_fullscreen) {
			init_maximized = true;
			window_mode = DisplayServer::WINDOW_MODE_MAXIMIZED;
		}
	}

	if (recovery_mode) {
		if (!editor) {
			OS::get_singleton()->print("Error: Recovery mode can only be used in the editor. Aborting.\n");
			goto error;
		}

		Engine::get_singleton()->set_recovery_mode_hint(true);
	}
#endif

	OS::get_singleton()->set_cmdline(execpath, main_args, user_args);

	Engine::get_singleton()->set_physics_ticks_per_second(GLOBAL_DEF_BASIC(PropertyInfo(Variant::INT, "physics/common/physics_ticks_per_second", PROPERTY_HINT_RANGE, "1,1000,1"), 60));
	Engine::get_singleton()->set_max_physics_steps_per_frame(GLOBAL_DEF_BASIC(PropertyInfo(Variant::INT, "physics/common/max_physics_steps_per_frame", PROPERTY_HINT_RANGE, "1,100,1"), 8));
	Engine::get_singleton()->set_physics_jitter_fix(GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "physics/common/physics_jitter_fix", PROPERTY_HINT_RANGE, "0,2,0.001,or_greater"), 0.5));
	Engine::get_singleton()->set_max_fps(GLOBAL_DEF(PropertyInfo(Variant::INT, "application/run/max_fps", PROPERTY_HINT_RANGE, "0,1000,1"), 0));
	if (max_fps >= 0) {
		Engine::get_singleton()->set_max_fps(max_fps);
	}

	// Initialize user data dir.
	OS::get_singleton()->ensure_user_data_dir();

	OS::get_singleton()->set_low_processor_usage_mode(GLOBAL_DEF("application/run/low_processor_mode", false));
	OS::get_singleton()->set_low_processor_usage_mode_sleep_usec(
			GLOBAL_DEF(PropertyInfo(Variant::INT, "application/run/low_processor_mode_sleep_usec", PROPERTY_HINT_RANGE, "0,33200,1,or_greater"), 6900)); // Roughly 144 FPS

	GLOBAL_DEF("application/run/delta_smoothing", true);
	GLOBAL_DEF("application/run/push_fatal_terminates", true);
	if (!delta_smoothing_override) {
		OS::get_singleton()->set_delta_smoothing(GLOBAL_GET("application/run/delta_smoothing"));
	}

	GLOBAL_DEF("debug/settings/stdout/print_fps", false);
	GLOBAL_DEF("debug/settings/stdout/print_gpu_profile", false);
	GLOBAL_DEF("debug/settings/stdout/verbose_stdout", false);
	GLOBAL_DEF("debug/settings/physics_interpolation/enable_warnings", true);
	if (!OS::get_singleton()->_verbose_stdout) { // Not manually overridden.
		OS::get_singleton()->_verbose_stdout = GLOBAL_GET("debug/settings/stdout/verbose_stdout");
	}

	register_early_core_singletons();
	initialize_modules(MODULE_INITIALIZATION_LEVEL_CORE);
	register_core_extensions(); // core extensions must be registered after globals setup and before display

	if (!editor) {
		ResourceUID::get_singleton()->enable_reverse_cache();
	}
	ResourceUID::get_singleton()->load_from_cache(true); // Load UUIDs from cache.
	ProjectSettings::get_singleton()->fix_autoload_paths(); // Handles autoloads saved as UID.

	if (ProjectSettings::get_singleton()->has_custom_feature("dedicated_server")) {
		audio_driver = NULL_AUDIO_DRIVER;
		display_driver = NULL_DISPLAY_DRIVER;
	}

	GLOBAL_DEF(PropertyInfo(Variant::INT, "network/limits/debugger/max_chars_per_second", PROPERTY_HINT_RANGE, "256,4096,1,or_greater"), 32768);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "network/limits/debugger/max_queued_messages", PROPERTY_HINT_RANGE, "128,8192,1,or_greater"), 2048);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "network/limits/debugger/max_errors_per_second", PROPERTY_HINT_RANGE, "1,200,1,or_greater"), 400);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "network/limits/debugger/max_warnings_per_second", PROPERTY_HINT_RANGE, "1,200,1,or_greater"), 400);

	EngineDebugger::initialize(debug_uri, skip_breakpoints, ignore_error_breaks, breakpoints, []() {
		if (editor_pid) {
			DisplayServer::get_singleton()->enable_for_stealing_focus(editor_pid);
		}
	});

#ifdef TOOLS_ENABLED
	if (editor) {
		packed_data->set_disabled(true);
	}
#endif

	GLOBAL_DEF("debug/file_logging/enable_file_logging", false);
	// Only file logging by default on desktop platforms as logs can't be
	// accessed easily on mobile/Web platforms (if at all).
	// This also prevents logs from being created for the editor instance, as feature tags
	// are disabled while in the editor (even if they should logically apply).
	GLOBAL_DEF("debug/file_logging/enable_file_logging.pc", true);
	GLOBAL_DEF("debug/file_logging/log_path", "user://logs/foundry.log");
	GLOBAL_DEF(PropertyInfo(Variant::INT, "debug/file_logging/max_log_files", PROPERTY_HINT_RANGE, "0,20,1,or_greater"), 5);

	// If `--log-file` is used to override the log path, allow creating logs for the project manager or editor
	// and even if file logging is disabled in the Project Settings.
	// `--log-file` can be used with any path (including absolute paths outside the project folder),
	// so check for filesystem access if it's used.
	if (FileAccess::get_create_func(!log_file.is_empty() ? FileAccess::ACCESS_FILESYSTEM : FileAccess::ACCESS_USERDATA) &&
			(!log_file.is_empty() || (!editor && GLOBAL_GET("debug/file_logging/enable_file_logging")))) {
		// Don't create logs for the projectless editor shell as they would be written
		// to the current working directory, which is inconvenient.
		String base_path;
		int max_files;
		if (!log_file.is_empty()) {
			base_path = log_file;
			// Ensure log file name respects the specified override by disabling log rotation.
			max_files = 1;
		} else {
			base_path = GLOBAL_GET("debug/file_logging/log_path");
			max_files = GLOBAL_GET("debug/file_logging/max_log_files");
		}
		OS::get_singleton()->add_logger(memnew(RotatedFileLogger(base_path, max_files)));
	}

	// A `project run --script <path>` or `project test --runner <path>` invocation is applied later
	// in `Main::start()`, after this check runs. Treat a pending CLI script or test runner as a
	// valid run target so a project without a main scene can still execute instead of aborting here.
	// Projectless CLI tools are handled later in `Main::start()` and never require a main scene.
	if (main_args.is_empty() &&
			!FoundryCLIParser::can_run_without_main_scene(foundry_cli_parse.invocation) &&
			String(GLOBAL_GET("application/run/main_scene")) == "") {
#ifdef TOOLS_ENABLED
		if (!editor) {
#endif
			const String error_msg = "Error: Can't run project: no main scene defined in the project.\n";
			OS::get_singleton()->print("%s", error_msg.utf8().get_data());
			OS::get_singleton()->alert(error_msg);
			goto error;
#ifdef TOOLS_ENABLED
		}
#endif
	}

	if (editor) {
		Engine::get_singleton()->set_editor_hint(true);
		use_custom_res = false;
		input_map->load_default(); //keys for editor
	} else {
		input_map->load_from_project_settings(); //keys for game
	}

	if (bool(GLOBAL_GET("application/run/disable_stdout"))) {
		quiet_stdout = true;
	}
	if (bool(GLOBAL_GET("application/run/disable_stderr"))) {
		CoreGlobals::print_error_enabled = false;
	}
	if (!bool(GLOBAL_GET("application/run/print_header"))) {
		// --no-header option for project settings.
		Engine::get_singleton()->_print_header = false;
	}

	if (quiet_stdout) {
		CoreGlobals::print_line_enabled = false;
	}

	Logger::set_flush_stdout_on_print(GLOBAL_GET("application/run/flush_stdout_on_print"));

	// Rendering drivers configuration.

	// Always include all supported drivers as hint, as this is used by the editor host platform
	// for project settings. For example, a Linux user should be able to configure that they want
	// to export for D3D12 on Windows and Metal on macOS even if their host platform can't use those.

	{
		// RenderingDevice driver overrides per platform.
		GLOBAL_DEF_RST("rendering/rendering_device/driver", "vulkan");
		GLOBAL_DEF_RST(PropertyInfo(Variant::STRING, "rendering/rendering_device/driver.windows", PROPERTY_HINT_ENUM, "vulkan,d3d12"), "vulkan");
		GLOBAL_DEF_RST(PropertyInfo(Variant::STRING, "rendering/rendering_device/driver.linuxbsd", PROPERTY_HINT_ENUM, "vulkan"), "vulkan");
		GLOBAL_DEF_RST(PropertyInfo(Variant::STRING, "rendering/rendering_device/driver.android", PROPERTY_HINT_ENUM, "vulkan"), "vulkan");
		GLOBAL_DEF_RST(PropertyInfo(Variant::STRING, "rendering/rendering_device/driver.ios", PROPERTY_HINT_ENUM, "metal,vulkan"), "metal");
		GLOBAL_DEF_RST(PropertyInfo(Variant::STRING, "rendering/rendering_device/driver.visionos", PROPERTY_HINT_ENUM, "metal"), "metal");
		GLOBAL_DEF_RST(PropertyInfo(Variant::STRING, "rendering/rendering_device/driver.macos", PROPERTY_HINT_ENUM, "metal,vulkan"), "metal");

		GLOBAL_DEF_RST("rendering/rendering_device/fallback_to_vulkan", true);
		GLOBAL_DEF_RST("rendering/rendering_device/fallback_to_d3d12", true);
		GLOBAL_DEF_RST("rendering/rendering_device/fallback_to_opengl3", true);
	}

	{
		// GL Compatibility driver overrides per platform.
		GLOBAL_DEF_RST("rendering/gl_compatibility/driver", "opengl3");
		GLOBAL_DEF_RST(PropertyInfo(Variant::STRING, "rendering/gl_compatibility/driver.windows", PROPERTY_HINT_ENUM, "opengl3,opengl3_angle"), "opengl3");
		GLOBAL_DEF_RST(PropertyInfo(Variant::STRING, "rendering/gl_compatibility/driver.linuxbsd", PROPERTY_HINT_ENUM, "opengl3,opengl3_es"), "opengl3");
		GLOBAL_DEF_RST(PropertyInfo(Variant::STRING, "rendering/gl_compatibility/driver.web", PROPERTY_HINT_ENUM, "opengl3"), "opengl3");
		GLOBAL_DEF_RST(PropertyInfo(Variant::STRING, "rendering/gl_compatibility/driver.android", PROPERTY_HINT_ENUM, "opengl3"), "opengl3");
		GLOBAL_DEF_RST(PropertyInfo(Variant::STRING, "rendering/gl_compatibility/driver.ios", PROPERTY_HINT_ENUM, "opengl3"), "opengl3");
		GLOBAL_DEF_RST(PropertyInfo(Variant::STRING, "rendering/gl_compatibility/driver.macos", PROPERTY_HINT_ENUM, "opengl3,opengl3_angle"), "opengl3");

		GLOBAL_DEF_RST("rendering/gl_compatibility/nvidia_disable_threaded_optimization", true);
		GLOBAL_DEF_RST("rendering/gl_compatibility/fallback_to_angle", true);
		GLOBAL_DEF_RST("rendering/gl_compatibility/fallback_to_native", true);
		GLOBAL_DEF_RST("rendering/gl_compatibility/fallback_to_gles", true);

		Array force_angle_list;

#define FORCE_ANGLE(m_vendor, m_name)       \
	{                                       \
		Dictionary device;                  \
		device["vendor"] = m_vendor;        \
		device["name"] = m_name;            \
		force_angle_list.push_back(device); \
	}

		// AMD GPUs.
		FORCE_ANGLE("ATI", "Radeon 9"); // ATI Radeon 9000 Series
		FORCE_ANGLE("ATI", "Radeon X"); // ATI Radeon X500-X2000 Series
		FORCE_ANGLE("ATI", "Radeon HD 2"); // AMD/ATI (Mobility) Radeon HD 2xxx Series
		FORCE_ANGLE("ATI", "Radeon HD 3"); // AMD/ATI (Mobility) Radeon HD 3xxx Series
		FORCE_ANGLE("ATI", "Radeon HD 4"); // AMD/ATI (Mobility) Radeon HD 4xxx Series
		FORCE_ANGLE("ATI", "Radeon HD 5"); // AMD/ATI (Mobility) Radeon HD 5xxx Series
		FORCE_ANGLE("ATI", "Radeon HD 6"); // AMD/ATI (Mobility) Radeon HD 6xxx Series
		FORCE_ANGLE("ATI", "Radeon HD 7"); // AMD/ATI (Mobility) Radeon HD 7xxx Series
		FORCE_ANGLE("ATI", "Radeon HD 8"); // AMD/ATI (Mobility) Radeon HD 8xxx Series
		FORCE_ANGLE("ATI", "Radeon(TM) R2 Graphics"); // APUs
		FORCE_ANGLE("ATI", "Radeon(TM) R3 Graphics");
		FORCE_ANGLE("ATI", "Radeon(TM) R4 Graphics");
		FORCE_ANGLE("ATI", "Radeon(TM) R5 Graphics");
		FORCE_ANGLE("ATI", "Radeon(TM) R6 Graphics");
		FORCE_ANGLE("ATI", "Radeon(TM) R7 Graphics");
		FORCE_ANGLE("AMD", "Radeon(TM) R7 Graphics");
		FORCE_ANGLE("AMD", "Radeon(TM) R8 Graphics");
		FORCE_ANGLE("ATI", "Radeon R5 Graphics");
		FORCE_ANGLE("ATI", "Radeon R6 Graphics");
		FORCE_ANGLE("ATI", "Radeon R7 Graphics");
		FORCE_ANGLE("AMD", "Radeon R7 Graphics");
		FORCE_ANGLE("AMD", "Radeon R8 Graphics");
		FORCE_ANGLE("ATI", "Radeon R5 2"); // Rx 2xx Series
		FORCE_ANGLE("ATI", "Radeon R7 2");
		FORCE_ANGLE("ATI", "Radeon R9 2");
		FORCE_ANGLE("ATI", "Radeon R5 M2"); // Rx M2xx Series
		FORCE_ANGLE("ATI", "Radeon R7 M2");
		FORCE_ANGLE("ATI", "Radeon R9 M2");
		FORCE_ANGLE("ATI", "Radeon (TM) R9 Fury");
		FORCE_ANGLE("ATI", "Radeon (TM) R5 3"); // Rx 3xx Series
		FORCE_ANGLE("AMD", "Radeon (TM) R5 3");
		FORCE_ANGLE("ATI", "Radeon (TM) R7 3");
		FORCE_ANGLE("AMD", "Radeon (TM) R7 3");
		FORCE_ANGLE("ATI", "Radeon (TM) R9 3");
		FORCE_ANGLE("AMD", "Radeon (TM) R9 3");
		FORCE_ANGLE("ATI", "Radeon (TM) R5 M3"); // Rx M3xx Series
		FORCE_ANGLE("AMD", "Radeon (TM) R5 M3");
		FORCE_ANGLE("ATI", "Radeon (TM) R7 M3");
		FORCE_ANGLE("AMD", "Radeon (TM) R7 M3");
		FORCE_ANGLE("ATI", "Radeon (TM) R9 M3");
		FORCE_ANGLE("AMD", "Radeon (TM) R9 M3");

		// Intel GPUs (Gen7-Gen9.5 devices).
		FORCE_ANGLE("Intel", "Intel(R) HD Graphics");
		FORCE_ANGLE("Intel", "Intel HD Graphics");
		FORCE_ANGLE("Intel", "Intel(R) Vallyview Graphics");
		FORCE_ANGLE("Intel", "Intel(R) Iris(TM) Graphics 5100");
		FORCE_ANGLE("Intel", "Intel(R) Iris(TM) Pro Graphics 5200");
		FORCE_ANGLE("Intel", "Intel(R) Iris(TM) Graphics 6100");
		FORCE_ANGLE("Intel", "Intel(R) Iris(TM) Pro Graphics 6200");
		FORCE_ANGLE("Intel", "Intel(R) Iris(TM) Pro Graphics P6300");
		FORCE_ANGLE("Intel", "Intel(R) Iris Graphics 540");
		FORCE_ANGLE("Intel", "Intel(R) Iris Plus Graphics 640");
		FORCE_ANGLE("Intel", "Intel(R) Iris Plus Graphics 650");
		FORCE_ANGLE("Intel", "Intel(R) Iris Pro Graphics 580");
		FORCE_ANGLE("Intel", "Intel(R) Iris Pro Graphics P580");

#undef FORCE_ANGLE

		GLOBAL_DEF_RST_NOVAL(PropertyInfo(Variant::ARRAY, "rendering/gl_compatibility/force_angle_on_devices", PROPERTY_HINT_ARRAY_TYPE, vformat("%s/%s:%s", Variant::DICTIONARY, PROPERTY_HINT_NONE, String())), force_angle_list);
	}

	// Start with RenderingDevice-based backends.
#ifdef RD_ENABLED
	renderer_hints = "forward_plus,mobile";
	default_renderer_mobile = "mobile";
#endif

	// And Compatibility next, or first if Vulkan is disabled.
#ifdef GLES3_ENABLED
	if (!renderer_hints.is_empty()) {
		renderer_hints += ",";
	}
	renderer_hints += "gl_compatibility";
	if (default_renderer_mobile.is_empty()) {
		default_renderer_mobile = "gl_compatibility";
	}
#endif

	if (!rendering_method.is_empty()) {
		if (rendering_method != "forward_plus" &&
				rendering_method != "mobile" &&
				rendering_method != "gl_compatibility" &&
				rendering_method != "dummy") {
			OS::get_singleton()->print("Unknown rendering method '%s', aborting.\nValid options are ",
					rendering_method.utf8().get_data());

			Vector<String> rendering_method_hints = renderer_hints.split(",");
			rendering_method_hints.push_back("dummy");
			for (int i = 0; i < rendering_method_hints.size(); i++) {
				if (i == rendering_method_hints.size() - 1) {
					OS::get_singleton()->print(" and ");
				} else if (i != 0) {
					OS::get_singleton()->print(", ");
				}
				OS::get_singleton()->print("'%s'", rendering_method_hints[i].utf8().get_data());
			}

			OS::get_singleton()->print(".\n");
			goto error;
		}
	}
	if (renderer_hints.is_empty()) {
		renderer_hints = "dummy";
	}

	if (!rendering_driver.is_empty()) {
		// As the rendering drivers available may depend on the display driver and renderer
		// selected, we can't do an exhaustive check here, but we can look through all
		// the options in all the display drivers for a match.

		bool found = false;
		for (int i = 0; i < DisplayServer::get_create_function_count(); i++) {
			Vector<String> r_drivers = DisplayServer::get_create_function_rendering_drivers(i);

			for (int d = 0; d < r_drivers.size(); d++) {
				if (rendering_driver == r_drivers[d]) {
					found = true;
					break;
				}
			}
		}

		if (!found) {
			OS::get_singleton()->print("Unknown rendering driver '%s', aborting.\nValid options are ",
					rendering_driver.utf8().get_data());

			// Deduplicate driver entries, as a rendering driver may be supported by several display servers.
			Vector<String> unique_rendering_drivers;
			for (int i = 0; i < DisplayServer::get_create_function_count(); i++) {
				Vector<String> r_drivers = DisplayServer::get_create_function_rendering_drivers(i);

				for (int d = 0; d < r_drivers.size(); d++) {
					if (!unique_rendering_drivers.has(r_drivers[d])) {
						unique_rendering_drivers.append(r_drivers[d]);
					}
				}
			}

			for (int i = 0; i < unique_rendering_drivers.size(); i++) {
				if (i == unique_rendering_drivers.size() - 1) {
					OS::get_singleton()->print(" and ");
				} else if (i != 0) {
					OS::get_singleton()->print(", ");
				}
				OS::get_singleton()->print("'%s'", unique_rendering_drivers[i].utf8().get_data());
			}

			OS::get_singleton()->print(".\n");

			goto error;
		}

		// Set a default renderer if none selected. Try to choose one that matches the driver.
		if (rendering_method.is_empty()) {
			if (rendering_driver == "dummy") {
				rendering_method = "dummy";
			} else if (rendering_driver == "opengl3" || rendering_driver == "opengl3_angle" || rendering_driver == "opengl3_es") {
				rendering_method = "gl_compatibility";
			} else {
				rendering_method = "forward_plus";
			}
		}

		// Now validate whether the selected driver matches with the renderer.
		bool valid_combination = false;
		Vector<String> available_drivers;
		if (rendering_method == "forward_plus" || rendering_method == "mobile") {
#ifdef VULKAN_ENABLED
			available_drivers.push_back("vulkan");
#endif
#ifdef D3D12_ENABLED
			available_drivers.push_back("d3d12");
#endif
#ifdef METAL_ENABLED
			available_drivers.push_back("metal");
#endif
		}
#ifdef GLES3_ENABLED
		if (rendering_method == "gl_compatibility") {
			available_drivers.push_back("opengl3");
			available_drivers.push_back("opengl3_angle");
			available_drivers.push_back("opengl3_es");
		}
#endif
		if (rendering_method == "dummy") {
			available_drivers.push_back("dummy");
		}
		if (available_drivers.is_empty()) {
			OS::get_singleton()->print("Unknown renderer name '%s', aborting.\n", rendering_method.utf8().get_data());
			goto error;
		}

		for (int i = 0; i < available_drivers.size(); i++) {
			if (rendering_driver == available_drivers[i]) {
				valid_combination = true;
				break;
			}
		}

		if (!valid_combination) {
			OS::get_singleton()->print("Invalid renderer/driver combination '%s' and '%s', aborting. %s only supports the following drivers ", rendering_method.utf8().get_data(), rendering_driver.utf8().get_data(), rendering_method.utf8().get_data());

			for (int d = 0; d < available_drivers.size(); d++) {
				OS::get_singleton()->print("'%s', ", available_drivers[d].utf8().get_data());
			}

			OS::get_singleton()->print(".\n");

			goto error;
		}
	}

	default_renderer = renderer_hints.get_slicec(',', 0);
	GLOBAL_DEF_RST_BASIC(PropertyInfo(Variant::STRING, "rendering/renderer/rendering_method", PROPERTY_HINT_ENUM, renderer_hints), default_renderer);
	GLOBAL_DEF_RST_BASIC("rendering/renderer/rendering_method.mobile", default_renderer_mobile);
	GLOBAL_DEF_RST_BASIC(PropertyInfo(Variant::STRING, "rendering/renderer/rendering_method.web", PROPERTY_HINT_ENUM, "gl_compatibility"), "gl_compatibility"); // This is a bit of a hack until we have WebGPU support.

	// Default to ProjectSettings default if nothing set on the command line.
	if (rendering_method.is_empty()) {
		rendering_method = GLOBAL_GET("rendering/renderer/rendering_method");
	}

	if (rendering_driver.is_empty()) {
		if (rendering_method == "dummy") {
			rendering_driver = "dummy";
		} else if (rendering_method == "gl_compatibility") {
			rendering_driver = GLOBAL_GET("rendering/gl_compatibility/driver");
		} else {
			rendering_driver = GLOBAL_GET("rendering/rendering_device/driver");
		}
	}

	// always convert to lower case for consistency in the code
	rendering_driver = rendering_driver.to_lower();

	OS::get_singleton()->set_current_rendering_driver_name(rendering_driver);
	OS::get_singleton()->set_current_rendering_method(rendering_method);

	if (use_custom_res) {
		if (!force_res) {
			window_size.width = GLOBAL_GET("display/window/size/viewport_width");
			window_size.height = GLOBAL_GET("display/window/size/viewport_height");

			if (globals->has_setting("display/window/size/window_width_override") &&
					globals->has_setting("display/window/size/window_height_override")) {
				int desired_width = GLOBAL_GET("display/window/size/window_width_override");
				if (desired_width > 0) {
					window_size.width = desired_width;
				}
				int desired_height = GLOBAL_GET("display/window/size/window_height_override");
				if (desired_height > 0) {
					window_size.height = desired_height;
				}
			}
		}

		if (!bool(GLOBAL_GET("display/window/size/resizable"))) {
			window_flags |= DisplayServer::WINDOW_FLAG_RESIZE_DISABLED_BIT;
		}
		if (bool(GLOBAL_GET("display/window/size/minimize_disabled"))) {
			window_flags |= DisplayServer::WINDOW_FLAG_MINIMIZE_DISABLED_BIT;
		}
		if (bool(GLOBAL_GET("display/window/size/maximize_disabled"))) {
			window_flags |= DisplayServer::WINDOW_FLAG_MAXIMIZE_DISABLED_BIT;
		}
		if (bool(GLOBAL_GET("display/window/size/borderless"))) {
			window_flags |= DisplayServer::WINDOW_FLAG_BORDERLESS_BIT;
		}
		if (bool(GLOBAL_GET("display/window/size/always_on_top"))) {
			window_flags |= DisplayServer::WINDOW_FLAG_ALWAYS_ON_TOP_BIT;
		}
		if (bool(GLOBAL_GET("display/window/size/transparent"))) {
			window_flags |= DisplayServer::WINDOW_FLAG_TRANSPARENT_BIT;
		}
		if (bool(GLOBAL_GET("display/window/size/extend_to_title"))) {
			window_flags |= DisplayServer::WINDOW_FLAG_EXTEND_TO_TITLE_BIT;
		}
		if (bool(GLOBAL_GET("display/window/size/no_focus"))) {
			window_flags |= DisplayServer::WINDOW_FLAG_NO_FOCUS_BIT;
		}
		if (bool(GLOBAL_GET("display/window/size/sharp_corners"))) {
			window_flags |= DisplayServer::WINDOW_FLAG_SHARP_CORNERS_BIT;
		}
		window_mode = (DisplayServer::WindowMode)(GLOBAL_GET("display/window/size/mode").operator int());
		int initial_position_type = GLOBAL_GET("display/window/size/initial_position_type").operator int();
		if (initial_position_type == Window::WINDOW_INITIAL_POSITION_ABSOLUTE) { // Absolute.
			if (!init_use_custom_pos) {
				init_custom_pos = GLOBAL_GET("display/window/size/initial_position").operator Vector2i();
				init_use_custom_pos = true;
			}
		} else if (initial_position_type == Window::WINDOW_INITIAL_POSITION_CENTER_PRIMARY_SCREEN || initial_position_type == Window::WINDOW_INITIAL_POSITION_CENTER_MAIN_WINDOW_SCREEN) { // Center of Primary Screen.
			if (!init_use_custom_screen) {
				init_screen = DisplayServer::SCREEN_PRIMARY;
				init_use_custom_screen = true;
			}
		} else if (initial_position_type == Window::WINDOW_INITIAL_POSITION_CENTER_OTHER_SCREEN) { // Center of Other Screen.
			if (!init_use_custom_screen) {
				init_screen = GLOBAL_GET("display/window/size/initial_screen").operator int();
				init_use_custom_screen = true;
			}
		} else if (initial_position_type == Window::WINDOW_INITIAL_POSITION_CENTER_SCREEN_WITH_MOUSE_FOCUS) { // Center of Screen With Mouse Pointer.
			if (!init_use_custom_screen) {
				init_screen = DisplayServer::SCREEN_WITH_MOUSE_FOCUS;
				init_use_custom_screen = true;
			}
		} else if (initial_position_type == Window::WINDOW_INITIAL_POSITION_CENTER_SCREEN_WITH_KEYBOARD_FOCUS) { // Center of Screen With Keyboard Focus.
			if (!init_use_custom_screen) {
				init_screen = DisplayServer::SCREEN_WITH_KEYBOARD_FOCUS;
				init_use_custom_screen = true;
			}
		}
	}

	OS::get_singleton()->_allow_hidpi = GLOBAL_DEF("display/window/dpi/allow_hidpi", true);
	OS::get_singleton()->_allow_layered = GLOBAL_DEF_RST("display/window/per_pixel_transparency/allowed", false);

	load_shell_env = GLOBAL_DEF("application/run/load_shell_environment", false);

#ifdef TOOLS_ENABLED
	if (editor) {
		// The editor always detects and uses hiDPI if needed.
		OS::get_singleton()->_allow_hidpi = true;
		load_shell_env = true;
	}
#endif
	if (load_shell_env) {
		OS::get_singleton()->load_shell_environment();
	}

	if (separate_thread_render == -1) {
		separate_thread_render = (int)GLOBAL_DEF("rendering/driver/threads/thread_model", OS::RENDER_THREAD_SAFE) == OS::RENDER_SEPARATE_THREAD;
	}

	if (editor) {
		// The editor cannot run with rendering in a separate thread (it will crash on startup).
		separate_thread_render = 0;
	}
#if !defined(THREADS_ENABLED)
	separate_thread_render = 0;
#endif
	OS::get_singleton()->_separate_thread_render = separate_thread_render;

	/* Determine audio and video drivers */

	// Display driver, e.g. X11, Wayland.
	// Make sure that headless is the last one, which it is assumed to be by design.
	DEV_ASSERT(NULL_DISPLAY_DRIVER == DisplayServer::get_create_function_name(DisplayServer::get_create_function_count() - 1));

	GLOBAL_DEF_NOVAL("display/display_server/driver", "default");
	GLOBAL_DEF_NOVAL(PropertyInfo(Variant::STRING, "display/display_server/driver.windows", PROPERTY_HINT_ENUM, "default,windows,headless"), "default");
	GLOBAL_DEF_NOVAL(PropertyInfo(Variant::STRING, "display/display_server/driver.linuxbsd", PROPERTY_HINT_ENUM, "default,x11,wayland,headless"), "default");
	GLOBAL_DEF_NOVAL(PropertyInfo(Variant::STRING, "display/display_server/driver.android", PROPERTY_HINT_ENUM, "default,android,headless"), "default");
	GLOBAL_DEF_NOVAL(PropertyInfo(Variant::STRING, "display/display_server/driver.ios", PROPERTY_HINT_ENUM, "default,iOS,headless"), "default");
	GLOBAL_DEF_NOVAL(PropertyInfo(Variant::STRING, "display/display_server/driver.visionos", PROPERTY_HINT_ENUM, "default,visionOS,headless"), "default");
	GLOBAL_DEF_NOVAL(PropertyInfo(Variant::STRING, "display/display_server/driver.macos", PROPERTY_HINT_ENUM, "default,macos,headless"), "default");

	GLOBAL_DEF_RST_NOVAL("audio/driver/driver", AudioDriverManager::get_driver(0)->get_name());
	if (audio_driver.is_empty()) { // Specified in project.foundry.
		audio_driver = GLOBAL_GET("audio/driver/driver");
	}

	// Make sure that dummy is the last one, which it is assumed to be by design.
	DEV_ASSERT(NULL_AUDIO_DRIVER == AudioDriverManager::get_driver(AudioDriverManager::get_driver_count() - 1)->get_name());
	for (int i = 0; i < AudioDriverManager::get_driver_count(); i++) {
		if (audio_driver == AudioDriverManager::get_driver(i)->get_name()) {
			audio_driver_idx = i;
			break;
		}
	}

	if (audio_driver_idx < 0) {
		// If the requested driver wasn't found, pick the first entry.
		// If all else failed it would be the dummy driver (no sound).
		audio_driver_idx = 0;
	}

	if (Engine::get_singleton()->get_write_movie_path() != String()) {
		// Always use dummy driver for audio driver (which is last), also in no threaded mode.
		audio_driver_idx = AudioDriverManager::get_driver_count() - 1;
		AudioDriverDummy::get_dummy_singleton()->set_use_threads(false);
	}

	{
		window_orientation = DisplayServer::ScreenOrientation(int(GLOBAL_DEF_BASIC("display/window/handheld/orientation", DisplayServer::ScreenOrientation::SCREEN_LANDSCAPE)));
	}
	{
		window_vsync_mode = DisplayServer::VSyncMode(int(GLOBAL_DEF_BASIC("display/window/vsync/vsync_mode", DisplayServer::VSyncMode::VSYNC_ENABLED)));
		if (disable_vsync) {
			window_vsync_mode = DisplayServer::VSyncMode::VSYNC_DISABLED;
		}
	}

	GLOBAL_DEF_RST(PropertyInfo(Variant::INT, "audio/driver/output_latency", PROPERTY_HINT_RANGE, "1,100,1"), 15);
	// Use a safer default output_latency for web to avoid audio cracking on low-end devices, especially mobile.
	GLOBAL_DEF_RST("audio/driver/output_latency.web", 50);

	Engine::get_singleton()->set_audio_output_latency(GLOBAL_GET("audio/driver/output_latency"));

#if defined(MACOS_ENABLED) || defined(IOS_ENABLED)
	OS::get_singleton()->set_environment("MVK_CONFIG_LOG_LEVEL", OS::get_singleton()->_verbose_stdout ? "3" : "1"); // 1 = Errors only, 3 = Info
#endif

	if (frame_delay == 0) {
		frame_delay = GLOBAL_DEF(PropertyInfo(Variant::INT, "application/run/frame_delay_msec", PROPERTY_HINT_RANGE, "0,100,1,or_greater"), 0);
		if (Engine::get_singleton()->is_editor_hint()) {
			frame_delay = 0;
		}
	}

	if (audio_output_latency >= 1) {
		Engine::get_singleton()->set_audio_output_latency(audio_output_latency);
	}

	GLOBAL_DEF("display/window/ios/allow_high_refresh_rate", true);
	GLOBAL_DEF("display/window/ios/hide_home_indicator", true);
	GLOBAL_DEF("display/window/ios/hide_status_bar", true);
	GLOBAL_DEF("display/window/ios/suppress_ui_gesture", true);

#ifndef _3D_DISABLED
	// XR project settings.
	GLOBAL_DEF_RST_BASIC("xr/openxr/enabled", false);
	GLOBAL_DEF(PropertyInfo(Variant::STRING, "xr/openxr/target_api_version"), "");
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::STRING, "xr/openxr/default_action_map", PROPERTY_HINT_FILE, "*.tres"), "res://openxr_action_map.tres");
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::INT, "xr/openxr/form_factor", PROPERTY_HINT_ENUM, "Head Mounted,Handheld"), "0");
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::INT, "xr/openxr/view_configuration", PROPERTY_HINT_ENUM, "Mono,Stereo"), "1"); // "Mono,Stereo,Quad,Observer"
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::INT, "xr/openxr/reference_space", PROPERTY_HINT_ENUM, "Local,Stage,Local Floor"), "1");
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::INT, "xr/openxr/environment_blend_mode", PROPERTY_HINT_ENUM, "Opaque,Additive,Alpha"), "0");
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::INT, "xr/openxr/foveation_level", PROPERTY_HINT_ENUM, "Off,Low,Medium,High"), "0");
	GLOBAL_DEF_BASIC("xr/openxr/foveation_dynamic", false);

	GLOBAL_DEF_BASIC("xr/openxr/submit_depth_buffer", false);
	GLOBAL_DEF_BASIC("xr/openxr/startup_alert", true);

	// OpenXR project extensions settings.
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::INT, "xr/openxr/extensions/debug_utils", PROPERTY_HINT_ENUM, "Disabled,Error,Warning,Info,Verbose"), "0");
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::INT, "xr/openxr/extensions/debug_message_types", PROPERTY_HINT_FLAGS, "General,Validation,Performance,Conformance"), "15");
	GLOBAL_DEF_BASIC("xr/openxr/extensions/frame_synthesis", false);
	GLOBAL_DEF_BASIC("xr/openxr/extensions/hand_tracking", false);
	GLOBAL_DEF_BASIC("xr/openxr/extensions/hand_tracking_unobstructed_data_source", false); // XR_HAND_TRACKING_DATA_SOURCE_UNOBSTRUCTED_EXT
	GLOBAL_DEF_BASIC("xr/openxr/extensions/hand_tracking_controller_data_source", false); // XR_HAND_TRACKING_DATA_SOURCE_CONTROLLER_EXT
	GLOBAL_DEF_RST_BASIC("xr/openxr/extensions/hand_interaction_profile", false);
	GLOBAL_DEF_BASIC("xr/openxr/extensions/spatial_entity/enabled", false);
	GLOBAL_DEF_BASIC("xr/openxr/extensions/spatial_entity/enable_spatial_anchors", false);
	GLOBAL_DEF_BASIC("xr/openxr/extensions/spatial_entity/enable_persistent_anchors", false);
	GLOBAL_DEF_BASIC("xr/openxr/extensions/spatial_entity/enable_builtin_anchor_detection", false);
	GLOBAL_DEF_BASIC("xr/openxr/extensions/spatial_entity/enable_plane_tracking", false);
	GLOBAL_DEF_BASIC("xr/openxr/extensions/spatial_entity/enable_builtin_plane_detection", false);
	GLOBAL_DEF_BASIC("xr/openxr/extensions/spatial_entity/enable_marker_tracking", false);
	GLOBAL_DEF_BASIC("xr/openxr/extensions/spatial_entity/enable_builtin_marker_tracking", false);
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::INT, "xr/openxr/extensions/spatial_entity/aruco_dict", PROPERTY_HINT_ENUM, "4x4 50 IDs,4x4 100 IDs,4x4 250 IDs,4x4 1000 IDs,5x5 50 IDs,5x5 100 IDs,5x5 250 IDs,5x5 1000 IDs,6x6 50 IDs,6x6 100 IDs,6x6 250 IDs,6x6 1000 IDs,7x7 50 IDs,7x7 100 IDs,7x7 250 IDs,7x7 1000 IDs"), "15");
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::INT, "xr/openxr/extensions/spatial_entity/april_tag_dict", PROPERTY_HINT_ENUM, "4x4H5,5x5H9,6x6H10,6x6H11"), "3");
	GLOBAL_DEF_RST_BASIC("xr/openxr/extensions/eye_gaze_interaction", false);
	GLOBAL_DEF_BASIC("xr/openxr/extensions/render_model", false);
	GLOBAL_DEF_BASIC("xr/openxr/extensions/user_presence", false);

	// OpenXR Binding modifier settings
	GLOBAL_DEF_BASIC("xr/openxr/binding_modifiers/analog_threshold", false);
	GLOBAL_DEF_RST_BASIC("xr/openxr/binding_modifiers/dpad_binding", false);

#ifdef TOOLS_ENABLED
	// Disabled for now, using XR inside of the editor we'll be working on during the coming months.

	// editor settings (it seems we're too early in the process when setting up rendering, to access editor settings...)
	// EDITOR_DEF_RST("xr/openxr/in_editor", false);
	// GLOBAL_DEF("xr/openxr/in_editor", false);
#endif // TOOLS_ENABLED
#endif // _3D_DISABLED

	Engine::get_singleton()->set_frame_delay(frame_delay);

	message_queue = memnew(MessageQueue);

	Thread::release_main_thread(); // If setup2() is called from another thread, that one will become main thread, so preventively release this one.
	set_current_thread_safe_for_nodes(false);

#if defined(STEAMAPI_ENABLED)
	if (editor) {
		steam_tracker = memnew(SteamTracker);
	}
#endif

	OS::get_singleton()->benchmark_end_measure("Startup", "Main::Setup");

	if (p_second_phase) {
		exit_err = setup2();
		if (exit_err != OK) {
			goto error;
		}
	}

	return OK;

error:

	text_driver = "";
	display_driver = "";
	audio_driver = "";
	tablet_driver = "";
	Engine::get_singleton()->set_write_movie_path(String());
	project_path = "";

	args.clear();
	main_args.clear();

	if (show_help) {
		print_help(execpath);
	}

	if (editor) {
		OS::get_singleton()->remove_lock_file();
	}

	EngineDebugger::deinitialize();

	if (performance) {
		memdelete(performance);
	}
	if (input_map) {
		memdelete(input_map);
	}
	if (translation_server) {
		memdelete(translation_server);
	}
	if (globals) {
		memdelete(globals);
	}
	if (packed_data) {
		memdelete(packed_data);
	}

	unregister_core_driver_types();
	unregister_core_extensions();

	if (engine) {
		memdelete(engine);
	}

	unregister_core_types();

	OS::get_singleton()->_cmdline.clear();
	OS::get_singleton()->_user_args.clear();

	if (message_queue) {
		memdelete(message_queue);
	}

	OS::get_singleton()->benchmark_end_measure("Startup", "Main::Setup");

#if defined(STEAMAPI_ENABLED)
	if (steam_tracker) {
		memdelete(steam_tracker);
	}
#endif

	OS::get_singleton()->finalize_core();
	locale = String();

	return exit_err;
}

Error _parse_resource_dummy(void *p_data, VariantParser::Stream *p_stream, Ref<Resource> &r_res, int &line, String &r_err_str) {
	VariantParser::Token token;
	VariantParser::get_token(p_stream, token, line, r_err_str);
	if (token.type != VariantParser::TK_NUMBER && token.type != VariantParser::TK_STRING) {
		r_err_str = "Expected number (old style sub-resource index) or String (ext-resource ID)";
		return ERR_PARSE_ERROR;
	}

	r_res.unref();

	VariantParser::get_token(p_stream, token, line, r_err_str);
	if (token.type != VariantParser::TK_PARENTHESIS_CLOSE) {
		r_err_str = "Expected ')'";
		return ERR_PARSE_ERROR;
	}

	return OK;
}

Error Main::setup2(bool p_show_boot_logo) {
	FoundryProfileZone("setup2");
	OS::get_singleton()->benchmark_begin_measure("Startup", "Main::Setup2");

	Thread::make_main_thread(); // Make whatever thread call this the main thread.
	set_current_thread_safe_for_nodes(true);

	// Don't use rich formatting to prevent ANSI escape codes from being written to log files.
	print_header(false);

#ifdef TOOLS_ENABLED
	int accessibility_mode_editor = 0;
	int tablet_driver_editor = -1;
	if (editor || cmdline_tool) {
		OS::get_singleton()->benchmark_begin_measure("Startup", "Initialize Early Settings");

		EditorPaths::create();

		// Editor setting class is not available, load config directly.
		if (!init_use_custom_screen && editor && EditorPaths::get_singleton()->are_paths_valid()) {
			ERR_FAIL_COND_V(!DirAccess::dir_exists_absolute(EditorPaths::get_singleton()->get_config_dir()), FAILED);

			String config_file_path = EditorSettings::get_existing_settings_path();
			if (FileAccess::exists(config_file_path)) {
				Error err;
				Ref<FileAccess> f = FileAccess::open(config_file_path, FileAccess::READ, &err);
				if (f.is_valid()) {
					VariantParser::StreamFile stream;
					stream.f = f;

					String assign;
					Variant value;
					VariantParser::Tag next_tag;

					int lines = 0;
					String error_text;

					VariantParser::ResourceParser rp_new;
					rp_new.ext_func = _parse_resource_dummy;
					rp_new.sub_func = _parse_resource_dummy;

					bool screen_found = false;
					String screen_property;

					bool prefer_wayland_found = false;
					bool prefer_wayland = false;

					bool tablet_found = false;

					bool ac_found = false;

					if (editor) {
						screen_property = "interface/editor/editor_screen";
					} else {
						// Skip.
						screen_found = true;
					}

					if (!display_driver.is_empty()) {
						// Skip.
						prefer_wayland_found = true;
					}

					while (!screen_found || !init_expand_to_title_found || !init_display_scale_found || !init_custom_scale_found || !prefer_wayland_found || !tablet_found || !ac_found) {
						assign = Variant();
						next_tag.fields.clear();
						next_tag.name = String();

						err = VariantParser::parse_tag_assign_eof(&stream, lines, error_text, next_tag, assign, value, &rp_new, true);
						if (err == ERR_FILE_EOF) {
							break;
						}

						if (err == OK && !assign.is_empty()) {
							if (!screen_found && assign == screen_property) {
								init_screen = value;
								screen_found = true;

								if (editor) {
									restore_editor_window_layout = value.operator int() == EditorSettings::InitialScreen::INITIAL_SCREEN_AUTO;
								}
							}
							if (!ac_found && assign == "interface/accessibility/accessibility_support") {
								accessibility_mode_editor = value;
								ac_found = true;
							} else if (!init_expand_to_title_found && assign == "interface/editor/expand_to_title") {
								init_expand_to_title = value;
								init_expand_to_title_found = true;
							} else if (!init_display_scale_found && assign == "interface/editor/display_scale") {
								init_display_scale = value;
								init_display_scale_found = true;
							} else if (!init_custom_scale_found && assign == "interface/editor/custom_display_scale") {
								init_custom_scale = value;
								init_custom_scale_found = true;
							} else if (!prefer_wayland_found && assign == "run/platforms/linuxbsd/prefer_wayland") {
								if (!OS::get_singleton()->get_environment("WAYLAND_DISPLAY").is_empty()) {
									// Do not prefer Wayland if not currently on a Wayland session.
									// This avoids error messages on startup when currently on X11
									// and the Prefer Wayland setting is enabled.
									prefer_wayland = value;
								}
								prefer_wayland_found = true;
							} else if (!tablet_found && assign == "interface/editor/tablet_driver") {
								tablet_driver_editor = value;
								tablet_found = true;
							}
						}
					}

					if (display_driver.is_empty()) {
						if (prefer_wayland) {
							display_driver = "wayland";
						} else {
							display_driver = "default";
						}
					}
				}
			}
		}

		if (found_project && EditorPaths::get_singleton()->is_self_contained()) {
			if (ProjectSettings::get_singleton()->get_resource_path() == OS::get_singleton()->get_executable_path().get_base_dir()) {
				ERR_PRINT("You are trying to run a self-contained editor at the same location as a project. This is not allowed, since editor files will mix with project files.");
				OS::get_singleton()->set_exit_code(EXIT_FAILURE);
				return FAILED;
			}
		}

		// Record a successful interactive editor open into the global known-project store
		// (#1135) so the next launch can auto-open it and the projectless recents list
		// reflects it. Requires an eligible editor launch, an actual editor session
		// (`editor`), and no explicit-path error: command-line editor tools (export/import)
		// and background editor-mode services (`lsp serve`) must not reorder the user's GUI
		// recents; a bare launch that runs the working-directory project as a game
		// (editor == false) must not either; and an invalid `--project` that still resolved
		// an ambient cwd project via upward search must not record that unintended project.
		if (interactive_editor_launch && editor && found_project && !foundry_cli_project_path_error &&
				EditorPaths::get_singleton()->are_paths_valid()) {
			KnownProjectStore known_projects;
			known_projects.load();
			StartupRouter::record_project_opened(known_projects, ProjectSettings::get_singleton()->get_resource_path());
		}

		bool has_command_line_window_override = init_use_custom_pos || init_use_custom_screen || init_windowed;
		if (editor && !has_command_line_window_override && restore_editor_window_layout) {
			Ref<ConfigFile> config;
			config.instantiate();
			// Load and amend existing config if it exists.
			const String layout_path = projectless_editor_shell
					? EditorPaths::get_singleton()->get_data_dir().path_join("projectless_editor_layout.cfg")
					: EditorPaths::get_singleton()->get_project_settings_dir().path_join("editor_layout.cfg");
			Error err = config->load(layout_path);
			if (err == OK) {
				init_screen = config->get_value("EditorWindow", "screen", init_screen);
				String mode = config->get_value("EditorWindow", "mode", "maximized");
				window_size = config->get_value("EditorWindow", "size", window_size);
				if (mode == "windowed") {
					window_mode = DisplayServer::WINDOW_MODE_WINDOWED;
					init_windowed = true;
				} else if (mode == "fullscreen") {
					window_mode = DisplayServer::WINDOW_MODE_FULLSCREEN;
					init_fullscreen = true;
				} else {
					window_mode = DisplayServer::WINDOW_MODE_MAXIMIZED;
					init_maximized = true;
				}

				if (init_windowed) {
					init_use_custom_pos = true;
					init_custom_pos = config->get_value("EditorWindow", "position", Vector2i(0, 0));
				}
			}
		}

		if (init_screen == EditorSettings::InitialScreen::INITIAL_SCREEN_AUTO) {
			init_screen = DisplayServer::SCREEN_PRIMARY;
		}

		OS::get_singleton()->benchmark_end_measure("Startup", "Initialize Early Settings");
	}
#endif

	OS::get_singleton()->benchmark_begin_measure("Startup", "Servers");

	tsman = memnew(TextServerManager);
	if (tsman) {
		Ref<TextServerDummy> ts;
		ts.instantiate();
		tsman->add_interface(ts);
	}

#ifndef PHYSICS_3D_DISABLED
	physics_server_3d_manager = memnew(PhysicsServer3DManager);
#endif // PHYSICS_3D_DISABLED
#ifndef PHYSICS_2D_DISABLED
	physics_server_2d_manager = memnew(PhysicsServer2DManager);
#endif // PHYSICS_2D_DISABLED

#ifndef NAVIGATION_2D_DISABLED
	NavigationServer2DManager::initialize_server_manager();
#endif // NAVIGATION_2D_DISABLED
#ifndef NAVIGATION_3D_DISABLED
	NavigationServer3DManager::initialize_server_manager();
#endif // NAVIGATION_3D_DISABLED

	register_server_types();
	{
		OS::get_singleton()->benchmark_begin_measure("Servers", "Modules and Extensions");

		initialize_modules(MODULE_INITIALIZATION_LEVEL_SERVERS);
		FoundryExtensionManager::get_singleton()->initialize_extensions(FoundryExtension::INITIALIZATION_LEVEL_SERVERS);

		OS::get_singleton()->benchmark_end_measure("Servers", "Modules and Extensions");
	}

	/* Initialize Input */

	{
		OS::get_singleton()->benchmark_begin_measure("Servers", "Input");

		input = memnew(Input);
		OS::get_singleton()->initialize_joypads();

		OS::get_singleton()->benchmark_end_measure("Servers", "Input");
	}

	/* Initialize Display Server */

	{
		OS::get_singleton()->benchmark_begin_measure("Servers", "Display");

		if (display_driver.is_empty()) {
			display_driver = GLOBAL_GET("display/display_server/driver");
		}

		int display_driver_idx = -1;

		if (display_driver.is_empty() || display_driver == "default") {
			display_driver_idx = 0;
		} else {
			for (int i = 0; i < DisplayServer::get_create_function_count(); i++) {
				String name = DisplayServer::get_create_function_name(i);
				if (display_driver == name) {
					display_driver_idx = i;
					break;
				}
			}

			if (display_driver_idx < 0) {
				// If the requested driver wasn't found, pick the first entry.
				// If all else failed it would be the headless server.
				display_driver_idx = 0;
			}
		}

		Vector2i *window_position = nullptr;
		Vector2i position = init_custom_pos;
		if (init_use_custom_pos) {
			window_position = &position;
		}

		Color boot_bg_color = GLOBAL_DEF_BASIC("application/boot_splash/bg_color", boot_splash_bg_color);
		DisplayServer::set_early_window_clear_color_override(true, boot_bg_color);

		DisplayServer::Context context;
		if (editor) {
			context = DisplayServer::CONTEXT_EDITOR;
		} else {
			context = DisplayServer::CONTEXT_ENGINE;
		}

		if (init_embed_parent_window_id) {
			// Reset flags and other settings to be sure it's borderless and windowed. The position and size should have been initialized correctly
			// from --position and --resolution parameters.
			window_mode = DisplayServer::WINDOW_MODE_WINDOWED;
			window_flags = DisplayServer::WINDOW_FLAG_BORDERLESS_BIT;
			if (bool(GLOBAL_GET("display/window/size/transparent"))) {
				window_flags |= DisplayServer::WINDOW_FLAG_TRANSPARENT_BIT;
			}
		}

#ifdef TOOLS_ENABLED
		if (editor && init_expand_to_title) {
			window_flags |= DisplayServer::WINDOW_FLAG_EXTEND_TO_TITLE_BIT;
		}
#endif

		if (!accessibility_mode_set) {
#ifdef TOOLS_ENABLED
			if (editor || cmdline_tool) {
				accessibility_mode = (AccessibilityServerEnums::AccessibilityMode)accessibility_mode_editor;
			} else {
#else
			{
#endif
				accessibility_mode = (AccessibilityServerEnums::AccessibilityMode)GLOBAL_GET("accessibility/general/accessibility_support").operator int64_t();
			}
		}
		if (accessibility_driver_name.is_empty()) {
			if (!editor) {
				accessibility_driver_name = GLOBAL_GET("accessibility/general/accessibility_driver");
			} else {
				accessibility_driver_name = "accesskit";
			}
		}
		if (display_driver == NULL_DISPLAY_DRIVER || display_driver == EMBEDDED_DISPLAY_DRIVER || accessibility_mode == AccessibilityServerEnums::AccessibilityMode::ACCESSIBILITY_DISABLED) {
			accessibility_driver_name = "dummy";
		}
		int accessibility_driver_idx = -1;

		if (accessibility_driver_name.is_empty() || accessibility_driver_name == "default") {
			accessibility_driver_idx = 0;
		} else {
			for (int i = 0; i < AccessibilityServer::get_create_function_count(); i++) {
				String name = AccessibilityServer::get_create_function_name(i);
				if (accessibility_driver_name == name) {
					accessibility_driver_idx = i;
					break;
				}
			}

			if (accessibility_driver_idx < 0) {
				// If the requested driver wasn't found, pick the first entry.
				// If all else failed it would be the headless server.
				accessibility_driver_idx = 0;
			}
		}

		Error err;
		accessibility_server = AccessibilityServer::create(accessibility_driver_idx, err);
		if (err != OK || accessibility_server == nullptr) {
			String last_name = AccessibilityServer::get_create_function_name(accessibility_driver_idx);

			for (int i = 0; i < AccessibilityServer::get_create_function_count(); i++) {
				if (i == accessibility_driver_idx) {
					continue; // Don't try the same twice.
				}
				String name = AccessibilityServer::get_create_function_name(i);
				WARN_VERBOSE(vformat("Accessibility driver %s failed, falling back to %s.", last_name, name));

				accessibility_server = AccessibilityServer::create(i, err);
				if (err == OK && accessibility_server != nullptr) {
					break;
				}
			}
		}
		accessibility_server->set_mode(accessibility_mode);

		// rendering_driver now held in static global String in main and initialized in setup()
		display_server = DisplayServer::create(display_driver_idx, rendering_driver, window_mode, window_vsync_mode, window_flags, window_position, window_size, init_screen, context, init_embed_parent_window_id, err);
		if (err != OK || display_server == nullptr) {
			String last_name = DisplayServer::get_create_function_name(display_driver_idx);

			// We can't use this display server, try other ones as fallback.
			// Skip headless (always last registered) because that's not what users
			// would expect if they didn't request it explicitly.
			for (int i = 0; i < DisplayServer::get_create_function_count() - 1; i++) {
				if (i == display_driver_idx) {
					continue; // Don't try the same twice.
				}
				String name = DisplayServer::get_create_function_name(i);
				WARN_PRINT(vformat("Display driver %s failed, falling back to %s.", last_name, name));

				display_server = DisplayServer::create(i, rendering_driver, window_mode, window_vsync_mode, window_flags, window_position, window_size, init_screen, context, init_embed_parent_window_id, err);
				if (err == OK && display_server != nullptr) {
					break;
				}
			}
		}

		if (err != OK || display_server == nullptr) {
			ERR_PRINT("Unable to create DisplayServer, all display drivers failed.\nUse \"--headless\" command line argument to run the engine in headless mode if this is desired (e.g. for continuous integration).");

			if (display_server) {
				memdelete(display_server);
			}

			FoundryExtensionManager::get_singleton()->deinitialize_extensions(FoundryExtension::INITIALIZATION_LEVEL_SERVERS);
			uninitialize_modules(MODULE_INITIALIZATION_LEVEL_SERVERS);
			unregister_server_types();

			if (input) {
				memdelete(input);
			}
			if (tsman) {
				memdelete(tsman);
			}
#ifndef PHYSICS_3D_DISABLED
			if (physics_server_3d_manager) {
				memdelete(physics_server_3d_manager);
			}
#endif // PHYSICS_3D_DISABLED
#ifndef PHYSICS_2D_DISABLED
			if (physics_server_2d_manager) {
				memdelete(physics_server_2d_manager);
			}
#endif // PHYSICS_2D_DISABLED

			return err;
		}

		if (display_server->has_feature(DisplayServer::FEATURE_SUBWINDOWS)) {
			display_server->show_window(DisplayServer::MAIN_WINDOW_ID);
		}

		if (display_server->has_feature(DisplayServer::FEATURE_ORIENTATION)) {
			display_server->screen_set_orientation(window_orientation);
		}

		OS::get_singleton()->benchmark_end_measure("Servers", "Display");
	}

	// Max FPS needs to be set after the DisplayServer is created.
	RenderingDevice *rd = RenderingDevice::get_singleton();
	if (rd) {
		rd->_set_max_fps(engine->get_max_fps());
	}

#ifdef TOOLS_ENABLED
	// If the editor is running in windowed mode, ensure the window rect fits
	// the screen in case screen count or position has changed.
	if (editor && init_windowed) {
		// We still need to check we are actually in windowed mode, because
		// certain platform might only support one fullscreen window.
		if (DisplayServer::get_singleton()->window_get_mode() == DisplayServer::WINDOW_MODE_WINDOWED) {
			Vector2i current_size = DisplayServer::get_singleton()->window_get_size();
			Vector2i current_pos = DisplayServer::get_singleton()->window_get_position();
			int screen = DisplayServer::get_singleton()->window_get_current_screen();
			Rect2i screen_rect = DisplayServer::get_singleton()->screen_get_usable_rect(screen);

			Vector2i adjusted_end = screen_rect.get_end().min(current_pos + current_size);
			Vector2i adjusted_pos = screen_rect.get_position().max(adjusted_end - current_size);
			Vector2i adjusted_size = DisplayServer::get_singleton()->window_get_min_size().max(adjusted_end - adjusted_pos);

			if (current_pos != adjusted_end || current_size != adjusted_size) {
				DisplayServer::get_singleton()->window_set_position(adjusted_pos);
				DisplayServer::get_singleton()->window_set_size(adjusted_size);
			}
		}
	}
#endif

	if (GLOBAL_GET("debug/settings/stdout/print_fps") || print_fps) {
		// Print requested V-Sync mode at startup to diagnose the printed FPS not going above the monitor refresh rate.
		switch (window_vsync_mode) {
			case DisplayServer::VSyncMode::VSYNC_DISABLED:
				print_line("Requested V-Sync mode: Disabled");
				break;
			case DisplayServer::VSyncMode::VSYNC_ENABLED:
				print_line("Requested V-Sync mode: Enabled - FPS will likely be capped to the monitor refresh rate.");
				break;
			case DisplayServer::VSyncMode::VSYNC_ADAPTIVE:
				print_line("Requested V-Sync mode: Adaptive");
				break;
			case DisplayServer::VSyncMode::VSYNC_MAILBOX:
				print_line("Requested V-Sync mode: Mailbox");
				break;
		}
	}

	if (OS::get_singleton()->_separate_thread_render) {
		WARN_PRINT("The separate rendering thread feature is experimental. Feel free to try it since it will eventually become a stable feature.\n"
				   "However, bear in mind that at the moment it can lead to project crashes or instability.\n"
				   "So, unless you want to test the engine, set the \"rendering/driver/threads/thread_model\" project setting to 'Safe'.");
	}

	/* Initialize Pen Tablet Driver */

	{
		OS::get_singleton()->benchmark_begin_measure("Servers", "Tablet Driver");

		GLOBAL_DEF_RST_NOVAL("input_devices/pen_tablet/driver", "");
		GLOBAL_DEF_RST_NOVAL(PropertyInfo(Variant::STRING, "input_devices/pen_tablet/driver.windows", PROPERTY_HINT_ENUM, "auto,winink,wintab,dummy"), "");

#ifdef TOOLS_ENABLED
		if (tablet_driver.is_empty() && tablet_driver_editor != -1) {
			tablet_driver = DisplayServer::get_singleton()->tablet_get_driver_name(tablet_driver_editor);
		}
#endif

		if (tablet_driver.is_empty()) { // specified in project.foundry
			tablet_driver = GLOBAL_GET("input_devices/pen_tablet/driver");
			if (tablet_driver.is_empty()) {
				tablet_driver = DisplayServer::get_singleton()->tablet_get_driver_name(0);
			}
		}

		for (int i = 0; i < DisplayServer::get_singleton()->tablet_get_driver_count(); i++) {
			if (tablet_driver == DisplayServer::get_singleton()->tablet_get_driver_name(i)) {
				DisplayServer::get_singleton()->tablet_set_current_driver(DisplayServer::get_singleton()->tablet_get_driver_name(i));
				break;
			}
		}

		if (DisplayServer::get_singleton()->tablet_get_current_driver().is_empty()) {
			DisplayServer::get_singleton()->tablet_set_current_driver(DisplayServer::get_singleton()->tablet_get_driver_name(0));
		}

		print_verbose("Using \"" + DisplayServer::get_singleton()->tablet_get_current_driver() + "\" pen tablet driver...");

		OS::get_singleton()->benchmark_end_measure("Servers", "Tablet Driver");
	}

	/* Initialize Rendering Server */

	{
		OS::get_singleton()->benchmark_begin_measure("Servers", "Rendering");

		rendering_server = memnew(RenderingServerDefault(OS::get_singleton()->is_separate_thread_rendering_enabled()));

		rendering_server->init();
		//rendering_server->call_set_use_vsync(OS::get_singleton()->_use_vsync);
		rendering_server->set_render_loop_enabled(!disable_render_loop);

		if (profile_gpu || (!editor && bool(GLOBAL_GET("debug/settings/stdout/print_gpu_profile")))) {
			rendering_server->set_print_gpu_profile(true);
		}

		OS::get_singleton()->benchmark_end_measure("Servers", "Rendering");
	}

#ifdef UNIX_ENABLED
	// Print warning after initializing the renderer but before initializing audio.
	if (OS::get_singleton()->get_environment("USER") == "root" && !OS::get_singleton()->has_environment("FOUNDRY_SILENCE_ROOT_WARNING")) {
		WARN_PRINT("Started the engine as `root`/superuser. This is a security risk, and subsystems like audio may not work correctly.\nSet the environment variable `FOUNDRY_SILENCE_ROOT_WARNING` to 1 to silence this warning.");
	}
#endif

	/* Initialize Audio Driver */

	{
		OS::get_singleton()->benchmark_begin_measure("Servers", "Audio");

		AudioDriverManager::initialize(audio_driver_idx);

		// Right moment to create and initialize the audio server.
		audio_server = memnew(AudioServer);
		audio_server->init();

		OS::get_singleton()->benchmark_end_measure("Servers", "Audio");
	}

#ifndef XR_DISABLED
	/* Initialize XR Server */

	{
		OS::get_singleton()->benchmark_begin_measure("Servers", "XR");

		xr_server = memnew(XRServer);

		OS::get_singleton()->benchmark_end_measure("Servers", "XR");
	}
#endif // XR_DISABLED

	OS::get_singleton()->benchmark_end_measure("Startup", "Servers");

#ifndef WEB_ENABLED
	// Add a blank line for readability.
	Engine::get_singleton()->print_header("");
#endif // WEB_ENABLED

	register_core_singletons();

	/* Initialize the main window and boot screen */

	{
		OS::get_singleton()->benchmark_begin_measure("Startup", "Setup Window and Boot");

		MAIN_PRINT("Main: Setup Logo");

		if (!init_embed_parent_window_id) {
			if (init_windowed) {
				//do none..
			} else if (init_maximized) {
				DisplayServer::get_singleton()->window_set_mode(DisplayServer::WINDOW_MODE_MAXIMIZED);
			} else if (init_fullscreen) {
				DisplayServer::get_singleton()->window_set_mode(DisplayServer::WINDOW_MODE_FULLSCREEN);
			}
			if (init_always_on_top) {
				DisplayServer::get_singleton()->window_set_flag(DisplayServer::WINDOW_FLAG_ALWAYS_ON_TOP, true);
			}
		}

		Color clear = GLOBAL_DEF_BASIC("rendering/environment/defaults/default_clear_color", Color(0.3, 0.3, 0.3));
		RenderingServer::get_singleton()->set_default_clear_color(clear);

		if (p_show_boot_logo) {
			setup_boot_logo();
		}

		MAIN_PRINT("Main: Clear Color");

		DisplayServer::set_early_window_clear_color_override(false);

		GLOBAL_DEF_BASIC(PropertyInfo(Variant::STRING, "application/config/icon", PROPERTY_HINT_FILE, "*.png,*.bmp,*.hdr,*.jpg,*.jpeg,*.svg,*.tga,*.exr,*.webp"), String());
		GLOBAL_DEF(PropertyInfo(Variant::STRING, "application/config/macos_native_icon", PROPERTY_HINT_FILE, "*.icns"), String());
		GLOBAL_DEF(PropertyInfo(Variant::STRING, "application/config/windows_native_icon", PROPERTY_HINT_FILE, "*.ico"), String());

		MAIN_PRINT("Main: Touch Input");

		Input *id = Input::get_singleton();
		if (id) {
			bool agile_input_event_flushing = GLOBAL_DEF("input_devices/buffering/agile_event_flushing", false);
			id->set_agile_input_event_flushing(agile_input_event_flushing);

			if (bool(GLOBAL_DEF_BASIC("input_devices/pointing/emulate_touch_from_mouse", false)) &&
					!editor) {
				if (!DisplayServer::get_singleton()->is_touchscreen_available()) {
					//only if no touchscreen ui hint, set emulation
					id->set_emulate_touch_from_mouse(true);
				}
			}

			id->set_emulate_mouse_from_touch(bool(GLOBAL_DEF_BASIC("input_devices/pointing/emulate_mouse_from_touch", true)));

			if (editor) {
				id->set_emulate_mouse_from_touch(true);
			}
		}

		OS::get_singleton()->benchmark_end_measure("Startup", "Setup Window and Boot");
	}

	MAIN_PRINT("Main: Load Translations and Remaps");

	/* Setup translations and remaps */

	{
		OS::get_singleton()->benchmark_begin_measure("Startup", "Translations and Remaps");

		translation_server->setup(); //register translations, load them, etc.
		if (!locale.is_empty()) {
			translation_server->set_locale(locale);
		}
		translation_server->load_project_translations(translation_server->get_main_domain());
		ResourceLoader::load_translation_remaps(); //load remaps for resources

		OS::get_singleton()->benchmark_end_measure("Startup", "Translations and Remaps");
	}

	MAIN_PRINT("Main: Load TextServer");

	/* Setup Text Server */

	{
		OS::get_singleton()->benchmark_begin_measure("Startup", "Text Server");

		/* Enum text drivers */
		GLOBAL_DEF_RST("internationalization/rendering/text_driver", "");
		String text_driver_options;
		for (int i = 0; i < TextServerManager::get_singleton()->get_interface_count(); i++) {
			const String driver_name = TextServerManager::get_singleton()->get_interface(i)->get_name();
			if (driver_name == "Dummy") {
				// Dummy text driver cannot draw any text, making the editor unusable if selected.
				continue;
			}
			if (!text_driver_options.is_empty() && !text_driver_options.contains_char(',')) {
				// Not the first option; add a comma before it as a separator for the property hint.
				text_driver_options += ",";
			}
			text_driver_options += driver_name;
		}
		ProjectSettings::get_singleton()->set_custom_property_info(PropertyInfo(Variant::STRING, "internationalization/rendering/text_driver", PROPERTY_HINT_ENUM, text_driver_options));

		/* Determine text driver */
		if (text_driver.is_empty()) {
			text_driver = GLOBAL_GET("internationalization/rendering/text_driver");
		}

		if (!text_driver.is_empty()) {
			/* Load user selected text server. */
			for (int i = 0; i < TextServerManager::get_singleton()->get_interface_count(); i++) {
				if (TextServerManager::get_singleton()->get_interface(i)->get_name() == text_driver) {
					text_driver_idx = i;
					break;
				}
			}
		}

		if (text_driver_idx < 0) {
			/* If not selected, use one with the most features available. */
			int max_features = 0;
			for (int i = 0; i < TextServerManager::get_singleton()->get_interface_count(); i++) {
				uint32_t features = TextServerManager::get_singleton()->get_interface(i)->get_features();
				int feature_number = 0;
				while (features) {
					feature_number += features & 1;
					features >>= 1;
				}
				if (feature_number >= max_features) {
					max_features = feature_number;
					text_driver_idx = i;
				}
			}
		}
		if (text_driver_idx >= 0) {
			Ref<TextServer> ts = TextServerManager::get_singleton()->get_interface(text_driver_idx);
			TextServerManager::get_singleton()->set_primary_interface(ts);
			if (ts->has_feature(TextServer::FEATURE_USE_SUPPORT_DATA)) {
				ts->load_support_data("res://" + ts->get_support_data_filename());
			}
		} else {
			ERR_FAIL_V_MSG(ERR_CANT_CREATE, "TextServer: Unable to create TextServer interface.");
		}

		OS::get_singleton()->benchmark_end_measure("Startup", "Text Server");
	}

	MAIN_PRINT("Main: Load Scene Types");

	OS::get_singleton()->benchmark_begin_measure("Startup", "Scene");

	// Initialize ThemeDB early so that scene types can register their theme items.
	// Default theme will be initialized later, after modules and ScriptServer are ready.
	initialize_theme_db();

#if !defined(NAVIGATION_2D_DISABLED) || !defined(NAVIGATION_3D_DISABLED)
	MAIN_PRINT("Main: Load Navigation");
#endif // !defined(NAVIGATION_2D_DISABLED) || !defined(NAVIGATION_3D_DISABLED)

#ifndef NAVIGATION_3D_DISABLED
	NavigationServer3DManager::initialize_server();
#endif // NAVIGATION_3D_DISABLED
#ifndef NAVIGATION_2D_DISABLED
	NavigationServer2DManager::initialize_server();
#endif // NAVIGATION_2D_DISABLED

	register_scene_types();
	register_driver_types();

	register_scene_singletons();

	{
		OS::get_singleton()->benchmark_begin_measure("Scene", "Modules and Extensions");

		initialize_modules(MODULE_INITIALIZATION_LEVEL_SCENE);
		FoundryExtensionManager::get_singleton()->initialize_extensions(FoundryExtension::INITIALIZATION_LEVEL_SCENE);

		OS::get_singleton()->benchmark_end_measure("Scene", "Modules and Extensions");

		// We need to initialize the movie writer here in case
		// one of the user-provided FoundryExtensions subclasses MovieWriter.
		if (Engine::get_singleton()->get_write_movie_path() != String()) {
			movie_writer = MovieWriter::find_writer_for_file(Engine::get_singleton()->get_write_movie_path());
			if (movie_writer == nullptr) {
				ERR_PRINT("Can't find movie writer for file type, aborting: " + Engine::get_singleton()->get_write_movie_path());
				Engine::get_singleton()->set_write_movie_path(String());
			}
		}
	}

	PackedStringArray extensions;
	extensions.push_back("gd");
	if (ClassDB::class_exists("CSharpScript")) {
		extensions.push_back("cs");
	}
	extensions.push_back("gdshader");
	GLOBAL_DEF_NOVAL(PropertyInfo(Variant::PACKED_STRING_ARRAY, "editor/script/search_in_file_extensions"), extensions); // Note: should be defined after Scene level modules init to see .NET.

	OS::get_singleton()->benchmark_end_measure("Startup", "Scene");

#ifdef TOOLS_ENABLED
	ClassDB::set_current_api(ClassDB::API_EDITOR);
	register_editor_types();

	{
		OS::get_singleton()->benchmark_begin_measure("Editor", "Modules and Extensions");

		initialize_modules(MODULE_INITIALIZATION_LEVEL_EDITOR);
		FoundryExtensionManager::get_singleton()->initialize_extensions(FoundryExtension::INITIALIZATION_LEVEL_EDITOR);

		OS::get_singleton()->benchmark_end_measure("Editor", "Modules and Extensions");
	}

	ClassDB::set_current_api(ClassDB::API_CORE);

#endif

	MAIN_PRINT("Main: Load Platforms");

	OS::get_singleton()->benchmark_begin_measure("Startup", "Platforms");

	register_platform_apis();

	OS::get_singleton()->benchmark_end_measure("Startup", "Platforms");

	GLOBAL_DEF_BASIC(PropertyInfo(Variant::STRING, "display/mouse_cursor/custom_image", PROPERTY_HINT_FILE, "*.png,*.bmp,*.hdr,*.jpg,*.jpeg,*.svg,*.tga,*.exr,*.webp"), String());
	GLOBAL_DEF_BASIC("display/mouse_cursor/custom_image_hotspot", Vector2());
	GLOBAL_DEF_BASIC("display/mouse_cursor/tooltip_position_offset", Point2(10, 10));

	if (String(GLOBAL_GET("display/mouse_cursor/custom_image")) != String()) {
		Ref<Texture2D> cursor = ResourceLoader::load(
				GLOBAL_GET("display/mouse_cursor/custom_image"));
		if (cursor.is_valid()) {
			Vector2 hotspot = GLOBAL_GET("display/mouse_cursor/custom_image_hotspot");
			Input::get_singleton()->set_custom_mouse_cursor(cursor, Input::CURSOR_ARROW, hotspot);
		}
	}

	OS::get_singleton()->benchmark_begin_measure("Startup", "Finalize Setup");

	camera_server = CameraServer::create();

	MAIN_PRINT("Main: Load Physics");

	initialize_physics();

	register_server_singletons();

	// This loads global classes, so it must happen before custom loaders and savers are registered
	ScriptServer::init_languages();

#if TOOLS_ENABLED

	// Setting up the callback to execute a scan for UIDs on disk when a UID
	// does not exist in the UID cache on startup. This prevents invalid UID errors
	// when opening a project without a UID cache file or with an invalid cache.
	if (editor) {
		ResourceUID::scan_for_uid_on_startup = EditorFileSystem::scan_for_uid;
	}

#endif

	theme_db->initialize_theme();
	audio_server->load_default_bus_layout();

#if defined(MODULE_MONO_ENABLED) && defined(TOOLS_ENABLED)
	// Hacky to have it here, but we don't have good facility yet to let modules
	// register command line options to call at the right time. This needs to happen
	// after init'ing the ScriptServer, but also after init'ing the ThemeDB,
	// for the C# docs generation in the bindings.
	List<String> cmdline_args = OS::get_singleton()->get_cmdline_args();
	BindingsGenerator::handle_cmdline_args(cmdline_args);
#endif

	if (use_debug_profiler && EngineDebugger::is_active()) {
		// Start the "scripts" profiler, used in local debugging.
		// We could add more, and make the CLI arg require a comma-separated list of profilers.
		EngineDebugger::get_singleton()->profiler_enable("scripts", true);
	}

	{
		// Now that the engine is able to load resources,
		// load the global shader variables.
		// If running on editor, don't load the textures because the editor
		// may want to import them first. Editor will reload those later.
		rendering_server->global_shader_parameters_load_settings(!editor);
	}

	OS::get_singleton()->benchmark_end_measure("Startup", "Finalize Setup");

	_start_success = true;

	ClassDB::set_current_api(ClassDB::API_NONE); //no more APIs are registered at this point

	print_verbose("CORE API HASH: " + uitos(ClassDB::get_api_hash(ClassDB::API_CORE)));
	print_verbose("EDITOR API HASH: " + uitos(ClassDB::get_api_hash(ClassDB::API_EDITOR)));
	MAIN_PRINT("Main: Done");

	OS::get_singleton()->benchmark_end_measure("Startup", "Main::Setup2");

	return OK;
}

void Main::setup_boot_logo() {
	FoundryProfileZone("setup_boot_logo");
	MAIN_PRINT("Main: Load Boot Image");

#if !defined(TOOLS_ENABLED) && defined(WEB_ENABLED)
	bool show_logo = false;
#else
	bool show_logo = true;
#endif

	if (show_logo) { //boot logo!
		const bool boot_logo_image = GLOBAL_DEF_BASIC("application/boot_splash/show_image", true);

		const RenderingServer::SplashStretchMode boot_stretch_mode = GLOBAL_DEF_BASIC(PropertyInfo(Variant::INT, "application/boot_splash/stretch_mode", PROPERTY_HINT_ENUM, "Disabled,Keep,Keep Width,Keep Height,Cover,Ignore"), 1);
		const bool boot_logo_filter = GLOBAL_DEF_BASIC("application/boot_splash/use_filter", true);
		String boot_logo_path = GLOBAL_DEF_BASIC(PropertyInfo(Variant::STRING, "application/boot_splash/image", PROPERTY_HINT_FILE, "*.png"), String());

		// If the UID cache is missing or invalid, it could be 'normal' for the UID to not exist in memory.
		// It's too soon to scan the project files since the ResourceFormatImporter is not loaded yet,
		// so to prevent printing errors, we will just skip the custom boot logo this time.
		if (boot_logo_path.begins_with("uid://")) {
			const ResourceUID::ID logo_id = ResourceUID::get_singleton()->text_to_id(boot_logo_path);
			if (ResourceUID::get_singleton()->has_id(logo_id)) {
				boot_logo_path = ResourceUID::get_singleton()->get_id_path(logo_id).strip_edges();
			} else {
				boot_logo_path = String();
			}
		}

		Ref<Image> boot_logo;

		if (boot_logo_image) {
			if (!boot_logo_path.is_empty()) {
				boot_logo.instantiate();
				Error load_err = ImageLoader::load_image(boot_logo_path, boot_logo);
				if (load_err) {
					String msg = (boot_logo_path.ends_with(".png") ? "" : "The only supported format is PNG.");
					ERR_PRINT("Non-existing or invalid boot splash at '" + boot_logo_path + +"'. " + msg + " Loading default splash.");
				}
			}
		} else {
			// Create a 1×1 transparent image. This will effectively hide the splash image.
			boot_logo.instantiate();
			boot_logo->initialize_data(1, 1, false, Image::FORMAT_RGBA8);
			boot_logo->set_pixel(0, 0, Color(0, 0, 0, 0));
		}

		Color boot_bg_color = GLOBAL_GET("application/boot_splash/bg_color");

#if defined(TOOLS_ENABLED) && !defined(NO_EDITOR_SPLASH)
		boot_bg_color = GLOBAL_DEF_BASIC("application/boot_splash/bg_color", editor ? boot_splash_editor_bg_color : boot_splash_bg_color);
#endif
		if (boot_logo.is_valid()) {
			RenderingServer::get_singleton()->set_boot_image_with_stretch(boot_logo, boot_bg_color, boot_stretch_mode, boot_logo_filter);

		} else {
#ifndef NO_DEFAULT_BOOT_LOGO
			MAIN_PRINT("Main: Create bootsplash");
#if defined(TOOLS_ENABLED) && !defined(NO_EDITOR_SPLASH)
			Ref<Image> splash = editor ? memnew(Image(boot_splash_editor_png)) : memnew(Image(boot_splash_png));
#else
				Ref<Image> splash = memnew(Image(boot_splash_png));
#endif

			MAIN_PRINT("Main: ClearColor");
			RenderingServer::get_singleton()->set_default_clear_color(boot_bg_color);
			MAIN_PRINT("Main: Image");
			RenderingServer::get_singleton()->set_boot_image_with_stretch(splash, boot_bg_color, RenderingServer::SPLASH_STRETCH_MODE_KEEP);
#endif
		}

#if defined(TOOLS_ENABLED) && defined(MACOS_ENABLED)
		if (DisplayServer::get_singleton()->has_feature(DisplayServer::FEATURE_ICON) && OS::get_singleton()->get_bundle_icon_path().is_empty()) {
			Ref<Image> icon = memnew(Image(app_icon_png));
			DisplayServer::get_singleton()->set_icon(icon);
		}
#endif
	}
	RenderingServer::get_singleton()->set_default_clear_color(
			GLOBAL_GET("rendering/environment/defaults/default_clear_color"));
}

String Main::get_rendering_driver_name() {
	return rendering_driver;
}

String Main::get_locale_override() {
	return locale;
}

// everything the main loop needs to know about frame timings
static MainTimerSync main_timer_sync;

// Return value should be EXIT_SUCCESS if we start successfully
// and should move on to `OS::run`, and EXIT_FAILURE otherwise for
// an early exit with that error code.
static Ref<ScriptRunner> load_script_runner(const String &p_path) {
	Ref<Script> script_res = ResourceLoader::load(p_path);
	ERR_FAIL_COND_V_MSG(script_res.is_null(), Ref<ScriptRunner>(), vformat("Can't load script runner: %s", p_path));
	ERR_FAIL_COND_V_MSG(!script_res->is_valid(), Ref<ScriptRunner>(), vformat("Script runner has parse errors: %s", p_path));
	ERR_FAIL_COND_V_MSG(!script_res->can_instantiate(), Ref<ScriptRunner>(), vformat("Can't instantiate script runner: %s", p_path));
	ERR_FAIL_COND_V_MSG(!ClassDB::is_parent_class(script_res->get_instance_base_type(), "ScriptRunner"),
			Ref<ScriptRunner>(), vformat("Script runner must extend ScriptRunner: %s", p_path));

	Object *obj = ClassDB::instantiate(script_res->get_instance_base_type());
	ScriptRunner *runner_object = Object::cast_to<ScriptRunner>(obj);
	if (!runner_object) {
		if (obj) {
			memdelete(obj);
		}
		runner_object = memnew(ScriptRunner);
	}
	Ref<ScriptRunner> runner(runner_object);
	runner->set_script(script_res);
	return runner;
}

int Main::start() {
	FoundryProfileZone("start");
	OS::get_singleton()->benchmark_begin_measure("Startup", "Main::Start");

	ERR_FAIL_COND_V(!_start_success, EXIT_FAILURE);

	bool has_icon = false;
	String positional_arg;
	String game_path;
	String script;
	String test_runner_path;
	String eval_source;
	String main_loop_type;
	bool check_only = false;

#ifdef TOOLS_ENABLED
	String doc_tool_path;
	bool doc_tool_implicit_cwd = false;
	BitField<DocTools::GenerateFlags> gen_flags = {};
	String _export_preset;
	Vector<String> patches;
	bool export_debug = false;
	bool export_pack_only = false;
	bool install_android_build_template = false;
	bool export_patch = false;
#ifdef MODULE_FOUNDRY_SCRIPT_ENABLED
	String fs_docs_path;
	bool fs_format_requested = false;
	bool fs_lint_requested = false;
	String fs_migrate_path;
	bool fs_migrate_apply = false;
	bool fs_migrate_strict_null = false;
	bool fs_migrate_strict_dynamic = false;
	bool fs_migrate_activate_strict = false;
	bool fs_migrate_confirm = false;
	bool fs_migrate_allow_violations = false;
	bool fs_migrate_acknowledge_vcs = false;
	String fs_migrate_follow_up_path;
#endif
#endif // TOOLS_ENABLED

	const FoundryCLIParser::CLIInvocation &cli_invocation = foundry_cli_parse.invocation;
	using CLIKind = FoundryCLIParser::CLIInvocation::Kind;
	const bool eval_requested = cli_invocation.kind == CLIKind::SCRIPT_EVAL;
	switch (cli_invocation.kind) {
		case CLIKind::PROJECT_RUN:
			if (!cli_invocation.scene.is_empty()) {
				game_path = ResourceUID::ensure_path(cli_invocation.scene);
			}
			if (!cli_invocation.script.is_empty()) {
				script = cli_invocation.script;
			}
			check_only = cli_invocation.check_only;
			break;
		case CLIKind::PROJECT_TEST:
			test_runner_path = cli_invocation.runner;
			break;
		case CLIKind::SCRIPT_EVAL:
			eval_source = cli_invocation.eval_source;
			break;
#ifdef TOOLS_ENABLED
		case CLIKind::PROJECT_EXPORT:
			_export_preset = cli_invocation.export_preset;
			positional_arg = cli_invocation.export_output;
			export_debug = cli_invocation.export_mode == "debug";
			export_pack_only = cli_invocation.export_mode == "pack" || cli_invocation.export_mode == "patch";
			export_patch = cli_invocation.export_mode == "patch";
			if (!cli_invocation.export_patches.is_empty()) {
				patches = cli_invocation.export_patches.split(",", false);
			}
			install_android_build_template = cli_invocation.install_android_build_template;
			break;
		case CLIKind::DOCS_GENERATE_ENGINE:
			doc_tool_path = cli_invocation.docs_engine_output.is_empty() ? "." : cli_invocation.docs_engine_output;
			doc_tool_implicit_cwd = cli_invocation.docs_engine_output.is_empty();
			if (cli_invocation.docs_no_docbase) {
				gen_flags.set_flag(DocTools::GENERATE_FLAG_SKIP_BASIC_TYPES);
			}
			break;
		case CLIKind::DOCS_GENERATE_SCRIPT:
			fs_docs_path = cli_invocation.docs_script_source;
			doc_tool_path = cli_invocation.docs_script_output.is_empty() ? "." : cli_invocation.docs_script_output;
			doc_tool_implicit_cwd = cli_invocation.docs_script_output.is_empty();
			break;
#ifdef MODULE_FOUNDRY_SCRIPT_ENABLED
		case CLIKind::SCRIPT_FORMAT:
			fs_format_requested = true;
			break;
		case CLIKind::SCRIPT_LINT:
			fs_lint_requested = true;
			break;
		case CLIKind::SCRIPT_MIGRATE:
			fs_migrate_path = cli_invocation.project_path;
			fs_migrate_apply = cli_invocation.migrate_apply;
			fs_migrate_strict_null = cli_invocation.migrate_strict_null;
			fs_migrate_strict_dynamic = cli_invocation.migrate_strict_dynamic;
			fs_migrate_activate_strict = cli_invocation.migrate_activate_strict;
			fs_migrate_confirm = cli_invocation.migrate_confirm;
			fs_migrate_allow_violations = cli_invocation.migrate_allow_violations;
			fs_migrate_acknowledge_vcs = cli_invocation.migrate_acknowledge_vcs;
			fs_migrate_follow_up_path = cli_invocation.migrate_follow_up;
			break;
#endif // MODULE_FOUNDRY_SCRIPT_ENABLED
#endif // TOOLS_ENABLED
		default:
			break;
	}

	main_timer_sync.init(OS::get_singleton()->get_ticks_usec());
	List<String> args = OS::get_singleton()->get_cmdline_args();

	for (List<String>::Element *E = args.front(); E; E = E->next()) {
		// First check parameters that do not have an argument to the right.

		// Doctest Unit Testing Handler
		// Designed to override and pass arguments to the unit test handler.
		if (E->get() == "--check-only") {
			check_only = true;
#ifdef TOOLS_ENABLED
		} else if (E->get() == "--no-docbase") {
			gen_flags.set_flag(DocTools::GENERATE_FLAG_SKIP_BASIC_TYPES);
		} else if (E->get() == "--foundryextension-docs") {
			gen_flags.set_flag(DocTools::GENERATE_FLAG_SKIP_BASIC_TYPES);
			gen_flags.set_flag(DocTools::GENERATE_FLAG_EXTENSION_CLASSES_ONLY);
		} else if (E->get() == "-e" || E->get() == "--editor") {
			editor = true;
		} else if (E->get() == "--recovery-mode") {
			recovery_mode = true;
		} else if (E->get() == "--install-android-build-template") {
			install_android_build_template = true;
#ifdef MODULE_FOUNDRY_SCRIPT_ENABLED
		} else if (E->get() == "--foundry_script-format") {
			fs_format_requested = true;
		} else if (E->get() == "--foundry_script-lint") {
			fs_lint_requested = true;
		} else if (E->get() == "--foundry_script-migrate-apply") {
			fs_migrate_apply = true;
		} else if (E->get() == "--foundry_script-migrate-strict-null-checks") {
			fs_migrate_strict_null = true;
		} else if (E->get() == "--foundry_script-migrate-strict-dynamic-checks") {
			fs_migrate_strict_dynamic = true;
		} else if (E->get() == "--foundry_script-migrate-activate-strict") {
			fs_migrate_activate_strict = true;
		} else if (E->get() == "--foundry_script-migrate-confirm") {
			fs_migrate_confirm = true;
		} else if (E->get() == "--foundry_script-migrate-allow-violations") {
			fs_migrate_allow_violations = true;
		} else if (E->get() == "--foundry_script-migrate-acknowledge-vcs") {
			fs_migrate_acknowledge_vcs = true;
#endif // MODULE_FOUNDRY_SCRIPT_ENABLED
#endif // TOOLS_ENABLED
		} else if (E->get() == "--scene") {
#if defined(OVERRIDE_PATH_ENABLED)
			E = E->next();
			if (E) {
				game_path = ResourceUID::ensure_path(E->get());
			} else {
				ERR_FAIL_V_MSG(EXIT_FAILURE, "Missing scene path, aborting.");
			}
#else
			ERR_PRINT(
					"`--scene` was specified on the command line, but this Foundry binary was compiled without support for path overrides. Aborting.\n"
					"To be able to use it, use the `disable_path_overrides=no` SCons option when compiling Foundry.\n");
			return EXIT_FAILURE;
#endif // defined(OVERRIDE_PATH_ENABLED)
		} else if (E->get().length() && E->get()[0] != '-' && positional_arg.is_empty() && game_path.is_empty()) {
			positional_arg = E->get();

			String scene_path = ResourceUID::ensure_path(E->get());
			if (scene_path.ends_with(".scn") ||
					scene_path.ends_with(".tscn") ||
					scene_path.ends_with(".escn") ||
					scene_path.ends_with(".res") ||
					scene_path.ends_with(".tres")) {
				// Only consider the positional argument to be a scene path if it ends with
				// a file extension associated with Foundry scenes. This makes it possible
				// for projects to parse command-line arguments for custom CLI arguments
				// or other file extensions without trouble. This can be used to implement
				// "drag-and-drop onto executable" logic, which can prove helpful
				// for non-game applications.
#if defined(OVERRIDE_PATH_ENABLED)
				game_path = scene_path;
#else
				ERR_PRINT(
						"Scene path was specified on the command line, but this Foundry binary was compiled without support for path overrides. Aborting.\n"
						"To be able to use it, use the `disable_path_overrides=no` SCons option when compiling Foundry.\n");
				return EXIT_FAILURE;
#endif // defined(OVERRIDE_PATH_ENABLED)
			}
		}
		// Then parameters that have an argument to the right.
		else if (E->next()) {
			bool parsed_pair = true;
			if (E->get() == "-s" || E->get() == "--script") {
				script = E->next()->get();
			} else if (E->get() == "--run-test-runner") {
				test_runner_path = E->next()->get();
			} else if (E->get() == "--main-loop") {
				main_loop_type = E->next()->get();
#ifdef TOOLS_ENABLED
			} else if (E->get() == "--doctool") {
				doc_tool_path = E->next()->get();
				if (doc_tool_path.begins_with("-")) {
					// Assuming other command line arg, so default to cwd.
					doc_tool_path = ".";
					doc_tool_implicit_cwd = true;
					parsed_pair = false;
				}
#ifdef MODULE_FOUNDRY_SCRIPT_ENABLED
			} else if (E->get() == "--foundry_script-docs") {
				fs_docs_path = E->next()->get();
			} else if (E->get() == "--foundry_script-migrate") {
				fs_migrate_path = E->next()->get();
			} else if (E->get() == "--foundry_script-migrate-follow-up") {
				fs_migrate_follow_up_path = E->next()->get();
#endif
			} else if (E->get() == "--export-release") {
				ERR_FAIL_COND_V_MSG(!editor && !found_project, EXIT_FAILURE, "Please provide a valid project path when exporting, aborting.");
				editor = true; //needs editor
				_export_preset = E->next()->get();
			} else if (E->get() == "--export-debug") {
				ERR_FAIL_COND_V_MSG(!editor && !found_project, EXIT_FAILURE, "Please provide a valid project path when exporting, aborting.");
				editor = true; //needs editor
				_export_preset = E->next()->get();
				export_debug = true;
			} else if (E->get() == "--export-pack") {
				ERR_FAIL_COND_V_MSG(!editor && !found_project, EXIT_FAILURE, "Please provide a valid project path when exporting, aborting.");
				editor = true;
				_export_preset = E->next()->get();
				export_pack_only = true;
			} else if (E->get() == "--export-patch") {
				ERR_FAIL_COND_V_MSG(!editor && !found_project, EXIT_FAILURE, "Please provide a valid project path when exporting, aborting.");
				editor = true;
				_export_preset = E->next()->get();
				export_pack_only = true;
				export_patch = true;
			} else if (E->get() == "--patches") {
				patches = E->next()->get().split(",", false);
#endif
			} else {
				// The parameter does not match anything known, don't skip the next argument
				parsed_pair = false;
			}
			if (parsed_pair) {
				E = E->next();
			}
		} else if (E->get().begins_with("--export-")) {
			ERR_FAIL_V_MSG(EXIT_FAILURE, "Missing export preset name, aborting.");
		} else if (E->get() == "--run-test-runner") {
			ERR_FAIL_V_MSG(EXIT_FAILURE, "Missing script path for --run-test-runner, aborting.");
		}
#ifdef TOOLS_ENABLED
		// Handle case where no path is given to --doctool.
		else if (E->get() == "--doctool") {
			doc_tool_path = ".";
			doc_tool_implicit_cwd = true;
		}
#endif
	}

	if (!test_runner_path.is_empty()) {
#ifdef TOOLS_ENABLED
		if (!script.is_empty() || editor || check_only || !_export_preset.is_empty()) {
			ERR_FAIL_V_MSG(EXIT_FAILURE,
					"--run-test-runner cannot be combined with --script, the editor, --check-only, or --export-* flags. Aborting.");
		}
#else
		if (!script.is_empty() || check_only) {
			ERR_FAIL_V_MSG(EXIT_FAILURE, "--run-test-runner cannot be combined with --script or --check-only. Aborting.");
		}
#endif
		ERR_FAIL_COND_V_MSG(!ProjectSettings::get_singleton()->is_project_loaded(), EXIT_FAILURE,
				"Please provide a valid project path for --run-test-runner, aborting.");
	}

	uint64_t minimum_time_msec = GLOBAL_DEF(PropertyInfo(Variant::INT, "application/boot_splash/minimum_display_time", PROPERTY_HINT_RANGE, "0,100,1,or_greater,suffix:ms"), 0);
	if (Engine::get_singleton()->is_editor_hint()) {
		minimum_time_msec = 0;
	}

#ifdef TOOLS_ENABLED
#ifdef MODULE_FOUNDRY_SCRIPT_ENABLED
	if (!doc_tool_path.is_empty() && fs_docs_path.is_empty()) {
#else
		if (!doc_tool_path.is_empty()) {
#endif
		// Needed to instance editor-only classes for their default values
		Engine::get_singleton()->set_editor_hint(true);

		// Translate the class reference only when `-l LOCALE` parameter is given.
		if (!locale.is_empty() && locale != "en") {
			load_doc_translations(locale);
		}

		{
			Ref<DirAccess> da = DirAccess::open(doc_tool_path);
			ERR_FAIL_COND_V_MSG(da.is_null(), EXIT_FAILURE, "Argument supplied to --doctool must be a valid directory path.");
			// Ensure that doctool is running in the root dir, but only if
			// user did not manually specify a path as argument.
			if (doc_tool_implicit_cwd) {
				ERR_FAIL_COND_V_MSG(!da->dir_exists("doc"), EXIT_FAILURE, "--doctool must be run from the Foundry repository's root folder, or specify a path that points there.");
			}
		}

#ifndef MODULE_MONO_ENABLED
		// Hack to define .NET-specific project settings even on non-.NET builds,
		// so that we don't lose their descriptions and default values in DocTools.
		// Default values should be synced with mono_gd/gd_mono.cpp.
		GLOBAL_DEF("dotnet/project/assembly_name", "");
		GLOBAL_DEF("dotnet/project/solution_directory", "");
		GLOBAL_DEF(PropertyInfo(Variant::INT, "dotnet/project/assembly_reload_attempts", PROPERTY_HINT_RANGE, "1,16,1,or_greater"), 3);
#endif

		Error err;
		DocTools doc;
		doc.generate(gen_flags);

		DocTools docsrc;
		HashMap<String, String> doc_data_classes;
		HashSet<String> checked_paths;
		print_line("Loading docs...");

		const bool foundry_extension_docs = gen_flags.has_flag(DocTools::GENERATE_FLAG_EXTENSION_CLASSES_ONLY);

		if (!foundry_extension_docs) {
			for (int i = 0; i < _doc_data_class_path_count; i++) {
				// Custom modules are always located by absolute path.
				String path = _doc_data_class_paths[i].path;
				if (path.is_relative_path()) {
					path = doc_tool_path.path_join(path);
				}
				String name = _doc_data_class_paths[i].name;
				doc_data_classes[name] = path;
				if (!checked_paths.has(path)) {
					checked_paths.insert(path);

					// Create the module documentation directory if it doesn't exist
					Ref<DirAccess> da = DirAccess::create_for_path(path);
					err = da->make_dir_recursive(path);
					ERR_FAIL_COND_V_MSG(err != OK, EXIT_FAILURE, "Error: Can't create directory: " + path + ": " + itos(err));

					print_line("Loading docs from: " + path);
					err = docsrc.load_classes(path);
					ERR_FAIL_COND_V_MSG(err != OK, EXIT_FAILURE, "Error loading docs from: " + path + ": " + itos(err));
				}
			}
		}

		// For FoundryExtension docs, use a path that is compatible with Godot modules.
		String index_path = foundry_extension_docs ? doc_tool_path.path_join("doc_classes") : doc_tool_path.path_join("doc/classes");
		// Create the main documentation directory if it doesn't exist
		Ref<DirAccess> da = DirAccess::create_for_path(index_path);
		err = da->make_dir_recursive(index_path);
		ERR_FAIL_COND_V_MSG(err != OK, EXIT_FAILURE, "Error: Can't create index directory: " + index_path + ": " + itos(err));

		print_line("Loading classes from: " + index_path);
		err = docsrc.load_classes(index_path);
		ERR_FAIL_COND_V_MSG(err != OK, EXIT_FAILURE, "Error loading classes from: " + index_path + ": " + itos(err));
		checked_paths.insert(index_path);

		print_line("Merging docs...");
		doc.merge_from(docsrc);

		for (const String &E : checked_paths) {
			print_line("Erasing old docs at: " + E);
			err = DocTools::erase_classes(E);
			ERR_FAIL_COND_V_MSG(err != OK, EXIT_FAILURE, "Error erasing old docs at: " + E + ": " + itos(err));
		}

		print_line("Generating new docs...");
		err = doc.save_classes(index_path, doc_data_classes, !foundry_extension_docs);
		ERR_FAIL_COND_V_MSG(err != OK, EXIT_FAILURE, "Error saving new docs:" + itos(err));

		print_line("Deleting docs cache...");
		if (FileAccess::exists(EditorHelp::get_cache_full_path())) {
			DirAccess::remove_file_or_error(EditorHelp::get_cache_full_path());
		}

		return EXIT_SUCCESS;
	}

	// FoundryExtension API and interface.
	{
		if (dump_foundry_extension_interface) {
			FoundryExtensionInterfaceDump::generate_foundry_extension_interface_file("foundry_extension_interface.json");
		}

		if (dump_foundry_extension_interface_header) {
			FoundryExtensionInterfaceHeaderGenerator::generate_foundry_extension_interface_header("foundry_extension_interface.h");
		}

		if (dump_extension_api) {
			Engine::get_singleton()->set_editor_hint(true); // "extension_api.json" should always contains editor singletons.
			FoundryExtensionAPIDump::generate_extension_json_file("extension_api.json", include_docs_in_extension_api_dump);
		}

		if (dump_foundry_extension_interface || dump_foundry_extension_interface_header || dump_extension_api) {
			return EXIT_SUCCESS;
		}

		if (validate_extension_api) {
			Engine::get_singleton()->set_editor_hint(true); // "extension_api.json" should always contains editor singletons.
			bool valid = FoundryExtensionAPIDump::validate_extension_json_file(validate_extension_api_file) == OK;
			return valid ? EXIT_SUCCESS : EXIT_FAILURE;
		}
	}

#endif // TOOLS_ENABLED

#if defined(OVERRIDE_PATH_ENABLED)
	bool disable_override = GLOBAL_GET("application/config/disable_project_settings_override");
	if (disable_override) {
		script = String();
		test_runner_path = String();
		game_path = String();
		main_loop_type = String();
	}
#else
	script = String();
	test_runner_path = String();
	game_path = String();
	main_loop_type = String();
#endif // defined(OVERRIDE_PATH_ENABLED)

	// `script eval` runs a generated inline runner, not a project main scene, so never
	// resolve (and possibly abort on) the project's configured main scene for it.
	bool skip_main_scene_resolution = eval_requested;
#if defined(TOOLS_ENABLED) && defined(MODULE_FOUNDRY_SCRIPT_ENABLED)
	// These Foundry Script CLI tools are handled before the game branch and never run the main scene,
	// so do not resolve (and possibly abort on) an unimported uid:// main scene -- that would fail a
	// fresh CI/source checkout before the tool could even print its report.
	skip_main_scene_resolution = skip_main_scene_resolution || fs_format_requested || fs_lint_requested || !fs_migrate_path.is_empty();
#endif

#if defined(TOOLS_ENABLED) && defined(MODULE_FOUNDRY_SCRIPT_ENABLED)
	if (fs_format_requested) {
		Vector<String> format_args;
		for (int i = 0; i < cli_invocation.command_args.size(); i++) {
			format_args.push_back(cli_invocation.command_args[i]);
		}
		FSFormatterCLI::run_from_cmdline(format_args);
		return OS::get_singleton()->get_exit_code();
	}
	if (fs_lint_requested) {
		Vector<String> lint_args;
		for (int i = 0; i < cli_invocation.command_args.size(); i++) {
			lint_args.push_back(cli_invocation.command_args[i]);
		}
		FSLintCLI::run_from_cmdline(lint_args);
		return OS::get_singleton()->get_exit_code();
	}
#endif // TOOLS_ENABLED && MODULE_FOUNDRY_SCRIPT_ENABLED

	if (!skip_main_scene_resolution && script.is_empty() && game_path.is_empty() && test_runner_path.is_empty()) {
		const String main_scene = GLOBAL_GET("application/run/main_scene");
		if (main_scene.begins_with("uid://")) {
			ResourceUID::ID id = ResourceUID::get_singleton()->text_to_id(main_scene);
			if (!editor && !ResourceUID::get_singleton()->has_id(id) && !FileAccess::exists(ResourceUID::get_singleton()->get_cache_file())) {
				OS::get_singleton()->alert("Main scene's path could not be resolved from UID. Make sure the project is imported first. Aborting.");
				ERR_FAIL_V_MSG(EXIT_FAILURE, "Main scene's path could not be resolved from UID. Make sure the project is imported first. Aborting.");
			}
			game_path = ResourceUID::get_singleton()->get_id_path(id);
		} else {
			game_path = main_scene;
		}
	}

#ifdef TOOLS_ENABLED
	if (!editor && !cmdline_tool && script.is_empty() && game_path.is_empty() && test_runner_path.is_empty()) {
		// If we end up here, it means we didn't manage to detect what we want to run.
		// Let's throw an error gently. The code leading to this is pretty brittle so
		// this might end up triggered by valid usage, in which case we'll have to
		// fine-tune further.
		OS::get_singleton()->alert("Couldn't detect whether to run the editor, the project manager or a specific project. Aborting.");
		ERR_FAIL_V_MSG(EXIT_FAILURE, "Couldn't detect whether to run the editor, the project manager or a specific project. Aborting.");
	}
#endif

#if defined(TOOLS_ENABLED) && defined(MODULE_FOUNDRY_SCRIPT_ENABLED)
	if (!fs_migrate_path.is_empty()) {
		// Headless strict-typing migration wizard. Reuses the same orchestrator the editor entry
		// point drives (report -> apply -> gated strict activation), so a scripted or
		// continuous-integration run produces the identical flow without a window. Handled here --
		// before the project main loop is resolved/instantiated and before the game branch loads and
		// instantiates project autoloads -- so it runs independently of the project's runtime
		// main-loop setting and a dry-run preview never executes arbitrary project code before
		// printing its report.

		// Refuse to run unless a real project was loaded. Otherwise ProjectSettings::setup() may
		// have left res:// bound to the process working directory, and an --apply run could rewrite
		// an unintended tree (and falsely report success).
		ERR_FAIL_COND_V_MSG(!found_project, EXIT_FAILURE,
				"--foundry_script-migrate requires a valid project; none was found at the given path. Aborting.");

		// --foundry_script-migrate-follow-up takes a path; reject a missing one (the next token was
		// another option, or the flag ended the command line) so the request is not silently
		// dropped or made to consume an unrelated option as its path.
		ERR_FAIL_COND_V_MSG(fs_migrate_follow_up_path.begins_with("-"), EXIT_FAILURE,
				"--foundry_script-migrate-follow-up requires a file path argument. Aborting.");

		MigrationWizardOptions options;
		options.apply = fs_migrate_apply;
		options.strict_null_checks = fs_migrate_strict_null;
		options.strict_dynamic_checks = fs_migrate_strict_dynamic;
		options.activate_strict = fs_migrate_activate_strict;
		options.confirm_strict_activation = fs_migrate_confirm;
		options.allow_strict_with_violations = fs_migrate_allow_violations;
		options.acknowledge_vcs_warning = fs_migrate_acknowledge_vcs;
		options.follow_up_path = fs_migrate_follow_up_path;

		// The migration path was loaded as the project (project_path), so res:// resolves to it; the
		// wizard scans the whole project tree.
		const MigrationWizardResult migration_result = FSMigrationWizard::run("res://", options);
		OS::get_singleton()->print("%s", migration_result.summary().utf8().get_data());
		// succeeded() is stricter than ok: it also fails a gated strict activation and an activation
		// that flipped but could not be persisted, so a scripted or CI run enforcing strict
		// activation never mistakes a blocked or unsaved flip for success.
		return migration_result.succeeded() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
#endif // TOOLS_ENABLED && MODULE_FOUNDRY_SCRIPT_ENABLED

#ifdef MODULE_FOUNDRY_SCRIPT_ENABLED
	// `--script --check-only` still loads and validates the script, so run pre_compile before script loading.
	// Inline eval only needs the build pipeline when it runs against a real project (so
	// project-provided generated types resolve); a projectless probe skips the stages.
	const bool eval_with_project = eval_requested && ProjectSettings::get_singleton()->is_project_loaded();
	bool foundry_runtime_build_stages_enabled = !editor && (!game_path.is_empty() || !script.is_empty() || !test_runner_path.is_empty() || eval_with_project);
#endif // MODULE_FOUNDRY_SCRIPT_ENABLED
	bool custom_resource_handlers_registered = false;

#ifdef MODULE_FOUNDRY_SCRIPT_ENABLED
	if (foundry_runtime_build_stages_enabled) {
		if (!run_foundry_build_stage_for_cli(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE,
					"Foundry pre_compile runtime stage")) {
			return EXIT_FAILURE;
		}

		ResourceLoader::add_custom_loaders();
		ResourceSaver::add_custom_savers();
		custom_resource_handlers_registered = true;
	}
#endif // MODULE_FOUNDRY_SCRIPT_ENABLED

	MainLoop *main_loop = nullptr;
	Ref<ScriptRunner> script_runner;
	if (!test_runner_path.is_empty()) {
		if (!editor && ProjectSettings::get_singleton()->is_project_loaded() && !ProjectSettings::get_singleton()->is_using_datapack()) {
			ScriptServer::scan_global_classes();
		}

		script_runner = load_script_runner(test_runner_path);
		ERR_FAIL_COND_V_MSG(script_runner.is_null(), EXIT_FAILURE, "Failed to load script runner.");
	}
#ifdef MODULE_FOUNDRY_SCRIPT_ENABLED
	else if (eval_requested) {
		// A projectless eval is valid, but an explicit `--project` that could not be honored must
		// not silently degrade to projectless (or ambient-project) execution: a CI probe expecting
		// the requested project's context would otherwise pass while evaluating elsewhere. This
		// covers both a directory that failed to apply (`foundry_cli_project_path_error`, e.g. it
		// does not exist, so an ambient project under the original cwd may have loaded instead) and
		// a valid directory that simply has no project to load.
		if (!cli_invocation.project_path.is_empty() &&
				(foundry_cli_project_path_error || !ProjectSettings::get_singleton()->is_project_loaded())) {
			ERR_PRINT(vformat("script eval could not use the requested project at \"%s\".", cli_invocation.project_path));
			return EXIT_FAILURE;
		}

		// Scan project global classes (in memory) so an inline snippet can reference the
		// project's `class_name` scripts, mirroring `project run --script`.
		if (!editor && ProjectSettings::get_singleton()->is_project_loaded() && !ProjectSettings::get_singleton()->is_using_datapack()) {
			ScriptServer::scan_global_classes();
		}

		String eval_error;
		script_runner = FSInlineEval::compile_runner(eval_source, eval_error);
		if (script_runner.is_null()) {
			ERR_PRINT(eval_error.is_empty() ? String("Failed to evaluate inline Foundry Script.") : eval_error);
			return EXIT_FAILURE;
		}
	}
#endif // MODULE_FOUNDRY_SCRIPT_ENABLED

	if (editor) {
		main_loop = memnew(SceneTree);
	}
	if (main_loop_type.is_empty()) {
		main_loop_type = GLOBAL_GET("application/run/main_loop_type");
	}

	if (!script.is_empty()) {
		// Without the editor there is no EditorFileSystem to rebuild the global script class cache,
		// so a missing or stale `global_script_class_cache.cfg` would break `class_name` resolution
		// for the whole run. Rescan the project for global classes in memory instead (never written
		// back to disk). Exported projects (running from a datapack) keep trusting the cache bundled
		// at export time: their scripts may be compiled to bytecode the scan cannot parse.
		if (!editor && ProjectSettings::get_singleton()->is_project_loaded() && !ProjectSettings::get_singleton()->is_using_datapack()) {
			ScriptServer::scan_global_classes();
		}

		Ref<Script> script_res = ResourceLoader::load(script);
		ERR_FAIL_COND_V_MSG(script_res.is_null(), EXIT_FAILURE, "Can't load script: " + script);

		if (check_only) {
			if (!script_res->is_valid()) {
				return EXIT_FAILURE;
			}
#ifdef MODULE_FOUNDRY_SCRIPT_ENABLED
			if (foundry_runtime_build_stages_enabled &&
					!run_foundry_build_stage_for_cli(ProjectBuildPipelineConfig::STAGE_POST_COMPILE,
							"Foundry post_compile runtime stage")) {
				return EXIT_FAILURE;
			}
#endif // MODULE_FOUNDRY_SCRIPT_ENABLED
			return EXIT_SUCCESS;
		}

		if (script_res->can_instantiate()) {
			StringName instance_type = script_res->get_instance_base_type();
			Object *obj = ClassDB::instantiate(instance_type);
			MainLoop *script_loop = Object::cast_to<MainLoop>(obj);
			if (!script_loop) {
				if (obj) {
					memdelete(obj);
				}
				OS::get_singleton()->alert(vformat("Can't load the script \"%s\" as it doesn't inherit from SceneTree or MainLoop.", script));
				ERR_FAIL_V_MSG(EXIT_FAILURE, vformat("Can't load the script \"%s\" as it doesn't inherit from SceneTree or MainLoop.", script));
			}

			script_loop->set_script(script_res);
			main_loop = script_loop;
		} else {
			return EXIT_FAILURE;
		}
	} else { // Not based on script path.
		if (!editor && !ClassDB::class_exists(main_loop_type) && ScriptServer::is_global_class(main_loop_type)) {
			String script_path = ScriptServer::get_global_class_path(main_loop_type);
			Ref<Script> script_res = ResourceLoader::load(script_path);
			if (script_res.is_null()) {
				OS::get_singleton()->alert("Error: Could not load MainLoop script type: " + main_loop_type);
				ERR_FAIL_V_MSG(EXIT_FAILURE, vformat("Could not load global class %s.", main_loop_type));
			}
			StringName script_base = script_res->get_instance_base_type();
			Object *obj = ClassDB::instantiate(script_base);
			MainLoop *script_loop = Object::cast_to<MainLoop>(obj);
			if (!script_loop) {
				if (obj) {
					memdelete(obj);
				}
				OS::get_singleton()->alert("Error: Invalid MainLoop script base type: " + script_base);
				ERR_FAIL_V_MSG(EXIT_FAILURE, vformat("The global class %s does not inherit from SceneTree or MainLoop.", main_loop_type));
			}
			script_loop->set_script(script_res);
			main_loop = script_loop;
		}
	}

	if (!main_loop && main_loop_type.is_empty()) {
		main_loop_type = "SceneTree";
	}

	if (!main_loop) {
		if (!ClassDB::class_exists(main_loop_type)) {
			OS::get_singleton()->alert("Error: MainLoop type doesn't exist: " + main_loop_type);
			return EXIT_FAILURE;
		} else {
			Object *ml = ClassDB::instantiate(main_loop_type);
			ERR_FAIL_NULL_V_MSG(ml, EXIT_FAILURE, "Can't instance MainLoop type.");

			main_loop = Object::cast_to<MainLoop>(ml);
			if (!main_loop) {
				memdelete(ml);
				ERR_FAIL_V_MSG(EXIT_FAILURE, "Invalid MainLoop type.");
			}
		}
	}

	OS::get_singleton()->set_main_loop(main_loop);

	SceneTree *sml = Object::cast_to<SceneTree>(main_loop);
	if (sml) {
#ifdef DEBUG_ENABLED
		if (debug_collisions) {
			sml->set_debug_collisions_hint(true);
		}
		if (debug_paths) {
			sml->set_debug_paths_hint(true);
		}

		if (debug_navigation) {
			sml->set_debug_navigation_hint(true);
#ifndef NAVIGATION_2D_DISABLED
			NavigationServer2D::get_singleton()->set_debug_navigation_enabled(true);
#endif // NAVIGATION_2D_DISABLED
#ifndef NAVIGATION_3D_DISABLED
			NavigationServer3D::get_singleton()->set_debug_navigation_enabled(true);
#endif // NAVIGATION_3D_DISABLED
		}
		if (debug_avoidance) {
#ifndef NAVIGATION_2D_DISABLED
			NavigationServer2D::get_singleton()->set_debug_avoidance_enabled(true);
#endif // NAVIGATION_2D_DISABLED
#ifndef NAVIGATION_3D_DISABLED
			NavigationServer3D::get_singleton()->set_debug_avoidance_enabled(true);
#endif // NAVIGATION_3D_DISABLED
		}
		if (debug_navigation || debug_avoidance) {
#ifndef NAVIGATION_2D_DISABLED
			NavigationServer2D::get_singleton()->set_active(true);
			NavigationServer2D::get_singleton()->set_debug_enabled(true);
#endif // NAVIGATION_2D_DISABLED
#ifndef NAVIGATION_3D_DISABLED
			NavigationServer3D::get_singleton()->set_active(true);
			NavigationServer3D::get_singleton()->set_debug_enabled(true);
#endif // NAVIGATION_3D_DISABLED
		}
		if (debug_canvas_item_redraw) {
			RenderingServer::get_singleton()->canvas_item_set_debug_redraw(true);
		}

		if (debug_mute_audio) {
			AudioServer::get_singleton()->set_debug_mute(true);
		}
#endif

		if (single_threaded_scene) {
			sml->set_disable_node_threading(true);
		}

		bool embed_subwindows = GLOBAL_GET("display/window/subwindows/embed_subwindows");

		if (single_window || (!editor && embed_subwindows) || !DisplayServer::get_singleton()->has_feature(DisplayServer::Feature::FEATURE_SUBWINDOWS)) {
			sml->get_root()->set_embedding_subwindows(true);
		}

		if (!custom_resource_handlers_registered) {
			ResourceLoader::add_custom_loaders();
			ResourceSaver::add_custom_savers();
			custom_resource_handlers_registered = true;
		}

		if (!editor) { // game
			if (!game_path.is_empty() || !script.is_empty() || !test_runner_path.is_empty() || eval_requested) {
				//autoload
				OS::get_singleton()->benchmark_begin_measure("Startup", "Load Autoloads");
				Vector<ProjectSettings::AutoloadInfo> autoloads;
#ifdef MODULE_FOUNDRY_SCRIPT_ENABLED
				FSAutoloadIndex autoload_index;
				const Error autoload_index_err = autoload_index.rebuild_for_runtime_startup();
				if (autoload_index_err != OK) {
					WARN_PRINT(vformat("Failed to load FoundryScript autoload index cache; falling back to project settings: %s.",
							error_names[autoload_index_err]));
					autoload_index.rebuild_from_project_settings();
				}
				autoload_index.register_startup_autoloads_in_project_settings();
				autoloads = autoload_index.get_startup_autoloads();
#else
				for (const KeyValue<StringName, ProjectSettings::AutoloadInfo> &E : ProjectSettings::get_singleton()->get_autoload_list()) {
					autoloads.push_back(E.value);
				}
#endif // MODULE_FOUNDRY_SCRIPT_ENABLED

				//first pass, add the constants so they exist before any script is loaded
				for (const ProjectSettings::AutoloadInfo &info : autoloads) {
					if (info.is_singleton) {
						for (int i = 0; i < ScriptServer::get_language_count(); i++) {
							ScriptLanguage *language = ScriptServer::get_language(i);
							// A reserved name (e.g. the `godot` reflection namespace) wins over the
							// autoload, so don't even seed a placeholder global for it; that would
							// leave a stale Nil entry shadowing the reserved global.
							if (language->get_reserved_global_names().has(String(info.name))) {
								continue;
							}
							language->add_global_constant(info.name, Variant());
						}
					}
				}

				//second pass, load into global constants
				List<Node *> to_add;
				for (const ProjectSettings::AutoloadInfo &info : autoloads) {
					Node *n = nullptr;
					if (ResourceLoader::get_resource_type(info.path) == "PackedScene") {
						// Cache the scene reference before loading it (for cyclic references)
						Ref<PackedScene> scn;
						scn.instantiate();
						scn->set_path(ResourceUID::ensure_path(info.path));
						scn->reload_from_file();
						ERR_CONTINUE_MSG(scn.is_null(), vformat("Failed to instantiate an autoload, can't load from path: %s.", info.path));

						if (scn.is_valid()) {
							n = scn->instantiate();
						}
					} else {
						Ref<Resource> res = ResourceLoader::load(info.path);
						ERR_CONTINUE_MSG(res.is_null(), vformat("Failed to instantiate an autoload, can't load from path: %s.", info.path));

						Ref<Script> script_res = res;
						if (script_res.is_valid()) {
							StringName ibt = script_res->get_instance_base_type();
							bool valid_type = ClassDB::is_parent_class(ibt, "Node");
							ERR_CONTINUE_MSG(!valid_type, vformat("Failed to instantiate an autoload, script '%s' does not inherit from 'Node'.", info.path));

							Object *obj = ClassDB::instantiate(ibt);
							ERR_CONTINUE_MSG(!obj, vformat("Failed to instantiate an autoload, cannot instantiate '%s'.", ibt));

							n = Object::cast_to<Node>(obj);
							n->set_script(script_res);
						}
					}

					ERR_CONTINUE_MSG(!n, vformat("Failed to instantiate an autoload, path is not pointing to a scene or a script: %s.", info.path));
					n->set_name(info.name);

					//defer so references are all valid on _ready()
					to_add.push_back(n);

					if (info.is_singleton) {
						for (int i = 0; i < ScriptServer::get_language_count(); i++) {
							ScriptLanguage *language = ScriptServer::get_language(i);
							// A language-reserved name (e.g. the `godot` reflection namespace)
							// wins over a project autoload of the same name, so do not register
							// the autoload as that language's global. Mirrors the editor and the
							// analyzer; surfaces the misconfiguration instead of silently shadowing.
							if (language->get_reserved_global_names().has(String(info.name))) {
								WARN_PRINT(vformat("Autoload \"%s\" shadows a reserved engine namespace of the same name and was not registered as a global constant. Rename the autoload to use it.", String(info.name)));
								continue;
							}
							language->add_global_constant(info.name, n);
						}
					}
				}

				for (Node *E : to_add) {
					sml->get_root()->add_child(E);
				}
				OS::get_singleton()->benchmark_end_measure("Startup", "Load Autoloads");
			}
		}

#ifdef TOOLS_ENABLED
#ifdef MODULE_FOUNDRY_SCRIPT_ENABLED
		if (!doc_tool_path.is_empty() && !fs_docs_path.is_empty()) {
			DocTools docs;
			Error err;

			Vector<String> paths = get_files_with_extension(fs_docs_path, "fs");
			ERR_FAIL_COND_V_MSG(paths.is_empty(), EXIT_FAILURE, "Couldn't find any FoundryScript files under the given directory: " + fs_docs_path);

			for (const String &path : paths) {
				Ref<FoundryScript> foundry_script = ResourceLoader::load(path);
				for (const DocData::ClassDoc &class_doc : foundry_script->get_documentation()) {
					docs.add_doc(class_doc);
				}
			}

			if (doc_tool_implicit_cwd) {
				doc_tool_path = "./docs";
			}

			Ref<DirAccess> da = DirAccess::create_for_path(doc_tool_path);
			err = da->make_dir_recursive(doc_tool_path);
			ERR_FAIL_COND_V_MSG(err != OK, EXIT_FAILURE, "Error: Can't create FoundryScript docs directory: " + doc_tool_path + ": " + itos(err));

			HashMap<String, String> doc_data_classes;
			err = docs.save_classes(doc_tool_path, doc_data_classes, false);
			ERR_FAIL_COND_V_MSG(err != OK, EXIT_FAILURE, "Error saving FoundryScript docs:" + itos(err));

			return EXIT_SUCCESS;
		}
#endif // MODULE_FOUNDRY_SCRIPT_ENABLED

		EditorNode *editor_node = nullptr;
		if (editor) {
			OS::get_singleton()->benchmark_begin_measure("Startup", "Editor");

			sml->get_root()->set_translation_domain("godot.editor");
			if (editor_pseudolocalization) {
				translation_server->get_editor_domain()->set_pseudolocalization_enabled(true);
			}

			editor_node = memnew(EditorNode);
			sml->get_root()->add_child(editor_node);

			if (!_export_preset.is_empty()) {
				editor_node->export_preset(_export_preset, positional_arg, export_debug, export_pack_only, install_android_build_template, export_patch, patches);
				game_path = ""; // Do not load anything.
			}

			OS::get_singleton()->benchmark_end_measure("Startup", "Editor");
		}
#endif
		sml->set_auto_accept_quit(GLOBAL_GET("application/config/auto_accept_quit"));
		sml->set_quit_on_go_back(GLOBAL_GET("application/config/quit_on_go_back"));

		if (!editor) {
			//standard helpers that can be changed from main config

			String stretch_mode = GLOBAL_GET("display/window/stretch/mode");
			String stretch_aspect = GLOBAL_GET("display/window/stretch/aspect");
			Size2i stretch_size = Size2i(GLOBAL_GET("display/window/size/viewport_width"),
					GLOBAL_GET("display/window/size/viewport_height"));
			real_t stretch_scale = GLOBAL_GET("display/window/stretch/scale");
			String stretch_scale_mode = GLOBAL_GET("display/window/stretch/scale_mode");

			Window::ContentScaleMode cs_sm = Window::CONTENT_SCALE_MODE_DISABLED;
			if (stretch_mode == "canvas_items") {
				cs_sm = Window::CONTENT_SCALE_MODE_CANVAS_ITEMS;
			} else if (stretch_mode == "viewport") {
				cs_sm = Window::CONTENT_SCALE_MODE_VIEWPORT;
			}

			Window::ContentScaleAspect cs_aspect = Window::CONTENT_SCALE_ASPECT_IGNORE;
			if (stretch_aspect == "keep") {
				cs_aspect = Window::CONTENT_SCALE_ASPECT_KEEP;
			} else if (stretch_aspect == "keep_width") {
				cs_aspect = Window::CONTENT_SCALE_ASPECT_KEEP_WIDTH;
			} else if (stretch_aspect == "keep_height") {
				cs_aspect = Window::CONTENT_SCALE_ASPECT_KEEP_HEIGHT;
			} else if (stretch_aspect == "expand") {
				cs_aspect = Window::CONTENT_SCALE_ASPECT_EXPAND;
			}

			Window::ContentScaleStretch cs_stretch = Window::CONTENT_SCALE_STRETCH_FRACTIONAL;
			if (stretch_scale_mode == "integer") {
				cs_stretch = Window::CONTENT_SCALE_STRETCH_INTEGER;
			}

			sml->get_root()->set_content_scale_mode(cs_sm);
			sml->get_root()->set_content_scale_aspect(cs_aspect);
			sml->get_root()->set_content_scale_stretch(cs_stretch);
			sml->get_root()->set_content_scale_size(stretch_size);
			sml->get_root()->set_content_scale_factor(stretch_scale);

			sml->set_auto_accept_quit(GLOBAL_GET("application/config/auto_accept_quit"));
			sml->set_quit_on_go_back(GLOBAL_GET("application/config/quit_on_go_back"));
			String appname = GLOBAL_GET("application/config/name");
			appname = TranslationServer::get_singleton()->translate(appname);
#ifdef DEBUG_ENABLED
			// Append a suffix to the window title to denote that the project is running
			// from a debug build (including the editor). Since this results in lower performance,
			// this should be clearly presented to the user.
			DisplayServer::get_singleton()->window_set_title(vformat("%s (DEBUG)", appname));
#else
			DisplayServer::get_singleton()->window_set_title(appname);
#endif

			bool snap_controls = GLOBAL_GET("gui/common/snap_controls_to_pixels");
			sml->get_root()->set_snap_controls_to_pixels(snap_controls);

			int drag_threshold = GLOBAL_GET("gui/common/drag_threshold");
			sml->get_root()->set_drag_threshold(drag_threshold);

			bool font_oversampling = GLOBAL_GET("gui/fonts/dynamic_fonts/use_oversampling");
			sml->get_root()->set_use_oversampling(font_oversampling);

			int texture_filter = GLOBAL_GET("rendering/textures/canvas_textures/default_texture_filter");
			int texture_repeat = GLOBAL_GET("rendering/textures/canvas_textures/default_texture_repeat");
			sml->get_root()->set_default_canvas_item_texture_filter(
					Viewport::DefaultCanvasItemTextureFilter(texture_filter));
			sml->get_root()->set_default_canvas_item_texture_repeat(
					Viewport::DefaultCanvasItemTextureRepeat(texture_repeat));
		}

#ifdef TOOLS_ENABLED
		if (editor) {
			bool editor_embed_subwindows = EDITOR_GET("interface/editor/single_window_mode");

			if (editor_embed_subwindows) {
				sml->get_root()->set_embedding_subwindows(true);
			}
			restore_editor_window_layout = EDITOR_GET("interface/editor/editor_screen").operator int() == EditorSettings::InitialScreen::INITIAL_SCREEN_AUTO;
		}
#endif

		String local_game_path;
		if (!game_path.is_empty()) {
			local_game_path = game_path.replace_char('\\', '/');

			if (!local_game_path.begins_with("res://")) {
				bool absolute =
						(local_game_path.size() > 1) && (local_game_path[0] == '/' || local_game_path[1] == ':');

				if (!absolute) {
					if (ProjectSettings::get_singleton()->is_using_datapack()) {
						local_game_path = "res://" + local_game_path;

					} else {
						int sep = local_game_path.rfind_char('/');

						if (sep == -1) {
							Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
							ERR_FAIL_COND_V(da.is_null(), EXIT_FAILURE);

							local_game_path = da->get_current_dir().path_join(local_game_path);
						} else {
							Ref<DirAccess> da = DirAccess::open(local_game_path.substr(0, sep));
							if (da.is_valid()) {
								local_game_path = da->get_current_dir().path_join(
										local_game_path.substr(sep + 1));
							}
						}
					}
				}
			}

			local_game_path = ProjectSettings::get_singleton()->localize_path(local_game_path);

#ifdef TOOLS_ENABLED
			// The projectless editor shell has no loaded project, so a requested scene
			// cannot be opened; skip scene loading (the startup dialog is shown instead).
			if (editor && !projectless_editor_shell) {
				if (!recovery_mode && (game_path != ResourceUID::ensure_path(String(GLOBAL_GET("application/run/main_scene"))) || !editor_node->has_scenes_in_session())) {
					Error serr = editor_node->load_scene(local_game_path);
					if (serr != OK) {
						ERR_PRINT("Failed to load scene");
					}
				}
				if (!debug_server_uri.is_empty()) {
					EditorDebuggerNode::get_singleton()->start(debug_server_uri);
					EditorDebuggerNode::get_singleton()->set_keep_open(true);
				}
			}
#endif
		}

		if (!editor) { // game

			OS::get_singleton()->benchmark_begin_measure("Startup", "Load Game");

			// Load SSL Certificates from Project Settings (or builtin).
			Crypto::load_default_certificates(GLOBAL_GET("network/tls/certificate_bundle_override"));

			if (!game_path.is_empty()) {
				Node *scene = nullptr;
				Ref<PackedScene> scenedata = ResourceLoader::load(local_game_path);
				if (scenedata.is_valid()) {
					scene = scenedata->instantiate();
				}

				ERR_FAIL_NULL_V_MSG(scene, EXIT_FAILURE, "Failed loading scene: " + local_game_path + ".");
				sml->add_current_scene(scene);

#ifdef MACOS_ENABLED
#ifndef TOOLS_ENABLED
				if ((FileAccess::exists(OS::get_singleton()->get_bundle_resource_dir().path_join("Assets.car")) && !OS::get_singleton()->get_bundle_icon_name().is_empty()) || (!OS::get_singleton()->get_bundle_icon_path().is_empty())) {
					has_icon = true; // Bundle has embedded icon, do not override with project icon.
				}
#endif
				String mac_icon_path = GLOBAL_GET("application/config/macos_native_icon");
				if (DisplayServer::get_singleton()->has_feature(DisplayServer::FEATURE_NATIVE_ICON) && !mac_icon_path.is_empty() && !has_icon) {
					DisplayServer::get_singleton()->set_native_icon(mac_icon_path);
					has_icon = true;
				}
#endif

#ifdef WINDOWS_ENABLED
				String win_icon_path = GLOBAL_GET("application/config/windows_native_icon");
				if (DisplayServer::get_singleton()->has_feature(DisplayServer::FEATURE_NATIVE_ICON) && !win_icon_path.is_empty()) {
					DisplayServer::get_singleton()->set_native_icon(win_icon_path);
					has_icon = true;
				}
#endif

				String icon_path = GLOBAL_GET("application/config/icon");
				if (DisplayServer::get_singleton()->has_feature(DisplayServer::FEATURE_ICON) && !icon_path.is_empty() && !has_icon) {
					Ref<Image> icon;
					icon.instantiate();
					if (ImageLoader::load_image(icon_path, icon) == OK) {
						DisplayServer::get_singleton()->set_icon(icon);
						has_icon = true;
					}
				}
			}

			OS::get_singleton()->benchmark_end_measure("Startup", "Load Game");
#ifdef MODULE_FOUNDRY_SCRIPT_ENABLED
			if (foundry_runtime_build_stages_enabled &&
					!run_foundry_build_stage_for_cli(ProjectBuildPipelineConfig::STAGE_POST_COMPILE,
							"Foundry post_compile runtime stage")) {
				return EXIT_FAILURE;
			}
			if (!test_runner_path.is_empty() || eval_requested) {
				ERR_FAIL_COND_V_MSG(sml == nullptr, EXIT_FAILURE, "Script runner requires a SceneTree main loop.");
				PackedStringArray user_args;
				for (const String &user_arg : OS::get_singleton()->get_cmdline_user_args()) {
					user_args.push_back(user_arg);
				}
				ScriptRunner::launch_host(sml, script_runner, user_args);
			}
#endif // MODULE_FOUNDRY_SCRIPT_ENABLED
		}

#ifdef TOOLS_ENABLED
		if (editor) {
			// Load SSL Certificates from Editor Settings (or builtin)
			Crypto::load_default_certificates(
					EditorSettings::get_singleton()->get_setting("network/tls/editor_tls_certificates").operator String());
		}

		if (recovery_mode) {
			Engine::get_singleton()->set_recovery_mode_hint(true);
		}
#endif
	}

	if (DisplayServer::get_singleton()->has_feature(DisplayServer::FEATURE_ICON) && !has_icon && OS::get_singleton()->get_bundle_icon_path().is_empty()) {
		Ref<Image> icon = memnew(Image(app_icon_png));
		DisplayServer::get_singleton()->set_icon(icon);
	}

	if (movie_writer) {
		Size2i movie_size = Size2i(GLOBAL_GET("display/window/size/viewport_width"), GLOBAL_GET("display/window/size/viewport_height"));
		String stretch_mode = GLOBAL_GET("display/window/stretch/mode");
		if (stretch_mode != "viewport") {
			// `canvas_items` and `disabled` modes use the window size override instead,
			// which allows for higher resolution recording with 2D elements designed for a lower resolution.
			const int window_width_override = GLOBAL_GET("display/window/size/window_width_override");
			if (window_width_override > 0) {
				movie_size.width = window_width_override;
			}
			const int window_height_override = GLOBAL_GET("display/window/size/window_height_override");
			if (window_height_override > 0) {
				movie_size.height = window_height_override;
			}
		}
		movie_writer->begin(movie_size, fixed_fps, Engine::get_singleton()->get_write_movie_path());
	}

	FoundryExtensionManager::get_singleton()->startup();

#ifdef MACOS_ENABLED
	// TODO: Used to fix full-screen splash drawing on macOS, processing events before main loop is fully initialized cause issues on Wayland, and has no effect on other platforms.
	if (minimum_time_msec) {
		int64_t minimum_time = 1000 * minimum_time_msec;
		uint64_t prev_time = OS::get_singleton()->get_ticks_usec();
		while (minimum_time > 0) {
			DisplayServer::get_singleton()->process_events();
			OS::get_singleton()->delay_usec(100);

			uint64_t next_time = OS::get_singleton()->get_ticks_usec();
			minimum_time -= (next_time - prev_time);
			prev_time = next_time;
		}
	} else {
		DisplayServer::get_singleton()->process_events();
	}
#else
	if (minimum_time_msec) {
		uint64_t minimum_time = 1000 * minimum_time_msec;
		uint64_t elapsed_time = OS::get_singleton()->get_ticks_usec();
		if (elapsed_time < minimum_time) {
			OS::get_singleton()->delay_usec(minimum_time - elapsed_time);
		}
	}
#endif

	OS::get_singleton()->benchmark_end_measure("Startup", "Main::Start");
	OS::get_singleton()->benchmark_dump();

	return EXIT_SUCCESS;
}

/* Main iteration
 *
 * This is the iteration of the engine's game loop, advancing the state of physics,
 * rendering and audio.
 * It's called directly by the platform's OS::run method, where the loop is created
 * and monitored.
 *
 * The OS implementation can impact its draw step with the Main::force_redraw() method.
 */

uint64_t Main::last_ticks = 0;
uint32_t Main::frames = 0;
uint32_t Main::hide_print_fps_attempts = 3;
uint32_t Main::frame = 0;
bool Main::force_redraw_requested = false;
int Main::iterating = 0;

bool Main::is_iterating() {
	return iterating > 0;
}

// For performance metrics.
static uint64_t physics_process_max = 0;
static uint64_t process_max = 0;
static uint64_t navigation_process_max = 0;

// Return false means iterating further, returning true means `OS::run`
// will terminate the program. In case of failure, the OS exit code needs
// to be set explicitly here (defaults to EXIT_SUCCESS).
bool Main::iteration() {
	FoundryProfileZone("Main::iteration");
	FoundryProfileZoneGroupedFirst(_profile_zone, "prepare");
	iterating++;

#ifdef TOOLS_ENABLED
	// A latched SIGINT/SIGTERM is serviced here rather than in the handler itself,
	// so child cleanup and the tree quit happen on the main thread.
	EditorToolingHost::process_pending_shutdown();
#endif

	const uint64_t ticks = OS::get_singleton()->get_ticks_usec();
	Engine::get_singleton()->_frame_ticks = ticks;
	main_timer_sync.set_cpu_ticks_usec(ticks);
	main_timer_sync.set_fixed_fps(fixed_fps);

	const uint64_t ticks_elapsed = ticks - last_ticks;

	const int physics_ticks_per_second = Engine::get_singleton()->get_user_physics_ticks_per_second();
	const double physics_step = 1.0 / physics_ticks_per_second;

	const double time_scale = Engine::get_singleton()->get_effective_time_scale();

	MainFrameTime advance = main_timer_sync.advance(physics_step, physics_ticks_per_second);
	double process_step = advance.process_step;
	double scaled_step = process_step * time_scale;

	Engine::get_singleton()->_process_step = process_step;
	Engine::get_singleton()->_physics_interpolation_fraction = advance.interpolation_fraction;

	uint64_t physics_process_ticks = 0;
	uint64_t process_ticks = 0;
#if !defined(NAVIGATION_2D_DISABLED) || !defined(NAVIGATION_3D_DISABLED)
	uint64_t navigation_process_ticks = 0;
#endif // !defined(NAVIGATION_2D_DISABLED) || !defined(NAVIGATION_3D_DISABLED)

	frame += ticks_elapsed;

	last_ticks = ticks;

	const int max_physics_steps = Engine::get_singleton()->get_user_max_physics_steps_per_frame();
	if (fixed_fps == -1 && advance.physics_steps > max_physics_steps) {
		process_step -= (advance.physics_steps - max_physics_steps) * physics_step;
		advance.physics_steps = max_physics_steps;
	}

	bool exit = false;

	// process all our active interfaces
#ifndef XR_DISABLED
	FoundryProfileZoneGrouped(_profile_zone, "xr_server->_process");
	XRServer::get_singleton()->_process();
#endif // XR_DISABLED

	FoundryProfileZoneGrouped(_profile_zone, "physics");
	for (int iters = 0; iters < advance.physics_steps; ++iters) {
		FoundryProfileZone("Physics Step");
		FoundryProfileZoneGroupedFirst(_physics_zone, "setup");
		if (Input::get_singleton()->is_agile_input_event_flushing()) {
			Input::get_singleton()->flush_buffered_events();
		}

		Engine::get_singleton()->_in_physics = true;
		Engine::get_singleton()->_physics_frames++;

		uint64_t physics_begin = OS::get_singleton()->get_ticks_usec();

		// Prepare the fixed timestep interpolated nodes BEFORE they are updated
		// by the physics server, otherwise the current and previous transforms
		// may be the same, and no interpolation takes place.
		FoundryProfileZoneGrouped(_physics_zone, "main loop iteration prepare");
		OS::get_singleton()->get_main_loop()->iteration_prepare();

#ifndef PHYSICS_3D_DISABLED
		FoundryProfileZoneGrouped(_physics_zone, "PhysicsServer3D::sync");
		PhysicsServer3D::get_singleton()->sync();
		PhysicsServer3D::get_singleton()->flush_queries();
#endif // PHYSICS_3D_DISABLED

#ifndef PHYSICS_2D_DISABLED
		FoundryProfileZoneGrouped(_physics_zone, "PhysicsServer2D::sync");
		PhysicsServer2D::get_singleton()->sync();
		PhysicsServer2D::get_singleton()->flush_queries();
#endif // PHYSICS_2D_DISABLED

		FoundryProfileZoneGrouped(_physics_zone, "physics_process");
		if (OS::get_singleton()->get_main_loop()->physics_process(physics_step * time_scale)) {
#ifndef PHYSICS_3D_DISABLED
			PhysicsServer3D::get_singleton()->end_sync();
#endif // PHYSICS_3D_DISABLED
#ifndef PHYSICS_2D_DISABLED
			PhysicsServer2D::get_singleton()->end_sync();
#endif // PHYSICS_2D_DISABLED

			Engine::get_singleton()->_in_physics = false;
			exit = true;
			break;
		}

#if !defined(NAVIGATION_2D_DISABLED) || !defined(NAVIGATION_3D_DISABLED)
		uint64_t navigation_begin = OS::get_singleton()->get_ticks_usec();

#ifndef NAVIGATION_2D_DISABLED
		FoundryProfileZoneGrouped(_profile_zone, "NavigationServer2D::physics_process");
		NavigationServer2D::get_singleton()->physics_process(physics_step * time_scale);
#endif // NAVIGATION_2D_DISABLED
#ifndef NAVIGATION_3D_DISABLED
		FoundryProfileZoneGrouped(_profile_zone, "NavigationServer3D::physics_process");
		NavigationServer3D::get_singleton()->physics_process(physics_step * time_scale);
#endif // NAVIGATION_3D_DISABLED

		navigation_process_ticks = MAX(navigation_process_ticks, OS::get_singleton()->get_ticks_usec() - navigation_begin); // keep the largest one for reference
		navigation_process_max = MAX(OS::get_singleton()->get_ticks_usec() - navigation_begin, navigation_process_max);

		message_queue->flush();
#endif // !defined(NAVIGATION_2D_DISABLED) || !defined(NAVIGATION_3D_DISABLED)

#ifndef PHYSICS_3D_DISABLED
		FoundryProfileZoneGrouped(_profile_zone, "3D physics");
		PhysicsServer3D::get_singleton()->end_sync();
		PhysicsServer3D::get_singleton()->step(physics_step * time_scale);
#endif // PHYSICS_3D_DISABLED

#ifndef PHYSICS_2D_DISABLED
		FoundryProfileZoneGrouped(_profile_zone, "2D physics");
		PhysicsServer2D::get_singleton()->end_sync();
		PhysicsServer2D::get_singleton()->step(physics_step * time_scale);
#endif // PHYSICS_2D_DISABLED

		message_queue->flush();

		FoundryProfileZoneGrouped(_profile_zone, "main loop iteration end");
		OS::get_singleton()->get_main_loop()->iteration_end();

		physics_process_ticks = MAX(physics_process_ticks, OS::get_singleton()->get_ticks_usec() - physics_begin); // keep the largest one for reference
		physics_process_max = MAX(OS::get_singleton()->get_ticks_usec() - physics_begin, physics_process_max);

		Engine::get_singleton()->_in_physics = false;
	}

	if (Input::get_singleton()->is_agile_input_event_flushing()) {
		Input::get_singleton()->flush_buffered_events();
	}

	uint64_t process_begin = OS::get_singleton()->get_ticks_usec();

	FoundryProfileZoneGrouped(_profile_zone, "process");
	if (OS::get_singleton()->get_main_loop()->process(process_step * time_scale)) {
		exit = true;
	}
	message_queue->flush();

	// A component may have requested a graceful shutdown during process()
	// (e.g. via push_fatal()). Honor it here so this iteration ends the loop;
	// this sits before the `fixed_fps` early-return below so both return paths
	// observe it.
	if (OS::get_singleton()->is_exit_requested()) {
		exit = true;
	}

#ifndef NAVIGATION_2D_DISABLED
	FoundryProfileZoneGrouped(_profile_zone, "process 2D navigation");
	NavigationServer2D::get_singleton()->process(process_step * time_scale);
#endif // NAVIGATION_2D_DISABLED
#ifndef NAVIGATION_3D_DISABLED
	FoundryProfileZoneGrouped(_profile_zone, "process 3D navigation");
	NavigationServer3D::get_singleton()->process(process_step * time_scale);
#endif // NAVIGATION_3D_DISABLED

	FoundryProfileZoneGrouped(_profile_zone, "RenderingServer::sync");
	RenderingServer::get_singleton()->sync(); //sync if still drawing from previous frames.

	FoundryProfileZoneGrouped(_profile_zone, "RenderingServer::draw");
	const bool has_pending_resources_for_processing = RD::get_singleton() && RD::get_singleton()->has_pending_resources_for_processing();
	bool wants_present = (DisplayServer::get_singleton()->can_any_window_draw() ||
								 DisplayServer::get_singleton()->has_additional_outputs()) &&
			RenderingServer::get_singleton()->is_render_loop_enabled();

	if (wants_present || has_pending_resources_for_processing) {
		wants_present |= force_redraw_requested;
		if ((!force_redraw_requested) && OS::get_singleton()->is_in_low_processor_usage_mode()) {
			if (RenderingServer::get_singleton()->has_changed()) {
				RenderingServer::get_singleton()->draw(wants_present, scaled_step); // flush visual commands
				Engine::get_singleton()->increment_frames_drawn();
			}
		} else {
			RenderingServer::get_singleton()->draw(wants_present, scaled_step); // flush visual commands
			Engine::get_singleton()->increment_frames_drawn();
			force_redraw_requested = false;
		}
	}

	process_ticks = OS::get_singleton()->get_ticks_usec() - process_begin;
	process_max = MAX(process_ticks, process_max);
	uint64_t frame_time = OS::get_singleton()->get_ticks_usec() - ticks;

	FoundryProfileZoneGrouped(_profile_zone, "FoundryExtensionManager::frame");
	FoundryExtensionManager::get_singleton()->frame();

	FoundryProfileZoneGrouped(_profile_zone, "ScriptServer::frame");
	for (int i = 0; i < ScriptServer::get_language_count(); i++) {
		ScriptServer::get_language(i)->frame();
	}

	FoundryProfileZoneGrouped(_profile_zone, "AudioServer::update");
	AudioServer::get_singleton()->update();

	if (EngineDebugger::is_active()) {
		EngineDebugger::get_singleton()->iteration(frame_time, process_ticks, physics_process_ticks, physics_step);
	}

	frames++;
	Engine::get_singleton()->_process_frames++;

	if (frame > 1000000) {
		// Wait a few seconds before printing FPS, as FPS reporting just after the engine has started is inaccurate.
		if (hide_print_fps_attempts == 0) {
			if (editor) {
				if (print_fps) {
					print_line(vformat("Editor FPS: %d (%s mspf)", frames, rtos(1000.0 / frames).pad_decimals(2)));
				}
			} else if (print_fps || GLOBAL_GET("debug/settings/stdout/print_fps")) {
				print_line(vformat("Project FPS: %d (%s mspf)", frames, rtos(1000.0 / frames).pad_decimals(2)));
			}
		} else {
			hide_print_fps_attempts--;
		}

		Engine::get_singleton()->_fps = frames;
		performance->set_process_time(USEC_TO_SEC(process_max));
		performance->set_physics_process_time(USEC_TO_SEC(physics_process_max));
		performance->set_navigation_process_time(USEC_TO_SEC(navigation_process_max));
		process_max = 0;
		physics_process_max = 0;
		navigation_process_max = 0;

		frame %= 1000000;
		frames = 0;
	}

	iterating--;

	if (movie_writer) {
		FoundryProfileZoneGrouped(_profile_zone, "movie_writer->add_frame");
		movie_writer->add_frame();
	}

#ifdef TOOLS_ENABLED
	bool quit_after_timeout = false;
#endif
	if ((quit_after > 0) && (Engine::get_singleton()->_process_frames >= quit_after)) {
#ifdef TOOLS_ENABLED
		quit_after_timeout = true;
#endif
		exit = true;
	}

#ifdef TOOLS_ENABLED
	if (wait_for_import && EditorFileSystem::get_singleton() && EditorFileSystem::get_singleton()->doing_first_scan()) {
		exit = false;
	}
#endif

	if (fixed_fps != -1) {
		return exit;
	}

	SceneTree *scene_tree = SceneTree::get_singleton();
	bool wake_for_events = scene_tree && scene_tree->is_accessibility_enabled();

	FoundryProfileZoneGrouped(_profile_zone, "OS::add_frame_delay");
	OS::get_singleton()->add_frame_delay(DisplayServer::get_singleton()->window_can_draw(), wake_for_events);

#ifdef TOOLS_ENABLED
	if (auto_build_solutions) {
		auto_build_solutions = false;
		// Only relevant when running the editor.
		if (!editor) {
			OS::get_singleton()->set_exit_code(EXIT_FAILURE);
			ERR_FAIL_V_MSG(true,
					"Command line option --build-solutions was passed, but no project is being edited. Aborting.");
		}
		if (!EditorNode::get_singleton()->call_build()) {
			OS::get_singleton()->set_exit_code(EXIT_FAILURE);
			ERR_FAIL_V_MSG(true,
					"Command line option --build-solutions was passed, but the build callback failed. Aborting.");
		}
	}
#endif

#ifdef TOOLS_ENABLED
	if (exit && quit_after_timeout && EditorNode::get_singleton()) {
		EditorNode::get_singleton()->unload_editor_addons();
	}
#endif

	return exit;
}

void Main::force_redraw() {
	force_redraw_requested = true;
}

/* Engine deinitialization
 *
 * Responsible for freeing all the memory allocated by previous setup steps,
 * so that the engine closes cleanly without leaking memory or crashing.
 * The order matters as some of those steps are linked with each other.
 */
void Main::cleanup(bool p_force) {
	FoundryProfileZone("cleanup");
	OS::get_singleton()->benchmark_begin_measure("Shutdown", "Main::Cleanup");
	if (!p_force) {
		ERR_FAIL_COND(!_start_success);
	}

	// Printing in the usual way can become problematic during/after cleanup.
	CoreGlobals::print_ready = false;

#ifdef DEBUG_ENABLED
	if (input) {
		input->flush_frame_parsed_events();
	}
#endif

	FoundryExtensionManager::get_singleton()->shutdown();

	for (int i = 0; i < TextServerManager::get_singleton()->get_interface_count(); i++) {
		TextServerManager::get_singleton()->get_interface(i)->cleanup();
	}

	if (movie_writer) {
		movie_writer->end();
	}

	ResourceLoader::clear_thread_load_tasks();

	ResourceLoader::remove_custom_loaders();
	ResourceSaver::remove_custom_savers();
	PropertyListHelper::clear_base_helpers();

	// Remove the lock file if the engine exits successfully. Some automated processes such as
	// --export/--import can bypass and/or finish faster than the existing check to remove the lock file.
	if (OS::get_singleton()->get_exit_code() == EXIT_SUCCESS) {
		OS::get_singleton()->remove_lock_file();
	}

	// Flush before uninitializing the scene, but delete the MessageQueue as late as possible.
	message_queue->flush();

	OS::get_singleton()->delete_main_loop();

	OS::get_singleton()->_cmdline.clear();
	OS::get_singleton()->_user_args.clear();
	OS::get_singleton()->_execpath = "";
	OS::get_singleton()->_local_clipboard = "";

	ResourceLoader::clear_translation_remaps();

	WorkerThreadPool::get_singleton()->exit_languages_threads();

	ScriptServer::finish_languages();

	// Sync pending commands that may have been queued from a different thread during ScriptServer finalization
	RenderingServer::get_singleton()->sync();

	//clear global shader variables before scene and other graphics stuff are deinitialized.
	rendering_server->global_shader_parameters_clear();

#ifndef XR_DISABLED
	if (xr_server) {
		// Now that we're unregistering properly in plugins we need to keep access to xr_server for a little longer
		// We do however unset our primary interface
		xr_server->set_primary_interface(Ref<XRInterface>());
	}
#endif // XR_DISABLED

#ifdef TOOLS_ENABLED
	FoundryExtensionManager::get_singleton()->deinitialize_extensions(FoundryExtension::INITIALIZATION_LEVEL_EDITOR);
	uninitialize_modules(MODULE_INITIALIZATION_LEVEL_EDITOR);
	unregister_editor_types();

#endif

	ImageLoader::cleanup();

	FoundryExtensionManager::get_singleton()->deinitialize_extensions(FoundryExtension::INITIALIZATION_LEVEL_SCENE);
	uninitialize_modules(MODULE_INITIALIZATION_LEVEL_SCENE);

	unregister_platform_apis();
	unregister_driver_types();
	unregister_scene_types();

	finalize_theme_db();

// Before deinitializing server extensions, finalize servers which may be loaded as extensions.
#ifndef NAVIGATION_2D_DISABLED
	NavigationServer2DManager::finalize_server();
	NavigationServer2DManager::finalize_server_manager();
#endif // NAVIGATION_2D_DISABLED
#ifndef NAVIGATION_3D_DISABLED
	NavigationServer3DManager::finalize_server();
	NavigationServer3DManager::finalize_server_manager();
#endif // NAVIGATION_3D_DISABLED
	finalize_physics();

	FoundryExtensionManager::get_singleton()->deinitialize_extensions(FoundryExtension::INITIALIZATION_LEVEL_SERVERS);
	uninitialize_modules(MODULE_INITIALIZATION_LEVEL_SERVERS);
	unregister_server_types();

	EngineDebugger::deinitialize();

#ifndef XR_DISABLED
	if (xr_server) {
		memdelete(xr_server);
	}
#endif // XR_DISABLED

	if (audio_server) {
		audio_server->finish();
		memdelete(audio_server);
	}

	if (camera_server) {
		memdelete(camera_server);
	}

	OS::get_singleton()->finalize();

	finalize_display();

	if (input) {
		memdelete(input);
	}

	if (packed_data) {
		memdelete(packed_data);
	}
	if (performance) {
		memdelete(performance);
	}
	if (input_map) {
		memdelete(input_map);
	}
	if (translation_server) {
		memdelete(translation_server);
	}
	if (tsman) {
		memdelete(tsman);
	}
#ifndef PHYSICS_3D_DISABLED
	if (physics_server_3d_manager) {
		memdelete(physics_server_3d_manager);
	}
#endif // PHYSICS_3D_DISABLED
#ifndef PHYSICS_2D_DISABLED
	if (physics_server_2d_manager) {
		memdelete(physics_server_2d_manager);
	}
#endif // PHYSICS_2D_DISABLED
	if (globals) {
		memdelete(globals);
	}

	if (OS::get_singleton()->is_restart_on_exit_set()) {
		//attempt to restart with arguments
		List<String> args = OS::get_singleton()->get_restart_on_exit_arguments();
		OS::get_singleton()->create_instance(args);
		OS::get_singleton()->set_restart_on_exit(false, List<String>()); //clear list (uses memory)
	}

	// Now should be safe to delete MessageQueue (famous last words).
	message_queue->flush();
	memdelete(message_queue);

#if defined(STEAMAPI_ENABLED)
	if (steam_tracker) {
		memdelete(steam_tracker);
	}
#endif

	unregister_core_driver_types();
	unregister_core_extensions();
	uninitialize_modules(MODULE_INITIALIZATION_LEVEL_CORE);

	if (engine) {
		memdelete(engine);
	}

	unregister_core_types();

	OS::get_singleton()->benchmark_end_measure("Shutdown", "Main::Cleanup");
	OS::get_singleton()->benchmark_dump();

	OS::get_singleton()->finalize_core();
}
