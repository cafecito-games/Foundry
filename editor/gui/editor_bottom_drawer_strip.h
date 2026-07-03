/**************************************************************************/
/*  editor_bottom_drawer_strip.h                                          */
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

#include "scene/gui/panel_container.h"

class Button;
class EditorBottomPanel;
class EditorDock;
class HBoxContainer;

// Slim full-window status strip shown along the bottom of the editor. It mirrors
// the bottom drawer's TabContainer as a row of toggle buttons (the TabContainer's
// own tab bar is hidden), hosts the close button and the toaster/version/pin/
// expand controls, and forwards right-clicks to the dock context popup. It never
// owns drawer state: it is a pure mirror driven by the TabContainer's signals.
class EditorBottomDrawerStrip : public PanelContainer {
	FOUNDRY_CLASS(EditorBottomDrawerStrip, PanelContainer);

	EditorBottomPanel *bottom_panel = nullptr;
	HBoxContainer *main_hbox = nullptr;
	HBoxContainer *toggles_hbox = nullptr;
	Button *close_button = nullptr;

	LocalVector<Button *> toggle_buttons;
	LocalVector<int> toggle_tab_indices;
	LocalVector<EditorDock *> toggle_docks;

	void _rebuild_toggles();
	void _refresh_toggle(int p_toggle_index);
	void _dock_style_changed(EditorDock *p_dock);
	void _toggle_pressed(int p_tab_index);
	void _toggle_gui_input(const Ref<InputEvent> &p_event, int p_tab_index);
	void _close_pressed();
	void _update_active_states();

protected:
	void _notification(int p_what);

public:
	// Hosts the distraction-free button while the drawer is expanded, keeping the
	// expand button as the strip's last control.
	void host_distraction_free_button(Button *p_button);

	EditorBottomDrawerStrip(EditorBottomPanel *p_bottom_panel);
};
