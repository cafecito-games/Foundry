/**************************************************************************/
/*  fs_build_pipeline_runner.h                                            */
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

#pragma once

#include "core/config/project_build_pipeline_config.h"
#include "core/config/project_build_pipeline_status.h"

class FoundryBuildPipelineRunner {
public:
	enum RunMode {
		RUN_AUTOMATIC_DIRTY_TASKS,
		RUN_DIRTY_TASKS,
		RUN_ALL_TASKS,
	};

	typedef void (*TaskOutputCallback)(void *p_userdata, const PackedStringArray &p_outputs);

	struct OutputCallback {
		TaskOutputCallback callback;
		void *userdata;

		OutputCallback(TaskOutputCallback p_callback = nullptr, void *p_userdata = nullptr) :
				callback(p_callback),
				userdata(p_userdata) {}

		bool is_valid() const { return callback != nullptr; }
		void call(const PackedStringArray &p_outputs) const {
			if (callback != nullptr) {
				callback(userdata, p_outputs);
			}
		}
	};

	struct StageRunResult {
		ProjectBuildPipelineStatusSnapshot snapshot;
		bool ran_any_task = false;
		Error error = OK;

		bool is_success() const;
		bool blocks_flow() const;
	};

	static ProjectBuildPipelineStatusSnapshot get_stage_status(
			ProjectBuildPipelineConfig::Stage p_stage, bool p_compute_current_fingerprints = true);
	static StageRunResult run_stage(ProjectBuildPipelineConfig::Stage p_stage,
			RunMode p_mode = RUN_DIRTY_TASKS, const OutputCallback &p_output_callback = OutputCallback());
	static bool status_blocks_flow(const ProjectBuildPipelineStatusSnapshot &p_snapshot);
	static void print_diagnostics(const ProjectBuildPipelineStatusSnapshot &p_snapshot, const String &p_context);
};
