/**************************************************************************/
/*  test_logger.h                                                         */
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

#include "core/io/dir_access.h"
#include "core/io/logger.h"
#include "modules/regex/regex.h"
#include "tests/test_macros.h"

namespace TestLogger {

constexpr int sleep_duration = 1200000;

String get_logs_dir_name() {
	return "process_" + itos(OS::get_singleton()->get_process_id());
}

String get_logs_dir() {
	return String("user://logs").path_join(get_logs_dir_name());
}

String get_logs_dir_absolute() {
	return OS::get_singleton()->get_user_data_dir().path_join("logs").path_join(get_logs_dir_name());
}

String get_log_file_path(const String &p_file_name) {
	return get_logs_dir().path_join(p_file_name);
}

String get_peer_log_file_name() {
	return "peer_process_" + itos(OS::get_singleton()->get_process_id()) + ".log";
}

String get_peer_log_file_path() {
	return String("user://logs").path_join(get_peer_log_file_name());
}

String get_peer_log_file_path_absolute() {
	return OS::get_singleton()->get_user_data_dir().path_join("logs").path_join(get_peer_log_file_name());
}

void initialize_logs() {
	ProjectSettings::get_singleton()->set_setting("application/config/name", "foundry_tests");
	DirAccess::make_dir_recursive_absolute(get_logs_dir_absolute());
}

void cleanup_logs() {
	ProjectSettings::get_singleton()->set_setting("application/config/name", "foundry_tests");
	Ref<DirAccess> dir = DirAccess::open(get_logs_dir());
	if (dir.is_null()) {
		return;
	}

	dir->list_dir_begin();
	String file = dir->get_next();
	while (file != "") {
		if (file.match("*.log")) {
			dir->remove(file);
		}
		file = dir->get_next();
	}
	dir->list_dir_end();
	DirAccess::remove_absolute(get_logs_dir_absolute());
}

TEST_CASE("[Logger][RotatedFileLogger] Cleanup leaves unrelated log files intact") {
	initialize_logs();

	const String unrelated_file_path = get_peer_log_file_path();
	Error err = Error::OK;
	Ref<FileAccess> unrelated_file = FileAccess::open(unrelated_file_path, FileAccess::WRITE, &err);
	REQUIRE_EQ(err, Error::OK);
	unrelated_file->store_string("preserved");
	unrelated_file.unref();

	cleanup_logs();

	CHECK(FileAccess::exists(unrelated_file_path));
	DirAccess::remove_absolute(get_peer_log_file_path_absolute());
}

TEST_CASE("[Logger][RotatedFileLogger] Creates the first log file and logs on it") {
	initialize_logs();

	String waiting_for_foundry = "Waiting for Foundry";
	RotatedFileLogger logger(get_log_file_path("foundry.log"));
	logger.logf("%s", "Waiting for Foundry");

	Error err = Error::OK;
	Ref<FileAccess> log = FileAccess::open(get_log_file_path("foundry.log"), FileAccess::READ, &err);
	CHECK_EQ(err, Error::OK);
	CHECK_EQ(log->get_as_text(), waiting_for_foundry);

	cleanup_logs();
}

TEST_CASE("[Logger][RotatedFileLogger] Falls back when the log destination cannot be opened") {
	initialize_logs();

	Ref<DirAccess> logs_dir = DirAccess::open(get_logs_dir());
	REQUIRE(logs_dir.is_valid());
	if (logs_dir->dir_exists("unavailable.log") || FileAccess::exists(get_log_file_path("unavailable.log"))) {
		REQUIRE_EQ(logs_dir->remove("unavailable.log"), OK);
	}
	REQUIRE_EQ(logs_dir->make_dir("unavailable.log"), OK);

	{
		RotatedFileLogger logger(get_log_file_path("unavailable.log"));
		logger.logf("%s", "Console logging remains available");
	}

	CHECK_EQ(logs_dir->remove("unavailable.log"), OK);
	cleanup_logs();
}

void get_log_files(Vector<String> &log_files) {
	Ref<DirAccess> dir = DirAccess::open(get_logs_dir());
	dir->list_dir_begin();
	String file = dir->get_next();
	while (file != "") {
		// Filtering foundry.log because ordered_insert will put it first and should be the last.
		if (file.match("*.log") && file != "foundry.log") {
			log_files.ordered_insert(file);
		}
		file = dir->get_next();
	}
	if (FileAccess::exists(get_log_file_path("foundry.log"))) {
		log_files.push_back("foundry.log");
	}
}

TEST_CASE("[Logger][RotatedFileLogger] Creates unique backups without waiting") {
	initialize_logs();

	const int logger_count = 3;
	for (int i = 0; i < logger_count; i++) {
		RotatedFileLogger logger(get_log_file_path("foundry.log"), logger_count);
		logger.logf("Logger %d", i);
	}

	Vector<String> log_files;
	get_log_files(log_files);
	CHECK_EQ(log_files.size(), logger_count);

	cleanup_logs();
}

// All things related to log file rotation are in the same test because testing it require some sleeps.
TEST_CASE("[Logger][RotatedFileLogger] Rotates logs files") {
	initialize_logs();

	Vector<String> all_waiting_for_foundry;

	const int number_of_files = 3;
	for (int i = 0; i < number_of_files; i++) {
		String waiting_for_foundry = "Waiting for Foundry " + itos(i);
		RotatedFileLogger logger(get_log_file_path("foundry.log"), number_of_files);
		logger.logf("%s", waiting_for_foundry.ascii().get_data());
		all_waiting_for_foundry.push_back(waiting_for_foundry);

		// Required to ensure the rotation of the log file.
		OS::get_singleton()->delay_usec(sleep_duration);
	}

	Vector<String> log_files;
	get_log_files(log_files);
	CHECK_MESSAGE(log_files.size() == number_of_files, "Did not rotate all files");

	for (int i = 0; i < log_files.size(); i++) {
		Error err = Error::OK;
		Ref<FileAccess> log_file = FileAccess::open(get_log_file_path(log_files[i]), FileAccess::READ, &err);
		REQUIRE_EQ(err, Error::OK);
		CHECK_EQ(log_file->get_as_text(), all_waiting_for_foundry[i]);
	}

	// Required to ensure the rotation of the log file.
	OS::get_singleton()->delay_usec(sleep_duration);

	// This time the oldest log must be removed and foundry.log updated.
	String new_waiting_for_foundry = "Waiting for Foundry " + itos(number_of_files);
	all_waiting_for_foundry = all_waiting_for_foundry.slice(1, all_waiting_for_foundry.size());
	all_waiting_for_foundry.push_back(new_waiting_for_foundry);
	RotatedFileLogger logger(get_log_file_path("foundry.log"), number_of_files);
	logger.logf("%s", new_waiting_for_foundry.ascii().get_data());

	log_files.clear();
	get_log_files(log_files);
	CHECK_MESSAGE(log_files.size() == number_of_files, "Did not remove old log file");

	for (int i = 0; i < log_files.size(); i++) {
		Error err = Error::OK;
		Ref<FileAccess> log_file = FileAccess::open(get_log_file_path(log_files[i]), FileAccess::READ, &err);
		REQUIRE_EQ(err, Error::OK);
		CHECK_EQ(log_file->get_as_text(), all_waiting_for_foundry[i]);
	}

	cleanup_logs();
}

TEST_CASE("[Logger][CompositeLogger] Logs the same into multiple loggers") {
	initialize_logs();

	Vector<Logger *> all_loggers;
	all_loggers.push_back(memnew(RotatedFileLogger(get_log_file_path("foundry_logger_1.log"), 1)));
	all_loggers.push_back(memnew(RotatedFileLogger(get_log_file_path("foundry_logger_2.log"), 1)));

	String waiting_for_foundry = "Waiting for Foundry";
	CompositeLogger logger(all_loggers);
	logger.logf("%s", "Waiting for Foundry");

	Error err = Error::OK;
	Ref<FileAccess> log = FileAccess::open(get_log_file_path("foundry_logger_1.log"), FileAccess::READ, &err);
	CHECK_EQ(err, Error::OK);
	CHECK_EQ(log->get_as_text(), waiting_for_foundry);
	log = FileAccess::open(get_log_file_path("foundry_logger_2.log"), FileAccess::READ, &err);
	CHECK_EQ(err, Error::OK);
	CHECK_EQ(log->get_as_text(), waiting_for_foundry);

	cleanup_logs();
}

} // namespace TestLogger
