/**************************************************************************/
/*  test_bottom_drawer_layout.h                                           */
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

#include "editor/gui/bottom_drawer_layout.h"

#include "tests/test_macros.h"

namespace TestBottomDrawerLayout {

static const String SECTION = "EditorNode";

static Dictionary get_dict(const Ref<ConfigFile> &p_config, const String &p_key) {
	return p_config->get_value(SECTION, p_key, Dictionary());
}

static void check_bottom_drawer_configs_equal(const Ref<ConfigFile> &p_a, const Ref<ConfigFile> &p_b) {
	CHECK(get_dict(p_a, "bottom_panel_offsets") == get_dict(p_b, "bottom_panel_offsets"));
	CHECK(get_dict(p_a, "bottom_panel_widths") == get_dict(p_b, "bottom_panel_widths"));
	CHECK(get_dict(p_a, "bottom_panel_pinned") == get_dict(p_b, "bottom_panel_pinned"));
	CHECK(p_a->get_value(SECTION, "bottom_panel_pinned_by_default", false) == p_b->get_value(SECTION, "bottom_panel_pinned_by_default", false));
}

static void check_width_dict_only_positive(const Dictionary &p_dict) {
	const LocalVector<Variant> keys = p_dict.get_key_list();
	for (const Variant &key : keys) {
		CHECK(int(p_dict[key]) > 0);
	}
}

static Dictionary make_dict(const String &p_key, const Variant &p_value) {
	Dictionary dict;
	dict[p_key] = p_value;
	return dict;
}

TEST_CASE("[Editor][BottomDrawerLayout] round-trip-current-layout-values") {
	BottomDrawerLayoutState state;
	state.offsets["Audio"] = 450;
	state.offsets["Output"] = 320;
	state.widths["Audio"] = 960;
	state.widths["Output"] = 720;
	state.pinned["Audio"] = true;
	state.pinned["Output"] = false;
	state.pinned_by_default = false;

	Ref<ConfigFile> config_a;
	config_a.instantiate();
	state.save_to_config(config_a, SECTION);

	BottomDrawerLayoutState loaded;
	loaded.load_from_config(config_a, SECTION);

	Ref<ConfigFile> config_b;
	config_b.instantiate();
	loaded.save_to_config(config_b, SECTION);

	check_bottom_drawer_configs_equal(config_a, config_b);
	CHECK(bool(get_dict(config_b, "bottom_panel_pinned")["Output"]) == false);
}

TEST_CASE("[Editor][BottomDrawerLayout] upgrade-missing-default-key-stays-pinned") {
	Ref<ConfigFile> config;
	config.instantiate();
	config->set_value(SECTION, "bottom_panel_offsets", make_dict("Audio", -450));

	BottomDrawerLayoutState loaded;
	loaded.load_from_config(config, SECTION);

	CHECK(loaded.offsets["Audio"] == 450);
	CHECK(loaded.pinned_by_default == true);
	CHECK(loaded.is_pinned("Audio") == true);
	CHECK(loaded.is_pinned("Unknown") == true);

	Ref<ConfigFile> saved;
	saved.instantiate();
	loaded.save_to_config(saved, SECTION);

	BottomDrawerLayoutState reloaded;
	reloaded.load_from_config(saved, SECTION);
	CHECK(reloaded.pinned_by_default == true);
	CHECK(reloaded.is_pinned("Audio") == true);
	CHECK(reloaded.is_pinned("Unknown") == true);
}

TEST_CASE("[Editor][BottomDrawerLayout] fresh-default-false-stays-false") {
	Ref<ConfigFile> config;
	config.instantiate();
	config->set_value(SECTION, "bottom_panel_offsets", make_dict("Audio", 450));
	config->set_value(SECTION, "bottom_panel_pinned_by_default", false);

	BottomDrawerLayoutState loaded;
	loaded.load_from_config(config, SECTION);

	CHECK(loaded.pinned_by_default == false);
	CHECK(loaded.is_pinned("Audio") == false);
	CHECK(loaded.is_pinned("Unknown") == false);

	Ref<ConfigFile> saved;
	saved.instantiate();
	loaded.save_to_config(saved, SECTION);

	BottomDrawerLayoutState reloaded;
	reloaded.load_from_config(saved, SECTION);
	CHECK(reloaded.pinned_by_default == false);
	CHECK(reloaded.is_pinned("Audio") == false);
	CHECK(reloaded.is_pinned("Unknown") == false);
}

TEST_CASE("[Editor][BottomDrawerLayout] explicit-pinned-overrides-default") {
	Ref<ConfigFile> config;
	config.instantiate();
	config->set_value(SECTION, "bottom_panel_pinned_by_default", true);
	Dictionary pinned;
	pinned["Audio"] = false;
	pinned["Output"] = true;
	config->set_value(SECTION, "bottom_panel_pinned", pinned);

	BottomDrawerLayoutState loaded;
	loaded.load_from_config(config, SECTION);

	CHECK(loaded.is_pinned("Audio") == false);
	CHECK(loaded.is_pinned("Output") == true);
	CHECK(loaded.is_pinned("Unknown") == true);

	Ref<ConfigFile> saved;
	saved.instantiate();
	loaded.save_to_config(saved, SECTION);

	CHECK(bool(get_dict(saved, "bottom_panel_pinned")["Audio"]) == false);

	BottomDrawerLayoutState reloaded;
	reloaded.load_from_config(saved, SECTION);
	CHECK(reloaded.is_pinned("Audio") == false);
	CHECK(bool(get_dict(saved, "bottom_panel_pinned")["Audio"]) == false);
}

TEST_CASE("[Editor][BottomDrawerLayout] widths-round-trip-and-ignore-non-positive-values") {
	{
		BottomDrawerLayoutState state;
		state.widths["Audio"] = 960;
		state.widths["Output"] = 0;
		state.widths["Debugger"] = -25;

		Ref<ConfigFile> config;
		config.instantiate();
		state.save_to_config(config, SECTION);

		const Dictionary widths = get_dict(config, "bottom_panel_widths");
		CHECK(widths.has("Audio"));
		CHECK(!widths.has("Output"));
		CHECK(!widths.has("Debugger"));
		check_width_dict_only_positive(widths);
	}

	{
		BottomDrawerLayoutState state;
		state.set_width("Audio", 960);
		state.set_width("Output", 0);
		state.set_width("Debugger", -25);

		CHECK(state.widths.has("Audio"));
		CHECK(!state.widths.has("Output"));
		CHECK(!state.widths.has("Debugger"));

		Ref<ConfigFile> config;
		config.instantiate();
		state.save_to_config(config, SECTION);

		const Dictionary widths = get_dict(config, "bottom_panel_widths");
		CHECK(widths.has("Audio"));
		CHECK(!widths.has("Output"));
		CHECK(!widths.has("Debugger"));
		check_width_dict_only_positive(widths);
	}

	{
		Ref<ConfigFile> config;
		config.instantiate();
		Dictionary widths;
		widths["Audio"] = 960;
		widths["Output"] = 0;
		widths["Debugger"] = -25;
		config->set_value(SECTION, "bottom_panel_widths", widths);

		BottomDrawerLayoutState loaded;
		loaded.load_from_config(config, SECTION);

		CHECK(loaded.widths.has("Audio"));
		CHECK(!loaded.widths.has("Output"));
		CHECK(!loaded.widths.has("Debugger"));

		Ref<ConfigFile> saved;
		saved.instantiate();
		loaded.save_to_config(saved, SECTION);

		const Dictionary saved_widths = get_dict(saved, "bottom_panel_widths");
		CHECK(saved_widths.has("Audio"));
		CHECK(!saved_widths.has("Output"));
		CHECK(!saved_widths.has("Debugger"));
		check_width_dict_only_positive(saved_widths);
	}
}

TEST_CASE("[Editor][BottomDrawerLayout] load-replaces-previous-state") {
	Ref<ConfigFile> audio_config;
	audio_config.instantiate();
	audio_config->set_value(SECTION, "bottom_panel_offsets", make_dict("Audio", 450));
	audio_config->set_value(SECTION, "bottom_panel_widths", make_dict("Audio", 960));
	audio_config->set_value(SECTION, "bottom_panel_pinned", make_dict("Audio", true));

	BottomDrawerLayoutState state;
	state.load_from_config(audio_config, SECTION);
	CHECK(state.offsets.has("Audio"));
	CHECK(state.widths.has("Audio"));
	CHECK(state.pinned.has("Audio"));

	Ref<ConfigFile> output_config;
	output_config.instantiate();
	output_config->set_value(SECTION, "bottom_panel_offsets", make_dict("Output", 320));

	state.load_from_config(output_config, SECTION);
	CHECK(!state.offsets.has("Audio"));
	CHECK(state.offsets.has("Output"));
	CHECK(!state.widths.has("Audio"));
	CHECK(state.pinned.is_empty());
}

} // namespace TestBottomDrawerLayout
