/**************************************************************************/
/*  test_conformance_registry_declarations_clear.h                        */
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

#pragma once

#include "modules/foundry_script/fs_conformance_registry.h"
#include "modules/foundry_script/fs_function.h"

#include "tests/test_macros.h"

// Regression coverage for issue #1965: the fixture runner's per-fixture reset must narrow to the
// declaration side only, or a fixture that shares a compiled declaring file with an earlier fixture
// in the same process loses the ability to dispatch that file's runtime witnesses. This test drives
// `FSConformanceRegistry` directly against fabricated declaration and runtime entries, standing in
// for what the analyzer and compiler register in a real run, so it does not depend on compiling any
// `.fs` source.
namespace TestConformanceRegistryDeclarationsClear {

struct RegistryDeclarationsClearFixture {
	static constexpr const char *SOURCE_FILE = "res://declarations_clear_fixture.fs";
	static constexpr const char *TARGET_KEY = "DeclarationsClearTarget";
	static constexpr const char *TRAIT_NAME = "DeclarationsClearTrait";
	static constexpr const char *METHOD_NAME = "declarations_clear_witness";

	// Never dereferenced by the registry paths this test exercises (`register_runtime_witnesses`,
	// `find_witness_function`); only its identity as a non-null pointer is observed.
	FSFunction *witness_function = reinterpret_cast<FSFunction *>(0x1);

	RegistryDeclarationsClearFixture() {
		FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();

		FSConformanceRegistry::Conformance declaration;
		declaration.target_keys.push_back(TARGET_KEY);
		declaration.target_fqcn = TARGET_KEY;
		declaration.target_script_path = SOURCE_FILE;
		declaration.target_is_root_class = true;
		declaration.trait_name = TRAIT_NAME;
		declaration.source_file = SOURCE_FILE;
		Vector<FSConformanceRegistry::Conformance> declarations;
		declarations.push_back(declaration);
		registry->register_file_conformances(SOURCE_FILE, declarations);

		FSConformanceRegistry::RuntimeConformance runtime_conformance;
		runtime_conformance.target_script = nullptr;
		runtime_conformance.target_keys.push_back(TARGET_KEY);
		runtime_conformance.trait_name = TRAIT_NAME;
		runtime_conformance.functions[METHOD_NAME] = witness_function;
		Vector<FSConformanceRegistry::RuntimeConformance> runtime_conformances;
		runtime_conformances.push_back(runtime_conformance);
		registry->register_runtime_witnesses(SOURCE_FILE, runtime_conformances);
	}

	~RegistryDeclarationsClearFixture() {
		FSConformanceRegistry::get_singleton()->clear();
	}
};

TEST_CASE("[Modules][FoundryScript][Conformance] clear_declarations keeps runtime witnesses dispatchable") {
	RegistryDeclarationsClearFixture fixture;
	FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();

	REQUIRE(registry->has_conformance(RegistryDeclarationsClearFixture::TARGET_KEY, RegistryDeclarationsClearFixture::TRAIT_NAME));
	REQUIRE(registry->find_witness_function(RegistryDeclarationsClearFixture::TARGET_KEY, RegistryDeclarationsClearFixture::METHOD_NAME) == fixture.witness_function);

	registry->clear_declarations();

	CHECK_FALSE(registry->has_conformance(RegistryDeclarationsClearFixture::TARGET_KEY, RegistryDeclarationsClearFixture::TRAIT_NAME));
	CHECK(registry->get_conformance_source(RegistryDeclarationsClearFixture::TARGET_KEY, RegistryDeclarationsClearFixture::TRAIT_NAME).is_empty());
	CHECK(registry->find_witness_function(RegistryDeclarationsClearFixture::TARGET_KEY, RegistryDeclarationsClearFixture::METHOD_NAME) == fixture.witness_function);
}

TEST_CASE("[Modules][FoundryScript][Conformance] clear empties both declarations and runtime witnesses") {
	RegistryDeclarationsClearFixture fixture;
	FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();

	REQUIRE(registry->has_conformance(RegistryDeclarationsClearFixture::TARGET_KEY, RegistryDeclarationsClearFixture::TRAIT_NAME));
	REQUIRE(registry->find_witness_function(RegistryDeclarationsClearFixture::TARGET_KEY, RegistryDeclarationsClearFixture::METHOD_NAME) == fixture.witness_function);

	registry->clear();

	CHECK_FALSE(registry->has_conformance(RegistryDeclarationsClearFixture::TARGET_KEY, RegistryDeclarationsClearFixture::TRAIT_NAME));
	CHECK(registry->find_witness_function(RegistryDeclarationsClearFixture::TARGET_KEY, RegistryDeclarationsClearFixture::METHOD_NAME) == nullptr);
}

} // namespace TestConformanceRegistryDeclarationsClear
