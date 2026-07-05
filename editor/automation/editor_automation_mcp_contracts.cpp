/**************************************************************************/
/*  editor_automation_mcp_contracts.cpp                                   */
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

#include "editor_automation_mcp_contracts.h"

namespace {

String _schema_type_name(EditorAutomationMCPJsonSchema::Type p_type) {
	switch (p_type) {
		case EditorAutomationMCPJsonSchema::Type::OBJECT:
			return "object";
		case EditorAutomationMCPJsonSchema::Type::ARRAY:
			return "array";
		case EditorAutomationMCPJsonSchema::Type::STRING:
			return "string";
		case EditorAutomationMCPJsonSchema::Type::INTEGER:
			return "integer";
		case EditorAutomationMCPJsonSchema::Type::NUMBER:
			return "number";
		case EditorAutomationMCPJsonSchema::Type::BOOLEAN:
			return "boolean";
	}
	return String();
}

Array _packed_to_array(const PackedStringArray &p_values) {
	Array values;
	for (int i = 0; i < p_values.size(); i++) {
		values.push_back(p_values[i]);
	}
	return values;
}

bool _enum_has_value(const PackedStringArray &p_values, const String &p_value) {
	return p_values.has(p_value);
}

EditorAutomationMCPParseError _make_error(const String &p_field, const String &p_message) {
	EditorAutomationMCPParseError error;
	error.field = p_field;
	error.message = p_message;
	return error;
}

bool _read_optional_string(const Dictionary &p_dict, const char *p_key, Dictionary &r_values, EditorAutomationMCPParseError &r_error, const PackedStringArray &p_enum_values = PackedStringArray()) {
	if (!p_dict.has(p_key)) {
		return true;
	}
	const Variant value = p_dict.get(p_key, Variant());
	if (value.get_type() != Variant::STRING && value.get_type() != Variant::STRING_NAME) {
		r_error = _make_error(p_key, vformat("'%s' must be a string.", p_key));
		return false;
	}
	const String text = value;
	if (!p_enum_values.is_empty() && !_enum_has_value(p_enum_values, text)) {
		r_error = _make_error(p_key, vformat("'%s' has unsupported value '%s'.", p_key, text));
		return false;
	}
	r_values[p_key] = text;
	return true;
}

bool _read_required_string(const Dictionary &p_dict, const char *p_key, String &r_value, EditorAutomationMCPParseError &r_error, const PackedStringArray &p_enum_values = PackedStringArray()) {
	if (!p_dict.has(p_key)) {
		r_error = _make_error(p_key, vformat("'%s' is required.", p_key));
		return false;
	}
	Dictionary values;
	if (!_read_optional_string(p_dict, p_key, values, r_error, p_enum_values)) {
		return false;
	}
	r_value = values[p_key];
	if (r_value.is_empty()) {
		r_error = _make_error(p_key, vformat("'%s' must not be empty.", p_key));
		return false;
	}
	return true;
}

bool _read_optional_bool(const Dictionary &p_dict, const char *p_key, Dictionary &r_values, EditorAutomationMCPParseError &r_error) {
	if (!p_dict.has(p_key)) {
		return true;
	}
	const Variant value = p_dict.get(p_key, Variant());
	if (value.get_type() != Variant::BOOL) {
		r_error = _make_error(p_key, vformat("'%s' must be a boolean.", p_key));
		return false;
	}
	r_values[p_key] = value;
	return true;
}

bool _read_optional_int(const Dictionary &p_dict, const char *p_key, Dictionary &r_values, EditorAutomationMCPParseError &r_error) {
	if (!p_dict.has(p_key)) {
		return true;
	}
	const Variant value = p_dict.get(p_key, Variant());
	if (!value.is_num()) {
		r_error = _make_error(p_key, vformat("'%s' must be an integer.", p_key));
		return false;
	}
	r_values[p_key] = (int)value;
	return true;
}

bool _read_optional_dictionary(const Dictionary &p_dict, const char *p_key, Dictionary &r_values, EditorAutomationMCPParseError &r_error) {
	if (!p_dict.has(p_key)) {
		return true;
	}
	const Variant value = p_dict.get(p_key, Variant());
	if (value.get_type() != Variant::DICTIONARY) {
		r_error = _make_error(p_key, vformat("'%s' must be an object.", p_key));
		return false;
	}
	r_values[p_key] = value;
	return true;
}

bool _read_optional_number_array(const Dictionary &p_dict, const char *p_key, Dictionary &r_values, EditorAutomationMCPParseError &r_error) {
	if (!p_dict.has(p_key)) {
		return true;
	}
	const Variant value = p_dict.get(p_key, Variant());
	if (value.get_type() != Variant::ARRAY) {
		r_error = _make_error(p_key, vformat("'%s' must be an array.", p_key));
		return false;
	}
	const Array array = value;
	for (int i = 0; i < array.size(); i++) {
		const Variant item = array[i];
		if (!item.is_num()) {
			r_error = _make_error(p_key, vformat("'%s' must contain only numbers.", p_key));
			return false;
		}
	}
	r_values[p_key] = array;
	return true;
}

bool _read_optional_number(const Dictionary &p_dict, const char *p_key, Dictionary &r_values, EditorAutomationMCPParseError &r_error) {
	if (!p_dict.has(p_key)) {
		return true;
	}
	const Variant value = p_dict.get(p_key, Variant());
	if (!value.is_num()) {
		r_error = _make_error(p_key, vformat("'%s' must be a number.", p_key));
		return false;
	}
	r_values[p_key] = value;
	return true;
}

bool _validate_position_spec_dictionary(const Dictionary &p_dict, const String &p_field, EditorAutomationMCPParseError &r_error) {
	if (p_dict.has("position")) {
		const Variant value = p_dict.get("position", Variant());
		if (value.get_type() != Variant::ARRAY) {
			r_error = _make_error("position", "'position' must be an array.");
			r_error.field = p_field + ".position";
			return false;
		}
		const Array array = value;
		for (int i = 0; i < array.size(); i++) {
			if (!array[i].is_num()) {
				r_error = _make_error("position", "'position' must contain only numbers.");
				r_error.field = p_field + ".position";
				return false;
			}
		}
	}
	if (p_dict.has("anchor")) {
		const Variant value = p_dict.get("anchor", Variant());
		if (value.get_type() != Variant::STRING && value.get_type() != Variant::STRING_NAME) {
			r_error = _make_error("anchor", "'anchor' must be a string.");
			r_error.field = p_field + ".anchor";
			return false;
		}
		if (!_enum_has_value(EditorAutomationMCPContracts::pointer_anchor_enum_values(), value)) {
			r_error = _make_error("anchor", vformat("'%s' has unsupported value '%s'.", "anchor", String(value)));
			r_error.field = p_field + ".anchor";
			return false;
		}
	}
	if (p_dict.has("x")) {
		if (!p_dict.get("x", Variant()).is_num()) {
			r_error = _make_error("x", "'x' must be a number.");
			r_error.field = p_field + ".x";
			return false;
		}
	}
	if (p_dict.has("y")) {
		if (!p_dict.get("y", Variant()).is_num()) {
			r_error = _make_error("y", "'y' must be a number.");
			r_error.field = p_field + ".y";
			return false;
		}
	}
	return true;
}

Ref<EditorAutomationMCPJsonSchema> _position_spec_schema(const String &p_description) {
	Ref<EditorAutomationMCPJsonSchema> schema = EditorAutomationMCPJsonSchema::object(p_description);
	schema->add_property("position", EditorAutomationMCPJsonSchema::array(EditorAutomationMCPJsonSchema::number("Coordinate component."), "Relative [x, y] offset inside the element bounds."));
	schema->add_property("anchor", EditorAutomationMCPJsonSchema::enum_string(EditorAutomationMCPContracts::pointer_anchor_enum_values(), "Named anchor within the element bounds."));
	schema->add_property("x", EditorAutomationMCPJsonSchema::number("Horizontal offset. Values in [0, 1] are normalized; otherwise treated as pixels from the element origin."));
	schema->add_property("y", EditorAutomationMCPJsonSchema::number("Vertical offset. Values in [0, 1] are normalized; otherwise treated as pixels from the element origin."));
	return schema;
}

bool _read_optional_string_array(const Dictionary &p_dict, const char *p_key, Dictionary &r_values, EditorAutomationMCPParseError &r_error, const PackedStringArray &p_enum_values = PackedStringArray(), bool p_accept_single_string = false) {
	if (!p_dict.has(p_key)) {
		return true;
	}
	const Variant value = p_dict.get(p_key, Variant());
	if (p_accept_single_string && (value.get_type() == Variant::STRING || value.get_type() == Variant::STRING_NAME)) {
		const String text = value;
		if (!p_enum_values.is_empty() && !_enum_has_value(p_enum_values, text)) {
			r_error = _make_error(p_key, vformat("'%s' has unsupported value '%s'.", p_key, text));
			return false;
		}
		r_values[p_key] = text;
		return true;
	}
	Array result;
	if (value.get_type() == Variant::PACKED_STRING_ARRAY) {
		const PackedStringArray strings = value;
		for (int i = 0; i < strings.size(); i++) {
			if (!p_enum_values.is_empty() && !_enum_has_value(p_enum_values, strings[i])) {
				r_error = _make_error(p_key, vformat("'%s' has unsupported value '%s'.", p_key, strings[i]));
				return false;
			}
			result.push_back(strings[i]);
		}
		r_values[p_key] = result;
		return true;
	}
	if (value.get_type() != Variant::ARRAY) {
		r_error = _make_error(p_key, vformat("'%s' must be an array of strings.", p_key));
		return false;
	}
	const Array array = value;
	for (int i = 0; i < array.size(); i++) {
		const Variant item = array[i];
		if (item.get_type() != Variant::STRING && item.get_type() != Variant::STRING_NAME) {
			r_error = _make_error(p_key, vformat("'%s' must contain only strings.", p_key));
			return false;
		}
		const String text = item;
		if (!p_enum_values.is_empty() && !_enum_has_value(p_enum_values, text)) {
			r_error = _make_error(p_key, vformat("'%s' has unsupported value '%s'.", p_key, text));
			return false;
		}
		result.push_back(text);
	}
	r_values[p_key] = result;
	return true;
}

bool _read_optional_point_path(const Dictionary &p_dict, const char *p_key, Dictionary &r_values, EditorAutomationMCPParseError &r_error) {
	if (!p_dict.has(p_key)) {
		return true;
	}
	const Variant value = p_dict.get(p_key, Variant());
	if (value.get_type() != Variant::ARRAY) {
		r_error = _make_error(p_key, vformat("'%s' must be an array.", p_key));
		return false;
	}
	const Array path = value;
	for (int i = 0; i < path.size(); i++) {
		const Variant point_value = path[i];
		if (point_value.get_type() != Variant::ARRAY) {
			r_error = _make_error(p_key, vformat("'%s' must contain coordinate arrays.", p_key));
			return false;
		}
		const Array point = point_value;
		for (int j = 0; j < point.size(); j++) {
			if (!Variant(point[j]).is_num()) {
				r_error = _make_error(p_key, vformat("'%s' coordinate arrays must contain only numbers.", p_key));
				return false;
			}
		}
	}
	r_values[p_key] = path;
	return true;
}

Ref<EditorAutomationMCPJsonSchema> _element_node_schema() {
	Ref<EditorAutomationMCPJsonSchema> schema = EditorAutomationMCPJsonSchema::object(
			"Semantic UI element node returned by observe_ui, find_elements, and targeted resources. "
			"Use id for immediate same-snapshot follow-up and handle when a later request needs to refer to the same element.");
	schema->add_property("id", EditorAutomationMCPJsonSchema::string("Snapshot-scoped element id. It is precise for the current response, but may be reconciled on later snapshots."));
	schema->add_property("handle", EditorAutomationMCPJsonSchema::string("Durable element handle. Prefer this for targeted resources or follow-up actions that may happen after a new snapshot."));
	schema->add_property("role", EditorAutomationMCPJsonSchema::string("Semantic role, such as button, text_field, menu_item, or panel."));
	schema->add_property("name", EditorAutomationMCPJsonSchema::string("Accessible or visible name used by selector name/name_contains."));
	schema->add_property("text", EditorAutomationMCPJsonSchema::string("Visible text or current value used by selector text/text_contains."));
	schema->add_property("class", EditorAutomationMCPJsonSchema::string("Engine class name for class/class_contains selectors."));
	schema->add_property("path", EditorAutomationMCPJsonSchema::string("Node path, useful as a fallback selector when semantic labels are ambiguous."));
	schema->add_property("visible", EditorAutomationMCPJsonSchema::boolean("Whether the element is visible in the current snapshot."));
	schema->add_property("enabled", EditorAutomationMCPJsonSchema::boolean("Whether the element can currently accept interaction."));
	schema->add_property("focused", EditorAutomationMCPJsonSchema::boolean("Whether the element currently has keyboard focus."));
	schema->add_property("pressed", EditorAutomationMCPJsonSchema::boolean("Whether the element is in a pressed/toggled state."));
	schema->add_property("selected", EditorAutomationMCPJsonSchema::boolean("Whether the element is in a selected state."));
	schema->add_property("internal", EditorAutomationMCPJsonSchema::boolean("True for implementation-detail elements exposed only via include_internal (a SpinBox line edit, Tree/ItemList scrollbars, ...). Absent/false for the stable, user-facing surface; avoid depending on internal elements by default."));
	schema->add_property("bounds", EditorAutomationMCPJsonSchema::array(EditorAutomationMCPJsonSchema::integer("Coordinate or size component."), "Viewport bounds as [x, y, width, height]."));
	schema->add_property("actions", EditorAutomationMCPJsonSchema::array(EditorAutomationMCPJsonSchema::string("Semantic action name."), "Semantic actions the element advertises as likely supported."));
	schema->add_property("metadata", EditorAutomationMCPJsonSchema::object("Additional element metadata for specialized selector matching."));
	schema->add_property("children", EditorAutomationMCPJsonSchema::array(EditorAutomationMCPJsonSchema::object("Child semantic UI element node."), "Child element nodes included in this page."));
	schema->add_property("children_truncated", EditorAutomationMCPJsonSchema::boolean("True when additional children were omitted and children_next_cursor should be used."));
	schema->add_property("child_count", EditorAutomationMCPJsonSchema::integer("Total child count when children are truncated."));
	schema->add_property("children_next_cursor", EditorAutomationMCPJsonSchema::string("Opaque cursor for observe_ui subtree_cursor to fetch the next page of children."));
	return schema;
}

Ref<EditorAutomationMCPJsonSchema> _ok_result_schema() {
	Ref<EditorAutomationMCPJsonSchema> schema = EditorAutomationMCPJsonSchema::object(
			"Common structured tool result. When ok is false, inspect kind, message, and details in the same payload when present.");
	schema->add_property("ok", EditorAutomationMCPJsonSchema::boolean("Whether the requested operation succeeded at the automation layer."));
	return schema;
}

Ref<EditorAutomationMCPJsonSchema> _failure_attachment_schema() {
	PackedStringArray statuses;
	statuses.push_back("available");
	statuses.push_back("unavailable");
	statuses.push_back("truncated");
	PackedStringArray capture_modes;
	capture_modes.push_back("full_window");
	capture_modes.push_back("cropped");

	Ref<EditorAutomationMCPJsonSchema> schema = EditorAutomationMCPJsonSchema::object(
			"Optional screenshot attachment included in failure details when attach_screenshot_on_failure was requested.");
	schema->add_property("status", EditorAutomationMCPJsonSchema::enum_string(statuses, "Screenshot capture status: available, unavailable, or truncated."));
	schema->add_property("reason", EditorAutomationMCPJsonSchema::string("Reason when status is unavailable or truncated."));
	schema->add_property("format", EditorAutomationMCPJsonSchema::string("Image format. Currently png when data is available."));
	schema->add_property("encoding", EditorAutomationMCPJsonSchema::string("Inline payload encoding. Currently base64 when data is available."));
	schema->add_property("data", EditorAutomationMCPJsonSchema::string("Inline image payload when available."));
	schema->add_property("capture_mode", EditorAutomationMCPJsonSchema::enum_string(capture_modes, "Whether the image is full-window or cropped to the target/focused element."));
	schema->add_property("viewport", EditorAutomationMCPJsonSchema::object("Captured viewport/window metadata."));
	schema->add_property("image", EditorAutomationMCPJsonSchema::object("Returned image dimensions."));
	schema->add_property("crop", EditorAutomationMCPJsonSchema::object("Source crop rectangle in viewport coordinates."));
	schema->add_property("highlight", EditorAutomationMCPJsonSchema::object("Target/focused element bounds in viewport coordinates."));
	schema->add_property("byte_size", EditorAutomationMCPJsonSchema::integer("Raw encoded image size in bytes."));
	schema->add_property("max_bytes", EditorAutomationMCPJsonSchema::integer("Configured max inline payload size."));
	return schema;
}

void _add_failure_details_output_props(const Ref<EditorAutomationMCPJsonSchema> &p_output_schema) {
	Ref<EditorAutomationMCPJsonSchema> details_schema = EditorAutomationMCPJsonSchema::object(
			"Failure details payload. Agents should read this before retrying because it may include selector diagnostics, traces, modal state, and screenshots.");
	details_schema->add_property("screenshot", _failure_attachment_schema());
	p_output_schema->add_property("details", details_schema);
}

EditorAutomationMCPToolDefinition _make_tool(const String &p_name, const String &p_description, const Ref<EditorAutomationMCPJsonSchema> &p_input_schema, const Ref<EditorAutomationMCPJsonSchema> &p_output_schema) {
	EditorAutomationMCPToolDefinition tool;
	tool.name = p_name;
	tool.description = p_description;
	tool.input_schema = p_input_schema;
	tool.output_schema = p_output_schema;
	return tool;
}

EditorAutomationMCPResourceDefinition _make_resource(const String &p_uri, const String &p_name, const String &p_description) {
	EditorAutomationMCPResourceDefinition resource;
	resource.uri = p_uri;
	resource.name = p_name;
	resource.description = p_description;
	return resource;
}

EditorAutomationMCPResourceTemplateDefinition _make_resource_template(const String &p_uri_template, const String &p_name, const String &p_description) {
	EditorAutomationMCPResourceTemplateDefinition resource;
	resource.uri_template = p_uri_template;
	resource.name = p_name;
	resource.description = p_description;
	return resource;
}

} // namespace

Ref<EditorAutomationMCPJsonSchema> EditorAutomationMCPJsonSchema::make(Type p_type, const String &p_description) {
	Ref<EditorAutomationMCPJsonSchema> schema;
	schema.instantiate();
	schema->type = p_type;
	schema->description = p_description;
	return schema;
}

Ref<EditorAutomationMCPJsonSchema> EditorAutomationMCPJsonSchema::object(const String &p_description) {
	return make(Type::OBJECT, p_description);
}

Ref<EditorAutomationMCPJsonSchema> EditorAutomationMCPJsonSchema::array(const Ref<EditorAutomationMCPJsonSchema> &p_items, const String &p_description) {
	Ref<EditorAutomationMCPJsonSchema> schema = make(Type::ARRAY, p_description);
	schema->items = p_items;
	return schema;
}

Ref<EditorAutomationMCPJsonSchema> EditorAutomationMCPJsonSchema::string(const String &p_description) {
	return make(Type::STRING, p_description);
}

Ref<EditorAutomationMCPJsonSchema> EditorAutomationMCPJsonSchema::integer(const String &p_description) {
	return make(Type::INTEGER, p_description);
}

Ref<EditorAutomationMCPJsonSchema> EditorAutomationMCPJsonSchema::number(const String &p_description) {
	return make(Type::NUMBER, p_description);
}

Ref<EditorAutomationMCPJsonSchema> EditorAutomationMCPJsonSchema::boolean(const String &p_description) {
	return make(Type::BOOLEAN, p_description);
}

Ref<EditorAutomationMCPJsonSchema> EditorAutomationMCPJsonSchema::enum_string(const PackedStringArray &p_values, const String &p_description) {
	Ref<EditorAutomationMCPJsonSchema> schema = string(p_description);
	schema->enum_values = p_values;
	return schema;
}

EditorAutomationMCPJsonSchema &EditorAutomationMCPJsonSchema::add_property(const StringName &p_name, const Ref<EditorAutomationMCPJsonSchema> &p_schema) {
	Property property;
	property.name = p_name;
	property.schema = p_schema;
	properties.push_back(property);
	return *this;
}

EditorAutomationMCPJsonSchema &EditorAutomationMCPJsonSchema::add_required(const StringName &p_name) {
	required.push_back(String(p_name));
	return *this;
}

EditorAutomationMCPJsonSchema &EditorAutomationMCPJsonSchema::set_additional_properties(bool p_allowed) {
	has_additional_properties = true;
	additional_properties = p_allowed;
	return *this;
}

Dictionary EditorAutomationMCPJsonSchema::to_dictionary() const {
	Dictionary schema;
	schema["type"] = _schema_type_name(type);
	if (!description.is_empty()) {
		schema["description"] = description;
	}
	if (!properties.is_empty()) {
		Dictionary props;
		for (uint32_t i = 0; i < properties.size(); i++) {
			if (properties[i].schema.is_valid()) {
				props[properties[i].name] = properties[i].schema->to_dictionary();
			}
		}
		schema["properties"] = props;
	}
	if (!required.is_empty()) {
		schema["required"] = _packed_to_array(required);
	}
	if (!enum_values.is_empty()) {
		schema["enum"] = _packed_to_array(enum_values);
	}
	if (items.is_valid()) {
		schema["items"] = items->to_dictionary();
	}
	if (has_additional_properties) {
		schema["additionalProperties"] = additional_properties;
	}
	return schema;
}

Dictionary EditorAutomationMCPToolDefinition::to_dictionary() const {
	Dictionary tool;
	tool["name"] = name;
	tool["description"] = description;
	tool["inputSchema"] = input_schema.is_valid() ? input_schema->to_dictionary() : Dictionary();
	tool["outputSchema"] = output_schema.is_valid() ? output_schema->to_dictionary() : Dictionary();
	return tool;
}

Dictionary EditorAutomationMCPResourceDefinition::to_dictionary() const {
	Dictionary resource;
	resource["uri"] = uri;
	resource["name"] = name;
	resource["description"] = description;
	resource["mimeType"] = mime_type;
	return resource;
}

Dictionary EditorAutomationMCPResourceTemplateDefinition::to_dictionary() const {
	Dictionary resource;
	resource["uriTemplate"] = uri_template;
	resource["name"] = name;
	resource["description"] = description;
	resource["mimeType"] = mime_type;
	return resource;
}

Ref<EditorAutomationMCPJsonSchema> EditorAutomationMCPLogMarker::schema() {
	Ref<EditorAutomationMCPJsonSchema> schema = EditorAutomationMCPJsonSchema::object(
			"Log read marker returned by read_editor_log. Pass it back as since to read only entries after the previous response.");
	schema->add_property("message_index", EditorAutomationMCPJsonSchema::integer("Exclusive lower bound message index from the previous marker."));
	return schema;
}

EditorAutomationMCPParseResult<EditorAutomationMCPLogMarker> EditorAutomationMCPLogMarker::parse(const Variant &p_value, const String &p_field) {
	if (p_value.get_type() != Variant::DICTIONARY) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPLogMarker>::invalid(p_field, vformat("'%s' must be an object.", p_field));
	}
	const Dictionary dict = p_value;
	EditorAutomationMCPLogMarker marker;
	Dictionary values;
	EditorAutomationMCPParseError error;
	if (!_read_optional_int(dict, "message_index", values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPLogMarker>::invalid(p_field + ".message_index", error.message);
	}
	if (values.has("message_index")) {
		marker.has_message_index = true;
		marker.message_index = values["message_index"];
	}
	return EditorAutomationMCPParseResult<EditorAutomationMCPLogMarker>::success(marker);
}

Dictionary EditorAutomationMCPLogMarker::to_dictionary() const {
	Dictionary dict;
	if (has_message_index) {
		dict["message_index"] = message_index;
	}
	return dict;
}

Ref<EditorAutomationMCPJsonSchema> EditorAutomationMCPEventMarker::schema() {
	Ref<EditorAutomationMCPJsonSchema> schema = EditorAutomationMCPJsonSchema::object(
			"Automation event marker returned by poll_events. Pass it back as since to continue polling without duplicates.");
	schema->add_property("event_index", EditorAutomationMCPJsonSchema::integer("Exclusive lower bound event index from the previous marker."));
	return schema;
}

EditorAutomationMCPParseResult<EditorAutomationMCPEventMarker> EditorAutomationMCPEventMarker::parse(const Variant &p_value, const String &p_field) {
	if (p_value.get_type() != Variant::DICTIONARY) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPEventMarker>::invalid(p_field, vformat("'%s' must be an object.", p_field));
	}
	const Dictionary dict = p_value;
	EditorAutomationMCPEventMarker marker;
	Dictionary values;
	EditorAutomationMCPParseError error;
	if (!_read_optional_int(dict, "event_index", values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPEventMarker>::invalid(p_field + ".event_index", error.message);
	}
	if (values.has("event_index")) {
		marker.has_event_index = true;
		marker.event_index = values["event_index"];
	}
	return EditorAutomationMCPParseResult<EditorAutomationMCPEventMarker>::success(marker);
}

Dictionary EditorAutomationMCPEventMarker::to_dictionary() const {
	Dictionary dict;
	if (has_event_index) {
		dict["event_index"] = event_index;
	}
	return dict;
}

void EditorAutomationMCPFailureAttachmentOptionsInput::add_schema_properties(const Ref<EditorAutomationMCPJsonSchema> &p_schema) {
	p_schema->add_property("attach_screenshot_on_failure", EditorAutomationMCPJsonSchema::boolean("When true, attach an optional viewport screenshot to failure diagnostics. Use this for ambiguous/no-match/action failures that need visual context."));
	p_schema->add_property("max_screenshot_bytes", EditorAutomationMCPJsonSchema::integer("Maximum inline screenshot payload size in bytes (default 524288). Larger captures are reported as truncated."));
}

EditorAutomationMCPParseResult<EditorAutomationMCPFailureAttachmentOptionsInput> EditorAutomationMCPFailureAttachmentOptionsInput::parse(const Dictionary &p_dict) {
	EditorAutomationMCPFailureAttachmentOptionsInput options;
	Dictionary values;
	EditorAutomationMCPParseError error;
	if (!_read_optional_bool(p_dict, "attach_screenshot_on_failure", values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPFailureAttachmentOptionsInput>::invalid(error.field, error.message);
	}
	if (!_read_optional_int(p_dict, "max_screenshot_bytes", values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPFailureAttachmentOptionsInput>::invalid(error.field, error.message);
	}
	if (values.has("attach_screenshot_on_failure")) {
		options.has_attach_screenshot_on_failure = true;
		options.attach_screenshot_on_failure = values["attach_screenshot_on_failure"];
	}
	if (values.has("max_screenshot_bytes")) {
		options.has_max_screenshot_bytes = true;
		options.max_screenshot_bytes = values["max_screenshot_bytes"];
	}
	return EditorAutomationMCPParseResult<EditorAutomationMCPFailureAttachmentOptionsInput>::success(options);
}

void EditorAutomationMCPFailureAttachmentOptionsInput::append_to_dictionary(Dictionary &r_dict) const {
	if (has_attach_screenshot_on_failure) {
		r_dict["attach_screenshot_on_failure"] = attach_screenshot_on_failure;
	}
	if (has_max_screenshot_bytes) {
		r_dict["max_screenshot_bytes"] = max_screenshot_bytes;
	}
}

Ref<EditorAutomationMCPJsonSchema> EditorAutomationMCPSelector::schema() {
	Ref<EditorAutomationMCPJsonSchema> schema = EditorAutomationMCPJsonSchema::object(
			"Semantic selector used by find_elements, act, wait_for, and nested action targets. Prefer role plus name/text "
			"for resilient automation; use id for immediate follow-up or handle when reusing an element across snapshots.");
	schema->add_property("id", EditorAutomationMCPJsonSchema::string("Snapshot-scoped opaque element id returned by observe_ui/find_elements."));
	schema->add_property("handle", EditorAutomationMCPJsonSchema::string("Durable element handle returned by observe_ui/find_elements."));
	schema->add_property("role", EditorAutomationMCPJsonSchema::string("Exact semantic role, such as button, text_field, menu_item, or panel."));
	schema->add_property("role_contains", EditorAutomationMCPJsonSchema::string("Substring match against role when an exact role is not known."));
	schema->add_property("name", EditorAutomationMCPJsonSchema::string("Exact accessible or visible name."));
	schema->add_property("name_contains", EditorAutomationMCPJsonSchema::string("Substring match against name for partially known labels."));
	schema->add_property("text", EditorAutomationMCPJsonSchema::string("Exact visible text or current value."));
	schema->add_property("text_contains", EditorAutomationMCPJsonSchema::string("Substring match against visible text or current value."));
	schema->add_property("class", EditorAutomationMCPJsonSchema::string("Exact engine class name for low-level disambiguation."));
	schema->add_property("class_contains", EditorAutomationMCPJsonSchema::string("Substring match against engine class name."));
	schema->add_property("path", EditorAutomationMCPJsonSchema::string("Exact node path. Use only when semantic role/name/text are insufficient."));
	schema->add_property("path_contains", EditorAutomationMCPJsonSchema::string("Substring match against node path."));
	schema->add_property("visible", EditorAutomationMCPJsonSchema::boolean("Match the element's visible state exactly."));
	schema->add_property("enabled", EditorAutomationMCPJsonSchema::boolean("Match the element's enabled state exactly."));
	schema->add_property("focused", EditorAutomationMCPJsonSchema::boolean("Match the element's focus state exactly."));
	schema->add_property("visible_only", EditorAutomationMCPJsonSchema::boolean("Keep only visible elements when true."));
	schema->add_property("enabled_only", EditorAutomationMCPJsonSchema::boolean("Keep only enabled elements when true."));
	schema->add_property("selected", EditorAutomationMCPJsonSchema::boolean("Match selected/toggled state when the element exposes one."));
	schema->add_property("metadata", EditorAutomationMCPJsonSchema::object("Metadata key/value pairs to match for specialized controls."));
	schema->add_property("case_sensitive", EditorAutomationMCPJsonSchema::boolean("Use case-sensitive string matching. Defaults to false."));
	schema->add_property("nth", EditorAutomationMCPJsonSchema::integer("Pick the zero-based Nth match after filtering."));
	schema->add_property("index", EditorAutomationMCPJsonSchema::integer("Synonym for nth."));
	schema->add_property("within", EditorAutomationMCPJsonSchema::object("Nested selector scope. Accepts the same selector fields as the parent selector."));
	return schema;
}

EditorAutomationMCPParseResult<EditorAutomationMCPSelector> EditorAutomationMCPSelector::parse(const Variant &p_value, const String &p_field) {
	if (p_value.get_type() != Variant::DICTIONARY) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPSelector>::invalid(p_field, vformat("'%s' must be an object.", p_field));
	}
	const Dictionary dict = p_value;
	EditorAutomationMCPSelector selector;
	EditorAutomationMCPParseError error;
	const char *string_keys[] = {
		"id",
		"handle",
		"role",
		"role_contains",
		"name",
		"name_contains",
		"text",
		"text_contains",
		"class",
		"class_contains",
		"path",
		"path_contains",
	};
	for (const char *key : string_keys) {
		if (!_read_optional_string(dict, key, selector.values, error)) {
			return EditorAutomationMCPParseResult<EditorAutomationMCPSelector>::invalid(p_field + "." + error.field, error.message);
		}
	}
	const char *bool_keys[] = { "visible", "enabled", "focused", "visible_only", "enabled_only", "selected", "case_sensitive" };
	for (const char *key : bool_keys) {
		if (!_read_optional_bool(dict, key, selector.values, error)) {
			return EditorAutomationMCPParseResult<EditorAutomationMCPSelector>::invalid(p_field + "." + error.field, error.message);
		}
	}
	if (!_read_optional_int(dict, "nth", selector.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPSelector>::invalid(p_field + "." + error.field, error.message);
	}
	if (!_read_optional_int(dict, "index", selector.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPSelector>::invalid(p_field + "." + error.field, error.message);
	}
	if (!_read_optional_dictionary(dict, "metadata", selector.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPSelector>::invalid(p_field + "." + error.field, error.message);
	}
	if (dict.has("within")) {
		const EditorAutomationMCPParseResult<EditorAutomationMCPSelector> within = parse(dict.get("within", Variant()), p_field + ".within");
		if (!within.ok) {
			return within;
		}
		selector.values["within"] = within.value.to_dictionary();
	}
	return EditorAutomationMCPParseResult<EditorAutomationMCPSelector>::success(selector);
}

Dictionary EditorAutomationMCPSelector::to_dictionary() const {
	return values;
}

Ref<EditorAutomationMCPJsonSchema> EditorAutomationMCPActionArgs::schema() {
	Ref<EditorAutomationMCPJsonSchema> schema = EditorAutomationMCPJsonSchema::object(
			"Action-specific arguments passed to act. Only provide fields used by the chosen action.");
	schema->add_property("text", EditorAutomationMCPJsonSchema::string("Text for type_text or set_text actions."));
	schema->add_property("key", EditorAutomationMCPJsonSchema::string("Key name for press_key, using the editor's key naming conventions."));
	schema->add_property("value", EditorAutomationMCPJsonSchema::string("Value for set_value actions."));
	schema->add_property("route", EditorAutomationMCPJsonSchema::enum_string(EditorAutomationMCPContracts::route_enum_values(), "Route preference override for this action."));
	schema->add_property("button", EditorAutomationMCPJsonSchema::string("Mouse button for click or drag actions."));
	schema->add_property("modifiers", EditorAutomationMCPJsonSchema::array(EditorAutomationMCPJsonSchema::string("Modifier key name."), "Modifier keys to hold during input-routed actions."));
	schema->add_property("position", EditorAutomationMCPJsonSchema::array(EditorAutomationMCPJsonSchema::number("Coordinate component."), "Source-side relative click/drag start position [x, y] inside the selected element."));
	schema->add_property("anchor", EditorAutomationMCPJsonSchema::enum_string(EditorAutomationMCPContracts::pointer_anchor_enum_values(), "Source-side anchor for click/drag start within the selected element."));
	schema->add_property("x", EditorAutomationMCPJsonSchema::number("Source-side horizontal offset for click/drag start. Values in [0, 1] are normalized."));
	schema->add_property("y", EditorAutomationMCPJsonSchema::number("Source-side vertical offset for click/drag start. Values in [0, 1] are normalized."));
	schema->add_property("source", _position_spec_schema("Source-side positioning override for drag/click. Top-level position/anchor/x/y are shorthand for these fields."));
	schema->add_property("direction", EditorAutomationMCPJsonSchema::enum_string(EditorAutomationMCPContracts::scroll_direction_enum_values(), "Scroll direction."));
	schema->add_property("amount", EditorAutomationMCPJsonSchema::integer("Scroll amount in wheel steps unless page is true."));
	schema->add_property("page", EditorAutomationMCPJsonSchema::boolean("Scroll by page when true."));
	schema->add_property("target", EditorAutomationMCPSelector::schema());
	schema->add_property("target_position", _position_spec_schema("Target-side positioning when drag uses a target selector. Does not apply to target_point."));
	schema->add_property("target_anchor", EditorAutomationMCPJsonSchema::enum_string(EditorAutomationMCPContracts::pointer_anchor_enum_values(), "Target-side anchor shorthand when drag uses a target selector."));
	schema->add_property("target_x", EditorAutomationMCPJsonSchema::number("Target-side horizontal offset shorthand when drag uses a target selector."));
	schema->add_property("target_y", EditorAutomationMCPJsonSchema::number("Target-side vertical offset shorthand when drag uses a target selector."));
	schema->add_property("target_point", EditorAutomationMCPJsonSchema::array(EditorAutomationMCPJsonSchema::number("Coordinate component."), "Absolute drag release point [x, y] in global coordinates."));
	schema->add_property("waypoints", EditorAutomationMCPJsonSchema::array(EditorAutomationMCPJsonSchema::array(EditorAutomationMCPJsonSchema::number("Coordinate component.")), "Drag path as absolute [x, y] waypoint arrays."));
	schema->add_property("path", EditorAutomationMCPJsonSchema::array(EditorAutomationMCPJsonSchema::array(EditorAutomationMCPJsonSchema::number("Coordinate component.")), "Alias for waypoints."));
	return schema;
}

EditorAutomationMCPParseResult<EditorAutomationMCPActionArgs> EditorAutomationMCPActionArgs::parse(const Variant &p_value, const String &p_field) {
	if (p_value.get_type() == Variant::NIL) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPActionArgs>::success(EditorAutomationMCPActionArgs());
	}
	if (p_value.get_type() != Variant::DICTIONARY) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPActionArgs>::invalid(p_field, vformat("'%s' must be an object.", p_field));
	}
	const Dictionary dict = p_value;
	EditorAutomationMCPActionArgs args;
	EditorAutomationMCPParseError error;
	const char *string_keys[] = { "text", "key", "value", "button" };
	for (const char *key : string_keys) {
		if (!_read_optional_string(dict, key, args.values, error)) {
			return EditorAutomationMCPParseResult<EditorAutomationMCPActionArgs>::invalid(p_field + "." + error.field, error.message);
		}
	}
	if (!_read_optional_string(dict, "route", args.values, error, EditorAutomationMCPContracts::route_enum_values())) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPActionArgs>::invalid(p_field + "." + error.field, error.message);
	}
	if (!_read_optional_string_array(dict, "modifiers", args.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPActionArgs>::invalid(p_field + "." + error.field, error.message);
	}
	if (!_read_optional_number_array(dict, "position", args.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPActionArgs>::invalid(p_field + "." + error.field, error.message);
	}
	if (!_read_optional_string(dict, "anchor", args.values, error, EditorAutomationMCPContracts::pointer_anchor_enum_values())) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPActionArgs>::invalid(p_field + "." + error.field, error.message);
	}
	if (!_read_optional_number(dict, "x", args.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPActionArgs>::invalid(p_field + "." + error.field, error.message);
	}
	if (!_read_optional_number(dict, "y", args.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPActionArgs>::invalid(p_field + "." + error.field, error.message);
	}
	if (dict.has("source")) {
		const Variant source_value = dict.get("source", Variant());
		if (source_value.get_type() != Variant::DICTIONARY) {
			return EditorAutomationMCPParseResult<EditorAutomationMCPActionArgs>::invalid(p_field + ".source", "'source' must be an object.");
		}
		const Dictionary source_dict = source_value;
		if (!_validate_position_spec_dictionary(source_dict, p_field + ".source", error)) {
			return EditorAutomationMCPParseResult<EditorAutomationMCPActionArgs>::invalid(error.field, error.message);
		}
		args.values["source"] = source_dict;
	}
	if (!_read_optional_string(dict, "direction", args.values, error, EditorAutomationMCPContracts::scroll_direction_enum_values())) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPActionArgs>::invalid(p_field + "." + error.field, error.message);
	}
	if (!_read_optional_int(dict, "amount", args.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPActionArgs>::invalid(p_field + "." + error.field, error.message);
	}
	if (!_read_optional_bool(dict, "page", args.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPActionArgs>::invalid(p_field + "." + error.field, error.message);
	}
	if (dict.has("target")) {
		const EditorAutomationMCPParseResult<EditorAutomationMCPSelector> target = EditorAutomationMCPSelector::parse(dict.get("target", Variant()), p_field + ".target");
		if (!target.ok) {
			return EditorAutomationMCPParseResult<EditorAutomationMCPActionArgs>::invalid(target.error.field, target.error.message);
		}
		args.values["target"] = target.value.to_dictionary();
	}
	if (dict.has("target_position")) {
		const Variant target_position_value = dict.get("target_position", Variant());
		if (target_position_value.get_type() != Variant::DICTIONARY) {
			return EditorAutomationMCPParseResult<EditorAutomationMCPActionArgs>::invalid(p_field + ".target_position", "'target_position' must be an object.");
		}
		const Dictionary target_position_dict = target_position_value;
		if (!_validate_position_spec_dictionary(target_position_dict, p_field + ".target_position", error)) {
			return EditorAutomationMCPParseResult<EditorAutomationMCPActionArgs>::invalid(error.field, error.message);
		}
		args.values["target_position"] = target_position_dict;
	}
	if (!_read_optional_string(dict, "target_anchor", args.values, error, EditorAutomationMCPContracts::pointer_anchor_enum_values())) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPActionArgs>::invalid(p_field + "." + error.field, error.message);
	}
	if (!_read_optional_number(dict, "target_x", args.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPActionArgs>::invalid(p_field + "." + error.field, error.message);
	}
	if (!_read_optional_number(dict, "target_y", args.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPActionArgs>::invalid(p_field + "." + error.field, error.message);
	}
	if (!_read_optional_number_array(dict, "target_point", args.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPActionArgs>::invalid(p_field + "." + error.field, error.message);
	}
	if (!_read_optional_point_path(dict, "waypoints", args.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPActionArgs>::invalid(p_field + "." + error.field, error.message);
	}
	if (!_read_optional_point_path(dict, "path", args.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPActionArgs>::invalid(p_field + "." + error.field, error.message);
	}
	return EditorAutomationMCPParseResult<EditorAutomationMCPActionArgs>::success(args);
}

Dictionary EditorAutomationMCPActionArgs::to_dictionary() const {
	return values;
}

Ref<EditorAutomationMCPJsonSchema> EditorAutomationMCPWaitCondition::schema() {
	Ref<EditorAutomationMCPJsonSchema> schema = EditorAutomationMCPJsonSchema::object(
			"Wait condition object used by act.wait. Use type or condition, plus the fields required by that condition kind.");
	schema->add_property("type", EditorAutomationMCPJsonSchema::enum_string(EditorAutomationMCPContracts::wait_condition_types(), "Wait condition kind."));
	schema->add_property("condition", EditorAutomationMCPJsonSchema::enum_string(EditorAutomationMCPContracts::wait_condition_types(), "Shorthand alias for type."));
	schema->add_property("selector", EditorAutomationMCPSelector::schema());
	schema->add_property("text", EditorAutomationMCPJsonSchema::string("Substring for log_contains conditions."));
	schema->add_property("severity", EditorAutomationMCPJsonSchema::enum_string(EditorAutomationMCPContracts::severity_enum_values(), "Severity filter."));
	schema->add_property("severities", EditorAutomationMCPJsonSchema::array(EditorAutomationMCPJsonSchema::string("Log severity."), "Multiple severities."));
	schema->add_property("marker", EditorAutomationMCPLogMarker::schema());
	schema->add_property("fields", EditorAutomationMCPJsonSchema::object("Field equality checks for selector_matches."));
	schema->add_property("baseline", EditorAutomationMCPJsonSchema::object("Baseline modal stack for modal_stack_changed conditions."));
	return schema;
}

EditorAutomationMCPParseResult<EditorAutomationMCPWaitCondition> EditorAutomationMCPWaitCondition::parse(const Variant &p_value, const String &p_field) {
	if (p_value.get_type() == Variant::NIL) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPWaitCondition>::success(EditorAutomationMCPWaitCondition());
	}
	if (p_value.get_type() != Variant::DICTIONARY) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPWaitCondition>::invalid(p_field, vformat("'%s' must be an object.", p_field));
	}
	const Dictionary dict = p_value;
	EditorAutomationMCPWaitCondition condition;
	EditorAutomationMCPParseError error;
	if (!_read_optional_string(dict, "type", condition.values, error, EditorAutomationMCPContracts::wait_condition_types())) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPWaitCondition>::invalid(p_field + "." + error.field, error.message);
	}
	if (!_read_optional_string(dict, "condition", condition.values, error, EditorAutomationMCPContracts::wait_condition_types())) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPWaitCondition>::invalid(p_field + "." + error.field, error.message);
	}
	if (dict.has("selector")) {
		const EditorAutomationMCPParseResult<EditorAutomationMCPSelector> selector = EditorAutomationMCPSelector::parse(dict.get("selector", Variant()), p_field + ".selector");
		if (!selector.ok) {
			return EditorAutomationMCPParseResult<EditorAutomationMCPWaitCondition>::invalid(selector.error.field, selector.error.message);
		}
		condition.values["selector"] = selector.value.to_dictionary();
	}
	if (!_read_optional_string(dict, "text", condition.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPWaitCondition>::invalid(p_field + "." + error.field, error.message);
	}
	if (!_read_optional_string(dict, "severity", condition.values, error, EditorAutomationMCPContracts::severity_enum_values())) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPWaitCondition>::invalid(p_field + "." + error.field, error.message);
	}
	if (!_read_optional_string_array(dict, "severities", condition.values, error, EditorAutomationMCPContracts::severity_enum_values())) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPWaitCondition>::invalid(p_field + "." + error.field, error.message);
	}
	if (dict.has("marker")) {
		const EditorAutomationMCPParseResult<EditorAutomationMCPLogMarker> marker = EditorAutomationMCPLogMarker::parse(dict.get("marker", Variant()), p_field + ".marker");
		if (!marker.ok) {
			return EditorAutomationMCPParseResult<EditorAutomationMCPWaitCondition>::invalid(marker.error.field, marker.error.message);
		}
		condition.values["marker"] = marker.value.to_dictionary();
	}
	if (!_read_optional_dictionary(dict, "fields", condition.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPWaitCondition>::invalid(p_field + "." + error.field, error.message);
	}
	if (!_read_optional_dictionary(dict, "baseline", condition.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPWaitCondition>::invalid(p_field + "." + error.field, error.message);
	}
	return EditorAutomationMCPParseResult<EditorAutomationMCPWaitCondition>::success(condition);
}

Dictionary EditorAutomationMCPWaitCondition::to_dictionary() const {
	return values;
}

bool EditorAutomationMCPWaitCondition::is_empty() const {
	return values.is_empty();
}

Ref<EditorAutomationMCPJsonSchema> EditorAutomationMCPToolFailure::schema() {
	Ref<EditorAutomationMCPJsonSchema> schema = EditorAutomationMCPJsonSchema::object(
			"Structured automation failure. Tool results use this shape inside structuredContent while transport errors use JSON-RPC errors.");
	schema->add_property("ok", EditorAutomationMCPJsonSchema::boolean("Always false for structured automation failures."));
	schema->add_property("kind", EditorAutomationMCPJsonSchema::string("Stable failure kind suitable for branching retry logic."));
	schema->add_property("message", EditorAutomationMCPJsonSchema::string("Human-readable failure message."));
	schema->add_property("details", EditorAutomationMCPJsonSchema::object("Structured failure diagnostics for selectors, action traces, modal state, or screenshots."));
	return schema;
}

Dictionary EditorAutomationMCPToolFailure::to_dictionary() const {
	Dictionary dict;
	dict["ok"] = false;
	if (!kind.is_empty()) {
		dict["kind"] = kind;
	}
	if (!message.is_empty()) {
		dict["message"] = message;
	}
	if (!details.is_empty()) {
		dict["details"] = details;
	}
	return dict;
}

EditorAutomationMCPElementNode EditorAutomationMCPElementNode::from_element(const EditorAutomationElement &p_element) {
	EditorAutomationMCPElementNode node;
	node.id = p_element.id;
	node.handle = p_element.handle;
	node.role = p_element.role;
	node.name = p_element.name;
	node.text = p_element.text;
	node.class_name = p_element.class_name;
	node.path = p_element.path;
	node.visible = p_element.visible;
	node.enabled = p_element.enabled;
	node.focused = p_element.focused;
	node.pressed = p_element.pressed;
	node.selected = p_element.selected;
	node.internal = p_element.internal;
	node.bounds = p_element.bounds;
	node.actions = p_element.actions;
	node.metadata = p_element.metadata;
	return node;
}

Dictionary EditorAutomationMCPElementNode::to_dictionary() const {
	Dictionary dict;
	dict["id"] = id;
	dict["handle"] = handle;
	dict["role"] = role;
	dict["name"] = name;
	dict["text"] = text;
	dict["class"] = class_name;
	dict["path"] = path;
	dict["visible"] = visible;
	dict["enabled"] = enabled;
	dict["focused"] = focused;
	dict["pressed"] = pressed;
	dict["selected"] = selected;
	if (internal) {
		dict["internal"] = true;
	}

	Array bounds_array;
	bounds_array.push_back(bounds.position.x);
	bounds_array.push_back(bounds.position.y);
	bounds_array.push_back(bounds.size.x);
	bounds_array.push_back(bounds.size.y);
	dict["bounds"] = bounds_array;
	dict["actions"] = actions;
	if (!metadata.is_empty()) {
		dict["metadata"] = metadata;
	}
	if (has_children) {
		dict["children"] = children;
	}
	if (children_truncated) {
		dict["children_truncated"] = true;
		dict["child_count"] = child_count;
		if (!children_next_cursor.is_empty()) {
			dict["children_next_cursor"] = children_next_cursor;
		}
	}
	return dict;
}

Ref<EditorAutomationMCPJsonSchema> EditorAutomationMCPObserveUIInput::schema() {
	Ref<EditorAutomationMCPJsonSchema> schema = EditorAutomationMCPJsonSchema::object(
			"Input for observe_ui. Use this first when an agent needs a fresh semantic UI snapshot. "
			"Prefer subtree_cursor pagination over raising max_depth or max_children for large trees.");
	schema->add_property("max_depth", EditorAutomationMCPJsonSchema::integer("Maximum tree depth to include from each root (default 8)."));
	schema->add_property("include_hidden", EditorAutomationMCPJsonSchema::boolean("Include hidden elements when true. Defaults to false for action-oriented snapshots."));
	schema->add_property("include_internal", EditorAutomationMCPJsonSchema::boolean("Include internal implementation children of controls (SpinBox line edits, Tree/ItemList scrollbars, ...) flagged internal:true (default false). Window/dialog internals such as dialog buttons are always included."));
	schema->add_property("max_children", EditorAutomationMCPJsonSchema::integer("Maximum children per node before pagination (default 32)."));
	schema->add_property("subtree_cursor", EditorAutomationMCPJsonSchema::string("Opaque cursor from children_next_cursor to fetch the next child page without refreshing the whole tree. The cursor remembers include_hidden/include_internal from the originating call."));
	return schema;
}

EditorAutomationMCPParseResult<EditorAutomationMCPObserveUIInput> EditorAutomationMCPObserveUIInput::parse(const Dictionary &p_dict) {
	EditorAutomationMCPObserveUIInput input;
	EditorAutomationMCPParseError error;
	if (!_read_optional_int(p_dict, "max_depth", input.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPObserveUIInput>::invalid(error.field, error.message);
	}
	if (!_read_optional_bool(p_dict, "include_hidden", input.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPObserveUIInput>::invalid(error.field, error.message);
	}
	if (!_read_optional_bool(p_dict, "include_internal", input.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPObserveUIInput>::invalid(error.field, error.message);
	}
	if (!_read_optional_int(p_dict, "max_children", input.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPObserveUIInput>::invalid(error.field, error.message);
	}
	if (!_read_optional_string(p_dict, "subtree_cursor", input.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPObserveUIInput>::invalid(error.field, error.message);
	}
	return EditorAutomationMCPParseResult<EditorAutomationMCPObserveUIInput>::success(input);
}

Dictionary EditorAutomationMCPObserveUIInput::to_dictionary() const {
	return values;
}

Ref<EditorAutomationMCPJsonSchema> EditorAutomationMCPFindElementsInput::schema() {
	Ref<EditorAutomationMCPJsonSchema> schema = EditorAutomationMCPJsonSchema::object(
			"Input for find_elements. Use this to resolve a selector before acting, inspect ambiguity, or page through large match sets.");
	schema->add_property("selector", EditorAutomationMCPSelector::schema());
	schema->add_property("max_results", EditorAutomationMCPJsonSchema::integer("Maximum number of matches to return per page (default 20)."));
	schema->add_property("cursor", EditorAutomationMCPJsonSchema::string("Opaque pagination cursor returned as next_cursor from a previous find_elements response."));
	schema->add_property("include_internal", EditorAutomationMCPJsonSchema::boolean("Match against internal implementation children too (default false)."));
	EditorAutomationMCPFailureAttachmentOptionsInput::add_schema_properties(schema);
	schema->add_required("selector");
	return schema;
}

EditorAutomationMCPParseResult<EditorAutomationMCPFindElementsInput> EditorAutomationMCPFindElementsInput::parse(const Dictionary &p_dict) {
	if (!p_dict.has("selector")) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPFindElementsInput>::invalid("selector", "'selector' is required.");
	}
	EditorAutomationMCPFindElementsInput input;
	const EditorAutomationMCPParseResult<EditorAutomationMCPSelector> selector = EditorAutomationMCPSelector::parse(p_dict.get("selector", Variant()));
	if (!selector.ok) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPFindElementsInput>::invalid(selector.error.field, selector.error.message);
	}
	input.selector = selector.value;
	input.values["selector"] = selector.value.to_dictionary();
	EditorAutomationMCPParseError error;
	if (!_read_optional_int(p_dict, "max_results", input.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPFindElementsInput>::invalid(error.field, error.message);
	}
	if (!_read_optional_string(p_dict, "cursor", input.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPFindElementsInput>::invalid(error.field, error.message);
	}
	if (!_read_optional_bool(p_dict, "include_internal", input.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPFindElementsInput>::invalid(error.field, error.message);
	}
	const EditorAutomationMCPParseResult<EditorAutomationMCPFailureAttachmentOptionsInput> attachments = EditorAutomationMCPFailureAttachmentOptionsInput::parse(p_dict);
	if (!attachments.ok) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPFindElementsInput>::invalid(attachments.error.field, attachments.error.message);
	}
	input.failure_attachments = attachments.value;
	input.failure_attachments.append_to_dictionary(input.values);
	return EditorAutomationMCPParseResult<EditorAutomationMCPFindElementsInput>::success(input);
}

Dictionary EditorAutomationMCPFindElementsInput::to_dictionary() const {
	return values;
}

Ref<EditorAutomationMCPJsonSchema> EditorAutomationMCPActInput::schema() {
	Ref<EditorAutomationMCPJsonSchema> schema = EditorAutomationMCPJsonSchema::object(
			"Input for act. Provide action and selector for a new interaction; optionally include wait to combine action and postcondition. "
			"When polling a pending cooperative act+wait, pass wait_id without repeating selector/action.");
	schema->add_property("selector", EditorAutomationMCPSelector::schema());
	schema->add_property("action", EditorAutomationMCPJsonSchema::enum_string(EditorAutomationMCPContracts::action_names(), "Action to perform for a new interaction."));
	schema->add_property("route", EditorAutomationMCPJsonSchema::enum_string(EditorAutomationMCPContracts::route_enum_values(), "Route preference: auto, semantic, or input."));
	schema->add_property("args", EditorAutomationMCPActionArgs::schema());
	schema->add_property("wait", EditorAutomationMCPWaitCondition::schema());
	schema->add_property("wait_timeout_ms", EditorAutomationMCPJsonSchema::integer("Timeout in milliseconds for the optional wait clause (default 5000)."));
	schema->add_property("wait_id", EditorAutomationMCPJsonSchema::string("Poll or continue an existing cooperative act+wait by id."));
	schema->add_property("include_internal", EditorAutomationMCPJsonSchema::boolean("Resolve the selector against internal implementation children too (default false). Required when targeting an element observed with include_internal."));
	EditorAutomationMCPFailureAttachmentOptionsInput::add_schema_properties(schema);
	// `action` is required only for new interactions. Polling an existing
	// cooperative act+wait uses wait_id without repeating action/selector; the
	// parser enforces that conditional requirement.
	return schema;
}

EditorAutomationMCPParseResult<EditorAutomationMCPActInput> EditorAutomationMCPActInput::parse(const Dictionary &p_dict) {
	EditorAutomationMCPActInput input;
	EditorAutomationMCPParseError error;
	if (!_read_optional_string(p_dict, "wait_id", input.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPActInput>::invalid(error.field, error.message);
	}
	const bool has_wait_id = input.values.has("wait_id") && !String(input.values["wait_id"]).is_empty();
	if (!has_wait_id) {
		String action;
		if (!_read_required_string(p_dict, "action", action, error, EditorAutomationMCPContracts::action_names())) {
			return EditorAutomationMCPParseResult<EditorAutomationMCPActInput>::invalid(error.field, error.message);
		}
		input.values["action"] = action;
	}
	if (p_dict.has("selector")) {
		const EditorAutomationMCPParseResult<EditorAutomationMCPSelector> selector = EditorAutomationMCPSelector::parse(p_dict.get("selector", Variant()));
		if (!selector.ok) {
			return EditorAutomationMCPParseResult<EditorAutomationMCPActInput>::invalid(selector.error.field, selector.error.message);
		}
		input.values["selector"] = selector.value.to_dictionary();
	}
	if (!_read_optional_string(p_dict, "route", input.values, error, EditorAutomationMCPContracts::route_enum_values())) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPActInput>::invalid(error.field, error.message);
	}
	if (p_dict.has("args")) {
		const EditorAutomationMCPParseResult<EditorAutomationMCPActionArgs> args = EditorAutomationMCPActionArgs::parse(p_dict.get("args", Variant()));
		if (!args.ok) {
			return EditorAutomationMCPParseResult<EditorAutomationMCPActInput>::invalid(args.error.field, args.error.message);
		}
		input.values["args"] = args.value.to_dictionary();
	}
	if (p_dict.has("wait")) {
		const EditorAutomationMCPParseResult<EditorAutomationMCPWaitCondition> wait = EditorAutomationMCPWaitCondition::parse(p_dict.get("wait", Variant()));
		if (!wait.ok) {
			return EditorAutomationMCPParseResult<EditorAutomationMCPActInput>::invalid(wait.error.field, wait.error.message);
		}
		input.values["wait"] = wait.value.to_dictionary();
	}
	if (!_read_optional_int(p_dict, "wait_timeout_ms", input.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPActInput>::invalid(error.field, error.message);
	}
	if (!_read_optional_bool(p_dict, "include_internal", input.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPActInput>::invalid(error.field, error.message);
	}
	const EditorAutomationMCPParseResult<EditorAutomationMCPFailureAttachmentOptionsInput> attachments = EditorAutomationMCPFailureAttachmentOptionsInput::parse(p_dict);
	if (!attachments.ok) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPActInput>::invalid(attachments.error.field, attachments.error.message);
	}
	input.failure_attachments = attachments.value;
	input.failure_attachments.append_to_dictionary(input.values);
	return EditorAutomationMCPParseResult<EditorAutomationMCPActInput>::success(input);
}

Dictionary EditorAutomationMCPActInput::to_dictionary() const {
	return values;
}

Ref<EditorAutomationMCPJsonSchema> EditorAutomationMCPWaitForInput::schema() {
	Ref<EditorAutomationMCPJsonSchema> schema = EditorAutomationMCPJsonSchema::object(
			"Input for wait_for. Start a wait with condition/type, or pass wait_id to poll/cancel a cooperative wait returned earlier.");
	schema->add_property("condition", EditorAutomationMCPJsonSchema::enum_string(EditorAutomationMCPContracts::wait_condition_types(), "Wait condition shorthand."));
	schema->add_property("type", EditorAutomationMCPJsonSchema::enum_string(EditorAutomationMCPContracts::wait_condition_types(), "Wait condition kind."));
	schema->add_property("selector", EditorAutomationMCPSelector::schema());
	schema->add_property("timeout_ms", EditorAutomationMCPJsonSchema::integer("Timeout in milliseconds (default 5000)."));
	schema->add_property("wait_id", EditorAutomationMCPJsonSchema::string("Poll or cancel an existing cooperative wait by id."));
	schema->add_property("cancel", EditorAutomationMCPJsonSchema::boolean("When true with wait_id, cancel the pending wait."));
	schema->add_property("cooperative", EditorAutomationMCPJsonSchema::boolean("When true (default), return quickly with wait_id while the wait is pending instead of blocking until timeout."));
	schema->add_property("text", EditorAutomationMCPJsonSchema::string("Substring for log_contains conditions."));
	schema->add_property("severity", EditorAutomationMCPJsonSchema::string("Severity filter for log conditions."));
	schema->add_property("marker", EditorAutomationMCPLogMarker::schema());
	schema->add_property("fields", EditorAutomationMCPJsonSchema::object("Field equality checks for selector_matches."));
	EditorAutomationMCPFailureAttachmentOptionsInput::add_schema_properties(schema);
	return schema;
}

EditorAutomationMCPParseResult<EditorAutomationMCPWaitForInput> EditorAutomationMCPWaitForInput::parse(const Dictionary &p_dict) {
	EditorAutomationMCPWaitForInput input;
	EditorAutomationMCPParseError error;
	if (!_read_optional_string(p_dict, "wait_id", input.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPWaitForInput>::invalid(error.field, error.message);
	}
	const bool has_wait_id = input.values.has("wait_id") && !String(input.values["wait_id"]).is_empty();
	if (!_read_optional_bool(p_dict, "cancel", input.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPWaitForInput>::invalid(error.field, error.message);
	}
	if (!_read_optional_bool(p_dict, "cooperative", input.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPWaitForInput>::invalid(error.field, error.message);
	}
	if (!_read_optional_string(p_dict, "condition", input.values, error, EditorAutomationMCPContracts::wait_condition_types())) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPWaitForInput>::invalid(error.field, error.message);
	}
	if (!_read_optional_string(p_dict, "type", input.values, error, EditorAutomationMCPContracts::wait_condition_types())) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPWaitForInput>::invalid(error.field, error.message);
	}
	if (!has_wait_id && !input.values.has("condition") && !input.values.has("type")) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPWaitForInput>::invalid("condition", "wait_for requires 'condition' or 'wait_id'.");
	}
	if (p_dict.has("selector")) {
		const EditorAutomationMCPParseResult<EditorAutomationMCPSelector> selector = EditorAutomationMCPSelector::parse(p_dict.get("selector", Variant()));
		if (!selector.ok) {
			return EditorAutomationMCPParseResult<EditorAutomationMCPWaitForInput>::invalid(selector.error.field, selector.error.message);
		}
		input.values["selector"] = selector.value.to_dictionary();
	}
	if (!_read_optional_int(p_dict, "timeout_ms", input.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPWaitForInput>::invalid(error.field, error.message);
	}
	if (!_read_optional_string(p_dict, "text", input.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPWaitForInput>::invalid(error.field, error.message);
	}
	if (!_read_optional_string(p_dict, "severity", input.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPWaitForInput>::invalid(error.field, error.message);
	}
	if (p_dict.has("marker")) {
		const EditorAutomationMCPParseResult<EditorAutomationMCPLogMarker> marker = EditorAutomationMCPLogMarker::parse(p_dict.get("marker", Variant()), "marker");
		if (!marker.ok) {
			return EditorAutomationMCPParseResult<EditorAutomationMCPWaitForInput>::invalid(marker.error.field, marker.error.message);
		}
		input.values["marker"] = marker.value.to_dictionary();
	}
	if (!_read_optional_dictionary(p_dict, "fields", input.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPWaitForInput>::invalid(error.field, error.message);
	}
	const EditorAutomationMCPParseResult<EditorAutomationMCPFailureAttachmentOptionsInput> attachments = EditorAutomationMCPFailureAttachmentOptionsInput::parse(p_dict);
	if (!attachments.ok) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPWaitForInput>::invalid(attachments.error.field, attachments.error.message);
	}
	input.failure_attachments = attachments.value;
	input.failure_attachments.append_to_dictionary(input.values);
	return EditorAutomationMCPParseResult<EditorAutomationMCPWaitForInput>::success(input);
}

Dictionary EditorAutomationMCPWaitForInput::to_dictionary() const {
	return values;
}

Ref<EditorAutomationMCPJsonSchema> EditorAutomationMCPReadEditorStateInput::schema() {
	return EditorAutomationMCPJsonSchema::object(
			"Input for read_editor_state. No arguments are currently accepted; call this for a lightweight editor/session summary.");
}

EditorAutomationMCPParseResult<EditorAutomationMCPReadEditorStateInput> EditorAutomationMCPReadEditorStateInput::parse(const Dictionary &p_dict) {
	return EditorAutomationMCPParseResult<EditorAutomationMCPReadEditorStateInput>::success(EditorAutomationMCPReadEditorStateInput());
}

Dictionary EditorAutomationMCPReadEditorStateInput::to_dictionary() const {
	return Dictionary();
}

Ref<EditorAutomationMCPJsonSchema> EditorAutomationMCPReadEditorLogInput::schema() {
	Ref<EditorAutomationMCPJsonSchema> schema = EditorAutomationMCPJsonSchema::object(
			"Input for read_editor_log. Use since with the marker from the previous response for incremental log polling.");
	schema->add_property("severity", EditorAutomationMCPJsonSchema::enum_string(EditorAutomationMCPContracts::severity_enum_values(), "Optional severity filter."));
	schema->add_property("since", EditorAutomationMCPLogMarker::schema());
	schema->add_property("limit", EditorAutomationMCPJsonSchema::integer("Maximum recent entries when no marker is provided (default 64)."));
	return schema;
}

EditorAutomationMCPParseResult<EditorAutomationMCPReadEditorLogInput> EditorAutomationMCPReadEditorLogInput::parse(const Dictionary &p_dict) {
	EditorAutomationMCPReadEditorLogInput input;
	EditorAutomationMCPParseError error;
	if (!_read_optional_string(p_dict, "severity", input.values, error, EditorAutomationMCPContracts::severity_enum_values())) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPReadEditorLogInput>::invalid(error.field, error.message);
	}
	if (p_dict.has("since")) {
		const EditorAutomationMCPParseResult<EditorAutomationMCPLogMarker> marker = EditorAutomationMCPLogMarker::parse(p_dict.get("since", Variant()), "since");
		if (!marker.ok) {
			return EditorAutomationMCPParseResult<EditorAutomationMCPReadEditorLogInput>::invalid(marker.error.field, marker.error.message);
		}
		input.values["since"] = marker.value.to_dictionary();
	}
	if (!_read_optional_int(p_dict, "limit", input.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPReadEditorLogInput>::invalid(error.field, error.message);
	}
	return EditorAutomationMCPParseResult<EditorAutomationMCPReadEditorLogInput>::success(input);
}

Dictionary EditorAutomationMCPReadEditorLogInput::to_dictionary() const {
	return values;
}

Ref<EditorAutomationMCPJsonSchema> EditorAutomationMCPRunCommandInput::schema() {
	Ref<EditorAutomationMCPJsonSchema> schema = EditorAutomationMCPJsonSchema::object(
			"Input for run_command. Get valid command keys from list_commands or foundry://commands before executing.");
	schema->add_property("command", EditorAutomationMCPJsonSchema::string("Command palette command key or editor shortcut path to execute."));
	schema->add_required("command");
	return schema;
}

EditorAutomationMCPParseResult<EditorAutomationMCPRunCommandInput> EditorAutomationMCPRunCommandInput::parse(const Dictionary &p_dict) {
	EditorAutomationMCPRunCommandInput input;
	EditorAutomationMCPParseError error;
	if (!_read_required_string(p_dict, "command", input.command, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPRunCommandInput>::invalid(error.field, error.message);
	}
	return EditorAutomationMCPParseResult<EditorAutomationMCPRunCommandInput>::success(input);
}

Dictionary EditorAutomationMCPRunCommandInput::to_dictionary() const {
	Dictionary dict;
	dict["command"] = command;
	return dict;
}

Ref<EditorAutomationMCPJsonSchema> EditorAutomationMCPListCommandsInput::schema() {
	Ref<EditorAutomationMCPJsonSchema> schema = EditorAutomationMCPJsonSchema::object(
			"Input for list_commands. Use this to discover command keys before calling run_command.");
	schema->add_property("query", EditorAutomationMCPJsonSchema::string("Optional substring filter against command keys and labels."));
	schema->add_property("category", EditorAutomationMCPJsonSchema::string("Optional category prefix filter."));
	schema->add_property("runnable_only", EditorAutomationMCPJsonSchema::boolean("When true, include only commands runnable by run_command."));
	schema->add_property("limit", EditorAutomationMCPJsonSchema::integer("Optional maximum number of commands to return."));
	return schema;
}

EditorAutomationMCPParseResult<EditorAutomationMCPListCommandsInput> EditorAutomationMCPListCommandsInput::parse(const Dictionary &p_dict) {
	EditorAutomationMCPListCommandsInput input;
	EditorAutomationMCPParseError error;
	if (!_read_optional_string(p_dict, "query", input.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPListCommandsInput>::invalid(error.field, error.message);
	}
	if (!_read_optional_string(p_dict, "category", input.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPListCommandsInput>::invalid(error.field, error.message);
	}
	if (!_read_optional_bool(p_dict, "runnable_only", input.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPListCommandsInput>::invalid(error.field, error.message);
	}
	if (!_read_optional_int(p_dict, "limit", input.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPListCommandsInput>::invalid(error.field, error.message);
	}
	return EditorAutomationMCPParseResult<EditorAutomationMCPListCommandsInput>::success(input);
}

Dictionary EditorAutomationMCPListCommandsInput::to_dictionary() const {
	return values;
}

Ref<EditorAutomationMCPJsonSchema> EditorAutomationMCPPollEventsInput::schema() {
	PackedStringArray kinds;
	kinds.push_back("editor_log_error");
	kinds.push_back("editor_log_warning");
	kinds.push_back("automation");

	Ref<EditorAutomationMCPJsonSchema> schema = EditorAutomationMCPJsonSchema::object(
			"Input for poll_events. Prefer this over read_editor_log when an MCP client needs notification-like updates over POST-only transport.");
	schema->add_property("since", EditorAutomationMCPEventMarker::schema());
	schema->add_property("kinds", EditorAutomationMCPJsonSchema::array(EditorAutomationMCPJsonSchema::enum_string(kinds, "Automation event kind."), "Optional event kind filter."));
	schema->add_property("limit", EditorAutomationMCPJsonSchema::integer("Maximum events to return (default 64)."));
	return schema;
}

EditorAutomationMCPParseResult<EditorAutomationMCPPollEventsInput> EditorAutomationMCPPollEventsInput::parse(const Dictionary &p_dict) {
	PackedStringArray kinds;
	kinds.push_back("editor_log_error");
	kinds.push_back("editor_log_warning");
	kinds.push_back("automation");

	EditorAutomationMCPPollEventsInput input;
	EditorAutomationMCPParseError error;
	if (p_dict.has("since")) {
		const EditorAutomationMCPParseResult<EditorAutomationMCPEventMarker> marker = EditorAutomationMCPEventMarker::parse(p_dict.get("since", Variant()), "since");
		if (!marker.ok) {
			return EditorAutomationMCPParseResult<EditorAutomationMCPPollEventsInput>::invalid(marker.error.field, marker.error.message);
		}
		input.values["since"] = marker.value.to_dictionary();
	}
	if (!_read_optional_string_array(p_dict, "kinds", input.values, error, kinds, true)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPPollEventsInput>::invalid(error.field, error.message);
	}
	if (!_read_optional_int(p_dict, "limit", input.values, error)) {
		return EditorAutomationMCPParseResult<EditorAutomationMCPPollEventsInput>::invalid(error.field, error.message);
	}
	return EditorAutomationMCPParseResult<EditorAutomationMCPPollEventsInput>::success(input);
}

Dictionary EditorAutomationMCPPollEventsInput::to_dictionary() const {
	return values;
}

PackedStringArray EditorAutomationMCPContracts::action_names() {
	PackedStringArray actions;
	actions.push_back("click");
	actions.push_back("focus");
	actions.push_back("type_text");
	actions.push_back("set_text");
	actions.push_back("submit");
	actions.push_back("press_key");
	actions.push_back("drag");
	actions.push_back("select");
	actions.push_back("activate");
	actions.push_back("expand");
	actions.push_back("collapse");
	actions.push_back("scroll");
	actions.push_back("choose_menu_item");
	actions.push_back("set_value");
	return actions;
}

PackedStringArray EditorAutomationMCPContracts::route_enum_values() {
	PackedStringArray values;
	values.push_back("auto");
	values.push_back("semantic");
	values.push_back("input");
	return values;
}

PackedStringArray EditorAutomationMCPContracts::severity_enum_values() {
	PackedStringArray values;
	values.push_back("error");
	values.push_back("warning");
	values.push_back("editor");
	values.push_back("stdout");
	values.push_back("stdout_rich");
	return values;
}

PackedStringArray EditorAutomationMCPContracts::scroll_direction_enum_values() {
	PackedStringArray values;
	values.push_back("up");
	values.push_back("down");
	values.push_back("left");
	values.push_back("right");
	return values;
}

PackedStringArray EditorAutomationMCPContracts::pointer_anchor_enum_values() {
	PackedStringArray values;
	values.push_back("top_left");
	values.push_back("top_right");
	values.push_back("bottom_left");
	values.push_back("bottom_right");
	values.push_back("center");
	return values;
}

PackedStringArray EditorAutomationMCPContracts::wait_condition_types() {
	PackedStringArray types;
	types.push_back("next_frame");
	types.push_back("editor_idle");
	types.push_back("selector_appears");
	types.push_back("selector_disappears");
	types.push_back("selector_matches");
	types.push_back("focus_matches");
	types.push_back("modal_stack_changed");
	types.push_back("modal_stack_settled");
	types.push_back("filesystem_idle");
	types.push_back("import_reload_idle");
	types.push_back("script_analysis_idle");
	types.push_back("log_contains");
	types.push_back("no_new_errors");
	return types;
}

PackedStringArray EditorAutomationMCPContracts::wait_status_enum_values() {
	PackedStringArray values;
	values.push_back("pending");
	values.push_back("complete");
	values.push_back("cancelled");
	return values;
}

Array EditorAutomationMCPContracts::build_tools_list() {
	Array tools;

	{
		Ref<EditorAutomationMCPJsonSchema> output = EditorAutomationMCPJsonSchema::object(
				"Output from observe_ui. Treat tree/windows as a semantic snapshot for selecting elements and use cursors for truncated children.");
		output->add_property("generation", EditorAutomationMCPJsonSchema::integer("Snapshot generation used to correlate ids and diagnostics."));
		output->add_property("focused_element_id", EditorAutomationMCPJsonSchema::string("Snapshot-scoped id of the focused element, when any."));
		output->add_property("tree", EditorAutomationMCPJsonSchema::array(_element_node_schema(), "Root element trees for the current editor windows."));
		output->add_property("windows", EditorAutomationMCPJsonSchema::array(_element_node_schema(), "Alias of tree for clients that model top-level windows explicitly."));
		output->add_property("modal_stack", EditorAutomationMCPJsonSchema::array(EditorAutomationMCPJsonSchema::object("Modal stack entry."), "Modal stack entries from bottom to top."));
		output->add_property("element_count", EditorAutomationMCPJsonSchema::integer("Total elements in snapshot."));
		output->add_property("limits", EditorAutomationMCPJsonSchema::object("Applied limits and truncation flags."));
		output->add_property("subtree", _element_node_schema());
		tools.push_back(_make_tool("observe_ui",
				"Returns the current windows, focused element, modal stack, and visible semantic tree.",
				EditorAutomationMCPObserveUIInput::schema(), output)
						.to_dictionary());
	}

	{
		Ref<EditorAutomationMCPJsonSchema> output = EditorAutomationMCPJsonSchema::object(
				"Output from find_elements. Use elements for selector refinement or follow-up actions; inspect details when ok is false.");
		output->add_property("ok", EditorAutomationMCPJsonSchema::boolean("True when at least one match was found."));
		output->add_property("elements", EditorAutomationMCPJsonSchema::array(_element_node_schema(), "Matched element summaries for this page."));
		output->add_property("match_count", EditorAutomationMCPJsonSchema::integer("Total match count before pagination."));
		output->add_property("truncated", EditorAutomationMCPJsonSchema::boolean("True when more matches remain after this page."));
		output->add_property("next_cursor", EditorAutomationMCPJsonSchema::string("Cursor for the next page of matches."));
		_add_failure_details_output_props(output);
		tools.push_back(_make_tool("find_elements",
				"Resolves selectors and returns matches or structured no-match/ambiguous diagnostics.",
				EditorAutomationMCPFindElementsInput::schema(), output)
						.to_dictionary());
	}

	{
		Ref<EditorAutomationMCPJsonSchema> act_output = _ok_result_schema();
		_add_failure_details_output_props(act_output);
		tools.push_back(_make_tool("act",
				"Performs a semantic or input action on a selected element and optionally waits for a UI condition in one call.",
				EditorAutomationMCPActInput::schema(), act_output)
						.to_dictionary());
	}

	{
		Ref<EditorAutomationMCPJsonSchema> output = _ok_result_schema();
		output->add_property("status", EditorAutomationMCPJsonSchema::enum_string(wait_status_enum_values(), "Cooperative wait status when the wait is pending, complete, or cancelled."));
		output->add_property("wait_id", EditorAutomationMCPJsonSchema::string("Cooperative wait id to pass back to wait_for for polling or cancellation."));
		_add_failure_details_output_props(output);
		tools.push_back(_make_tool("wait_for",
				"Waits cooperatively for a UI condition and returns success/failure diagnostics without blocking the editor for the full timeout.",
				EditorAutomationMCPWaitForInput::schema(), output)
						.to_dictionary());
	}

	tools.push_back(_make_tool("read_editor_state",
			"Returns selected nodes, open scenes, active scene, current script, playing state, and unsaved state.",
			EditorAutomationMCPReadEditorStateInput::schema(), EditorAutomationMCPJsonSchema::object("Output from read_editor_state with a lightweight snapshot of editor/session state."))
					.to_dictionary());

	{
		Ref<EditorAutomationMCPJsonSchema> output = EditorAutomationMCPJsonSchema::object(
				"Output from read_editor_log. Store marker and pass it as since on the next call to avoid duplicate entries.");
		output->add_property("entries", EditorAutomationMCPJsonSchema::array(EditorAutomationMCPJsonSchema::object("Editor log entry."), "Log entries."));
		output->add_property("count", EditorAutomationMCPJsonSchema::integer("Returned entry count."));
		output->add_property("marker", EditorAutomationMCPLogMarker::schema());
		tools.push_back(_make_tool("read_editor_log",
				"Returns editor log entries, optionally filtered by severity and since-marker. For push notifications, use poll_events; this tool remains the polling fallback.",
				EditorAutomationMCPReadEditorLogInput::schema(), output)
						.to_dictionary());
	}

	tools.push_back(_make_tool("run_command",
			"Executes a command palette command or editor shortcut action by key via the existing editor registries.",
			EditorAutomationMCPRunCommandInput::schema(), _ok_result_schema())
					.to_dictionary());

	{
		Ref<EditorAutomationMCPJsonSchema> output = EditorAutomationMCPJsonSchema::object(
				"Output from list_commands. Use runnable_by_run_command before passing a command key to run_command.");
		output->add_property("ok", EditorAutomationMCPJsonSchema::boolean("Whether listing succeeded."));
		output->add_property("commands", EditorAutomationMCPJsonSchema::array(EditorAutomationMCPJsonSchema::object("Editor command entry."), "Command entries."));
		tools.push_back(_make_tool("list_commands",
				"Lists command palette commands and editor shortcut actions with runnable metadata.",
				EditorAutomationMCPListCommandsInput::schema(), output)
						.to_dictionary());
	}

	{
		Ref<EditorAutomationMCPJsonSchema> output = EditorAutomationMCPJsonSchema::object(
				"Output from poll_events. Store marker and pass it as since on the next poll to avoid duplicate events.");
		output->add_property("events", EditorAutomationMCPJsonSchema::array(EditorAutomationMCPJsonSchema::object("Automation event entry."), "Automation events since the marker."));
		output->add_property("count", EditorAutomationMCPJsonSchema::integer("Returned event count."));
		output->add_property("marker", EditorAutomationMCPEventMarker::schema());
		output->add_property("has_more", EditorAutomationMCPJsonSchema::boolean("True when additional events remain after this page."));
		output->add_property("transport", EditorAutomationMCPJsonSchema::object("Transport capabilities for notifications."));
		tools.push_back(_make_tool("poll_events",
				"Returns queued editor log/automation events. The POST-only MCP transport cannot push notifications; poll this tool (or read_editor_log) instead.",
				EditorAutomationMCPPollEventsInput::schema(), output)
						.to_dictionary());
	}

	return tools;
}

Array EditorAutomationMCPContracts::build_resource_templates_list() {
	Array templates;
	templates.push_back(_make_resource_template("foundry://element/{id}", "Element",
			"Read a single semantic UI element summary by id or handle without requesting a full UI tree.")
					.to_dictionary());
	templates.push_back(_make_resource_template("foundry://ui/subtree/{id}", "UI Subtree",
			"Read a semantic UI subtree rooted at an element id or handle when observe_ui returned truncated children.")
					.to_dictionary());
	templates.push_back(_make_resource_template("foundry://ui/subtree/{id}/depth/{depth}", "UI Subtree (depth)",
			"Read a semantic UI subtree with an explicit max depth to inspect a focused region of the editor UI.")
					.to_dictionary());
	templates.push_back(_make_resource_template("foundry://scene/tree", "Scene Tree",
			"Read the edited scene node hierarchy. Query parameters may narrow future scene-tree projections.")
					.to_dictionary());
	return templates;
}

Array EditorAutomationMCPContracts::build_resources_list() {
	Array resources;
	resources.push_back(_make_resource("foundry://ui/tree", "UI Tree",
			"Read the current visible semantic UI tree. Prefer observe_ui when tool output metadata is also needed.")
					.to_dictionary());
	resources.push_back(_make_resource("foundry://editor/state", "Editor State",
			"Read the current editor/session state: scenes, selection, script, play state, and unsaved status.")
					.to_dictionary());
	resources.push_back(_make_resource("foundry://editor/log", "Editor Log",
			"Read recent editor log entries. Prefer poll_events for notification-like incremental updates.")
					.to_dictionary());
	resources.push_back(_make_resource("foundry://scene/active", "Active Scene",
			"Read active scene and edited root information for scene-aware automation.")
					.to_dictionary());
	resources.push_back(_make_resource("foundry://scene/tree", "Scene Tree",
			"Read the edited scene node hierarchy.")
					.to_dictionary());
	resources.push_back(_make_resource("foundry://commands", "Editor Commands",
			"Read command palette commands and editor shortcut actions with runnable metadata for run_command.")
					.to_dictionary());
	return resources;
}
