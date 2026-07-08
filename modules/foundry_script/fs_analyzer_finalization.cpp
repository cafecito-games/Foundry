/**************************************************************************/
/*  fs_analyzer_finalization.cpp                                          */
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

#include "fs_analyzer.h"

Error FSAnalyzer::analyzer_result_from_parser_errors() const {
	return parser->errors.is_empty() ? OK : ERR_PARSE_ERROR;
}

// Phase 8 — Final diagnostics and dependency finalization
// Requires: witness bodies analyzed for full `analyze()` runs.
// Produces: applied pending warnings and dependency parsers raised to `INHERITANCE_SOLVED`.
// May report: delayed `@warning_ignore` warnings and dependency resolution failures.
// Must not: mutate parser status beyond the existing dependency-finalization step.
void FSAnalyzer::run_phase_apply_pending_warnings() {
#ifdef DEBUG_ENABLED
	parser->apply_pending_warnings();
#endif // DEBUG_ENABLED
}

Error FSAnalyzer::run_phase_finalize_analyzer_warnings() {
	run_phase_apply_pending_warnings();
	return analyzer_result_from_parser_errors();
}

Error FSAnalyzer::run_phase_final_diagnostics_and_dependencies() {
	AnalyzerPhaseScope phase_scope(this, AnalyzerPhase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES);
	for (KeyValue<String, Ref<FSParserRef>> &K : parser->depended_parsers) {
		if (K.value.is_null()) {
			return ERR_PARSE_ERROR;
		}
		dependency_parser_access.raise_parser_to_status(K.value, FSParserRef::INHERITANCE_SOLVED);
	}

	return analyzer_result_from_parser_errors();
}
