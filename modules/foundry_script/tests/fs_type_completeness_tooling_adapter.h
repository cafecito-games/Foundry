/**************************************************************************/
/*  fs_type_completeness_tooling_adapter.h                                */
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

#include "fs_type_completeness_adapter.h"

#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/vector.h"

// The tooling surfaces this adapter observes are the editor's language-server helpers, which are
// compiled into editor builds and only when the protocol modules they need are available. The
// harness has to answer "could this build observe a tooling cell at all" in exactly one place, so
// the registration of the adapter, the `tools_enabled` member of a published report, and the reason
// a tooling cell is reported as not covered are all decided by this macro.
#if defined(TOOLS_ENABLED) && !defined(FOUNDRY_SCRIPT_NO_LSP)
#define FS_COMPLETENESS_TOOLING_ADAPTER_AVAILABLE
#endif

namespace FSTests {

// The adapter id the nine tooling rule manifests name. Declared in every build, including the ones
// that do not compile the adapter: a build that cannot register it still has to recognize the id as
// a configuration-gated one rather than as a typo in a manifest.
String tooling_adapter_id();

// Leaves of the `tooling_action` axis. The nine tooling surfaces the catalog partitions, declared
// here so the partition file and the adapter cannot disagree about the vocabulary.
Vector<String> tooling_actions();

// The subset of `tooling_actions()` this adapter can currently drive. A manifest domain that selects
// any other leaf is refused when it is validated, so a family whose surface has no driver yet is a
// catalog defect rather than a cell that quietly observes nothing.
Vector<String> tooling_driven_actions();

// The declared types a tooling cell can hold, and the axes and leaves the adapter renders. Declared
// unconditionally, so a build that does not compile the adapter still validates a tooling manifest
// against exactly what an editor build would: catalog validity is a property of the catalog, never of
// the configuration reading it.
Vector<String> tooling_destinations();
HashMap<String, Vector<String>> tooling_renderable_leaves();
HashSet<String> tooling_observable_dimensions();

// What one tooling cell compares: the type the tooling surface rendered for a program's carried
// member, and the type the analyzer resolved for the same member of the same program.
struct FSCompletenessToolingEvidence {
	String tooling_rendered_type;
	String analyzer_rendered_type;
};

// The one place a pair of renderings becomes a `tooling_parity` outcome, so a tooling disagreement
// can never be counted in one part of the harness and missed in another.
//
// `agrees` is byte equality. `wording_differs` is equality after collapsing the spacing a renderer
// is free to choose, which is the only difference the contract lets a family defer. Everything else,
// including a tooling surface that rendered nothing at all, is `type_differs`.
String compare_tooling_evidence(const FSCompletenessToolingEvidence &p_evidence);

// The ports a tooling host announced on its `FOUNDRY_TOOLING {...}` readiness line. A host that
// never announced one, or announced a malformed one, is unavailable rather than a host bound to an
// unknown port, so a caller only has to test the returned error.
struct FSCompletenessToolingHostReadiness {
	int lsp_port = -1;
	int dap_port = -1;
	bool local_only = false;
};

// Reads one line of a tooling host's standard output. Returns OK only for a well-formed readiness
// record announcing both listeners; every other line, including the host's own error record, is
// ERR_UNAVAILABLE. The pilot family drives its surface in process, so this is what a family that
// needs a host process reports `tooling_host_unavailable` from, and the tracked capture under
// `fixtures/tooling` is what holds it to the record the host really emits.
Error parse_tooling_host_readiness(
		const String &p_line, FSCompletenessToolingHostReadiness &r_readiness);

// The declared type a hover body shows for p_member_name. A hover renders the member's declaration
// verbatim (`var value: uint = 0U`), so the type is what stands between the member's type separator
// and its initializer. Returns an empty string when the body shows no declaration of that member or
// shows one with no declared type at all, which is a lost type rather than an unreadable body.
String rendered_type_in_hover_contents(const String &p_contents, const String &p_member_name);

#ifdef FS_COMPLETENESS_TOOLING_ADAPTER_AVAILABLE

namespace ToolingInternal {

// Test seam: makes the tooling surface render no type for the carried member, which is what a
// regression that stopped putting the declared type in a hover would produce. A dimension nothing
// can flip is a dimension nobody is observing.
void set_blank_tooling_rendering_for_test(bool p_blank);

// Test seam: makes the tooling host refuse to come up, so the structural stage that separates an
// unavailable host from a wrong type can be proven to fire rather than being assumed.
void set_tooling_host_unavailable_for_test(bool p_unavailable);

// Test seam: renders the carried type with the spacing a renderer is free to choose but the same
// type, so the one difference the contract lets a family defer can be told apart from a real one.
void set_respaced_tooling_rendering_for_test(bool p_respaced);

} // namespace ToolingInternal

// One completeness family per tooling surface: the editor-facing consumer that shows a user what
// type the front-end resolved. Every family renders the same program - a class holding one member of
// the declared type - and differs only in which tooling surface it asks about that member, so a
// surface is a query function here rather than a family-shaped copy of the whole adapter.
//
// `tooling_parity` is read by rendering the member's type through the tooling surface and again
// through the analyzer, off the same program. It is never derived from the cell's coordinates: a
// test seam blanks the tooling rendering and the regression tests require it to flip.
//
// What a family observes is the type its surface renders, not the request path that delivers it.
// The hover pilot builds the same document symbol and the same markdown body the language server
// answers a resolved hover with; the cursor-position resolution and the JSON-RPC envelope around it
// need a workspace rooted in a real project and a connected client, which a completeness run does
// not stand up.
class FSToolingAdapter : public FSCompletenessFamilyAdapter {
public:
	// The one instance of this adapter, and the one the registry hands out.
	static const FSToolingAdapter &shared();

	// Families whose rule manifests name this adapter. A rendered program carries its coordinates and
	// its identity but never the family that derived them, so the adapter needs the list to prove a
	// batch's case IDs are the canonical identities of their coordinates under one family.
	static Vector<String> families();

	String id() const override;
	Error render(const FSCompletenessResolvedCell &p_cell, FSCompletenessProgram &r_program) const override;
	FSCompletenessObservation analyze(
			const FSCompletenessProgram &p_program, const String &p_surface) const override;
	Error execute(const String &p_scratch_root, const Vector<FSCompletenessProgram> &p_programs,
			FSCompletenessRuntimeBatch &r_batch) const override;
	HashSet<String> observable_dimensions() const override;
	HashMap<String, Vector<String>> renderable_leaves() const override;

	// The observation one program produces, without the batch bookkeeping `execute` wraps it in.
	// `r_structural_error`, when given, reports whether the observation failed as a harness defect
	// rather than as an observation about the product: a cell whose tooling host never came up has no
	// reading under which its silence means the tooling surface agreed.
	FSCompletenessObservation observe_tooling_surface(
			const FSCompletenessProgram &p_program, Error *r_structural_error = nullptr) const;
};

#endif // FS_COMPLETENESS_TOOLING_ADAPTER_AVAILABLE

} // namespace FSTests
