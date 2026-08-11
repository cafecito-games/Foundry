/**************************************************************************/
/*  test_startup_sequence_macos.mm                                        */
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

#import "tests/platform/test_startup_sequence_macos.h"

#import <AppKit/AppKit.h>

// The doctest cases live in the companion header so they are registered from the same translation
// unit as `test_main.cpp`. This file supplies the Objective-C half: real `NSEvent` instances, so
// the gate is exercised against the event types AppKit actually produces rather than transcribed
// constants.

namespace TestStartupSequenceMacOS {

unsigned long key_down_event_type() {
	NSEvent *event = [NSEvent keyEventWithType:NSEventTypeKeyDown
									  location:NSZeroPoint
								 modifierFlags:0
									 timestamp:0
								  windowNumber:0
									   context:nil
									characters:@"a"
				   charactersIgnoringModifiers:@"a"
									 isARepeat:NO
									   keyCode:0];
	return (unsigned long)[event type];
}

unsigned long key_up_event_type() {
	NSEvent *event = [NSEvent keyEventWithType:NSEventTypeKeyUp
									  location:NSZeroPoint
								 modifierFlags:0
									 timestamp:0
								  windowNumber:0
									   context:nil
									characters:@"a"
				   charactersIgnoringModifiers:@"a"
									 isARepeat:NO
									   keyCode:0];
	return (unsigned long)[event type];
}

unsigned long left_mouse_down_event_type() {
	NSEvent *event = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
										location:NSZeroPoint
								   modifierFlags:0
									   timestamp:0
									windowNumber:0
										 context:nil
									 eventNumber:0
									  clickCount:1
										pressure:1.0];
	return (unsigned long)[event type];
}

unsigned long scroll_wheel_event_type() {
	return (unsigned long)NSEventTypeScrollWheel;
}

unsigned long flags_changed_event_type() {
	return (unsigned long)NSEventTypeFlagsChanged;
}

unsigned long magnify_event_type() {
	return (unsigned long)NSEventTypeMagnify;
}

unsigned long tablet_point_event_type() {
	return (unsigned long)NSEventTypeTabletPoint;
}

unsigned long application_defined_event_type() {
	NSEvent *event = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
										location:NSZeroPoint
								   modifierFlags:0
									   timestamp:0
									windowNumber:0
										 context:nil
										 subtype:0
										   data1:0
										   data2:0];
	return (unsigned long)[event type];
}

unsigned long appkit_defined_event_type() {
	return (unsigned long)NSEventTypeAppKitDefined;
}

unsigned long system_defined_event_type() {
	return (unsigned long)NSEventTypeSystemDefined;
}

unsigned long periodic_event_type() {
	return (unsigned long)NSEventTypePeriodic;
}

unsigned long cursor_update_event_type() {
	return (unsigned long)NSEventTypeCursorUpdate;
}

} // namespace TestStartupSequenceMacOS
