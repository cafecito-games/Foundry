# Editor Documentation Cache Upgrade Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Silently discard incompatible editor documentation caches and correctly parse empty self-closing tutorial sections.

**Architecture:** Keep invalidation inside `EditorHelp`, using the binary resource loader's existing silent type-recognition path before any full cache load. Make the XML reader recognize an empty tutorials element before entering its child loop. Cover both paths with observable C++ tests.

**Tech Stack:** C++17, Foundry `ResourceLoader`/`ResourceSaver`, `DocTools`, doctest, SCons/Ninja through `scripts/agent_build.py`.

---

### Task 1: Accept Empty Tutorial Sections

**Files:**
- Create: `tests/editor/test_doc_tools_xml.h`
- Modify: `tests/test_main.cpp:75-90`
- Modify: `editor/doc/doc_tools.cpp:1328-1349`

- [ ] **Step 1: Write the failing parser test**

Create a doctest that feeds a complete class document directly to `DocTools::load_xml()` and verifies
that a method following `<tutorials />` survives parsing:

```cpp
TEST_CASE("[Editor][DocToolsXml] self-closing tutorials preserve following methods") {
	const CharString xml = String(R"(<?xml version="1.0" encoding="UTF-8" ?>
<class name="EmptyTutorialsFixture" inherits="RefCounted">
	<brief_description>Fixture.</brief_description>
	<description>Fixture description.</description>
	<tutorials />
	<methods>
		<method name="after_tutorials">
			<return type="void" />
			<description>Still parsed.</description>
		</method>
	</methods>
</class>)")
					   .utf8();

	DocTools docs;
	REQUIRE_EQ(docs.load_xml(reinterpret_cast<const uint8_t *>(xml.get_data()), xml.length()), OK);
	REQUIRE(docs.class_list.has("EmptyTutorialsFixture"));
	const DocData::ClassDoc &class_doc = docs.class_list["EmptyTutorialsFixture"];
	REQUIRE_EQ(class_doc.methods.size(), 1);
	CHECK_EQ(class_doc.methods[0].name, "after_tutorials");
}
```

Include the new header from `tests/test_main.cpp`.

- [ ] **Step 2: Build and run the parser test to verify RED**

Run `python3 scripts/agent_build.py --backend ninja --test --case "*[Editor][DocToolsXml]*"`.

Expected: the test fails because `load_xml()` returns `ERR_FILE_CORRUPT` after reporting
`Invalid tag in doc file: methods.`

- [ ] **Step 3: Implement empty-section handling**

Before entering the tutorial child loop in `DocTools::_load()`, accept the empty element:

```cpp
} else if (name2 == "tutorials") {
	if (parser->is_empty()) {
		continue;
	}
	while (parser->read() == OK) {
```

- [ ] **Step 4: Run the parser test to verify GREEN**

Run `python3 scripts/agent_build.py --backend ninja --test --case "*[Editor][DocToolsXml]*"`.

Expected: the focused test passes without an invalid-tag diagnostic.

- [ ] **Step 5: Commit the parser fix**

```sh
git add editor/doc/doc_tools.cpp tests/editor/test_doc_tools_xml.h tests/test_main.cpp
git commit -m "fix(editor): Parse empty documentation tutorials"
```

### Task 2: Discard Incompatible Documentation Caches Before Loading

**Files:**
- Create: `tests/editor/test_editor_help_cache.h`
- Modify: `tests/test_main.cpp:75-100`
- Modify: `editor/doc/editor_help.h:190-250`
- Modify: `editor/doc/editor_help.cpp:3331-3535,3650-3670`

- [ ] **Step 1: Write failing cache behavior tests**

Create a helper in the test that saves an ordinary uncompressed `Resource`, then add two cases:

```cpp
static String save_cache_resource(const String &p_stem) {
	const String path = TestUtils::get_temp_path(p_stem + ".res");
	Ref<Resource> resource;
	resource.instantiate();
	CHECK_EQ(ResourceSaver::save(resource, path), OK);
	return path;
}

TEST_CASE("[Editor][EditorHelpCache] a current binary resource cache is retained") {
	const String path = save_cache_resource("editor_help_current_cache");
	CHECK(EditorHelp::test_prepare_doc_cache(path));
	CHECK(FileAccess::exists(path));
	DirAccess::remove_absolute(path);
}

TEST_CASE("[Editor][EditorHelpCache] a future binary resource cache is discarded") {
	const String path = save_cache_resource("editor_help_future_cache");
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ_WRITE);
	REQUIRE(file.is_valid());
	file->seek(20); // RSRC magic, endian, real width, engine major, and engine minor.
	file->store_32(UINT32_MAX);
	file.unref();

	CHECK_FALSE(EditorHelp::test_prepare_doc_cache(path));
	CHECK_FALSE(FileAccess::exists(path));
}
```

Include `core/io/dir_access.h`, `core/io/file_access.h`, `core/io/resource_saver.h`,
`editor/doc/editor_help.h`, `tests/test_macros.h`, and `tests/test_utils.h`. Include the new test header
from `tests/test_main.cpp`.

- [ ] **Step 2: Build to verify RED**

Run `python3 scripts/agent_build.py --backend ninja --test --case "*[Editor][EditorHelpCache]*"`.

Expected: compilation fails because `EditorHelp::test_prepare_doc_cache()` does not exist.

- [ ] **Step 3: Add the cache preparation helper**

Add a private production helper and a test-only forwarding seam:

```cpp
static bool _prepare_doc_cache(const String &p_path);

#ifdef TESTS_ENABLED
static bool test_prepare_doc_cache(const String &p_path) { return _prepare_doc_cache(p_path); }
#endif
```

Implement it in `editor_help.cpp`:

```cpp
bool EditorHelp::_prepare_doc_cache(const String &p_path) {
	if (!FileAccess::exists(p_path)) {
		return false;
	}
	if (ResourceLoader::get_resource_type(p_path) == "Resource") {
		return true;
	}

	DirAccess::remove_file_or_error(ProjectSettings::get_singleton()->globalize_path(p_path));
	return false;
}
```

- [ ] **Step 4: Route both cache entry points through the helper**

In `generate_doc()`, replace the raw existence check with:

```cpp
if (p_use_cache && _prepare_doc_cache(get_cache_full_path())) {
```

In `load_script_doc_cache()`, replace the raw existence check with:

```cpp
if (!_prepare_doc_cache(get_script_doc_cache_full_path())) {
	print_verbose("Script documentation cache not found or incompatible. Regenerating it may take a while for projects with many scripts.");
	regenerate_script_doc_cache();
	return;
}
```

- [ ] **Step 5: Build and run the cache tests to verify GREEN**

Run `python3 scripts/agent_build.py --backend ninja --test --case "*[Editor][EditorHelpCache]*"`.

Expected: both cache tests pass, the future-format file is gone, and no unsupported-format error is printed.

- [ ] **Step 6: Commit the cache fix**

```sh
git add editor/doc/editor_help.cpp editor/doc/editor_help.h tests/editor/test_editor_help_cache.h tests/test_main.cpp
git commit -m "fix(editor): Silently discard incompatible doc caches"
```

### Task 3: Validate Startup and the Full Change

**Files:**
- Modify only if validation reveals a defect in the approved scope.

- [ ] **Step 1: Run both focused regression suites together**

Run `python3 scripts/agent_build.py --backend ninja --test --case "*[Editor][DocToolsXml]*" --case "*[Editor][EditorHelpCache]*"`.

Expected: three passing test cases.

- [ ] **Step 2: Run native strict validation**

Run `python3 scripts/agent_build.py --test --case "*[Editor][DocToolsXml]*" --case "*[Editor][EditorHelpCache]*"`.

Expected: strict native build succeeds and all three focused cases pass.

- [ ] **Step 3: Verify editor startup regeneration**

Move a copy of a future-format test cache into an isolated test project's `.foundry/editor/` cache
path, launch the editor through the repository's editor automation bridge, wait for initialization,
and poll the editor log. Confirm that the cache is regenerated without either `can't be loaded` or
`Invalid tag in doc file: methods.`

- [ ] **Step 4: Inspect repository hygiene**

```sh
git diff --check develop...HEAD
git status --short
git diff --stat develop...HEAD
```

Expected: no whitespace errors; only the approved source, tests, spec, and plan are tracked; the
user-owned `.opencode/` directory remains untouched and untracked.

- [ ] **Step 5: Review and publish**

Run the requesting-code-review workflow, address verified findings, then use the GitHub publication
workflow to push the branch and open a draft pull request targeting `develop`.
