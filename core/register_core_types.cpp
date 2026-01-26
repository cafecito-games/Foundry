/**************************************************************************/
/*  register_core_types.cpp                                               */
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

#include "register_core_types.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/core_bind.h"
#include "core/crypto/aes_context.h"
#include "core/crypto/crypto.h"
#include "core/crypto/hashing_context.h"
#include "core/debugger/engine_profiler.h"
#include "core/extension/foundry_extension.h"
#include "core/extension/foundry_extension_manager.h"
#include "core/extension/foundry_instance.h"
#include "core/input/input.h"
#include "core/input/input_map.h"
#include "core/input/shortcut.h"
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/dtls_server.h"
#include "core/io/file_access_encrypted.h"
#include "core/io/http_client.h"
#include "core/io/image_loader.h"
#include "core/io/json.h"
#include "core/io/marshalls.h"
#include "core/io/missing_resource.h"
#include "core/io/packet_peer.h"
#include "core/io/packet_peer_dtls.h"
#include "core/io/packet_peer_udp.h"
#include "core/io/pck_packer.h"
#include "core/io/resource_format_binary.h"
#include "core/io/resource_importer.h"
#include "core/io/resource_uid.h"
#include "core/io/stream_peer_gzip.h"
#include "core/io/stream_peer_tls.h"
#include "core/io/tcp_server.h"
#include "core/io/translation_loader_po.h"
#include "core/io/udp_server.h"
#include "core/io/uds_server.h"
#include "core/io/xml_parser.h"
#include "core/math/a_star.h"
#include "core/math/a_star_grid_2d.h"
#include "core/math/expression.h"
#include "core/math/random_number_generator.h"
#include "core/math/triangle_mesh.h"
#include "core/object/class_db.h"
#include "core/object/script_backtrace.h"
#include "core/object/script_diagnostic_capture.h"
#include "core/object/script_diagnostic_capture_scope.h"
#include "core/object/script_function_state.h"
#include "core/object/script_language_extension.h"
#include "core/object/script_runner.h"
#include "core/object/undo_redo.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/main_loop.h"
#include "core/os/time.h"
#include "core/string/optimized_translation.h"
#include "core/string/translation.h"
#include "core/string/translation_server.h"

static Ref<ResourceFormatSaverBinary> resource_saver_binary;
static Ref<ResourceFormatLoaderBinary> resource_loader_binary;
static Ref<ResourceFormatImporter> resource_format_importer;
static Ref<ResourceFormatImporterSaver> resource_format_importer_saver;
static Ref<ResourceFormatLoaderImage> resource_format_image;
static Ref<TranslationLoaderPO> resource_format_po;
static Ref<ResourceFormatSaverCrypto> resource_format_saver_crypto;
static Ref<ResourceFormatLoaderCrypto> resource_format_loader_crypto;
static Ref<FoundryExtensionResourceLoader> resource_loader_foundry_extension;
static Ref<ResourceFormatSaverJSON> resource_saver_json;
static Ref<ResourceFormatLoaderJSON> resource_loader_json;

static CoreBind::ResourceLoader *_resource_loader = nullptr;
static CoreBind::ResourceSaver *_resource_saver = nullptr;
static CoreBind::OS *_os = nullptr;
static CoreBind::Engine *_engine = nullptr;
static CoreBind::Special::ClassDB *_classdb = nullptr;
static CoreBind::Marshalls *_marshalls = nullptr;
static CoreBind::EngineDebugger *_engine_debugger = nullptr;

static IP *ip = nullptr;
static Time *_time = nullptr;

static CoreBind::Geometry2D *_geometry_2d = nullptr;
static CoreBind::Geometry3D *_geometry_3d = nullptr;

static WorkerThreadPool *worker_thread_pool = nullptr;

extern Mutex _global_mutex;

static FoundryExtensionManager *foundry_extension_manager = nullptr;

extern void register_global_constants();
extern void unregister_global_constants();

static ResourceUID *resource_uid = nullptr;

static bool _is_core_extensions_registered = false;

void register_core_types() {
	OS::get_singleton()->benchmark_begin_measure("Core", "Register Types");

	//consistency check
	static_assert(sizeof(Callable) <= 16);

	ObjectDB::setup();
	StringName::setup();
	register_global_constants();
	CoreStringNames::create();

	FOUNDRY_REGISTER_CLASS(Object);
	FOUNDRY_REGISTER_CLASS(RefCounted);
	FOUNDRY_REGISTER_CLASS(WeakRef);
	FOUNDRY_REGISTER_CLASS(Resource);

	FOUNDRY_REGISTER_CLASS(Time);
	_time = memnew(Time);
	ResourceLoader::initialize();

	Variant::register_types();

	FOUNDRY_REGISTER_CLASS(ResourceFormatLoader);
	FOUNDRY_REGISTER_CLASS(ResourceFormatSaver);

	if constexpr (GD_IS_CLASS_ENABLED(Translation)) {
		resource_format_po.instantiate();
		ResourceLoader::add_resource_format_loader(resource_format_po);
	}

	resource_saver_binary.instantiate();
	ResourceSaver::add_resource_format_saver(resource_saver_binary);
	resource_loader_binary.instantiate();
	ResourceLoader::add_resource_format_loader(resource_loader_binary);

	resource_format_importer.instantiate();
	ResourceLoader::add_resource_format_loader(resource_format_importer);

	resource_format_importer_saver.instantiate();
	ResourceSaver::add_resource_format_saver(resource_format_importer_saver);

	if constexpr (GD_IS_CLASS_ENABLED(Image)) {
		resource_format_image.instantiate();
		ResourceLoader::add_resource_format_loader(resource_format_image);
	}

	FOUNDRY_REGISTER_ABSTRACT_CLASS(Script);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(ScriptLanguage);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(ScriptFunctionState);
	FOUNDRY_REGISTER_CLASS(ScriptBacktrace);
	FOUNDRY_REGISTER_CLASS(ScriptDiagnosticCapture);
	FOUNDRY_REGISTER_CLASS(ScriptDiagnosticCaptureScope);
	FOUNDRY_REGISTER_CLASS(ScriptDiagnosticCaptureResult);
	FOUNDRY_REGISTER_CLASS(ScriptDiagnosticCapturePendingState);
	FOUNDRY_REGISTER_CLASS(ScriptRunner);
	FOUNDRY_REGISTER_VIRTUAL_CLASS(ScriptExtension);
	FOUNDRY_REGISTER_VIRTUAL_CLASS(ScriptLanguageExtension);

	FOUNDRY_REGISTER_CLASS(MissingResource);
	FOUNDRY_REGISTER_CLASS(Image);

	FOUNDRY_REGISTER_CLASS(Shortcut);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(InputEvent);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(InputEventFromWindow);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(InputEventWithModifiers);
	FOUNDRY_REGISTER_CLASS(InputEventKey);
	FOUNDRY_REGISTER_CLASS(InputEventShortcut);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(InputEventMouse);
	FOUNDRY_REGISTER_CLASS(InputEventMouseButton);
	FOUNDRY_REGISTER_CLASS(InputEventMouseMotion);
	FOUNDRY_REGISTER_CLASS(InputEventJoypadButton);
	FOUNDRY_REGISTER_CLASS(InputEventJoypadMotion);
	FOUNDRY_REGISTER_CLASS(InputEventScreenDrag);
	FOUNDRY_REGISTER_CLASS(InputEventScreenTouch);
	FOUNDRY_REGISTER_CLASS(InputEventAction);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(InputEventGesture);
	FOUNDRY_REGISTER_CLASS(InputEventMagnifyGesture);
	FOUNDRY_REGISTER_CLASS(InputEventPanGesture);
	FOUNDRY_REGISTER_CLASS(InputEventMIDI);

	// Network
	FOUNDRY_REGISTER_ABSTRACT_CLASS(StreamPeer);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(StreamPeerSocket);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(SocketServer);
	FOUNDRY_REGISTER_CLASS(StreamPeerExtension);
	FOUNDRY_REGISTER_CLASS(StreamPeerBuffer);
	FOUNDRY_REGISTER_CLASS(StreamPeerGZIP);
	FOUNDRY_REGISTER_CLASS(StreamPeerTCP);
	FOUNDRY_REGISTER_CLASS(TCPServer);

	// IPC using UNIX domain sockets.
	FOUNDRY_REGISTER_CLASS(StreamPeerUDS);
	FOUNDRY_REGISTER_CLASS(UDSServer);

	FOUNDRY_REGISTER_ABSTRACT_CLASS(PacketPeer);
	FOUNDRY_REGISTER_CLASS(PacketPeerExtension);
	FOUNDRY_REGISTER_CLASS(PacketPeerStream);
	FOUNDRY_REGISTER_CLASS(PacketPeerUDP);
	FOUNDRY_REGISTER_CLASS(UDPServer);

	FOUNDRY_REGISTER_ABSTRACT_CLASS(WorkerThreadPool);

	ClassDB::register_custom_instance_class<HTTPClient>();

	// Crypto
	FOUNDRY_REGISTER_CLASS(HashingContext);
	FOUNDRY_REGISTER_CLASS(AESContext);
	ClassDB::register_custom_instance_class<X509Certificate>();
	ClassDB::register_custom_instance_class<CryptoKey>();
	FOUNDRY_REGISTER_ABSTRACT_CLASS(TLSOptions);
	ClassDB::register_custom_instance_class<HMACContext>();
	ClassDB::register_custom_instance_class<Crypto>();
	ClassDB::register_custom_instance_class<StreamPeerTLS>();
	ClassDB::register_custom_instance_class<PacketPeerDTLS>();
	ClassDB::register_custom_instance_class<DTLSServer>();

	if constexpr (GD_IS_CLASS_ENABLED(Crypto)) {
		resource_format_saver_crypto.instantiate();
		ResourceSaver::add_resource_format_saver(resource_format_saver_crypto);

		resource_format_loader_crypto.instantiate();
		ResourceLoader::add_resource_format_loader(resource_format_loader_crypto);
	}

	if constexpr (GD_IS_CLASS_ENABLED(JSON)) {
		resource_saver_json.instantiate();
		ResourceSaver::add_resource_format_saver(resource_saver_json);

		resource_loader_json.instantiate();
		ResourceLoader::add_resource_format_loader(resource_loader_json);
	}

	FOUNDRY_REGISTER_CLASS(MainLoop);
	FOUNDRY_REGISTER_CLASS(Translation);
	FOUNDRY_REGISTER_CLASS(TranslationDomain);
	FOUNDRY_REGISTER_CLASS(OptimizedTranslation);
	FOUNDRY_REGISTER_CLASS(UndoRedo);
	FOUNDRY_REGISTER_CLASS(TriangleMesh);

	FOUNDRY_REGISTER_ABSTRACT_CLASS(FileAccess);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(DirAccess);
	FOUNDRY_REGISTER_CLASS(CoreBind::Thread);
	FOUNDRY_REGISTER_CLASS(CoreBind::Mutex);
	FOUNDRY_REGISTER_CLASS(CoreBind::Semaphore);
	FOUNDRY_REGISTER_VIRTUAL_CLASS(CoreBind::Logger);

	FOUNDRY_REGISTER_CLASS(XMLParser);
	FOUNDRY_REGISTER_CLASS(JSON);

	FOUNDRY_REGISTER_CLASS(ConfigFile);

	FOUNDRY_REGISTER_CLASS(PCKPacker);

	FOUNDRY_REGISTER_CLASS(AStar3D);
	FOUNDRY_REGISTER_CLASS(AStar2D);
	FOUNDRY_REGISTER_CLASS(AStarGrid2D);
	FOUNDRY_REGISTER_CLASS(EncodedObjectAsID);
	FOUNDRY_REGISTER_CLASS(RandomNumberGenerator);

	FOUNDRY_REGISTER_ABSTRACT_CLASS(ImageFormatLoader);
	FOUNDRY_REGISTER_CLASS(ImageFormatLoaderExtension);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(ResourceImporter);

	FOUNDRY_REGISTER_CLASS(FoundryExtension);

	FOUNDRY_REGISTER_ABSTRACT_CLASS(FoundryInstance);

	FOUNDRY_REGISTER_ABSTRACT_CLASS(FoundryExtensionManager);

	FOUNDRY_REGISTER_ABSTRACT_CLASS(ResourceUID);

	FOUNDRY_REGISTER_CLASS(EngineProfiler);

	resource_uid = memnew(ResourceUID);

	foundry_extension_manager = memnew(FoundryExtensionManager);

	if constexpr (GD_IS_CLASS_ENABLED(FoundryExtension)) {
		resource_loader_foundry_extension.instantiate();
		ResourceLoader::add_resource_format_loader(resource_loader_foundry_extension);
	}

	FOUNDRY_REGISTER_ABSTRACT_CLASS(IP);
	FOUNDRY_REGISTER_CLASS(CoreBind::Geometry2D);
	FOUNDRY_REGISTER_CLASS(CoreBind::Geometry3D);
	FOUNDRY_REGISTER_CLASS(CoreBind::ResourceLoader);
	FOUNDRY_REGISTER_CLASS(CoreBind::ResourceSaver);
	FOUNDRY_REGISTER_CLASS(CoreBind::OS);
	FOUNDRY_REGISTER_CLASS(CoreBind::Engine);
	FOUNDRY_REGISTER_CLASS(CoreBind::Special::ClassDB);
	FOUNDRY_REGISTER_CLASS(CoreBind::Marshalls);
	FOUNDRY_REGISTER_CLASS(CoreBind::EngineDebugger);

	FOUNDRY_REGISTER_CLASS(TranslationServer);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(Input);
	FOUNDRY_REGISTER_CLASS(InputMap);
	FOUNDRY_REGISTER_CLASS(Expression);
	FOUNDRY_REGISTER_CLASS(ProjectSettings);

	ip = IP::create();

	_geometry_2d = memnew(CoreBind::Geometry2D);
	_geometry_3d = memnew(CoreBind::Geometry3D);

	_resource_loader = memnew(CoreBind::ResourceLoader);
	_resource_saver = memnew(CoreBind::ResourceSaver);
	_os = memnew(CoreBind::OS);
	_engine = memnew(CoreBind::Engine);
	_classdb = memnew(CoreBind::Special::ClassDB);
	_marshalls = memnew(CoreBind::Marshalls);
	_engine_debugger = memnew(CoreBind::EngineDebugger);

	FOUNDRY_REGISTER_NATIVE_STRUCT(ObjectID, "uint64_t id = 0");
	FOUNDRY_REGISTER_NATIVE_STRUCT(AudioFrame, "float left;float right");
	FOUNDRY_REGISTER_NATIVE_STRUCT(ScriptLanguageExtensionProfilingInfo, "StringName signature;uint64_t call_count;uint64_t total_time;uint64_t self_time");

	worker_thread_pool = memnew(WorkerThreadPool);

	OS::get_singleton()->benchmark_end_measure("Core", "Register Types");
}

void register_core_settings() {
	// Since in register core types, globals may not be present.
	GLOBAL_DEF(PropertyInfo(Variant::INT, "network/limits/tcp/connect_timeout_seconds", PROPERTY_HINT_RANGE, "1,1800,1"), (30));
	GLOBAL_DEF(PropertyInfo(Variant::INT, "network/limits/unix/connect_timeout_seconds", PROPERTY_HINT_RANGE, "1,1800,1"), (30));
	GLOBAL_DEF_RST(PropertyInfo(Variant::INT, "network/limits/packet_peer_stream/max_buffer_po2", PROPERTY_HINT_RANGE, "8,64,1,or_greater"), (16));
	GLOBAL_DEF(PropertyInfo(Variant::STRING, "network/tls/certificate_bundle_override", PROPERTY_HINT_FILE, "*.crt"), "");

	GLOBAL_DEF("threading/worker_pool/max_threads", -1);
	GLOBAL_DEF("threading/worker_pool/low_priority_thread_ratio", 0.3);
}

void register_early_core_singletons() {
	Engine::get_singleton()->add_singleton(Engine::Singleton("Engine", CoreBind::Engine::get_singleton()));
	Engine::get_singleton()->add_singleton(Engine::Singleton("ProjectSettings", ProjectSettings::get_singleton()));
	Engine::get_singleton()->add_singleton(Engine::Singleton("OS", CoreBind::OS::get_singleton()));
	Engine::get_singleton()->add_singleton(Engine::Singleton("Time", Time::get_singleton()));
	Engine::get_singleton()->add_singleton(Engine::Singleton("ClassDB", _classdb));
}

void register_core_singletons() {
	OS::get_singleton()->benchmark_begin_measure("Core", "Register Singletons");

	Engine::get_singleton()->add_singleton(Engine::Singleton("IP", IP::get_singleton(), "IP"));
	Engine::get_singleton()->add_singleton(Engine::Singleton("Geometry2D", CoreBind::Geometry2D::get_singleton()));
	Engine::get_singleton()->add_singleton(Engine::Singleton("Geometry3D", CoreBind::Geometry3D::get_singleton()));
	Engine::get_singleton()->add_singleton(Engine::Singleton("ResourceLoader", CoreBind::ResourceLoader::get_singleton()));
	Engine::get_singleton()->add_singleton(Engine::Singleton("ResourceSaver", CoreBind::ResourceSaver::get_singleton()));
	Engine::get_singleton()->add_singleton(Engine::Singleton("Marshalls", CoreBind::Marshalls::get_singleton()));
	Engine::get_singleton()->add_singleton(Engine::Singleton("TranslationServer", TranslationServer::get_singleton()));
	Engine::get_singleton()->add_singleton(Engine::Singleton("Input", Input::get_singleton()));
	Engine::get_singleton()->add_singleton(Engine::Singleton("InputMap", InputMap::get_singleton()));
	Engine::get_singleton()->add_singleton(Engine::Singleton("EngineDebugger", CoreBind::EngineDebugger::get_singleton()));
	Engine::get_singleton()->add_singleton(Engine::Singleton("FoundryExtensionManager", FoundryExtensionManager::get_singleton()));
	Engine::get_singleton()->add_singleton(Engine::Singleton("ResourceUID", ResourceUID::get_singleton()));
	Engine::get_singleton()->add_singleton(Engine::Singleton("WorkerThreadPool", worker_thread_pool));

	OS::get_singleton()->benchmark_end_measure("Core", "Register Singletons");
}

void register_core_extensions() {
	OS::get_singleton()->benchmark_begin_measure("Core", "Register Extensions");

	// Hardcoded for now.
	FoundryExtension::initialize_foundry_extensions();
	foundry_extension_manager->load_extensions();
	foundry_extension_manager->initialize_extensions(FoundryExtension::INITIALIZATION_LEVEL_CORE);
	_is_core_extensions_registered = true;

	OS::get_singleton()->benchmark_end_measure("Core", "Register Extensions");
}

void unregister_core_extensions() {
	OS::get_singleton()->benchmark_begin_measure("Core", "Unregister Extensions");

	if (_is_core_extensions_registered) {
		foundry_extension_manager->deinitialize_extensions(FoundryExtension::INITIALIZATION_LEVEL_CORE);
	}
	FoundryExtension::finalize_foundry_extensions();

	OS::get_singleton()->benchmark_end_measure("Core", "Unregister Extensions");
}

void unregister_core_types() {
	OS::get_singleton()->benchmark_begin_measure("Core", "Unregister Types");

	// Destroy singletons in reverse order to ensure dependencies are not broken.

	memdelete(worker_thread_pool);

	memdelete(_engine_debugger);
	memdelete(_marshalls);
	memdelete(_classdb);
	memdelete(_engine);
	memdelete(_os);
	memdelete(_resource_saver);
	memdelete(_resource_loader);

	memdelete(_geometry_3d);
	memdelete(_geometry_2d);

	memdelete(foundry_extension_manager);

	memdelete(resource_uid);

	if (ip) {
		memdelete(ip);
	}

	if constexpr (GD_IS_CLASS_ENABLED(Image)) {
		ResourceLoader::remove_resource_format_loader(resource_format_image);
		resource_format_image.unref();
	}

	ResourceSaver::remove_resource_format_saver(resource_saver_binary);
	resource_saver_binary.unref();

	ResourceLoader::remove_resource_format_loader(resource_loader_binary);
	resource_loader_binary.unref();

	ResourceLoader::remove_resource_format_loader(resource_format_importer);
	resource_format_importer.unref();

	ResourceSaver::remove_resource_format_saver(resource_format_importer_saver);
	resource_format_importer_saver.unref();

	if constexpr (GD_IS_CLASS_ENABLED(Translation)) {
		ResourceLoader::remove_resource_format_loader(resource_format_po);
		resource_format_po.unref();
	}

	if constexpr (GD_IS_CLASS_ENABLED(Crypto)) {
		ResourceSaver::remove_resource_format_saver(resource_format_saver_crypto);
		resource_format_saver_crypto.unref();

		ResourceLoader::remove_resource_format_loader(resource_format_loader_crypto);
		resource_format_loader_crypto.unref();
	}

	if constexpr (GD_IS_CLASS_ENABLED(JSON)) {
		ResourceSaver::remove_resource_format_saver(resource_saver_json);
		resource_saver_json.unref();

		ResourceLoader::remove_resource_format_loader(resource_loader_json);
		resource_loader_json.unref();
	}

	if constexpr (GD_IS_CLASS_ENABLED(FoundryExtension)) {
		ResourceLoader::remove_resource_format_loader(resource_loader_foundry_extension);
		resource_loader_foundry_extension.unref();
	}

	ResourceLoader::finalize();

	ClassDB::cleanup_defaults();
	memdelete(_time);
	ObjectDB::cleanup();

	Variant::unregister_types();

	unregister_global_constants();

	ResourceCache::clear();
	ClassDB::cleanup();
	CoreStringNames::free();
	StringName::cleanup();

	FileAccessEncrypted::deinitialize();

	OS::get_singleton()->benchmark_end_measure("Core", "Unregister Types");
}
