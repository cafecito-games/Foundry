/**************************************************************************/
/*  export_plugin.cpp                                                     */
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

#include "export_plugin.h"

#include "logo_svg.gen.h"
#include "run_icon_svg.gen.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/image_loader.h"
#include "core/io/json.h"
#include "core/io/marshalls.h"
#include "core/string/translation_server.h"
#include "core/version.h"
#include "editor/editor_log.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/export/export_template_manager.h"
#include "editor/file_system/editor_paths.h"
#include "editor/import/resource_importer_texture_settings.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "main/splash.gen.h"
#include "scene/resources/image_texture.h"

#include "modules/modules_enabled.gen.h" // For mono.
#include "modules/svg/image_loader_svg.h"

#ifdef MODULE_MONO_ENABLED
#include "modules/mono/utils/path_utils.h"
#endif

static const char *ANDROID_PERMS[] = {
	"ACCESS_CHECKIN_PROPERTIES",
	"ACCESS_COARSE_LOCATION",
	"ACCESS_FINE_LOCATION",
	"ACCESS_LOCATION_EXTRA_COMMANDS",
	"ACCESS_MEDIA_LOCATION",
	"ACCESS_MOCK_LOCATION",
	"ACCESS_NETWORK_STATE",
	"ACCESS_SURFACE_FLINGER",
	"ACCESS_WIFI_STATE",
	"ACCOUNT_MANAGER",
	"ADD_VOICEMAIL",
	"AUTHENTICATE_ACCOUNTS",
	"BATTERY_STATS",
	"BIND_ACCESSIBILITY_SERVICE",
	"BIND_APPWIDGET",
	"BIND_DEVICE_ADMIN",
	"BIND_INPUT_METHOD",
	"BIND_NFC_SERVICE",
	"BIND_NOTIFICATION_LISTENER_SERVICE",
	"BIND_PRINT_SERVICE",
	"BIND_REMOTEVIEWS",
	"BIND_TEXT_SERVICE",
	"BIND_VPN_SERVICE",
	"BIND_WALLPAPER",
	"BLUETOOTH",
	"BLUETOOTH_ADMIN",
	"BLUETOOTH_PRIVILEGED",
	"BRICK",
	"BROADCAST_PACKAGE_REMOVED",
	"BROADCAST_SMS",
	"BROADCAST_STICKY",
	"BROADCAST_WAP_PUSH",
	"CALL_PHONE",
	"CALL_PRIVILEGED",
	"CAMERA",
	"CAPTURE_AUDIO_OUTPUT",
	"CAPTURE_SECURE_VIDEO_OUTPUT",
	"CAPTURE_VIDEO_OUTPUT",
	"CHANGE_COMPONENT_ENABLED_STATE",
	"CHANGE_CONFIGURATION",
	"CHANGE_NETWORK_STATE",
	"CHANGE_WIFI_MULTICAST_STATE",
	"CHANGE_WIFI_STATE",
	"CLEAR_APP_CACHE",
	"CLEAR_APP_USER_DATA",
	"CONTROL_LOCATION_UPDATES",
	"DELETE_CACHE_FILES",
	"DELETE_PACKAGES",
	"DEVICE_POWER",
	"DIAGNOSTIC",
	"DISABLE_KEYGUARD",
	"DUMP",
	"EXPAND_STATUS_BAR",
	"FACTORY_TEST",
	"FLASHLIGHT",
	"FORCE_BACK",
	"GET_ACCOUNTS",
	"GET_PACKAGE_SIZE",
	"GET_TASKS",
	"GET_TOP_ACTIVITY_INFO",
	"GLOBAL_SEARCH",
	"HARDWARE_TEST",
	"INJECT_EVENTS",
	"INSTALL_LOCATION_PROVIDER",
	"INSTALL_PACKAGES",
	"INSTALL_SHORTCUT",
	"INTERNAL_SYSTEM_WINDOW",
	"INTERNET",
	"KILL_BACKGROUND_PROCESSES",
	"LOCATION_HARDWARE",
	"MANAGE_ACCOUNTS",
	"MANAGE_APP_TOKENS",
	"MANAGE_DOCUMENTS",
	"MANAGE_EXTERNAL_STORAGE",
	"MANAGE_MEDIA",
	"MASTER_CLEAR",
	"MEDIA_CONTENT_CONTROL",
	"MODIFY_AUDIO_SETTINGS",
	"MODIFY_PHONE_STATE",
	"MOUNT_FORMAT_FILESYSTEMS",
	"MOUNT_UNMOUNT_FILESYSTEMS",
	"NFC",
	"PERSISTENT_ACTIVITY",
	"POST_NOTIFICATIONS",
	"PROCESS_OUTGOING_CALLS",
	"READ_CALENDAR",
	"READ_CALL_LOG",
	"READ_CONTACTS",
	"READ_EXTERNAL_STORAGE",
	"READ_FRAME_BUFFER",
	"READ_HISTORY_BOOKMARKS",
	"READ_INPUT_STATE",
	"READ_LOGS",
	"READ_MEDIA_AUDIO",
	"READ_MEDIA_IMAGES",
	"READ_MEDIA_VIDEO",
	"READ_MEDIA_VISUAL_USER_SELECTED",
	"READ_PHONE_STATE",
	"READ_PROFILE",
	"READ_SMS",
	"READ_SOCIAL_STREAM",
	"READ_SYNC_SETTINGS",
	"READ_SYNC_STATS",
	"READ_USER_DICTIONARY",
	"REBOOT",
	"RECEIVE_BOOT_COMPLETED",
	"RECEIVE_MMS",
	"RECEIVE_SMS",
	"RECEIVE_WAP_PUSH",
	"RECORD_AUDIO",
	"REORDER_TASKS",
	"RESTART_PACKAGES",
	"SEND_RESPOND_VIA_MESSAGE",
	"SEND_SMS",
	"SET_ACTIVITY_WATCHER",
	"SET_ALARM",
	"SET_ALWAYS_FINISH",
	"SET_ANIMATION_SCALE",
	"SET_DEBUG_APP",
	"SET_ORIENTATION",
	"SET_POINTER_SPEED",
	"SET_PREFERRED_APPLICATIONS",
	"SET_PROCESS_LIMIT",
	"SET_TIME",
	"SET_TIME_ZONE",
	"SET_WALLPAPER",
	"SET_WALLPAPER_HINTS",
	"SIGNAL_PERSISTENT_PROCESSES",
	"STATUS_BAR",
	"SUBSCRIBED_FEEDS_READ",
	"SUBSCRIBED_FEEDS_WRITE",
	"SYSTEM_ALERT_WINDOW",
	"TRANSMIT_IR",
	"UNINSTALL_SHORTCUT",
	"UPDATE_DEVICE_STATS",
	"USE_CREDENTIALS",
	"USE_SIP",
	"VIBRATE",
	"WAKE_LOCK",
	"WRITE_APN_SETTINGS",
	"WRITE_CALENDAR",
	"WRITE_CALL_LOG",
	"WRITE_CONTACTS",
	"WRITE_EXTERNAL_STORAGE",
	"WRITE_GSERVICES",
	"WRITE_HISTORY_BOOKMARKS",
	"WRITE_PROFILE",
	"WRITE_SECURE_SETTINGS",
	"WRITE_SETTINGS",
	"WRITE_SMS",
	"WRITE_SOCIAL_STREAM",
	"WRITE_SYNC_SETTINGS",
	"WRITE_USER_DICTIONARY",
	nullptr
};

static const char *MISMATCHED_VERSIONS_MESSAGE = "Android build version mismatch:\n| Template installed: %s\n| Requested version: %s\nPlease reinstall Android build template from 'Project' menu.";

static const char *FOUNDRY_EXTENSION_LIBS_PATH = "libs/foundryextensionlibs.json";

// This template string must match platform/android/java/lib/src/main/res/mipmap-anydpi-v26/icon.xml.
static const String ICON_XML_TEMPLATE =
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
		"<adaptive-icon xmlns:android=\"http://schemas.android.com/apk/res/android\">\n"
		"    <background android:drawable=\"@mipmap/icon_background\"/>\n"
		"    <foreground android:drawable=\"@mipmap/icon_foreground\"/>\n"
		"%s" // Placeholder for the optional monochrome tag.
		"</adaptive-icon>";

static const String ICON_XML_PATH = "res/mipmap-anydpi-v26/icon.xml";
static const String THEMED_ICON_XML_PATH = "res/mipmap-anydpi-v26/themed_icon.xml";

static const int ICON_DENSITIES_COUNT = 6;
static const char *LAUNCHER_ICON_OPTION = PNAME("launcher_icons/main_192x192");
static const char *LAUNCHER_ADAPTIVE_ICON_FOREGROUND_OPTION = PNAME("launcher_icons/adaptive_foreground_432x432");
static const char *LAUNCHER_ADAPTIVE_ICON_BACKGROUND_OPTION = PNAME("launcher_icons/adaptive_background_432x432");
static const char *LAUNCHER_ADAPTIVE_ICON_MONOCHROME_OPTION = PNAME("launcher_icons/adaptive_monochrome_432x432");

static const LauncherIcon LAUNCHER_ICONS[ICON_DENSITIES_COUNT] = {
	{ "res/mipmap-xxxhdpi-v4/icon.webp", 192 },
	{ "res/mipmap-xxhdpi-v4/icon.webp", 144 },
	{ "res/mipmap-xhdpi-v4/icon.webp", 96 },
	{ "res/mipmap-hdpi-v4/icon.webp", 72 },
	{ "res/mipmap-mdpi-v4/icon.webp", 48 },
	{ "res/mipmap/icon.webp", 192 }
};

static const LauncherIcon LAUNCHER_ADAPTIVE_ICON_FOREGROUNDS[ICON_DENSITIES_COUNT] = {
	{ "res/mipmap-xxxhdpi-v4/icon_foreground.webp", 432 },
	{ "res/mipmap-xxhdpi-v4/icon_foreground.webp", 324 },
	{ "res/mipmap-xhdpi-v4/icon_foreground.webp", 216 },
	{ "res/mipmap-hdpi-v4/icon_foreground.webp", 162 },
	{ "res/mipmap-mdpi-v4/icon_foreground.webp", 108 },
	{ "res/mipmap/icon_foreground.webp", 432 }
};

static const LauncherIcon LAUNCHER_ADAPTIVE_ICON_BACKGROUNDS[ICON_DENSITIES_COUNT] = {
	{ "res/mipmap-xxxhdpi-v4/icon_background.webp", 432 },
	{ "res/mipmap-xxhdpi-v4/icon_background.webp", 324 },
	{ "res/mipmap-xhdpi-v4/icon_background.webp", 216 },
	{ "res/mipmap-hdpi-v4/icon_background.webp", 162 },
	{ "res/mipmap-mdpi-v4/icon_background.webp", 108 },
	{ "res/mipmap/icon_background.webp", 432 }
};

static const LauncherIcon LAUNCHER_ADAPTIVE_ICON_MONOCHROMES[ICON_DENSITIES_COUNT] = {
	{ "res/mipmap-xxxhdpi-v4/icon_monochrome.webp", 432 },
	{ "res/mipmap-xxhdpi-v4/icon_monochrome.webp", 324 },
	{ "res/mipmap-xhdpi-v4/icon_monochrome.webp", 216 },
	{ "res/mipmap-hdpi-v4/icon_monochrome.webp", 162 },
	{ "res/mipmap-mdpi-v4/icon_monochrome.webp", 108 },
	{ "res/mipmap/icon_monochrome.webp", 432 }
};

static const int EXPORT_FORMAT_APK = 0;
static const int EXPORT_FORMAT_AAB = 1;

static const char *APK_ASSETS_DIRECTORY = "src/main/assets";
static const char *AAB_ASSETS_DIRECTORY = "assetPackInstallTime/src/main/assets";

static const int DEFAULT_MIN_SDK_VERSION = 24; // Should match the value in 'platform/android/java/app/config.gradle#minSdk'
static const int VULKAN_MIN_SDK_VERSION = 29; // Minimum recommended sdk version for Vulkan 1.1 support. See https://developer.android.com/games/develop/vulkan/native-engine-support#recommendations
static const int DEFAULT_TARGET_SDK_VERSION = 36; // Should match the value in 'platform/android/java/app/config.gradle#targetSdk'

void EditorExportPlatformAndroid::_check_for_changes_poll_thread(void *ud) {
	if (!EditorSettings::get_singleton()) {
		// Some methods called here query editor settings, so we need it to be ready first.
		// If it's not ready yet, just wait for the next iteration.
		return;
	}

	EditorExportPlatformAndroid *ea = static_cast<EditorExportPlatformAndroid *>(ud);

	while (!ea->quit_request.is_set()) {
		// Check for devices updates
		String adb = get_adb_path();
		// adb.exe was locking the editor_doc_cache file on startup. Adding a check for is_editor_ready provides just enough time
		// to regenerate the doc cache.
		if (ea->has_runnable_preset.is_set() && FileAccess::exists(adb) && EditorNode::get_singleton()->is_editor_ready()) {
			String devices;
			List<String> args;
			args.push_back("devices");
			int ec;
			OS::get_singleton()->execute(adb, args, &devices, &ec);

			Vector<String> ds = devices.split("\n");
			Vector<String> ldevices;
			for (int i = 1; i < ds.size(); i++) {
				String d = ds[i];
				int dpos = d.find("device");
				if (dpos == -1) {
					continue;
				}
				d = d.substr(0, dpos).strip_edges();
				ldevices.push_back(d);
			}

			MutexLock lock(ea->device_lock);

			bool different = false;

			if (ea->devices.size() != ldevices.size()) {
				different = true;
			} else {
				for (int i = 0; i < ea->devices.size(); i++) {
					if (ea->devices[i].id != ldevices[i]) {
						different = true;
						break;
					}
				}
			}

			if (different) {
				Vector<Device> ndevices;

				for (int i = 0; i < ldevices.size(); i++) {
					Device d;
					d.id = ldevices[i];
					for (int j = 0; j < ea->devices.size(); j++) {
						if (ea->devices[j].id == ldevices[i]) {
							d.description = ea->devices[j].description;
							d.name = ea->devices[j].name;
							d.api_level = ea->devices[j].api_level;
						}
					}

					if (d.description.is_empty()) {
						//in the oven, request!
						args.clear();
						args.push_back("-s");
						args.push_back(d.id);
						args.push_back("shell");
						args.push_back("getprop");
						int ec2;
						String dp;

						OS::get_singleton()->execute(adb, args, &dp, &ec2);

						Vector<String> props = dp.split("\n");
						String vendor;
						String device;
						d.description = "Device ID: " + d.id + "\n";
						d.api_level = 0;
						for (int j = 0; j < props.size(); j++) {
							// got information by `shell cat /system/build.prop` before and its format is "property=value"
							// it's now changed to use `shell getporp` because of permission issue with Android 8.0 and above
							// its format is "[property]: [value]" so changed it as like build.prop
							String p = props[j];
							p = p.replace("]: ", "=");
							p = p.remove_chars("[]");

							if (p.begins_with("ro.product.model=")) {
								device = p.get_slicec('=', 1).strip_edges();
							} else if (p.begins_with("ro.product.brand=")) {
								vendor = p.get_slicec('=', 1).strip_edges().capitalize();
							} else if (p.begins_with("ro.build.display.id=")) {
								d.description += "Build: " + p.get_slicec('=', 1).strip_edges() + "\n";
							} else if (p.begins_with("ro.build.version.release=")) {
								d.description += "Release: " + p.get_slicec('=', 1).strip_edges() + "\n";
							} else if (p.begins_with("ro.build.version.sdk=")) {
								d.api_level = p.get_slicec('=', 1).to_int();
							} else if (p.begins_with("ro.product.cpu.abi=")) {
								d.architecture = p.get_slicec('=', 1).strip_edges();
								d.description += "CPU: " + d.architecture + "\n";
							} else if (p.begins_with("ro.product.manufacturer=")) {
								d.description += "Manufacturer: " + p.get_slicec('=', 1).strip_edges() + "\n";
							} else if (p.begins_with("ro.board.platform=")) {
								d.description += "Chipset: " + p.get_slicec('=', 1).strip_edges() + "\n";
							} else if (p.begins_with("ro.opengles.version=")) {
								uint32_t opengl = p.get_slicec('=', 1).to_int();
								d.description += "OpenGL: " + itos(opengl >> 16) + "." + itos((opengl >> 8) & 0xFF) + "." + itos((opengl) & 0xFF) + "\n";
							}
						}

						d.name = vendor + " " + device;
						if (device.is_empty()) {
							continue;
						}
					}

					ndevices.push_back(d);
				}

				ea->devices = ndevices;
				ea->devices_changed.set();
			}
		}

		uint64_t sleep = 200;
		uint64_t wait = 3000000;
		uint64_t time = OS::get_singleton()->get_ticks_usec();
		while (OS::get_singleton()->get_ticks_usec() - time < wait) {
			OS::get_singleton()->delay_usec(1000 * sleep);
			if (ea->quit_request.is_set()) {
				break;
			}
		}
	}

	if (ea->has_runnable_preset.is_set() && EDITOR_GET("export/android/shutdown_adb_on_exit")) {
		String adb = get_adb_path();
		if (!FileAccess::exists(adb)) {
			return; //adb not configured
		}

		List<String> args;
		args.push_back("kill-server");
		OS::get_singleton()->execute(adb, args);
	}
}

void EditorExportPlatformAndroid::_update_preset_status() {
	const int preset_count = EditorExport::get_singleton()->get_export_preset_count();
	bool has_runnable = false;

	for (int i = 0; i < preset_count; i++) {
		const Ref<EditorExportPreset> &preset = EditorExport::get_singleton()->get_export_preset(i);
		if (preset->get_platform() == this && preset->is_runnable()) {
			has_runnable = true;
			break;
		}
	}

	if (has_runnable) {
		has_runnable_preset.set();
		_start_check_for_changes_poll_thread();
	} else {
		has_runnable_preset.clear();
		_stop_check_for_changes_poll_thread();
	}
	devices_changed.set();
}

void EditorExportPlatformAndroid::_start_check_for_changes_poll_thread() {
	quit_request.clear();
	if (!check_for_changes_thread.is_started()) {
		check_for_changes_thread.start(_check_for_changes_poll_thread, this);
	}
}

void EditorExportPlatformAndroid::_stop_check_for_changes_poll_thread() {
	quit_request.set();
	if (check_for_changes_thread.is_started()) {
		check_for_changes_thread.wait_to_finish();
	}
}

String EditorExportPlatformAndroid::get_project_name(const Ref<EditorExportPreset> &p_preset, const String &p_name) const {
	String aname;
	if (!p_name.is_empty()) {
		aname = p_name;
	} else {
		aname = get_project_setting(p_preset, "application/config/name");
	}

	if (aname.is_empty()) {
		aname = FOUNDRY_VERSION_NAME;
	}

	return aname;
}

String EditorExportPlatformAndroid::get_package_name(const Ref<EditorExportPreset> &p_preset, const String &p_package) const {
	String pname = p_package;
	String name = get_valid_basename(p_preset);
	pname = pname.replace("$genname", name);
	return pname;
}

// Returns the project name without invalid characters
// or the "noname" string if all characters are invalid.
String EditorExportPlatformAndroid::get_valid_basename(const Ref<EditorExportPreset> &p_preset) const {
	String basename = get_project_setting(p_preset, "application/config/name");
	basename = basename.to_lower();

	String name;
	bool first = true;
	for (int i = 0; i < basename.length(); i++) {
		char32_t c = basename[i];
		if (is_digit(c) && first) {
			continue;
		}
		if (is_ascii_identifier_char(c)) {
			name += String::chr(c);
			first = false;
		}
	}

	if (name.is_empty()) {
		name = "noname";
	}

	return name;
}

String EditorExportPlatformAndroid::get_assets_directory(const Ref<EditorExportPreset> &p_preset, int p_export_format) const {
	String gradle_build_directory = ExportTemplateManager::get_android_build_directory(p_preset);
	return gradle_build_directory.path_join(p_export_format == EXPORT_FORMAT_AAB ? AAB_ASSETS_DIRECTORY : APK_ASSETS_DIRECTORY);
}

bool EditorExportPlatformAndroid::is_package_name_valid(const Ref<EditorExportPreset> &p_preset, const String &p_package, String *r_error) const {
	String pname = get_package_name(p_preset, p_package);

	if (pname.length() == 0) {
		if (r_error) {
			*r_error = TTR("Package name is missing.");
		}
		return false;
	}

	int segments = 0;
	bool first = true;
	for (int i = 0; i < pname.length(); i++) {
		char32_t c = pname[i];
		if (first && c == '.') {
			if (r_error) {
				*r_error = TTR("Package segments must be of non-zero length.");
			}
			return false;
		}
		if (c == '.') {
			segments++;
			first = true;
			continue;
		}
		if (!is_ascii_identifier_char(c)) {
			if (r_error) {
				*r_error = vformat(TTR("The character '%s' is not allowed in Android application package names."), String::chr(c));
			}
			return false;
		}
		if (first && is_digit(c)) {
			if (r_error) {
				*r_error = TTR("A digit cannot be the first character in a package segment.");
			}
			return false;
		}
		if (first && is_underscore(c)) {
			if (r_error) {
				*r_error = vformat(TTR("The character '%s' cannot be the first character in a package segment."), String::chr(c));
			}
			return false;
		}
		first = false;
	}

	if (segments == 0) {
		if (r_error) {
			*r_error = TTR("The package must have at least one '.' separator.");
		}
		return false;
	}

	if (first) {
		if (r_error) {
			*r_error = TTR("Package segments must be of non-zero length.");
		}
		return false;
	}

	return true;
}

bool EditorExportPlatformAndroid::is_project_name_valid(const Ref<EditorExportPreset> &p_preset) const {
	// Get the original project name and convert to lowercase.
	String basename = get_project_setting(p_preset, "application/config/name");
	basename = basename.to_lower();
	// Check if there are invalid characters.
	if (basename != get_valid_basename(p_preset)) {
		return false;
	}
	return true;
}

bool EditorExportPlatformAndroid::_should_compress_asset(const String &p_path, const Vector<uint8_t> &p_data) {
	/*
	 *  By not compressing files with little or no benefit in doing so,
	 *  a performance gain is expected at runtime. Moreover, if the APK is
	 *  zip-aligned, assets stored as they are can be efficiently read by
	 *  Android by memory-mapping them.
	 */

	// -- Unconditional uncompress to mimic AAPT plus some other

	static const char *unconditional_compress_ext[] = {
		// From https://github.com/android/platform_frameworks_base/blob/master/tools/aapt/Package.cpp
		// These formats are already compressed, or don't compress well:
		".jpg", ".jpeg", ".png", ".gif",
		".wav", ".mp2", ".mp3", ".ogg", ".aac",
		".mpg", ".mpeg", ".mid", ".midi", ".smf", ".jet",
		".rtttl", ".imy", ".xmf", ".mp4", ".m4a",
		".m4v", ".3gp", ".3gpp", ".3g2", ".3gpp2",
		".amr", ".awb", ".wma", ".wmv",
		// Godot-specific:
		".webp", // Same reasoning as .png
		".cfb", // Don't let small config files slow-down startup
		".scn", // Binary scenes are usually already compressed
		".ctex", // Streamable textures are usually already compressed
		".pck", // Pack.
		// Trailer for easier processing
		nullptr
	};

	for (const char **ext = unconditional_compress_ext; *ext; ++ext) {
		if (p_path.to_lower().ends_with(String(*ext))) {
			return false;
		}
	}

	// -- Compressed resource?

	if (p_data.size() >= 4 && p_data[0] == 'R' && p_data[1] == 'S' && p_data[2] == 'C' && p_data[3] == 'C') {
		// Already compressed
		return false;
	}

	// --- TODO: Decide on texture resources according to their image compression setting

	return true;
}

zip_fileinfo EditorExportPlatformAndroid::get_zip_fileinfo() {
	OS::DateTime dt = OS::get_singleton()->get_datetime();

	zip_fileinfo zipfi;
	zipfi.tmz_date.tm_year = dt.year;
	zipfi.tmz_date.tm_mon = dt.month - 1; // tm_mon is zero indexed
	zipfi.tmz_date.tm_mday = dt.day;
	zipfi.tmz_date.tm_hour = dt.hour;
	zipfi.tmz_date.tm_min = dt.minute;
	zipfi.tmz_date.tm_sec = dt.second;
	zipfi.dosDate = 0;
	zipfi.external_fa = 0;
	zipfi.internal_fa = 0;

	return zipfi;
}

Vector<EditorExportPlatformAndroid::ABI> EditorExportPlatformAndroid::get_abis() {
	// Should have the same order and size as get_archs.
	Vector<ABI> abis;
	abis.push_back(ABI("armeabi-v7a", "arm32"));
	abis.push_back(ABI("arm64-v8a", "arm64"));
	abis.push_back(ABI("x86", "x86_32"));
	abis.push_back(ABI("x86_64", "x86_64"));
	return abis;
}

Error EditorExportPlatformAndroid::store_in_apk(APKExportData *ed, const String &p_path, const Vector<uint8_t> &p_data, int compression_method) {
	zip_fileinfo zipfi = get_zip_fileinfo();
	zipOpenNewFileInZip(ed->apk,
			p_path.utf8().get_data(),
			&zipfi,
			nullptr,
			0,
			nullptr,
			0,
			nullptr,
			compression_method,
			Z_DEFAULT_COMPRESSION);

	zipWriteInFileInZip(ed->apk, p_data.ptr(), p_data.size());
	zipCloseFileInZip(ed->apk);

	return OK;
}

Error EditorExportPlatformAndroid::save_apk_so(const Ref<EditorExportPreset> &p_preset, void *p_userdata, const SharedObject &p_so) {
	if (!p_so.path.get_file().begins_with("lib")) {
		String err = "Android .so file names must start with \"lib\", but got: " + p_so.path;
		ERR_PRINT(err);
		return FAILED;
	}
	APKExportData *ed = static_cast<APKExportData *>(p_userdata);
	Vector<ABI> abis = get_abis();
	bool exported = false;
	for (int i = 0; i < p_so.tags.size(); ++i) {
		// shared objects can be fat (compatible with multiple ABIs)
		int abi_index = -1;
		for (int j = 0; j < abis.size(); ++j) {
			if (abis[j].abi == p_so.tags[i] || abis[j].arch == p_so.tags[i]) {
				abi_index = j;
				break;
			}
		}
		if (abi_index != -1) {
			exported = true;
			String abi = abis[abi_index].abi;
			String dst_path = String("lib").path_join(abi).path_join(p_so.path.get_file());
			Vector<uint8_t> array = FileAccess::get_file_as_bytes(p_so.path);
			Error store_err = store_in_apk(ed, dst_path, array, Z_NO_COMPRESSION);
			ERR_FAIL_COND_V_MSG(store_err, store_err, "Cannot store in apk file '" + dst_path + "'.");
		}
	}
	if (!exported) {
		ERR_PRINT("Cannot determine architecture for library \"" + p_so.path + "\". One of the supported architectures must be used as a tag: " + join_abis(abis, " ", true));
		return FAILED;
	}
	return OK;
}

Error EditorExportPlatformAndroid::save_apk_file(const Ref<EditorExportPreset> &p_preset, void *p_userdata, const String &p_path, const Vector<uint8_t> &p_data, int p_file, int p_total, const Vector<String> &p_enc_in_filters, const Vector<String> &p_enc_ex_filters, const Vector<uint8_t> &p_key, uint64_t p_seed, bool p_delta) {
	APKExportData *ed = static_cast<APKExportData *>(p_userdata);

	const String simplified_path = simplify_path(p_path);

	Vector<uint8_t> enc_data;
	EditorExportPlatform::SavedData sd;
	Error err = _store_temp_file(simplified_path, p_data, p_enc_in_filters, p_enc_ex_filters, p_key, p_seed, p_delta, enc_data, sd);
	if (err != OK) {
		return err;
	}

	const String dst_path = String("assets/") + simplified_path.trim_prefix("res://");
	print_verbose("Saving project files from " + simplified_path + " into " + dst_path);
	store_in_apk(ed, dst_path, enc_data, _should_compress_asset(simplified_path, enc_data) ? Z_DEFLATED : 0);

	ed->pd.file_ofs.push_back(sd);

	return OK;
}

Error EditorExportPlatformAndroid::ignore_apk_file(const Ref<EditorExportPreset> &p_preset, void *p_userdata, const String &p_path, const Vector<uint8_t> &p_data, int p_file, int p_total, const Vector<String> &p_enc_in_filters, const Vector<String> &p_enc_ex_filters, const Vector<uint8_t> &p_key, uint64_t p_seed, bool p_delta) {
	return OK;
}

Error EditorExportPlatformAndroid::copy_gradle_so(const Ref<EditorExportPreset> &p_preset, void *p_userdata, const SharedObject &p_so) {
	ERR_FAIL_COND_V_MSG(!p_so.path.get_file().begins_with("lib"), FAILED,
			"Android .so file names must start with \"lib\", but got: " + p_so.path);
	Vector<ABI> abis = get_abis();
	CustomExportData *export_data = static_cast<CustomExportData *>(p_userdata);
	bool exported = false;
	for (int i = 0; i < p_so.tags.size(); ++i) {
		int abi_index = -1;
		for (int j = 0; j < abis.size(); ++j) {
			if (abis[j].abi == p_so.tags[i] || abis[j].arch == p_so.tags[i]) {
				abi_index = j;
				break;
			}
		}
		if (abi_index != -1) {
			exported = true;
			String type = export_data->debug ? "debug" : "release";
			String abi = abis[abi_index].abi;
			String filename = p_so.path.get_file();
			String dst_path = export_data->libs_directory.path_join(type).path_join(abi).path_join(filename);
			Vector<uint8_t> data = FileAccess::get_file_as_bytes(p_so.path);
			print_verbose("Copying .so file from " + p_so.path + " to " + dst_path);
			Error err = store_file_at_path(dst_path, data);
			ERR_FAIL_COND_V_MSG(err, err, "Failed to copy .so file from " + p_so.path + " to " + dst_path);
			export_data->libs.push_back(dst_path);
		}
	}
	ERR_FAIL_COND_V_MSG(!exported, FAILED,
			"Cannot determine architecture for library \"" + p_so.path + "\". One of the supported architectures must be used as a tag:" + join_abis(abis, " ", true));
	return OK;
}

bool EditorExportPlatformAndroid::_has_read_write_storage_permission(const Vector<String> &p_permissions) {
	return p_permissions.has("android.permission.READ_EXTERNAL_STORAGE") || p_permissions.has("android.permission.WRITE_EXTERNAL_STORAGE");
}

bool EditorExportPlatformAndroid::_has_manage_external_storage_permission(const Vector<String> &p_permissions) {
	return p_permissions.has("android.permission.MANAGE_EXTERNAL_STORAGE");
}

bool EditorExportPlatformAndroid::_uses_vulkan(const Ref<EditorExportPreset> &p_preset) const {
	String rendering_method = get_project_setting(p_preset, "rendering/renderer/rendering_method.mobile");
	String rendering_driver = get_project_setting(p_preset, "rendering/rendering_device/driver.android");
	return (rendering_method == "forward_plus" || rendering_method == "mobile") && rendering_driver == "vulkan";
}

void EditorExportPlatformAndroid::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_POSTINITIALIZE: {
			if (EditorExport::get_singleton()) {
				EditorExport::get_singleton()->connect_presets_runnable_updated(callable_mp(this, &EditorExportPlatformAndroid::_update_preset_status));
			}
		} break;

		case EditorSettings::NOTIFICATION_EDITOR_SETTINGS_CHANGED: {
			if (EditorSettings::get_singleton()->check_changed_settings_in_group("export/android")) {
				_create_editor_debug_keystore_if_needed();
			}
		} break;
	}
}

void EditorExportPlatformAndroid::_create_editor_debug_keystore_if_needed() {
	// Check if we have a valid keytool path.
	String keytool_path = get_keytool_path();
	if (!FileAccess::exists(keytool_path)) {
		return;
	}

	// Check if the current editor debug keystore exists.
	String editor_debug_keystore = EDITOR_GET("export/android/debug_keystore");
	if (FileAccess::exists(editor_debug_keystore)) {
		return;
	}

	// Generate the debug keystore.
	String keystore_path = EditorPaths::get_singleton()->get_debug_keystore_path();
	String keystores_dir = keystore_path.get_base_dir();
	if (!DirAccess::exists(keystores_dir)) {
		Ref<DirAccess> dir_access = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		Error err = dir_access->make_dir_recursive(keystores_dir);
		if (err != OK) {
			WARN_PRINT(TTR("Error creating keystores directory:") + "\n" + keystores_dir);
			return;
		}
	}

	if (!FileAccess::exists(keystore_path)) {
		String output;
		List<String> args;
		args.push_back("-genkey");
		args.push_back("-keystore");
		args.push_back(keystore_path);
		args.push_back("-storepass");
		args.push_back("android");
		args.push_back("-alias");
		args.push_back(DEFAULT_ANDROID_KEYSTORE_DEBUG_USER);
		args.push_back("-keypass");
		args.push_back(DEFAULT_ANDROID_KEYSTORE_DEBUG_PASSWORD);
		args.push_back("-keyalg");
		args.push_back("RSA");
		args.push_back("-keysize");
		args.push_back("2048");
		args.push_back("-validity");
		args.push_back("10000");
		args.push_back("-dname");
		args.push_back("cn=Foundry, ou=Foundry, o=Cafecito Games, c=US");
		Error error = OS::get_singleton()->execute(keytool_path, args, &output, nullptr, true);
		print_verbose(output);
		if (error != OK) {
			WARN_PRINT("Error: Unable to create debug keystore");
			return;
		}
	}

	// Update the editor settings.
	EditorSettings::get_singleton()->set("export/android/debug_keystore", keystore_path);
	EditorSettings::get_singleton()->set("export/android/debug_keystore_user", DEFAULT_ANDROID_KEYSTORE_DEBUG_USER);
	EditorSettings::get_singleton()->set("export/android/debug_keystore_pass", DEFAULT_ANDROID_KEYSTORE_DEBUG_PASSWORD);
	print_verbose("Updated editor debug keystore to " + keystore_path);
}

void EditorExportPlatformAndroid::_get_manifest_info(const Ref<EditorExportPreset> &p_preset, bool p_give_internet, Vector<String> &r_permissions, Vector<FeatureInfo> &r_features, Vector<MetadataInfo> &r_metadata) {
	const char **aperms = ANDROID_PERMS;
	while (*aperms) {
		bool enabled = p_preset->get("permissions/" + String(*aperms).to_lower());
		if (enabled) {
			r_permissions.push_back("android.permission." + String(*aperms));
		}
		aperms++;
	}
	PackedStringArray user_perms = p_preset->get("permissions/custom_permissions");
	for (int i = 0; i < user_perms.size(); i++) {
		String user_perm = user_perms[i].strip_edges();
		if (!user_perm.is_empty()) {
			r_permissions.push_back(user_perm);
		}
	}
	if (p_give_internet) {
		if (!r_permissions.has("android.permission.INTERNET")) {
			r_permissions.push_back("android.permission.INTERNET");
		}
	}

	if (_uses_vulkan(p_preset)) {
		// Optionally request vulkan hardware level 1 support.
		FeatureInfo vulkan_level = {
			"android.hardware.vulkan.level", // name
			false, // required
			"1" // version
		};
		r_features.append(vulkan_level);

		// Require vulkan version 1.1 if fallback_to_opengl3 is disabled.
		bool vulkan_1_1_required = !GLOBAL_GET("rendering/rendering_device/fallback_to_opengl3");
		FeatureInfo vulkan_version = {
			"android.hardware.vulkan.version", // name
			vulkan_1_1_required, // required
			"0x401000" // version - Encoded value for api version 1.1
		};
		r_features.append(vulkan_version);
	}

	MetadataInfo rendering_method_metadata = {
		"org.godotengine.rendering.method",
		p_preset->get_project_setting("rendering/renderer/rendering_method.mobile")
	};
	r_metadata.append(rendering_method_metadata);

	MetadataInfo editor_version_metadata = {
		"org.godotengine.editor.version",
		String(FOUNDRY_VERSION_FULL_CONFIG)
	};
	r_metadata.append(editor_version_metadata);
}

void EditorExportPlatformAndroid::_write_tmp_manifest(const Ref<EditorExportPreset> &p_preset, bool p_give_internet, bool p_debug) {
	print_verbose("Building temporary manifest...");
	String manifest_text =
			"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
			"<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"\n"
			"    xmlns:tools=\"http://schemas.android.com/tools\">\n";

	manifest_text += _get_screen_sizes_tag(p_preset);
	manifest_text += _get_gles_tag();

	Vector<String> perms;
	Vector<FeatureInfo> features;
	Vector<MetadataInfo> manifest_metadata;
	_get_manifest_info(p_preset, p_give_internet, perms, features, manifest_metadata);
	for (int i = 0; i < perms.size(); i++) {
		String permission = perms.get(i);
		if (permission == "android.permission.WRITE_EXTERNAL_STORAGE" || (permission == "android.permission.READ_EXTERNAL_STORAGE" && _has_manage_external_storage_permission(perms))) {
			manifest_text += vformat("    <uses-permission android:name=\"%s\" android:maxSdkVersion=\"29\" />\n", permission);
		} else {
			manifest_text += vformat("    <uses-permission android:name=\"%s\" />\n", permission);
		}
	}

	for (int i = 0; i < features.size(); i++) {
		manifest_text += vformat("    <uses-feature tools:node=\"replace\" android:name=\"%s\" android:required=\"%s\" android:version=\"%s\" />\n", features[i].name, features[i].required, features[i].version);
	}

	Vector<Ref<EditorExportPlugin>> export_plugins = EditorExport::get_singleton()->get_export_plugins();
	for (int i = 0; i < export_plugins.size(); i++) {
		if (export_plugins[i]->supports_platform(Ref<EditorExportPlatform>(this))) {
			const String contents = export_plugins[i]->get_android_manifest_element_contents(Ref<EditorExportPlatform>(this), p_debug);
			if (!contents.is_empty()) {
				const String export_plugin_name = export_plugins[i]->get_name();
				manifest_text += "<!-- Start of manifest element contents from " + export_plugin_name + " -->\n";
				manifest_text += contents;
				manifest_text += "\n";
				manifest_text += "<!-- End of manifest element contents from " + export_plugin_name + " -->\n";
			}
		}
	}

	manifest_text += _get_application_tag(Ref<EditorExportPlatform>(this), p_preset, _has_read_write_storage_permission(perms), p_debug, manifest_metadata);
	manifest_text += "</manifest>\n";
	String manifest_path = ExportTemplateManager::get_android_build_directory(p_preset).path_join(vformat("src/%s/AndroidManifest.xml", (p_debug ? "debug" : "release")));

	print_verbose("Storing manifest into " + manifest_path + ": " + "\n" + manifest_text);
	store_string_at_path(manifest_path, manifest_text);
}

bool EditorExportPlatformAndroid::_is_transparency_allowed(const Ref<EditorExportPreset> &p_preset) const {
	return (bool)get_project_setting(p_preset, "display/window/per_pixel_transparency/allowed");
}

void EditorExportPlatformAndroid::_fix_themes_xml(const Ref<EditorExportPreset> &p_preset) {
	const String themes_xml_path = ExportTemplateManager::get_android_build_directory(p_preset).path_join("res/values/themes.xml");

	if (!FileAccess::exists(themes_xml_path)) {
		print_error("res/values/themes.xml does not exist.");
		return;
	}

	bool transparency_allowed = _is_transparency_allowed(p_preset);

	// Default/Reserved theme attributes.
	Dictionary main_theme_attributes;
	main_theme_attributes["android:windowSwipeToDismiss"] = bool_to_string(p_preset->get("gesture/swipe_to_dismiss"));
	main_theme_attributes["android:windowIsTranslucent"] = bool_to_string(transparency_allowed);
	if (transparency_allowed) {
		main_theme_attributes["android:windowBackground"] = "@android:color/transparent";
	} else {
		main_theme_attributes["android:windowBackground"] = "#" + p_preset->get("screen/background_color").operator Color().to_html(false);
	}

	Dictionary splash_theme_attributes;
	splash_theme_attributes["android:windowSplashScreenBackground"] = "@mipmap/icon_background";
	splash_theme_attributes["windowSplashScreenAnimatedIcon"] = "@mipmap/icon_foreground";
	splash_theme_attributes["postSplashScreenTheme"] = "@style/FoundryAppMainTheme";
	splash_theme_attributes["android:windowIsTranslucent"] = bool_to_string(transparency_allowed);

	PackedStringArray reserved_splash_keys;
	reserved_splash_keys.append("postSplashScreenTheme");
	reserved_splash_keys.append("android:windowIsTranslucent");

	Dictionary custom_theme_attributes = p_preset->get("gradle_build/custom_theme_attributes");

	// Does not override default/reserved theme attributes; skips any duplicates from custom_theme_attributes.
	for (const Variant &k : custom_theme_attributes.keys()) {
		String key = k;
		String value = custom_theme_attributes[k];
		if (key.begins_with("[splash]")) {
			String splash_key = key.trim_prefix("[splash]");
			if (reserved_splash_keys.has(splash_key)) {
				WARN_PRINT(vformat("Skipped custom_theme_attribute '%s'; this is a reserved attribute configured via other export options or project settings.", splash_key));
			} else {
				splash_theme_attributes[splash_key] = value;
			}
		} else {
			if (main_theme_attributes.has(key)) {
				WARN_PRINT(vformat("Skipped custom_theme_attribute '%s'; this is a reserved attribute configured via other export options or project settings.", key));
			} else {
				main_theme_attributes[key] = value;
			}
		}
	}

	Ref<FileAccess> file = FileAccess::open(themes_xml_path, FileAccess::READ);
	PackedStringArray lines = file->get_as_text().split("\n");
	file->close();

	PackedStringArray new_lines;
	bool inside_main_theme = false;
	bool inside_splash_theme = false;

	for (int i = 0; i < lines.size(); i++) {
		String line = lines[i];

		if (line.contains("<style name=\"FoundryAppMainTheme\"")) {
			inside_main_theme = true;
			new_lines.append(line);
			continue;
		}
		if (line.contains("<style name=\"FoundryAppSplashTheme\"")) {
			inside_splash_theme = true;
			new_lines.append(line);
			continue;
		}

		// Inject FoundryAppMainTheme attributes.
		if (inside_main_theme && line.contains("</style>")) {
			for (const Variant &attribute : main_theme_attributes.keys()) {
				String value = main_theme_attributes[attribute];
				String item_line = vformat("		<item name=\"%s\">%s</item>", attribute, value);
				new_lines.append(item_line);
			}
			new_lines.append(line); // Add </style> in the end.
			inside_main_theme = false;
			continue;
		}

		// Inject FoundryAppSplashTheme attributes.
		if (inside_splash_theme && line.contains("</style>")) {
			for (const Variant &attribute : splash_theme_attributes.keys()) {
				String value = splash_theme_attributes[attribute];
				String item_line = vformat("		<item name=\"%s\">%s</item>", attribute, value);
				new_lines.append(item_line);
			}
			new_lines.append(line); // Add </style> in the end.
			inside_splash_theme = false;
			continue;
		}

		// Add all other lines unchanged.
		if (!inside_main_theme && !inside_splash_theme) {
			new_lines.append(line);
		}
	}

	// Reconstruct the XML content from the modified lines.
	String xml_content = String("\n").join(new_lines);
	store_string_at_path(themes_xml_path, xml_content);
	print_verbose("Successfully modified " + themes_xml_path + ": " + "\n" + xml_content);
}

void EditorExportPlatformAndroid::_fix_manifest(const Ref<EditorExportPreset> &p_preset, Vector<uint8_t> &p_manifest, bool p_give_internet) {
	// Leaving the unused types commented because looking these constants up
	// again later would be annoying
	// const int CHUNK_AXML_FILE = 0x00080003;
	// const int CHUNK_RESOURCEIDS = 0x00080180;
	const int CHUNK_STRINGS = 0x001C0001;
	// const int CHUNK_XML_END_NAMESPACE = 0x00100101;
	const int CHUNK_XML_END_TAG = 0x00100103;
	// const int CHUNK_XML_START_NAMESPACE = 0x00100100;
	const int CHUNK_XML_START_TAG = 0x00100102;
	// const int CHUNK_XML_TEXT = 0x00100104;
	const int UTF8_FLAG = 0x00000100;

	Vector<String> string_table;

	uint32_t ofs = 8;

	uint32_t string_count = 0;
	uint32_t string_flags = 0;
	uint32_t string_data_offset = 0;

	uint32_t string_table_begins = 0;
	uint32_t string_table_ends = 0;
	Vector<uint8_t> stable_extra;

	String version_name = p_preset->get_version("version/name");
	int version_code = p_preset->get("version/code");
	String package_name = p_preset->get("package/unique_name");

	const int screen_orientation =
			_get_android_orientation_value(DisplayServer::ScreenOrientation(int(get_project_setting(p_preset, "display/window/handheld/orientation"))));

	bool screen_support_small = p_preset->get("screen/support_small");
	bool screen_support_normal = p_preset->get("screen/support_normal");
	bool screen_support_large = p_preset->get("screen/support_large");
	bool screen_support_xlarge = p_preset->get("screen/support_xlarge");

	bool backup_allowed = p_preset->get("user_data_backup/allow");
	int app_category = p_preset->get("package/app_category");
	bool retain_data_on_uninstall = p_preset->get("package/retain_data_on_uninstall");
	bool exclude_from_recents = p_preset->get("package/exclude_from_recents");
	bool is_resizeable = bool(get_project_setting(p_preset, "display/window/size/resizable"));

	Vector<String> perms;
	Vector<FeatureInfo> features;
	Vector<MetadataInfo> manifest_metadata;
	_get_manifest_info(p_preset, p_give_internet, perms, features, manifest_metadata);
	bool has_read_write_storage_permission = _has_read_write_storage_permission(perms);

	while (ofs < (uint32_t)p_manifest.size()) {
		uint32_t chunk = decode_uint32(&p_manifest[ofs]);
		uint32_t size = decode_uint32(&p_manifest[ofs + 4]);

		switch (chunk) {
			case CHUNK_STRINGS: {
				int iofs = ofs + 8;

				string_count = decode_uint32(&p_manifest[iofs]);
				string_flags = decode_uint32(&p_manifest[iofs + 8]);
				string_data_offset = decode_uint32(&p_manifest[iofs + 12]);

				uint32_t st_offset = iofs + 20;
				string_table.resize(string_count);
				uint32_t string_end = 0;

				string_table_begins = st_offset;

				for (uint32_t i = 0; i < string_count; i++) {
					uint32_t string_at = decode_uint32(&p_manifest[st_offset + i * 4]);
					string_at += st_offset + string_count * 4;

					ERR_FAIL_COND_MSG(string_flags & UTF8_FLAG, "Unimplemented, can't read UTF-8 string table.");

					if (string_flags & UTF8_FLAG) {
					} else {
						uint32_t len = decode_uint16(&p_manifest[string_at]);
						Vector<char32_t> ucstring;
						ucstring.resize(len + 1);
						for (uint32_t j = 0; j < len; j++) {
							uint16_t c = decode_uint16(&p_manifest[string_at + 2 + 2 * j]);
							ucstring.write[j] = c;
						}
						string_end = MAX(string_at + 2 + 2 * len, string_end);
						ucstring.write[len] = 0;
						string_table.write[i] = ucstring.ptr();
					}
				}

				for (uint32_t i = string_end; i < (ofs + size); i++) {
					stable_extra.push_back(p_manifest[i]);
				}

				string_table_ends = ofs + size;

			} break;
			case CHUNK_XML_START_TAG: {
				int iofs = ofs + 8;
				uint32_t name = decode_uint32(&p_manifest[iofs + 12]);

				String tname = string_table[name];
				uint32_t attrcount = decode_uint32(&p_manifest[iofs + 20]);
				iofs += 28;

				for (uint32_t i = 0; i < attrcount; i++) {
					uint32_t attr_nspace = decode_uint32(&p_manifest[iofs]);
					uint32_t attr_name = decode_uint32(&p_manifest[iofs + 4]);
					uint32_t attr_value = decode_uint32(&p_manifest[iofs + 8]);
					uint32_t attr_resid = decode_uint32(&p_manifest[iofs + 16]);

					const String value = (attr_value != 0xFFFFFFFF) ? string_table[attr_value] : "Res #" + itos(attr_resid);
					String attrname = string_table[attr_name];
					const String nspace = (attr_nspace != 0xFFFFFFFF) ? string_table[attr_nspace] : "";

					//replace project information
					if (tname == "manifest" && attrname == "package") {
						string_table.write[attr_value] = get_package_name(p_preset, package_name);
					}

					if (tname == "manifest" && attrname == "versionCode") {
						encode_uint32(version_code, &p_manifest.write[iofs + 16]);
					}

					if (tname == "manifest" && attrname == "versionName") {
						if (attr_value == 0xFFFFFFFF) {
							WARN_PRINT("Version name in a resource, should be plain text");
						} else {
							string_table.write[attr_value] = version_name;
						}
					}

					if (tname == "application" && attrname == "requestLegacyExternalStorage") {
						encode_uint32(has_read_write_storage_permission ? 0xFFFFFFFF : 0, &p_manifest.write[iofs + 16]);
					}

					if (tname == "application" && attrname == "allowBackup") {
						encode_uint32(backup_allowed, &p_manifest.write[iofs + 16]);
					}

					if (tname == "application" && attrname == "appCategory") {
						encode_uint32(_get_app_category_value(app_category), &p_manifest.write[iofs + 16]);
					}

					if (tname == "application" && attrname == "isGame") {
						encode_uint32(app_category == APP_CATEGORY_GAME, &p_manifest.write[iofs + 16]);
					}

					if (tname == "application" && attrname == "hasFragileUserData") {
						encode_uint32(retain_data_on_uninstall, &p_manifest.write[iofs + 16]);
					}

					if (tname == "activity" && attrname == "screenOrientation") {
						encode_uint32(screen_orientation, &p_manifest.write[iofs + 16]);
					}

					if (tname == "activity" && attrname == "excludeFromRecents") {
						encode_uint32(exclude_from_recents, &p_manifest.write[iofs + 16]);
					}

					if (tname == "activity" && attrname == "resizeableActivity") {
						encode_uint32(is_resizeable, &p_manifest.write[iofs + 16]);
					}

					if (tname == "provider" && attrname == "authorities") {
						string_table.write[attr_value] = get_package_name(p_preset, package_name) + String(".fileprovider");
					}

					if (tname == "supports-screens") {
						if (attrname == "smallScreens") {
							encode_uint32(screen_support_small ? 0xFFFFFFFF : 0, &p_manifest.write[iofs + 16]);

						} else if (attrname == "normalScreens") {
							encode_uint32(screen_support_normal ? 0xFFFFFFFF : 0, &p_manifest.write[iofs + 16]);

						} else if (attrname == "largeScreens") {
							encode_uint32(screen_support_large ? 0xFFFFFFFF : 0, &p_manifest.write[iofs + 16]);

						} else if (attrname == "xlargeScreens") {
							encode_uint32(screen_support_xlarge ? 0xFFFFFFFF : 0, &p_manifest.write[iofs + 16]);
						}
					}

					iofs += 20;
				}

			} break;
			case CHUNK_XML_END_TAG: {
				int iofs = ofs + 8;
				uint32_t name = decode_uint32(&p_manifest[iofs + 12]);
				String tname = string_table[name];

				if (tname == "manifest" || tname == "application") {
					// save manifest ending so we can restore it
					Vector<uint8_t> manifest_end;
					uint32_t manifest_cur_size = p_manifest.size();

					manifest_end.resize(p_manifest.size() - ofs);
					memcpy(manifest_end.ptrw(), &p_manifest[ofs], manifest_end.size());

					int32_t attr_name_string = string_table.find("name");
					ERR_FAIL_COND_MSG(attr_name_string == -1, "Template does not have 'name' attribute.");

					int32_t ns_android_string = string_table.find("http://schemas.android.com/apk/res/android");
					if (ns_android_string == -1) {
						string_table.push_back("http://schemas.android.com/apk/res/android");
						ns_android_string = string_table.size() - 1;
					}

					if (tname == "manifest") {
						// Updating manifest features
						int32_t attr_uses_feature_string = string_table.find("uses-feature");
						if (attr_uses_feature_string == -1) {
							string_table.push_back("uses-feature");
							attr_uses_feature_string = string_table.size() - 1;
						}

						int32_t attr_required_string = string_table.find("required");
						if (attr_required_string == -1) {
							string_table.push_back("required");
							attr_required_string = string_table.size() - 1;
						}

						for (int i = 0; i < features.size(); i++) {
							const String &feature_name = features[i].name;
							bool feature_required = features[i].required;
							String feature_version = features[i].version;
							bool has_version_attribute = !feature_version.is_empty();

							print_line("Adding feature " + feature_name);

							int32_t feature_string = string_table.find(feature_name);
							if (feature_string == -1) {
								string_table.push_back(feature_name);
								feature_string = string_table.size() - 1;
							}

							String required_value_string = feature_required ? "true" : "false";
							int32_t required_value = string_table.find(required_value_string);
							if (required_value == -1) {
								string_table.push_back(required_value_string);
								required_value = string_table.size() - 1;
							}

							int32_t attr_version_string = -1;
							int32_t version_value = -1;
							int tag_size;
							int attr_count;
							if (has_version_attribute) {
								attr_version_string = string_table.find("version");
								if (attr_version_string == -1) {
									string_table.push_back("version");
									attr_version_string = string_table.size() - 1;
								}

								version_value = string_table.find(feature_version);
								if (version_value == -1) {
									string_table.push_back(feature_version);
									version_value = string_table.size() - 1;
								}

								tag_size = 96; // node and three attrs + end node
								attr_count = 3;
							} else {
								tag_size = 76; // node and two attrs + end node
								attr_count = 2;
							}
							manifest_cur_size += tag_size + 24;
							p_manifest.resize(manifest_cur_size);

							// start tag
							encode_uint16(0x102, &p_manifest.write[ofs]); // type
							encode_uint16(16, &p_manifest.write[ofs + 2]); // headersize
							encode_uint32(tag_size, &p_manifest.write[ofs + 4]); // size
							encode_uint32(0, &p_manifest.write[ofs + 8]); // lineno
							encode_uint32(-1, &p_manifest.write[ofs + 12]); // comment
							encode_uint32(-1, &p_manifest.write[ofs + 16]); // ns
							encode_uint32(attr_uses_feature_string, &p_manifest.write[ofs + 20]); // name
							encode_uint16(20, &p_manifest.write[ofs + 24]); // attr_start
							encode_uint16(20, &p_manifest.write[ofs + 26]); // attr_size
							encode_uint16(attr_count, &p_manifest.write[ofs + 28]); // num_attrs
							encode_uint16(0, &p_manifest.write[ofs + 30]); // id_index
							encode_uint16(0, &p_manifest.write[ofs + 32]); // class_index
							encode_uint16(0, &p_manifest.write[ofs + 34]); // style_index

							// android:name attribute
							encode_uint32(ns_android_string, &p_manifest.write[ofs + 36]); // ns
							encode_uint32(attr_name_string, &p_manifest.write[ofs + 40]); // 'name'
							encode_uint32(feature_string, &p_manifest.write[ofs + 44]); // raw_value
							encode_uint16(8, &p_manifest.write[ofs + 48]); // typedvalue_size
							p_manifest.write[ofs + 50] = 0; // typedvalue_always0
							p_manifest.write[ofs + 51] = 0x03; // typedvalue_type (string)
							encode_uint32(feature_string, &p_manifest.write[ofs + 52]); // typedvalue reference

							// android:required attribute
							encode_uint32(ns_android_string, &p_manifest.write[ofs + 56]); // ns
							encode_uint32(attr_required_string, &p_manifest.write[ofs + 60]); // 'name'
							encode_uint32(required_value, &p_manifest.write[ofs + 64]); // raw_value
							encode_uint16(8, &p_manifest.write[ofs + 68]); // typedvalue_size
							p_manifest.write[ofs + 70] = 0; // typedvalue_always0
							p_manifest.write[ofs + 71] = 0x03; // typedvalue_type (string)
							encode_uint32(required_value, &p_manifest.write[ofs + 72]); // typedvalue reference

							ofs += 76;

							if (has_version_attribute) {
								// android:version attribute
								encode_uint32(ns_android_string, &p_manifest.write[ofs]); // ns
								encode_uint32(attr_version_string, &p_manifest.write[ofs + 4]); // 'name'
								encode_uint32(version_value, &p_manifest.write[ofs + 8]); // raw_value
								encode_uint16(8, &p_manifest.write[ofs + 12]); // typedvalue_size
								p_manifest.write[ofs + 14] = 0; // typedvalue_always0
								p_manifest.write[ofs + 15] = 0x03; // typedvalue_type (string)
								encode_uint32(version_value, &p_manifest.write[ofs + 16]); // typedvalue reference

								ofs += 20;
							}

							// end tag
							encode_uint16(0x103, &p_manifest.write[ofs]); // type
							encode_uint16(16, &p_manifest.write[ofs + 2]); // headersize
							encode_uint32(24, &p_manifest.write[ofs + 4]); // size
							encode_uint32(0, &p_manifest.write[ofs + 8]); // lineno
							encode_uint32(-1, &p_manifest.write[ofs + 12]); // comment
							encode_uint32(-1, &p_manifest.write[ofs + 16]); // ns
							encode_uint32(attr_uses_feature_string, &p_manifest.write[ofs + 20]); // name

							ofs += 24;
						}

						// Updating manifest permissions
						int32_t attr_uses_permission_string = string_table.find("uses-permission");
						if (attr_uses_permission_string == -1) {
							string_table.push_back("uses-permission");
							attr_uses_permission_string = string_table.size() - 1;
						}

						for (int i = 0; i < perms.size(); ++i) {
							print_line("Adding permission " + perms[i]);

							manifest_cur_size += 56 + 24; // node + end node
							p_manifest.resize(manifest_cur_size);

							// Add permission to the string pool
							int32_t perm_string = string_table.find(perms[i]);
							if (perm_string == -1) {
								string_table.push_back(perms[i]);
								perm_string = string_table.size() - 1;
							}

							// start tag
							encode_uint16(0x102, &p_manifest.write[ofs]); // type
							encode_uint16(16, &p_manifest.write[ofs + 2]); // headersize
							encode_uint32(56, &p_manifest.write[ofs + 4]); // size
							encode_uint32(0, &p_manifest.write[ofs + 8]); // lineno
							encode_uint32(-1, &p_manifest.write[ofs + 12]); // comment
							encode_uint32(-1, &p_manifest.write[ofs + 16]); // ns
							encode_uint32(attr_uses_permission_string, &p_manifest.write[ofs + 20]); // name
							encode_uint16(20, &p_manifest.write[ofs + 24]); // attr_start
							encode_uint16(20, &p_manifest.write[ofs + 26]); // attr_size
							encode_uint16(1, &p_manifest.write[ofs + 28]); // num_attrs
							encode_uint16(0, &p_manifest.write[ofs + 30]); // id_index
							encode_uint16(0, &p_manifest.write[ofs + 32]); // class_index
							encode_uint16(0, &p_manifest.write[ofs + 34]); // style_index

							// attribute
							encode_uint32(ns_android_string, &p_manifest.write[ofs + 36]); // ns
							encode_uint32(attr_name_string, &p_manifest.write[ofs + 40]); // 'name'
							encode_uint32(perm_string, &p_manifest.write[ofs + 44]); // raw_value
							encode_uint16(8, &p_manifest.write[ofs + 48]); // typedvalue_size
							p_manifest.write[ofs + 50] = 0; // typedvalue_always0
							p_manifest.write[ofs + 51] = 0x03; // typedvalue_type (string)
							encode_uint32(perm_string, &p_manifest.write[ofs + 52]); // typedvalue reference

							ofs += 56;

							// end tag
							encode_uint16(0x103, &p_manifest.write[ofs]); // type
							encode_uint16(16, &p_manifest.write[ofs + 2]); // headersize
							encode_uint32(24, &p_manifest.write[ofs + 4]); // size
							encode_uint32(0, &p_manifest.write[ofs + 8]); // lineno
							encode_uint32(-1, &p_manifest.write[ofs + 12]); // comment
							encode_uint32(-1, &p_manifest.write[ofs + 16]); // ns
							encode_uint32(attr_uses_permission_string, &p_manifest.write[ofs + 20]); // name

							ofs += 24;
						}
					}

					if (tname == "application") {
						// Updating application meta-data
						int32_t attr_meta_data_string = string_table.find("meta-data");
						if (attr_meta_data_string == -1) {
							string_table.push_back("meta-data");
							attr_meta_data_string = string_table.size() - 1;
						}

						int32_t attr_value_string = string_table.find("value");
						if (attr_value_string == -1) {
							string_table.push_back("value");
							attr_value_string = string_table.size() - 1;
						}

						for (int i = 0; i < manifest_metadata.size(); i++) {
							String meta_data_name = manifest_metadata[i].name;
							String meta_data_value = manifest_metadata[i].value;

							print_line("Adding application metadata " + meta_data_name);

							int32_t meta_data_name_string = string_table.find(meta_data_name);
							if (meta_data_name_string == -1) {
								string_table.push_back(meta_data_name);
								meta_data_name_string = string_table.size() - 1;
							}

							int32_t meta_data_value_string = string_table.find(meta_data_value);
							if (meta_data_value_string == -1) {
								string_table.push_back(meta_data_value);
								meta_data_value_string = string_table.size() - 1;
							}

							int tag_size = 76; // node and two attrs + end node
							int attr_count = 2;
							manifest_cur_size += tag_size + 24;
							p_manifest.resize(manifest_cur_size);

							// start tag
							encode_uint16(0x102, &p_manifest.write[ofs]); // type
							encode_uint16(16, &p_manifest.write[ofs + 2]); // headersize
							encode_uint32(tag_size, &p_manifest.write[ofs + 4]); // size
							encode_uint32(0, &p_manifest.write[ofs + 8]); // lineno
							encode_uint32(-1, &p_manifest.write[ofs + 12]); // comment
							encode_uint32(-1, &p_manifest.write[ofs + 16]); // ns
							encode_uint32(attr_meta_data_string, &p_manifest.write[ofs + 20]); // name
							encode_uint16(20, &p_manifest.write[ofs + 24]); // attr_start
							encode_uint16(20, &p_manifest.write[ofs + 26]); // attr_size
							encode_uint16(attr_count, &p_manifest.write[ofs + 28]); // num_attrs
							encode_uint16(0, &p_manifest.write[ofs + 30]); // id_index
							encode_uint16(0, &p_manifest.write[ofs + 32]); // class_index
							encode_uint16(0, &p_manifest.write[ofs + 34]); // style_index

							// android:name attribute
							encode_uint32(ns_android_string, &p_manifest.write[ofs + 36]); // ns
							encode_uint32(attr_name_string, &p_manifest.write[ofs + 40]); // 'name'
							encode_uint32(meta_data_name_string, &p_manifest.write[ofs + 44]); // raw_value
							encode_uint16(8, &p_manifest.write[ofs + 48]); // typedvalue_size
							p_manifest.write[ofs + 50] = 0; // typedvalue_always0
							p_manifest.write[ofs + 51] = 0x03; // typedvalue_type (string)
							encode_uint32(meta_data_name_string, &p_manifest.write[ofs + 52]); // typedvalue reference

							// android:value attribute
							encode_uint32(ns_android_string, &p_manifest.write[ofs + 56]); // ns
							encode_uint32(attr_value_string, &p_manifest.write[ofs + 60]); // 'value'
							encode_uint32(meta_data_value_string, &p_manifest.write[ofs + 64]); // raw_value
							encode_uint16(8, &p_manifest.write[ofs + 68]); // typedvalue_size
							p_manifest.write[ofs + 70] = 0; // typedvalue_always0
							p_manifest.write[ofs + 71] = 0x03; // typedvalue_type (string)
							encode_uint32(meta_data_value_string, &p_manifest.write[ofs + 72]); // typedvalue reference

							ofs += 76;

							// end tag
							encode_uint16(0x103, &p_manifest.write[ofs]); // type
							encode_uint16(16, &p_manifest.write[ofs + 2]); // headersize
							encode_uint32(24, &p_manifest.write[ofs + 4]); // size
							encode_uint32(0, &p_manifest.write[ofs + 8]); // lineno
							encode_uint32(-1, &p_manifest.write[ofs + 12]); // comment
							encode_uint32(-1, &p_manifest.write[ofs + 16]); // ns
							encode_uint32(attr_meta_data_string, &p_manifest.write[ofs + 20]); // name

							ofs += 24;
						}
					}

					// copy footer back in
					memcpy(&p_manifest.write[ofs], manifest_end.ptr(), manifest_end.size());
				}
			} break;
		}

		ofs += size;
	}

	// Create new android manifest binary.

	Vector<uint8_t> ret;
	ret.resize(string_table_begins + string_table.size() * 4);

	for (uint32_t i = 0; i < string_table_begins; i++) {
		ret.write[i] = p_manifest[i];
	}

	ofs = 0;
	for (int i = 0; i < string_table.size(); i++) {
		encode_uint32(ofs, &ret.write[string_table_begins + i * 4]);
		ofs += string_table[i].length() * 2 + 2 + 2;
	}

	ret.resize(ret.size() + ofs);
	string_data_offset = ret.size() - ofs;
	uint8_t *chars = &ret.write[string_data_offset];
	for (int i = 0; i < string_table.size(); i++) {
		String s = string_table[i];
		encode_uint16(s.length(), chars);
		chars += 2;
		for (int j = 0; j < s.length(); j++) {
			encode_uint16(s[j], chars);
			chars += 2;
		}
		encode_uint16(0, chars);
		chars += 2;
	}

	for (int i = 0; i < stable_extra.size(); i++) {
		ret.push_back(stable_extra[i]);
	}

	//pad
	while (ret.size() % 4) {
		ret.push_back(0);
	}

	uint32_t new_stable_end = ret.size();

	uint32_t extra = (p_manifest.size() - string_table_ends);
	ret.resize(new_stable_end + extra);
	for (uint32_t i = 0; i < extra; i++) {
		ret.write[new_stable_end + i] = p_manifest[string_table_ends + i];
	}

	while (ret.size() % 4) {
		ret.push_back(0);
	}
	encode_uint32(ret.size(), &ret.write[4]); //update new file size

	encode_uint32(new_stable_end - 8, &ret.write[12]); //update new string table size
	encode_uint32(string_table.size(), &ret.write[16]); //update new number of strings
	encode_uint32(string_data_offset - 8, &ret.write[28]); //update new string data offset

	p_manifest = ret;
}

String EditorExportPlatformAndroid::_get_keystore_path(const Ref<EditorExportPreset> &p_preset, bool p_debug) {
	String keystore_preference = p_debug ? "keystore/debug" : "keystore/release";
	String keystore_env_variable = p_debug ? ENV_ANDROID_KEYSTORE_DEBUG_PATH : ENV_ANDROID_KEYSTORE_RELEASE_PATH;
	String keystore_path = p_preset->get_or_env(keystore_preference, keystore_env_variable);

	return ProjectSettings::get_singleton()->globalize_path(keystore_path).simplify_path();
}

String EditorExportPlatformAndroid::_parse_string(const uint8_t *p_bytes, bool p_utf8) {
	uint32_t offset = 0;
	uint32_t len = 0;

	if (p_utf8) {
		uint8_t byte = p_bytes[offset];
		if (byte & 0x80) {
			offset += 2;
		} else {
			offset += 1;
		}
		byte = p_bytes[offset];
		offset++;
		if (byte & 0x80) {
			len = byte & 0x7F;
			len = (len << 8) + p_bytes[offset];
			offset++;
		} else {
			len = byte;
		}
	} else {
		len = decode_uint16(&p_bytes[offset]);
		offset += 2;
		if (len & 0x8000) {
			len &= 0x7FFF;
			len = (len << 16) + decode_uint16(&p_bytes[offset]);
			offset += 2;
		}
	}

	if (p_utf8) {
		Vector<uint8_t> str8;
		str8.resize(len + 1);
		for (uint32_t i = 0; i < len; i++) {
			str8.write[i] = p_bytes[offset + i];
		}
		str8.write[len] = 0;
		return String::utf8((const char *)str8.ptr(), len);
	} else {
		String str;
		for (uint32_t i = 0; i < len; i++) {
			char32_t c = decode_uint16(&p_bytes[offset + i * 2]);
			if (c == 0) {
				break;
			}
			str += String::chr(c);
		}
		return str;
	}
}

void EditorExportPlatformAndroid::_fix_resources(const Ref<EditorExportPreset> &p_preset, Vector<uint8_t> &r_manifest) {
	const int UTF8_FLAG = 0x00000100;

	uint32_t string_block_len = decode_uint32(&r_manifest[16]);
	uint32_t string_count = decode_uint32(&r_manifest[20]);
	uint32_t string_flags = decode_uint32(&r_manifest[28]);
	const uint32_t string_table_begins = 40;

	Vector<String> string_table;

	const String project_name = get_project_name(p_preset, p_preset->get("package/name"));
	const Dictionary appnames = get_project_setting(p_preset, "application/config/name_localized");
	const StringName domain_name = "godot.project_name_localization";
	Ref<TranslationDomain> domain = TranslationServer::get_singleton()->get_or_add_domain(domain_name);
	TranslationServer::get_singleton()->load_project_translations(domain);

	for (uint32_t i = 0; i < string_count; i++) {
		uint32_t offset = decode_uint32(&r_manifest[string_table_begins + i * 4]);
		offset += string_table_begins + string_count * 4;

		String str = _parse_string(&r_manifest[offset], string_flags & UTF8_FLAG);

		if (str == "foundry-project-name") {
			str = project_name;
		} else if (str.begins_with("foundry-project-name")) {
			String lang = str.substr(str.rfind_char('-') + 1).replace_char('-', '_');

			if (appnames.is_empty()) {
				domain->set_locale_override(lang);
				str = domain->translate(project_name, String());
			} else {
				str = appnames.get(lang, project_name);
			}
		}

		string_table.push_back(str);
	}

	TranslationServer::get_singleton()->remove_domain(domain_name);

	//write a new string table, but use 16 bits
	Vector<uint8_t> ret;
	ret.resize(string_table_begins + string_table.size() * 4);

	for (uint32_t i = 0; i < string_table_begins; i++) {
		ret.write[i] = r_manifest[i];
	}

	int ofs = 0;
	for (int i = 0; i < string_table.size(); i++) {
		encode_uint32(ofs, &ret.write[string_table_begins + i * 4]);
		ofs += string_table[i].length() * 2 + 2 + 2;
	}

	ret.resize(ret.size() + ofs);
	uint8_t *chars = &ret.write[ret.size() - ofs];
	for (int i = 0; i < string_table.size(); i++) {
		String s = string_table[i];
		encode_uint16(s.length(), chars);
		chars += 2;
		for (int j = 0; j < s.length(); j++) {
			encode_uint16(s[j], chars);
			chars += 2;
		}
		encode_uint16(0, chars);
		chars += 2;
	}

	//pad
	while (ret.size() % 4) {
		ret.push_back(0);
	}

	//change flags to not use utf8
	encode_uint32(string_flags & ~0x100, &ret.write[28]);
	//change length
	encode_uint32(ret.size() - 12, &ret.write[16]);
	//append the rest...
	int rest_from = 12 + string_block_len;
	int rest_to = ret.size();
	int rest_len = (r_manifest.size() - rest_from);
	ret.resize(ret.size() + (r_manifest.size() - rest_from));
	for (int i = 0; i < rest_len; i++) {
		ret.write[rest_to + i] = r_manifest[rest_from + i];
	}
	//finally update the size
	encode_uint32(ret.size(), &ret.write[4]);

	r_manifest = ret;
	//printf("end\n");
}

void EditorExportPlatformAndroid::_process_launcher_icons(const String &p_file_name, const Ref<Image> &p_source_image, int dimension, Vector<uint8_t> &p_data) {
	Ref<Image> working_image = p_source_image;

	if (p_source_image->get_width() != dimension || p_source_image->get_height() != dimension) {
		working_image = p_source_image->duplicate();
		working_image->resize(dimension, dimension, Image::Interpolation::INTERPOLATE_LANCZOS);
	}

	Vector<uint8_t> buffer = working_image->save_webp_to_buffer();
	p_data.resize(buffer.size());
	memcpy(p_data.ptrw(), buffer.ptr(), p_data.size());
}

void EditorExportPlatformAndroid::load_icon_refs(const Ref<EditorExportPreset> &p_preset, Ref<Image> &icon, Ref<Image> &foreground, Ref<Image> &background, Ref<Image> &monochrome) {
	String project_icon_path = get_project_setting(p_preset, "application/config/icon");

	Error err = OK;

	// Regular icon: user selection -> project icon -> default.
	String path = static_cast<String>(p_preset->get(LAUNCHER_ICON_OPTION)).strip_edges();
	print_verbose("Loading regular icon from " + path);
	if (!path.is_empty()) {
		icon = _load_icon_or_splash_image(path, &err);
	}
	if (path.is_empty() || err != OK || icon.is_null() || icon->is_empty()) {
		print_verbose("- falling back to project icon: " + project_icon_path);
		if (!project_icon_path.is_empty()) {
			icon = _load_icon_or_splash_image(project_icon_path, &err);
		} else {
			ERR_PRINT("No project icon specified. Please specify one in the Project Settings under Application -> Config -> Icon");
		}
	}

	// Adaptive foreground: user selection -> regular icon (user selection -> project icon -> default).
	path = static_cast<String>(p_preset->get(LAUNCHER_ADAPTIVE_ICON_FOREGROUND_OPTION)).strip_edges();
	print_verbose("Loading adaptive foreground icon from " + path);
	if (!path.is_empty()) {
		foreground = _load_icon_or_splash_image(path, &err);
	}
	if (path.is_empty() || err != OK || foreground.is_null() || foreground->is_empty()) {
		print_verbose("- falling back to using the regular icon");
		foreground = icon;
	}

	// Adaptive background: user selection -> default.
	path = static_cast<String>(p_preset->get(LAUNCHER_ADAPTIVE_ICON_BACKGROUND_OPTION)).strip_edges();
	if (!path.is_empty()) {
		print_verbose("Loading adaptive background icon from " + path);
		background = _load_icon_or_splash_image(path, &err);
	}

	// Adaptive monochrome: user selection -> default.
	path = static_cast<String>(p_preset->get(LAUNCHER_ADAPTIVE_ICON_MONOCHROME_OPTION)).strip_edges();
	if (!path.is_empty()) {
		print_verbose("Loading adaptive monochrome icon from " + path);
		monochrome = _load_icon_or_splash_image(path, &err);
	}
}

void EditorExportPlatformAndroid::_copy_icons_to_gradle_project(const Ref<EditorExportPreset> &p_preset,
		const Ref<Image> &p_main_image,
		const Ref<Image> &p_foreground,
		const Ref<Image> &p_background,
		const Ref<Image> &p_monochrome) {
	String gradle_build_dir = ExportTemplateManager::get_android_build_directory(p_preset);

	String monochrome_tag = "";

	// Prepare images to be resized for the icons. If some image ends up being uninitialized,
	// the default image from the export template will be used.

	for (int i = 0; i < ICON_DENSITIES_COUNT; ++i) {
		if (p_main_image.is_valid() && !p_main_image->is_empty()) {
			print_verbose("Processing launcher icon for dimension " + itos(LAUNCHER_ICONS[i].dimensions) + " into " + LAUNCHER_ICONS[i].export_path);
			Vector<uint8_t> data;
			_process_launcher_icons(LAUNCHER_ICONS[i].export_path, p_main_image, LAUNCHER_ICONS[i].dimensions, data);
			store_file_at_path(gradle_build_dir.path_join(LAUNCHER_ICONS[i].export_path), data);
		}

		if (p_foreground.is_valid() && !p_foreground->is_empty()) {
			print_verbose("Processing launcher adaptive icon p_foreground for dimension " + itos(LAUNCHER_ADAPTIVE_ICON_FOREGROUNDS[i].dimensions) + " into " + LAUNCHER_ADAPTIVE_ICON_FOREGROUNDS[i].export_path);
			Vector<uint8_t> data;
			_process_launcher_icons(LAUNCHER_ADAPTIVE_ICON_FOREGROUNDS[i].export_path, p_foreground,
					LAUNCHER_ADAPTIVE_ICON_FOREGROUNDS[i].dimensions, data);
			store_file_at_path(gradle_build_dir.path_join(LAUNCHER_ADAPTIVE_ICON_FOREGROUNDS[i].export_path), data);
		}

		if (p_background.is_valid() && !p_background->is_empty()) {
			print_verbose("Processing launcher adaptive icon p_background for dimension " + itos(LAUNCHER_ADAPTIVE_ICON_BACKGROUNDS[i].dimensions) + " into " + LAUNCHER_ADAPTIVE_ICON_BACKGROUNDS[i].export_path);
			Vector<uint8_t> data;
			_process_launcher_icons(LAUNCHER_ADAPTIVE_ICON_BACKGROUNDS[i].export_path, p_background,
					LAUNCHER_ADAPTIVE_ICON_BACKGROUNDS[i].dimensions, data);
			store_file_at_path(gradle_build_dir.path_join(LAUNCHER_ADAPTIVE_ICON_BACKGROUNDS[i].export_path), data);
		}

		if (p_monochrome.is_valid() && !p_monochrome->is_empty()) {
			print_verbose("Processing launcher adaptive icon p_monochrome for dimension " + itos(LAUNCHER_ADAPTIVE_ICON_MONOCHROMES[i].dimensions) + " into " + LAUNCHER_ADAPTIVE_ICON_MONOCHROMES[i].export_path);
			Vector<uint8_t> data;
			_process_launcher_icons(LAUNCHER_ADAPTIVE_ICON_MONOCHROMES[i].export_path, p_monochrome,
					LAUNCHER_ADAPTIVE_ICON_MONOCHROMES[i].dimensions, data);
			store_file_at_path(gradle_build_dir.path_join(LAUNCHER_ADAPTIVE_ICON_MONOCHROMES[i].export_path), data);
			monochrome_tag = "    <monochrome android:drawable=\"@mipmap/icon_monochrome\"/>\n";
		}
	}

	// Finalize the icon.xml by formatting the template with the optional monochrome tag.
	store_string_at_path(gradle_build_dir.path_join(ICON_XML_PATH), vformat(ICON_XML_TEMPLATE, monochrome_tag));
}

Vector<EditorExportPlatformAndroid::ABI> EditorExportPlatformAndroid::get_enabled_abis(const Ref<EditorExportPreset> &p_preset) {
	Vector<ABI> abis = get_abis();
	Vector<ABI> enabled_abis;
	for (int i = 0; i < abis.size(); ++i) {
		bool is_enabled = p_preset->get("architectures/" + abis[i].abi);
		if (is_enabled) {
			enabled_abis.push_back(abis[i]);
		}
	}
	return enabled_abis;
}

static bool _is_exact_foundry_java_coordinate(const String &p_coordinate) {
	PackedStringArray parts = p_coordinate.split(":");
	if (parts.size() != 3 || p_coordinate.contains("+") || p_coordinate.contains("[") || p_coordinate.contains("]") || parts[2].to_lower().contains("latest")) {
		return false;
	}
	for (const String &part : parts) {
		if (part.is_empty()) {
			return false;
		}
		for (int i = 0; i < part.length(); i++) {
			const char32_t character = part[i];
			if (!(is_ascii_alphanumeric_char(character) || character == '.' || character == '_' || character == '-')) {
				return false;
			}
		}
	}
	return true;
}

#ifdef MACOS_ENABLED
static bool _foundry_java_normalize_macos_system_root_alias(const Ref<DirAccess> &p_dir, String &r_path) {
	String expected_target;
	if (r_path == "/etc") {
		expected_target = "private/etc";
	} else if (r_path == "/tmp") {
		expected_target = "private/tmp";
	} else if (r_path == "/var") {
		expected_target = "private/var";
	}
	if (expected_target.is_empty() || p_dir->read_link(r_path) != expected_target) {
		return false;
	}
	r_path = "/" + expected_target;
	return true;
}
#endif

static bool _foundry_java_path_has_symlink(const String &p_path) {
	Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	ERR_FAIL_COND_V(dir.is_null(), true);

	const String with_normalized_separators = p_path.replace("\\", "/");
	PackedStringArray components = with_normalized_separators.split("/", false);
	String current;
	if (with_normalized_separators.is_network_share_path()) {
		current = "//";
	} else if (with_normalized_separators.begins_with("/")) {
		current = "/";
	}
	for (const String &component : components) {
		if (component == ".") {
			continue;
		}
		if (component == "..") {
			current = current.get_base_dir();
			continue;
		}
		current = current.is_empty() ? component : current.path_join(component);
		if (dir->is_link(current)) {
#ifdef MACOS_ENABLED
			// macOS exposes these fixed root-owned aliases as symlinks into
			// /private. Continue checking every component below the exact target.
			if (_foundry_java_normalize_macos_system_root_alias(dir, current)) {
				continue;
			}
#endif
			return true;
		}
	}
	return false;
}

static constexpr uint64_t FOUNDRY_JAVA_MAX_INPUT_ARCHIVE_ENTRIES = 65534;
static constexpr uint64_t FOUNDRY_JAVA_MAX_INPUT_ARCHIVE_DEPTH = 8;
static constexpr uint64_t FOUNDRY_JAVA_MAX_INPUT_ARCHIVES = 64;
static constexpr uint64_t FOUNDRY_JAVA_MAX_INPUT_ARCHIVE_ENTRY_UNCOMPRESSED_BYTES = 128 * 1024 * 1024;
static constexpr uint64_t FOUNDRY_JAVA_MAX_INPUT_ARCHIVE_TOTAL_UNCOMPRESSED_BYTES = 512 * 1024 * 1024;
static constexpr uint64_t FOUNDRY_JAVA_INPUT_ARCHIVE_READ_CHUNK_BYTES = 1024 * 1024;
static constexpr uint64_t FOUNDRY_JAVA_MAX_FINAL_ARTIFACT_ENTRIES = 131072;
static constexpr uint64_t FOUNDRY_JAVA_MAX_FINAL_ARTIFACT_REQUIRED_ENTRY_UNCOMPRESSED_BYTES = 128 * 1024 * 1024;
static constexpr uint64_t FOUNDRY_JAVA_MAX_FINAL_ARTIFACT_REQUIRED_ENTRIES_UNCOMPRESSED_BYTES = 512 * 1024 * 1024;
static constexpr uint64_t FOUNDRY_JAVA_MAX_ARCHIVE_ENTRY_NAME_BYTES = 16383;
static constexpr uint64_t FOUNDRY_JAVA_MAX_ARCHIVE_ENTRY_NAMES_BYTES = 16 * 1024 * 1024;
static constexpr uint64_t FOUNDRY_JAVA_LOCAL_FILE_ENTRY_SIZE = 30;
static constexpr uint64_t FOUNDRY_JAVA_CENTRAL_DIRECTORY_ENTRY_SIZE = 46;
static constexpr uint64_t FOUNDRY_JAVA_CLASSIC_END_OF_CENTRAL_DIRECTORY_SIZE = 22;
static constexpr uint64_t FOUNDRY_JAVA_MAX_ZIP_COMMENT_BYTES = 65535;
static constexpr uint32_t FOUNDRY_JAVA_LOCAL_FILE_ENTRY_SIGNATURE = 0x04034b50;
static constexpr uint32_t FOUNDRY_JAVA_CENTRAL_DIRECTORY_ENTRY_SIGNATURE = 0x02014b50;
static constexpr uint32_t FOUNDRY_JAVA_CENTRAL_DIRECTORY_DIGITAL_SIGNATURE = 0x05054b50;
static constexpr uint32_t FOUNDRY_JAVA_CLASSIC_END_OF_CENTRAL_DIRECTORY_SIGNATURE = 0x06054b50;
static constexpr uint32_t FOUNDRY_JAVA_ZIP64_END_OF_CENTRAL_DIRECTORY_SIGNATURE = 0x06064b50;
static constexpr uint32_t FOUNDRY_JAVA_ZIP64_END_OF_CENTRAL_DIRECTORY_LOCATOR_SIGNATURE = 0x07064b50;

enum FoundryJavaCentralDirectoryConsistency {
	FOUNDRY_JAVA_CENTRAL_DIRECTORY_CLEAN,
	FOUNDRY_JAVA_CENTRAL_DIRECTORY_UNREPORTED_ENTRY,
	FOUNDRY_JAVA_CENTRAL_DIRECTORY_CORRUPT,
};

enum FoundryJavaZip64ExtentResult {
	FOUNDRY_JAVA_ZIP64_EXTENT_NOT_PRESENT,
	FOUNDRY_JAVA_ZIP64_EXTENT_VALID,
	FOUNDRY_JAVA_ZIP64_EXTENT_CORRUPT,
};

static String _foundry_java_safe_diagnostic_value(const String &p_value) {
	constexpr int MAX_DIAGNOSTIC_VALUE_LENGTH = 512;
	const String escaped = p_value.c_escape();
	if (escaped.length() <= MAX_DIAGNOSTIC_VALUE_LENGTH) {
		return escaped;
	}
	return escaped.left(MAX_DIAGNOSTIC_VALUE_LENGTH) + vformat("... <%d characters>", escaped.length());
}

static Error _foundry_java_invalid_option(const String &p_option, const String &p_value, const String &p_reason, String &r_error) {
	r_error = vformat(TTR("Invalid export option %s value '%s': %s."), p_option, _foundry_java_safe_diagnostic_value(p_value), p_reason);
	return ERR_INVALID_PARAMETER;
}

static bool _foundry_java_contains_property_separator(const String &p_value) {
	return p_value.contains("|") || p_value.contains("\n") || p_value.contains("\r");
}

static bool _foundry_java_has_safe_uri_syntax(const String &p_value, bool p_allow_square_brackets) {
	for (int i = 0; i < p_value.length(); i++) {
		const char32_t character = p_value[i];
		if (character <= 0x20 || character >= 0x7f ||
				character == '\\' || character == '"' || character == '<' || character == '>' ||
				character == '{' || character == '}' || character == '^' || character == '`' ||
				character == '|' || (!p_allow_square_brackets && (character == '[' || character == ']'))) {
			return false;
		}
		if (character == '%') {
			if (i + 2 >= p_value.length() ||
					!is_hex_digit(p_value[i + 1]) ||
					!is_hex_digit(p_value[i + 2])) {
				return false;
			}
			i += 2;
		}
	}
	return true;
}

static bool _foundry_java_has_safe_repository_url(const String &p_value) {
	if (p_value.contains("?") || p_value.contains("#")) {
		return false;
	}
	if (p_value.begins_with("file:///")) {
		const String path = p_value.substr(8);
		return !path.begins_with("/") && _foundry_java_has_safe_uri_syntax(path, false);
	}
	if (!p_value.begins_with("https://") || !_foundry_java_has_safe_uri_syntax(p_value, true)) {
		return false;
	}
	const String remainder = p_value.substr(8);
	const int path_separator = remainder.find("/");
	const String authority = path_separator == -1 ? remainder : remainder.left(path_separator);
	const String raw_path = path_separator == -1 ? String() : remainder.substr(path_separator);
	if (authority.is_empty() || authority.contains("@") || !_foundry_java_has_safe_uri_syntax(raw_path, false)) {
		return false;
	}
	if (authority.begins_with("[")) {
		const int closing_bracket = authority.find("]");
		if (closing_bracket == -1 ||
				(closing_bracket + 1 < authority.length() && authority[closing_bracket + 1] != ':')) {
			return false;
		}
	} else if (authority.contains("[") || authority.contains("]")) {
		return false;
	}
	String scheme;
	String host;
	String path;
	String fragment;
	int port = 0;
	if (p_value.parse_url(scheme, host, port, path, fragment) != OK || scheme != "https://" || !fragment.is_empty()) {
		return false;
	}
	const bool host_is_ip_address = host.is_valid_ip_address();
	if (authority.begins_with("[") && (!host.contains(":") || !host_is_ip_address)) {
		return false;
	}
	if (host_is_ip_address) {
		return true;
	}
	String hostname = host;
	if (hostname.ends_with(".")) {
		hostname = hostname.left(-1);
	}
	if (hostname.is_empty()) {
		return false;
	}
	const PackedStringArray labels = hostname.split(".");
	for (const String &label : labels) {
		if (label.is_empty() || label.length() > 63 ||
				!is_ascii_alphanumeric_char(label[0]) ||
				!is_ascii_alphanumeric_char(label[label.length() - 1])) {
			return false;
		}
		for (int i = 1; i < label.length() - 1; i++) {
			if (!is_ascii_alphanumeric_char(label[i]) && label[i] != '-') {
				return false;
			}
		}
	}
	return true;
}

static uint16_t _foundry_java_decode_uint16(const uint8_t *p_bytes) {
	return uint16_t(p_bytes[0]) | (uint16_t(p_bytes[1]) << 8);
}

static uint32_t _foundry_java_decode_uint32(const uint8_t *p_bytes) {
	return uint32_t(p_bytes[0]) | (uint32_t(p_bytes[1]) << 8) | (uint32_t(p_bytes[2]) << 16) | (uint32_t(p_bytes[3]) << 24);
}

static uint64_t _foundry_java_decode_uint64(const uint8_t *p_bytes) {
	return uint64_t(_foundry_java_decode_uint32(p_bytes)) | (uint64_t(_foundry_java_decode_uint32(p_bytes + 4)) << 32);
}

static bool _foundry_java_checked_add(uint64_t p_left, uint64_t p_right, uint64_t &r_sum) {
	if (p_right > UINT64_MAX - p_left) {
		return false;
	}
	r_sum = p_left + p_right;
	return true;
}

static bool _foundry_java_read_archive_bytes(const Ref<FileAccess> &p_archive_file, uint64_t p_position, uint8_t *r_bytes, uint64_t p_size) {
	if (p_archive_file.is_null()) {
		return false;
	}
	const uint64_t archive_length = p_archive_file->get_length();
	if (p_position > archive_length || p_size > archive_length - p_position) {
		return false;
	}
	p_archive_file->seek(p_position);
	return p_archive_file->get_buffer(r_bytes, p_size) == p_size;
}

static bool _foundry_java_read_archive_uint32(const Ref<FileAccess> &p_archive_file, uint64_t p_position, uint32_t &r_value) {
	uint8_t bytes[4];
	if (!_foundry_java_read_archive_bytes(p_archive_file, p_position, bytes, sizeof(bytes))) {
		return false;
	}
	r_value = _foundry_java_decode_uint32(bytes);
	return true;
}

static FoundryJavaZip64ExtentResult _foundry_java_find_zip64_central_directory_extent(
		const Ref<FileAccess> &p_archive_file,
		const unz_global_info64 &p_global_info,
		uint64_t p_end_record_position,
		uint16_t p_classic_disk_entry_count,
		uint16_t p_classic_total_entry_count,
		uint32_t p_classic_central_directory_size,
		uint32_t p_classic_central_directory_offset,
		uint64_t &r_byte_before_zip,
		uint64_t &r_central_directory_start,
		uint64_t &r_central_directory_end) {
	if (p_end_record_position < 20) {
		return FOUNDRY_JAVA_ZIP64_EXTENT_NOT_PRESENT;
	}
	uint8_t locator[20];
	if (!_foundry_java_read_archive_bytes(
				p_archive_file,
				p_end_record_position - sizeof(locator),
				locator,
				sizeof(locator)) ||
			_foundry_java_decode_uint32(locator) != FOUNDRY_JAVA_ZIP64_END_OF_CENTRAL_DIRECTORY_LOCATOR_SIGNATURE) {
		return FOUNDRY_JAVA_ZIP64_EXTENT_NOT_PRESENT;
	}

	// Bundled minizip treats the locator's ZIP64 record offset as a physical
	// archive offset, including any prepended SFX bytes.
	const uint64_t zip64_end_record_position = _foundry_java_decode_uint64(locator + 8);
	if (zip64_end_record_position >= p_end_record_position - sizeof(locator)) {
		return FOUNDRY_JAVA_ZIP64_EXTENT_NOT_PRESENT;
	}
	uint8_t zip64_end_record[56];
	if (!_foundry_java_read_archive_bytes(
				p_archive_file,
				zip64_end_record_position,
				zip64_end_record,
				sizeof(zip64_end_record)) ||
			_foundry_java_decode_uint32(zip64_end_record) != FOUNDRY_JAVA_ZIP64_END_OF_CENTRAL_DIRECTORY_SIGNATURE) {
		return FOUNDRY_JAVA_ZIP64_EXTENT_NOT_PRESENT;
	}
	const uint64_t zip64_end_record_data_size = _foundry_java_decode_uint64(zip64_end_record + 4);
	uint64_t zip64_end_record_end = 0;
	if (zip64_end_record_data_size < 44 ||
			!_foundry_java_checked_add(zip64_end_record_position, 12, zip64_end_record_end) ||
			!_foundry_java_checked_add(zip64_end_record_end, zip64_end_record_data_size, zip64_end_record_end) ||
			zip64_end_record_end != p_end_record_position - sizeof(locator)) {
		return FOUNDRY_JAVA_ZIP64_EXTENT_NOT_PRESENT;
	}

	const uint64_t zip64_disk_entry_count = _foundry_java_decode_uint64(zip64_end_record + 24);
	const uint64_t zip64_total_entry_count = _foundry_java_decode_uint64(zip64_end_record + 32);
	const uint64_t zip64_central_directory_size = _foundry_java_decode_uint64(zip64_end_record + 40);
	const uint64_t zip64_central_directory_offset = _foundry_java_decode_uint64(zip64_end_record + 48);
	if (_foundry_java_decode_uint32(locator + 4) != 0 ||
			_foundry_java_decode_uint32(locator + 16) != 1 ||
			_foundry_java_decode_uint32(zip64_end_record + 16) != 0 ||
			_foundry_java_decode_uint32(zip64_end_record + 20) != 0 ||
			zip64_disk_entry_count != zip64_total_entry_count ||
			zip64_total_entry_count != p_global_info.number_entry ||
			(p_classic_disk_entry_count != UINT16_MAX && p_classic_disk_entry_count != zip64_disk_entry_count) ||
			(p_classic_total_entry_count != UINT16_MAX && p_classic_total_entry_count != zip64_total_entry_count) ||
			(p_classic_central_directory_size != UINT32_MAX && p_classic_central_directory_size != zip64_central_directory_size) ||
			(p_classic_central_directory_offset != UINT32_MAX && p_classic_central_directory_offset != zip64_central_directory_offset)) {
		return FOUNDRY_JAVA_ZIP64_EXTENT_CORRUPT;
	}

	uint64_t central_directory_start = 0;
	uint64_t central_directory_end = 0;
	if (zip64_central_directory_offset > zip64_end_record_position ||
			zip64_central_directory_size > zip64_end_record_position - zip64_central_directory_offset) {
		return FOUNDRY_JAVA_ZIP64_EXTENT_CORRUPT;
	}
	const uint64_t byte_before_zip = zip64_end_record_position - zip64_central_directory_offset - zip64_central_directory_size;
	if (!_foundry_java_checked_add(byte_before_zip, zip64_central_directory_offset, central_directory_start) ||
			!_foundry_java_checked_add(central_directory_start, zip64_central_directory_size, central_directory_end) ||
			central_directory_end != zip64_end_record_position ||
			central_directory_start >= central_directory_end) {
		return FOUNDRY_JAVA_ZIP64_EXTENT_CORRUPT;
	}
	uint32_t central_directory_signature = 0;
	if (!_foundry_java_read_archive_uint32(p_archive_file, central_directory_start, central_directory_signature) ||
			central_directory_signature != FOUNDRY_JAVA_CENTRAL_DIRECTORY_ENTRY_SIGNATURE) {
		return FOUNDRY_JAVA_ZIP64_EXTENT_CORRUPT;
	}
	r_byte_before_zip = byte_before_zip;
	r_central_directory_start = central_directory_start;
	r_central_directory_end = central_directory_end;
	return FOUNDRY_JAVA_ZIP64_EXTENT_VALID;
}

static bool _foundry_java_find_central_directory_extent(
		const Ref<FileAccess> &p_archive_file,
		const unz_global_info64 &p_global_info,
		uint64_t &r_byte_before_zip,
		uint64_t &r_central_directory_start,
		uint64_t &r_central_directory_end) {
	const uint64_t archive_length = p_archive_file->get_length();
	if (archive_length < FOUNDRY_JAVA_CLASSIC_END_OF_CENTRAL_DIRECTORY_SIZE) {
		return false;
	}
	const uint64_t tail_size = MIN(
			archive_length,
			FOUNDRY_JAVA_CLASSIC_END_OF_CENTRAL_DIRECTORY_SIZE + FOUNDRY_JAVA_MAX_ZIP_COMMENT_BYTES);
	Vector<uint8_t> tail;
	tail.resize(tail_size);
	if (!_foundry_java_read_archive_bytes(p_archive_file, archive_length - tail_size, tail.ptrw(), tail_size)) {
		return false;
	}

	const uint8_t *tail_bytes = tail.ptr();
	for (int64_t index = tail_size - FOUNDRY_JAVA_CLASSIC_END_OF_CENTRAL_DIRECTORY_SIZE; index >= 0; index--) {
		if (_foundry_java_decode_uint32(tail_bytes + index) != FOUNDRY_JAVA_CLASSIC_END_OF_CENTRAL_DIRECTORY_SIGNATURE) {
			continue;
		}
		const uint16_t comment_size = _foundry_java_decode_uint16(tail_bytes + index + 20);
		if (uint64_t(index) + FOUNDRY_JAVA_CLASSIC_END_OF_CENTRAL_DIRECTORY_SIZE + comment_size != tail_size) {
			continue;
		}
		const uint16_t disk_number = _foundry_java_decode_uint16(tail_bytes + index + 4);
		const uint16_t central_directory_disk = _foundry_java_decode_uint16(tail_bytes + index + 6);
		const uint16_t disk_entry_count = _foundry_java_decode_uint16(tail_bytes + index + 8);
		const uint16_t total_entry_count = _foundry_java_decode_uint16(tail_bytes + index + 10);
		const uint32_t central_directory_size = _foundry_java_decode_uint32(tail_bytes + index + 12);
		const uint32_t central_directory_offset = _foundry_java_decode_uint32(tail_bytes + index + 16);
		if (disk_number != 0 || central_directory_disk != 0) {
			continue;
		}

		const uint64_t end_record_position = archive_length - tail_size + index;
		const FoundryJavaZip64ExtentResult zip64_result = _foundry_java_find_zip64_central_directory_extent(
				p_archive_file,
				p_global_info,
				end_record_position,
				disk_entry_count,
				total_entry_count,
				central_directory_size,
				central_directory_offset,
				r_byte_before_zip,
				r_central_directory_start,
				r_central_directory_end);
		if (zip64_result == FOUNDRY_JAVA_ZIP64_EXTENT_VALID) {
			return true;
		}
		if (zip64_result == FOUNDRY_JAVA_ZIP64_EXTENT_CORRUPT) {
			continue;
		}

		if (disk_entry_count != total_entry_count ||
				total_entry_count != p_global_info.number_entry ||
				central_directory_size == UINT32_MAX ||
				central_directory_offset == UINT32_MAX) {
			continue;
		}
		uint64_t central_directory_start = 0;
		uint64_t central_directory_end = 0;
		if (central_directory_offset > end_record_position ||
				central_directory_size > end_record_position - central_directory_offset) {
			continue;
		}
		const uint64_t byte_before_zip = end_record_position - central_directory_offset - central_directory_size;
		if (!_foundry_java_checked_add(byte_before_zip, central_directory_offset, central_directory_start) ||
				!_foundry_java_checked_add(central_directory_start, central_directory_size, central_directory_end) ||
				central_directory_end != end_record_position ||
				central_directory_start >= central_directory_end) {
			continue;
		}
		uint32_t central_directory_signature = 0;
		if (!_foundry_java_read_archive_uint32(p_archive_file, central_directory_start, central_directory_signature) ||
				central_directory_signature != FOUNDRY_JAVA_CENTRAL_DIRECTORY_ENTRY_SIGNATURE) {
			continue;
		}
		r_byte_before_zip = byte_before_zip;
		r_central_directory_start = central_directory_start;
		r_central_directory_end = central_directory_end;
		return true;
	}
	return false;
}

struct FoundryJavaCentralDirectoryContext {
	bool initialized = false;
	uint64_t byte_before_zip = 0;
	uint64_t central_directory_start = 0;
	uint64_t central_directory_end = 0;
};

static bool _foundry_java_resolve_zip64_entry_values(
		const Vector<uint8_t> &p_extra,
		uint32_t p_raw_uncompressed_size,
		uint32_t p_raw_compressed_size,
		uint32_t p_raw_local_header_offset,
		uint16_t p_raw_disk_start,
		bool p_has_offset_and_disk,
		uint64_t &r_uncompressed_size,
		uint64_t &r_compressed_size,
		uint64_t &r_local_header_offset,
		uint32_t &r_disk_start) {
	r_uncompressed_size = p_raw_uncompressed_size;
	r_compressed_size = p_raw_compressed_size;
	r_local_header_offset = p_raw_local_header_offset;
	r_disk_start = p_raw_disk_start;

	const bool needs_uncompressed_size = p_raw_uncompressed_size == UINT32_MAX;
	const bool needs_compressed_size = p_raw_compressed_size == UINT32_MAX;
	const bool needs_local_header_offset = p_has_offset_and_disk && p_raw_local_header_offset == UINT32_MAX;
	const bool needs_disk_start = p_has_offset_and_disk && p_raw_disk_start == UINT16_MAX;
	const bool needs_zip64 = needs_uncompressed_size || needs_compressed_size || needs_local_header_offset || needs_disk_start;
	bool found_zip64 = false;
	uint64_t extra_cursor = 0;
	while (extra_cursor < uint64_t(p_extra.size())) {
		if (uint64_t(p_extra.size()) - extra_cursor < 4) {
			return false;
		}
		const uint16_t header_id = _foundry_java_decode_uint16(p_extra.ptr() + extra_cursor);
		const uint16_t data_size = _foundry_java_decode_uint16(p_extra.ptr() + extra_cursor + 2);
		extra_cursor += 4;
		if (data_size > uint64_t(p_extra.size()) - extra_cursor) {
			return false;
		}
		if (header_id == 0x0001) {
			if (found_zip64) {
				return false;
			}
			found_zip64 = true;
			uint64_t zip64_cursor = extra_cursor;
			const uint64_t zip64_end = extra_cursor + data_size;
			if (needs_uncompressed_size) {
				if (zip64_end - zip64_cursor < sizeof(uint64_t)) {
					return false;
				}
				r_uncompressed_size = _foundry_java_decode_uint64(p_extra.ptr() + zip64_cursor);
				zip64_cursor += sizeof(uint64_t);
			}
			if (needs_compressed_size) {
				if (zip64_end - zip64_cursor < sizeof(uint64_t)) {
					return false;
				}
				r_compressed_size = _foundry_java_decode_uint64(p_extra.ptr() + zip64_cursor);
				zip64_cursor += sizeof(uint64_t);
			}
			if (needs_local_header_offset) {
				if (zip64_end - zip64_cursor < sizeof(uint64_t)) {
					return false;
				}
				r_local_header_offset = _foundry_java_decode_uint64(p_extra.ptr() + zip64_cursor);
				zip64_cursor += sizeof(uint64_t);
			}
			if (needs_disk_start) {
				if (zip64_end - zip64_cursor < sizeof(uint32_t)) {
					return false;
				}
				r_disk_start = _foundry_java_decode_uint32(p_extra.ptr() + zip64_cursor);
			}
		}
		extra_cursor += data_size;
	}
	return !needs_zip64 || found_zip64;
}

static bool _foundry_java_validate_central_directory_entry(
		const Ref<FileAccess> &p_archive_file,
		uint64_t p_entry_start,
		uint64_t p_central_directory_start,
		uint64_t p_central_directory_end,
		uint64_t p_byte_before_zip,
		uint64_t &r_entry_end,
		const unz_file_info64 *p_info = nullptr) {
	uint8_t header[FOUNDRY_JAVA_CENTRAL_DIRECTORY_ENTRY_SIZE];
	if (!_foundry_java_read_archive_bytes(p_archive_file, p_entry_start, header, sizeof(header)) ||
			_foundry_java_decode_uint32(header) != FOUNDRY_JAVA_CENTRAL_DIRECTORY_ENTRY_SIGNATURE) {
		return false;
	}
	const uint16_t central_flags = _foundry_java_decode_uint16(header + 8);
	const uint16_t central_compression_method = _foundry_java_decode_uint16(header + 10);
	const uint32_t central_crc = _foundry_java_decode_uint32(header + 16);
	const uint16_t filename_size = _foundry_java_decode_uint16(header + 28);
	const uint16_t extra_size = _foundry_java_decode_uint16(header + 30);
	const uint16_t comment_size = _foundry_java_decode_uint16(header + 32);
	const uint16_t raw_disk_start = _foundry_java_decode_uint16(header + 34);
	const uint32_t raw_compressed_size = _foundry_java_decode_uint32(header + 20);
	const uint32_t raw_uncompressed_size = _foundry_java_decode_uint32(header + 24);
	const uint32_t raw_local_header_offset = _foundry_java_decode_uint32(header + 42);

	uint64_t entry_end = 0;
	if (!_foundry_java_checked_add(p_entry_start, FOUNDRY_JAVA_CENTRAL_DIRECTORY_ENTRY_SIZE, entry_end) ||
			!_foundry_java_checked_add(entry_end, filename_size, entry_end) ||
			!_foundry_java_checked_add(entry_end, extra_size, entry_end) ||
			!_foundry_java_checked_add(entry_end, comment_size, entry_end) ||
			entry_end > p_central_directory_end) {
		return false;
	}

	Vector<uint8_t> central_extra;
	central_extra.resize(extra_size);
	const uint64_t central_extra_position = p_entry_start + FOUNDRY_JAVA_CENTRAL_DIRECTORY_ENTRY_SIZE + filename_size;
	if (extra_size > 0 &&
			!_foundry_java_read_archive_bytes(p_archive_file, central_extra_position, central_extra.ptrw(), extra_size)) {
		return false;
	}
	uint64_t central_uncompressed_size = 0;
	uint64_t central_compressed_size = 0;
	uint64_t local_header_offset = 0;
	uint32_t central_disk_start = 0;
	if (!_foundry_java_resolve_zip64_entry_values(
				central_extra,
				raw_uncompressed_size,
				raw_compressed_size,
				raw_local_header_offset,
				raw_disk_start,
				true,
				central_uncompressed_size,
				central_compressed_size,
				local_header_offset,
				central_disk_start) ||
			central_disk_start != 0) {
		return false;
	}
	if (p_info &&
			(filename_size != p_info->size_filename ||
					extra_size != p_info->size_file_extra ||
					comment_size != p_info->size_file_comment ||
					central_flags != p_info->flag ||
					central_compression_method != p_info->compression_method ||
					central_crc != p_info->crc ||
					central_compressed_size != p_info->compressed_size ||
					central_uncompressed_size != p_info->uncompressed_size ||
					central_disk_start != p_info->disk_num_start)) {
		return false;
	}

	uint64_t local_header_position = 0;
	if (!_foundry_java_checked_add(p_byte_before_zip, local_header_offset, local_header_position) ||
			local_header_position >= p_central_directory_start) {
		return false;
	}
	uint8_t local_header[FOUNDRY_JAVA_LOCAL_FILE_ENTRY_SIZE];
	if (!_foundry_java_read_archive_bytes(p_archive_file, local_header_position, local_header, sizeof(local_header)) ||
			_foundry_java_decode_uint32(local_header) != FOUNDRY_JAVA_LOCAL_FILE_ENTRY_SIGNATURE) {
		return false;
	}
	const uint16_t local_flags = _foundry_java_decode_uint16(local_header + 6);
	const uint16_t local_compression_method = _foundry_java_decode_uint16(local_header + 8);
	if (central_flags != local_flags || central_compression_method != local_compression_method) {
		return false;
	}
	const uint16_t local_filename_size = _foundry_java_decode_uint16(local_header + 26);
	const uint16_t local_extra_size = _foundry_java_decode_uint16(local_header + 28);
	uint64_t local_filename_position = 0;
	uint64_t local_filename_end = 0;
	uint64_t local_extra_end = 0;
	uint64_t local_payload_end = 0;
	if (local_filename_size != filename_size ||
			!_foundry_java_checked_add(local_header_position, FOUNDRY_JAVA_LOCAL_FILE_ENTRY_SIZE, local_filename_position) ||
			!_foundry_java_checked_add(local_filename_position, local_filename_size, local_filename_end) ||
			!_foundry_java_checked_add(local_filename_end, local_extra_size, local_extra_end) ||
			!_foundry_java_checked_add(local_extra_end, central_compressed_size, local_payload_end) ||
			local_payload_end > p_central_directory_start) {
		return false;
	}
	Vector<uint8_t> local_extra;
	local_extra.resize(local_extra_size);
	if (local_extra_size > 0 &&
			!_foundry_java_read_archive_bytes(p_archive_file, local_filename_end, local_extra.ptrw(), local_extra_size)) {
		return false;
	}
	uint64_t local_uncompressed_size = 0;
	uint64_t local_compressed_size = 0;
	uint64_t ignored_local_header_offset = 0;
	uint32_t ignored_disk_start = 0;
	const bool uses_data_descriptor = (central_flags & 0x0008) != 0;
	if (!_foundry_java_resolve_zip64_entry_values(
				local_extra,
				uses_data_descriptor ? 0 : _foundry_java_decode_uint32(local_header + 22),
				uses_data_descriptor ? 0 : _foundry_java_decode_uint32(local_header + 18),
				0,
				0,
				false,
				local_uncompressed_size,
				local_compressed_size,
				ignored_local_header_offset,
				ignored_disk_start)) {
		return false;
	}
	if (!uses_data_descriptor &&
			(central_crc != _foundry_java_decode_uint32(local_header + 14) ||
					central_compressed_size != local_compressed_size ||
					central_uncompressed_size != local_uncompressed_size)) {
		return false;
	}
	if (filename_size > 0) {
		Vector<uint8_t> central_filename;
		Vector<uint8_t> local_filename;
		central_filename.resize(filename_size);
		local_filename.resize(filename_size);
		if (!_foundry_java_read_archive_bytes(
					p_archive_file,
					p_entry_start + FOUNDRY_JAVA_CENTRAL_DIRECTORY_ENTRY_SIZE,
					central_filename.ptrw(),
					filename_size) ||
				!_foundry_java_read_archive_bytes(
						p_archive_file,
						local_filename_position,
						local_filename.ptrw(),
						filename_size) ||
				memcmp(central_filename.ptr(), local_filename.ptr(), filename_size) != 0) {
			return false;
		}
	}

	r_entry_end = entry_end;
	return true;
}

static bool _foundry_java_validate_current_central_directory_entry(
		unzFile p_archive,
		const Ref<FileAccess> &p_archive_file,
		const unz_global_info64 &p_global_info,
		const unz_file_info64 &p_info,
		FoundryJavaCentralDirectoryContext &r_context,
		uint64_t &r_entry_end) {
	if (!p_archive || p_archive_file.is_null()) {
		return false;
	}

	const uint64_t entry_offset = unzGetOffset64(p_archive);
	if (!r_context.initialized) {
		if (!_foundry_java_find_central_directory_extent(
					p_archive_file,
					p_global_info,
					r_context.byte_before_zip,
					r_context.central_directory_start,
					r_context.central_directory_end)) {
			return false;
		}
		r_context.initialized = true;
	}
	uint64_t entry_start = 0;
	if (!_foundry_java_checked_add(r_context.byte_before_zip, entry_offset, entry_start) ||
			entry_start < r_context.central_directory_start ||
			entry_start >= r_context.central_directory_end) {
		return false;
	}

	return _foundry_java_validate_central_directory_entry(
			p_archive_file,
			entry_start,
			r_context.central_directory_start,
			r_context.central_directory_end,
			r_context.byte_before_zip,
			r_entry_end,
			&p_info);
}

static FoundryJavaCentralDirectoryConsistency _foundry_java_check_central_directory_tail(
		const Ref<FileAccess> &p_archive_file,
		const FoundryJavaCentralDirectoryContext &p_context,
		uint64_t p_entry_end) {
	if (!p_context.initialized || p_archive_file.is_null()) {
		return FOUNDRY_JAVA_CENTRAL_DIRECTORY_CORRUPT;
	}
	if (p_entry_end == p_context.central_directory_end) {
		return FOUNDRY_JAVA_CENTRAL_DIRECTORY_CLEAN;
	}

	uint32_t next_signature = 0;
	if (!_foundry_java_read_archive_uint32(p_archive_file, p_entry_end, next_signature)) {
		return FOUNDRY_JAVA_CENTRAL_DIRECTORY_CORRUPT;
	}
	if (next_signature == FOUNDRY_JAVA_CENTRAL_DIRECTORY_ENTRY_SIGNATURE) {
		uint64_t next_entry_end = 0;
		if (_foundry_java_validate_central_directory_entry(
					p_archive_file,
					p_entry_end,
					p_context.central_directory_start,
					p_context.central_directory_end,
					p_context.byte_before_zip,
					next_entry_end)) {
			return FOUNDRY_JAVA_CENTRAL_DIRECTORY_UNREPORTED_ENTRY;
		}
		return FOUNDRY_JAVA_CENTRAL_DIRECTORY_CORRUPT;
	}
	if (next_signature == FOUNDRY_JAVA_CENTRAL_DIRECTORY_DIGITAL_SIGNATURE) {
		uint8_t signature_size_bytes[2];
		if (!_foundry_java_read_archive_bytes(p_archive_file, p_entry_end + sizeof(uint32_t), signature_size_bytes, sizeof(signature_size_bytes))) {
			return FOUNDRY_JAVA_CENTRAL_DIRECTORY_CORRUPT;
		}
		uint64_t signature_end = 0;
		if (_foundry_java_checked_add(p_entry_end, sizeof(uint32_t) + sizeof(uint16_t), signature_end) &&
				_foundry_java_checked_add(signature_end, _foundry_java_decode_uint16(signature_size_bytes), signature_end) &&
				signature_end == p_context.central_directory_end) {
			return FOUNDRY_JAVA_CENTRAL_DIRECTORY_CLEAN;
		}
	}
	return FOUNDRY_JAVA_CENTRAL_DIRECTORY_CORRUPT;
}

struct FoundryJavaInputArchiveBudget {
	uint64_t archive_count = 0;
	uint64_t total_uncompressed_bytes = 0;
};

static String _foundry_java_archive_context(const String &p_parent, const String &p_entry) {
	return p_parent.is_empty() ? p_entry : p_parent + "!" + p_entry;
}

static String _foundry_java_archive_error(const String &p_context, const String &p_error) {
	return p_context.is_empty() ? p_error : vformat(TTR("nested archive '%s': %s"), _foundry_java_safe_diagnostic_value(p_context), p_error);
}

static bool _foundry_java_is_nested_archive_entry(const String &p_entry) {
	const String lower_entry = p_entry.to_lower();
	return lower_entry.ends_with(".aar") || lower_entry.ends_with(".jar") || lower_entry.ends_with(".zip");
}

static Error _foundry_java_scan_open_archive(
		unzFile p_archive,
		const Ref<FileAccess> &p_archive_file,
		const String &p_context,
		uint64_t p_depth,
		FoundryJavaInputArchiveBudget &r_budget,
		bool &r_contains_host_library,
		String &r_entry,
		String &r_error);

static Error _foundry_java_scan_nested_archive_entry(
		unzFile p_parent_archive,
		const unz_file_info64 &p_info,
		const String &p_context,
		uint64_t p_depth,
		FoundryJavaInputArchiveBudget &r_budget,
		bool &r_contains_host_library,
		String &r_entry,
		String &r_error) {
	if (p_depth > FOUNDRY_JAVA_MAX_INPUT_ARCHIVE_DEPTH) {
		r_error = vformat(
				TTR("nested archive depth exceeds the maximum %d at '%s'"),
				FOUNDRY_JAVA_MAX_INPUT_ARCHIVE_DEPTH,
				_foundry_java_safe_diagnostic_value(p_context));
		return ERR_FILE_CORRUPT;
	}
	Error temp_error = OK;
	Ref<FileAccess> temp_file = FileAccess::create_temp(
			FileAccess::WRITE_READ,
			"foundry_java_archive",
			"zip",
			false,
			&temp_error);
	if (temp_error != OK || temp_file.is_null()) {
		r_error = vformat(
				TTR("temporary storage for nested archive '%s' could not be created"),
				_foundry_java_safe_diagnostic_value(p_context));
		return ERR_FILE_CORRUPT;
	}
	if (unzOpenCurrentFile(p_parent_archive) != UNZ_OK) {
		r_error = vformat(
				TTR("nested archive entry '%s' could not be opened"),
				_foundry_java_safe_diagnostic_value(p_context));
		return ERR_FILE_CORRUPT;
	}

	Vector<uint8_t> buffer;
	buffer.resize(FOUNDRY_JAVA_INPUT_ARCHIVE_READ_CHUNK_BYTES);
	uint64_t bytes_read = 0;
	Error read_error = OK;
	while (true) {
		const int result = unzReadCurrentFile(p_parent_archive, buffer.ptrw(), buffer.size());
		if (result < 0) {
			read_error = ERR_FILE_CORRUPT;
			break;
		}
		if (result == 0) {
			break;
		}
		if (uint64_t(result) > p_info.uncompressed_size - bytes_read ||
				!temp_file->store_buffer(buffer.ptr(), result)) {
			read_error = ERR_FILE_CORRUPT;
			break;
		}
		bytes_read += result;
	}
	const int close_result = unzCloseCurrentFile(p_parent_archive);
	if (read_error != OK || bytes_read != p_info.uncompressed_size || close_result != UNZ_OK) {
		r_error = vformat(
				TTR("nested archive entry '%s' failed integrity validation"),
				_foundry_java_safe_diagnostic_value(p_context));
		return ERR_FILE_CORRUPT;
	}
	temp_file->flush();
	const String temp_path = temp_file->get_path_absolute();
	temp_file->close();

	Ref<FileAccess> archive_file;
	zlib_filefunc_def io = zipio_create_io(&archive_file);
	unzFile archive = unzOpen2(temp_path.utf8().get_data(), &io);
	if (!archive) {
		r_error = vformat(
				TTR("nested archive '%s' could not be opened"),
				_foundry_java_safe_diagnostic_value(p_context));
		return ERR_FILE_CORRUPT;
	}
	return _foundry_java_scan_open_archive(
			archive,
			archive_file,
			p_context,
			p_depth,
			r_budget,
			r_contains_host_library,
			r_entry,
			r_error);
}

static Error _foundry_java_scan_open_archive(
		unzFile p_archive,
		const Ref<FileAccess> &p_archive_file,
		const String &p_context,
		uint64_t p_depth,
		FoundryJavaInputArchiveBudget &r_budget,
		bool &r_contains_host_library,
		String &r_entry,
		String &r_error) {
	r_budget.archive_count++;
	if (r_budget.archive_count > FOUNDRY_JAVA_MAX_INPUT_ARCHIVES) {
		r_error = vformat(
				TTR("nested archive count exceeds the maximum %d at '%s'"),
				FOUNDRY_JAVA_MAX_INPUT_ARCHIVES,
				_foundry_java_safe_diagnostic_value(p_context));
		unzClose(p_archive);
		return ERR_FILE_CORRUPT;
	}

	unz_global_info64 global_info;
	memset(&global_info, 0, sizeof(global_info));
	if (unzGetGlobalInfo64(p_archive, &global_info) != UNZ_OK) {
		r_error = _foundry_java_archive_error(p_context, TTR("archive global metadata could not be read"));
		unzClose(p_archive);
		return ERR_FILE_CORRUPT;
	}
	if (global_info.number_entry > FOUNDRY_JAVA_MAX_INPUT_ARCHIVE_ENTRIES) {
		r_error = _foundry_java_archive_error(
				p_context,
				vformat(
						TTR("archive reports too many entries (%d; maximum %d)"),
						global_info.number_entry,
						FOUNDRY_JAVA_MAX_INPUT_ARCHIVE_ENTRIES));
		unzClose(p_archive);
		return ERR_FILE_CORRUPT;
	}
	if (global_info.number_entry == 0) {
		const uint64_t expected_empty_archive_size =
				FOUNDRY_JAVA_CLASSIC_END_OF_CENTRAL_DIRECTORY_SIZE + global_info.size_comment;
		if (p_archive_file.is_null() || p_archive_file->get_length() != expected_empty_archive_size) {
			r_error = _foundry_java_archive_error(
					p_context,
					TTR("archive entry count does not match its non-empty central directory"));
			unzClose(p_archive);
			return ERR_FILE_CORRUPT;
		}
		if (unzClose(p_archive) != UNZ_OK) {
			r_error = _foundry_java_archive_error(p_context, TTR("archive could not be closed cleanly"));
			return ERR_FILE_CORRUPT;
		}
		return OK;
	}

	uint64_t entries_scanned = 0;
	uint64_t total_entry_name_bytes = 0;
	FoundryJavaCentralDirectoryContext central_directory_context;
	int result = unzGoToFirstFile(p_archive);
	while (result == UNZ_OK) {
		unz_file_info64 info;
		memset(&info, 0, sizeof(info));
		if (unzGetCurrentFileInfo64(p_archive, &info, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK) {
			r_error = _foundry_java_archive_error(p_context, TTR("archive entry metadata could not be read"));
			unzClose(p_archive);
			return ERR_FILE_CORRUPT;
		}
		if (info.size_filename > FOUNDRY_JAVA_MAX_ARCHIVE_ENTRY_NAME_BYTES) {
			r_error = _foundry_java_archive_error(
					p_context,
					vformat(TTR("archive entry name is too long (%d bytes; maximum %d)"), info.size_filename, FOUNDRY_JAVA_MAX_ARCHIVE_ENTRY_NAME_BYTES));
			unzClose(p_archive);
			return ERR_FILE_CORRUPT;
		}
		if (info.size_filename > FOUNDRY_JAVA_MAX_ARCHIVE_ENTRY_NAMES_BYTES - total_entry_name_bytes) {
			r_error = _foundry_java_archive_error(
					p_context,
					vformat(TTR("archive entry names exceed the aggregate %d-byte limit"), FOUNDRY_JAVA_MAX_ARCHIVE_ENTRY_NAMES_BYTES));
			unzClose(p_archive);
			return ERR_FILE_CORRUPT;
		}
		total_entry_name_bytes += info.size_filename;

		Vector<char> filename;
		filename.resize(info.size_filename + 1);
		if (unzGetCurrentFileInfo64(p_archive, &info, filename.ptrw(), filename.size(), nullptr, 0, nullptr, 0) != UNZ_OK) {
			r_error = _foundry_java_archive_error(p_context, TTR("archive entry name could not be read"));
			unzClose(p_archive);
			return ERR_FILE_CORRUPT;
		}
		uint64_t central_directory_entry_end = 0;
		if (!_foundry_java_validate_current_central_directory_entry(
					p_archive,
					p_archive_file,
					global_info,
					info,
					central_directory_context,
					central_directory_entry_end)) {
			r_error = _foundry_java_archive_error(p_context, TTR("archive central directory metadata is corrupt"));
			unzClose(p_archive);
			return ERR_FILE_CORRUPT;
		}
		filename.write[info.size_filename] = '\0';
		for (uint64_t i = 0; i < info.size_filename; i++) {
			if (filename[i] == '\0') {
				r_error = _foundry_java_archive_error(p_context, TTR("archive entry name contains an embedded NUL byte"));
				unzClose(p_archive);
				return ERR_FILE_CORRUPT;
			}
		}
		String entry;
		if (entry.append_utf8(filename.ptr(), info.size_filename) != OK) {
			r_error = _foundry_java_archive_error(p_context, TTR("archive entry name is not valid UTF-8"));
			unzClose(p_archive);
			return ERR_FILE_CORRUPT;
		}
		const String entry_context = _foundry_java_archive_context(p_context, entry);
		const bool is_directory = entry.ends_with("/");
		if (!is_directory) {
			if (info.uncompressed_size > FOUNDRY_JAVA_MAX_INPUT_ARCHIVE_ENTRY_UNCOMPRESSED_BYTES) {
				r_error = vformat(
						TTR("archive entry decompressed size exceeds the %d-byte limit at '%s'"),
						FOUNDRY_JAVA_MAX_INPUT_ARCHIVE_ENTRY_UNCOMPRESSED_BYTES,
						_foundry_java_safe_diagnostic_value(entry_context));
				unzClose(p_archive);
				return ERR_FILE_CORRUPT;
			}
			if (info.uncompressed_size > FOUNDRY_JAVA_MAX_INPUT_ARCHIVE_TOTAL_UNCOMPRESSED_BYTES - r_budget.total_uncompressed_bytes) {
				r_error = vformat(
						TTR("archive entries exceed the aggregate %d-byte decompressed size limit at '%s'"),
						FOUNDRY_JAVA_MAX_INPUT_ARCHIVE_TOTAL_UNCOMPRESSED_BYTES,
						_foundry_java_safe_diagnostic_value(entry_context));
				unzClose(p_archive);
				return ERR_FILE_CORRUPT;
			}
			r_budget.total_uncompressed_bytes += info.uncompressed_size;
		}
		if (entry == "libfoundry_android.so" || entry.ends_with("/libfoundry_android.so")) {
			r_contains_host_library = true;
			r_entry = entry_context;
		}
		if (!is_directory && _foundry_java_is_nested_archive_entry(entry)) {
			if (r_budget.archive_count >= FOUNDRY_JAVA_MAX_INPUT_ARCHIVES) {
				r_error = vformat(
						TTR("nested archive count exceeds the maximum %d at '%s'"),
						FOUNDRY_JAVA_MAX_INPUT_ARCHIVES,
						_foundry_java_safe_diagnostic_value(entry_context));
				unzClose(p_archive);
				return ERR_FILE_CORRUPT;
			}
			if (_foundry_java_scan_nested_archive_entry(
						p_archive,
						info,
						entry_context,
						p_depth + 1,
						r_budget,
						r_contains_host_library,
						r_entry,
						r_error) != OK) {
				unzClose(p_archive);
				return ERR_FILE_CORRUPT;
			}
		}
		entries_scanned++;
		if (entries_scanned == global_info.number_entry) {
			const FoundryJavaCentralDirectoryConsistency consistency =
					_foundry_java_check_central_directory_tail(p_archive_file, central_directory_context, central_directory_entry_end);
			if (consistency == FOUNDRY_JAVA_CENTRAL_DIRECTORY_UNREPORTED_ENTRY) {
				r_error = _foundry_java_archive_error(p_context, TTR("archive entry count does not match its central directory"));
				unzClose(p_archive);
				return ERR_FILE_CORRUPT;
			}
			if (consistency == FOUNDRY_JAVA_CENTRAL_DIRECTORY_CORRUPT) {
				r_error = _foundry_java_archive_error(p_context, TTR("archive central directory metadata is corrupt"));
				unzClose(p_archive);
				return ERR_FILE_CORRUPT;
			}
			result = UNZ_END_OF_LIST_OF_FILE;
			break;
		}
		result = unzGoToNextFile(p_archive);
	}
	if (result != UNZ_OK && result != UNZ_END_OF_LIST_OF_FILE) {
		r_error = _foundry_java_archive_error(p_context, TTR("archive traversal failed"));
		unzClose(p_archive);
		return ERR_FILE_CORRUPT;
	}
	if (unzClose(p_archive) != UNZ_OK) {
		r_error = _foundry_java_archive_error(p_context, TTR("archive could not be closed cleanly"));
		return ERR_FILE_CORRUPT;
	}
	return OK;
}

static Error _foundry_java_scan_archive(const String &p_path, bool &r_contains_host_library, String &r_entry, String &r_error) {
	r_contains_host_library = false;
	r_entry.clear();
	r_error.clear();

	Ref<FileAccess> archive_file;
	zlib_filefunc_def io = zipio_create_io(&archive_file);
	unzFile archive = unzOpen2(p_path.utf8().get_data(), &io);
	if (!archive) {
		r_error = TTR("archive could not be opened");
		return ERR_FILE_CORRUPT;
	}
	FoundryJavaInputArchiveBudget budget;
	return _foundry_java_scan_open_archive(
			archive,
			archive_file,
			String(),
			0,
			budget,
			r_contains_host_library,
			r_entry,
			r_error);
}

static bool _foundry_java_validate_current_archive_entry_payload(unzFile p_archive, const unz_file_info64 &p_info) {
	if (unzOpenCurrentFile(p_archive) != UNZ_OK) {
		return false;
	}

	Vector<uint8_t> buffer;
	buffer.resize(FOUNDRY_JAVA_INPUT_ARCHIVE_READ_CHUNK_BYTES);
	uint64_t bytes_read = 0;
	bool valid = true;
	while (true) {
		const int result = unzReadCurrentFile(p_archive, buffer.ptrw(), buffer.size());
		if (result < 0) {
			valid = false;
			break;
		}
		if (result == 0) {
			break;
		}
		if (bytes_read > p_info.uncompressed_size || uint64_t(result) > p_info.uncompressed_size - bytes_read) {
			valid = false;
			break;
		}
		bytes_read += result;
	}
	const int close_result = unzCloseCurrentFile(p_archive);
	return valid && bytes_read == p_info.uncompressed_size && close_result == UNZ_OK;
}

Error EditorExportPlatformAndroid::_inspect_foundry_java_artifact(const String &p_path, const Vector<ABI> &p_enabled_abis, int p_export_format, String &r_error) const {
	const String artifact_kind = p_export_format == EXPORT_FORMAT_AAB ? "AAB" : "APK";
	const String root = p_export_format == EXPORT_FORMAT_AAB ? "base/" : "";
	const String configuration = root + "assets/FoundryJava.foundryextension";
	const String registry_index = root + "assets/foundry_java/registry-index-v2.txt";
	Vector<String> expected_bridges;
	for (const ABI &abi : p_enabled_abis) {
		expected_bridges.push_back(root + "lib/" + abi.abi + "/libfoundry_java.so");
	}

	Ref<FileAccess> artifact_file;
	zlib_filefunc_def io = zipio_create_io(&artifact_file);
	unzFile artifact = unzOpen2(p_path.utf8().get_data(), &io);
	if (!artifact) {
		r_error = vformat(TTR("Unable to inspect final Foundry-Java %s: archive could not be opened."), artifact_kind);
		return ERR_FILE_CORRUPT;
	}
	unz_global_info64 global_info;
	memset(&global_info, 0, sizeof(global_info));
	if (unzGetGlobalInfo64(artifact, &global_info) != UNZ_OK) {
		r_error = vformat(TTR("Unable to inspect final Foundry-Java %s: archive global metadata could not be read."), artifact_kind);
		unzClose(artifact);
		return ERR_FILE_CORRUPT;
	}
	if (global_info.number_entry > FOUNDRY_JAVA_MAX_FINAL_ARTIFACT_ENTRIES) {
		r_error = vformat(
				TTR("Unable to inspect final Foundry-Java %s: archive reports too many entries (%d; maximum %d)."),
				artifact_kind,
				global_info.number_entry,
				FOUNDRY_JAVA_MAX_FINAL_ARTIFACT_ENTRIES);
		unzClose(artifact);
		return ERR_FILE_CORRUPT;
	}

	int configuration_count = 0;
	int registry_index_count = 0;
	Vector<String> bridge_entries;
	uint64_t entries_scanned = 0;
	uint64_t total_entry_name_bytes = 0;
	uint64_t total_required_entry_uncompressed_bytes = 0;
	FoundryJavaCentralDirectoryContext central_directory_context;
	int result = unzGoToFirstFile(artifact);
	while (result == UNZ_OK) {
		unz_file_info64 info;
		memset(&info, 0, sizeof(info));
		if (unzGetCurrentFileInfo64(artifact, &info, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK ||
				info.size_filename > FOUNDRY_JAVA_MAX_ARCHIVE_ENTRY_NAME_BYTES ||
				info.size_filename > FOUNDRY_JAVA_MAX_ARCHIVE_ENTRY_NAMES_BYTES - total_entry_name_bytes) {
			r_error = vformat(TTR("Unable to inspect final Foundry-Java %s: archive entry metadata exceeds inspection limits."), artifact_kind);
			unzClose(artifact);
			return ERR_FILE_CORRUPT;
		}
		total_entry_name_bytes += info.size_filename;
		Vector<char> filename;
		filename.resize(info.size_filename + 1);
		if (unzGetCurrentFileInfo64(artifact, &info, filename.ptrw(), filename.size(), nullptr, 0, nullptr, 0) != UNZ_OK) {
			r_error = vformat(TTR("Unable to inspect final Foundry-Java %s: archive entry name could not be read."), artifact_kind);
			unzClose(artifact);
			return ERR_FILE_CORRUPT;
		}
		filename.write[info.size_filename] = '\0';
		if (info.size_filename >= 3 &&
				uint8_t(filename[0]) == 0xEF &&
				uint8_t(filename[1]) == 0xBB &&
				uint8_t(filename[2]) == 0xBF) {
			r_error = vformat(TTR("Unable to inspect final Foundry-Java %s: archive entry name begins with a UTF-8 byte order mark."), artifact_kind);
			unzClose(artifact);
			return ERR_FILE_CORRUPT;
		}
		for (uint64_t i = 0; i < info.size_filename; i++) {
			if (filename[i] == '\0') {
				r_error = vformat(TTR("Unable to inspect final Foundry-Java %s: archive entry name contains an embedded NUL byte."), artifact_kind);
				unzClose(artifact);
				return ERR_FILE_CORRUPT;
			}
		}
		String entry;
		if (entry.append_utf8(filename.ptr(), info.size_filename) != OK) {
			r_error = vformat(TTR("Unable to inspect final Foundry-Java %s: archive entry name is not valid UTF-8."), artifact_kind);
			unzClose(artifact);
			return ERR_FILE_CORRUPT;
		}
		uint64_t central_directory_entry_end = 0;
		if (!_foundry_java_validate_current_central_directory_entry(
					artifact,
					artifact_file,
					global_info,
					info,
					central_directory_context,
					central_directory_entry_end)) {
			r_error = vformat(TTR("Unable to inspect final Foundry-Java %s: archive central directory metadata is corrupt."), artifact_kind);
			unzClose(artifact);
			return ERR_FILE_CORRUPT;
		}
		bool is_required_entry = false;
		if (entry == configuration) {
			configuration_count++;
			is_required_entry = true;
		}
		if (entry == registry_index) {
			registry_index_count++;
			is_required_entry = true;
		}
		if (entry.ends_with("/libfoundry_java.so")) {
			bridge_entries.push_back(entry);
			is_required_entry = expected_bridges.has(entry);
		}
		if (is_required_entry &&
				info.uncompressed_size > FOUNDRY_JAVA_MAX_FINAL_ARTIFACT_REQUIRED_ENTRY_UNCOMPRESSED_BYTES) {
			r_error = vformat(
					TTR("Unable to inspect final Foundry-Java %s: required entry '%s' exceeds the %d-byte decompressed size limit."),
					artifact_kind,
					_foundry_java_safe_diagnostic_value(entry),
					FOUNDRY_JAVA_MAX_FINAL_ARTIFACT_REQUIRED_ENTRY_UNCOMPRESSED_BYTES);
			unzClose(artifact);
			return ERR_FILE_CORRUPT;
		}
		if (is_required_entry &&
				info.uncompressed_size >
						FOUNDRY_JAVA_MAX_FINAL_ARTIFACT_REQUIRED_ENTRIES_UNCOMPRESSED_BYTES - total_required_entry_uncompressed_bytes) {
			r_error = vformat(
					TTR("Unable to inspect final Foundry-Java %s: required entries exceed the aggregate %d-byte decompressed size limit at '%s'."),
					artifact_kind,
					FOUNDRY_JAVA_MAX_FINAL_ARTIFACT_REQUIRED_ENTRIES_UNCOMPRESSED_BYTES,
					_foundry_java_safe_diagnostic_value(entry));
			unzClose(artifact);
			return ERR_FILE_CORRUPT;
		}
		if (is_required_entry) {
			total_required_entry_uncompressed_bytes += info.uncompressed_size;
		}
		if (is_required_entry && !_foundry_java_validate_current_archive_entry_payload(artifact, info)) {
			r_error = vformat(
					TTR("Unable to inspect final Foundry-Java %s: required entry '%s' failed integrity validation."),
					artifact_kind,
					_foundry_java_safe_diagnostic_value(entry));
			unzClose(artifact);
			return ERR_FILE_CORRUPT;
		}
		entries_scanned++;
		if (entries_scanned > FOUNDRY_JAVA_MAX_FINAL_ARTIFACT_ENTRIES) {
			r_error = vformat(TTR("Unable to inspect final Foundry-Java %s: archive contains too many entries."), artifact_kind);
			unzClose(artifact);
			return ERR_FILE_CORRUPT;
		}
		if (entries_scanned == global_info.number_entry) {
			const FoundryJavaCentralDirectoryConsistency consistency =
					_foundry_java_check_central_directory_tail(artifact_file, central_directory_context, central_directory_entry_end);
			if (consistency == FOUNDRY_JAVA_CENTRAL_DIRECTORY_UNREPORTED_ENTRY) {
				r_error = vformat(TTR("Unable to inspect final Foundry-Java %s: archive entry count does not match its central directory."), artifact_kind);
				unzClose(artifact);
				return ERR_FILE_CORRUPT;
			}
			if (consistency == FOUNDRY_JAVA_CENTRAL_DIRECTORY_CORRUPT) {
				r_error = vformat(TTR("Unable to inspect final Foundry-Java %s: archive central directory metadata is corrupt."), artifact_kind);
				unzClose(artifact);
				return ERR_FILE_CORRUPT;
			}
			result = UNZ_END_OF_LIST_OF_FILE;
			break;
		}
		result = unzGoToNextFile(artifact);
	}
	if (result != UNZ_END_OF_LIST_OF_FILE) {
		r_error = vformat(TTR("Unable to inspect final Foundry-Java %s: archive traversal failed."), artifact_kind);
		unzClose(artifact);
		return ERR_FILE_CORRUPT;
	}
	if (unzClose(artifact) != UNZ_OK) {
		r_error = vformat(TTR("Unable to inspect final Foundry-Java %s: archive could not be closed cleanly."), artifact_kind);
		return ERR_FILE_CORRUPT;
	}

	if (configuration_count != 1) {
		r_error = vformat(TTR("Final Foundry-Java %s must contain exactly one %s; found %d."), artifact_kind, configuration, configuration_count);
		return ERR_INVALID_DATA;
	}
	if (registry_index_count != 1) {
		r_error = vformat(TTR("Final Foundry-Java %s must contain exactly one %s; found %d."), artifact_kind, registry_index, registry_index_count);
		return ERR_INVALID_DATA;
	}

	bridge_entries.sort();
	expected_bridges.sort();
	if (bridge_entries != expected_bridges) {
		r_error = vformat(
				TTR("Final Foundry-Java %s bridge entries differ from the requested ABI set: expected [%s], found [%s]."),
				artifact_kind,
				String(", ").join(expected_bridges),
				String(", ").join(bridge_entries));
		return ERR_INVALID_DATA;
	}
	return OK;
}

Error EditorExportPlatformAndroid::_get_foundry_java_export_config(const EditorExportPreset *p_preset, FoundryJavaExportConfig &r_config, String &r_error) const {
	static const String ENABLED_OPTION = "gradle_build/foundry_java/enabled";
	static const String USE_GRADLE_OPTION = "gradle_build/use_gradle_build";
	static const String PLUGIN_MAVEN_OPTION = "gradle_build/foundry_java/gradle_plugin_maven";
	static const String PLUGIN_LOCAL_OPTION = "gradle_build/foundry_java/gradle_plugin_local";
	static const String REPOSITORIES_OPTION = "gradle_build/foundry_java/maven_repositories";
	static const String MAVEN_ARTIFACTS_OPTION = "gradle_build/foundry_java/maven_artifacts";
	static const String LOCAL_ARTIFACTS_OPTION = "gradle_build/foundry_java/local_artifacts";

	r_config = FoundryJavaExportConfig();
	r_config.enabled = bool(p_preset->get(ENABLED_OPTION));
	if (!r_config.enabled) {
		return OK;
	}
	if (!bool(p_preset->get(USE_GRADLE_OPTION))) {
		r_error = vformat(
				TTR("Export option %s value 'true' requires %s value 'true', but its offending value is 'false'."),
				ENABLED_OPTION,
				USE_GRADLE_OPTION);
		return ERR_INVALID_PARAMETER;
	}

	const String plugin_maven_raw = p_preset->get(PLUGIN_MAVEN_OPTION);
	const String plugin_local_raw = p_preset->get(PLUGIN_LOCAL_OPTION);
	if (_foundry_java_contains_property_separator(plugin_maven_raw)) {
		return _foundry_java_invalid_option(PLUGIN_MAVEN_OPTION, plugin_maven_raw, TTR("must not contain carriage returns, newlines, or '|'"), r_error);
	}
	if (_foundry_java_contains_property_separator(plugin_local_raw)) {
		return _foundry_java_invalid_option(PLUGIN_LOCAL_OPTION, plugin_local_raw, TTR("must not contain carriage returns, newlines, or '|'"), r_error);
	}
	const String plugin_maven = plugin_maven_raw.strip_edges();
	const String plugin_local = plugin_local_raw.strip_edges();
	if (plugin_maven.is_empty() == plugin_local.is_empty()) {
		r_error = vformat(
				TTR("Export options %s value '%s' and %s value '%s' must select exactly one Maven or local Gradle plugin."),
				PLUGIN_MAVEN_OPTION,
				_foundry_java_safe_diagnostic_value(plugin_maven_raw),
				PLUGIN_LOCAL_OPTION,
				_foundry_java_safe_diagnostic_value(plugin_local_raw));
		return ERR_INVALID_PARAMETER;
	}
	if (!plugin_maven.is_empty()) {
		if (!_is_exact_foundry_java_coordinate(plugin_maven)) {
			return _foundry_java_invalid_option(PLUGIN_MAVEN_OPTION, plugin_maven_raw, TTR("must be an exact group:artifact:version value"), r_error);
		}
		r_config.plugin_kind = "maven";
		r_config.plugin = plugin_maven;
	} else {
		r_config.plugin_kind = "local";
		const String globalized_plugin = ProjectSettings::get_singleton()->globalize_path(plugin_local);
		const String simplified_plugin = globalized_plugin.simplify_path();
		if (_foundry_java_path_has_symlink(simplified_plugin)) {
			return _foundry_java_invalid_option(PLUGIN_LOCAL_OPTION, plugin_local_raw, TTR("must not traverse a symbolic link"), r_error);
		}
		if (!FileAccess::exists(simplified_plugin) || DirAccess::exists(simplified_plugin) || !simplified_plugin.to_lower().ends_with(".jar")) {
			return _foundry_java_invalid_option(PLUGIN_LOCAL_OPTION, plugin_local_raw, TTR("must name a regular .jar file"), r_error);
		}
		Error open_error = OK;
		Ref<FileAccess> plugin_file = FileAccess::open(simplified_plugin, FileAccess::READ, &open_error);
		if (open_error != OK || plugin_file.is_null()) {
			return _foundry_java_invalid_option(PLUGIN_LOCAL_OPTION, plugin_local_raw, TTR("could not be opened as a regular .jar file"), r_error);
		}
		r_config.plugin = plugin_file->get_path_absolute().simplify_path();
		plugin_file.unref();

		bool contains_host_library = false;
		String forbidden_entry;
		String archive_error;
		if (_foundry_java_scan_archive(r_config.plugin, contains_host_library, forbidden_entry, archive_error) != OK) {
			return _foundry_java_invalid_option(PLUGIN_LOCAL_OPTION, plugin_local_raw, archive_error, r_error);
		}
		if (contains_host_library) {
			r_error = vformat(
					TTR("Invalid export option %s value '%s': archive entry '%s' contains forbidden libfoundry_android.so."),
					PLUGIN_LOCAL_OPTION,
					_foundry_java_safe_diagnostic_value(plugin_local_raw),
					_foundry_java_safe_diagnostic_value(forbidden_entry));
			return ERR_INVALID_PARAMETER;
		}
	}

	r_config.repositories = p_preset->get(REPOSITORIES_OPTION);
	r_config.maven_artifacts = p_preset->get(MAVEN_ARTIFACTS_OPTION);
	const Variant local_artifacts_value = p_preset->get(LOCAL_ARTIFACTS_OPTION);
	if (local_artifacts_value.get_type() == Variant::PACKED_STRING_ARRAY) {
		r_config.local_artifacts = local_artifacts_value;
	} else if (local_artifacts_value.get_type() == Variant::ARRAY) {
		const Array local_artifacts = local_artifacts_value;
		for (const Variant &artifact : local_artifacts) {
			if (artifact.get_type() != Variant::STRING) {
				return _foundry_java_invalid_option(
						LOCAL_ARTIFACTS_OPTION,
						artifact.stringify(),
						vformat(TTR("must contain only path strings naming regular .jar or .aar files, not %s"), Variant::get_type_name(artifact.get_type())),
						r_error);
			}
			r_config.local_artifacts.push_back(artifact);
		}
	} else {
		return _foundry_java_invalid_option(
				LOCAL_ARTIFACTS_OPTION,
				local_artifacts_value.stringify(),
				vformat(TTR("must be an array of path strings naming regular .jar or .aar files, not %s"), Variant::get_type_name(local_artifacts_value.get_type())),
				r_error);
	}
	HashSet<String> seen;
	for (int i = 0; i < r_config.repositories.size(); i++) {
		const String raw = r_config.repositories[i];
		const String value = raw.strip_edges();
		if (_foundry_java_contains_property_separator(raw)) {
			return _foundry_java_invalid_option(REPOSITORIES_OPTION, "<redacted>", TTR("must not contain carriage returns, newlines, or '|'"), r_error);
		}
		if (raw != value || value.is_empty() || seen.has(value)) {
			return _foundry_java_invalid_option(REPOSITORIES_OPTION, "<redacted>", TTR("must contain non-empty unique values"), r_error);
		}
		seen.insert(value);
		if (!_foundry_java_has_safe_repository_url(value)) {
			return _foundry_java_invalid_option(
					REPOSITORIES_OPTION,
					"<redacted>",
					TTR("must use HTTPS or a local file URL without credentials, query, or fragment"),
					r_error);
		}
		r_config.repositories.set(i, value);
	}
	r_config.repositories.sort();

	seen.clear();
	for (int i = 0; i < r_config.maven_artifacts.size(); i++) {
		const String raw = r_config.maven_artifacts[i];
		const String value = raw.strip_edges();
		if (_foundry_java_contains_property_separator(raw)) {
			return _foundry_java_invalid_option(MAVEN_ARTIFACTS_OPTION, raw, TTR("must not contain carriage returns, newlines, or '|'"), r_error);
		}
		if (seen.has(value) || !_is_exact_foundry_java_coordinate(value)) {
			return _foundry_java_invalid_option(MAVEN_ARTIFACTS_OPTION, raw, TTR("must contain unique exact group:artifact:version values"), r_error);
		}
		seen.insert(value);
		r_config.maven_artifacts.set(i, value);
	}
	r_config.maven_artifacts.sort();

	seen.clear();
	Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	if (filesystem.is_null()) {
		return _foundry_java_invalid_option(
				LOCAL_ARTIFACTS_OPTION,
				String(", ").join(r_config.local_artifacts),
				TTR("could not create filesystem access for local artifact validation"),
				r_error);
	}
	Vector<String> canonical_local_artifacts;
	for (int i = 0; i < r_config.local_artifacts.size(); i++) {
		const String raw = r_config.local_artifacts[i];
		const String original = raw.strip_edges();
		if (_foundry_java_contains_property_separator(raw)) {
			return _foundry_java_invalid_option(LOCAL_ARTIFACTS_OPTION, raw, TTR("must not contain carriage returns, newlines, or '|'"), r_error);
		}
		if (original.is_empty()) {
			return _foundry_java_invalid_option(LOCAL_ARTIFACTS_OPTION, raw, TTR("must contain non-empty unique paths"), r_error);
		}
		const String globalized_path = ProjectSettings::get_singleton()->globalize_path(original);
		const String simplified_path = globalized_path.simplify_path();
		if (_foundry_java_path_has_symlink(simplified_path)) {
			return _foundry_java_invalid_option(LOCAL_ARTIFACTS_OPTION, raw, TTR("must not traverse a symbolic link"), r_error);
		}
		if (!FileAccess::exists(simplified_path) || DirAccess::exists(simplified_path) ||
				!(simplified_path.to_lower().ends_with(".jar") || simplified_path.to_lower().ends_with(".aar"))) {
			return _foundry_java_invalid_option(LOCAL_ARTIFACTS_OPTION, raw, TTR("must name a regular .jar or .aar file"), r_error);
		}
		Error open_error = OK;
		Ref<FileAccess> artifact_file = FileAccess::open(simplified_path, FileAccess::READ, &open_error);
		if (open_error != OK || artifact_file.is_null()) {
			return _foundry_java_invalid_option(LOCAL_ARTIFACTS_OPTION, raw, TTR("could not be opened as a regular .jar or .aar file"), r_error);
		}
		const String path = artifact_file->get_path_absolute().simplify_path();
		artifact_file.unref();

		for (const String &canonical_artifact : canonical_local_artifacts) {
			if (filesystem->is_equivalent(canonical_artifact, path)) {
				return _foundry_java_invalid_option(LOCAL_ARTIFACTS_OPTION, raw, TTR("resolves to a duplicate local artifact"), r_error);
			}
		}

		bool contains_host_library = false;
		String forbidden_entry;
		String archive_error;
		if (_foundry_java_scan_archive(path, contains_host_library, forbidden_entry, archive_error) != OK) {
			return _foundry_java_invalid_option(LOCAL_ARTIFACTS_OPTION, raw, archive_error, r_error);
		}
		if (contains_host_library) {
			r_error = vformat(
					TTR("Invalid export option %s value '%s': archive entry '%s' contains forbidden libfoundry_android.so."),
					LOCAL_ARTIFACTS_OPTION,
					_foundry_java_safe_diagnostic_value(raw),
					_foundry_java_safe_diagnostic_value(forbidden_entry));
			return ERR_INVALID_PARAMETER;
		}
		canonical_local_artifacts.push_back(path);
		r_config.local_artifacts.set(i, path);
	}
	r_config.local_artifacts.sort();

	if (r_config.maven_artifacts.is_empty() && r_config.local_artifacts.is_empty()) {
		r_error = vformat(
				TTR("Export options %s value '<empty>' and %s value '<empty>' require at least one Maven or local application artifact."),
				MAVEN_ARTIFACTS_OPTION,
				LOCAL_ARTIFACTS_OPTION);
		return ERR_INVALID_PARAMETER;
	}
	return OK;
}

void EditorExportPlatformAndroid::get_preset_features(const Ref<EditorExportPreset> &p_preset, List<String> *r_features) const {
	r_features->push_back("etc2");
	r_features->push_back("astc");

	if (!p_preset->is_dedicated_server() && p_preset->get("shader_baker/enabled")) {
		// Don't use the shader baker if exporting as a dedicated server, as no rendering is performed.
		r_features->push_back("shader_baker");
	}

	Vector<ABI> abis = get_enabled_abis(p_preset);
	for (int i = 0; i < abis.size(); ++i) {
		r_features->push_back(abis[i].arch);
	}
}

String EditorExportPlatformAndroid::get_export_option_warning(const EditorExportPreset *p_preset, const StringName &p_name) const {
	if (p_preset) {
		if (String(p_name).begins_with("gradle_build/foundry_java/")) {
			FoundryJavaExportConfig config;
			String error;
			if (_get_foundry_java_export_config(p_preset, config, error) != OK) {
				return error;
			}
		} else if (p_name == ("apk_expansion/public_key")) {
			bool apk_expansion = p_preset->get("apk_expansion/enable");
			String apk_expansion_pkey = p_preset->get("apk_expansion/public_key");
			if (apk_expansion && apk_expansion_pkey.is_empty()) {
				return TTR("Invalid public key for APK expansion.");
			}
		} else if (p_name == "package/unique_name") {
			String pn = p_preset->get("package/unique_name");
			String pn_err;

			if (!is_package_name_valid(Ref<EditorExportPreset>(p_preset), pn, &pn_err)) {
				return TTR("Invalid package name:") + " " + pn_err;
			}
		} else if (p_name == "gesture/swipe_to_dismiss") {
			bool gradle_build_enabled = p_preset->get("gradle_build/use_gradle_build");
			if (bool(p_preset->get("gesture/swipe_to_dismiss")) && !gradle_build_enabled) {
				return TTR("\"Use Gradle Build\" is required to enable \"Swipe to dismiss\".");
			}
		} else if (p_name == "gradle_build/compress_native_libraries") {
			bool gradle_build_enabled = p_preset->get("gradle_build/use_gradle_build");
			if (bool(p_preset->get("gradle_build/compress_native_libraries")) && !gradle_build_enabled) {
				return TTR("\"Compress Native Libraries\" is only valid when \"Use Gradle Build\" is enabled.");
			}
		} else if (p_name == "gradle_build/export_format") {
			bool gradle_build_enabled = p_preset->get("gradle_build/use_gradle_build");
			if (int(p_preset->get("gradle_build/export_format")) == EXPORT_FORMAT_AAB && !gradle_build_enabled) {
				return TTR("\"Export AAB\" is only valid when \"Use Gradle Build\" is enabled.");
			}
		} else if (p_name == "gradle_build/min_sdk") {
			String min_sdk_str = p_preset->get("gradle_build/min_sdk");
			bool gradle_build_enabled = p_preset->get("gradle_build/use_gradle_build");
			if (!min_sdk_str.is_empty()) { // Empty means no override, nothing to do.
				if (!gradle_build_enabled) {
					return TTR("\"Min SDK\" can only be overridden when \"Use Gradle Build\" is enabled.");
				}
				if (!min_sdk_str.is_valid_int()) {
					return vformat(TTR("\"Min SDK\" should be a valid integer, but got \"%s\" which is invalid."), min_sdk_str);
				} else {
					int min_sdk_int = min_sdk_str.to_int();
					if (min_sdk_int < DEFAULT_MIN_SDK_VERSION) {
						return vformat(TTR("\"Min SDK\" cannot be lower than %d, which is the version needed by the Godot library."), DEFAULT_MIN_SDK_VERSION);
					}
				}
			}
		} else if (p_name == "gradle_build/target_sdk") {
			String target_sdk_str = p_preset->get("gradle_build/target_sdk");
			int target_sdk_int = DEFAULT_TARGET_SDK_VERSION;

			String min_sdk_str = p_preset->get("gradle_build/min_sdk");
			int min_sdk_int = VULKAN_MIN_SDK_VERSION;
			if (min_sdk_str.is_valid_int()) {
				min_sdk_int = min_sdk_str.to_int();
			}
			bool gradle_build_enabled = p_preset->get("gradle_build/use_gradle_build");
			if (!target_sdk_str.is_empty()) { // Empty means no override, nothing to do.
				if (!gradle_build_enabled) {
					return TTR("\"Target SDK\" can only be overridden when \"Use Gradle Build\" is enabled.");
				}
				if (!target_sdk_str.is_valid_int()) {
					return vformat(TTR("\"Target SDK\" should be a valid integer, but got \"%s\" which is invalid."), target_sdk_str);
				} else {
					target_sdk_int = target_sdk_str.to_int();
					if (target_sdk_int < min_sdk_int) {
						return TTR("\"Target SDK\" version must be greater or equal to \"Min SDK\" version.");
					}
				}
			}
		} else if (p_name == "gradle_build/custom_theme_attributes") {
			bool gradle_build_enabled = p_preset->get("gradle_build/use_gradle_build");
			if (bool(p_preset->get("gradle_build/custom_theme_attributes")) && !gradle_build_enabled) {
				return TTR("\"Use Gradle Build\" is required to add custom theme attributes.");
			}
		} else if (p_name == "package/show_in_android_tv") {
			bool gradle_build_enabled = p_preset->get("gradle_build/use_gradle_build");
			if (bool(p_preset->get("package/show_in_android_tv")) && !gradle_build_enabled) {
				return TTR("\"Use Gradle Build\" must be enabled to enable \"Show In Android Tv\".");
			}
		} else if (p_name == "package/show_as_launcher_app") {
			bool gradle_build_enabled = p_preset->get("gradle_build/use_gradle_build");
			if (bool(p_preset->get("package/show_as_launcher_app")) && !gradle_build_enabled) {
				return TTR("\"Use Gradle Build\" must be enabled to enable \"Show As Launcher App\".");
			}
		} else if (p_name == "package/show_in_app_library") {
			bool gradle_build_enabled = p_preset->get("gradle_build/use_gradle_build");
			if (!bool(p_preset->get("package/show_in_app_library")) && !gradle_build_enabled) {
				return TTR("\"Use Gradle Build\" must be enabled to disable \"Show In App Library\".");
			}
		} else if (p_name == "shader_baker/enabled" && bool(p_preset->get("shader_baker/enabled"))) {
			String export_renderer = GLOBAL_GET("rendering/renderer/rendering_method.mobile");
			if (OS::get_singleton()->get_current_rendering_method() == "gl_compatibility") {
				return TTR("\"Shader Baker\" is not supported when using the Compatibility renderer.");
			} else if (OS::get_singleton()->get_current_rendering_method() != export_renderer) {
				return vformat(TTR("The editor is currently using a different renderer than what the target platform will use. \"Shader Baker\" won't be able to include core shaders. Switch to the \"%s\" renderer temporarily to fix this."), export_renderer);
			}
		}
	}
	return String();
}

void EditorExportPlatformAndroid::get_export_options(List<ExportOption> *r_options) const {
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "custom_template/debug", PROPERTY_HINT_GLOBAL_FILE, "*.apk"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "custom_template/release", PROPERTY_HINT_GLOBAL_FILE, "*.apk"), ""));

	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "gradle_build/use_gradle_build"), false, true, false));
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "gradle_build/foundry_java/enabled"), false, true, false));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "gradle_build/foundry_java/gradle_plugin_maven", PROPERTY_HINT_PLACEHOLDER_TEXT, "group:artifact:version"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "gradle_build/foundry_java/gradle_plugin_local", PROPERTY_HINT_GLOBAL_FILE, "*.jar"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::PACKED_STRING_ARRAY, "gradle_build/foundry_java/maven_repositories"), PackedStringArray()));
	r_options->push_back(ExportOption(PropertyInfo(Variant::PACKED_STRING_ARRAY, "gradle_build/foundry_java/maven_artifacts"), PackedStringArray()));
	r_options->push_back(ExportOption(
			PropertyInfo(
					Variant::ARRAY,
					"gradle_build/foundry_java/local_artifacts",
					PROPERTY_HINT_ARRAY_TYPE,
					vformat("%d/%d:*.jar,*.aar", Variant::STRING, PROPERTY_HINT_GLOBAL_FILE)),
			Array()));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "gradle_build/gradle_build_directory", PROPERTY_HINT_PLACEHOLDER_TEXT, "res://android"), "", false, false));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "gradle_build/android_source_template", PROPERTY_HINT_GLOBAL_FILE, "*.zip"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "gradle_build/compress_native_libraries"), false, false, true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::INT, "gradle_build/export_format", PROPERTY_HINT_ENUM, "Export APK,Export AAB"), EXPORT_FORMAT_APK, false, true));
	// Using String instead of int to default to an empty string (no override) with placeholder for instructions (see GH-62465).
	// This implies doing validation that the string is a proper int.
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "gradle_build/min_sdk", PROPERTY_HINT_PLACEHOLDER_TEXT, vformat("%d (default)", VULKAN_MIN_SDK_VERSION)), "", false, true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "gradle_build/target_sdk", PROPERTY_HINT_PLACEHOLDER_TEXT, vformat("%d (default)", DEFAULT_TARGET_SDK_VERSION)), "", false, true));

	r_options->push_back(ExportOption(PropertyInfo(Variant::DICTIONARY, "gradle_build/custom_theme_attributes", PROPERTY_HINT_DICTIONARY_TYPE, "String;String"), Dictionary()));

	// Android supports multiple architectures in an app bundle, so
	// we expose each option as a checkbox in the export dialog.
	const Vector<ABI> abis = get_abis();
	for (int i = 0; i < abis.size(); ++i) {
		const String abi = abis[i].abi;
		// All Android devices supporting Vulkan run 64-bit Android,
		// so there is usually no point in exporting for 32-bit Android.
		const bool is_default = abi == "arm64-v8a";
		r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, vformat("%s/%s", PNAME("architectures"), abi)), is_default));
	}

	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "keystore/debug", PROPERTY_HINT_GLOBAL_FILE, "*.keystore,*.jks", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_SECRET), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "keystore/debug_user", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_SECRET), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "keystore/debug_password", PROPERTY_HINT_PASSWORD, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_SECRET), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "keystore/release", PROPERTY_HINT_GLOBAL_FILE, "*.keystore,*.jks", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_SECRET), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "keystore/release_user", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_SECRET), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "keystore/release_password", PROPERTY_HINT_PASSWORD, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_SECRET), ""));

	r_options->push_back(ExportOption(PropertyInfo(Variant::INT, "version/code", PROPERTY_HINT_RANGE, "1,4096,1,or_greater"), 1));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "version/name", PROPERTY_HINT_PLACEHOLDER_TEXT, "Leave empty to use project version"), ""));

	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "package/unique_name", PROPERTY_HINT_PLACEHOLDER_TEXT, "ext.domain.name"), "com.example.$genname", false, true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "package/name", PROPERTY_HINT_PLACEHOLDER_TEXT, "Game Name [default if blank]"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "package/signed"), true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::INT, "package/app_category", PROPERTY_HINT_ENUM, "Accessibility,Audio,Game,Image,Maps,News,Productivity,Social,Video,Undefined"), APP_CATEGORY_GAME));
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "package/retain_data_on_uninstall"), false));
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "package/exclude_from_recents"), false));
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "package/show_in_android_tv"), false));
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "package/show_in_app_library"), true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "package/show_as_launcher_app"), false));

	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, LAUNCHER_ICON_OPTION, PROPERTY_HINT_FILE, "*.png,*.webp,*.svg"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, LAUNCHER_ADAPTIVE_ICON_FOREGROUND_OPTION, PROPERTY_HINT_FILE, "*.png,*.webp,*.svg"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, LAUNCHER_ADAPTIVE_ICON_BACKGROUND_OPTION, PROPERTY_HINT_FILE, "*.png,*.webp,*.svg"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, LAUNCHER_ADAPTIVE_ICON_MONOCHROME_OPTION, PROPERTY_HINT_FILE, "*.png,*.webp,*.svg"), ""));

	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "graphics/opengl_debug"), false));

	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "shader_baker/enabled"), false));

#ifndef XR_DISABLED
	r_options->push_back(ExportOption(PropertyInfo(Variant::INT, "xr_features/xr_mode", PROPERTY_HINT_ENUM, "Regular,OpenXR"), XR_MODE_REGULAR, false, true));
#endif // XR_DISABLED

	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "gesture/swipe_to_dismiss"), false));

	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "screen/immersive_mode"), true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "screen/edge_to_edge"), false));
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "screen/support_small"), true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "screen/support_normal"), true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "screen/support_large"), true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "screen/support_xlarge"), true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::COLOR, "screen/background_color", PROPERTY_HINT_COLOR_NO_ALPHA), Color()));

	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "user_data_backup/allow"), false));

	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "command_line/extra_args", PROPERTY_HINT_NONE, "monospace"), ""));

	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "apk_expansion/enable"), false, false, true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "apk_expansion/SALT"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "apk_expansion/public_key", PROPERTY_HINT_MULTILINE_TEXT, "monospace,no_wrap"), "", false, true));

	r_options->push_back(ExportOption(PropertyInfo(Variant::PACKED_STRING_ARRAY, "permissions/custom_permissions"), PackedStringArray()));

	const char **perms = ANDROID_PERMS;
	while (*perms) {
		r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, vformat("%s/%s", PNAME("permissions"), String(*perms).to_lower())), false));
		perms++;
	}
}

bool EditorExportPlatformAndroid::get_export_option_visibility(const EditorExportPreset *p_preset, const String &p_option) const {
	if (p_preset == nullptr) {
		return true;
	}

	bool advanced_options_enabled = p_preset->are_advanced_options_enabled();
	// Keep the Foundry-Java toggle visible even when Gradle builds are disabled
	// so a stale enabled preset can be repaired. Export validation still rejects
	// that inconsistent state before any build work begins.
	if (p_option.begins_with("gradle_build/foundry_java/") && p_option != "gradle_build/foundry_java/enabled") {
		return bool(p_preset->get("gradle_build/foundry_java/enabled"));
	}
	if (p_option == "graphics/opengl_debug" ||
			p_option == "gradle_build/custom_theme_attributes" ||
			p_option == "command_line/extra_args" ||
			p_option == "permissions/custom_permissions" ||
			p_option == "keystore/debug" ||
			p_option == "keystore/debug_user" ||
			p_option == "keystore/debug_password" ||
			p_option == "package/retain_data_on_uninstall" ||
			p_option == "package/exclude_from_recents" ||
			p_option == "package/show_in_app_library" ||
			p_option == "package/show_as_launcher_app" ||
			p_option == "gesture/swipe_to_dismiss" ||
			p_option == "apk_expansion/enable" ||
			p_option == "apk_expansion/SALT" ||
			p_option == "apk_expansion/public_key") {
		return advanced_options_enabled;
	}
	if (p_option == "gradle_build/gradle_build_directory" || p_option == "gradle_build/android_source_template") {
		return advanced_options_enabled && bool(p_preset->get("gradle_build/use_gradle_build"));
	}
	if (p_option == "custom_template/debug" || p_option == "custom_template/release") {
		// The APK templates are ignored if Gradle build is enabled.
		return advanced_options_enabled && !bool(p_preset->get("gradle_build/use_gradle_build"));
	}

	// Hide .NET embedding option (always enabled).
	if (p_option == "dotnet/embed_build_outputs") {
		return false;
	}

	if (p_option == "dotnet/android_use_linux_bionic") {
		return advanced_options_enabled;
	}
	return true;
}

String EditorExportPlatformAndroid::get_name() const {
	return "Android";
}

String EditorExportPlatformAndroid::get_os_name() const {
	return "Android";
}

Ref<Texture2D> EditorExportPlatformAndroid::get_logo() const {
	return logo;
}

bool EditorExportPlatformAndroid::should_update_export_options() {
	return false;
}

bool EditorExportPlatformAndroid::poll_export() {
	bool dc = devices_changed.is_set();
	if (dc) {
		// don't clear unless we're reporting true, to avoid race
		devices_changed.clear();
	}
	return dc;
}

int EditorExportPlatformAndroid::get_options_count() const {
	MutexLock lock(device_lock);
	return devices.size() + 1;
}

Ref<Texture2D> EditorExportPlatformAndroid::get_option_icon(int p_index) const {
	if (p_index == 0) {
		Ref<Theme> theme = EditorNode::get_singleton()->get_editor_theme();
		ERR_FAIL_COND_V(theme.is_null(), Ref<ImageTexture>());
		return theme->get_icon(use_scrcpy ? SNAME("GuiChecked") : SNAME("GuiUnchecked"), EditorStringName(EditorIcons));
	}
	return EditorExportPlatform::get_option_icon(p_index - 1);
}

String EditorExportPlatformAndroid::get_options_tooltip() const {
	return TTR("Select device from the list");
}

String EditorExportPlatformAndroid::get_option_label(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, devices.size() + 1, "");
	if (p_index == 0) {
		return TTR("Mirror Android devices");
	}
	MutexLock lock(device_lock);
	return devices[p_index - 1].name;
}

String EditorExportPlatformAndroid::get_option_tooltip(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, devices.size() + 1, "");
	if (p_index == 0) {
		return TTR("If enabled, \"scrcpy\" is used to start the project and automatically stream device display (or virtual display) content.");
	}
	MutexLock lock(device_lock);
	String s = devices[p_index - 1].description;
	if (devices.size() == 1) {
		// Tooltip will be:
		// Name
		// Description
		s = devices[p_index - 1].name + "\n\n" + s;
	}
	return s;
}

String EditorExportPlatformAndroid::get_device_architecture(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, devices.size() + 1, "");
	if (p_index == 0) {
		return String();
	}
	MutexLock lock(device_lock);
	return devices[p_index - 1].architecture;
}

Error EditorExportPlatformAndroid::run(const Ref<EditorExportPreset> &p_preset, int p_device, BitField<EditorExportPlatform::DebugFlags> p_debug_flags) {
	ERR_FAIL_INDEX_V(p_device, devices.size() + 1, ERR_INVALID_PARAMETER);
	if (p_device == 0) {
		use_scrcpy = !use_scrcpy;
		EditorSettings::get_singleton()->set_project_metadata("android", "use_scrcpy", use_scrcpy);
		devices_changed.set();
		return ERR_SKIP;
	}

	String can_export_error;
	bool can_export_missing_templates;
	if (!can_export(p_preset, can_export_error, can_export_missing_templates)) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Run"), can_export_error);
		return ERR_UNCONFIGURED;
	}

	MutexLock lock(device_lock);

	EditorProgress ep("run", vformat(TTR("Running on %s"), devices[p_device - 1].name), 3);

	String adb = get_adb_path();

	// Export_temp APK.
	if (ep.step(TTR("Exporting APK..."), 0)) {
		return ERR_SKIP;
	}

	const bool use_wifi_for_remote_debug = EDITOR_GET("export/android/use_wifi_for_remote_debug");
	const bool use_remote = p_debug_flags.has_flag(DEBUG_FLAG_REMOTE_DEBUG) || p_debug_flags.has_flag(DEBUG_FLAG_DUMB_CLIENT);
	const bool use_reverse = !use_wifi_for_remote_debug;

	if (use_reverse) {
		p_debug_flags.set_flag(DEBUG_FLAG_REMOTE_DEBUG_LOCALHOST);
	}

	String tmp_export_path = EditorPaths::get_singleton()->get_temp_dir().path_join("tmpexport." + uitos(OS::get_singleton()->get_unix_time()) + ".apk");

#define CLEANUP_AND_RETURN(m_err)                                        \
	{                                                                    \
		DirAccess::remove_file_or_error(tmp_export_path);                \
		if (FileAccess::exists(tmp_export_path + ".idsig")) {            \
			DirAccess::remove_file_or_error(tmp_export_path + ".idsig"); \
		}                                                                \
		return m_err;                                                    \
	}                                                                    \
	((void)0)

	// Export to temporary APK before sending to device.
	Error err = export_project_helper(p_preset, true, tmp_export_path, EXPORT_FORMAT_APK, true, p_debug_flags);

	if (err != OK) {
		CLEANUP_AND_RETURN(err);
	}

	List<String> args;
	int rv;
	String output;

	bool remove_prev = EDITOR_GET("export/android/one_click_deploy_clear_previous_install");
	String version_name = p_preset->get_version("version/name");
	String package_name = p_preset->get("package/unique_name");

	if (remove_prev) {
		if (ep.step(TTR("Uninstalling..."), 1)) {
			CLEANUP_AND_RETURN(ERR_SKIP);
		}

		print_line("Uninstalling previous version: " + devices[p_device - 1].name);

		args.push_back("-s");
		args.push_back(devices[p_device - 1].id);
		args.push_back("uninstall");
		if ((bool)EDITOR_GET("export/android/force_system_user") && devices[p_device - 1].api_level >= 17) {
			args.push_back("--user");
			args.push_back("0");
		}
		args.push_back(get_package_name(p_preset, package_name));

		output.clear();
		err = OS::get_singleton()->execute(adb, args, &output, &rv, true);
		print_verbose(output);
	}

	print_line("Installing to device (please wait...): " + devices[p_device - 1].name);
	if (ep.step(TTR("Installing to device, please wait..."), 2)) {
		CLEANUP_AND_RETURN(ERR_SKIP);
	}

	args.clear();
	args.push_back("-s");
	args.push_back(devices[p_device - 1].id);
	args.push_back("install");
	if ((bool)EDITOR_GET("export/android/force_system_user") && devices[p_device - 1].api_level >= 17) {
		args.push_back("--user");
		args.push_back("0");
	}
	args.push_back("-r");
	args.push_back(tmp_export_path);

	output.clear();
	err = OS::get_singleton()->execute(adb, args, &output, &rv, true);
	print_verbose(output);
	if (err || rv != 0) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Run"), vformat(TTR("Could not install to device: %s"), output));
		CLEANUP_AND_RETURN(ERR_CANT_CREATE);
	}

	if (use_remote) {
		if (use_reverse) {
			static const char *const msg = "--- Debugging over USB ---";
			EditorNode::get_singleton()->get_log()->add_message(msg, EditorLog::MSG_TYPE_EDITOR);
			print_line(String(msg).to_upper());

			args.clear();
			args.push_back("-s");
			args.push_back(devices[p_device - 1].id);
			args.push_back("reverse");
			args.push_back("--remove-all");
			output.clear();
			OS::get_singleton()->execute(adb, args, &output, &rv, true);
			print_verbose(output);

			if (p_debug_flags.has_flag(DEBUG_FLAG_REMOTE_DEBUG)) {
				int dbg_port = EDITOR_GET("network/debug/remote_port");
				args.clear();
				args.push_back("-s");
				args.push_back(devices[p_device - 1].id);
				args.push_back("reverse");
				args.push_back("tcp:" + itos(dbg_port));
				args.push_back("tcp:" + itos(dbg_port));

				output.clear();
				OS::get_singleton()->execute(adb, args, &output, &rv, true);
				print_verbose(output);
				print_line("Reverse result: " + itos(rv));
			}

			if (p_debug_flags.has_flag(DEBUG_FLAG_DUMB_CLIENT)) {
				int fs_port = EDITOR_GET("filesystem/file_server/port");

				args.clear();
				args.push_back("-s");
				args.push_back(devices[p_device - 1].id);
				args.push_back("reverse");
				args.push_back("tcp:" + itos(fs_port));
				args.push_back("tcp:" + itos(fs_port));

				output.clear();
				err = OS::get_singleton()->execute(adb, args, &output, &rv, true);
				print_verbose(output);
				print_line("Reverse result2: " + itos(rv));
			}
		} else {
			static const char *const api_version_msg = "--- Debugging over Wi-Fi ---";
			static const char *const manual_override_msg = "--- Wi-Fi remote debug enabled in project settings; debugging over Wi-Fi ---";

			const char *const msg = use_wifi_for_remote_debug ? manual_override_msg : api_version_msg;
			EditorNode::get_singleton()->get_log()->add_message(msg, EditorLog::MSG_TYPE_EDITOR);
			print_line(String(msg).to_upper());
		}
	}

	if (ep.step(TTR("Running on device..."), 3)) {
		CLEANUP_AND_RETURN(ERR_SKIP);
	}

	String scrcpy = "scrcpy";
	if (!EDITOR_GET("export/android/scrcpy/path").operator String().is_empty()) {
		scrcpy = EDITOR_GET("export/android/scrcpy/path").operator String();
	}

	args.clear();
	if (use_scrcpy) {
		args.push_back("-s");
		args.push_back(devices[p_device - 1].id);
		if (EDITOR_GET("export/android/scrcpy/virtual_display").operator bool()) {
			args.push_back("--new-display=" + EDITOR_GET("export/android/scrcpy/screen_size").operator String());
			if (EDITOR_GET("export/android/scrcpy/local_ime").operator bool()) {
				args.push_back("--display-ime-policy=local");
			}
			if (EDITOR_GET("export/android/scrcpy/no_decorations").operator bool()) {
				args.push_back("--no-vd-system-decorations");
			}
		}
		args.push_back("--start-app=+" + get_package_name(p_preset, package_name));

		Dictionary data = OS::get_singleton()->execute_with_pipe(scrcpy, args, false);
		if (!data.has("pid") || data["pid"].operator int() <= 0) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Run"), TTR("Could not start scrcpy executable. Configure scrcpy path in the Editor Settings (Export > Android > scrcpy > Path)."));
			CLEANUP_AND_RETURN(ERR_CANT_CREATE);
		}
		bool connected = false;
		uint64_t wait = 3000000;
		uint64_t time = OS::get_singleton()->get_ticks_usec();
		output.clear();
		String err_output;
		Ref<FileAccess> fa_out = data["stdio"];
		Ref<FileAccess> fa_err = data["stderr"];
		while (fa_out->is_open() && fa_err->is_open() && OS::get_singleton()->get_ticks_usec() - time < wait) {
			PackedByteArray buf;

			buf.resize(fa_out->get_length());
			uint64_t size = fa_out->get_buffer(buf.ptrw(), buf.size());
			output.append_utf8((const char *)buf.ptr(), size);

			buf.resize(fa_err->get_length());
			size = fa_err->get_buffer(buf.ptrw(), buf.size());
			err_output.append_utf8((const char *)buf.ptr(), size);

			if (output.contains("[server] INFO: Device:")) {
				connected = true;
				break;
			}
		}
		print_verbose(output);
		print_verbose(err_output);
		if (!connected) {
			OS::get_singleton()->kill(data["pid"].operator int());
			add_message(EXPORT_MESSAGE_ERROR, TTR("Run"), TTR("Could not execute on device, scrcpy failed with the following error:\n" + err_output));
			CLEANUP_AND_RETURN(ERR_CANT_CREATE);
		}
	} else {
		args.push_back("-s");
		args.push_back(devices[p_device - 1].id);
		args.push_back("shell");
		args.push_back("am");
		args.push_back("start");
		if ((bool)EDITOR_GET("export/android/force_system_user") && devices[p_device - 1].api_level >= 17) {
			args.push_back("--user");
			args.push_back("0");
		}
		args.push_back("-a");
		args.push_back("android.intent.action.MAIN");

		// Going with implicit launch first based on the LAUNCHER category and the app's package.
		args.push_back("-c");
		args.push_back("android.intent.category.LAUNCHER");
		args.push_back(get_package_name(p_preset, package_name));

		output.clear();
		err = OS::get_singleton()->execute(adb, args, &output, &rv, true);
		print_verbose(output);
		if (err || rv != 0 || output.contains("Error: Activity not started")) {
			// The implicit launch failed, let's try an explicit launch by specifying the component name before giving up.
			const String component_name = get_package_name(p_preset, package_name) + "/games.cafecito.foundry.game.FoundryAppLauncher";
			print_line("Implicit launch failed... Trying explicit launch using", component_name);
			args.erase(get_package_name(p_preset, package_name));
			args.push_back("-n");
			args.push_back(component_name);

			output.clear();
			err = OS::get_singleton()->execute(adb, args, &output, &rv, true);
			print_verbose(output);

			if (err || rv != 0 || output.begins_with("Error: Activity not started")) {
				add_message(EXPORT_MESSAGE_ERROR, TTR("Run"), TTR("Could not execute on device."));
				CLEANUP_AND_RETURN(ERR_CANT_CREATE);
			}
		}
	}

	CLEANUP_AND_RETURN(OK);
#undef CLEANUP_AND_RETURN
}

Ref<Texture2D> EditorExportPlatformAndroid::get_run_icon() const {
	return run_icon;
}

String EditorExportPlatformAndroid::get_java_path() {
	String exe_ext;
	if (OS::get_singleton()->get_name() == "Windows") {
		exe_ext = ".exe";
	}
	String java_sdk_path = EDITOR_GET("export/android/java_sdk_path");
	return java_sdk_path.path_join("bin/java" + exe_ext);
}

String EditorExportPlatformAndroid::get_keytool_path() {
	String exe_ext;
	if (OS::get_singleton()->get_name() == "Windows") {
		exe_ext = ".exe";
	}
	String java_sdk_path = EDITOR_GET("export/android/java_sdk_path");
	return java_sdk_path.path_join("bin/keytool" + exe_ext);
}

String EditorExportPlatformAndroid::get_adb_path() {
	String exe_ext;
	if (OS::get_singleton()->get_name() == "Windows") {
		exe_ext = ".exe";
	}
	String sdk_path = EDITOR_GET("export/android/android_sdk_path");
	return sdk_path.path_join("platform-tools/adb" + exe_ext);
}

String EditorExportPlatformAndroid::get_apksigner_path(int p_target_sdk, bool p_check_executes) {
	if (p_target_sdk == -1) {
		p_target_sdk = DEFAULT_TARGET_SDK_VERSION;
	}
	String exe_ext;
	if (OS::get_singleton()->get_name() == "Windows") {
		exe_ext = ".bat";
	}
	String apksigner_command_name = "apksigner" + exe_ext;
	String sdk_path = EDITOR_GET("export/android/android_sdk_path");
	String apksigner_path;

	Error errn;
	String build_tools_dir = sdk_path.path_join("build-tools");
	Ref<DirAccess> da = DirAccess::open(build_tools_dir, &errn);
	if (errn != OK) {
		print_error("Unable to open Android 'build-tools' directory.");
		return apksigner_path;
	}

	// There are additional versions directories we need to go through.
	Vector<String> dir_list = da->get_directories();

	// We need to use the version of build_tools that matches the Target SDK
	// If somehow we can't find that, we see if a version between 28 and the default target SDK exists.
	// We need to avoid versions <= 27 because they fail on Java versions >9
	// If we can't find that, we just use the first valid version.
	Vector<String> ideal_versions;
	Vector<String> other_versions;
	Vector<String> versions;
	bool found_target_sdk = false;
	// We only allow for versions <= 27 if specifically set
	int min_version = p_target_sdk <= 27 ? p_target_sdk : 28;
	for (String sub_dir : dir_list) {
		if (!sub_dir.begins_with(".")) {
			Vector<String> ver_numbers = sub_dir.split(".");
			// Dir not a version number, will use as last resort
			if (!ver_numbers.size() || !ver_numbers[0].is_valid_int()) {
				other_versions.push_back(sub_dir);
				continue;
			}
			int ver_number = ver_numbers[0].to_int();
			if (ver_number == p_target_sdk) {
				found_target_sdk = true;
				//ensure this is in front of the ones we check
				versions.push_back(sub_dir);
			} else {
				if (ver_number >= min_version && ver_number <= DEFAULT_TARGET_SDK_VERSION) {
					ideal_versions.push_back(sub_dir);
				} else {
					other_versions.push_back(sub_dir);
				}
			}
		}
	}
	// we will check ideal versions first, then other versions.
	versions.append_array(ideal_versions);
	versions.append_array(other_versions);

	if (!versions.size()) {
		print_error("Unable to find the 'apksigner' tool.");
		return apksigner_path;
	}

	int i;
	bool failed = false;
	String version_to_use;

	String java_sdk_path = EDITOR_GET("export/android/java_sdk_path");
	if (!java_sdk_path.is_empty()) {
		OS::get_singleton()->set_environment("JAVA_HOME", java_sdk_path);

#ifdef UNIX_ENABLED
		String env_path = OS::get_singleton()->get_environment("PATH");
		if (!env_path.contains(java_sdk_path)) {
			OS::get_singleton()->set_environment("PATH", java_sdk_path + "/bin:" + env_path);
		}
#endif
	}

	List<String> args;
	args.push_back("--version");
	String output;
	int retval;
	Error err;
	for (i = 0; i < versions.size(); i++) {
		// Check if the tool is here.
		apksigner_path = build_tools_dir.path_join(versions[i]).path_join(apksigner_command_name);
		if (FileAccess::exists(apksigner_path)) {
			version_to_use = versions[i];
			// If we aren't exporting, just break here.
			if (!p_check_executes) {
				break;
			}
			// we only check to see if it executes on export because it is slow to load
			err = OS::get_singleton()->execute(apksigner_path, args, &output, &retval, false);
			if (err || retval) {
				failed = true;
			} else {
				break;
			}
		}
	}
	if (i == versions.size()) {
		if (failed) {
			print_error("All located 'apksigner' tools in " + build_tools_dir + " failed to execute");
			return "<FAILED>";
		} else {
			print_error("Unable to find the 'apksigner' tool.");
			return "";
		}
	}
	if (!found_target_sdk) {
		print_line("Could not find version of build tools that matches Target SDK, using " + version_to_use);
	} else if (failed && found_target_sdk) {
		print_line("Version of build tools that matches Target SDK failed to execute, using " + version_to_use);
	}

	return apksigner_path;
}

static bool has_valid_keystore_credentials(String &r_error_str, const String &p_keystore, const String &p_username, const String &p_password, const String &p_type) {
	String output;
	List<String> args;
	args.push_back("-list");
	args.push_back("-keystore");
	args.push_back(p_keystore);
	args.push_back("-storepass");
	args.push_back(p_password);
	args.push_back("-alias");
	args.push_back(p_username);
	String keytool_path = EditorExportPlatformAndroid::get_keytool_path();
	Error error = OS::get_singleton()->execute(keytool_path, args, &output, nullptr, true);
	String keytool_error = "keytool error:";
	bool valid = output.substr(0, keytool_error.length()) != keytool_error;

	if (error != OK) {
		r_error_str = TTR("Error: There was a problem validating the keystore username and password");
		return false;
	}
	if (!valid) {
		r_error_str = TTR(p_type + " Username and/or Password is invalid for the given " + p_type + " Keystore");
		return false;
	}
	r_error_str = "";
	return true;
}

bool EditorExportPlatformAndroid::has_valid_username_and_password(const Ref<EditorExportPreset> &p_preset, String &r_error) {
	String dk = _get_keystore_path(p_preset, true);
	String dk_user = p_preset->get_or_env("keystore/debug_user", ENV_ANDROID_KEYSTORE_DEBUG_USER);
	String dk_password = p_preset->get_or_env("keystore/debug_password", ENV_ANDROID_KEYSTORE_DEBUG_PASS);
	String rk = _get_keystore_path(p_preset, false);
	String rk_user = p_preset->get_or_env("keystore/release_user", ENV_ANDROID_KEYSTORE_RELEASE_USER);
	String rk_password = p_preset->get_or_env("keystore/release_password", ENV_ANDROID_KEYSTORE_RELEASE_PASS);

	bool valid = true;
	if (!dk.is_empty() && !dk_user.is_empty() && !dk_password.is_empty()) {
		String err = "";
		valid = has_valid_keystore_credentials(err, dk, dk_user, dk_password, "Debug");
		r_error += err;
	}
	if (!rk.is_empty() && !rk_user.is_empty() && !rk_password.is_empty()) {
		String err = "";
		valid = has_valid_keystore_credentials(err, rk, rk_user, rk_password, "Release");
		r_error += err;
	}
	return valid;
}

#ifdef MODULE_MONO_ENABLED
static uint64_t _last_validate_tfm_time = 0;
static String _last_validate_tfm = "";

bool _validate_dotnet_tfm(const String &required_tfm, String &r_error) {
	String assembly_name = Path::get_csharp_project_name();
	String project_path = ProjectSettings::get_singleton()->globalize_path("res://" + assembly_name + ".csproj");

	if (!FileAccess::exists(project_path)) {
		return true;
	}

	uint64_t modified_time = FileAccess::get_modified_time(project_path);
	String tfm;

	if (modified_time == _last_validate_tfm_time) {
		tfm = _last_validate_tfm;
	} else {
		String pipe;
		List<String> args;
		args.push_back("build");
		args.push_back(project_path);
		args.push_back("/p:FoundryTargetPlatform=android");
		args.push_back("--getProperty:TargetFramework");

		int exitcode;
		Error err = OS::get_singleton()->execute("dotnet", args, &pipe, &exitcode, true);
		if (err != OK || exitcode != 0) {
			if (err != OK) {
				WARN_PRINT("Failed to execute dotnet command. Error " + String(error_names[err]));
			} else if (exitcode != 0) {
				print_line(pipe);
				WARN_PRINT("dotnet command exited with code " + itos(exitcode) + ". See output above for more details.");
			}
			r_error += vformat(TTR("Unable to determine the C# project's TFM, it may be incompatible. The export template only supports '%s'. Make sure the project targets '%s' or consider using gradle builds instead."), required_tfm, required_tfm) + "\n";
			return true;
		} else {
			tfm = pipe.strip_edges();
			_last_validate_tfm_time = modified_time;
			_last_validate_tfm = tfm;
		}
	}

	if (tfm != required_tfm) {
		r_error += vformat(TTR("C# project targets '%s' but the export template only supports '%s'. Consider using gradle builds instead."), tfm, required_tfm) + "\n";
		return false;
	}

	return true;
}
#endif

bool EditorExportPlatformAndroid::has_valid_export_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error, bool &r_missing_templates, bool p_debug) const {
	String err;
	bool valid = false;
	const bool gradle_build_enabled = p_preset->get("gradle_build/use_gradle_build");

#ifdef MODULE_MONO_ENABLED
	// Android export is still a work in progress, keep a message as a warning.
	err += TTR("Exporting to Android when using C#/.NET is experimental.") + "\n";

	if (!gradle_build_enabled) {
		// For template exports we only support .NET 9 because the template
		// includes .jar dependencies that may only be compatible with .NET 9.
		if (!_validate_dotnet_tfm("net9.0", err)) {
			r_error = err;
			return false;
		}
	}
#endif

	// Look for export templates (first official, and if defined custom templates).

	if (!gradle_build_enabled) {
		String template_err;
		bool dvalid = false;
		bool rvalid = false;
		bool has_export_templates = false;

		if (p_preset->get("custom_template/debug") != "") {
			dvalid = FileAccess::exists(p_preset->get("custom_template/debug"));
			if (!dvalid) {
				template_err += TTR("Custom debug template not found.") + "\n";
			}
			has_export_templates |= dvalid;
		} else {
			has_export_templates |= exists_export_template("android_debug.apk", &template_err);
		}

		if (p_preset->get("custom_template/release") != "") {
			rvalid = FileAccess::exists(p_preset->get("custom_template/release"));
			if (!rvalid) {
				template_err += TTR("Custom release template not found.") + "\n";
			}
			has_export_templates |= rvalid;
		} else {
			has_export_templates |= exists_export_template("android_release.apk", &template_err);
		}

		r_missing_templates = !has_export_templates;
		valid = dvalid || rvalid || has_export_templates;
		if (!valid) {
			err += template_err;
		}
	} else {
		// Validate the custom gradle android source template.
		bool android_source_template_valid = false;
		const String android_source_template = p_preset->get("gradle_build/android_source_template");
		if (!android_source_template.is_empty()) {
			android_source_template_valid = FileAccess::exists(android_source_template);
			if (!android_source_template_valid) {
				err += TTR("Custom Android source template not found.") + "\n";
			}
		}

		// Validate the installed build template.
		bool installed_android_build_template = FileAccess::exists(ExportTemplateManager::get_android_build_directory(p_preset).path_join("build.gradle"));
		if (!installed_android_build_template) {
			if (!android_source_template_valid) {
				r_missing_templates = !exists_export_template("android_source.zip", &err);
			}
			err += TTR("Android build template not installed in the project. Install it from the Project menu.") + "\n";
		} else {
			r_missing_templates = false;
		}

		valid = installed_android_build_template && !r_missing_templates;
	}

	// Validate the rest of the export configuration.

	if (p_debug) {
		String dk = _get_keystore_path(p_preset, true);
		String dk_user = p_preset->get_or_env("keystore/debug_user", ENV_ANDROID_KEYSTORE_DEBUG_USER);
		String dk_password = p_preset->get_or_env("keystore/debug_password", ENV_ANDROID_KEYSTORE_DEBUG_PASS);

		if ((dk.is_empty() || dk_user.is_empty() || dk_password.is_empty()) && (!dk.is_empty() || !dk_user.is_empty() || !dk_password.is_empty())) {
			valid = false;
			err += TTR("Either Debug Keystore, Debug User AND Debug Password settings must be configured OR none of them.") + "\n";
		}

		// Use OR to make the export UI able to show this error.
		if (!dk.is_empty() && !FileAccess::exists(dk)) {
			dk = EDITOR_GET("export/android/debug_keystore");
			if (!FileAccess::exists(dk)) {
				valid = false;
				err += TTR("Debug keystore not configured in the Editor Settings nor in the preset.") + "\n";
			}
		}
	} else {
		String rk = _get_keystore_path(p_preset, false);
		String rk_user = p_preset->get_or_env("keystore/release_user", ENV_ANDROID_KEYSTORE_RELEASE_USER);
		String rk_password = p_preset->get_or_env("keystore/release_password", ENV_ANDROID_KEYSTORE_RELEASE_PASS);

		if ((rk.is_empty() || rk_user.is_empty() || rk_password.is_empty()) && (!rk.is_empty() || !rk_user.is_empty() || !rk_password.is_empty())) {
			valid = false;
			err += TTR("Either Release Keystore, Release User AND Release Password settings must be configured OR none of them.") + "\n";
		}

		if (!rk.is_empty() && !FileAccess::exists(rk)) {
			valid = false;
			err += TTR("Release keystore incorrectly configured in the export preset.") + "\n";
		}
	}

	String java_sdk_path = EDITOR_GET("export/android/java_sdk_path");
	if (java_sdk_path.is_empty()) {
		err += TTR("A valid Java SDK path is required in Editor Settings.") + "\n";
		valid = false;
	} else {
		// Validate the given path by checking that `java` is present under the `bin` directory.
		Error errn;
		// Check for the bin directory.
		Ref<DirAccess> da = DirAccess::open(java_sdk_path.path_join("bin"), &errn);
		if (errn != OK) {
			err += TTR("Invalid Java SDK path in Editor Settings.") + " ";
			err += TTR("Missing 'bin' directory!");
			err += "\n";
			valid = false;
		} else {
			// Check for the `java` command.
			String java_path = get_java_path();
			if (!FileAccess::exists(java_path)) {
				err += TTR("Unable to find 'java' command using the Java SDK path.") + " ";
				err += TTR("Please check the Java SDK directory specified in Editor Settings.");
				err += "\n";
				valid = false;
			}
		}
	}

	String sdk_path = EDITOR_GET("export/android/android_sdk_path");
	if (sdk_path.is_empty()) {
		err += TTR("A valid Android SDK path is required in Editor Settings.") + "\n";
		valid = false;
	} else {
		Error errn;
		// Check for the platform-tools directory.
		Ref<DirAccess> da = DirAccess::open(sdk_path.path_join("platform-tools"), &errn);
		if (errn != OK) {
			err += TTR("Invalid Android SDK path in Editor Settings.") + " ";
			err += TTR("Missing 'platform-tools' directory!");
			err += "\n";
			valid = false;
		}

		// Validate that adb is available.
		String adb_path = get_adb_path();
		if (!FileAccess::exists(adb_path)) {
			err += TTR("Unable to find Android SDK platform-tools' adb command.") + " ";
			err += TTR("Please check in the Android SDK directory specified in Editor Settings.");
			err += "\n";
			valid = false;
		}

		// Check for the build-tools directory.
		Ref<DirAccess> build_tools_da = DirAccess::open(sdk_path.path_join("build-tools"), &errn);
		if (errn != OK) {
			err += TTR("Invalid Android SDK path in Editor Settings.") + " ";
			err += TTR("Missing 'build-tools' directory!");
			err += "\n";
			valid = false;
		}

		String target_sdk_version = p_preset->get("gradle_build/target_sdk");
		if (!target_sdk_version.is_valid_int()) {
			target_sdk_version = itos(DEFAULT_TARGET_SDK_VERSION);
		}
		// Validate that apksigner is available.
		String apksigner_path = get_apksigner_path(target_sdk_version.to_int());
		if (!FileAccess::exists(apksigner_path)) {
			err += TTR("Unable to find Android SDK build-tools' apksigner command.") + " ";
			err += TTR("Please check in the Android SDK directory specified in Editor Settings.");
			err += "\n";
			valid = false;
		}
	}
	if (!err.is_empty()) {
		r_error = err;
	}

	return valid;
}

bool EditorExportPlatformAndroid::has_valid_project_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error) const {
	String err;
	bool valid = true;
	FoundryJavaExportConfig foundry_java;
	String foundry_java_error;
	if (_get_foundry_java_export_config(p_preset.ptr(), foundry_java, foundry_java_error) != OK) {
		err += foundry_java_error + "\n";
		valid = false;
	} else if (foundry_java.enabled && get_enabled_abis(p_preset).is_empty()) {
		err += TTR("Foundry-Java requires at least one enabled Android architecture in the export preset.") + "\n";
		valid = false;
	}

	List<ExportOption> options;
	get_export_options(&options);
	for (const EditorExportPlatform::ExportOption &E : options) {
		if (String(E.option.name).begins_with("gradle_build/foundry_java/")) {
			continue;
		}
		if (get_export_option_visibility(p_preset.ptr(), E.option.name)) {
			String warn = get_export_option_warning(p_preset.ptr(), E.option.name);
			if (!warn.is_empty()) {
				err += warn + "\n";
				if (E.required) {
					valid = false;
				}
			}
		}
	}

	if (!ResourceImporterTextureSettings::should_import_etc2_astc()) {
		valid = false;
		if (EditorNode::is_cmdline_mode()) {
			err += TTR("ETC2/ASTC texture compression is required for Android export. In Project Settings, search for 'ETC2' in the search field, or enable 'Advanced Settings' and go to Rendering > Textures > VRAM Compression to enable 'Import ETC2 ASTC'.") + "\n";
		}
	}

	bool gradle_build_enabled = p_preset->get("gradle_build/use_gradle_build");
	if (gradle_build_enabled) {
		String build_version_path = ExportTemplateManager::get_android_build_directory(p_preset).get_base_dir().path_join(".build_version");
		Ref<FileAccess> f = FileAccess::open(build_version_path, FileAccess::READ);
		if (f.is_valid()) {
			String current_version = ExportTemplateManager::get_android_template_identifier(p_preset);
			String installed_version = f->get_line().strip_edges();
			if (current_version != installed_version) {
				err += vformat(TTR(MISMATCHED_VERSIONS_MESSAGE), installed_version, current_version);
				err += "\n";
			}
		}
	} else {
		if (_is_transparency_allowed(p_preset)) {
			// Warning only, so don't override `valid`.
			err += vformat(TTR("\"Use Gradle Build\" is required for transparent background on Android"));
			err += "\n";
		}
	}

	String target_sdk_str = p_preset->get("gradle_build/target_sdk");
	int target_sdk_int = DEFAULT_TARGET_SDK_VERSION;
	if (!target_sdk_str.is_empty()) { // Empty means no override, nothing to do.
		if (target_sdk_str.is_valid_int()) {
			target_sdk_int = target_sdk_str.to_int();
			if (target_sdk_int > DEFAULT_TARGET_SDK_VERSION) {
				// Warning only, so don't override `valid`.
				err += vformat(TTR("\"Target SDK\" %d is higher than the default version %d. This may work, but wasn't tested and may be unstable."), target_sdk_int, DEFAULT_TARGET_SDK_VERSION);
				err += "\n";
			}
		}
	}

	String current_renderer = get_project_setting(p_preset, "rendering/renderer/rendering_method.mobile");
	if (current_renderer == "forward_plus") {
		// Warning only, so don't override `valid`.
		err += vformat(TTR("The \"%s\" renderer is designed for Desktop devices, and is not suitable for Android devices."), current_renderer);
		err += "\n";
	}

	String min_sdk_str = p_preset->get("gradle_build/min_sdk");
	int min_sdk_int = VULKAN_MIN_SDK_VERSION;
	if (!min_sdk_str.is_empty()) { // Empty means no override, nothing to do.
		if (min_sdk_str.is_valid_int()) {
			min_sdk_int = min_sdk_str.to_int();
		}
	}
	bool fallback_to_opengl3 = GLOBAL_GET("rendering/rendering_device/fallback_to_opengl3");
	if (_uses_vulkan(p_preset) && min_sdk_int < VULKAN_MIN_SDK_VERSION && !fallback_to_opengl3) {
		// Warning only, so don't override `valid`.
		err += vformat(TTR("\"Min SDK\" should be greater or equal to %d for the \"%s\" renderer."), VULKAN_MIN_SDK_VERSION, current_renderer);
		err += "\n";
	}

	String package_name = p_preset->get("package/unique_name");
	if (package_name.contains("$genname") && !is_project_name_valid(p_preset)) {
		// Warning only, so don't override `valid`.
		err += vformat(TTR("The project name does not meet the requirement for the package name format and will be updated to \"%s\". Please explicitly specify the package name if needed."), get_valid_basename(p_preset));
		err += "\n";
	}

	r_error = err;
	return valid;
}

List<String> EditorExportPlatformAndroid::get_binary_extensions(const Ref<EditorExportPreset> &p_preset) const {
	List<String> list;
	int export_format = int(p_preset->get("gradle_build/export_format"));
	if (export_format == EXPORT_FORMAT_AAB) {
		list.push_back("aab");
	} else {
		list.push_back("apk");
	}
	return list;
}

String EditorExportPlatformAndroid::get_apk_expansion_fullpath(const Ref<EditorExportPreset> &p_preset, const String &p_path) {
	int version_code = p_preset->get("version/code");
	String package_name = p_preset->get("package/unique_name");
	String apk_file_name = "main." + itos(version_code) + "." + get_package_name(p_preset, package_name) + ".obb";
	String fullpath = p_path.get_base_dir().path_join(apk_file_name);
	return fullpath;
}

Error EditorExportPlatformAndroid::save_apk_expansion_file(const Ref<EditorExportPreset> &p_preset, bool p_debug, const String &p_path) {
	String fullpath = get_apk_expansion_fullpath(p_preset, p_path);
	Error err = save_pack(p_preset, p_debug, fullpath);
	return err;
}

void EditorExportPlatformAndroid::get_command_line_flags(const Ref<EditorExportPreset> &p_preset, const String &p_path, BitField<EditorExportPlatform::DebugFlags> p_flags, Vector<uint8_t> &r_command_line_flags) {
	String cmdline = p_preset->get("command_line/extra_args");
	Vector<String> command_line_strings = cmdline.strip_edges().split(" ");
	for (int i = 0; i < command_line_strings.size(); i++) {
		if (command_line_strings[i].strip_edges().length() == 0) {
			command_line_strings.remove_at(i);
			i--;
		}
	}

	command_line_strings.append_array(gen_export_flags(p_flags));

	bool apk_expansion = p_preset->get("apk_expansion/enable");
	if (apk_expansion) {
		String fullpath = get_apk_expansion_fullpath(p_preset, p_path);
		String apk_expansion_public_key = p_preset->get("apk_expansion/public_key");

		command_line_strings.push_back("--use_apk_expansion");
		command_line_strings.push_back("--apk_expansion_md5");
		command_line_strings.push_back(FileAccess::get_md5(fullpath));
		command_line_strings.push_back("--apk_expansion_key");
		command_line_strings.push_back(apk_expansion_public_key.strip_edges());
	}

#ifndef XR_DISABLED
	int xr_mode_index = p_preset->get("xr_features/xr_mode");
	if (xr_mode_index == XR_MODE_OPENXR) {
		command_line_strings.push_back("--xr_mode_openxr");
	} else { // XRMode.REGULAR is the default.
		command_line_strings.push_back("--xr_mode_regular");

		// Also override the 'xr/openxr/enabled' project setting.
		// This is useful for multi-platforms projects supporting both XR and non-XR devices. The project would need
		// to enable openxr for development, and would create multiple XR and non-XR export presets.
		// These command line args ensure that the non-XR export presets will have openxr disabled.
		command_line_strings.push_back("--xr-mode");
		command_line_strings.push_back("off");
	}
#endif // XR_DISABLED

	bool immersive = p_preset->get("screen/immersive_mode");
	if (immersive) {
		command_line_strings.push_back("--fullscreen");
	}

	bool edge_to_edge = p_preset->get("screen/edge_to_edge");
	if (edge_to_edge) {
		command_line_strings.push_back("--edge_to_edge");
	}

	String background_color = "#" + p_preset->get("screen/background_color").operator Color().to_html(false);

	// For Gradle build, _fix_themes_xml() sets background to transparent if _is_transparency_allowed().
	// Overriding to transparent here too as it's used as fallback for system bar appearance.
	if (_is_transparency_allowed(p_preset) && p_preset->get("gradle_build/use_gradle_build")) {
		background_color = "#00000000";
	}
	command_line_strings.push_back("--background_color");
	command_line_strings.push_back(background_color);

	bool debug_opengl = p_preset->get("graphics/opengl_debug");
	if (debug_opengl) {
		command_line_strings.push_back("--debug_opengl");
	}

	if (command_line_strings.size()) {
		r_command_line_flags.resize(4);
		encode_uint32(command_line_strings.size(), &r_command_line_flags.write[0]);
		for (int i = 0; i < command_line_strings.size(); i++) {
			print_line(itos(i) + " param: " + command_line_strings[i]);
			CharString command_line_argument = command_line_strings[i].utf8();
			int base = r_command_line_flags.size();
			int length = command_line_argument.length();
			if (length == 0) {
				continue;
			}
			r_command_line_flags.resize(base + 4 + length);
			encode_uint32(length, &r_command_line_flags.write[base]);
			memcpy(&r_command_line_flags.write[base + 4], command_line_argument.ptr(), length);
		}
	}
}

Error EditorExportPlatformAndroid::sign_apk(const Ref<EditorExportPreset> &p_preset, bool p_debug, const String &export_path, EditorProgress &ep) {
	int export_format = int(p_preset->get("gradle_build/export_format"));
	if (export_format == EXPORT_FORMAT_AAB) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Code Signing"), TTR("AAB signing is not supported"));
		return FAILED;
	}

	String keystore;
	String password;
	String user;
	if (p_debug) {
		keystore = _get_keystore_path(p_preset, true);
		password = p_preset->get_or_env("keystore/debug_password", ENV_ANDROID_KEYSTORE_DEBUG_PASS);
		user = p_preset->get_or_env("keystore/debug_user", ENV_ANDROID_KEYSTORE_DEBUG_USER);

		if (keystore.is_empty()) {
			keystore = EDITOR_GET("export/android/debug_keystore");
			password = EDITOR_GET("export/android/debug_keystore_pass");
			user = EDITOR_GET("export/android/debug_keystore_user");
		}

		if (ep.step(TTR("Signing debug APK..."), 104)) {
			return ERR_SKIP;
		}
	} else {
		keystore = _get_keystore_path(p_preset, false);
		password = p_preset->get_or_env("keystore/release_password", ENV_ANDROID_KEYSTORE_RELEASE_PASS);
		user = p_preset->get_or_env("keystore/release_user", ENV_ANDROID_KEYSTORE_RELEASE_USER);

		if (ep.step(TTR("Signing release APK..."), 104)) {
			return ERR_SKIP;
		}
	}

	if (!FileAccess::exists(keystore)) {
		if (p_debug) {
			add_message(EXPORT_MESSAGE_WARNING, TTR("Code Signing"), TTR("Could not find debug keystore, unable to export."));
		} else {
			add_message(EXPORT_MESSAGE_WARNING, TTR("Code Signing"), TTR("Could not find release keystore, unable to export."));
		}
		return ERR_FILE_CANT_OPEN;
	}

	String apk_path = export_path;
	if (apk_path.is_relative_path()) {
		apk_path = OS::get_singleton()->get_resource_dir().path_join(apk_path);
	}
	apk_path = ProjectSettings::get_singleton()->globalize_path(apk_path).simplify_path();

	Error err;
	String target_sdk_version = p_preset->get("gradle_build/target_sdk");
	if (!target_sdk_version.is_valid_int()) {
		target_sdk_version = itos(DEFAULT_TARGET_SDK_VERSION);
	}

	String apksigner = get_apksigner_path(target_sdk_version.to_int(), true);
	print_verbose("Starting signing of the APK binary using " + apksigner);
	if (apksigner == "<FAILED>") {
		add_message(EXPORT_MESSAGE_WARNING, TTR("Code Signing"), TTR("All 'apksigner' tools located in Android SDK 'build-tools' directory failed to execute. Please check that you have the correct version installed for your target sdk version. The resulting APK is unsigned."));
		return OK;
	}
	if (!FileAccess::exists(apksigner)) {
		add_message(EXPORT_MESSAGE_WARNING, TTR("Code Signing"), TTR("'apksigner' could not be found. Please check that the command is available in the Android SDK build-tools directory. The resulting APK is unsigned."));
		return OK;
	}

	String output;
	List<String> args;
	args.push_back("sign");
	args.push_back("--verbose");
	args.push_back("--ks");
	args.push_back(keystore);
	args.push_back("--ks-pass");
	args.push_back("pass:" + password);
	args.push_back("--ks-key-alias");
	args.push_back(user);
	args.push_back(apk_path);
	if (OS::get_singleton()->is_stdout_verbose() && p_debug) {
		// We only print verbose logs with credentials for debug builds to avoid leaking release keystore credentials.
		print_verbose("Signing debug binary using: " + String("\n") + apksigner + " " + join_list(args, String(" ")));
	} else {
		List<String> redacted_args = List<String>(args);
		redacted_args.find(keystore)->set("<REDACTED>");
		redacted_args.find("pass:" + password)->set("pass:<REDACTED>");
		redacted_args.find(user)->set("<REDACTED>");
		print_line("Signing binary using: " + String("\n") + apksigner + " " + join_list(redacted_args, String(" ")));
	}
	int retval;
	err = OS::get_singleton()->execute(apksigner, args, &output, &retval, true);
	if (err != OK) {
		add_message(EXPORT_MESSAGE_WARNING, TTR("Code Signing"), TTR("Could not start apksigner executable."));
		return err;
	}
	// By design, apksigner does not output credentials in its output unless --verbose is used
	print_line(output);
	if (retval) {
		add_message(EXPORT_MESSAGE_WARNING, TTR("Code Signing"), vformat(TTR("'apksigner' returned with error #%d"), retval));
		add_message(EXPORT_MESSAGE_WARNING, TTR("Code Signing"), vformat(TTR("output: \n%s"), output));
		return ERR_CANT_CREATE;
	}
	if (ep.step(TTR("Verifying APK..."), 105)) {
		return ERR_SKIP;
	}

	args.clear();
	args.push_back("verify");
	args.push_back("--verbose");
	args.push_back(apk_path);
	if (p_debug) {
		print_verbose("Verifying signed build using: " + String("\n") + apksigner + " " + join_list(args, String(" ")));
	}

	output.clear();
	err = OS::get_singleton()->execute(apksigner, args, &output, &retval, true);
	if (err != OK) {
		add_message(EXPORT_MESSAGE_WARNING, TTR("Code Signing"), TTR("Could not start apksigner executable."));
		return err;
	}
	print_verbose(output);
	if (retval) {
		add_message(EXPORT_MESSAGE_WARNING, TTR("Code Signing"), TTR("'apksigner' verification of APK failed."));
		add_message(EXPORT_MESSAGE_WARNING, TTR("Code Signing"), vformat(TTR("output: \n%s"), output));
		return ERR_CANT_CREATE;
	}
	print_verbose("Successfully completed signing build.");

	return OK;
}

void EditorExportPlatformAndroid::_clear_assets_directory(const Ref<EditorExportPreset> &p_preset) {
	Ref<DirAccess> da_res = DirAccess::create(DirAccess::ACCESS_RESOURCES);
	String gradle_build_directory = ExportTemplateManager::get_android_build_directory(p_preset);

	// Clear the APK assets directory
	String apk_assets_directory = gradle_build_directory.path_join(APK_ASSETS_DIRECTORY);
	if (da_res->dir_exists(apk_assets_directory)) {
		print_verbose("Clearing APK assets directory...");
		Ref<DirAccess> da_assets = DirAccess::open(apk_assets_directory);
		ERR_FAIL_COND(da_assets.is_null());

		da_assets->erase_contents_recursive();
		da_res->remove(apk_assets_directory);
	}

	// Clear the AAB assets directory
	String aab_assets_directory = gradle_build_directory.path_join(AAB_ASSETS_DIRECTORY);
	if (da_res->dir_exists(aab_assets_directory)) {
		print_verbose("Clearing AAB assets directory...");
		Ref<DirAccess> da_assets = DirAccess::open(aab_assets_directory);
		ERR_FAIL_COND(da_assets.is_null());

		da_assets->erase_contents_recursive();
		da_res->remove(aab_assets_directory);
	}
}

void EditorExportPlatformAndroid::_remove_copied_libs(String p_foundry_extension_libs_path) {
	print_verbose("Removing previously installed libraries...");
	Error error;
	String libs_json = FileAccess::get_file_as_string(p_foundry_extension_libs_path, &error);
	if (error || libs_json.is_empty()) {
		print_verbose("No previously installed libraries found");
		return;
	}

	JSON json;
	error = json.parse(libs_json);
	ERR_FAIL_COND_MSG(error, "Error parsing \"" + libs_json + "\" on line " + itos(json.get_error_line()) + ": " + json.get_error_message());

	Vector<String> libs = json.get_data();
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_RESOURCES);
	for (int i = 0; i < libs.size(); i++) {
		print_verbose("Removing previously installed library " + libs[i]);
		da->remove(libs[i]);
	}
	da->remove(p_foundry_extension_libs_path);
}

String EditorExportPlatformAndroid::join_list(const List<String> &p_parts, const String &p_separator) {
	String ret;
	for (List<String>::ConstIterator itr = p_parts.begin(); itr != p_parts.end(); ++itr) {
		if (itr != p_parts.begin()) {
			ret += p_separator;
		}
		ret += *itr;
	}
	return ret;
}

String EditorExportPlatformAndroid::join_abis(const Vector<EditorExportPlatformAndroid::ABI> &p_parts, const String &p_separator, bool p_use_arch) {
	String ret;
	for (int i = 0; i < p_parts.size(); ++i) {
		if (i > 0) {
			ret += p_separator;
		}
		ret += (p_use_arch) ? p_parts[i].arch : p_parts[i].abi;
	}
	return ret;
}

String EditorExportPlatformAndroid::_get_plugins_names(const Ref<EditorExportPreset> &p_preset) const {
	Vector<String> names;

	Vector<Ref<EditorExportPlugin>> export_plugins = EditorExport::get_singleton()->get_export_plugins();
	for (int i = 0; i < export_plugins.size(); i++) {
		if (export_plugins[i]->supports_platform(Ref<EditorExportPlatform>(this))) {
			names.push_back(export_plugins[i]->get_name());
		}
	}

	String plugins_names = String("|").join(names);
	return plugins_names;
}

bool EditorExportPlatformAndroid::_is_clean_build_required(const Ref<EditorExportPreset> &p_preset) {
	bool first_build = last_gradle_build_time == 0;
	bool have_plugins_changed = false;
	String gradle_build_dir = ExportTemplateManager::get_android_build_directory(p_preset);
	bool has_build_dir_changed = last_gradle_build_dir != gradle_build_dir;

	String plugin_names = _get_plugins_names(p_preset);

	if (!first_build) {
		have_plugins_changed = plugin_names != last_plugin_names;
	}

	last_gradle_build_time = OS::get_singleton()->get_unix_time();
	last_gradle_build_dir = gradle_build_dir;
	last_plugin_names = plugin_names;

	return have_plugins_changed || has_build_dir_changed || first_build;
}

Error EditorExportPlatformAndroid::export_project(const Ref<EditorExportPreset> &p_preset, bool p_debug, const String &p_path, BitField<EditorExportPlatform::DebugFlags> p_flags) {
	int export_format = int(p_preset->get("gradle_build/export_format"));
	bool should_sign = p_preset->get("package/signed");
	return export_project_helper(p_preset, p_debug, p_path, export_format, should_sign, p_flags);
}

Error EditorExportPlatformAndroid::_generate_sparse_pck_metadata(const Ref<EditorExportPreset> &p_preset, PackData &p_pack_data, Vector<uint8_t> &r_data) {
	Error err;
	Ref<FileAccess> ftmp = FileAccess::create_temp(FileAccess::WRITE_READ, "export_index", "tmp", false, &err);
	if (err != OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Save PCK"), TTR("Could not create temporary file!"));
		return err;
	}
	int64_t pck_start_pos = ftmp->get_position();
	uint64_t file_base_ofs = 0;
	uint64_t dir_base_ofs = 0;
	EditorExportPlatform::_store_header(ftmp, p_preset->get_enc_pck() && p_preset->get_enc_directory(), true, file_base_ofs, dir_base_ofs);

	// Write directory.
	uint64_t dir_offset = ftmp->get_position();
	ftmp->seek(dir_base_ofs);
	ftmp->store_64(dir_offset - pck_start_pos);
	ftmp->seek(dir_offset);

	Vector<uint8_t> key;
	if (p_preset->get_enc_pck() && p_preset->get_enc_directory()) {
		String script_key = _get_script_encryption_key(p_preset);
		key.resize(32);
		if (script_key.length() == 64) {
			for (int i = 0; i < 32; i++) {
				int v = 0;
				if (i * 2 < script_key.length()) {
					char32_t ct = script_key[i * 2];
					if (is_digit(ct)) {
						ct = ct - '0';
					} else if (ct >= 'a' && ct <= 'f') {
						ct = 10 + ct - 'a';
					}
					v |= ct << 4;
				}

				if (i * 2 + 1 < script_key.length()) {
					char32_t ct = script_key[i * 2 + 1];
					if (is_digit(ct)) {
						ct = ct - '0';
					} else if (ct >= 'a' && ct <= 'f') {
						ct = 10 + ct - 'a';
					}
					v |= ct;
				}
				key.write[i] = v;
			}
		}
	}

	if (!EditorExportPlatform::_encrypt_and_store_directory(ftmp, p_pack_data, key, p_preset->get_seed(), 0)) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Save PCK"), TTR("Can't create encrypted file."));
		return ERR_CANT_CREATE;
	}

	r_data.resize(ftmp->get_length());
	ftmp->seek(0);
	ftmp->get_buffer(r_data.ptrw(), r_data.size());
	ftmp.unref();

	return OK;
}

Error EditorExportPlatformAndroid::export_project_helper(const Ref<EditorExportPreset> &p_preset, bool p_debug, const String &p_path, int export_format, bool should_sign, BitField<EditorExportPlatform::DebugFlags> p_flags) {
	ExportNotifier notifier(*this, p_preset, p_debug, p_path, p_flags);
	FoundryJavaExportConfig foundry_java;
	String foundry_java_error;
	Error foundry_java_config_error = _get_foundry_java_export_config(p_preset.ptr(), foundry_java, foundry_java_error);
	if (foundry_java_config_error != OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), foundry_java_error);
		return foundry_java_config_error;
	}

	const String base_dir = p_path.get_base_dir();
	if (!DirAccess::exists(base_dir)) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Target folder does not exist or is inaccessible: \"%s\""), base_dir));
		return ERR_FILE_BAD_PATH;
	}

	String src_apk;
	Error err;

	EditorProgress ep("export", TTR("Exporting for Android"), 105, true);

	bool use_gradle_build = bool(p_preset->get("gradle_build/use_gradle_build"));
	String gradle_build_directory = use_gradle_build ? ExportTemplateManager::get_android_build_directory(p_preset) : "";
	bool p_give_internet = p_flags.has_flag(DEBUG_FLAG_DUMB_CLIENT) || p_flags.has_flag(DEBUG_FLAG_REMOTE_DEBUG);
	bool apk_expansion = p_preset->get("apk_expansion/enable");
	Vector<ABI> enabled_abis = get_enabled_abis(p_preset);
	if (foundry_java.enabled && enabled_abis.is_empty()) {
		const String error = TTR("Foundry-Java requires at least one enabled Android architecture in the export preset.");
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), error);
		return ERR_INVALID_PARAMETER;
	}

	print_verbose("Exporting for Android...");
	print_verbose("- debug build: " + bool_to_string(p_debug));
	print_verbose("- export path: " + p_path);
	print_verbose("- export format: " + itos(export_format));
	print_verbose("- sign build: " + bool_to_string(should_sign));
	print_verbose("- gradle build enabled: " + bool_to_string(use_gradle_build));
	print_verbose("- apk expansion enabled: " + bool_to_string(apk_expansion));
	print_verbose("- enabled abis: " + join_abis(enabled_abis, ",", false));
	print_verbose("- export filter: " + itos(p_preset->get_export_filter()));
	print_verbose("- include filter: " + p_preset->get_include_filter());
	print_verbose("- exclude filter: " + p_preset->get_exclude_filter());

	Ref<Image> main_image;
	Ref<Image> foreground;
	Ref<Image> background;
	Ref<Image> monochrome;

	load_icon_refs(p_preset, main_image, foreground, background, monochrome);

	Vector<uint8_t> command_line_flags;
	// Write command line flags into the command_line_flags variable.
	get_command_line_flags(p_preset, p_path, p_flags, command_line_flags);

	if (export_format == EXPORT_FORMAT_AAB) {
		if (!p_path.ends_with(".aab")) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), TTR("Invalid filename! Android App Bundle requires the *.aab extension."));
			return ERR_UNCONFIGURED;
		}
		if (apk_expansion) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), TTR("APK Expansion not compatible with Android App Bundle."));
			return ERR_UNCONFIGURED;
		}
	}
	if (export_format == EXPORT_FORMAT_APK && !p_path.ends_with(".apk")) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), TTR("Invalid filename! Android APK requires the *.apk extension."));
		return ERR_UNCONFIGURED;
	}
	if (export_format > EXPORT_FORMAT_AAB || export_format < EXPORT_FORMAT_APK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), TTR("Unsupported export format!"));
		return ERR_UNCONFIGURED;
	}
	String err_string;
	if (!has_valid_username_and_password(p_preset, err_string)) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), TTR(err_string));
		return ERR_UNCONFIGURED;
	}

	if (use_gradle_build) {
		print_verbose("Starting gradle build...");
		//test that installed build version is alright
		{
			print_verbose("Checking build version...");
			String gradle_base_directory = gradle_build_directory.get_base_dir();
			Ref<FileAccess> f = FileAccess::open(gradle_base_directory.path_join(".build_version"), FileAccess::READ);
			if (f.is_null()) {
				add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), TTR("Trying to build from a gradle built template, but no version info for it exists. Please reinstall from the 'Project' menu."));
				return ERR_UNCONFIGURED;
			}
			String current_version = ExportTemplateManager::get_android_template_identifier(p_preset);
			String installed_version = f->get_line().strip_edges();
			print_verbose("- build version: " + installed_version);
			if (installed_version != current_version) {
				add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR(MISMATCHED_VERSIONS_MESSAGE), installed_version, current_version));
				return ERR_UNCONFIGURED;
			}
		}
		const String assets_directory = get_assets_directory(p_preset, export_format);
		String java_sdk_path = EDITOR_GET("export/android/java_sdk_path");
		if (java_sdk_path.is_empty()) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), TTR("Java SDK path must be configured in Editor Settings at 'export/android/java_sdk_path'."));
			return ERR_UNCONFIGURED;
		}
		print_verbose("Java sdk path: " + java_sdk_path);

		String sdk_path = EDITOR_GET("export/android/android_sdk_path");
		if (sdk_path.is_empty()) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), TTR("Android SDK path must be configured in Editor Settings at 'export/android/android_sdk_path'."));
			return ERR_UNCONFIGURED;
		}
		print_verbose("Android sdk path: " + sdk_path);

		// TODO: should we use "package/name" or "application/config/name"?
		String project_name = get_project_name(p_preset, p_preset->get("package/name"));
		err = _create_project_name_strings_files(p_preset, project_name, gradle_build_directory, get_project_setting(p_preset, "application/config/name_localized")); //project name localization.
		if (err != OK) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), TTR("Unable to overwrite res/*.xml files with project name."));
		}
		// Copies the project icon files into the appropriate Gradle project directory.
		_copy_icons_to_gradle_project(p_preset, main_image, foreground, background, monochrome);
		// Write an AndroidManifest.xml file into the Gradle project directory.
		_write_tmp_manifest(p_preset, p_give_internet, p_debug);
		// Modify res/values/themes.xml file.
		_fix_themes_xml(p_preset);

		//stores all the project files inside the Gradle project directory. Also includes all ABIs
		_clear_assets_directory(p_preset);
		String foundry_extension_libs_path = gradle_build_directory.path_join(FOUNDRY_EXTENSION_LIBS_PATH);
		_remove_copied_libs(foundry_extension_libs_path);
		if (!apk_expansion) {
			print_verbose("Exporting project files...");
			CustomExportData user_data;
			user_data.assets_directory = assets_directory;
			user_data.libs_directory = gradle_build_directory.path_join("libs");
			user_data.debug = p_debug;
			if (p_flags.has_flag(DEBUG_FLAG_DUMB_CLIENT)) {
				err = export_project_files(p_preset, p_debug, ignore_apk_file, nullptr, &user_data, copy_gradle_so);
			} else {
				user_data.pd.path = "assets.sparsepck";
				user_data.pd.use_sparse_pck = true;
				err = export_project_files(p_preset, p_debug, rename_and_store_file_in_gradle_project, nullptr, &user_data, copy_gradle_so);

				Vector<uint8_t> enc_data;
				err = _generate_sparse_pck_metadata(p_preset, user_data.pd, enc_data);
				if (err != OK) {
					add_message(EXPORT_MESSAGE_ERROR, TTR("Save PCK"), TTR("Could not generate sparse pck metadata!"));
					return err;
				}

				err = store_file_at_path(user_data.assets_directory + "/assets.sparsepck", enc_data);
				if (err != OK) {
					add_message(EXPORT_MESSAGE_ERROR, TTR("Save PCK"), TTR("Could not write PCK directory!"));
					return err;
				}
			}
			if (err != OK) {
				add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), TTR("Could not export project files to gradle project."));
				return err;
			}
			if (user_data.libs.size() > 0) {
				Ref<FileAccess> fa = FileAccess::open(foundry_extension_libs_path, FileAccess::WRITE);
				fa->store_string(JSON::stringify(user_data.libs, "\t"));
			}
		} else {
			print_verbose("Saving apk expansion file...");
			err = save_apk_expansion_file(p_preset, p_debug, p_path);
			if (err != OK) {
				add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), TTR("Could not write expansion package file!"));
				return err;
			}
		}

		print_verbose("Storing command line flags...");
		store_file_at_path(assets_directory + "/_cl_", command_line_flags);

		print_verbose("Updating JAVA_HOME environment to " + java_sdk_path);
		OS::get_singleton()->set_environment("JAVA_HOME", java_sdk_path);

		print_verbose("Updating ANDROID_HOME environment to " + sdk_path);
		OS::get_singleton()->set_environment("ANDROID_HOME", sdk_path);
		String build_command;

#ifdef WINDOWS_ENABLED
		build_command = "gradlew.bat";
#else
		build_command = "gradlew";
#endif

		String build_path = ProjectSettings::get_singleton()->globalize_path(gradle_build_directory);
		build_command = build_path.path_join(build_command);

		String package_name = get_package_name(p_preset, p_preset->get("package/unique_name"));
		String version_code = itos(p_preset->get("version/code"));
		String version_name = p_preset->get_version("version/name");
		String min_sdk_version = p_preset->get("gradle_build/min_sdk");
		if (!min_sdk_version.is_valid_int()) {
			min_sdk_version = itos(VULKAN_MIN_SDK_VERSION);
		}
		String target_sdk_version = p_preset->get("gradle_build/target_sdk");
		if (!target_sdk_version.is_valid_int()) {
			target_sdk_version = itos(DEFAULT_TARGET_SDK_VERSION);
		}
		String enabled_abi_string = join_abis(enabled_abis, "|", false);
		String sign_flag = bool_to_string(should_sign);
		String zipalign_flag = "true";
		String compress_native_libraries_flag = bool_to_string(p_preset->get("gradle_build/compress_native_libraries"));

		bool has_dotnet_project = false;
		String openxr_loader_version;
		const String openxr_loader_feature_prefix = "openxr_loader_for_android:";
		Vector<Ref<EditorExportPlugin>> export_plugins = EditorExport::get_singleton()->get_export_plugins();
		for (int i = 0; i < export_plugins.size(); i++) {
			PackedStringArray features = export_plugins[i]->get_export_features(Ref<EditorExportPlatform>(this), p_debug);
			if (features.has("dotnet")) {
				has_dotnet_project = true;
			}
			for (const String &feature : features) {
				if (!feature.begins_with(openxr_loader_feature_prefix)) {
					continue;
				}

				String candidate = feature.substr(openxr_loader_feature_prefix.length());
				PackedStringArray components = candidate.split(".");
				bool valid_version = components.size() == 3;
				for (const String &component : components) {
					valid_version = valid_version && component.is_valid_int() && component.to_int() >= 0;
				}
				if (!valid_version) {
					add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Invalid OpenXR Android loader version: %s."), candidate));
					return ERR_INVALID_DATA;
				}
				if (!openxr_loader_version.is_empty() && openxr_loader_version != candidate) {
					add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Conflicting OpenXR Android loader versions: %s and %s."), openxr_loader_version, candidate));
					return ERR_INVALID_DATA;
				}
				openxr_loader_version = candidate;
			}
		}

		bool clean_build_required = _is_clean_build_required(p_preset);

		List<String> cmdline;
		cmdline.push_back("validateJavaVersion");
		if (clean_build_required) {
			cmdline.push_back("clean");
		}

		String edition = has_dotnet_project ? "Mono" : "Standard";
		String build_type = p_debug ? "Debug" : "Release";
		if (export_format == EXPORT_FORMAT_AAB) {
			String bundle_build_command = vformat("bundle%s%s", edition, build_type);
			cmdline.push_back(bundle_build_command);
		} else if (export_format == EXPORT_FORMAT_APK) {
			String apk_build_command = vformat("assemble%s%s", edition, build_type);
			cmdline.push_back(apk_build_command);
		}

		String addons_directory = ProjectSettings::get_singleton()->globalize_path("res://addons");

		cmdline.push_back("-p"); // argument to specify the start directory.
		cmdline.push_back(build_path); // start directory.
		cmdline.push_back("-Paddons_directory=" + addons_directory); // path to the addon directory as it may contain jar or aar dependencies
		cmdline.push_back("-Pexport_package_name=" + package_name); // argument to specify the package name.
		cmdline.push_back("-Pexport_version_code=" + version_code); // argument to specify the version code.
		cmdline.push_back("-Pexport_version_name=" + version_name); // argument to specify the version name.
		cmdline.push_back("-Pexport_version_min_sdk=" + min_sdk_version); // argument to specify the min sdk.
		cmdline.push_back("-Pexport_version_target_sdk=" + target_sdk_version); // argument to specify the target sdk.
		cmdline.push_back("-Pexport_enabled_abis=" + enabled_abi_string); // argument to specify enabled ABIs.
		if (foundry_java.enabled) {
			cmdline.push_back("-Pfoundry_java_registry_marker=registry-index-v2");
			cmdline.push_back("-Pfoundry_java_gradle_plugin_kind=" + foundry_java.plugin_kind);
			cmdline.push_back("-Pfoundry_java_gradle_plugin=" + foundry_java.plugin);
			if (!foundry_java.maven_artifacts.is_empty()) {
				cmdline.push_back("-Pfoundry_java_maven_artifacts=" + String("|").join(foundry_java.maven_artifacts));
			}
			if (!foundry_java.local_artifacts.is_empty()) {
				cmdline.push_back("-Pfoundry_java_local_artifacts=" + String("|").join(foundry_java.local_artifacts));
			}
		}
		if (!openxr_loader_version.is_empty()) {
			cmdline.push_back("-Popenxr_loader_version=" + openxr_loader_version); // fixed engine-owned OpenXR loader version.
		}
		cmdline.push_back("-Pperform_zipalign=" + zipalign_flag); // argument to specify whether the build should be zipaligned.
		cmdline.push_back("-Pperform_signing=" + sign_flag); // argument to specify whether the build should be signed.
		cmdline.push_back("-Pcompress_native_libraries=" + compress_native_libraries_flag); // argument to specify whether the build should compress native libraries.

		// NOTE: The release keystore is not included in the verbose logging
		// to avoid accidentally leaking sensitive information when sharing verbose logs for troubleshooting.
		// Any non-sensitive additions to the command line arguments must be done above this section.
		// Sensitive additions must be done below the logging statement.
		Vector<String> repository_output_redactions;
		const String repository_log_argument = foundry_java.enabled && !foundry_java.repositories.is_empty() ? " -Pfoundry_java_maven_repositories=<redacted>" : "";
		print_verbose("Build Android project using gradle command: " + String("\n") + build_command + " " + join_list(cmdline, String(" ")) + repository_log_argument);
		if (foundry_java.enabled && !foundry_java.repositories.is_empty()) {
			const String joined_repositories = String("|").join(foundry_java.repositories);
			repository_output_redactions.push_back(joined_repositories);
			for (const String &repository : foundry_java.repositories) {
				repository_output_redactions.push_back(repository);
				if (repository.begins_with("file:///")) {
					repository_output_redactions.push_back("file:" + repository.trim_prefix("file://"));
				}
			}
			cmdline.push_back("-Pfoundry_java_maven_repositories=" + joined_repositories);
		}

		if (should_sign) {
			if (p_debug) {
				String debug_keystore = _get_keystore_path(p_preset, true);
				String debug_password = p_preset->get_or_env("keystore/debug_password", ENV_ANDROID_KEYSTORE_DEBUG_PASS);
				String debug_user = p_preset->get_or_env("keystore/debug_user", ENV_ANDROID_KEYSTORE_DEBUG_USER);

				if (debug_keystore.is_empty()) {
					debug_keystore = EDITOR_GET("export/android/debug_keystore");
					debug_password = EDITOR_GET("export/android/debug_keystore_pass");
					debug_user = EDITOR_GET("export/android/debug_keystore_user");
				}
				if (debug_keystore.is_relative_path()) {
					debug_keystore = OS::get_singleton()->get_resource_dir().path_join(debug_keystore).simplify_path();
				}
				if (!FileAccess::exists(debug_keystore)) {
					add_message(EXPORT_MESSAGE_ERROR, TTR("Code Signing"), TTR("Could not find debug keystore, unable to export."));
					return ERR_FILE_CANT_OPEN;
				}
				cmdline.push_back("-Pdebug_keystore_file=" + debug_keystore); // argument to specify the debug keystore file.
				cmdline.push_back("-Pdebug_keystore_alias=" + debug_user); // argument to specify the debug keystore alias.
				cmdline.push_back("-Pdebug_keystore_password=" + debug_password); // argument to specify the debug keystore password.
			} else {
				// Pass the release keystore info as well
				String release_keystore = _get_keystore_path(p_preset, false);
				String release_username = p_preset->get_or_env("keystore/release_user", ENV_ANDROID_KEYSTORE_RELEASE_USER);
				String release_password = p_preset->get_or_env("keystore/release_password", ENV_ANDROID_KEYSTORE_RELEASE_PASS);
				if (release_keystore.is_relative_path()) {
					release_keystore = OS::get_singleton()->get_resource_dir().path_join(release_keystore).simplify_path();
				}
				if (!FileAccess::exists(release_keystore)) {
					add_message(EXPORT_MESSAGE_ERROR, TTR("Code Signing"), TTR("Could not find release keystore, unable to export."));
					return ERR_FILE_CANT_OPEN;
				}
				cmdline.push_back("-Prelease_keystore_file=" + release_keystore); // argument to specify the release keystore file.
				cmdline.push_back("-Prelease_keystore_alias=" + release_username); // argument to specify the release keystore alias.
				cmdline.push_back("-Prelease_keystore_password=" + release_password); // argument to specify the release keystore password.
			}
		}

		List<String> copy_args;
		String copy_command = "copyAndRenameBinary";
		copy_args.push_back(copy_command);

		copy_args.push_back("-p"); // argument to specify the start directory.
		copy_args.push_back(build_path); // start directory.

		copy_args.push_back("-Pexport_edition=" + edition.to_lower());

		copy_args.push_back("-Pexport_build_type=" + build_type.to_lower());

		String export_format_arg = export_format == EXPORT_FORMAT_AAB ? "aab" : "apk";
		copy_args.push_back("-Pexport_format=" + export_format_arg);

		String export_filename = p_path.get_file();
		String export_path = p_path.get_base_dir();
		if (export_path.is_relative_path()) {
			export_path = OS::get_singleton()->get_resource_dir().path_join(export_path);
		}
		export_path = ProjectSettings::get_singleton()->globalize_path(export_path).simplify_path();

		copy_args.push_back("-Pexport_path=file:" + export_path);
		copy_args.push_back("-Pexport_filename=" + export_filename);

		String build_project_output;
		int result = EditorNode::get_singleton()->execute_and_show_output(
				TTR("Building Android Project (gradle)"),
				build_command,
				cmdline,
				true,
				false,
				&build_project_output,
				repository_output_redactions);
		if (result != 0) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), TTR("Building of Android project failed, check output for the error:") + "\n\n" + build_project_output);
			return ERR_CANT_CREATE;
		} else {
			print_verbose(build_project_output);
		}

		print_verbose("Copying Android binary using gradle command: " + String("\n") + build_command + " " + join_list(copy_args, String(" ")));
		String copy_binary_output;
		int copy_result = EditorNode::get_singleton()->execute_and_show_output(TTR("Moving output"), build_command, copy_args, true, false, &copy_binary_output);
		if (copy_result != 0) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), TTR("Unable to copy and rename export file:") + "\n\n" + copy_binary_output);
			return ERR_CANT_CREATE;
		} else {
			print_verbose(copy_binary_output);
		}

		if (foundry_java.enabled) {
			String final_artifact_error;
			err = _inspect_foundry_java_artifact(p_path, enabled_abis, export_format, final_artifact_error);
			if (err != OK) {
				add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), final_artifact_error);
				const Error remove_error = DirAccess::remove_absolute(p_path);
				if (remove_error != OK || FileAccess::exists(p_path)) {
					add_message(
							EXPORT_MESSAGE_ERROR,
							TTR("Export"),
							vformat(TTR("Foundry-Java export could not remove rejected final artifact '%s'."), p_path));
				}
				return err;
			}
		}

		print_verbose("Successfully completed Android gradle build.");
		return OK;
	}
	// This is the start of the Legacy build system
	print_verbose("Starting legacy build system...");
	if (p_debug) {
		src_apk = p_preset->get("custom_template/debug");
	} else {
		src_apk = p_preset->get("custom_template/release");
	}
	src_apk = src_apk.strip_edges();
	if (src_apk.is_empty()) {
		if (p_debug) {
			src_apk = find_export_template("android_debug.apk");
		} else {
			src_apk = find_export_template("android_release.apk");
		}
		if (src_apk.is_empty()) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(p_debug ? TTR("Debug export template not found: \"%s\".") : TTR("Release export template not found: \"%s\"."), src_apk));
			return ERR_FILE_NOT_FOUND;
		}
	}

	Ref<FileAccess> io_fa;
	zlib_filefunc_def io = zipio_create_io(&io_fa);

	if (ep.step(TTR("Creating APK..."), 0)) {
		return ERR_SKIP;
	}

	unzFile pkg = unzOpen2(src_apk.utf8().get_data(), &io);
	if (!pkg) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Could not find template APK to export: \"%s\"."), src_apk));
		return ERR_FILE_NOT_FOUND;
	}

	int ret = unzGoToFirstFile(pkg);

	Ref<FileAccess> io2_fa;
	zlib_filefunc_def io2 = zipio_create_io(&io2_fa);

	String tmp_unaligned_path = EditorPaths::get_singleton()->get_temp_dir().path_join("tmpexport-unaligned." + uitos(OS::get_singleton()->get_unix_time()) + ".apk");

#define CLEANUP_AND_RETURN(m_err)                            \
	{                                                        \
		DirAccess::remove_file_or_error(tmp_unaligned_path); \
		return m_err;                                        \
	}                                                        \
	((void)0)

	zipFile unaligned_apk = zipOpen2(tmp_unaligned_path.utf8().get_data(), APPEND_STATUS_CREATE, nullptr, &io2);

	String cmdline = p_preset->get("command_line/extra_args");

	String version_name = p_preset->get_version("version/name");
	String package_name = p_preset->get("package/unique_name");

	String apk_expansion_pkey = p_preset->get("apk_expansion/public_key");

	Vector<ABI> invalid_abis(enabled_abis);

	//To temporarily store icon xml data.
	Vector<uint8_t> themed_icon_xml_data;
	int icon_xml_compression_method = -1;

	while (ret == UNZ_OK) {
		//get filename
		unz_file_info info;
		char fname[16384];
		ret = unzGetCurrentFileInfo(pkg, &info, fname, 16384, nullptr, 0, nullptr, 0);
		if (ret != UNZ_OK) {
			break;
		}

		bool skip = false;

		String file = String::utf8(fname);

		Vector<uint8_t> data;
		data.resize(info.uncompressed_size);

		//read
		unzOpenCurrentFile(pkg);
		unzReadCurrentFile(pkg, data.ptrw(), data.size());
		unzCloseCurrentFile(pkg);

		//write
		if (file == "AndroidManifest.xml") {
			_fix_manifest(p_preset, data, p_give_internet);

			// Allow editor export plugins to update the prebuilt manifest as needed.
			Vector<Ref<EditorExportPlugin>> export_plugins = EditorExport::get_singleton()->get_export_plugins();
			for (int i = 0; i < export_plugins.size(); i++) {
				if (export_plugins[i]->supports_platform(Ref<EditorExportPlatform>(this))) {
					PackedByteArray export_plugin_data = export_plugins[i]->update_android_prebuilt_manifest(Ref<EditorExportPlatform>(this), data);
					if (!export_plugin_data.is_empty()) {
						data = export_plugin_data;
					}
				}
			}
		}
		if (file == "resources.arsc") {
			_fix_resources(p_preset, data);
		}

		if (file == THEMED_ICON_XML_PATH) {
			// Store themed_icon.xml data.
			themed_icon_xml_data = data;
			skip = true;
		}

		if (file == ICON_XML_PATH) {
			if (monochrome.is_valid() && !monochrome->is_empty()) {
				// Defer processing of icon.xml until after themed_icon.xml is read.
				icon_xml_compression_method = info.compression_method;
				skip = true;
			}
		}

		if ((file.ends_with(".webp") || file.ends_with(".png")) && file.contains("mipmap")) {
			for (int i = 0; i < ICON_DENSITIES_COUNT; ++i) {
				if (main_image.is_valid() && !main_image->is_empty()) {
					if (file == LAUNCHER_ICONS[i].export_path) {
						_process_launcher_icons(file, main_image, LAUNCHER_ICONS[i].dimensions, data);
					}
				}
				if (foreground.is_valid() && !foreground->is_empty()) {
					if (file == LAUNCHER_ADAPTIVE_ICON_FOREGROUNDS[i].export_path) {
						_process_launcher_icons(file, foreground, LAUNCHER_ADAPTIVE_ICON_FOREGROUNDS[i].dimensions, data);
					}
				}
				if (background.is_valid() && !background->is_empty()) {
					if (file == LAUNCHER_ADAPTIVE_ICON_BACKGROUNDS[i].export_path) {
						_process_launcher_icons(file, background, LAUNCHER_ADAPTIVE_ICON_BACKGROUNDS[i].dimensions, data);
					}
				}
				if (monochrome.is_valid() && !monochrome->is_empty()) {
					if (file == LAUNCHER_ADAPTIVE_ICON_MONOCHROMES[i].export_path) {
						_process_launcher_icons(file, monochrome, LAUNCHER_ADAPTIVE_ICON_MONOCHROMES[i].dimensions, data);
					}
				}
			}
		}

		if (file.ends_with(".so")) {
			bool enabled = false;
			for (int i = 0; i < enabled_abis.size(); ++i) {
				if (file.begins_with("lib/" + enabled_abis[i].abi + "/")) {
					invalid_abis.erase(enabled_abis[i]);
					enabled = true;
					break;
				}
			}
			if (!enabled) {
				skip = true;
			}
		}

		if (file.begins_with("META-INF") && should_sign) {
			skip = true;
		}

		if (!skip) {
			print_line("ADDING: " + file);

			// Respect decision on compression made by AAPT for the export template
			const bool uncompressed = info.compression_method == 0;

			zip_fileinfo zipfi = get_zip_fileinfo();

			zipOpenNewFileInZip(unaligned_apk,
					file.utf8().get_data(),
					&zipfi,
					nullptr,
					0,
					nullptr,
					0,
					nullptr,
					uncompressed ? 0 : Z_DEFLATED,
					Z_DEFAULT_COMPRESSION);

			zipWriteInFileInZip(unaligned_apk, data.ptr(), data.size());
			zipCloseFileInZip(unaligned_apk);
		}

		ret = unzGoToNextFile(pkg);
	}

	// Process deferred icon.xml and replace it's data with themed_icon.xml.
	if (monochrome.is_valid() && !monochrome->is_empty()) {
		print_line("ADDING: " + ICON_XML_PATH + " (replacing with themed_icon.xml data)");

		const bool uncompressed = icon_xml_compression_method == 0;
		zip_fileinfo zipfi = get_zip_fileinfo();

		zipOpenNewFileInZip(unaligned_apk,
				ICON_XML_PATH.utf8().get_data(),
				&zipfi,
				nullptr,
				0,
				nullptr,
				0,
				nullptr,
				uncompressed ? 0 : Z_DEFLATED,
				Z_DEFAULT_COMPRESSION);

		zipWriteInFileInZip(unaligned_apk, themed_icon_xml_data.ptr(), themed_icon_xml_data.size());
		zipCloseFileInZip(unaligned_apk);
	}

	if (!invalid_abis.is_empty()) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Missing libraries in the export template for the selected architectures: %s. Please build a template with all required libraries, or uncheck the missing architectures in the export preset."), join_abis(invalid_abis, ", ", false)));
		CLEANUP_AND_RETURN(ERR_FILE_NOT_FOUND);
	}

	if (ep.step(TTR("Adding files..."), 1)) {
		CLEANUP_AND_RETURN(ERR_SKIP);
	}
	err = OK;

	if (p_flags.has_flag(DEBUG_FLAG_DUMB_CLIENT)) {
		APKExportData ed;
		ed.ep = &ep;
		ed.apk = unaligned_apk;
		err = export_project_files(p_preset, p_debug, ignore_apk_file, nullptr, &ed, save_apk_so);
	} else {
		if (apk_expansion) {
			err = save_apk_expansion_file(p_preset, p_debug, p_path);
			if (err != OK) {
				add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), TTR("Could not write expansion package file!"));
				return err;
			}
		} else {
			APKExportData ed;
			ed.ep = &ep;
			ed.apk = unaligned_apk;
			ed.pd.path = "assets.sparsepck";
			ed.pd.use_sparse_pck = true;
			err = export_project_files(p_preset, p_debug, save_apk_file, nullptr, &ed, save_apk_so);

			Vector<uint8_t> enc_data;
			err = _generate_sparse_pck_metadata(p_preset, ed.pd, enc_data);
			if (err != OK) {
				add_message(EXPORT_MESSAGE_ERROR, TTR("Save PCK"), TTR("Could not generate sparse pck metadata!"));
				return err;
			}

			store_in_apk(&ed, "assets/assets.sparsepck", enc_data, 0);
		}
	}

	if (err != OK) {
		unzClose(pkg);
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Could not export project files.")));
		CLEANUP_AND_RETURN(ERR_SKIP);
	}

	zip_fileinfo zipfi = get_zip_fileinfo();
	zipOpenNewFileInZip(unaligned_apk,
			"assets/_cl_",
			&zipfi,
			nullptr,
			0,
			nullptr,
			0,
			nullptr,
			0, // No compress (little size gain and potentially slower startup)
			Z_DEFAULT_COMPRESSION);
	zipWriteInFileInZip(unaligned_apk, command_line_flags.ptr(), command_line_flags.size());
	zipCloseFileInZip(unaligned_apk);
	zipClose(unaligned_apk, nullptr);
	unzClose(pkg);

	// Let's zip-align (must be done before signing)

	static const int PAGE_SIZE_KB = 16 * 1024;
	static const int ZIP_ALIGNMENT = 4;

	// If we're not signing the apk, then the next step should be the last.
	const int next_step = should_sign ? 103 : 105;
	if (ep.step(TTR("Aligning APK..."), next_step)) {
		CLEANUP_AND_RETURN(ERR_SKIP);
	}

	unzFile tmp_unaligned = unzOpen2(tmp_unaligned_path.utf8().get_data(), &io);
	if (!tmp_unaligned) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Could not unzip temporary unaligned APK.")));
		CLEANUP_AND_RETURN(ERR_FILE_NOT_FOUND);
	}

	ret = unzGoToFirstFile(tmp_unaligned);

	io2 = zipio_create_io(&io2_fa);
	zipFile final_apk = zipOpen2(p_path.utf8().get_data(), APPEND_STATUS_CREATE, nullptr, &io2);

	// Take files from the unaligned APK and write them out to the aligned one
	// in raw mode, i.e. not uncompressing and recompressing, aligning them as needed,
	// following what is done in https://github.com/android/platform_build/blob/master/tools/zipalign/ZipAlign.cpp
	int bias = 0;
	while (ret == UNZ_OK) {
		unz_file_info info;
		memset(&info, 0, sizeof(info));

		char fname[16384];
		char extra[16384];
		ret = unzGetCurrentFileInfo(tmp_unaligned, &info, fname, 16384, extra, 16384 - ZIP_ALIGNMENT, nullptr, 0);
		if (ret != UNZ_OK) {
			break;
		}

		String file = String::utf8(fname);

		Vector<uint8_t> data;
		data.resize(info.compressed_size);

		// read
		int method, level;
		unzOpenCurrentFile2(tmp_unaligned, &method, &level, 1); // raw read
		long file_offset = unzGetCurrentFileZStreamPos64(tmp_unaligned);
		unzReadCurrentFile(tmp_unaligned, data.ptrw(), data.size());
		unzCloseCurrentFile(tmp_unaligned);

		// align
		int padding = 0;
		if (!info.compression_method) {
			// Uncompressed file => Align
			long new_offset = file_offset + bias;
			const char *ext = strrchr(fname, '.');
			if (ext && strcmp(ext, ".so") == 0) {
				padding = (PAGE_SIZE_KB - (new_offset % PAGE_SIZE_KB)) % PAGE_SIZE_KB;
			} else {
				padding = (ZIP_ALIGNMENT - (new_offset % ZIP_ALIGNMENT)) % ZIP_ALIGNMENT;
			}
		}

		memset(extra + info.size_file_extra, 0, padding);

		zip_fileinfo fileinfo = get_zip_fileinfo();
		zipOpenNewFileInZip2(final_apk,
				file.utf8().get_data(),
				&fileinfo,
				extra,
				info.size_file_extra + padding,
				nullptr,
				0,
				nullptr,
				method,
				level,
				1); // raw write
		zipWriteInFileInZip(final_apk, data.ptr(), data.size());
		zipCloseFileInZipRaw(final_apk, info.uncompressed_size, info.crc);

		bias += padding;

		ret = unzGoToNextFile(tmp_unaligned);
	}

	zipClose(final_apk, nullptr);
	unzClose(tmp_unaligned);

	if (should_sign) {
		// Signing must be done last as any additional modifications to the
		// file will invalidate the signature.
		err = sign_apk(p_preset, p_debug, p_path, ep);
		if (err != OK) {
			// Message is supplied by the subroutine method.
			CLEANUP_AND_RETURN(err);
		}
	}

	CLEANUP_AND_RETURN(OK);
}

void EditorExportPlatformAndroid::get_platform_features(List<String> *r_features) const {
	r_features->push_back("mobile");
	r_features->push_back("android");
}

void EditorExportPlatformAndroid::resolve_platform_feature_priorities(const Ref<EditorExportPreset> &p_preset, HashSet<String> &p_features) {
}

void EditorExportPlatformAndroid::initialize() {
	if (EditorNode::get_singleton()) {
		Ref<Image> img = memnew(Image);
		const bool upsample = !Math::is_equal_approx(Math::round(EDSCALE), EDSCALE);

		ImageLoaderSVG::create_image_from_string(img, _android_logo_svg, EDSCALE, upsample, false);
		logo = ImageTexture::create_from_image(img);

		ImageLoaderSVG::create_image_from_string(img, _android_run_icon_svg, EDSCALE, upsample, false);
		run_icon = ImageTexture::create_from_image(img);

		devices_changed.set();
		_create_editor_debug_keystore_if_needed();
		_update_preset_status();
		use_scrcpy = EditorSettings::get_singleton()->get_project_metadata("android", "use_scrcpy", false);
	}
}

EditorExportPlatformAndroid::~EditorExportPlatformAndroid() {
	_stop_check_for_changes_poll_thread();
}
