/**************************************************************************/
/*  test_renderer_shutdown.h                                             */
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
/* permit persons to whom the Software is furnished to do so, subject to   */
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

#include "core/input/input.h"
#include "servers/rendering/renderer_scene_cull.h"
#include "servers/rendering/renderer_viewport.h"
#include "servers/rendering/rendering_server.h"
#include "servers/rendering/rendering_server_default.h"
#include "servers/rendering/rendering_server_globals.h"

#include "tests/display_server_mock.h"
#include "tests/test_macros.h"
#include "tests/test_tools.h"

struct RendererShutdownLeakDetector {
	ErrorHandlerList handler;
	bool has_leak = false;

	static void on_error(void *p_userdata, const char *, const char *, int, const char *p_error, const char *, bool, ErrorHandlerType) {
		RendererShutdownLeakDetector *detector = static_cast<RendererShutdownLeakDetector *>(p_userdata);
		String message = p_error;
		detector->has_leak = detector->has_leak ||
				message.contains("RID allocations") ||
				message.contains("GeometryInstance") ||
				message.contains("SceneForwardClusteredShaderRD") ||
				message.contains("SceneShaderForwardClustered");
	}

	RendererShutdownLeakDetector() {
		handler.errfunc = on_error;
		handler.userdata = this;
		add_error_handler(&handler);
	}

	~RendererShutdownLeakDetector() {
		remove_error_handler(&handler);
	}
};

TEST_CASE("[Rendering] Renderer shutdown drains owned RIDs") {
	memnew(Input);

	Error err = OK;
	for (int i = 0; i < DisplayServer::get_create_function_count(); i++) {
		if (String("mock") == DisplayServer::get_create_function_name(i)) {
			DisplayServer::create(i, "", DisplayServer::WindowMode::WINDOW_MODE_MINIMIZED, DisplayServer::VSyncMode::VSYNC_ENABLED, 0, nullptr, Vector2i(0, 0), DisplayServer::SCREEN_PRIMARY, DisplayServer::CONTEXT_EDITOR, 0, err);
			break;
		}
	}
	REQUIRE(err == OK);
	REQUIRE(DisplayServer::get_singleton() != nullptr);

	RenderingServerDefault *rendering_server = memnew(RenderingServerDefault);
	rendering_server->init();

	RID viewport = rendering_server->viewport_create();
	RID camera = rendering_server->camera_create();
	RID scenario = rendering_server->scenario_create();
	RID mesh = rendering_server->make_sphere_mesh(4, 4, 1.0);
	RID instance = rendering_server->instance_create();

	rendering_server->viewport_attach_camera(viewport, camera);
	rendering_server->viewport_set_scenario(viewport, scenario);
	rendering_server->instance_set_base(instance, mesh);
	rendering_server->instance_set_scenario(instance, scenario);
	rendering_server->sync();

	RendererSceneCull *scene = static_cast<RendererSceneCull *>(RSG::scene);
	CHECK_EQ(RSG::viewport->viewport_owner.get_rid_count(), 1);
	CHECK_EQ(scene->camera_owner.get_rid_count(), 1);
	CHECK_EQ(scene->scenario_owner.get_rid_count(), 1);
	CHECK_EQ(scene->instance_owner.get_rid_count(), 1);

	RSG::viewport->finalize();
	scene->finalize();

	CHECK_EQ(RSG::viewport->viewport_owner.get_rid_count(), 0);
	CHECK_EQ(scene->camera_owner.get_rid_count(), 0);
	CHECK_EQ(scene->scenario_owner.get_rid_count(), 0);
	CHECK_EQ(scene->instance_owner.get_rid_count(), 0);

	rendering_server->free_rid(mesh);

	RID shutdown_viewport = rendering_server->viewport_create();
	RID shutdown_camera = rendering_server->camera_create();
	RID shutdown_scenario = rendering_server->scenario_create();
	RID shutdown_instance = rendering_server->instance_create();
	rendering_server->viewport_attach_camera(shutdown_viewport, shutdown_camera);
	rendering_server->viewport_set_scenario(shutdown_viewport, shutdown_scenario);
	rendering_server->instance_set_scenario(shutdown_instance, shutdown_scenario);
	rendering_server->sync();

	CHECK_EQ(RSG::viewport->viewport_owner.get_rid_count(), 1);
	CHECK_EQ(scene->camera_owner.get_rid_count(), 1);
	CHECK_EQ(scene->scenario_owner.get_rid_count(), 1);
	CHECK_EQ(scene->instance_owner.get_rid_count(), 1);

	RendererShutdownLeakDetector errors;
	rendering_server->finish();
	memdelete(rendering_server);
	CHECK_FALSE(errors.has_leak);
}
