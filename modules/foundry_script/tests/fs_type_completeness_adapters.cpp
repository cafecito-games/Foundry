/**************************************************************************/
/*  fs_type_completeness_adapters.cpp                                     */
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

#include "fs_type_completeness_adapter.h"

#include "fs_type_completeness_destination_wrapper_adapter.h"
#include "fs_type_completeness_lifecycle_adapter.h"
#include "fs_type_completeness_tooling_adapter.h"

namespace FSTests {

namespace {

Vector<String> sorted_axes(const HashMap<String, Vector<String>> &p_leaves) {
	Vector<String> axes;
	for (const KeyValue<String, Vector<String>> &entry : p_leaves) {
		axes.push_back(entry.key);
	}
	axes.sort();
	return axes;
}

// A family with no product surface at all: every program is its own expected output and both surfaces
// observe the same thing. It exists so the runner's family-independent half - registry lookup,
// domain-derived cardinality, declarative witnesses, parity, reporting - is exercised by a second
// family without a second body of product-specific rendering. It is registered like any other adapter
// and is only reachable from a staged catalog, so nothing tracked can select it.
class FSSyntheticPairIdentityAdapter : public FSCompletenessFamilyAdapter {
public:
	static const FSSyntheticPairIdentityAdapter &shared() {
		static FSSyntheticPairIdentityAdapter adapter;
		return adapter;
	}

	String id() const override { return "synthetic_pair_identity"; }

	HashSet<String> observable_dimensions() const override {
		return HashSet<String>({ "synthetic_identity" });
	}

	HashMap<String, Vector<String>> renderable_leaves() const override {
		HashMap<String, Vector<String>> leaves;
		leaves["synthetic_shape"] = Vector<String>({ "scalar", "pair", "triple", "quadruple" });
		leaves["surface"] = Vector<String>({ "text", "bytecode" });
		return leaves;
	}

	Error render(const FSCompletenessResolvedCell &p_cell, FSCompletenessProgram &r_program) const override {
		r_program = FSCompletenessProgram();
		String shape;
		String surface;
		if (p_cell.case_id.is_empty() || !read_coordinate(p_cell.coordinates, "synthetic_shape", shape) ||
				!read_coordinate(p_cell.coordinates, "surface", surface)) {
			return ERR_INVALID_DATA;
		}
		FSCompletenessProgram program;
		program.case_id = p_cell.case_id;
		program.surface = surface;
		program.coordinates = p_cell.coordinates.duplicate();
		program.source = vformat("func test() -> void:\n\tprint(\"synthetic %s\")\n", shape);
		program.expected_output = vformat("synthetic %s\n", shape);
		r_program = program;
		return OK;
	}

	FSCompletenessObservation analyze(
			const FSCompletenessProgram &p_program, const String &p_surface) const override {
		FSCompletenessObservation observation;
		observation.case_id = p_program.case_id;
		observation.surface = p_surface;
		if (p_program.surface != p_surface) {
			observation.diagnostics.push_back(vformat(
					"Program surface '%s' does not match observed surface '%s'.", p_program.surface, p_surface));
			return observation;
		}
		observation.dimensions["synthetic_identity"] = "identical";
		observation.produced_output = p_program.expected_output;
		return observation;
	}

	Error execute(const String &p_scratch_root, const Vector<FSCompletenessProgram> &p_programs,
			FSCompletenessRuntimeBatch &r_batch) const override {
		r_batch = FSCompletenessRuntimeBatch();
		if (p_scratch_root.is_empty()) {
			return ERR_INVALID_PARAMETER;
		}
		FSCompletenessRuntimeBatch completed;
		for (const FSCompletenessProgram &program : p_programs) {
			HashMap<String, FSCompletenessRuntimeResult> *results =
					completed.results_for_surface(program.surface);
			if (results == nullptr || results->has(program.case_id)) {
				return ERR_INVALID_DATA;
			}
			FSCompletenessRuntimeResult result;
			static_cast<FSCompletenessObservation &>(result) = analyze(program, program.surface);
			result.passed = result.diagnostics.is_empty();
			result.status = result.passed ? "ok" : "analysis_rejected";
			results->insert(program.case_id, result);
		}
		r_batch = completed;
		return OK;
	}

private:
	bool read_coordinate(const Dictionary &p_coordinates, const String &p_axis, String &r_value) const {
		const Variant value = p_coordinates.get(p_axis, Variant());
		if (value.get_type() != Variant::STRING) {
			return false;
		}
		r_value = value;
		return can_render(p_axis, r_value);
	}
};

const Vector<const FSCompletenessFamilyAdapter *> &builtin_adapters() {
	static const Vector<const FSCompletenessFamilyAdapter *> table = {
		&FSDestinationWrapperAdapter::shared(),
		&FSLifecycleAdapter::shared(),
#ifdef FS_COMPLETENESS_TOOLING_ADAPTER_AVAILABLE
		// Registered only where the editor tooling surfaces it drives are compiled in. A build without
		// them declares the id and covers no tooling cell rather than pretending to observe one.
		&FSToolingAdapter::shared(),
#endif
		&FSSyntheticPairIdentityAdapter::shared(),
	};
	return table;
}

} // namespace

FSCompletenessSurfaceEvidenceMismatch compare_surface_evidence(
		const FSCompletenessRuntimeResult &p_text, const FSCompletenessRuntimeResult &p_bytecode) {
	FSCompletenessSurfaceEvidenceMismatch mismatch;
	mismatch.produced_output = p_text.produced_output != p_bytecode.produced_output;
	mismatch.diagnostics = p_text.diagnostics != p_bytecode.diagnostics;
	mismatch.diagnostic_records = p_text.diagnostic_records != p_bytecode.diagnostic_records;
	mismatch.runtime_status = p_text.passed != p_bytecode.passed || p_text.status != p_bytecode.status;
	return mismatch;
}

HashMap<String, FSCompletenessRuntimeResult> *FSCompletenessRuntimeBatch::results_for_surface(
		const String &p_surface) {
	if (p_surface == "text") {
		return &text;
	}
	return p_surface == "bytecode" ? &bytecode : nullptr;
}

const HashMap<String, FSCompletenessRuntimeResult> *FSCompletenessRuntimeBatch::results_for_surface(
		const String &p_surface) const {
	if (p_surface == "text") {
		return &text;
	}
	return p_surface == "bytecode" ? &bytecode : nullptr;
}

Error validate_declared_coordinates(const String &p_adapter_id,
		const HashMap<String, Vector<String>> &p_leaves, const FSCompletenessCatalog &p_catalog,
		Vector<String> &r_errors) {
	const int error_count_before = r_errors.size();
	if (p_leaves.is_empty()) {
		r_errors.push_back(vformat("adapter '%s' renders no coordinates", p_adapter_id));
	}
	for (const String &axis : sorted_axes(p_leaves)) {
		if (axis.is_empty()) {
			r_errors.push_back(vformat("adapter '%s' declares an empty axis", p_adapter_id));
			continue;
		}
		if (!p_catalog.has_axis(axis)) {
			r_errors.push_back(vformat("adapter '%s' renders unknown axis '%s'", p_adapter_id, axis));
			continue;
		}
		HashSet<String> seen;
		for (const String &leaf : p_leaves[axis]) {
			if (leaf.is_empty()) {
				r_errors.push_back(
						vformat("adapter '%s' declares an empty leaf on axis '%s'", p_adapter_id, axis));
				continue;
			}
			if (seen.has(leaf)) {
				r_errors.push_back(vformat(
						"adapter '%s' declares leaf '%s' twice on axis '%s'", p_adapter_id, leaf, axis));
				continue;
			}
			seen.insert(leaf);
		}
	}
	return r_errors.size() == error_count_before ? OK : ERR_INVALID_DATA;
}

Error FSCompletenessFamilyAdapter::renderable_coordinates(
		const FSCompletenessCatalog &p_catalog, Vector<String> &r_errors) const {
	return validate_declared_coordinates(id(), renderable_leaves(), p_catalog, r_errors);
}

bool FSCompletenessFamilyAdapter::can_render(const String &p_axis, const String &p_leaf) const {
	const HashMap<String, Vector<String>> leaves = renderable_leaves();
	const Vector<String> *axis_leaves = leaves.getptr(p_axis);
	return axis_leaves != nullptr && axis_leaves->has(p_leaf);
}

const FSCompletenessFamilyAdapter *FSCompletenessAdapterRegistry::find(const String &p_adapter_id) {
	if (p_adapter_id.is_empty()) {
		return nullptr;
	}
	for (const FSCompletenessFamilyAdapter *adapter : builtin_adapters()) {
		if (adapter != nullptr && adapter->id() == p_adapter_id) {
			return adapter;
		}
	}
	return nullptr;
}

Vector<String> FSCompletenessAdapterRegistry::configuration_gated_ids() {
	Vector<String> gated;
	gated.push_back(tooling_adapter_id());
	gated.sort();
	return gated;
}

bool FSCompletenessAdapterRegistry::is_configuration_gated(const String &p_adapter_id) {
	return !p_adapter_id.is_empty() && configuration_gated_ids().has(p_adapter_id);
}

bool FSCompletenessAdapterRegistry::is_declared(const String &p_adapter_id) {
	return find(p_adapter_id) != nullptr || is_configuration_gated(p_adapter_id);
}

bool FSCompletenessAdapterRegistry::declared_capability(
		const String &p_adapter_id, FSCompletenessAdapterCapability &r_capability) {
	r_capability = FSCompletenessAdapterCapability();
	const FSCompletenessFamilyAdapter *adapter = find(p_adapter_id);
	if (adapter != nullptr) {
		r_capability.renderable_leaves = adapter->renderable_leaves();
		r_capability.observable_dimensions = adapter->observable_dimensions();
		return true;
	}
	if (p_adapter_id == tooling_adapter_id()) {
		// The declaration the gated adapter itself returns, compiled into every build so a tooling
		// manifest is judged the same way everywhere.
		r_capability.renderable_leaves = tooling_renderable_leaves();
		r_capability.observable_dimensions = tooling_observable_dimensions();
		return true;
	}
	return false;
}

Vector<String> FSCompletenessAdapterRegistry::ids() {
	Vector<String> registered;
	for (const FSCompletenessFamilyAdapter *adapter : builtin_adapters()) {
		if (adapter != nullptr) {
			registered.push_back(adapter->id());
		}
	}
	registered.sort();
	return registered;
}

String FSCompletenessAdapterRegistry::validate(const Vector<const FSCompletenessFamilyAdapter *> &p_table) {
	HashSet<String> seen;
	for (const FSCompletenessFamilyAdapter *adapter : p_table) {
		if (adapter == nullptr) {
			return "<null>";
		}
		const String adapter_id = adapter->id();
		if (adapter_id.is_empty()) {
			return "<empty>";
		}
		if (seen.has(adapter_id)) {
			return adapter_id;
		}
		seen.insert(adapter_id);
	}
	return String();
}

String FSCompletenessAdapterRegistry::validate() {
	return validate(builtin_adapters());
}

} // namespace FSTests
