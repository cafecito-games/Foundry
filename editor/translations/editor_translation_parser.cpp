/**************************************************************************/
/*  editor_translation_parser.cpp                                         */
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

#include "editor_translation_parser.h"

#include "core/error/error_macros.h"
#include "core/object/script_language.h"
#include "core/templates/hash_set.h"

EditorTranslationParser *EditorTranslationParser::singleton = nullptr;

Error EditorTranslationParserPlugin::parse_file(const String &p_path, Vector<Vector<String>> *r_translations) {
	TypedArray<PackedStringArray> ret;

	if (FOUNDRY_VIRTUAL_CALL(_parse_file, p_path, ret)) {
		// Copy over entries directly.
		for (const PackedStringArray translation : ret) {
			r_translations->push_back(translation);
		}

		return OK;
	}

	ERR_PRINT("Custom translation parser plugin's \"_parse_file\" is undefined.");
	return ERR_UNAVAILABLE;
}

void EditorTranslationParserPlugin::get_recognized_extensions(List<String> *r_extensions) const {
	Vector<String> extensions;
	if (FOUNDRY_VIRTUAL_CALL(_get_recognized_extensions, extensions)) {
		for (int i = 0; i < extensions.size(); i++) {
			r_extensions->push_back(extensions[i]);
		}
	} else {
		ERR_PRINT("Custom translation parser plugin's \"func get_recognized_extensions()\" is undefined.");
	}
}

void EditorTranslationParserPlugin::_bind_methods() {
	FOUNDRY_VIRTUAL_BIND(_parse_file, "path");
	FOUNDRY_VIRTUAL_BIND(_get_recognized_extensions);
}

/////////////////////////

void EditorTranslationParser::get_recognized_extensions(List<String> *r_extensions) const {
	HashSet<String> extensions;
	List<String> temp;
	for (int i = 0; i < standard_parsers.size(); i++) {
		standard_parsers[i]->get_recognized_extensions(&temp);
	}
	for (int i = 0; i < custom_parsers.size(); i++) {
		custom_parsers[i]->get_recognized_extensions(&temp);
	}
	// Remove duplicates.
	for (const String &E : temp) {
		extensions.insert(E);
	}
	for (const String &E : extensions) {
		r_extensions->push_back(E);
	}
}

bool EditorTranslationParser::can_parse(const String &p_extension) const {
	List<String> extensions;
	get_recognized_extensions(&extensions);
	for (const String &extension : extensions) {
		if (p_extension == extension) {
			return true;
		}
	}
	return false;
}

Ref<EditorTranslationParserPlugin> EditorTranslationParser::get_parser(const String &p_extension) const {
	// Consider user-defined parsers first.
	for (int i = 0; i < custom_parsers.size(); i++) {
		List<String> temp;
		custom_parsers[i]->get_recognized_extensions(&temp);
		for (const String &E : temp) {
			if (E == p_extension) {
				return custom_parsers[i];
			}
		}
	}

	for (int i = 0; i < standard_parsers.size(); i++) {
		List<String> temp;
		standard_parsers[i]->get_recognized_extensions(&temp);
		for (const String &E : temp) {
			if (E == p_extension) {
				return standard_parsers[i];
			}
		}
	}

	WARN_PRINT("No translation parser available for \"" + p_extension + "\" extension.");

	return nullptr;
}

void EditorTranslationParser::add_parser(const Ref<EditorTranslationParserPlugin> &p_parser, ParserType p_type) {
	if (p_type == ParserType::STANDARD) {
		standard_parsers.push_back(p_parser);
	} else if (p_type == ParserType::CUSTOM) {
		custom_parsers.push_back(p_parser);
	}
}

void EditorTranslationParser::remove_parser(const Ref<EditorTranslationParserPlugin> &p_parser, ParserType p_type) {
	if (p_type == ParserType::STANDARD) {
		standard_parsers.erase(p_parser);
	} else if (p_type == ParserType::CUSTOM) {
		custom_parsers.erase(p_parser);
	}
}

void EditorTranslationParser::clean_parsers() {
	standard_parsers.clear();
	custom_parsers.clear();
}

EditorTranslationParser *EditorTranslationParser::get_singleton() {
	if (!singleton) {
		singleton = memnew(EditorTranslationParser);
	}
	return singleton;
}

EditorTranslationParser::~EditorTranslationParser() {
	memdelete(singleton);
	singleton = nullptr;
}
