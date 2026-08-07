/**************************************************************************/
/*  fs_refactoring_match_cases.cpp                                        */
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

#include "fs_refactoring_match_cases.h"

#ifdef TOOLS_ENABLED

#include "fs_refactoring_shared.h"

#include "../fs_analyzer.h"

#include "core/os/mutex.h"
#include "core/templates/hash_set.h"

#ifndef FOUNDRY_SCRIPT_NO_LSP
#include "../language_server/fs_extend_parser.h"
#include "../language_server/fs_workspace.h"
#endif // FOUNDRY_SCRIPT_NO_LSP

namespace {

constexpr const char *NO_MATCH_REASON = "Place the caret on a match over a tagged union.";
constexpr const char *ALREADY_COVERED_REASON = "All cases are already handled.";
constexpr const char *UNPROVABLE_REASON = "Match coverage cannot be determined.";
constexpr const char *CANNOT_ANALYZE_REASON = "Cannot analyze this script.";

// The `match` under the caret plus the local names visible where it sits, which the generated
// payload binds must not shadow.
struct MatchSearchResult {
	const FSParser::MatchNode *match_node = nullptr;
	HashSet<StringName> scope_names;
};

bool node_contains_line(const FSParser::Node *p_node, int p_line) {
	return p_node != nullptr && p_node->start_line <= p_line && p_line <= p_node->end_line;
}

bool find_match_in_suite(const FSParser::SuiteNode *p_suite, int p_line, HashSet<StringName> p_scope, MatchSearchResult &r_result);

bool find_match_in_statement(const FSParser::Node *p_statement, int p_line, const HashSet<StringName> &p_scope, MatchSearchResult &r_result) {
	switch (p_statement->type) {
		case FSParser::Node::IF: {
			const FSParser::IfNode *if_node = static_cast<const FSParser::IfNode *>(p_statement);
			return find_match_in_suite(if_node->true_block, p_line, p_scope, r_result) ||
					find_match_in_suite(if_node->false_block, p_line, p_scope, r_result);
		}
		case FSParser::Node::FOR: {
			const FSParser::ForNode *for_node = static_cast<const FSParser::ForNode *>(p_statement);
			return find_match_in_suite(for_node->loop, p_line, p_scope, r_result);
		}
		case FSParser::Node::WHILE: {
			const FSParser::WhileNode *while_node = static_cast<const FSParser::WhileNode *>(p_statement);
			return find_match_in_suite(while_node->loop, p_line, p_scope, r_result);
		}
		case FSParser::Node::MATCH: {
			const FSParser::MatchNode *match_node = static_cast<const FSParser::MatchNode *>(p_statement);
			// A nested `match` inside one of the branches is the more specific target.
			for (const FSParser::MatchBranchNode *branch : match_node->branches) {
				if (branch == nullptr) {
					continue;
				}
				if (find_match_in_suite(branch->guard_body, p_line, p_scope, r_result) ||
						find_match_in_suite(branch->block, p_line, p_scope, r_result)) {
					return true;
				}
			}
			r_result.match_node = match_node;
			r_result.scope_names = p_scope;
			return true;
		}
		default:
			return false;
	}
}

bool find_match_in_suite(const FSParser::SuiteNode *p_suite, int p_line, HashSet<StringName> p_scope, MatchSearchResult &r_result) {
	if (p_suite == nullptr) {
		return false;
	}
	for (const FSParser::SuiteNode::Local &local : p_suite->locals) {
		p_scope.insert(local.name);
	}
	for (const FSParser::Node *statement : p_suite->statements) {
		if (!node_contains_line(statement, p_line)) {
			continue;
		}
		if (find_match_in_statement(statement, p_line, p_scope, r_result)) {
			return true;
		}
	}
	return false;
}

bool find_match_in_class(const FSParser::ClassNode *p_class, int p_line, MatchSearchResult &r_result) {
	if (p_class == nullptr) {
		return false;
	}
	for (const FSParser::ClassNode::Member &member : p_class->members) {
		if (member.type == FSParser::ClassNode::Member::CLASS) {
			if (find_match_in_class(member.m_class, p_line, r_result)) {
				return true;
			}
			continue;
		}
		if (member.type != FSParser::ClassNode::Member::FUNCTION) {
			continue;
		}
		const FSParser::FunctionNode *function = member.function;
		if (function == nullptr || function->body == nullptr || !node_contains_line(function, p_line)) {
			continue;
		}
		HashSet<StringName> scope;
		for (const FSParser::ParameterNode *parameter : function->parameters) {
			if (parameter != nullptr && parameter->identifier != nullptr) {
				scope.insert(parameter->identifier->name);
			}
		}
		if (find_match_in_suite(function->body, p_line, scope, r_result)) {
			return true;
		}
	}
	return false;
}

// A payload bind may not shadow a name already visible at the match, and two binds of the same
// generated branch may not collide with each other. The smallest free numeric suffix keeps the
// choice deterministic across runs.
String unique_bind_name(const StringName &p_field_name, const HashSet<StringName> &p_scope_names, HashSet<StringName> &r_branch_names) {
	const String base = String(p_field_name);
	String name = base;
	int suffix = 2;
	while (p_scope_names.has(StringName(name)) || r_branch_names.has(StringName(name))) {
		name = base + itos(suffix);
		suffix++;
	}
	r_branch_names.insert(StringName(name));
	return name;
}

// Renders the uncovered arms as they would be written by hand: a contextual `.Case` head, implicit
// payload binds named after the declaration's fields, no parentheses on a payload-less case, and a
// single `pass` body. This is also exactly what the formatter emits, so the applied result is
// format-stable.
String render_uncovered_arms(
		const FSParser::MatchNode *p_match,
		const FSParser::DataType &p_match_type,
		const HashSet<StringName> &p_scope_names,
		const String &p_branch_indent,
		const String &p_body_indent) {
	String block;
	for (const StringName &case_name : p_match->uncovered_case_names) {
		String head = "." + String(case_name);
		const FSParser::DataType::EnumCasePayload *payload = p_match_type.get_enum_case_payload(case_name);
		if (payload != nullptr && !payload->field_names.is_empty()) {
			HashSet<StringName> branch_names;
			String binds;
			for (int i = 0; i < payload->field_names.size(); i++) {
				if (i > 0) {
					binds += ", ";
				}
				binds += unique_bind_name(payload->field_names[i], p_scope_names, branch_names);
			}
			head += "(" + binds + ")";
		}
		block += p_branch_indent + head + ":\n" + p_body_indent + "pass\n";
	}
	if (p_match->uncovered_includes_null) {
		block += p_branch_indent + "null:\n" + p_body_indent + "pass\n";
	}
	return block.substr(0, block.length() - 1); // Drop the trailing newline; the edit adds its own.
}

// Last line of the `match` body, found from the source rather than the node extents: a node's end
// line follows the last consumed token, which for a block is the newline/dedent boundary and not
// reliably the last line that carries body text.
int find_match_body_last_line(const Vector<String> &p_lines, int p_header_line) {
	const int header_indent = FSRefactorShared::get_leading_whitespace(p_lines[p_header_line]).length();
	int last_line = p_header_line;
	for (int i = p_header_line + 1; i < p_lines.size(); i++) {
		if (p_lines[i].strip_edges().is_empty()) {
			continue; // A blank line never ends a block.
		}
		if (FSRefactorShared::get_leading_whitespace(p_lines[i]).length() <= header_indent) {
			break;
		}
		last_line = i;
	}
	return last_line;
}

// First contiguous run of `pass`-only lines in the body of a branchless `match`. Those lines are the
// placeholder body the generated arms take the place of.
bool find_placeholder_pass_run(const Vector<String> &p_lines, int p_header_line, int p_body_end, int &r_first_line, int &r_last_line) {
	const int body_start = p_header_line + 1;
	const int body_end = p_body_end;
	for (int i = body_start; i <= body_end; i++) {
		if (p_lines[i].strip_edges() != "pass") {
			continue;
		}
		r_first_line = i;
		r_last_line = i;
		while (r_last_line + 1 <= body_end && p_lines[r_last_line + 1].strip_edges() == "pass") {
			r_last_line++;
		}
		return true;
	}
	return false;
}

FSRefactorMatchCases::FillMatchCasesCandidate disabled_candidate(const String &p_reason) {
	FSRefactorMatchCases::FillMatchCasesCandidate candidate;
	candidate.disabled_reason = p_reason;
	return candidate;
}

FSRefactorMatchCases::FillMatchCasesCandidate find_candidate_in_tree(
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		const FSParser::ClassNode *p_tree) {
	if (p_tree == nullptr) {
		return disabled_candidate(CANNOT_ANALYZE_REASON);
	}

	MatchSearchResult search;
	if (!find_match_in_class(p_tree, p_location.start_line + 1, search) || search.match_node == nullptr) {
		return disabled_candidate(NO_MATCH_REASON);
	}

	const FSParser::MatchNode *match_node = search.match_node;
	if (match_node->test == nullptr) {
		return disabled_candidate(NO_MATCH_REASON);
	}
	const FSParser::DataType match_type = match_node->test->get_datatype();
	if (!match_type.is_set() || !match_type.is_tagged_union_type()) {
		return disabled_candidate(NO_MATCH_REASON);
	}
	if (match_node->covers_subject_domain) {
		return disabled_candidate(ALREADY_COVERED_REASON);
	}
	// A non-constant pattern leaves the analyzer unable to prove what any branch covers, so it
	// publishes no uncovered cases at all while still reporting the match as non-covering.
	if (match_node->uncovered_case_names.is_empty() && !match_node->uncovered_includes_null) {
		return disabled_candidate(UNPROVABLE_REASON);
	}

	const int header_line = match_node->start_line - 1;
	if (header_line < 0 || header_line >= p_lines.size()) {
		return disabled_candidate(CANNOT_ANALYZE_REASON);
	}

	const FSParser::MatchBranchNode *last_branch = match_node->branches.is_empty()
			? nullptr
			: match_node->branches[match_node->branches.size() - 1];
	const String branch_indent = last_branch != nullptr && last_branch->start_line - 1 >= 0 && last_branch->start_line - 1 < p_lines.size()
			? FSRefactorShared::get_leading_whitespace(p_lines[last_branch->start_line - 1])
			: FSRefactorShared::get_leading_whitespace(p_lines[header_line]) + "\t";
	const String body_indent = branch_indent + "\t";
	const String block = render_uncovered_arms(match_node, match_type, search.scope_names, branch_indent, body_indent);

	FSRefactorMatchCases::FillMatchCasesCandidate candidate;
	candidate.anchor_column = body_indent.length();

	const int body_last_line = find_match_body_last_line(p_lines, header_line);
	int placeholder_first = -1;
	int placeholder_last = -1;
	if (last_branch == nullptr && find_placeholder_pass_run(p_lines, header_line, body_last_line, placeholder_first, placeholder_last)) {
		// The body is only a placeholder, so the arms take its place instead of following it.
		candidate.edit.start_line = placeholder_first;
		candidate.edit.start_column = 0;
		candidate.edit.end_line = placeholder_last;
		candidate.edit.end_column = p_lines[placeholder_last].length();
		candidate.edit.has_expected_text = true;
		String replaced;
		for (int i = placeholder_first; i <= placeholder_last; i++) {
			if (i > placeholder_first) {
				replaced += "\n";
			}
			replaced += p_lines[i];
		}
		candidate.edit.expected_text = replaced;
		candidate.edit.new_text = block;
		candidate.anchor_line_offset = 1;
	} else {
		const int anchor_line = body_last_line;
		candidate.edit.start_line = anchor_line;
		candidate.edit.start_column = p_lines[anchor_line].length();
		candidate.edit.end_line = anchor_line;
		candidate.edit.end_column = candidate.edit.start_column;
		candidate.edit.new_text = "\n" + block;
		candidate.anchor_line_offset = 2;
	}

	candidate.enabled = true;
	return candidate;
}

struct FillMatchCasesCache {
	bool valid = false;
	String path;
	uint64_t source_hash = 0;
	int source_length = 0;
	RefactorLocation location;
	FSRefactorMatchCases::FillMatchCasesCandidate candidate;
};

Mutex fill_match_cases_cache_mutex;
FillMatchCasesCache fill_match_cases_cache;

bool get_cached_candidate(const RefactorContext &p_context, const RefactorLocation &p_location, FSRefactorMatchCases::FillMatchCasesCandidate &r_candidate) {
	MutexLock lock(fill_match_cases_cache_mutex);

	if (!fill_match_cases_cache.valid ||
			fill_match_cases_cache.path != p_context.path ||
			fill_match_cases_cache.source_hash != p_context.source.hash64() ||
			fill_match_cases_cache.source_length != p_context.source.length() ||
			!FSRefactorShared::same_location(fill_match_cases_cache.location, p_location)) {
		return false;
	}
	r_candidate = fill_match_cases_cache.candidate;
	return true;
}

void cache_candidate(const RefactorContext &p_context, const RefactorLocation &p_location, const FSRefactorMatchCases::FillMatchCasesCandidate &p_candidate) {
	MutexLock lock(fill_match_cases_cache_mutex);

	fill_match_cases_cache.valid = true;
	fill_match_cases_cache.path = p_context.path;
	fill_match_cases_cache.source_hash = p_context.source.hash64();
	fill_match_cases_cache.source_length = p_context.source.length();
	fill_match_cases_cache.location = p_location;
	fill_match_cases_cache.candidate = p_candidate;
}

} // namespace

namespace FSRefactorMatchCases {

FillMatchCasesCandidate find_candidate(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const FSParseResultProvider *p_parse_results) {
	FillMatchCasesCandidate candidate;
	if (get_cached_candidate(p_context, p_location, candidate)) {
		return candidate;
	}

	const Vector<String> lines = p_context.source.split("\n");

#ifndef FOUNDRY_SCRIPT_NO_LSP
	if (p_parse_results != nullptr) {
		// Coverage must reflect the buffer the caret is in, not the last saved file.
		const ExtendFSParser *lsp_parser = p_parse_results->get_parse_result(p_context.path);
		if (lsp_parser != nullptr && lsp_parser->get_lines().size() == lines.size()) {
			bool same_source = true;
			for (int i = 0; i < lines.size(); i++) {
				if (lsp_parser->get_lines()[i] != lines[i]) {
					same_source = false;
					break;
				}
			}
			if (same_source) {
				candidate = find_candidate_in_tree(p_location, lines, lsp_parser->get_tree());
				cache_candidate(p_context, p_location, candidate);
				return candidate;
			}
		}
	}
#endif // FOUNDRY_SCRIPT_NO_LSP

	FSParser parser;
	if (parser.parse(p_context.source, p_context.path, false) != OK) {
		candidate = disabled_candidate(CANNOT_ANALYZE_REASON);
		cache_candidate(p_context, p_location, candidate);
		return candidate;
	}
	// A non-exhaustive `match` is not itself an analyzer error, but the surrounding script may hold
	// unrelated ones. Coverage is computed before any of them abort the pass, so the tree is used
	// regardless of the analyze result.
	FSAnalyzer analyzer(&parser);
	analyzer.analyze();

	candidate = find_candidate_in_tree(p_location, lines, parser.get_tree());
	cache_candidate(p_context, p_location, candidate);
	return candidate;
}

RefactorResult prepare(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const FSParseResultProvider *p_parse_results) {
	RefactorResult result;
	const FillMatchCasesCandidate candidate = find_candidate(p_context, p_location, p_parse_results);
	if (!candidate.enabled) {
		result.error_message = candidate.disabled_reason;
		return result;
	}

	result.ok = true;
	result.edits.push_back(candidate.edit);
	result.rename_anchor_line = candidate.edit.start_line + candidate.anchor_line_offset;
	result.rename_anchor_column = candidate.anchor_column;
	return result;
}

} // namespace FSRefactorMatchCases

#endif // TOOLS_ENABLED
