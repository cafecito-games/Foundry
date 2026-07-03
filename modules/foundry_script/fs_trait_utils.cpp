/**************************************************************************/
/*  fs_trait_utils.cpp                                                    */
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

#include "fs_trait_utils.h"

#ifndef FOUNDRY_SCRIPT_NO_FRONTEND

#include "fs_cache.h"
#include "fs_conformance_registry.h"

bool fs_class_has_named_trait(const FSParser::ClassNode *p_class, const StringName &p_trait_name) {
	if (p_class == nullptr || p_trait_name == StringName()) {
		return false;
	}

	const FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
	const FSParser::ClassNode *current = p_class;
	while (current != nullptr) {
		if (current->is_trait && fs_trait_identity_name(current) == p_trait_name) {
			return true;
		}

		for (const FSParser::ClassNode *trait : current->resolved_traits) {
			if (trait != nullptr && fs_trait_identity_name(trait) == p_trait_name) {
				return true;
			}
		}

		if (registry->has_conformance(current->fqcn, p_trait_name) ||
				registry->has_conformance(current->get_global_name(), p_trait_name)) {
			return true;
		}

		if (current->base_type.kind == FSParser::DataType::CLASS) {
			current = current->base_type.class_type;
		} else if (current->base_type.kind == FSParser::DataType::SCRIPT && !current->base_type.script_path.is_empty()) {
			Error err = OK;
			Ref<FSParserRef> base_parser_ref = FSCache::get_parser(current->base_type.script_path, FSParserRef::INTERFACE_SOLVED, err);
			if (err != OK || base_parser_ref.is_null()) {
				return false;
			}
			current = base_parser_ref->get_parser()->get_tree();
		} else if (current->base_type.kind == FSParser::DataType::NATIVE) {
			return registry->native_class_conforms(current->base_type.native_type, p_trait_name);
		} else {
			break;
		}
	}

	return false;
}

#else // FOUNDRY_SCRIPT_NO_FRONTEND

bool fs_class_has_named_trait(const FSParser::ClassNode *p_class, const StringName &p_trait_name) {
	(void)p_class;
	(void)p_trait_name;
	return false;
}

#endif // FOUNDRY_SCRIPT_NO_FRONTEND
