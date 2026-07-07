/**************************************************************************/
/*  editor_automation_screenshot.cpp                                      */
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

#include "editor_automation_screenshot.h"

#include "editor/automation/editor_automation_selector.h"

#include "core/crypto/crypto_core.h"
#include "core/io/image.h"
#include "editor/editor_main_screen.h"
#include "editor/editor_node.h"
#include "scene/gui/box_container.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"

Dictionary EditorAutomationScreenshotAttachment::to_dictionary() const {
	Dictionary dict;
	dict["status"] = status;
	if (!reason.is_empty()) {
		dict["reason"] = reason;
	}
	if (!format.is_empty()) {
		dict["format"] = format;
	}
	if (!encoding.is_empty()) {
		dict["encoding"] = encoding;
	}
	if (!data.is_empty()) {
		dict["data"] = data;
	}
	if (!capture_mode.is_empty()) {
		dict["capture_mode"] = capture_mode;
	}
	if (!viewport.is_empty()) {
		dict["viewport"] = viewport;
	}
	if (!image.is_empty()) {
		dict["image"] = image;
	}
	if (!crop.is_empty()) {
		dict["crop"] = crop;
	}
	if (!highlight.is_empty()) {
		dict["highlight"] = highlight;
	}
	if (byte_size > 0) {
		dict["byte_size"] = byte_size;
	}
	if (encoded_byte_size > 0) {
		dict["encoded_byte_size"] = encoded_byte_size;
	}
	if (max_bytes > 0) {
		dict["max_bytes"] = max_bytes;
	}
	return dict;
}

Dictionary EditorAutomationScreenshot::_rect_to_dictionary(const Rect2i &p_rect) {
	Dictionary dict;
	dict["x"] = p_rect.position.x;
	dict["y"] = p_rect.position.y;
	dict["width"] = p_rect.size.x;
	dict["height"] = p_rect.size.y;
	return dict;
}

Viewport *EditorAutomationScreenshot::_resolve_capture_viewport(Node *p_snapshot_root) {
	if (p_snapshot_root != nullptr && p_snapshot_root->is_inside_tree()) {
		Viewport *viewport = p_snapshot_root->get_viewport();
		if (viewport != nullptr) {
			return viewport;
		}
	}

	EditorNode *editor_node = EditorNode::get_singleton();
	if (editor_node != nullptr && editor_node->is_editor_ready()) {
		EditorMainScreen *main_screen = editor_node->get_editor_main_screen();
		if (main_screen != nullptr) {
			VBoxContainer *control = main_screen->get_control();
			if (control != nullptr) {
				Viewport *viewport = control->get_viewport();
				if (viewport != nullptr) {
					return viewport;
				}
			}
		}
		Control *gui_base = editor_node->get_gui_base();
		if (gui_base != nullptr) {
			Viewport *viewport = gui_base->get_viewport();
			if (viewport != nullptr) {
				return viewport;
			}
		}
	}

	SceneTree *tree = SceneTree::get_singleton();
	if (tree != nullptr) {
		Window *root = tree->get_root();
		if (root != nullptr) {
			return root->get_viewport();
		}
	}
	return nullptr;
}

Ref<Image> EditorAutomationScreenshot::_acquire_viewport_image(Node *p_snapshot_root, Dictionary &r_viewport_meta) {
	Viewport *viewport = _resolve_capture_viewport(p_snapshot_root);
	if (viewport == nullptr) {
		return Ref<Image>();
	}

	Ref<ViewportTexture> texture = viewport->get_texture();
	if (texture.is_null()) {
		return Ref<Image>();
	}

	Ref<Image> image = texture->get_image();
	if (image.is_null()) {
		return Ref<Image>();
	}

	const Vector2i viewport_size = viewport->get_visible_rect().size;
	r_viewport_meta["width"] = viewport_size.x;
	r_viewport_meta["height"] = viewport_size.y;
	Window *window = viewport->get_window();
	if (window != nullptr) {
		r_viewport_meta["window_title"] = window->get_title();
		r_viewport_meta["window_id"] = (int64_t)window->get_instance_id();
	}
	return image;
}

Rect2i EditorAutomationScreenshot::_resolve_highlight_rect(const EditorAutomationSnapshot &p_snapshot, const Dictionary &p_selector) {
	if (!p_selector.is_empty()) {
		const EditorAutomationSelectorResult selector_result = EditorAutomationSelector::resolve(p_snapshot, p_selector);
		if (!selector_result.match_indices.is_empty()) {
			Rect2i bounds = p_snapshot.get_element(selector_result.match_indices[0]).bounds;
			for (int i = 1; i < selector_result.match_indices.size(); i++) {
				bounds = bounds.merge(p_snapshot.get_element(selector_result.match_indices[i]).bounds);
			}
			if (bounds.size.x > 0 && bounds.size.y > 0) {
				return bounds;
			}
		}
	}

	const String &focused_id = p_snapshot.get_focused_element_id();
	if (!focused_id.is_empty()) {
		const EditorAutomationElement *focused = p_snapshot.find_by_id(focused_id);
		if (focused != nullptr && focused->bounds.size.x > 0 && focused->bounds.size.y > 0) {
			return focused->bounds;
		}
	}
	return Rect2i();
}

EditorAutomationScreenshotAttachment EditorAutomationScreenshot::encode_image_attachment(
		const Ref<Image> &p_image,
		const EditorAutomationScreenshotOptions &p_options,
		const String &p_capture_mode,
		const Dictionary &p_viewport,
		const Dictionary &p_crop,
		const Dictionary &p_highlight) {
	EditorAutomationScreenshotAttachment attachment;
	attachment.max_bytes = p_options.max_bytes > 0 ? p_options.max_bytes : DEFAULT_MAX_BYTES;
	attachment.capture_mode = p_capture_mode;
	attachment.viewport = p_viewport;
	attachment.crop = p_crop;
	attachment.highlight = p_highlight;

	if (p_image.is_null()) {
		attachment.status = "unavailable";
		attachment.reason = "screenshot_unavailable";
		return attachment;
	}

	Ref<Image> image = p_image;
	if (image->get_format() != Image::FORMAT_RGBA8) {
		image = image->duplicate();
		image->convert(Image::FORMAT_RGBA8);
	}

	Dictionary image_meta;
	image_meta["width"] = image->get_width();
	image_meta["height"] = image->get_height();
	attachment.image = image_meta;

	if (p_options.format != "png") {
		attachment.status = "unavailable";
		attachment.reason = vformat("Unsupported screenshot format '%s'.", p_options.format);
		return attachment;
	}

	const Vector<uint8_t> png_bytes = image->save_png_to_buffer();
	if (png_bytes.is_empty()) {
		attachment.status = "unavailable";
		attachment.reason = "screenshot_unavailable";
		return attachment;
	}

	attachment.format = "png";
	attachment.byte_size = png_bytes.size();
	const String encoded = CryptoCore::b64_encode_str(png_bytes.ptr(), png_bytes.size());
	ERR_FAIL_COND_V(encoded.is_empty(), attachment);
	attachment.encoded_byte_size = encoded.length();

	if (attachment.byte_size > attachment.max_bytes) {
		attachment.status = "truncated";
		attachment.reason = vformat("Screenshot exceeds max_bytes (%d > %d).", attachment.byte_size, attachment.max_bytes);
		return attachment;
	}

	attachment.status = "available";
	attachment.encoding = "base64";
	attachment.data = encoded;
	return attachment;
}

EditorAutomationScreenshotAttachment EditorAutomationScreenshot::capture_for_failure(
		const EditorAutomationSnapshot &p_snapshot,
		const Dictionary &p_selector,
		const EditorAutomationScreenshotOptions &p_options) {
	EditorAutomationScreenshotAttachment attachment;
	if (!p_options.enabled) {
		return attachment;
	}

	Dictionary viewport_meta;
	Ref<Image> image = _acquire_viewport_image(p_options.snapshot_root, viewport_meta);
	if (image.is_null()) {
		attachment.status = "unavailable";
		attachment.reason = "screenshot_unavailable";
		return attachment;
	}

	const Rect2i highlight = _resolve_highlight_rect(p_snapshot, p_selector);
	Dictionary highlight_dict;
	if (highlight.size.x > 0 && highlight.size.y > 0) {
		highlight_dict = _rect_to_dictionary(highlight);
	}

	String capture_mode = "full_window";
	Dictionary crop_dict;
	Ref<Image> output_image = image;

	if (p_options.crop_to_target && highlight.size.x > 0 && highlight.size.y > 0) {
		Rect2i crop = highlight;
		if (p_options.crop_padding_px > 0) {
			crop = crop.grow(p_options.crop_padding_px);
		}
		const Rect2i image_bounds = Rect2i(Vector2i(), image->get_size());
		crop = crop.intersection(image_bounds);
		if (crop.size.x > 0 && crop.size.y > 0) {
			output_image = image->get_region(crop);
			crop_dict = _rect_to_dictionary(crop);
			capture_mode = "cropped";
		}
	}

	return encode_image_attachment(output_image, p_options, capture_mode, viewport_meta, crop_dict, highlight_dict);
}

EditorAutomationScreenshotAttachment EditorAutomationScreenshot::capture_on_demand(
		const EditorAutomationScreenshotOptions &p_options,
		bool p_crop_to_element,
		const Rect2i &p_element_bounds) {
	EditorAutomationScreenshotAttachment attachment;

	Dictionary viewport_meta;
	Ref<Image> image = _acquire_viewport_image(p_options.snapshot_root, viewport_meta);
	if (image.is_null()) {
		attachment.status = "unavailable";
		attachment.reason = "screenshot_unavailable";
		return attachment;
	}

	String capture_mode = "full_window";
	Dictionary crop_dict;
	Dictionary highlight_dict;
	Ref<Image> output_image = image;

	if (p_crop_to_element && p_element_bounds.size.x > 0 && p_element_bounds.size.y > 0) {
		highlight_dict = _rect_to_dictionary(p_element_bounds);
		Rect2i crop = p_element_bounds;
		if (p_options.crop_padding_px > 0) {
			crop = crop.grow(p_options.crop_padding_px);
		}
		const Rect2i image_bounds = Rect2i(Vector2i(), image->get_size());
		crop = crop.intersection(image_bounds);
		if (crop.size.x > 0 && crop.size.y > 0) {
			output_image = image->get_region(crop);
			crop_dict = _rect_to_dictionary(crop);
			capture_mode = "cropped";
		}
	}

	return encode_image_attachment(output_image, p_options, capture_mode, viewport_meta, crop_dict, highlight_dict);
}
