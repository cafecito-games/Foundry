/**************************************************************************/
/*  bundle_probe.m                                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             FOUNDRY ENGINE                             */
/*          A fork of the Godot Engine (https://godotengine.org)          */
/*                       https://www.cafecito.games                       */
/**************************************************************************/
/* Copyright (c) 2026-present Cafecito Games LLC.                         */
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

// A minimal Cocoa application used as the payload of a generated `.app` bundle.
// Launch Services only reports a process identifier for an application that checks in
// with the window server, so the probe has to be a real `NSApplication` rather than a
// script. It records the identity it was launched with, optionally waits for a
// continuation file, and then terminates with a caller-chosen exit code.

#import <AppKit/AppKit.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *option_value(const char *argument, const char *name) {
	const size_t length = strlen(name);
	if (strncmp(argument, name, length) != 0) {
		return NULL;
	}
	return argument + length;
}

int main(int argc, char **argv) {
	int exit_code = 0;
	const char *report_path = NULL;
	const char *continuation_path = NULL;

	for (int i = 1; i < argc; i++) {
		const char *value = option_value(argv[i], "--exit-code=");
		if (value) {
			exit_code = atoi(value);
			continue;
		}
		value = option_value(argv[i], "--report=");
		if (value) {
			report_path = value;
			continue;
		}
		value = option_value(argv[i], "--wait-file=");
		if (value) {
			continuation_path = value;
		}
	}

	@autoreleasepool {
		[NSApplication sharedApplication];
		[NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

		if (report_path) {
			FILE *report = fopen(report_path, "w");
			if (report) {
				// The launching test proves the bundle route was taken by checking that this
				// process was not forked by it.
				fprintf(report, "pid=%d\nparent_pid=%d\nexit_code=%d\n", (int)getpid(), (int)getppid(), exit_code);
				fclose(report);
			}
		}

		dispatch_source_t timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
		dispatch_source_set_timer(timer, dispatch_time(DISPATCH_TIME_NOW, 0), 20ull * NSEC_PER_MSEC, 5ull * NSEC_PER_MSEC);
		dispatch_source_set_event_handler(timer, ^{
			if (continuation_path) {
				struct stat info;
				if (stat(continuation_path, &info) != 0) {
					return;
				}
			}
			exit(exit_code);
		});
		dispatch_resume(timer);

		[NSApp run];
	}

	return exit_code;
}
