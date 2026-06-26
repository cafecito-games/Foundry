/**************************************************************************/
/*  run_target.cpp                                                        */
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

#include "run_target.h"

#include "core/io/config_file.h"

namespace {

// The first-class fields of a target section. Any other key in a section is
// round-tripped verbatim through `RunTarget::extra_keys` for forward compat.
bool is_known_target_key(const String &p_key) {
	return p_key == "name" ||
			p_key == "platform" ||
			p_key == "export_preset" ||
			p_key == "device_id" ||
			p_key == "signing_mode" ||
			p_key == "team_id";
}

} // namespace

Error RunTarget::save_all(const String &p_path, const Vector<RunTarget> &p_targets) {
	Ref<ConfigFile> config;
	config.instantiate();

	for (int i = 0; i < p_targets.size(); i++) {
		const RunTarget &target = p_targets[i];
		const String section = "target." + itos(i);

		config->set_value(section, "name", target.name);
		config->set_value(section, "platform", target.platform);
		config->set_value(section, "export_preset", target.export_preset);
		config->set_value(section, "device_id", target.device_id);
		config->set_value(section, "signing_mode", target.signing_mode);
		config->set_value(section, "team_id", target.team_id);

		// Preserve any unknown keys written by a different editor version. Skip
		// ones that collide with a known field so the first-class value wins.
		const Array extra = target.extra_keys.keys();
		for (int j = 0; j < extra.size(); j++) {
			const String key = extra[j];
			if (is_known_target_key(key)) {
				continue;
			}
			config->set_value(section, key, target.extra_keys[key]);
		}
	}

	return config->save(p_path);
}

Vector<RunTarget> RunTarget::load_all(const String &p_path, Error *r_error) {
	Vector<RunTarget> targets;

	Ref<ConfigFile> config;
	config.instantiate();

	const Error err = config->load(p_path);
	if (err != OK) {
		// A missing or empty file is a valid "no targets" state, not an error.
		if (r_error) {
			*r_error = (err == ERR_FILE_NOT_FOUND || err == ERR_FILE_CANT_OPEN) ? OK : err;
		}
		return targets;
	}

	int index = 0;
	while (true) {
		const String section = "target." + itos(index);
		if (!config->has_section(section)) {
			break;
		}

		RunTarget target;
		target.name = config->get_value(section, "name", String());
		target.platform = config->get_value(section, "platform", String());
		target.export_preset = config->get_value(section, "export_preset", String());
		target.device_id = config->get_value(section, "device_id", String());
		target.signing_mode = config->get_value(section, "signing_mode", String());
		target.team_id = config->get_value(section, "team_id", String());

		const Vector<String> keys = config->get_section_keys(section);
		for (const String &key : keys) {
			if (is_known_target_key(key)) {
				continue;
			}
			target.extra_keys[key] = config->get_value(section, key);
		}

		targets.push_back(target);
		index++;
	}

	if (r_error) {
		*r_error = OK;
	}
	return targets;
}
