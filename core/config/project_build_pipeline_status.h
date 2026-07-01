/**************************************************************************/
/*  project_build_pipeline_status.h                                       */
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

#include "core/config/foundry_build_task_registry.h"
#include "core/config/project_build_pipeline_config.h"
#include "core/config/project_build_state.h"
#include "core/error/error_list.h"
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"

class ProjectBuildTrustStore {
	String trust_path;
	bool project_trusted = false;
	Error load_error = OK;

	static bool cli_trusted_execution;
	static String _default_trust_path();

public:
	ProjectBuildTrustStore();
	explicit ProjectBuildTrustStore(const String &p_trust_path);

	String get_trust_path() const { return trust_path; }
	Error get_load_error() const { return load_error; }

	Error load();
	Error save() const;

	void set_project_trusted(bool p_trusted) { project_trusted = p_trusted; }
	bool is_project_trusted() const { return project_trusted || cli_trusted_execution; }

	static void set_cli_trusted_execution(bool p_trusted);
	static bool is_cli_trusted_execution();
};

struct ProjectBuildPipelineDiagnostic {
	enum Kind {
		KIND_TRUST,
		KIND_DIRTY,
		KIND_PROVIDER,
		KIND_TASK,
	};

	Kind kind = KIND_TASK;
	String task_name;
	String provider_id;
	String provider_source_type;
	String provider_source_identifier;
	String provider_source_path;
	String provider_source_section;
	String provider_source_key;
	String command;
	int exit_code = 0;
	String stdout_tail;
	String stderr_tail;
	String message;
	String file;
	int line = 0;
	int column = 0;
	ProjectBuildState::DirtyReason dirty_reason = ProjectBuildState::DIRTY_NONE;

	Dictionary to_dictionary() const;
};

struct ProjectBuildPipelineStatusSnapshot {
	ProjectBuildPipelineStatusSnapshot();

	ProjectBuildPipelineStatusSnapshot(const ProjectBuildPipelineStatusSnapshot &p_other) = default;
	ProjectBuildPipelineStatusSnapshot &operator=(const ProjectBuildPipelineStatusSnapshot &p_other) = default;

	int state = 0;
	bool blocks_downstream_indexing = false;
	Vector<ProjectBuildPipelineDiagnostic> diagnostics;
};

class ProjectBuildPipelineStatus {
public:
	enum State {
		STATE_DISABLED,
		STATE_UNTRUSTED,
		STATE_CLEAN,
		STATE_DIRTY,
		STATE_RUNNING,
		STATE_BLOCKED,
	};

	static ProjectBuildPipelineStatusSnapshot evaluate(const ProjectBuildPipelineConfig &p_config,
			const FoundryBuildTaskRegistry &p_registry, const ProjectBuildState &p_state,
			const ProjectBuildTrustStore &p_trust, bool p_running = false,
			const PackedStringArray &p_current_fingerprints = PackedStringArray(),
			const Vector<FoundryBuildTaskRegistry::Diagnostic> &p_provider_diagnostics =
					Vector<FoundryBuildTaskRegistry::Diagnostic>());
};
