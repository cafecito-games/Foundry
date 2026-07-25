#pragma once

#ifdef TOOLS_ENABLED

#include "modules/foundry_script/fs_name_mangler_analysis.h"
#include "modules/foundry_script/fs_name_mangler_application.h"
#include "modules/foundry_script/tests/test_bytecode_serialization.h"

#include "tests/test_macros.h"

namespace FSTests {

TEST_CASE("[FoundryScript][NameManglerApplication] Scoped transaction exposes lifecycle") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"var private_marker_member: int\n"
			"func private_marker_method(value: int) -> int:\n"
			"\tprivate_marker_member += value\n"
			"\treturn private_marker_member\n");
	FSNameManglerAnalysis::Input analysis_input;
	analysis_input.scripts.push_back(script);
	const FSNameManglerAnalysis::Result analysis = FSNameManglerAnalysis::analyze(analysis_input);

	FSNameManglerApplication::Transaction transaction;
	Vector<FSNameManglerApplication::Diagnostic> diagnostics;
	CHECK_EQ(transaction.get_state(), FSNameManglerApplication::Transaction::STATE_UNUSED);
	CHECK_EQ(transaction.begin(analysis_input.scripts, analysis.rename_map, diagnostics), OK);
	CHECK(transaction.is_active());
	transaction.rollback();
	CHECK_FALSE(transaction.is_active());
	CHECK_EQ(transaction.get_state(), FSNameManglerApplication::Transaction::STATE_FINISHED);
}

} // namespace FSTests

#endif // TOOLS_ENABLED
