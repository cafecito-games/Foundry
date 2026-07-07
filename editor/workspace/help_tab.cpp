/**************************************************************************/
/*  help_tab.cpp                                                          */
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

#include "help_tab.h"

#include "core/object/object.h"
#include "core/templates/vector.h"
#include "editor/doc/doc_tools.h"
#include "editor/doc/editor_help.h"
#include "editor/script/script_editor_controller.h"
#include "scene/gui/control.h"

HelpTabType::HelpTabType(const StringName &p_type_id) :
		type_id_value(p_type_id) {
}

String HelpTabType::class_key_for_topic(const String &p_topic) {
	if (p_topic.is_empty()) {
		return String();
	}
	// Split on ':' while keeping '::' intact -- built-in script class names use it
	// (e.g. Outer::Inner) -- mirroring EditorHelp::_help_callback so the dedup key
	// matches the page the viewer actually opens. A deep topic
	// ("class_method:Node2D:queue_free") names its class in the second field; a
	// bare class name is a single field and is its own key.
	Vector<String> parts;
	int from = 0;
	int buffer_start = 0;
	while (true) {
		const int pos = p_topic.find_char(':', from);
		if (pos < 0) {
			parts.push_back(p_topic.substr(buffer_start));
			break;
		}
		if (pos + 1 < p_topic.length() && p_topic[pos + 1] == ':') {
			from = pos + 2;
		} else {
			parts.push_back(p_topic.substr(buffer_start, pos - buffer_start));
			from = pos + 1;
			buffer_start = from;
		}
	}
	return parts.size() > 1 ? parts[1] : parts[0];
}

String HelpTabType::derive_title(const String &p_class_key) {
	if (p_class_key.is_empty()) {
		return String("Help");
	}
	return vformat("Help: %s", p_class_key);
}

EditorHelp *HelpTabType::_resolve_surface(int p_stable_id) const {
	const ObjectID *id = mounted_surfaces.getptr(p_stable_id);
	if (!id) {
		return nullptr;
	}
	return Object::cast_to<EditorHelp>(ObjectDB::get_instance(*id));
}

EditorHelp *HelpTabType::_create_surface(Control *p_chrome_host) {
	EditorHelp *help = memnew(EditorHelp);
	help->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	help->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	help->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	p_chrome_host->add_child(help);

	// A class link clicked inside the page routes back through the controller,
	// which reveals or opens the target class as its own workspace help tab.
	if (ScriptEditorController *controller = ScriptEditorController::get_singleton()) {
		help->connect("go_to_help", callable_mp(controller, &ScriptEditorController::goto_help));
	}
	return help;
}

void HelpTabType::_render_class(EditorHelp *p_help, const WorkspaceTab &p_tab) const {
	if (!p_help) {
		return;
	}
	const Dictionary &payload = p_tab.get_payload();
	const String help_class = payload.has("help_class") ? String(payload["help_class"]) : p_tab.get_resource_key();
	// go_to_class dereferences the doc database, so guard against it being null
	// (early startup / headless). It waits for the doc worker thread internally, so
	// a class still being generated renders once the thread finishes; a class that
	// never resolves leaves edited_class empty, which _capture_payload must not
	// persist and activate() retries.
	if (!help_class.is_empty() && EditorHelp::get_doc_data()) {
		p_help->go_to_class(help_class);
	}
}

void HelpTabType::_apply_payload(EditorHelp *p_help, const WorkspaceTab &p_tab) const {
	if (!p_help) {
		return;
	}
	_render_class(p_help, p_tab);
	const Dictionary &payload = p_tab.get_payload();
	if (payload.has("scroll")) {
		p_help->set_scroll(int(payload["scroll"]));
	}
}

Dictionary HelpTabType::_capture_payload(EditorHelp *p_help, const WorkspaceTab &p_tab) const {
	if (!p_help) {
		return p_tab.get_payload();
	}
	// The live surface only knows its class once go_to_class has rendered a page;
	// while the doc database is still loading edited_class is empty, so keep the
	// stored key rather than blanking a valid page.
	String help_class = p_help->get_class();
	if (help_class.is_empty()) {
		const Dictionary &stored = p_tab.get_payload();
		help_class = stored.has("help_class") ? String(stored["help_class"]) : p_tab.get_resource_key();
	}
	Dictionary payload;
	payload["help_class"] = help_class;
	payload["scroll"] = p_help->get_scroll();
	return payload;
}

StringName HelpTabType::type_id() const {
	return type_id_value;
}

bool HelpTabType::can_open(const String &p_resource) const {
	return !p_resource.is_empty();
}

WorkspaceTab HelpTabType::make_tab(const String &p_resource, int p_stable_id) const {
	WorkspaceTab tab;
	tab.set_stable_id(p_stable_id);
	tab.set_type_id(type_id_value);
	tab.set_resource_key(p_resource);
	tab.set_title_cache(derive_title(p_resource));
	tab.set_icon_key_cache("Help");
	// Seed the payload so a tab mounted before it is ever unmounted still knows
	// which class to render and where to scroll.
	Dictionary payload;
	payload["help_class"] = p_resource;
	payload["scroll"] = 0;
	tab.set_payload(payload);
	return tab;
}

String HelpTabType::get_title(const WorkspaceTab &p_tab) const {
	return derive_title(p_tab.get_resource_key());
}

Ref<Texture2D> HelpTabType::get_icon(const WorkspaceTab &p_tab) const {
	return Ref<Texture2D>();
}

void HelpTabType::mount(WorkspaceTab &p_tab, Control *p_chrome_host) {
	ERR_FAIL_NULL(p_chrome_host);

	EditorHelp *help = _resolve_surface(p_tab.get_stable_id());
	if (help) {
		if (help->get_parent() != p_chrome_host) {
			if (help->get_parent()) {
				help->get_parent()->remove_child(help);
			}
			p_chrome_host->add_child(help);
		}
		help->show();
	} else {
		help = _create_surface(p_chrome_host);
		mounted_surfaces[p_tab.get_stable_id()] = help->get_instance_id();
		_apply_payload(help, p_tab);
	}
	// Focus is claimed in activate(), not here: mounting a non-focused pane's tab
	// during layout restore must not steal focus from the focused pane.
}

void HelpTabType::unmount(WorkspaceTab &p_tab) {
	EditorHelp *help = _resolve_surface(p_tab.get_stable_id());
	if (help) {
		// Persist the displayed class and scroll before the pane frees the detached
		// surface so a later mount (including one in another pane) restores it.
		p_tab.set_payload(_capture_payload(help, p_tab));
	}
	mounted_surfaces.erase(p_tab.get_stable_id());
}

void HelpTabType::activate(WorkspaceTab &p_tab) {
	EditorHelp *help = _resolve_surface(p_tab.get_stable_id());
	if (!help) {
		return;
	}
	// Retry rendering if the page never loaded (the doc database was still
	// generating when the tab mounted). Focusing the tab is a natural retry point --
	// docs are almost always ready by the time the user brings a help tab forward.
	if (help->get_class().is_empty()) {
		_render_class(help, p_tab);
	}
	help->set_focused();
}

WorkspaceTabCloseResult HelpTabType::request_close(WorkspaceTab &p_tab, const Callable &p_on_deferred_close) {
	// Help has no unsaved state, so it always closes without a prompt.
	return WorkspaceTabCloseResult::CLOSE;
}

Dictionary HelpTabType::save_payload(const WorkspaceTab &p_tab) const {
	return _capture_payload(_resolve_surface(p_tab.get_stable_id()), p_tab);
}

void HelpTabType::restore_payload(WorkspaceTab &p_tab, const Dictionary &p_payload) const {
	p_tab.set_payload(p_payload);
}

bool HelpTabType::is_resource_available(const WorkspaceTab &p_tab) const {
	const String help_class = p_tab.get_resource_key();
	if (help_class.is_empty()) {
		return false;
	}
	// A page whose class no longer exists is dropped on restore. The doc database
	// is the authoritative set of help pages -- it includes script and @GlobalScope
	// pages beyond ClassDB -- but it generates asynchronously and in phases (native
	// docs first, project script docs after). Only treat a missing class as
	// authoritative once the whole database is loaded; until then the page cannot
	// be verified, so keep it rather than discarding a valid layout (a script or
	// @GlobalScope page that has not finished loading is not yet in class_list).
	DocTools *doc = EditorHelp::get_doc_data();
	if (!doc || doc->class_list.is_empty() || !EditorHelp::are_script_docs_loaded()) {
		return true;
	}
	return doc->class_list.has(help_class);
}
