/**************************************************************************/
/*  editor_automation_mcp_contracts.h                                     */
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

#include "editor/automation/editor_automation_action.h"
#include "editor/automation/editor_automation_types.h"

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/templates/local_vector.h"
#include "core/variant/variant.h"

// Typed MCP contracts are the single source of truth for the editor automation
// MCP surface. Each contract owns the JSON Schema advertised through tools/list,
// the boundary parser used by tools/call, and the Dictionary shape passed to the
// existing automation internals. Keep schema(), parse(), and to_dictionary() in
// lockstep so clients, tests, and runtime validation describe the same contract.
class EditorAutomationMCPJsonSchema : public RefCounted {
	FOUNDRY_SOFTCLASS(EditorAutomationMCPJsonSchema, RefCounted);

public:
	enum class Type {
		OBJECT,
		ARRAY,
		STRING,
		INTEGER,
		NUMBER,
		BOOLEAN,
	};

	struct Property {
		StringName name;
		Ref<EditorAutomationMCPJsonSchema> schema;
	};

private:
	Type type = Type::OBJECT;
	String description;
	LocalVector<Property> properties;
	PackedStringArray required;
	PackedStringArray enum_values;
	Ref<EditorAutomationMCPJsonSchema> items;
	bool has_additional_properties = false;
	bool additional_properties = true;

public:
	static Ref<EditorAutomationMCPJsonSchema> make(Type p_type, const String &p_description = String());
	static Ref<EditorAutomationMCPJsonSchema> object(const String &p_description = String());
	static Ref<EditorAutomationMCPJsonSchema> array(const Ref<EditorAutomationMCPJsonSchema> &p_items, const String &p_description = String());
	static Ref<EditorAutomationMCPJsonSchema> string(const String &p_description = String());
	static Ref<EditorAutomationMCPJsonSchema> integer(const String &p_description = String());
	static Ref<EditorAutomationMCPJsonSchema> number(const String &p_description = String());
	static Ref<EditorAutomationMCPJsonSchema> boolean(const String &p_description = String());
	static Ref<EditorAutomationMCPJsonSchema> enum_string(const PackedStringArray &p_values, const String &p_description = String());

	EditorAutomationMCPJsonSchema &add_property(const StringName &p_name, const Ref<EditorAutomationMCPJsonSchema> &p_schema);
	EditorAutomationMCPJsonSchema &add_required(const StringName &p_name);
	EditorAutomationMCPJsonSchema &set_additional_properties(bool p_allowed);

	Dictionary to_dictionary() const;
};

struct EditorAutomationMCPToolDefinition {
	String name;
	String description;
	Ref<EditorAutomationMCPJsonSchema> input_schema;
	Ref<EditorAutomationMCPJsonSchema> output_schema;

	Dictionary to_dictionary() const;
};

struct EditorAutomationMCPResourceDefinition {
	String uri;
	String name;
	String description;
	String mime_type = "application/json";

	Dictionary to_dictionary() const;
};

struct EditorAutomationMCPResourceTemplateDefinition {
	String uri_template;
	String name;
	String description;
	String mime_type = "application/json";

	Dictionary to_dictionary() const;
};

struct EditorAutomationMCPParseError {
	String field;
	String message;
};

template <typename T>
struct EditorAutomationMCPParseResult {
	bool ok = false;
	T value;
	EditorAutomationMCPParseError error;

	static EditorAutomationMCPParseResult<T> success(const T &p_value) {
		EditorAutomationMCPParseResult<T> result;
		result.ok = true;
		result.value = p_value;
		return result;
	}

	static EditorAutomationMCPParseResult<T> invalid(const String &p_field, const String &p_message) {
		EditorAutomationMCPParseResult<T> result;
		result.ok = false;
		result.error.field = p_field;
		result.error.message = p_message;
		return result;
	}
};

struct EditorAutomationMCPLogMarker {
	bool has_message_index = false;
	int message_index = 0;

	static Ref<EditorAutomationMCPJsonSchema> schema();
	static EditorAutomationMCPParseResult<EditorAutomationMCPLogMarker> parse(const Variant &p_value, const String &p_field);
	Dictionary to_dictionary() const;
};

struct EditorAutomationMCPEventMarker {
	bool has_event_index = false;
	int event_index = 0;

	static Ref<EditorAutomationMCPJsonSchema> schema();
	static EditorAutomationMCPParseResult<EditorAutomationMCPEventMarker> parse(const Variant &p_value, const String &p_field);
	Dictionary to_dictionary() const;
};

struct EditorAutomationMCPFailureAttachmentOptionsInput {
	bool has_attach_screenshot_on_failure = false;
	bool attach_screenshot_on_failure = false;
	bool has_max_screenshot_bytes = false;
	int max_screenshot_bytes = 0;

	static void add_schema_properties(const Ref<EditorAutomationMCPJsonSchema> &p_schema);
	static EditorAutomationMCPParseResult<EditorAutomationMCPFailureAttachmentOptionsInput> parse(const Dictionary &p_dict);
	void append_to_dictionary(Dictionary &r_dict) const;
};

struct EditorAutomationMCPSelector {
	Dictionary values;

	static Ref<EditorAutomationMCPJsonSchema> schema();
	static EditorAutomationMCPParseResult<EditorAutomationMCPSelector> parse(const Variant &p_value, const String &p_field = "selector");
	Dictionary to_dictionary() const;
};

struct EditorAutomationMCPActionArgs {
	Dictionary values;

	static Ref<EditorAutomationMCPJsonSchema> schema();
	static EditorAutomationMCPParseResult<EditorAutomationMCPActionArgs> parse(const Variant &p_value, const String &p_field = "args");
	Dictionary to_dictionary() const;
};

struct EditorAutomationMCPWaitCondition {
	Dictionary values;

	static Ref<EditorAutomationMCPJsonSchema> schema();
	static EditorAutomationMCPParseResult<EditorAutomationMCPWaitCondition> parse(const Variant &p_value, const String &p_field = "wait");
	Dictionary to_dictionary() const;
	bool is_empty() const;
};

struct EditorAutomationMCPToolFailure {
	String kind;
	String message;
	Dictionary details;

	static Ref<EditorAutomationMCPJsonSchema> schema();
	Dictionary to_dictionary() const;
};

struct EditorAutomationMCPElementNode {
	String id;
	String handle;
	String role;
	String name;
	String text;
	String class_name;
	String path;
	bool visible = false;
	bool enabled = false;
	bool focused = false;
	bool pressed = false;
	bool selected = false;
	Rect2 bounds;
	PackedStringArray actions;
	Dictionary metadata;
	bool has_children = false;
	Array children;
	bool children_truncated = false;
	int child_count = 0;
	String children_next_cursor;

	static EditorAutomationMCPElementNode from_element(const EditorAutomationElement &p_element);
	Dictionary to_dictionary() const;
};

// Tool input structs intentionally preserve optional fields in `values` rather
// than normalizing everything into C++ members. This keeps the dispatcher
// compatible with the existing Dictionary-based automation core while still
// giving the MCP boundary typed field validation and documented schemas.
struct EditorAutomationMCPObserveUIInput {
	Dictionary values;

	static Ref<EditorAutomationMCPJsonSchema> schema();
	static EditorAutomationMCPParseResult<EditorAutomationMCPObserveUIInput> parse(const Dictionary &p_dict);
	Dictionary to_dictionary() const;
};

struct EditorAutomationMCPFindElementsInput {
	EditorAutomationMCPSelector selector;
	Dictionary values;
	EditorAutomationMCPFailureAttachmentOptionsInput failure_attachments;

	static Ref<EditorAutomationMCPJsonSchema> schema();
	static EditorAutomationMCPParseResult<EditorAutomationMCPFindElementsInput> parse(const Dictionary &p_dict);
	Dictionary to_dictionary() const;
};

struct EditorAutomationMCPActInput {
	Dictionary values;
	EditorAutomationMCPFailureAttachmentOptionsInput failure_attachments;

	static Ref<EditorAutomationMCPJsonSchema> schema();
	static EditorAutomationMCPParseResult<EditorAutomationMCPActInput> parse(const Dictionary &p_dict);
	Dictionary to_dictionary() const;
};

struct EditorAutomationMCPWaitForInput {
	Dictionary values;
	EditorAutomationMCPFailureAttachmentOptionsInput failure_attachments;

	static Ref<EditorAutomationMCPJsonSchema> schema();
	static EditorAutomationMCPParseResult<EditorAutomationMCPWaitForInput> parse(const Dictionary &p_dict);
	Dictionary to_dictionary() const;
};

struct EditorAutomationMCPReadEditorStateInput {
	static Ref<EditorAutomationMCPJsonSchema> schema();
	static EditorAutomationMCPParseResult<EditorAutomationMCPReadEditorStateInput> parse(const Dictionary &p_dict);
	Dictionary to_dictionary() const;
};

struct EditorAutomationMCPReadEditorLogInput {
	Dictionary values;

	static Ref<EditorAutomationMCPJsonSchema> schema();
	static EditorAutomationMCPParseResult<EditorAutomationMCPReadEditorLogInput> parse(const Dictionary &p_dict);
	Dictionary to_dictionary() const;
};

struct EditorAutomationMCPRunCommandInput {
	String command;

	static Ref<EditorAutomationMCPJsonSchema> schema();
	static EditorAutomationMCPParseResult<EditorAutomationMCPRunCommandInput> parse(const Dictionary &p_dict);
	Dictionary to_dictionary() const;
};

struct EditorAutomationMCPListCommandsInput {
	Dictionary values;

	static Ref<EditorAutomationMCPJsonSchema> schema();
	static EditorAutomationMCPParseResult<EditorAutomationMCPListCommandsInput> parse(const Dictionary &p_dict);
	Dictionary to_dictionary() const;
};

struct EditorAutomationMCPPollEventsInput {
	Dictionary values;

	static Ref<EditorAutomationMCPJsonSchema> schema();
	static EditorAutomationMCPParseResult<EditorAutomationMCPPollEventsInput> parse(const Dictionary &p_dict);
	Dictionary to_dictionary() const;
};

class EditorAutomationMCPContracts {
public:
	// Shared enum helpers are used by both schemas and parsers. Adding an enum
	// value in only one side silently breaks MCP clients, so route all shared
	// value lists through these helpers.
	static PackedStringArray action_names();
	static PackedStringArray route_enum_values();
	static PackedStringArray severity_enum_values();
	static PackedStringArray scroll_direction_enum_values();
	static PackedStringArray wait_condition_types();
	static PackedStringArray wait_status_enum_values();

	static Array build_tools_list();
	static Array build_resource_templates_list();
	static Array build_resources_list();
};
