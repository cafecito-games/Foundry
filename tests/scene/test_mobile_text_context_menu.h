/**************************************************************************/
/*  test_mobile_text_context_menu.h                                       */
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

#include "core/object/message_queue.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/text_edit.h"
#include "scene/main/scene_tree.h"

#include "tests/test_macros.h"

namespace TestMobileTextContextMenu {

static void send_left_button(const Point2i &p_pos, bool p_pressed, bool p_emulated_touch) {
	Ref<InputEventMouseButton> event;
	event.instantiate();
	event->set_position(p_pos);
	event->set_global_position(p_pos);
	event->set_button_index(MouseButton::LEFT);
	event->set_button_mask(p_pressed ? MouseButtonMask::LEFT : MouseButtonMask::NONE);
	event->set_pressed(p_pressed);
	event->set_factor(1);
	if (p_emulated_touch) {
		event->set_device(InputEvent::DEVICE_ID_EMULATION);
	}
	_SEND_DISPLAYSERVER_EVENT(event);
	MessageQueue::get_singleton()->flush();
}

static void send_left_motion(const Point2i &p_pos, bool p_emulated_touch) {
	Ref<InputEventMouseMotion> event;
	event.instantiate();
	event->set_position(p_pos);
	event->set_global_position(p_pos);
	event->set_button_mask(MouseButtonMask::LEFT);
	if (p_emulated_touch) {
		event->set_device(InputEvent::DEVICE_ID_EMULATION);
	}
	_SEND_DISPLAYSERVER_EVENT(event);
	MessageQueue::get_singleton()->flush();
}

static void wait_for_long_press() {
	SceneTree::get_singleton()->process(0.6);
	MessageQueue::get_singleton()->flush();
}

TEST_CASE("[SceneTree][MobileTextContextMenu] touch long press opens LineEdit menu") {
	LineEdit *line_edit = memnew(LineEdit);
	SceneTree::get_singleton()->get_root()->add_child(line_edit);
	line_edit->set_size(Size2(400, 40));
	line_edit->set_text("mobile clipboard");
	MessageQueue::get_singleton()->flush();

	CHECK_FALSE(line_edit->is_menu_visible());
	send_left_button(Point2i(20, 20), true, true);
	wait_for_long_press();
	CHECK(line_edit->is_menu_visible());

	memdelete(line_edit);
}

TEST_CASE("[SceneTree][MobileTextContextMenu] desktop left hold does not open LineEdit menu") {
	LineEdit *line_edit = memnew(LineEdit);
	SceneTree::get_singleton()->get_root()->add_child(line_edit);
	line_edit->set_size(Size2(400, 40));
	line_edit->set_text("desktop clipboard");
	MessageQueue::get_singleton()->flush();

	send_left_button(Point2i(20, 20), true, false);
	wait_for_long_press();
	CHECK_FALSE(line_edit->is_menu_visible());

	memdelete(line_edit);
}

TEST_CASE("[SceneTree][MobileTextContextMenu] touch long press opens TextEdit menu") {
	TextEdit *text_edit = memnew(TextEdit);
	SceneTree::get_singleton()->get_root()->add_child(text_edit);
	text_edit->set_size(Size2(400, 120));
	text_edit->set_text("mobile clipboard");
	MessageQueue::get_singleton()->flush();

	CHECK_FALSE(text_edit->is_menu_visible());
	send_left_button(Point2i(20, 20), true, true);
	wait_for_long_press();
	CHECK(text_edit->is_menu_visible());

	memdelete(text_edit);
}

TEST_CASE("[SceneTree][MobileTextContextMenu] touch long press opens RichTextLabel menu") {
	RichTextLabel *rich_text_label = memnew(RichTextLabel);
	SceneTree::get_singleton()->get_root()->add_child(rich_text_label);
	rich_text_label->set_size(Size2(400, 120));
	rich_text_label->set_text("mobile clipboard");
	rich_text_label->set_context_menu_enabled(true);
	MessageQueue::get_singleton()->flush();

	CHECK_FALSE(rich_text_label->is_menu_visible());
	send_left_button(Point2i(20, 20), true, true);
	wait_for_long_press();
	CHECK(rich_text_label->is_menu_visible());

	memdelete(rich_text_label);
}

TEST_CASE("[SceneTree][MobileTextContextMenu] release cancels LineEdit long press") {
	LineEdit *line_edit = memnew(LineEdit);
	SceneTree::get_singleton()->get_root()->add_child(line_edit);
	line_edit->set_size(Size2(400, 40));
	line_edit->set_text("release cancels");
	MessageQueue::get_singleton()->flush();

	send_left_button(Point2i(20, 20), true, true);
	send_left_button(Point2i(20, 20), false, true);
	wait_for_long_press();
	CHECK_FALSE(line_edit->is_menu_visible());

	memdelete(line_edit);
}

TEST_CASE("[SceneTree][MobileTextContextMenu] meaningful drag cancels TextEdit long press") {
	TextEdit *text_edit = memnew(TextEdit);
	SceneTree::get_singleton()->get_root()->add_child(text_edit);
	text_edit->set_size(Size2(400, 120));
	text_edit->set_text("drag cancels");
	MessageQueue::get_singleton()->flush();

	send_left_button(Point2i(20, 20), true, true);
	send_left_motion(Point2i(40, 20), true);
	wait_for_long_press();
	CHECK_FALSE(text_edit->is_menu_visible());

	memdelete(text_edit);
}

TEST_CASE("[SceneTree][MobileTextContextMenu] selection survives LineEdit long press") {
	LineEdit *line_edit = memnew(LineEdit);
	SceneTree::get_singleton()->get_root()->add_child(line_edit);
	line_edit->set_size(Size2(400, 40));
	line_edit->set_text("selected clipboard");
	line_edit->select(0, 8);
	MessageQueue::get_singleton()->flush();

	send_left_button(Point2i(20, 20), true, true);
	wait_for_long_press();
	CHECK(line_edit->is_menu_visible());
	CHECK_EQ(line_edit->get_selected_text(), "selected");

	memdelete(line_edit);
}

} // namespace TestMobileTextContextMenu
