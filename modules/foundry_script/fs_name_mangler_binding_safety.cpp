/**************************************************************************/
/*  fs_name_mangler_binding_safety.cpp                                   */
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

#include "fs_name_mangler_binding_safety.h"

#ifdef TOOLS_ENABLED

namespace {

String get_binding_kind_label(FSNameManglerBindingSafety::BindingKind p_kind) {
	switch (p_kind) {
		case FSNameManglerBindingSafety::BINDING_CONNECTION_SIGNAL:
			return "connection signal";
		case FSNameManglerBindingSafety::BINDING_CONNECTION_METHOD:
			return "connection method";
		case FSNameManglerBindingSafety::BINDING_SERIALIZED_PROPERTY:
			return "serialized property";
		case FSNameManglerBindingSafety::BINDING_ANIMATION_PROPERTY:
			return "animation property";
		case FSNameManglerBindingSafety::BINDING_ANIMATION_METHOD:
			return "animation method";
		case FSNameManglerBindingSafety::BINDING_RESOURCE_SCRIPT_CLASS:
			return "resource script class";
		case FSNameManglerBindingSafety::BINDING_TYPED_CONTAINER_SCRIPT_CLASS:
			return "typed container script class";
	}
	return "unknown binding";
}

struct EvidenceComparator {
	bool operator()(const FSNameManglerBindingSafety::Evidence &p_left,
			const FSNameManglerBindingSafety::Evidence &p_right) const {
		if (p_left.name != p_right.name) {
			return String(p_left.name) < String(p_right.name);
		}
		if (p_left.kind != p_right.kind) {
			return p_left.kind < p_right.kind;
		}
		if (p_left.source != p_right.source) {
			return p_left.source < p_right.source;
		}
		return p_left.owner < p_right.owner;
	}
};

bool evidence_matches(const FSNameManglerBindingSafety::Evidence &p_left,
		const FSNameManglerBindingSafety::Evidence &p_right) {
	return p_left.name == p_right.name && p_left.kind == p_right.kind &&
			p_left.source == p_right.source && p_left.owner == p_right.owner;
}

} // namespace

void FSNameManglerBindingSafety::Input::add_resource(
		const Ref<Resource> &p_resource, const String &p_source) {
	ResourceRoot root;
	root.resource = p_resource;
	root.source = p_source.is_empty() && p_resource.is_valid() ? p_resource->get_path() : p_source;
	resources.push_back(root);
}

String FSNameManglerBindingSafety::Evidence::detail() const {
	String result = get_binding_kind_label(kind) + " in " + source;
	if (!owner.is_empty()) {
		result += " owned by " + owner;
	}
	return result;
}

String FSNameManglerBindingSafety::Diagnostic::format() const {
	String result = source;
	if (!context.is_empty()) {
		result += ": " + context;
	}
	if (!message.is_empty()) {
		result += ": " + message;
	}
	return result;
}

Error FSNameManglerBindingSafety::Result::apply_to_input(
		FSNameManglerAnalysis::Input &r_input) const {
	if (error != OK) {
		return error;
	}
	if (!complete) {
		return ERR_INVALID_DATA;
	}

	Vector<Evidence> sorted_evidence = evidence;
	sorted_evidence.sort_custom<EvidenceComparator>();
	for (int i = sorted_evidence.size() - 1; i > 0; i--) {
		if (evidence_matches(sorted_evidence[i - 1], sorted_evidence[i])) {
			sorted_evidence.remove_at(i);
		}
	}
	for (const Evidence &item : sorted_evidence) {
		if (item.name.is_empty() || item.source.is_empty()) {
			return ERR_INVALID_DATA;
		}
	}
	for (const Evidence &item : sorted_evidence) {
		r_input.add_keep(
				item.name, FSNameManglerAnalysis::KEEP_SCENE_OR_RESOURCE, item.detail());
	}
	return OK;
}

FSNameManglerBindingSafety::Result FSNameManglerBindingSafety::collect(
		const Input &p_input, const FSNameManglerAnalysis::Input &p_analysis_input) {
	(void)p_input;
	(void)p_analysis_input;
	return Result();
}

#endif // TOOLS_ENABLED
