/**************************************************************************/
/*  gdscript_migration_wizard_plugin.cpp                                  */
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

#ifdef TOOLS_ENABLED

#include "gdscript_migration_wizard_plugin.h"

#include "gdscript_migration_wizard.h"

#include "editor/editor_string_names.h"
#include "editor/script/script_editor_plugin.h"
#include "editor/themes/editor_scale.h"

#include "scene/gui/box_container.h"
#include "scene/gui/check_box.h"
#include "scene/gui/label.h"
#include "scene/gui/rich_text_label.h"

// The project root the wizard operates on. The migration stages accept any `res://` path and the
// editor always works on the open project, so the whole project tree is the natural scope.
static const char *MIGRATION_PROJECT_ROOT = "res://";

void GDScriptMigrationWizardDialog::_refresh_report() {
	MigrationWizardOptions options;
	options.strict_null_checks = strict_null_checkbox->is_pressed();
	options.strict_dynamic_checks = strict_dynamic_checkbox->is_pressed();
	// A refresh is read-only: it never applies edits or flips settings, regardless of the toggles.
	options.apply = false;
	options.activate_strict = false;

	const MigrationWizardResult result = GDScriptMigrationWizard::run(MIGRATION_PROJECT_ROOT, options);
	report_output->set_text(result.summary());
	if (result.ok) {
		status_label->set_text(TTR("Dry-run report ready. Choose options below, then confirm to run."));
	} else {
		status_label->set_text(vformat(TTR("Report error: %s"), result.error_message));
	}
}

void GDScriptMigrationWizardDialog::_option_toggled(bool p_pressed) {
	// Strict checkboxes change the projected report, so refresh it; the apply/activate/ack toggles
	// only affect the confirmed run and need no re-projection.
	_refresh_report();
}

void GDScriptMigrationWizardDialog::_run_migration() {
	const bool apply_requested = apply_checkbox->is_pressed();

	// The migration writes scripts directly on disk and is documented as unsafe to run alongside a
	// live editing session: unsaved buffers in the script editor are invisible to it (so its edits
	// could be lost when the user later saves) and would be clobbered on disk by the apply. So an
	// apply is gated on a clean editor -- the user must save or discard open changes first. A
	// preview writes nothing, so it needs no gate.
	ScriptEditor *script_editor = ScriptEditor::get_singleton();
	if (apply_requested && script_editor) {
		const PackedStringArray unsaved = script_editor->get_unsaved_scripts();
		if (!unsaved.is_empty()) {
			status_label->set_text(vformat(TTR("Cannot apply: %d script(s) have unsaved changes in the editor. Save or discard them, then run again."), unsaved.size()));
			return;
		}
	}

	MigrationWizardOptions options;
	options.strict_null_checks = strict_null_checkbox->is_pressed();
	options.strict_dynamic_checks = strict_dynamic_checkbox->is_pressed();
	options.apply = apply_requested;
	options.acknowledge_vcs_warning = acknowledge_vcs_checkbox->is_pressed();
	// Activating from the editor is a single confirmed action: the user ticked the box and pressed
	// the dialog's confirm button, which is the explicit confirmation the activation gate requires.
	options.activate_strict = activate_strict_checkbox->is_pressed();
	options.confirm_strict_activation = activate_strict_checkbox->is_pressed();

	const MigrationWizardResult result = GDScriptMigrationWizard::run(MIGRATION_PROJECT_ROOT, options);
	report_output->set_text(result.summary());

	// Reload open script buffers from disk so they reflect the migration's on-disk result; a stale
	// buffer left open from before the apply would otherwise overwrite the migrated file on its next
	// save.
	if (result.applied && script_editor) {
		script_editor->reload_scripts(false);
	}

	if (!result.ok) {
		status_label->set_text(vformat(TTR("Migration error: %s"), result.error_message));
	} else if (result.applied) {
		status_label->set_text(TTR("Migration applied. Review the changes in your version control before committing."));
	} else if (result.blocked_by_vcs_guard) {
		status_label->set_text(TTR("Apply blocked by the version-control guard. Acknowledge the warning to proceed."));
	} else {
		status_label->set_text(TTR("Dry-run complete. No files were changed."));
	}

	// Keep the dialog open so the user can read the outcome; the report now reflects the run.
}

void GDScriptMigrationWizardDialog::popup_wizard() {
	_refresh_report();
	popup_centered_clamped(Size2(720, 640) * EDSCALE, 0.8);
}

void GDScriptMigrationWizardDialog::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_THEME_CHANGED: {
			report_output->add_theme_font_override(SNAME("normal_font"), get_theme_font(SNAME("output_source"), EditorStringName(EditorFonts)));
		} break;
	}
}

GDScriptMigrationWizardDialog::GDScriptMigrationWizardDialog() {
	set_title(TTR("Migrate to Strict Typing"));
	set_ok_button_text(TTR("Run"));
	// Keep the dialog open after Run so the user can read the outcome the run wrote back into the
	// report; closing is an explicit Cancel/close.
	set_hide_on_ok(false);

	VBoxContainer *layout = memnew(VBoxContainer);
	add_child(layout);

	Label *intro = memnew(Label);
	intro->set_text(TTR("This wizard infers and adds explicit type annotations across the project, then optionally enables strict-mode project settings. The report below is a read-only preview; nothing is written until you tick the options and press Run."));
	intro->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	layout->add_child(intro);

	report_output = memnew(RichTextLabel);
	report_output->set_selection_enabled(true);
	report_output->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	report_output->set_custom_minimum_size(Size2(0, 280) * EDSCALE);
	layout->add_child(report_output);

	strict_null_checkbox = memnew(CheckBox);
	strict_null_checkbox->set_text(TTR("Project strict null checks"));
	strict_null_checkbox->connect(SceneStringName(toggled), callable_mp(this, &GDScriptMigrationWizardDialog::_option_toggled));
	layout->add_child(strict_null_checkbox);

	strict_dynamic_checkbox = memnew(CheckBox);
	strict_dynamic_checkbox->set_text(TTR("Project strict dynamic checks"));
	strict_dynamic_checkbox->connect(SceneStringName(toggled), callable_mp(this, &GDScriptMigrationWizardDialog::_option_toggled));
	layout->add_child(strict_dynamic_checkbox);

	apply_checkbox = memnew(CheckBox);
	apply_checkbox->set_text(TTR("Apply inferred annotations to disk (writes files)"));
	layout->add_child(apply_checkbox);

	activate_strict_checkbox = memnew(CheckBox);
	activate_strict_checkbox->set_text(TTR("Enable the selected strict settings (only when the report is clean)"));
	layout->add_child(activate_strict_checkbox);

	acknowledge_vcs_checkbox = memnew(CheckBox);
	acknowledge_vcs_checkbox->set_text(TTR("Acknowledge the version-control warning and apply anyway"));
	layout->add_child(acknowledge_vcs_checkbox);

	status_label = memnew(Label);
	status_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	layout->add_child(status_label);

	connect(SceneStringName(confirmed), callable_mp(this, &GDScriptMigrationWizardDialog::_run_migration));
}

#endif // TOOLS_ENABLED
