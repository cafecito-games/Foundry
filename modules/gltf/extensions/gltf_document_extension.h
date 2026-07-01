/**************************************************************************/
/*  gltf_document_extension.h                                             */
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

#include "../gltf_state.h"

#include "scene/3d/node_3d.h"

class GLTFDocumentExtension : public Resource {
	FOUNDRY_CLASS(GLTFDocumentExtension, Resource);

protected:
	static void _bind_methods();

public:
	// Import process.
	virtual Error import_preflight(Ref<GLTFState> p_state, const Vector<String> &p_extensions);
	virtual Vector<String> get_supported_extensions();
	virtual Error parse_node_extensions(Ref<GLTFState> p_state, Ref<GLTFNode> p_gltf_node, const Dictionary &p_extensions);
	virtual Error parse_image_data(Ref<GLTFState> p_state, const PackedByteArray &p_image_data, const String &p_mime_type, Ref<Image> r_image);
	virtual String get_image_file_extension();
	virtual Error parse_texture_json(Ref<GLTFState> p_state, const Dictionary &p_texture_json, Ref<GLTFTexture> r_gltf_texture);
	virtual Ref<GLTFObjectModelProperty> import_object_model_property(Ref<GLTFState> p_state, const PackedStringArray &p_split_json_pointer, const TypedArray<NodePath> &p_partial_paths);
	virtual Error import_post_parse(Ref<GLTFState> p_state);
	virtual Error import_pre_generate(Ref<GLTFState> p_state);
	virtual Node3D *generate_scene_node(Ref<GLTFState> p_state, Ref<GLTFNode> p_gltf_node, Node *p_scene_parent);
	virtual Error import_node(Ref<GLTFState> p_state, Ref<GLTFNode> p_gltf_node, Dictionary &r_json, Node *p_node);
	virtual Error import_post(Ref<GLTFState> p_state, Node *p_node);
	// Export process.
	virtual Error export_preflight(Ref<GLTFState> p_state, Node *p_root);
	virtual void convert_scene_node(Ref<GLTFState> p_state, Ref<GLTFNode> p_gltf_node, Node *p_scene_node);
	virtual Error export_post_convert(Ref<GLTFState> p_state, Node *p_root);
	virtual Error export_preserialize(Ref<GLTFState> p_state);
	virtual Ref<GLTFObjectModelProperty> export_object_model_property(Ref<GLTFState> p_state, const NodePath &p_node_path, const Node *p_foundry_node, GLTFNodeIndex p_gltf_node_index, const Object *p_target_object, int p_target_depth);
	virtual Vector<String> get_saveable_image_formats();
	virtual PackedByteArray serialize_image_to_bytes(Ref<GLTFState> p_state, Ref<Image> p_image, Dictionary &r_image_dict, const String &p_image_format, float p_lossy_quality);
	virtual Error save_image_at_path(Ref<GLTFState> p_state, Ref<Image> p_image, const String &p_file_path, const String &p_image_format, float p_lossy_quality);
	virtual Error serialize_texture_json(Ref<GLTFState> p_state, Dictionary &r_texture_json, Ref<GLTFTexture> p_gltf_texture, const String &p_image_format);
	virtual Error export_node(Ref<GLTFState> p_state, Ref<GLTFNode> p_gltf_node, Dictionary &r_json, Node *p_node);
	virtual Error export_post(Ref<GLTFState> p_state);

	// Import process.
	FOUNDRY_VIRTUAL2R(Error, _import_preflight, Ref<GLTFState>, Vector<String>);
	FOUNDRY_VIRTUAL0R(Vector<String>, _get_supported_extensions);
	FOUNDRY_VIRTUAL3R(Error, _parse_node_extensions, Ref<GLTFState>, Ref<GLTFNode>, Dictionary);
	FOUNDRY_VIRTUAL4R(Error, _parse_image_data, Ref<GLTFState>, PackedByteArray, String, Ref<Image>);
	FOUNDRY_VIRTUAL0R(String, _get_image_file_extension);
	FOUNDRY_VIRTUAL3R(Error, _parse_texture_json, Ref<GLTFState>, Dictionary, Ref<GLTFTexture>);
	FOUNDRY_VIRTUAL3R(Ref<GLTFObjectModelProperty>, _import_object_model_property, Ref<GLTFState>, PackedStringArray, TypedArray<NodePath>);
	FOUNDRY_VIRTUAL1R(Error, _import_post_parse, Ref<GLTFState>);
	FOUNDRY_VIRTUAL1R(Error, _import_pre_generate, Ref<GLTFState>);
	FOUNDRY_VIRTUAL3R(Node3D *, _generate_scene_node, Ref<GLTFState>, Ref<GLTFNode>, Node *);
	FOUNDRY_VIRTUAL4R(Error, _import_node, Ref<GLTFState>, Ref<GLTFNode>, Dictionary, Node *);
	FOUNDRY_VIRTUAL2R(Error, _import_post, Ref<GLTFState>, Node *);
	// Export process.
	FOUNDRY_VIRTUAL2R(Error, _export_preflight, Ref<GLTFState>, Node *);
	FOUNDRY_VIRTUAL3(_convert_scene_node, Ref<GLTFState>, Ref<GLTFNode>, Node *);
	FOUNDRY_VIRTUAL2R(Error, _export_post_convert, Ref<GLTFState>, Node *);
	FOUNDRY_VIRTUAL1R(Error, _export_preserialize, Ref<GLTFState>);
	FOUNDRY_VIRTUAL6R(Ref<GLTFObjectModelProperty>, _export_object_model_property, Ref<GLTFState>, NodePath, const Node *, GLTFNodeIndex, const Object *, int);
	FOUNDRY_VIRTUAL0R(Vector<String>, _get_saveable_image_formats);
	FOUNDRY_VIRTUAL5R(PackedByteArray, _serialize_image_to_bytes, Ref<GLTFState>, Ref<Image>, Dictionary, String, float);
	FOUNDRY_VIRTUAL5R(Error, _save_image_at_path, Ref<GLTFState>, Ref<Image>, String, String, float);
	FOUNDRY_VIRTUAL4R(Error, _serialize_texture_json, Ref<GLTFState>, Dictionary, Ref<GLTFTexture>, String);
	FOUNDRY_VIRTUAL4R(Error, _export_node, Ref<GLTFState>, Ref<GLTFNode>, Dictionary, Node *);
	FOUNDRY_VIRTUAL1R(Error, _export_post, Ref<GLTFState>);
};
