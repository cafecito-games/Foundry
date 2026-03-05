/**************************************************************************/
/*  register_server_types.cpp                                             */
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

#include "register_server_types.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"

#include "audio/audio_effect.h"
#include "audio/audio_server.h"
#include "audio/audio_stream.h"
#include "audio/effects/audio_effect_amplify.h"
#include "audio/effects/audio_effect_capture.h"
#include "audio/effects/audio_effect_chorus.h"
#include "audio/effects/audio_effect_compressor.h"
#include "audio/effects/audio_effect_delay.h"
#include "audio/effects/audio_effect_distortion.h"
#include "audio/effects/audio_effect_eq.h"
#include "audio/effects/audio_effect_filter.h"
#include "audio/effects/audio_effect_hard_limiter.h"
#include "audio/effects/audio_effect_panner.h"
#include "audio/effects/audio_effect_phaser.h"
#include "audio/effects/audio_effect_pitch_shift.h"
#include "audio/effects/audio_effect_record.h"
#include "audio/effects/audio_effect_reverb.h"
#include "audio/effects/audio_effect_spectrum_analyzer.h"
#include "audio/effects/audio_effect_stereo_enhance.h"
#include "audio/effects/audio_stream_generator.h"
#include "camera/camera_feed.h"
#include "camera/camera_server.h"
#include "debugger/servers_debugger.h"
#include "display/display_server.h"
#include "display/native_menu.h"
#include "movie_writer/movie_writer.h"
#include "movie_writer/movie_writer_pngwav.h"
#include "rendering/renderer_rd/framebuffer_cache_rd.h"
#include "rendering/renderer_rd/storage_rd/render_data_rd.h"
#include "rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"
#include "rendering/renderer_rd/storage_rd/render_scene_data_rd.h"
#include "rendering/renderer_rd/uniform_set_cache_rd.h"
#include "rendering/rendering_device.h"
#include "rendering/rendering_device_binds.h"
#include "rendering/rendering_server.h"
#include "rendering/shader_include_db.h"
#include "rendering/storage/render_data.h"
#include "rendering/storage/render_scene_buffers.h"
#include "rendering/storage/render_scene_data.h"
#include "servers/rendering/shader_types.h"
#include "text/text_server.h"
#include "text/text_server_dummy.h"
#include "text/text_server_extension.h"

// 2D physics and navigation.
#ifndef NAVIGATION_2D_DISABLED
#include "servers/navigation_2d/navigation_server_2d.h"
#endif // NAVIGATION_2D_DISABLED
#ifndef PHYSICS_2D_DISABLED
#include "servers/physics_2d/physics_server_2d.h"
#include "servers/physics_2d/physics_server_2d_dummy.h"
#include "servers/physics_2d/physics_server_2d_extension.h"
#endif // PHYSICS_2D_DISABLED

// 3D physics and navigation.
#ifndef NAVIGATION_3D_DISABLED
#include "servers/navigation_3d/navigation_server_3d.h"
#endif // NAVIGATION_3D_DISABLED
#ifndef PHYSICS_3D_DISABLED
#include "servers/physics_3d/physics_server_3d.h"
#include "servers/physics_3d/physics_server_3d_dummy.h"
#include "servers/physics_3d/physics_server_3d_extension.h"
#endif // PHYSICS_3D_DISABLED
#ifndef XR_DISABLED
#include "xr/xr_body_tracker.h"
#include "xr/xr_controller_tracker.h"
#include "xr/xr_face_tracker.h"
#include "xr/xr_hand_tracker.h"
#include "xr/xr_interface.h"
#include "xr/xr_interface_extension.h"
#include "xr/xr_positional_tracker.h"
#include "xr/xr_server.h"
#endif // XR_DISABLED

ShaderTypes *shader_types = nullptr;

#ifndef PHYSICS_2D_DISABLED
static PhysicsServer2D *_create_dummy_physics_server_2d() {
	return memnew(PhysicsServer2DDummy);
}
#endif // PHYSICS_2D_DISABLED

#ifndef PHYSICS_3D_DISABLED
static PhysicsServer3D *_create_dummy_physics_server_3d() {
	return memnew(PhysicsServer3DDummy);
}
#endif // PHYSICS_3D_DISABLED

static bool has_server_feature_callback(const String &p_feature) {
	if (RenderingServer::get_singleton()) {
		if (RenderingServer::get_singleton()->has_os_feature(p_feature)) {
			return true;
		}
	}

	return false;
}

static MovieWriterPNGWAV *writer_pngwav = nullptr;

void register_server_types() {
	OS::get_singleton()->benchmark_begin_measure("Servers", "Register Extensions");

	shader_types = memnew(ShaderTypes);

	FOUNDRY_REGISTER_CLASS(TextServerManager);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(TextServer);
	FOUNDRY_REGISTER_CLASS(TextServerExtension);
	FOUNDRY_REGISTER_CLASS(TextServerDummy);

	FOUNDRY_REGISTER_NATIVE_STRUCT(Glyph, "int start = -1;int end = -1;uint8_t count = 0;uint8_t repeat = 1;uint16_t flags = 0;float x_off = 0.f;float y_off = 0.f;float advance = 0.f;RID font_rid;int font_size = 0;int32_t index = 0");
	FOUNDRY_REGISTER_NATIVE_STRUCT(CaretInfo, "Rect2 leading_caret;Rect2 trailing_caret;TextServer::Direction leading_direction;TextServer::Direction trailing_direction");

	Engine::get_singleton()->add_singleton(Engine::Singleton("TextServerManager", TextServerManager::get_singleton(), "TextServerManager"));

	OS::get_singleton()->set_has_server_feature_callback(has_server_feature_callback);

	FOUNDRY_REGISTER_ABSTRACT_CLASS(DisplayServer);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(RenderingServer);
	FOUNDRY_REGISTER_CLASS(AudioServer);

	FOUNDRY_REGISTER_CLASS(NativeMenu);

	FOUNDRY_REGISTER_CLASS(CameraServer);

	FOUNDRY_REGISTER_ABSTRACT_CLASS(RenderingDevice);

	FOUNDRY_REGISTER_CLASS(AudioStream);
	FOUNDRY_REGISTER_CLASS(AudioStreamPlayback);
	FOUNDRY_REGISTER_VIRTUAL_CLASS(AudioStreamPlaybackResampled);
	FOUNDRY_REGISTER_CLASS(AudioStreamMicrophone);
	FOUNDRY_REGISTER_CLASS(AudioStreamRandomizer);
	FOUNDRY_REGISTER_CLASS(AudioSample);
	FOUNDRY_REGISTER_CLASS(AudioSamplePlayback);
	FOUNDRY_REGISTER_VIRTUAL_CLASS(AudioEffect);
	FOUNDRY_REGISTER_VIRTUAL_CLASS(AudioEffectInstance);
	FOUNDRY_REGISTER_CLASS(AudioEffectEQ);
	FOUNDRY_REGISTER_CLASS(AudioEffectFilter);
	FOUNDRY_REGISTER_CLASS(AudioBusLayout);

	FOUNDRY_REGISTER_CLASS(AudioStreamGenerator);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(AudioStreamGeneratorPlayback);

	{
		//audio effects
		FOUNDRY_REGISTER_CLASS(AudioEffectAmplify);

		FOUNDRY_REGISTER_CLASS(AudioEffectReverb);

		FOUNDRY_REGISTER_CLASS(AudioEffectLowPassFilter);
		FOUNDRY_REGISTER_CLASS(AudioEffectHighPassFilter);
		FOUNDRY_REGISTER_CLASS(AudioEffectBandPassFilter);
		FOUNDRY_REGISTER_CLASS(AudioEffectNotchFilter);
		FOUNDRY_REGISTER_CLASS(AudioEffectBandLimitFilter);
		FOUNDRY_REGISTER_CLASS(AudioEffectLowShelfFilter);
		FOUNDRY_REGISTER_CLASS(AudioEffectHighShelfFilter);

		FOUNDRY_REGISTER_CLASS(AudioEffectEQ6);
		FOUNDRY_REGISTER_CLASS(AudioEffectEQ10);
		FOUNDRY_REGISTER_CLASS(AudioEffectEQ21);

		FOUNDRY_REGISTER_CLASS(AudioEffectDistortion);

		FOUNDRY_REGISTER_CLASS(AudioEffectStereoEnhance);

		FOUNDRY_REGISTER_CLASS(AudioEffectPanner);
		FOUNDRY_REGISTER_CLASS(AudioEffectChorus);
		FOUNDRY_REGISTER_CLASS(AudioEffectDelay);
		FOUNDRY_REGISTER_CLASS(AudioEffectCompressor);
		FOUNDRY_REGISTER_CLASS(AudioEffectHardLimiter);
		FOUNDRY_REGISTER_CLASS(AudioEffectPitchShift);
		FOUNDRY_REGISTER_CLASS(AudioEffectPhaser);
		FOUNDRY_REGISTER_CLASS(AudioEffectRecord);
		FOUNDRY_REGISTER_CLASS(AudioEffectSpectrumAnalyzer);
		FOUNDRY_REGISTER_ABSTRACT_CLASS(AudioEffectSpectrumAnalyzerInstance);

		FOUNDRY_REGISTER_CLASS(AudioEffectCapture);
	}

	FOUNDRY_REGISTER_ABSTRACT_CLASS(RenderingDevice);
	FOUNDRY_REGISTER_CLASS(ShaderIncludeDB);
	FOUNDRY_REGISTER_CLASS(RDTextureFormat);
	FOUNDRY_REGISTER_CLASS(RDTextureView);
	FOUNDRY_REGISTER_CLASS(RDAttachmentFormat);
	FOUNDRY_REGISTER_CLASS(RDFramebufferPass);
	FOUNDRY_REGISTER_CLASS(RDSamplerState);
	FOUNDRY_REGISTER_CLASS(RDVertexAttribute);
	FOUNDRY_REGISTER_CLASS(RDUniform);
	FOUNDRY_REGISTER_CLASS(RDPipelineRasterizationState);
	FOUNDRY_REGISTER_CLASS(RDPipelineMultisampleState);
	FOUNDRY_REGISTER_CLASS(RDPipelineDepthStencilState);
	FOUNDRY_REGISTER_CLASS(RDPipelineColorBlendStateAttachment);
	FOUNDRY_REGISTER_CLASS(RDPipelineColorBlendState);
	FOUNDRY_REGISTER_CLASS(RDShaderSource);
	FOUNDRY_REGISTER_CLASS(RDShaderSPIRV);
	FOUNDRY_REGISTER_CLASS(RDShaderFile);
	FOUNDRY_REGISTER_CLASS(RDPipelineSpecializationConstant);
	FOUNDRY_REGISTER_CLASS(RDAccelerationStructureGeometry);
	FOUNDRY_REGISTER_CLASS(RDAccelerationStructureInstance);

	FOUNDRY_REGISTER_ABSTRACT_CLASS(RenderData);
	FOUNDRY_REGISTER_CLASS(RenderDataExtension);
	FOUNDRY_REGISTER_CLASS(RenderDataRD);

	FOUNDRY_REGISTER_ABSTRACT_CLASS(RenderSceneData);
	FOUNDRY_REGISTER_CLASS(RenderSceneDataExtension);
	FOUNDRY_REGISTER_CLASS(RenderSceneDataRD);

	FOUNDRY_REGISTER_CLASS(RenderSceneBuffersConfiguration);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(RenderSceneBuffers);
	FOUNDRY_REGISTER_CLASS(RenderSceneBuffersExtension);
	FOUNDRY_REGISTER_CLASS(RenderSceneBuffersRD);

	FOUNDRY_REGISTER_CLASS(FramebufferCacheRD);
	FOUNDRY_REGISTER_CLASS(UniformSetCacheRD);

	FOUNDRY_REGISTER_CLASS(CameraFeed);

	FOUNDRY_REGISTER_VIRTUAL_CLASS(MovieWriter);

	ServersDebugger::initialize();

#ifndef NAVIGATION_2D_DISABLED
	FOUNDRY_REGISTER_CLASS(NavigationServer2DManager);
	Engine::get_singleton()->add_singleton(Engine::Singleton("NavigationServer2DManager", NavigationServer2DManager::get_singleton(), "NavigationServer2DManager"));

	FOUNDRY_REGISTER_ABSTRACT_CLASS(NavigationServer2D);
	FOUNDRY_REGISTER_CLASS(NavigationPathQueryParameters2D);
	FOUNDRY_REGISTER_CLASS(NavigationPathQueryResult2D);

	GLOBAL_DEF(PropertyInfo(Variant::STRING, NavigationServer2DManager::setting_property_name, PROPERTY_HINT_ENUM, "DEFAULT"), "DEFAULT");

	NavigationServer2DManager::get_singleton()->register_server("Dummy", callable_mp_static(NavigationServer2DManager::create_dummy_server_callback));
#endif // NAVIGATION_2D_DISABLED

#ifndef PHYSICS_2D_DISABLED
	// Physics 2D
	FOUNDRY_REGISTER_CLASS(PhysicsServer2DManager);
	Engine::get_singleton()->add_singleton(Engine::Singleton("PhysicsServer2DManager", PhysicsServer2DManager::get_singleton(), "PhysicsServer2DManager"));

	FOUNDRY_REGISTER_ABSTRACT_CLASS(PhysicsServer2D);
	FOUNDRY_REGISTER_VIRTUAL_CLASS(PhysicsServer2DExtension);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(PhysicsDirectBodyState2D);
	FOUNDRY_REGISTER_VIRTUAL_CLASS(PhysicsDirectBodyState2DExtension);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(PhysicsDirectSpaceState2D);
	FOUNDRY_REGISTER_VIRTUAL_CLASS(PhysicsDirectSpaceState2DExtension);

	FOUNDRY_REGISTER_NATIVE_STRUCT(PhysicsServer2DExtensionRayResult, "Vector2 position;Vector2 normal;RID rid;ObjectID collider_id;Object *collider;int shape");
	FOUNDRY_REGISTER_NATIVE_STRUCT(PhysicsServer2DExtensionShapeResult, "RID rid;ObjectID collider_id;Object *collider;int shape");
	FOUNDRY_REGISTER_NATIVE_STRUCT(PhysicsServer2DExtensionShapeRestInfo, "Vector2 point;Vector2 normal;RID rid;ObjectID collider_id;int shape;Vector2 linear_velocity");
	FOUNDRY_REGISTER_NATIVE_STRUCT(PhysicsServer2DExtensionMotionResult, "Vector2 travel;Vector2 remainder;Vector2 collision_point;Vector2 collision_normal;Vector2 collider_velocity;real_t collision_depth;real_t collision_safe_fraction;real_t collision_unsafe_fraction;int collision_local_shape;ObjectID collider_id;RID collider;int collider_shape");

	FOUNDRY_REGISTER_CLASS(PhysicsRayQueryParameters2D);
	FOUNDRY_REGISTER_CLASS(PhysicsPointQueryParameters2D);
	FOUNDRY_REGISTER_CLASS(PhysicsShapeQueryParameters2D);
	FOUNDRY_REGISTER_CLASS(PhysicsTestMotionParameters2D);
	FOUNDRY_REGISTER_CLASS(PhysicsTestMotionResult2D);

	GLOBAL_DEF(PropertyInfo(Variant::STRING, PhysicsServer2DManager::setting_property_name, PROPERTY_HINT_ENUM, "DEFAULT"), "DEFAULT");

	PhysicsServer2DManager::get_singleton()->register_server("Dummy", callable_mp_static(_create_dummy_physics_server_2d));
#endif // PHYSICS_2D_DISABLED

#ifndef NAVIGATION_3D_DISABLED
	FOUNDRY_REGISTER_CLASS(NavigationServer3DManager);
	Engine::get_singleton()->add_singleton(Engine::Singleton("NavigationServer3DManager", NavigationServer3DManager::get_singleton(), "NavigationServer3DManager"));

	FOUNDRY_REGISTER_ABSTRACT_CLASS(NavigationServer3D);
	FOUNDRY_REGISTER_CLASS(NavigationPathQueryParameters3D);
	FOUNDRY_REGISTER_CLASS(NavigationPathQueryResult3D);

	GLOBAL_DEF(PropertyInfo(Variant::STRING, NavigationServer3DManager::setting_property_name, PROPERTY_HINT_ENUM, "DEFAULT"), "DEFAULT");

	NavigationServer3DManager::get_singleton()->register_server("Dummy", callable_mp_static(NavigationServer3DManager::create_dummy_server_callback));
#endif // NAVIGATION_3D_DISABLED

#ifndef PHYSICS_3D_DISABLED
	// Physics 3D
	FOUNDRY_REGISTER_CLASS(PhysicsServer3DManager);
	Engine::get_singleton()->add_singleton(Engine::Singleton("PhysicsServer3DManager", PhysicsServer3DManager::get_singleton(), "PhysicsServer3DManager"));

	FOUNDRY_REGISTER_ABSTRACT_CLASS(PhysicsServer3D);
	FOUNDRY_REGISTER_VIRTUAL_CLASS(PhysicsServer3DExtension);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(PhysicsDirectBodyState3D);
	FOUNDRY_REGISTER_VIRTUAL_CLASS(PhysicsDirectBodyState3DExtension);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(PhysicsDirectSpaceState3D);
	FOUNDRY_REGISTER_VIRTUAL_CLASS(PhysicsDirectSpaceState3DExtension)
	FOUNDRY_REGISTER_VIRTUAL_CLASS(PhysicsServer3DRenderingServerHandler)

	FOUNDRY_REGISTER_NATIVE_STRUCT(PhysicsServer3DExtensionRayResult, "Vector3 position;Vector3 normal;RID rid;ObjectID collider_id;Object *collider;int shape;int face_index");
	FOUNDRY_REGISTER_NATIVE_STRUCT(PhysicsServer3DExtensionShapeResult, "RID rid;ObjectID collider_id;Object *collider;int shape");
	FOUNDRY_REGISTER_NATIVE_STRUCT(PhysicsServer3DExtensionShapeRestInfo, "Vector3 point;Vector3 normal;RID rid;ObjectID collider_id;int shape;Vector3 linear_velocity");
	FOUNDRY_REGISTER_NATIVE_STRUCT(PhysicsServer3DExtensionMotionCollision, "Vector3 position;Vector3 normal;Vector3 collider_velocity;Vector3 collider_angular_velocity;real_t depth;int local_shape;ObjectID collider_id;RID collider;int collider_shape");
	FOUNDRY_REGISTER_NATIVE_STRUCT(PhysicsServer3DExtensionMotionResult, "Vector3 travel;Vector3 remainder;real_t collision_depth;real_t collision_safe_fraction;real_t collision_unsafe_fraction;PhysicsServer3DExtensionMotionCollision collisions[32];int collision_count");

	FOUNDRY_REGISTER_CLASS(PhysicsRayQueryParameters3D);
	FOUNDRY_REGISTER_CLASS(PhysicsPointQueryParameters3D);
	FOUNDRY_REGISTER_CLASS(PhysicsShapeQueryParameters3D);
	FOUNDRY_REGISTER_CLASS(PhysicsTestMotionParameters3D);
	FOUNDRY_REGISTER_CLASS(PhysicsTestMotionResult3D);

	GLOBAL_DEF(PropertyInfo(Variant::STRING, PhysicsServer3DManager::setting_property_name, PROPERTY_HINT_ENUM, "DEFAULT"), "DEFAULT");

	PhysicsServer3DManager::get_singleton()->register_server("Dummy", callable_mp_static(_create_dummy_physics_server_3d));
#endif // PHYSICS_3D_DISABLED

#ifndef XR_DISABLED
	FOUNDRY_REGISTER_ABSTRACT_CLASS(XRInterface);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(XRTracker);
	FOUNDRY_REGISTER_CLASS(XRVRS);
	FOUNDRY_REGISTER_CLASS(XRPositionalTracker);
	FOUNDRY_REGISTER_CLASS(XRBodyTracker);
	FOUNDRY_REGISTER_CLASS(XRControllerTracker);
	FOUNDRY_REGISTER_CLASS(XRFaceTracker);
	FOUNDRY_REGISTER_CLASS(XRHandTracker);
	FOUNDRY_REGISTER_CLASS(XRInterfaceExtension); // can't register this as virtual because we need a creation function for our extensions.
	FOUNDRY_REGISTER_CLASS(XRPose);
	FOUNDRY_REGISTER_CLASS(XRServer);
#endif // XR_DISABLED

	if constexpr (GD_IS_CLASS_ENABLED(MovieWriterPNGWAV)) {
		writer_pngwav = memnew(MovieWriterPNGWAV);
		MovieWriter::add_writer(writer_pngwav);
	}

	OS::get_singleton()->benchmark_end_measure("Servers", "Register Extensions");
}

void unregister_server_types() {
	OS::get_singleton()->benchmark_begin_measure("Servers", "Unregister Extensions");

	ServersDebugger::deinitialize();
	memdelete(shader_types);
	if constexpr (GD_IS_CLASS_ENABLED(MovieWriterPNGWAV)) {
		memdelete(writer_pngwav);
	}

	OS::get_singleton()->benchmark_end_measure("Servers", "Unregister Extensions");
}

void register_server_singletons() {
	OS::get_singleton()->benchmark_begin_measure("Servers", "Register Singletons");

	Engine::get_singleton()->add_singleton(Engine::Singleton("AudioServer", AudioServer::get_singleton(), "AudioServer"));
	Engine::get_singleton()->add_singleton(Engine::Singleton("CameraServer", CameraServer::get_singleton(), "CameraServer"));
	Engine::get_singleton()->add_singleton(Engine::Singleton("DisplayServer", DisplayServer::get_singleton(), "DisplayServer"));
	Engine::get_singleton()->add_singleton(Engine::Singleton("NativeMenu", NativeMenu::get_singleton(), "NativeMenu"));
	Engine::get_singleton()->add_singleton(Engine::Singleton("RenderingServer", RenderingServer::get_singleton(), "RenderingServer"));
#ifndef NAVIGATION_2D_DISABLED
	Engine::get_singleton()->add_singleton(Engine::Singleton("NavigationServer2D", NavigationServer2D::get_singleton(), "NavigationServer2D"));
#endif // NAVIGATION_2D_DISABLED
#ifndef NAVIGATION_3D_DISABLED
	Engine::get_singleton()->add_singleton(Engine::Singleton("NavigationServer3D", NavigationServer3D::get_singleton(), "NavigationServer3D"));
#endif // NAVIGATION_3D_DISABLED
#ifndef PHYSICS_2D_DISABLED
	Engine::get_singleton()->add_singleton(Engine::Singleton("PhysicsServer2D", PhysicsServer2D::get_singleton(), "PhysicsServer2D"));
#endif // PHYSICS_2D_DISABLED
#ifndef PHYSICS_3D_DISABLED
	Engine::get_singleton()->add_singleton(Engine::Singleton("PhysicsServer3D", PhysicsServer3D::get_singleton(), "PhysicsServer3D"));
#endif // PHYSICS_3D_DISABLED
#ifndef XR_DISABLED
	Engine::get_singleton()->add_singleton(Engine::Singleton("XRServer", XRServer::get_singleton(), "XRServer"));
#endif // XR_DISABLED

	OS::get_singleton()->benchmark_end_measure("Servers", "Register Singletons");
}
