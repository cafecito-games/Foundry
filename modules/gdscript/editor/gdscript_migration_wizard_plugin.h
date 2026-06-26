/**************************************************************************/
/*  gdscript_migration_wizard_plugin.h                                    */
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

#ifdef TOOLS_ENABLED

#include "scene/gui/dialogs.h"

class CheckBox;
class Label;
class RichTextLabel;

// The editor entry point for the strict-typing migration wizard. It is the GUI counterpart to the
// `--gdscript-migrate` headless command: both drive the same GDScriptMigrationWizard orchestrator
// (dry-run report -> apply -> gated strict activation), so the editor and CI flows cannot diverge.
//
// The dialog always begins with the read-only dry-run report so the user sees the cost before any
// edit. The strict-mode and apply toggles, plus the explicit confirmation checkboxes, gate the
// destructive stages exactly as the orchestrator's options do; the dialog never writes anything the
// user did not opt into and confirm.
class GDScriptMigrationWizardDialog : public ConfirmationDialog {
	GDCLASS(GDScriptMigrationWizardDialog, ConfirmationDialog);

	RichTextLabel *report_output = nullptr;
	CheckBox *strict_null_checkbox = nullptr;
	CheckBox *strict_dynamic_checkbox = nullptr;
	CheckBox *apply_checkbox = nullptr;
	CheckBox *activate_strict_checkbox = nullptr;
	CheckBox *acknowledge_vcs_checkbox = nullptr;
	Label *status_label = nullptr;

	// Refreshes the read-only dry-run report for the current toggle state without writing anything.
	void _refresh_report();
	// Runs the migration wizard honoring the toggles, then re-shows the resulting summary.
	void _run_migration();
	void _option_toggled(bool p_pressed);

protected:
	void _notification(int p_what);

public:
	// Opens the dialog and computes the initial dry-run report for the current project.
	void popup_wizard();

	GDScriptMigrationWizardDialog();
};

#endif // TOOLS_ENABLED
