/**************************************************************************/
/*  http_file_serve.cpp                                                   */
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

#include "http_file_serve.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/time.h"
#include "core/templates/list.h"

namespace {

// A chain longer than this is a link loop for every practical purpose, and resolving it further
// would let a crafted tree keep the server walking instead of answering.
constexpr int MAX_LINK_HOPS = 40;

const char *WEEKDAY_NAMES[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
const char *MONTH_NAMES[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

struct ContentType {
	const char *extension;
	const char *type;
};

// Enough of the web platform to serve an application directory. An extension that is not listed is
// served as opaque bytes rather than guessed at, because a wrong text type is what turns an
// uploaded file into a script the browser will run.
constexpr ContentType CONTENT_TYPES[] = {
	{ "html", "text/html" },
	{ "htm", "text/html" },
	{ "css", "text/css" },
	{ "js", "application/javascript" },
	{ "mjs", "application/javascript" },
	{ "json", "application/json" },
	{ "map", "application/json" },
	{ "xml", "application/xml" },
	{ "txt", "text/plain" },
	{ "md", "text/markdown" },
	{ "csv", "text/csv" },
	{ "svg", "image/svg+xml" },
	{ "png", "image/png" },
	{ "jpg", "image/jpeg" },
	{ "jpeg", "image/jpeg" },
	{ "gif", "image/gif" },
	{ "webp", "image/webp" },
	{ "avif", "image/avif" },
	{ "ico", "image/x-icon" },
	{ "wasm", "application/wasm" },
	{ "pck", "application/octet-stream" },
	{ "woff", "font/woff" },
	{ "woff2", "font/woff2" },
	{ "ttf", "font/ttf" },
	{ "otf", "font/otf" },
	{ "mp3", "audio/mpeg" },
	{ "ogg", "audio/ogg" },
	{ "wav", "audio/wav" },
	{ "mp4", "video/mp4" },
	{ "webm", "video/webm" },
	{ "pdf", "application/pdf" },
};

String content_type_for(const String &p_path) {
	const String extension = p_path.get_extension().to_lower();
	for (const ContentType &candidate : CONTENT_TYPES) {
		if (extension == candidate.extension) {
			return candidate.type;
		}
	}
	return "application/octet-stream";
}

bool is_decimal(const String &p_value, int p_max_digits) {
	if (p_value.is_empty() || p_value.length() > p_max_digits) {
		return false;
	}
	for (int i = 0; i < p_value.length(); i++) {
		if (!is_digit(p_value[i])) {
			return false;
		}
	}
	return true;
}

String join_path(const String &p_base, const String &p_component) {
	return p_base.ends_with("/") ? p_base + p_component : p_base + "/" + p_component;
}

// Resolves an absolute path the way the operating system would: `.` and `..` are collapsed against
// what has been resolved so far rather than lexically, and every symbolic link along the way is
// replaced by its target before resolution continues. Components that do not exist resolve to
// themselves, so a path can be checked against a mount root before anything is opened.
//
// Returns an empty string for a path that is not absolute or that cannot be resolved, which callers
// treat exactly like a path that resolved outside the root.
String canonicalize(const String &p_path, const Ref<DirAccess> &p_filesystem) {
	const String input = p_path.simplify_path();

	Vector<String> components = input.split("/", false);
	int first_component = 0;
	String root_prefix;
	if (input.begins_with("/")) {
		root_prefix = "/";
	} else if (!components.is_empty() && components[0].ends_with(":")) {
		// A Windows drive letter is the root of its own tree.
		root_prefix = components[0] + "/";
		first_component = 1;
	} else {
		return String();
	}

	List<String> pending;
	for (int i = first_component; i < components.size(); i++) {
		pending.push_back(components[i]);
	}

	Vector<String> resolved;
	int hops = 0;
	while (!pending.is_empty()) {
		const String component = pending.front()->get();
		pending.pop_front();

		if (component.is_empty() || component == ".") {
			continue;
		}
		if (component == "..") {
			if (!resolved.is_empty()) {
				resolved.resize(resolved.size() - 1);
			}
			continue;
		}

		String candidate = root_prefix;
		for (const String &part : resolved) {
			candidate = join_path(candidate, part);
		}
		candidate = join_path(candidate, component);

		if (!p_filesystem->is_link(candidate)) {
			resolved.push_back(component);
			continue;
		}

		if (++hops > MAX_LINK_HOPS) {
			return String();
		}
		const String target = p_filesystem->read_link(candidate);
		if (target.is_empty()) {
			return String();
		}
		// An absolute target restarts from the root; a relative one continues from the directory
		// the link lives in, which is what has been resolved so far.
		const Vector<String> target_components = target.simplify_path().split("/", false);
		int first_target_component = 0;
		if (target.begins_with("/")) {
			resolved.clear();
		} else if (!target_components.is_empty() && target_components[0].ends_with(":")) {
			resolved.clear();
			root_prefix = target_components[0] + "/";
			first_target_component = 1;
		}
		for (int i = target_components.size() - 1; i >= first_target_component; i--) {
			pending.push_front(target_components[i]);
		}
	}

	String result = root_prefix;
	for (const String &part : resolved) {
		result = join_path(result, part);
	}
	return result;
}

// True when `p_path` is the root itself or lives underneath it. The separator matters: without it
// `/srv/public-old` would count as inside `/srv/public`.
bool is_inside_root(const String &p_canonical_root, const String &p_canonical_path) {
	if (p_canonical_path == p_canonical_root) {
		return true;
	}
	const String root = p_canonical_root.ends_with("/") ? p_canonical_root : p_canonical_root + "/";
	return p_canonical_path.begins_with(root);
}

// The shapes a request path may never have, checked before anything is resolved. The request has
// already percent-decoded the path, so `%2e%2e` arrives here as `..` and is caught with it.
bool is_servable_request_path(const String &p_relative_path) {
	for (int i = 0; i < p_relative_path.length(); i++) {
		const char32_t character = p_relative_path[i];
		// Control characters, including the NUL a truncation attack relies on, and the separator
		// that only some platforms honor, which is how one platform's safe path becomes another's
		// escape.
		if (character < 0x20 || character == 0x7f || character == '\\') {
			return false;
		}
	}
	// An empty segment is never part of a normalized path, and it is how an absolute path is
	// smuggled in behind a prefix.
	if (p_relative_path.contains("//")) {
		return false;
	}
	for (const String &component : p_relative_path.split("/", false)) {
		if (component == "." || component == "..") {
			return false;
		}
	}
	return true;
}

String http_date(int64_t p_unix_time) {
	const Dictionary datetime = Time::get_singleton()->get_datetime_dict_from_unix_time(p_unix_time);
	const int weekday = datetime["weekday"];
	const int month = datetime["month"];
	const int day = datetime["day"];
	const int year = datetime["year"];
	const int hour = datetime["hour"];
	const int minute = datetime["minute"];
	const int second = datetime["second"];
	return vformat("%s, %02d %s %04d %02d:%02d:%02d GMT", WEEKDAY_NAMES[CLAMP(weekday, 0, 6)], day,
			MONTH_NAMES[CLAMP(month, 1, 12) - 1], year, hour, minute, second);
}

// Parses the one date format RFC 9110 requires a sender to use. The two obsolete formats are not
// accepted; an unparsable date is reported as `-1` and the condition is ignored, which serves the
// body rather than wrongly claiming the client's copy is current.
int64_t parse_http_date(const String &p_value) {
	const Vector<String> fields = p_value.strip_edges().split(" ", false);
	// "Www, DD Mon YYYY HH:MM:SS GMT".
	if (fields.size() != 6 || fields[5] != "GMT") {
		return -1;
	}
	if (!is_decimal(fields[1], 2) || !is_decimal(fields[3], 4)) {
		return -1;
	}

	int month = 0;
	for (int i = 0; i < 12; i++) {
		if (fields[2] == MONTH_NAMES[i]) {
			month = i + 1;
			break;
		}
	}
	if (month == 0) {
		return -1;
	}

	const Vector<String> clock = fields[4].split(":", false);
	if (clock.size() != 3 || !is_decimal(clock[0], 2) || !is_decimal(clock[1], 2) || !is_decimal(clock[2], 2)) {
		return -1;
	}

	Dictionary datetime;
	datetime["year"] = fields[3].to_int();
	datetime["month"] = month;
	datetime["day"] = fields[1].to_int();
	datetime["hour"] = clock[0].to_int();
	datetime["minute"] = clock[1].to_int();
	datetime["second"] = clock[2].to_int();
	return Time::get_singleton()->get_unix_time_from_datetime_dict(datetime);
}

// The strong validator for a file: its size and its modification time. Both change whenever the
// bytes do, and neither costs a read of the contents.
String entity_tag(uint64_t p_size, uint64_t p_modified_time) {
	return "\"" + String::num_uint64(p_size, 16) + "-" + String::num_uint64(p_modified_time, 16) + "\"";
}

bool entity_tag_matches(const String &p_field, const String &p_entity_tag) {
	for (const String &candidate : p_field.split(",", false)) {
		String token = candidate.strip_edges();
		if (token == "*") {
			return true;
		}
		// A weak comparison is the right one for a conditional read, so the weakness marker is
		// dropped rather than compared.
		if (token.begins_with("W/")) {
			token = token.substr(2);
		}
		if (token == p_entity_tag) {
			return true;
		}
	}
	return false;
}

enum RangeOutcome {
	// No range was asked for, or the request asked in a way this layer does not act on, which RFC
	// 9110 answers with the whole representation.
	RANGE_WHOLE,
	RANGE_PARTIAL,
	RANGE_UNSATISFIABLE,
};

RangeOutcome parse_range(const String &p_field, uint64_t p_size, uint64_t &r_start, uint64_t &r_end) {
	const String field = p_field.strip_edges();
	if (field.is_empty() || !field.begins_with("bytes=")) {
		return RANGE_WHOLE;
	}

	const String specifier = field.substr(6).strip_edges();
	// Multiple ranges would need a multipart body; answering with the whole file stays correct.
	if (specifier.contains(",")) {
		return RANGE_WHOLE;
	}

	const int separator = specifier.find_char('-');
	if (separator < 0) {
		return RANGE_WHOLE;
	}
	const String first = specifier.substr(0, separator).strip_edges();
	const String last = specifier.substr(separator + 1).strip_edges();

	if (first.is_empty()) {
		// A suffix range counts back from the end of the file.
		if (!is_decimal(last, 18)) {
			return RANGE_WHOLE;
		}
		const uint64_t suffix = uint64_t(last.to_int());
		if (suffix == 0 || p_size == 0) {
			return RANGE_UNSATISFIABLE;
		}
		r_start = suffix >= p_size ? 0 : p_size - suffix;
		r_end = p_size - 1;
		return RANGE_PARTIAL;
	}

	if (!is_decimal(first, 18)) {
		return RANGE_WHOLE;
	}
	const uint64_t start = uint64_t(first.to_int());
	if (start >= p_size) {
		return RANGE_UNSATISFIABLE;
	}

	uint64_t end = p_size - 1;
	if (!last.is_empty()) {
		if (!is_decimal(last, 18)) {
			return RANGE_WHOLE;
		}
		end = uint64_t(last.to_int());
		if (end < start) {
			return RANGE_WHOLE;
		}
		end = MIN(end, p_size - 1);
	}

	r_start = start;
	r_end = end;
	return RANGE_PARTIAL;
}

HTTPFileServe::Result refuse(const Ref<HTTPResponse> &p_response) {
	// The same answer whether the path names something outside the root or nothing at all, so a
	// refusal never reports what does or does not exist out there.
	p_response->set_status(403);
	p_response->set_header("Content-Type", "text/plain");
	p_response->send_string("Forbidden");
	return HTTPFileServe::RESULT_RESOLVED;
}

} // namespace

String HTTPFileServe::canonicalize_directory(const String &p_directory) {
	if (p_directory.is_empty()) {
		return String();
	}

	String directory = p_directory;
	if (directory.begins_with("res://") || directory.begins_with("user://")) {
		directory = ProjectSettings::get_singleton()->globalize_path(directory);
	}

	Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	if (filesystem.is_null()) {
		return String();
	}
	if (directory.is_relative_path()) {
		directory = filesystem->get_current_dir().path_join(directory);
	}

	const String canonical = canonicalize(directory, filesystem);
	if (canonical.is_empty() || !filesystem->dir_exists(canonical)) {
		return String();
	}
	return canonical;
}

HTTPFileServe::Result HTTPFileServe::serve(const String &p_canonical_root, const String &p_relative_path,
		const Ref<HTTPServerRequest> &p_request, const Ref<HTTPResponse> &p_response) {
	ERR_FAIL_COND_V(p_request.is_null() || p_response.is_null(), RESULT_NOT_FOUND);

	const String method = p_request->get_method();
	const bool head_request = method == "HEAD";
	if (method != "GET" && !head_request) {
		// A mount is a read-only view of a directory, and the methods it does answer are worth
		// naming so a client does not have to guess.
		p_response->set_status(405);
		p_response->set_header("Allow", "GET, HEAD");
		p_response->set_header("Content-Type", "text/plain");
		p_response->send_string("Method Not Allowed");
		return RESULT_RESOLVED;
	}

	if (!is_servable_request_path(p_relative_path)) {
		return refuse(p_response);
	}

	Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	if (filesystem.is_null()) {
		p_response->set_status(500);
		p_response->send_string("Internal Server Error");
		return RESULT_RESOLVED;
	}

	// Canonicalizing before opening anything is the whole defense: whatever the path looked like on
	// the wire, what is checked here is the file the operating system would actually open.
	const String target = canonicalize(p_canonical_root.path_join(p_relative_path.trim_prefix("/")), filesystem);
	if (target.is_empty() || !is_inside_root(p_canonical_root, target)) {
		return refuse(p_response);
	}

	// A directory has no representation to send, and listing one would publish the shape of the
	// tree, so the mount simply holds nothing at that path.
	if (filesystem->dir_exists(target) || !FileAccess::exists(target)) {
		return RESULT_NOT_FOUND;
	}

	Ref<FileAccess> file = FileAccess::open(target, FileAccess::READ);
	if (file.is_null()) {
		// The path is inside the root but cannot be read, which is a refusal rather than a claim
		// that nothing is there.
		return refuse(p_response);
	}
	const uint64_t size = file->get_length();
	const uint64_t modified_time = FileAccess::get_modified_time(target);
	file.unref();

	const String tag = entity_tag(size, modified_time);
	const String last_modified = http_date(int64_t(modified_time));

	// A tag is the stronger validator, so when the client sends one the date is not consulted at
	// all, even if the two disagree.
	bool unchanged = false;
	if (p_request->has_header("If-None-Match")) {
		unchanged = entity_tag_matches(p_request->get_header("If-None-Match"), tag);
	} else if (p_request->has_header("If-Modified-Since")) {
		const int64_t since = parse_http_date(p_request->get_header("If-Modified-Since"));
		unchanged = since >= 0 && int64_t(modified_time) <= since;
	}

	if (unchanged) {
		p_response->set_status(304);
		p_response->set_header("ETag", tag);
		p_response->set_header("Last-Modified", last_modified);
		p_response->set_header("Accept-Ranges", "bytes");
		p_response->send(PackedByteArray());
		return RESULT_RESOLVED;
	}

	uint64_t start = 0;
	uint64_t end = size == 0 ? 0 : size - 1;
	const RangeOutcome outcome = parse_range(p_request->get_header("Range"), size, start, end);

	if (outcome == RANGE_UNSATISFIABLE) {
		p_response->set_status(416);
		p_response->set_header("Content-Range", "bytes */" + String::num_uint64(size));
		p_response->set_header("Accept-Ranges", "bytes");
		p_response->set_header("Content-Type", "text/plain");
		p_response->send_string("Range Not Satisfiable");
		return RESULT_RESOLVED;
	}

	p_response->set_header("Content-Type", content_type_for(target));
	p_response->set_header("ETag", tag);
	p_response->set_header("Last-Modified", last_modified);
	p_response->set_header("Accept-Ranges", "bytes");

	if (outcome == RANGE_PARTIAL) {
		p_response->set_status(206);
		p_response->set_header("Content-Range",
				"bytes " + String::num_uint64(start) + "-" + String::num_uint64(end) + "/" + String::num_uint64(size));
		p_response->send_file_range(target, start, end - start + 1);
	} else {
		p_response->set_status(200);
		p_response->send_file_range(target, 0, size);
	}
	return RESULT_RESOLVED;
}
