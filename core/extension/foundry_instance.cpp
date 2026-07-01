/**************************************************************************/
/*  foundry_instance.cpp                                                  */
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

#include "foundry_instance.h"

#include "core/extension/foundry_extension_manager.h"
#include "core/os/main_loop.h"
#include "main/main.h"
#include "servers/display/display_server.h"

void FoundryInstance::_bind_methods() {
	ClassDB::bind_method(D_METHOD("start"), &FoundryInstance::start);
	ClassDB::bind_method(D_METHOD("is_started"), &FoundryInstance::is_started);
	ClassDB::bind_method(D_METHOD("iteration"), &FoundryInstance::iteration);
	ClassDB::bind_method(D_METHOD("focus_in"), &FoundryInstance::focus_in);
	ClassDB::bind_method(D_METHOD("focus_out"), &FoundryInstance::focus_out);
	ClassDB::bind_method(D_METHOD("pause"), &FoundryInstance::pause);
	ClassDB::bind_method(D_METHOD("resume"), &FoundryInstance::resume);
}

FoundryInstance::FoundryInstance() {
}

FoundryInstance::~FoundryInstance() {
}

bool FoundryInstance::initialize(FoundryExtensionInitializationFunction p_init_func) {
	print_verbose("Foundry instance initialization");
	FoundryExtensionManager *foundry_extension_manager = FoundryExtensionManager::get_singleton();
	FoundryExtensionConstPtr<const FoundryExtensionInitializationFunction> ptr((const FoundryExtensionInitializationFunction *)&p_init_func);
	FoundryExtensionManager::LoadStatus status = foundry_extension_manager->load_extension_from_function("libfoundry://main", ptr);
	return status == FoundryExtensionManager::LoadStatus::LOAD_STATUS_OK;
}

bool FoundryInstance::start() {
	print_verbose("FoundryInstance::start()");
	Error err = Main::setup2();
	if (err != OK) {
		return false;
	}
	started = Main::start() == EXIT_SUCCESS;
	if (started) {
		OS::get_singleton()->get_main_loop()->initialize();
	}
	return started;
}

bool FoundryInstance::is_started() {
	return started;
}

bool FoundryInstance::iteration() {
	DisplayServer::get_singleton()->process_events();
	return Main::iteration();
}

void FoundryInstance::stop() {
	print_verbose("FoundryInstance::stop()");
	if (started) {
		OS::get_singleton()->get_main_loop()->finalize();
	}
	started = false;
}

void FoundryInstance::focus_out() {
	print_verbose("FoundryInstance::focus_out()");
	if (started) {
		if (OS::get_singleton()->get_main_loop()) {
			OS::get_singleton()->get_main_loop()->notification(MainLoop::NOTIFICATION_APPLICATION_FOCUS_OUT);
		}
	}
}

void FoundryInstance::focus_in() {
	print_verbose("FoundryInstance::focus_in()");
	if (started) {
		if (OS::get_singleton()->get_main_loop()) {
			OS::get_singleton()->get_main_loop()->notification(MainLoop::NOTIFICATION_APPLICATION_FOCUS_IN);
		}
	}
}

void FoundryInstance::pause() {
	print_verbose("FoundryInstance::pause()");
	if (started) {
		if (OS::get_singleton()->get_main_loop()) {
			OS::get_singleton()->get_main_loop()->notification(MainLoop::NOTIFICATION_APPLICATION_PAUSED);
		}
	}
}

void FoundryInstance::resume() {
	print_verbose("FoundryInstance::resume()");
	if (started) {
		if (OS::get_singleton()->get_main_loop()) {
			OS::get_singleton()->get_main_loop()->notification(MainLoop::NOTIFICATION_APPLICATION_RESUMED);
		}
	}
}
