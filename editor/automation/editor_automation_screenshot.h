/**************************************************************************/
/*  editor_automation_screenshot.h                                        */
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

#pragma once

#include "editor/automation/editor_automation_snapshot.h"

#include "core/io/image.h"
#include "core/variant/variant.h"

class Node;
class Viewport;

struct EditorAutomationScreenshotOptions {
	bool enabled = false;
	String format = "png";
	int max_bytes = 512 * 1024;
	bool crop_to_target = true;
	int crop_padding_px = 16;
	Node *snapshot_root = nullptr;
};

struct EditorAutomationScreenshotAttachment {
	String status; // "available", "unavailable", "truncated"
	String reason;
	String format;
	String encoding;
	String data;
	String capture_mode; // "full_window", "cropped"
	Dictionary viewport;
	Dictionary image;
	Dictionary crop;
	Dictionary highlight;
	int byte_size = 0;
	int encoded_byte_size = 0;
	int max_bytes = 0;

	Dictionary to_dictionary() const;
};

class EditorAutomationScreenshot {
public:
	static constexpr int DEFAULT_MAX_BYTES = 512 * 1024;

	static EditorAutomationScreenshotAttachment capture_for_failure(
			const EditorAutomationSnapshot &p_snapshot,
			const Dictionary &p_selector,
			const EditorAutomationScreenshotOptions &p_options);

	// On-demand capture of the current editor viewport. When p_crop_to_element is
	// true the image is cropped to p_element_bounds (grown by crop_padding_px and
	// clamped to the viewport), yielding capture_mode "cropped"; otherwise the
	// whole window is captured as "full_window". Shares the same viewport
	// acquisition and encode path as capture_for_failure.
	static EditorAutomationScreenshotAttachment capture_on_demand(
			const EditorAutomationScreenshotOptions &p_options,
			bool p_crop_to_element,
			const Rect2i &p_element_bounds);

	static EditorAutomationScreenshotAttachment encode_image_attachment(
			const Ref<Image> &p_image,
			const EditorAutomationScreenshotOptions &p_options,
			const String &p_capture_mode,
			const Dictionary &p_viewport,
			const Dictionary &p_crop,
			const Dictionary &p_highlight);

private:
	static Viewport *_resolve_capture_viewport(Node *p_snapshot_root);
	static Ref<Image> _acquire_viewport_image(Node *p_snapshot_root, Dictionary &r_viewport_meta);
	static Rect2i _resolve_highlight_rect(const EditorAutomationSnapshot &p_snapshot, const Dictionary &p_selector);
	static Dictionary _rect_to_dictionary(const Rect2i &p_rect);
};
