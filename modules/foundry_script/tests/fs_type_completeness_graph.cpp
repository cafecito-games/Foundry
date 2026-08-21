/**************************************************************************/
/*  fs_type_completeness_graph.cpp                                        */
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

#include "fs_type_completeness_graph.h"

#include "core/templates/hash_set.h"
#include "core/variant/array.h"

namespace FSTests {

namespace {

struct DerivationState {
	int cell_index = -1;
	Dictionary coordinates;
	Dictionary dimensions;
	HashSet<String> used_relation_ids;
	HashSet<String> visited_cells;
	Vector<FSCompletenessProvenanceStep> provenance;
};

struct ReachableDisposition {
	Variant expected;
	Vector<FSCompletenessProvenanceStep> provenance;
};

struct DimensionDispositions {
	String dimension;
	Vector<ReachableDisposition> dispositions;
};

struct CellDispositions {
	Dictionary coordinates;
	Vector<DimensionDispositions> dimensions;
};

struct TransformCandidate {
	int relation_index = -1;
	int exception_index = -1;
	String dimension;
	Variant transformation;
	bool produces_dimension = true;
	Dictionary target_coordinates;
	String target_key;
	Vector<uint8_t> matched_cells;
};

struct PossibleEdge {
	int target_cell = -1;
	String relation_id;
};

struct SelectedApplication {
	int relation_index = -1;
	int exception_index = -1;
	Dictionary target_coordinates;
	String target_key;
	Dictionary output_dimensions;
};

static Vector<String> sorted_dictionary_keys(const Dictionary &p_dictionary) {
	Vector<String> keys;
	const Array raw_keys = p_dictionary.keys();
	keys.reserve(raw_keys.size());
	for (int i = 0; i < raw_keys.size(); i++) {
		keys.push_back(raw_keys[i]);
	}
	keys.sort();
	return keys;
}

static String length_encoded(const String &p_value) {
	return vformat("%d:%s", p_value.length(), p_value);
}

static String canonical_variant_identity(const Variant &p_value);

static String canonical_dictionary_identity(const Dictionary &p_dictionary) {
	String identity = "{";
	for (const String &key : sorted_dictionary_keys(p_dictionary)) {
		identity += length_encoded(key);
		identity += length_encoded(canonical_variant_identity(p_dictionary[key]));
	}
	return identity + "}";
}

static String canonical_variant_identity(const Variant &p_value) {
	if (p_value.get_type() == Variant::DICTIONARY) {
		return "D" + canonical_dictionary_identity(p_value);
	}
	if (p_value.get_type() == Variant::ARRAY) {
		String identity = "A[";
		const Array values = p_value;
		for (int i = 0; i < values.size(); i++) {
			identity += length_encoded(canonical_variant_identity(values[i]));
		}
		return identity + "]";
	}
	const String representation = p_value.stringify();
	return vformat("T%d:%s", p_value.get_type(), length_encoded(representation));
}

static String provenance_identity(const Vector<FSCompletenessProvenanceStep> &p_provenance) {
	String identity;
	for (const FSCompletenessProvenanceStep &step : p_provenance) {
		identity += length_encoded(step.relation_id);
		identity += length_encoded(step.exception_id);
		identity += length_encoded(canonical_dictionary_identity(step.source_coordinates));
		identity += length_encoded(canonical_dictionary_identity(step.target_coordinates));
		identity += length_encoded(canonical_dictionary_identity(step.input_dimensions));
		identity += length_encoded(canonical_dictionary_identity(step.output_dimensions));
	}
	return identity;
}

static Vector<String> sorted_set_members(const HashSet<String> &p_set) {
	Vector<String> members;
	members.reserve(p_set.size());
	for (const String &member : p_set) {
		members.push_back(member);
	}
	members.sort();
	return members;
}

static String derivation_state_identity(const DerivationState &p_state) {
	String identity = length_encoded(canonical_dictionary_identity(p_state.coordinates));
	identity += length_encoded(canonical_dictionary_identity(p_state.dimensions));
	for (const String &relation_id : sorted_set_members(p_state.used_relation_ids)) {
		identity += length_encoded(relation_id);
	}
	identity += "|";
	for (const String &cell : sorted_set_members(p_state.visited_cells)) {
		identity += length_encoded(cell);
	}
	identity += "|" + length_encoded(provenance_identity(p_state.provenance));
	return identity;
}

static bool append_unique_state(Vector<DerivationState> &r_states, HashSet<String> &r_state_ids,
		const DerivationState &p_state) {
	const String identity = derivation_state_identity(p_state);
	if (r_state_ids.has(identity)) {
		return false;
	}
	r_state_ids.insert(identity);
	r_states.push_back(p_state);
	return true;
}

static void append_unique_error(Vector<String> &r_errors, HashSet<String> &r_emitted_errors, const String &p_error) {
	if (!r_emitted_errors.has(p_error)) {
		r_emitted_errors.insert(p_error);
		r_errors.push_back(p_error);
	}
}

static String coordinate_key(const Dictionary &p_coordinates, const Vector<String> &p_axis_order) {
	String key;
	for (const String &axis : p_axis_order) {
		const String value = p_coordinates.get(axis, String());
		key += vformat("%d:%s=%d:%s;", axis.length(), axis, value.length(), value);
	}
	return key;
}

static String coordinate_description(const Dictionary &p_coordinates, const Vector<String> &p_axis_order) {
	Vector<String> parts;
	for (const String &axis : p_axis_order) {
		parts.push_back(vformat("%s=%s", axis, String(p_coordinates.get(axis, String()))));
	}
	return String(", ").join(parts);
}

static bool coordinates_have_domain_shape(const Dictionary &p_coordinates, const Vector<String> &p_axis_order) {
	if (p_coordinates.size() != p_axis_order.size()) {
		return false;
	}
	for (const String &axis : p_axis_order) {
		if (!p_coordinates.has(axis)) {
			return false;
		}
	}
	return true;
}

static void expand_domain_recursive(const FSCompletenessManifest &p_manifest, int p_axis_index,
		Dictionary &r_coordinates, Vector<Dictionary> &r_cells, Vector<String> &r_errors) {
	if (p_axis_index == p_manifest.domain_axis_order.size()) {
		r_cells.push_back(r_coordinates.duplicate());
		return;
	}

	const String &axis = p_manifest.domain_axis_order[p_axis_index];
	const Vector<String> *values = p_manifest.domain.getptr(axis);
	if (values == nullptr || values->is_empty()) {
		r_errors.push_back(vformat("domain axis '%s' has no declared values", axis));
		return;
	}
	for (const String &value : *values) {
		r_coordinates[axis] = value;
		expand_domain_recursive(p_manifest, p_axis_index + 1, r_coordinates, r_cells, r_errors);
	}
	r_coordinates.erase(axis);
}

static bool predicate_conjunction_matches(const Dictionary &p_relation_predicate,
		const Dictionary *p_exception_predicate, const Dictionary &p_coordinates,
		const FSCompletenessCatalog &p_catalog) {
	if (!FSCompletenessGraph::predicate_matches(p_relation_predicate, p_coordinates, p_catalog)) {
		return false;
	}
	return p_exception_predicate == nullptr ||
			FSCompletenessGraph::predicate_matches(*p_exception_predicate, p_coordinates, p_catalog);
}

static Vector<uint8_t> normalized_matched_cells(const Dictionary &p_relation_predicate,
		const Dictionary *p_exception_predicate, const Vector<Dictionary> &p_domain_cells,
		const FSCompletenessCatalog &p_catalog) {
	Vector<uint8_t> matched;
	matched.resize(p_domain_cells.size());
	for (int i = 0; i < p_domain_cells.size(); i++) {
		matched.write[i] = predicate_conjunction_matches(
				p_relation_predicate, p_exception_predicate, p_domain_cells[i], p_catalog);
	}
	return matched;
}

static bool strict_subset(const Vector<uint8_t> &p_left, const Vector<uint8_t> &p_right) {
	bool different = false;
	for (int i = 0; i < p_left.size(); i++) {
		if (p_left[i] && !p_right[i]) {
			return false;
		}
		if (!p_left[i] && p_right[i]) {
			different = true;
		}
	}
	return different;
}

static bool equal_sets(const Vector<uint8_t> &p_left, const Vector<uint8_t> &p_right) {
	if (p_left.size() != p_right.size()) {
		return false;
	}
	for (int i = 0; i < p_left.size(); i++) {
		if (p_left[i] != p_right[i]) {
			return false;
		}
	}
	return true;
}

static bool set_has_member(const Vector<uint8_t> &p_set) {
	for (const uint8_t member : p_set) {
		if (member) {
			return true;
		}
	}
	return false;
}

static void append_unique_dimension(Vector<String> &r_dimensions, const String &p_dimension) {
	if (!r_dimensions.has(p_dimension)) {
		r_dimensions.push_back(p_dimension);
	}
}

static void detect_cycle_from(int p_cell, const Vector<Vector<PossibleEdge>> &p_edges,
		const Vector<Dictionary> &p_domain_cells, const Vector<String> &p_axis_order,
		Vector<uint8_t> &r_colors, Vector<String> &r_errors, HashSet<String> &r_emitted_errors) {
	r_colors.write[p_cell] = 1;
	for (const PossibleEdge &edge : p_edges[p_cell]) {
		if (r_colors[edge.target_cell] == 1) {
			append_unique_error(r_errors, r_emitted_errors,
					vformat("relation '%s' would revisit domain cell {%s}", edge.relation_id,
							coordinate_description(p_domain_cells[edge.target_cell], p_axis_order)));
			continue;
		}
		if (r_colors[edge.target_cell] == 0) {
			detect_cycle_from(edge.target_cell, p_edges, p_domain_cells, p_axis_order,
					r_colors, r_errors, r_emitted_errors);
		}
	}
	r_colors.write[p_cell] = 2;
}

static bool same_candidate_group(const TransformCandidate &p_left, const TransformCandidate &p_right) {
	return p_left.target_key == p_right.target_key && p_left.dimension == p_right.dimension;
}

static bool same_application(const SelectedApplication &p_application, const TransformCandidate &p_candidate) {
	return p_application.relation_index == p_candidate.relation_index &&
			p_application.exception_index == p_candidate.exception_index &&
			p_application.target_key == p_candidate.target_key;
}

static DimensionDispositions *find_dimension_dispositions(CellDispositions &r_cell, const String &p_dimension) {
	for (DimensionDispositions &dimension : r_cell.dimensions) {
		if (dimension.dimension == p_dimension) {
			return &dimension;
		}
	}
	return nullptr;
}

static const DimensionDispositions *find_dimension_dispositions(
		const CellDispositions &p_cell, const String &p_dimension) {
	for (const DimensionDispositions &dimension : p_cell.dimensions) {
		if (dimension.dimension == p_dimension) {
			return &dimension;
		}
	}
	return nullptr;
}

static void record_state(const DerivationState &p_state, Vector<CellDispositions> &r_dispositions) {
	CellDispositions &cell = r_dispositions.write[p_state.cell_index];
	for (const String &dimension_name : sorted_dictionary_keys(p_state.dimensions)) {
		DimensionDispositions *dimension = find_dimension_dispositions(cell, dimension_name);
		if (dimension == nullptr) {
			DimensionDispositions added;
			added.dimension = dimension_name;
			cell.dimensions.push_back(added);
			dimension = &cell.dimensions.write[cell.dimensions.size() - 1];
		}
		ReachableDisposition disposition;
		disposition.expected = p_state.dimensions[dimension_name];
		disposition.provenance = p_state.provenance;
		const String identity = canonical_variant_identity(disposition.expected) +
				length_encoded(provenance_identity(disposition.provenance));
		bool already_recorded = false;
		for (const ReachableDisposition &existing : dimension->dispositions) {
			const String existing_identity = canonical_variant_identity(existing.expected) +
					length_encoded(provenance_identity(existing.provenance));
			if (existing_identity == identity) {
				already_recorded = true;
				break;
			}
		}
		if (already_recorded) {
			continue;
		}
		dimension->dispositions.push_back(disposition);
	}
}

static bool provenance_less(const Vector<FSCompletenessProvenanceStep> &p_left,
		const Vector<FSCompletenessProvenanceStep> &p_right) {
	if (p_left.size() != p_right.size()) {
		return p_left.size() < p_right.size();
	}
	for (int i = 0; i < p_left.size(); i++) {
		if (p_left[i].relation_id != p_right[i].relation_id) {
			return p_left[i].relation_id < p_right[i].relation_id;
		}
		if (p_left[i].exception_id != p_right[i].exception_id) {
			return p_left[i].exception_id < p_right[i].exception_id;
		}
	}
	return provenance_identity(p_left) < provenance_identity(p_right);
}

static void sort_provenance_paths(Vector<Vector<FSCompletenessProvenanceStep>> &r_paths) {
	for (int i = 1; i < r_paths.size(); i++) {
		const Vector<FSCompletenessProvenanceStep> path = r_paths[i];
		int insertion_index = i;
		while (insertion_index > 0 && provenance_less(path, r_paths[insertion_index - 1])) {
			r_paths.write[insertion_index] = r_paths[insertion_index - 1];
			insertion_index--;
		}
		r_paths.write[insertion_index] = path;
	}
}

} // namespace

const FSCompletenessResolvedDimension *FSCompletenessResolvedCell::find_dimension(const String &p_dimension) const {
	return dimensions.getptr(p_dimension);
}

bool FSCompletenessGraph::predicate_matches(const Dictionary &p_predicate, const Dictionary &p_coordinates,
		const FSCompletenessCatalog &p_catalog, Vector<String> *r_errors) {
	bool matches = true;
	for (const String &axis : sorted_dictionary_keys(p_predicate)) {
		if (!p_coordinates.has(axis) || p_coordinates[axis].get_type() != Variant::STRING) {
			if (r_errors != nullptr) {
				r_errors->push_back(vformat("predicate axis '%s' has no concrete coordinate", axis));
			}
			matches = false;
			continue;
		}

		const String coordinate = p_coordinates[axis];
		const Variant selector = p_predicate[axis];
		if (selector.get_type() == Variant::STRING) {
			matches = matches && coordinate == String(selector);
			continue;
		}
		if (selector.get_type() == Variant::DICTIONARY) {
			const Dictionary class_selector = selector;
			if (class_selector.size() == 1 && class_selector.has("class") &&
					class_selector["class"].get_type() == Variant::STRING) {
				matches = matches && p_catalog.class_contains(axis, class_selector["class"], coordinate);
				continue;
			}
		}
		if (r_errors != nullptr) {
			r_errors->push_back(vformat(
					"predicate selector for axis '%s' must be a string leaf or an object containing only 'class'", axis));
		}
		matches = false;
	}
	return matches;
}

Error FSCompletenessGraph::resolve(const FSCompletenessManifest &p_manifest, const FSCompletenessCatalog &p_catalog,
		FSCompletenessResolution &r_resolution, Vector<String> &r_errors) {
	r_resolution = FSCompletenessResolution();
	r_errors.clear();
	if (validate_manifest_vocabulary(p_manifest, p_catalog, r_errors) != OK) {
		return ERR_INVALID_DATA;
	}

	Vector<Dictionary> domain_cells;
	Dictionary coordinates;
	expand_domain_recursive(p_manifest, 0, coordinates, domain_cells, r_errors);
	if (p_manifest.domain.size() != p_manifest.domain_axis_order.size()) {
		r_errors.push_back("domain axis order does not name every domain axis exactly once");
	}

	HashMap<String, int> domain_indices;
	for (int i = 0; i < domain_cells.size(); i++) {
		const String key = coordinate_key(domain_cells[i], p_manifest.domain_axis_order);
		if (domain_indices.has(key)) {
			r_errors.push_back(vformat("domain contains duplicate cell {%s}",
					coordinate_description(domain_cells[i], p_manifest.domain_axis_order)));
		} else {
			domain_indices.insert(key, i);
		}
	}

	Vector<CellDispositions> dispositions;
	dispositions.resize(domain_cells.size());
	for (int i = 0; i < domain_cells.size(); i++) {
		dispositions.write[i].coordinates = domain_cells[i];
	}

	HashSet<String> emitted_errors;
	for (const FSCompletenessException &exception : p_manifest.exceptions) {
		for (const FSCompletenessRelation &relation : p_manifest.relations) {
			if (relation.id != exception.parent) {
				continue;
			}
			const Vector<uint8_t> parent_cells = normalized_matched_cells(
					relation.from, nullptr, domain_cells, p_catalog);
			const Vector<uint8_t> exception_cells = normalized_matched_cells(
					relation.from, &exception.when, domain_cells, p_catalog);
			if (!set_has_member(exception_cells)) {
				append_unique_error(r_errors, emitted_errors,
						vformat("exception '%s' matches no domain cells", exception.id));
			} else if (!strict_subset(exception_cells, parent_cells)) {
				append_unique_error(r_errors, emitted_errors,
						vformat("exception '%s' must strictly narrow parent relation '%s'", exception.id, relation.id));
			}
			break;
		}
	}

	Vector<Vector<PossibleEdge>> possible_edges;
	possible_edges.resize(domain_cells.size());
	for (int cell_index = 0; cell_index < domain_cells.size(); cell_index++) {
		for (const FSCompletenessRelation &relation : p_manifest.relations) {
			if (!predicate_matches(relation.from, domain_cells[cell_index], p_catalog)) {
				continue;
			}
			Dictionary target = domain_cells[cell_index].duplicate();
			for (const String &axis : sorted_dictionary_keys(relation.to)) {
				target[axis] = relation.to[axis];
			}
			const String target_key = coordinate_key(target, p_manifest.domain_axis_order);
			const int *target_cell = domain_indices.getptr(target_key);
			if (!coordinates_have_domain_shape(target, p_manifest.domain_axis_order) || target_cell == nullptr) {
				append_unique_error(r_errors, emitted_errors,
						vformat("relation '%s' targets undeclared domain cell {%s}", relation.id,
								coordinate_description(target, p_manifest.domain_axis_order)));
				continue;
			}
			PossibleEdge edge;
			edge.target_cell = *target_cell;
			edge.relation_id = relation.id;
			possible_edges.write[cell_index].push_back(edge);
		}
	}
	Vector<uint8_t> cycle_colors;
	cycle_colors.resize(domain_cells.size());
	for (int cell_index = 0; cell_index < domain_cells.size(); cell_index++) {
		if (cycle_colors[cell_index] == 0) {
			detect_cycle_from(cell_index, possible_edges, domain_cells, p_manifest.domain_axis_order,
					cycle_colors, r_errors, emitted_errors);
		}
	}

	Vector<DerivationState> frontier;
	HashSet<String> frontier_state_ids;
	for (const FSCompletenessAnchor &anchor : p_manifest.anchors) {
		Vector<Dictionary> expanded_coordinates;
		if (!anchor.coordinates.has("surface") && !anchor.surfaces.is_empty()) {
			for (const String &surface : anchor.surfaces) {
				Dictionary expanded = anchor.coordinates.duplicate();
				expanded["surface"] = surface;
				expanded_coordinates.push_back(expanded);
			}
		} else {
			expanded_coordinates.push_back(anchor.coordinates.duplicate());
		}

		for (const Dictionary &anchor_coordinates : expanded_coordinates) {
			const String key = coordinate_key(anchor_coordinates, p_manifest.domain_axis_order);
			const int *cell_index = domain_indices.getptr(key);
			if (!coordinates_have_domain_shape(anchor_coordinates, p_manifest.domain_axis_order) || cell_index == nullptr) {
				append_unique_error(r_errors, emitted_errors,
						vformat("anchor '%s' coordinates {%s} are not a declared domain cell", anchor.id,
								coordinate_description(anchor_coordinates, p_manifest.domain_axis_order)));
				continue;
			}

			DerivationState state;
			state.cell_index = *cell_index;
			state.coordinates = anchor_coordinates;
			state.dimensions = anchor.expect.duplicate();
			state.visited_cells.insert(key);
			if (append_unique_state(frontier, frontier_state_ids, state)) {
				record_state(state, dispositions);
			}
		}
	}

	for (int depth = 0; depth < p_manifest.max_chain_length; depth++) {
		Vector<DerivationState> next_frontier;
		HashSet<String> next_frontier_state_ids;
		for (const DerivationState &state : frontier) {
			Vector<TransformCandidate> candidates;
			for (int relation_index = 0; relation_index < p_manifest.relations.size(); relation_index++) {
				const FSCompletenessRelation &relation = p_manifest.relations[relation_index];
				if (!predicate_matches(relation.from, state.coordinates, p_catalog)) {
					continue;
				}

				Dictionary target = state.coordinates.duplicate();
				for (const String &axis : sorted_dictionary_keys(relation.to)) {
					target[axis] = relation.to[axis];
				}
				const String target_key = coordinate_key(target, p_manifest.domain_axis_order);
				if (!coordinates_have_domain_shape(target, p_manifest.domain_axis_order) || !domain_indices.has(target_key)) {
					append_unique_error(r_errors, emitted_errors,
							vformat("relation '%s' targets undeclared domain cell {%s}", relation.id,
									coordinate_description(target, p_manifest.domain_axis_order)));
					continue;
				}
				if (state.used_relation_ids.has(relation.id)) {
					append_unique_error(r_errors, emitted_errors,
							vformat("relation '%s' would repeat in a derivation chain", relation.id));
					continue;
				}
				if (state.visited_cells.has(target_key)) {
					append_unique_error(r_errors, emitted_errors,
							vformat("relation '%s' would revisit domain cell {%s}", relation.id,
									coordinate_description(target, p_manifest.domain_axis_order)));
					continue;
				}

				Vector<int> matching_exceptions;
				Vector<String> effective_dimensions = sorted_dictionary_keys(relation.derive);
				for (int exception_index = 0; exception_index < p_manifest.exceptions.size(); exception_index++) {
					const FSCompletenessException &exception = p_manifest.exceptions[exception_index];
					if (exception.parent != relation.id || !predicate_matches(exception.when, state.coordinates, p_catalog)) {
						continue;
					}
					matching_exceptions.push_back(exception_index);
					for (const String &dimension : sorted_dictionary_keys(exception.derive)) {
						append_unique_dimension(effective_dimensions, dimension);
					}
				}
				effective_dimensions.sort();

				const Vector<uint8_t> parent_matched_cells = normalized_matched_cells(
						relation.from, nullptr, domain_cells, p_catalog);
				for (const String &dimension : effective_dimensions) {
					TransformCandidate candidate;
					candidate.relation_index = relation_index;
					candidate.dimension = dimension;
					candidate.produces_dimension = relation.derive.has(dimension);
					if (candidate.produces_dimension) {
						candidate.transformation = relation.derive[dimension];
						if (candidate.transformation == Variant("same") && !state.dimensions.has(dimension)) {
							candidate.produces_dimension = false;
						}
					}
					candidate.target_coordinates = target;
					candidate.target_key = target_key;
					candidate.matched_cells = parent_matched_cells;
					candidates.push_back(candidate);
				}

				for (const int exception_index : matching_exceptions) {
					const FSCompletenessException &exception = p_manifest.exceptions[exception_index];
					const Vector<uint8_t> exception_matched_cells = normalized_matched_cells(
							relation.from, &exception.when, domain_cells, p_catalog);
					for (const String &dimension : effective_dimensions) {
						TransformCandidate candidate;
						candidate.relation_index = relation_index;
						candidate.exception_index = exception_index;
						candidate.dimension = dimension;
						candidate.produces_dimension = exception.derive.has(dimension);
						if (candidate.produces_dimension) {
							candidate.transformation = exception.derive[dimension];
							if (candidate.transformation == Variant("same") && !state.dimensions.has(dimension)) {
								candidate.produces_dimension = false;
							}
						}
						candidate.target_coordinates = target;
						candidate.target_key = target_key;
						candidate.matched_cells = exception_matched_cells;
						candidates.push_back(candidate);
					}
				}
			}

			Vector<uint8_t> selected;
			selected.resize(candidates.size());
			for (int candidate_index = 0; candidate_index < candidates.size(); candidate_index++) {
				bool first_in_group = true;
				for (int previous = 0; previous < candidate_index; previous++) {
					if (same_candidate_group(candidates[previous], candidates[candidate_index])) {
						first_in_group = false;
						break;
					}
				}
				if (!first_in_group) {
					continue;
				}

				Vector<int> maxima;
				for (int possible = candidate_index; possible < candidates.size(); possible++) {
					if (!same_candidate_group(candidates[possible], candidates[candidate_index])) {
						continue;
					}
					bool maximal = true;
					for (int challenger = candidate_index; challenger < candidates.size(); challenger++) {
						if (possible == challenger ||
								!same_candidate_group(candidates[challenger], candidates[candidate_index])) {
							continue;
						}
						if (strict_subset(candidates[challenger].matched_cells, candidates[possible].matched_cells)) {
							maximal = false;
							break;
						}
					}
					if (maximal) {
						maxima.push_back(possible);
					}
				}

				if (maxima.size() == 1) {
					selected.write[maxima[0]] = true;
					continue;
				}

				bool has_incomparable = false;
				for (int i = 0; i < maxima.size() && !has_incomparable; i++) {
					for (int j = i + 1; j < maxima.size(); j++) {
						if (!equal_sets(candidates[maxima[i]].matched_cells, candidates[maxima[j]].matched_cells)) {
							has_incomparable = true;
							break;
						}
					}
				}
				append_unique_error(r_errors, emitted_errors,
						vformat("cell {%s} dimension '%s' has %s",
								coordinate_description(state.coordinates, p_manifest.domain_axis_order),
								candidates[candidate_index].dimension,
								has_incomparable ? "incomparable maximal predicates" : "tied maximal predicates"));
			}

			Vector<SelectedApplication> applications;
			for (int candidate_index = 0; candidate_index < candidates.size(); candidate_index++) {
				if (!selected[candidate_index]) {
					continue;
				}
				const TransformCandidate &candidate = candidates[candidate_index];
				if (!candidate.produces_dimension) {
					continue;
				}
				SelectedApplication *application = nullptr;
				for (SelectedApplication &existing : applications) {
					if (same_application(existing, candidate)) {
						application = &existing;
						break;
					}
				}
				if (application == nullptr) {
					SelectedApplication added;
					added.relation_index = candidate.relation_index;
					added.exception_index = candidate.exception_index;
					added.target_coordinates = candidate.target_coordinates;
					added.target_key = candidate.target_key;
					applications.push_back(added);
					application = &applications.write[applications.size() - 1];
				}
				application->output_dimensions[candidate.dimension] =
						candidate.transformation == Variant("same") ? state.dimensions[candidate.dimension] : candidate.transformation;
			}

			for (const SelectedApplication &application : applications) {
				const FSCompletenessRelation &relation = p_manifest.relations[application.relation_index];
				DerivationState next;
				next.cell_index = *domain_indices.getptr(application.target_key);
				next.coordinates = application.target_coordinates;
				next.dimensions = application.output_dimensions;
				next.used_relation_ids = state.used_relation_ids;
				next.used_relation_ids.insert(relation.id);
				next.visited_cells = state.visited_cells;
				next.visited_cells.insert(application.target_key);
				next.provenance = state.provenance;

				FSCompletenessProvenanceStep step;
				step.relation_id = relation.id;
				if (application.exception_index >= 0) {
					step.exception_id = p_manifest.exceptions[application.exception_index].id;
				}
				step.source_coordinates = state.coordinates;
				step.target_coordinates = application.target_coordinates;
				step.input_dimensions = state.dimensions;
				step.output_dimensions = application.output_dimensions;
				next.provenance.push_back(step);

				if (append_unique_state(next_frontier, next_frontier_state_ids, next)) {
					record_state(next, dispositions);
				}
			}
		}
		frontier = next_frontier;
	}

	FSCompletenessResolution resolved;
	resolved.cells.resize(domain_cells.size());
	for (int cell_index = 0; cell_index < dispositions.size(); cell_index++) {
		const CellDispositions &cell_dispositions = dispositions[cell_index];
		FSCompletenessResolvedCell &resolved_cell = resolved.cells.write[cell_index];
		resolved_cell.coordinates = cell_dispositions.coordinates;

		Vector<String> dimension_names;
		for (const DimensionDispositions &dimension : cell_dispositions.dimensions) {
			if (!dimension.dispositions.is_empty()) {
				dimension_names.push_back(dimension.dimension);
			}
		}
		dimension_names.sort();
		for (const String &dimension_name : dimension_names) {
			const DimensionDispositions *dimension = find_dimension_dispositions(cell_dispositions, dimension_name);
			if (dimension == nullptr || dimension->dispositions.is_empty()) {
				continue;
			}

			const Variant expected = dimension->dispositions[0].expected;
			bool outcomes_agree = true;
			for (const ReachableDisposition &disposition : dimension->dispositions) {
				if (disposition.expected != expected) {
					outcomes_agree = false;
					break;
				}
			}
			if (!outcomes_agree) {
				resolved.ambiguous_dimension_count++;
				append_unique_error(r_errors, emitted_errors,
						vformat("cell {%s} dimension '%s' has incompatible outcomes",
								coordinate_description(cell_dispositions.coordinates, p_manifest.domain_axis_order), dimension_name));
				continue;
			}

			FSCompletenessResolvedDimension resolved_dimension;
			resolved_dimension.dimension = dimension_name;
			resolved_dimension.expected = expected;
			HashSet<String> provenance_ids;
			for (const ReachableDisposition &disposition : dimension->dispositions) {
				const String identity = provenance_identity(disposition.provenance);
				if (provenance_ids.has(identity)) {
					continue;
				}
				provenance_ids.insert(identity);
				resolved_dimension.agreeing_provenance.push_back(disposition.provenance);
			}
			sort_provenance_paths(resolved_dimension.agreeing_provenance);
			if (!resolved_dimension.agreeing_provenance.is_empty()) {
				resolved_dimension.canonical_provenance = resolved_dimension.agreeing_provenance[0];
			}
			for (const Vector<FSCompletenessProvenanceStep> &path : resolved_dimension.agreeing_provenance) {
				resolved.max_observed_chain_length = MAX(resolved.max_observed_chain_length, path.size());
			}
			resolved_cell.dimensions.insert(dimension_name, resolved_dimension);
		}

		if (dimension_names.is_empty()) {
			append_unique_error(r_errors, emitted_errors,
					vformat("cell {%s} has no reachable disposition",
							coordinate_description(cell_dispositions.coordinates, p_manifest.domain_axis_order)));
		}

		Vector<String> required_dimensions;
		for (const FSCompletenessRequiredDimension &required : p_manifest.required_dimensions) {
			if (!predicate_matches(required.when, cell_dispositions.coordinates, p_catalog)) {
				continue;
			}
			append_unique_dimension(required_dimensions, required.dimension);
		}
		required_dimensions.sort();
		for (const String &required_dimension : required_dimensions) {
			const DimensionDispositions *reachable = find_dimension_dispositions(cell_dispositions, required_dimension);
			if (reachable == nullptr || reachable->dispositions.is_empty()) {
				resolved.uncovered_dimension_count++;
				append_unique_error(r_errors, emitted_errors,
						vformat("cell {%s} required dimension '%s' has no reachable disposition",
								coordinate_description(cell_dispositions.coordinates, p_manifest.domain_axis_order),
								required_dimension));
			}
		}
	}

	if (!r_errors.is_empty()) {
		return ERR_INVALID_DATA;
	}
	r_resolution = resolved;
	return OK;
}

} // namespace FSTests
