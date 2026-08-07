# Namespaced Native Classes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give native C++ classes a qualified, namespaced identity (e.g. `foundry.http.server.HTTPServer`) reachable in Foundry Script via `import`, usable as nodes in scenes, with the qualified name as the canonical ClassDB key and the bare global name an explicit, removable alias — proven end-to-end on one pilot class.

**Architecture:** ClassDB gains a `namespace_path` + derived `qualified_name` on `ClassInfo`; the registry is keyed by the qualified name (empty-namespace classes keep identical keys, so nothing else moves). A bare-name alias map provides optional global re-export with ambiguity detection. Foundry Script's existing `namespace`/`import` machinery is wired to consult ClassDB (not just `ScriptServer`) so namespaced natives resolve through `import`. Scenes store the qualified type string and resolve it through one `resolve_type_name` choke point. Docs and the editor carry the namespace alongside the class name.

**Tech Stack:** C++ (engine core), Foundry Script (`.fs`), SCons build via `python3 scripts/agent_build.py`, doctest C++ tests, Foundry Script fixtures under `modules/foundry_script/tests/scripts/`, pre-commit, `doc/classes` XML + `doc/class.xsd`.

**Spec:** `docs/superpowers/specs/2026-08-06-namespaced-native-classes-design.md`

---

## File Structure

**Modified — ClassDB (source of truth):**
- `core/object/class_db.h` — `ClassInfo` fields (`namespace_path`, `qualified_name`); new accessor declarations; alias map; `FOUNDRY_REGISTER_NAMESPACE` macro.
- `core/object/class_db.cpp` — `_add_class` qualified keying, `register_namespace` rekey, alias map logic, `class_exists`/`_instantiate_internal` bare-name resolution, new accessors, `resolve_type_name`.

**Modified — Foundry Script (resolution):**
- `modules/foundry_script/foundry_script.cpp` — `FSLanguage::init` excludes namespaced natives from flat globals; qualified `FSNativeClass` map.
- `modules/foundry_script/foundry_script.h` — qualified map member + accessors.
- `modules/foundry_script/fs_analyzer.cpp` — extend `get_global_class_in_namespace`, `get_imported_global_class`, `get_namespace_global_class_from_type_chain`; qualified identity in `reduce_identifier`/`resolve_datatype`.
- `modules/foundry_script/fs_compiler.cpp` — resolve qualified `native_type` from the qualified map.
- `modules/foundry_script/fs_editor.cpp` — namespace completion.
- `modules/foundry_script/GRAMMAR.md` — §4.2 reachability note.

**Modified — Scene/loader/editor/docs:**
- `scene/resources/packed_scene.cpp` — `resolve_type_name` at the instantiation choke point; qualified `MissingNode` preservation; packer writes qualified name.
- `scene/resources/packed_scene.h` — (if needed) helper.
- `editor/gui/create_dialog.cpp` — namespace grouping + qualified instantiation.
- `core/object/object.h` / `class_db.cpp` — `get_class()`/`is_class` runtime identity policy.
- `doc/class.xsd` — `namespace` attribute.
- `modules/foundry_script/doc_classes/` or `doc/classes/` — `foundry.http.server.HTTPServer.xml`.

**Created:**
- `scene/main/http_server.h` / `scene/main/http_server.cpp` — pilot class.
- `scene/register_scene_types.cpp` (modify) — register pilot class + namespace.
- `tests/` — ClassDB namespacing doctest.
- `modules/foundry_script/tests/scripts/` — FS fixtures + `.out`.
- A scene fixture exercising the qualified type.

---

### Task 1: ClassInfo namespace fields + canonical qualified keying

**Goal:** Store the namespace on `ClassInfo`, derive `qualified_name`, and make `_add_class` key the map by the qualified name (no-op for empty namespace, new identity for namespaced).

**Files:**
- Modify: `core/object/class_db.h` (`ClassInfo` struct ~line 122-169; the `classes` HashMap is already keyed by `StringName`)
- Modify: `core/object/class_db.cpp` (`_add_class` ~line 864-887)
- Test: `tests/test_class_db_namespace.h` (created here, wired in Task 5)

**Acceptance Criteria:**
- [ ] `ClassInfo` has `StringName namespace_path;` and `StringName qualified_name;`.
- [ ] `_add_class` keys `classes` by `qualified_name`, where `qualified_name = namespace_path.is_empty() ? name : namespace_path + "." + name`. Since `namespace_path` is empty at `_add_class` time, the key equals `name` (byte-identical to today).
- [ ] Existing classes still register without the "already exists" error (regression).

**Verify:** `python3 scripts/agent_build.py --backend ninja --test --case "*ClassDB*"` → existing ClassDB tests still pass; build succeeds.

**Steps:**

- [ ] **Step 1: Add fields to ClassInfo.** In `core/object/class_db.h`, inside `struct ClassInfo { ... }`, add after `StringName name;`:

```cpp
	StringName name;
	StringName namespace_path; // dotted, e.g. "foundry.http.server"; empty = global namespace.
	StringName qualified_name; // derived: namespace_path.is_empty() ? name : namespace_path + "." + name.
```

- [ ] **Step 2: Key `_add_class` by qualified name.** In `core/object/class_db.cpp`, `_add_class` currently keys by `name`. Since `namespace_path` is empty at add time, `qualified_name == name`; set it explicitly so later rekeying has a consistent invariant. Replace the body's keying:

```cpp
void ClassDB::_add_class(const GDType &p_class, const GDType *p_inherits) {
	Locker::Lock lock(Locker::STATE_WRITE);

	const StringName &name = p_class.get_name();
	// namespace_path is empty at _add_class time (set later by register_namespace),
	// so qualified_name == name. The invariant is maintained by register_namespace's rekey.
	const StringName &key = name;

	ERR_FAIL_COND_MSG(classes.has(key), vformat("Class '%s' already exists.", key));

	classes[key] = ClassInfo();
	ClassInfo &ti = classes[key];
	ti.name = name;
	ti.namespace_path = StringName();
	ti.qualified_name = name;
	ti.gdtype = &p_class;
	if (p_inherits) {
		ti.inherits = p_inherits->get_name();
	}
	ti.api = current_api;

	if (ti.inherits) {
		ERR_FAIL_COND(!classes.has(ti.inherits)); //it MUST be registered.
		ti.inherits_ptr = &classes[ti.inherits];
	} else {
		ti.inherits_ptr = nullptr;
	}
}
```

(The `key` indirection is explicit so a future change to add-time namespacing only touches one line.)

- [ ] **Step 3: Build and run existing ClassDB tests to confirm no regression.**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*ClassDB*"`
Expected: build succeeds; all existing ClassDB doctests pass.

- [ ] **Step 4: Commit.**

```bash
git add core/object/class_db.h core/object/class_db.cpp
git commit -m "feat(class_db): Add namespace_path/qualified_name to ClassInfo"
```

---

### Task 2: FOUNDRY_REGISTER_NAMESPACE macro + ClassDB::register_namespace (rekey)

**Goal:** Provide the registration-site seam that declares a class's namespace and rekeys the registry entry from its simple name to its qualified name.

**Files:**
- Modify: `core/object/class_db.h` (macro near `FOUNDRY_REGISTER_*` ~line 578; `register_namespace` declaration)
- Modify: `core/object/class_db.cpp` (`register_namespace` implementation)

**Acceptance Criteria:**
- [ ] `FOUNDRY_REGISTER_NAMESPACE(ClassName, "ns.path")` macro expands to `ClassDB::register_namespace(#ClassName, "ns.path")`.
- [ ] `ClassDB::register_namespace(p_class, p_namespace)` looks the class up by its simple name, sets `namespace_path` and `qualified_name`, and rekeys the `classes` map (copy ClassInfo → erase old key → insert under qualified key).
- [ ] Rekey copies the full `ClassInfo` (including `inherits_ptr`, `creation_func`, `exposed`, etc.) so the re-inserted entry is complete.
- [ ] `register_namespace` is a no-op (with an `ERR_PRINT`) if the class was already namespaced, to keep the simple-key invariant.

**Verify:** `python3 scripts/agent_build.py --backend ninja` → builds. (Behavioral test lands in Task 5.)

**Steps:**

- [ ] **Step 1: Declare `register_namespace`.** In `core/object/class_db.h`, in the public API section near the other `register_*` helpers:

```cpp
	static void register_namespace(const StringName &p_class, const StringName &p_namespace);
```

- [ ] **Step 2: Implement `register_namespace` with rekey.** In `core/object/class_db.cpp`:

```cpp
void ClassDB::register_namespace(const StringName &p_class, const StringName &p_namespace) {
	Locker::Lock lock(Locker::STATE_WRITE);

	// At this point the class is still keyed by its simple name (namespace was empty at _add_class).
	HashMap<StringName, ClassInfo>::Iterator it = classes.find(p_class);
	ERR_FAIL_COND_MSG(!it, vformat("Cannot set namespace for unknown class '%s'. Call FOUNDRY_REGISTER_CLASS before FOUNDRY_REGISTER_NAMESPACE.", p_class));
	if (!it->value.namespace_path.is_empty()) {
		ERR_PRINT(vformat("Class '%s' is already namespaced; ignoring re-registration.", p_class));
		return;
	}

	// Copy the entry, stamp the namespace, rekey.
	ClassInfo copy = it->value;
	copy.namespace_path = p_namespace;
	copy.qualified_name = StringName(p_namespace.operator String() + "." + copy.name.operator String());

	classes.remove(it);
	HashMap<StringName, ClassInfo>::Iterator inserted = classes.insert(copy.qualified_name, copy);
	// Re-stablish the inherits_ptr: re-point into the (unchanged) parent entry, which is not moved by this rekey.
	if (inserted->value.inherits) {
		HashMap<StringName, ClassInfo>::Iterator parent = classes.find(inserted->value.inherits);
		if (parent) {
			inserted->value.inherits_ptr = &parent->value;
		}
	}
}
```

Note: `inherits` is stored as the parent's *simple* name (e.g. `Node`), which is also its qualified name for flat classes, so the parent lookup is correct. A namespaced class inheriting another namespaced class is a follow-up (needs qualified `inherits`); not exercised by the pilot.

- [ ] **Step 3: Add the macro.** In `core/object/class_db.h`, next to `FOUNDRY_REGISTER_CLASS`:

```cpp
#define FOUNDRY_REGISTER_NAMESPACE(m_class, m_namespace) \
	::ClassDB::register_namespace(m_class::_get_class_name_static(), m_namespace);
```

(Use the class's static name accessor consistent with how `FOUNDRY_REGISTER_CLASS` resolves the name; if that macro uses `#m_class` instead, match it — verify against `FOUNDRY_REGISTER_CLASS` at ~line 578 and use the identical token form.)

- [ ] **Step 4: Build.**

Run: `python3 scripts/agent_build.py --backend ninja`
Expected: builds cleanly.

- [ ] **Step 5: Commit.**

```bash
git add core/object/class_db.h core/object/class_db.cpp
git commit -m "feat(class_db): Add FOUNDRY_REGISTER_NAMESPACE + register_namespace rekey"
```

---

### Task 3: Bare-name alias map + global re-export + lookup order

**Goal:** Add the optional global (bare-name) re-export layer with ambiguity detection, and route bare-name lookups through canonical-first-then-alias order.

**Files:**
- Modify: `core/object/class_db.h` (alias map member, `class_register_global_alias` declaration)
- Modify: `core/object/class_db.cpp` (`class_register_global_alias`, `class_exists`, `_instantiate_internal` bare resolution)

**Acceptance Criteria:**
- [ ] `class_register_global_alias(p_qualified, p_alias)` records that the qualified class is reachable by the bare alias.
- [ ] Bare-name lookup order: canonical `classes` map first (flat classes always win), then the alias map; 2+ owners on the alias map is ambiguous and the lookup fails.
- [ ] A new internal resolver `_resolve_by_any_name(StringName)` returns the qualified name (or empty on miss/ambiguity), used by `class_exists`/`_instantiate_internal`.

**Verify:** `python3 scripts/agent_build.py --backend ninja` → builds. (Behavioral test in Task 5.)

**Steps:**

- [ ] **Step 1: Add the alias map + helper declarations.** In `core/object/class_db.h`, near the `classes` member (~line 205) and the public API:

```cpp
	// bare alias -> qualified names that expose it (global re-export layer).
	HashMap<StringName, LocalVector<StringName>> bare_aliases;

	static void class_register_global_alias(const StringName &p_qualified, const StringName &p_alias);
	// Resolve any name (qualified, bare-flat, or bare-alias) to a canonical qualified name.
	// Returns empty StringName on miss or ambiguous bare alias.
	static StringName _resolve_by_any_name(const StringName &p_name);
```

- [ ] **Step 2: Implement the helper + alias registration.** In `core/object/class_db.cpp`:

```cpp
void ClassDB::class_register_global_alias(const StringName &p_qualified, const StringName &p_alias) {
	Locker::Lock lock(Locker::STATE_WRITE);
	ERR_FAIL_COND_MSG(!classes.has(p_qualified), vformat("Cannot alias unknown class '%s'.", p_qualified));
	bare_aliases[p_alias].push_back(p_qualified);
}

StringName ClassDB::_resolve_by_any_name(const StringName &p_name) {
	// Canonical key first: a flat class whose key IS the bare name always wins.
	HashMap<StringName, ClassInfo>::ConstIterator canon = classes.find(p_name);
	if (canon) {
		return canon->value.qualified_name;
	}
	// Then the alias map (re-exported bare names of namespaced classes).
	HashMap<StringName, LocalVector<StringName>>::ConstIterator alias = bare_aliases.find(p_name);
	if (alias) {
		if (alias->value.size() == 1) {
			return alias->value[0];
		}
		// Ambiguous: more than one namespaced class re-exports this bare name.
		return StringName();
	}
	return StringName();
}
```

- [ ] **Step 3: Route `class_exists` through the resolver.** Update `class_exists` (currently `return classes.has(p_class);` at ~line 561-564) to also accept bare aliases:

```cpp
bool ClassDB::class_exists(const StringName &p_class) {
	Locker::Lock lock(Locker::STATE_READ);
	return classes.has(p_class) || _resolve_by_any_name(p_class) != StringName();
}
```

(`classes.has(p_class)` is the fast path for the canonical key; the resolver covers bare aliases. A bare alias with 2+ owners resolves to empty → `class_exists` returns false, forcing qualification.)

- [ ] **Step 4: Route `_instantiate_internal` through the resolver.** In `_instantiate_internal` (~line 578), resolve the incoming name to canonical before the `classes[p_class]` lookup so bare aliases instantiate correctly:

```cpp
StringName resolved = _resolve_by_any_name(p_class);
if (resolved == StringName() && !classes.has(p_class)) {
	// neither canonical nor a unique alias -> try compat fallback (unchanged path below)
	resolved = p_class; // keep existing behavior for compat_classes lookup
}
const StringName &key = classes.has(resolved) ? resolved : p_class;
// ... use `key` in place of p_class for the classes[...] lookup and compat fallback below
```

(Preserve the existing `compat_classes` fallback semantics; the only change is that a unique bare alias resolves to its canonical key first.)

- [ ] **Step 5: Build.**

Run: `python3 scripts/agent_build.py --backend ninja`
Expected: builds cleanly.

- [ ] **Step 6: Commit.**

```bash
git add core/object/class_db.h core/object/class_db.cpp
git commit -m "feat(class_db): Add bare-name alias map + canonical-first lookup order"
```

---

### Task 4: ClassDB read accessors + resolve_type_name + class_get_in_namespace

**Goal:** Expose the namespace data to scripts/scenes/editor and provide the central type-name resolver used by the scene loader.

**Files:**
- Modify: `core/object/class_db.h` (declarations)
- Modify: `core/object/class_db.cpp` (implementations)

**Acceptance Criteria:**
- [ ] `class_get_qualified_name(p_class)`, `class_get_namespace(p_class)`, `class_get_by_qualified_name(p_qualified)` work.
- [ ] `class_get_in_namespace(p_namespace, p_class_name)` returns the qualified name of the class with that namespace + simple name, or empty.
- [ ] `resolve_type_name(StringName)` resolves qualified → canonical; bare-unique → canonical; bare-ambiguous/flat-flat → passthrough or empty per the rules. This is the public version used by the scene loader.

**Verify:** `python3 scripts/agent_build.py --backend ninja` → builds. (Behavioral test in Task 5.)

**Steps:**

- [ ] **Step 1: Declare the accessors.** In `core/object/class_db.h` public API:

```cpp
	static StringName class_get_qualified_name(const StringName &p_class);
	static StringName class_get_namespace(const StringName &p_class);
	static bool class_get_by_qualified_name(const StringName &p_qualified, StringName &r_class_name);
	static StringName class_get_in_namespace(const StringName &p_namespace, const StringName &p_class_name);
	// Central resolver for the scene loader: returns the canonical qualified name for a type string.
	static StringName resolve_type_name(const StringName &p_name);
```

- [ ] **Step 2: Implement them.** In `core/object/class_db.cpp`:

```cpp
StringName ClassDB::class_get_qualified_name(const StringName &p_class) {
	Locker::Lock lock(Locker::STATE_READ);
	StringName resolved = _resolve_by_any_name(p_class);
	HashMap<StringName, ClassInfo>::ConstIterator it = classes.find(resolved != StringName() ? resolved : p_class);
	ERR_FAIL_COND_V_MSG(!it, StringName(), vformat("Class '%s' not found.", p_class));
	return it->value.qualified_name;
}

StringName ClassDB::class_get_namespace(const StringName &p_class) {
	Locker::Lock lock(Locker::STATE_READ);
	StringName resolved = _resolve_by_any_name(p_class);
	HashMap<StringName, ClassInfo>::ConstIterator it = classes.find(resolved != StringName() ? resolved : p_class);
	ERR_FAIL_COND_V_MSG(!it, StringName(), vformat("Class '%s' not found.", p_class));
	return it->value.namespace_path;
}

bool ClassDB::class_get_by_qualified_name(const StringName &p_qualified, StringName &r_class_name) {
	Locker::Lock lock(Locker::STATE_READ);
	HashMap<StringName, ClassInfo>::ConstIterator it = classes.find(p_qualified);
	if (!it) {
		return false;
	}
	r_class_name = it->value.name;
	return true;
}

StringName ClassDB::class_get_in_namespace(const StringName &p_namespace, const StringName &p_class_name) {
	Locker::Lock lock(Locker::STATE_READ);
	StringName candidate = p_namespace.is_empty()
			? p_class_name
			: StringName(p_namespace.operator String() + "." + p_class_name.operator String());
	HashMap<StringName, ClassInfo>::ConstIterator it = classes.find(candidate);
	if (it) {
		return it->value.qualified_name;
	}
	return StringName();
}

StringName ClassDB::resolve_type_name(const StringName &p_name) {
	// Qualified or canonical key -> direct hit.
	if (classes.has(p_name)) {
		return p_name;
	}
	// Bare name -> unique alias resolves, ambiguous/missing returns empty.
	StringName resolved = _resolve_by_any_name(p_name);
	return resolved;
}
```

(`resolve_type_name` is intentionally `STATE_READ`-free here because the callers already hold the lock context at the instantiation site; add a `Locker::Lock` if called from unlocked contexts — see Task 12. Keep it consistent with `_resolve_by_any_name`'s locking when finalized.)

- [ ] **Step 3: Build.**

Run: `python3 scripts/agent_build.py --backend ninja`
Expected: builds cleanly.

- [ ] **Step 4: Commit.**

```bash
git add core/object/class_db.h core/object/class_db.cpp
git commit -m "feat(class_db): Add namespace accessors + resolve_type_name"
```

---

### Task 5: ClassDB namespacing doctest

**Goal:** Prove the ClassDB layer behaves correctly: keying regression, rekey to qualified, alias uniqueness, ambiguity rejection, `resolve_type_name`.

**Files:**
- Create: `tests/test_class_db_namespace.h`
- Modify: `tests/test_main.cpp` (include the new header)
- Modify: `tests/test_class_db.h` or a registration helper if the test needs throwaway classes (use existing internal test classes registered for the test, or register runtime classes if available)

**Acceptance Criteria:**
- [ ] Registering a class with `FOUNDRY_REGISTER_NAMESPACE` rekeys it to the qualified name; `class_get_by_qualified_name` finds it, the bare key no longer exists (namespace-only).
- [ ] An empty-namespace class still keys by its simple name (regression).
- [ ] `class_register_global_alias` makes the bare name resolve uniquely; a second alias on the same bare name makes `class_exists`/`resolve_type_name` return false/empty for the bare name (ambiguous), while both qualified names still resolve.
- [ ] `class_get_in_namespace` resolves the namespace+simple pair.

**Verify:** `python3 scripts/agent_build.py --backend ninja --test --case "*ClassDBNamespace*"` → all assertions pass.

**Steps:**

- [ ] **Step 1: Write the test header.** Create `tests/test_class_db_namespace.h`. Use two already-registered internal test classes for the alias-collision case (verify which test-only classes exist in `tests/` — e.g. any `Foo`/`Bar` test types; if none are reusable, register two minimal `Object` subclasses inside the test via `ClassDB::register_class`). Skeleton:

```cpp
#pragma once

#include "tests/test_macros.h"
#include "core/object/class_db.h"

// Throwaway classes for the alias-collision test.
class _NsTestHttp : public Object {
	FOUNDRY_CLASS(_NsTestHttp, Object);
protected:
	static void _bind_methods() {}
};
class _NsTestFtp : public Object {
	FOUNDRY_CLASS(_NsTestFtp, Object);
protected:
	static void _bind_methods() {}
};

TEST_CASE("[ClassDBNamespace] Empty-namespace class keys by simple name") {
	// Node is a flat class: qualified name == simple name.
	CHECK(ClassDB::class_get_qualified_name("Node") == StringName("Node"));
	CHECK(ClassDB::class_get_namespace("Node") == StringName());
}

TEST_CASE("[ClassDBNamespace] register_namespace rekeys to qualified name") {
	ClassDB::register_class<_NsTestHttp>();
	ClassDB::register_namespace("_NsTestHttp", "foundry.test.http");
	// Canonical key is now the qualified name.
	StringName found;
	CHECK(ClassDB::class_get_by_qualified_name("foundry.test.http._NsTestHttp", found));
	CHECK(found == StringName("_NsTestHttp"));
	CHECK(ClassDB::class_get_namespace("_NsTestHttp") == StringName("foundry.test.http"));
	CHECK(ClassDB::class_get_in_namespace("foundry.test.http", "_NsTestHttp") == StringName("foundry.test.http._NsTestHttp"));
}

TEST_CASE("[ClassDBNamespace] Ambiguous bare alias is rejected, qualified still resolves") {
	ClassDB::register_class<_NsTestFtp>();
	ClassDB::register_namespace("_NsTestFtp", "foundry.test.ftp");
	// Re-export both under the same bare alias "TestServer".
	ClassDB::class_register_global_alias("foundry.test.http._NsTestHttp", "TestServer");
	CHECK(ClassDB::class_exists("TestServer") == true); // unique so far
	ClassDB::class_register_global_alias("foundry.test.ftp._NsTestFtp", "TestServer");
	// Now ambiguous: bare name no longer resolves, but qualified names do.
	CHECK(ClassDB::class_exists("TestServer") == false);
	CHECK(ClassDB::resolve_type_name("TestServer") == StringName());
	CHECK(ClassDB::resolve_type_name("foundry.test.http._NsTestHttp") == StringName("foundry.test.http._NsTestHttp"));
	CHECK(ClassDB::resolve_type_name("foundry.test.ftp._NsTestFtp") == StringName("foundry.test.ftp._NsTestFtp"));
}
```

- [ ] **Step 2: Wire the header.** In `tests/test_main.cpp`, add `#include "test_class_db_namespace.h"` alongside the other test includes.

- [ ] **Step 3: Build and run.**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*ClassDBNamespace*"`
Expected: all three subcases PASS.

- [ ] **Step 4: Commit.**

```bash
git add tests/test_class_db_namespace.h tests/test_main.cpp
git commit -m "test(class_db): Namespace keying, rekey, and alias ambiguity"
```

---

### Task 6: Pilot HTTPServer native class + namespace registration

**Goal:** Add a minimal-but-real `HTTPServer : public Node` registered under `foundry.http.server`, with enough surface to exercise every resolution path.

**Files:**
- Create: `scene/main/http_server.h`
- Create: `scene/main/http_server.cpp`
- Modify: `scene/main/SCsub` or `scene/register_scene_types.cpp` (register the class + namespace)
- Modify: `doc/classes/HTTPServer.xml` (created here minimally; full doc in Task 15)

**Acceptance Criteria:**
- [ ] `HTTPServer` is a `Node` subclass, constructible via `ClassDB::instantiate`, with one method and one property.
- [ ] Registered with `FOUNDRY_REGISTER_CLASS(HTTPServer)` then `FOUNDRY_REGISTER_NAMESPACE(HTTPServer, "foundry.http.server")`.
- [ ] `ClassDB::class_get_by_qualified_name("foundry.http.server.HTTPServer", ...)` succeeds; the bare key `HTTPServer` does not exist (namespace-only, no alias).
- [ ] `get_class()` on an instance returns `foundry.http.server.HTTPServer` (verified after Task 13 lands the identity policy; here just confirm instantiation).

**Verify:** `python3 scripts/agent_build.py --backend ninja --test --case "*ClassDB*"` → builds and the qualified-name lookup is reachable.

**Steps:**

- [ ] **Step 1: Create the header.** `scene/main/http_server.h`:

```cpp
#pragma once

#include "scene/main/node.h"

// Pilot class for namespaced native classes. Minimal surface: enough to
// exercise script import, scene instantiation, and inheritance resolution.
class HTTPServer : public Node {
	FOUNDRY_CLASS(HTTPServer, Node);

	int port = 8080;

protected:
	static void _bind_methods();

public:
	void set_port(int p_port) { port = p_port; }
	int get_port() const { return port; }

	bool start();
	void stop();

	HTTPServer();
};
```

- [ ] **Step 2: Create the implementation.** `scene/main/http_server.cpp`:

```cpp
#include "http_server.h"

#include "core/object/class_db.h"

void HTTPServer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("start"), &HTTPServer::start);
	ClassDB::bind_method(D_METHOD("stop"), &HTTPServer::stop);
	ClassDB::bind_method(D_METHOD("set_port", "port"), &HTTPServer::set_port);
	ClassDB::bind_method(D_METHOD("get_port"), &HTTPServer::get_port);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "port"), "set_port", "get_port");
}

bool HTTPServer::start() {
	// Pilot stub: full HTTP semantics are a follow-up.
	return true;
}

void HTTPServer::stop() {
}

HTTPServer::HTTPServer() {
}
```

- [ ] **Step 3: Register the class and its namespace.** In `scene/register_scene_types.cpp`, in the scene initialization level (near the existing `FOUNDRY_REGISTER_CLASS(HTTPRequest)` call site):

```cpp
#include "main/http_server.h"
// ...
FOUNDRY_REGISTER_CLASS(HTTPServer);
FOUNDRY_REGISTER_NAMESPACE(HTTPServer, "foundry.http.server");
```

(Add the matching `FOUNDRY_UNREGISTER_CLASS` / cleanup in the uninitialize function if that file performs explicit unregistration; mirror the `HTTPRequest` block.)

- [ ] **Step 4: Ensure the sources are compiled.** Confirm `scene/main/http_server.*` is picked up by `scene/main/SCsub` glob (most `SCsub` files glob `*.cpp`); if not, add the file explicitly.

- [ ] **Step 5: Build and run a quick lookup check via doctest.** Add a temporary doctest (or extend Task 5) asserting:

```cpp
TEST_CASE("[ClassDBNamespace] HTTPServer pilot is namespaced") {
	StringName cls;
	CHECK(ClassDB::class_get_by_qualified_name("foundry.http.server.HTTPServer", cls));
	CHECK(cls == StringName("HTTPServer"));
	CHECK_FALSE(ClassDB::class_exists("HTTPServer")); // namespace-only, no alias
}
```

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*HTTPServer*"`
Expected: PASS.

- [ ] **Step 6: Commit.**

```bash
git add scene/main/http_server.h scene/main/http_server.cpp scene/register_scene_types.cpp
git commit -m "feat(scene): Add HTTPServer pilot class under foundry.http.server"
```

---

### Task 7: FS — exclude namespaced natives from flat globals + qualified FSNativeClass map

**Goal:** Stop exposing namespaced natives as bare globals, and hold them in a qualified-name map for the compiler/analyzer to resolve.

**Files:**
- Modify: `modules/foundry_script/foundry_script.h` (`FSLanguage`: add `HashMap<StringName, Ref<FSNativeClass>> native_class_by_qualified;` + accessor)
- Modify: `modules/foundry_script/foundry_script.cpp` (`FSLanguage::init` loop ~line 3571-3579)

**Acceptance Criteria:**
- [ ] In `FSLanguage::init`, classes with a non-empty namespace are NOT added to the flat global table (`_add_global`).
- [ ] Every native class (namespaced or not) is wrapped in an `FSNativeClass` and stored in `native_class_by_qualified` keyed by its qualified name.
- [ ] Empty-namespace classes keep their existing bare-global registration unchanged.

**Verify:** `python3 scripts/agent_build.py --backend ninja --test --case "*FoundryScript*"` → existing FS tests still pass (no native class became a global).

**Steps:**

- [ ] **Step 1: Add the map + accessor to FSLanguage.** In `modules/foundry_script/foundry_script.h`, on `FSLanguage`:

```cpp
	HashMap<StringName, Ref<FSNativeClass>> native_class_by_qualified;
	Ref<FSNativeClass> get_native_class_by_qualified(const StringName &p_qualified) const;
```

Implement the accessor in the `.cpp`:

```cpp
Ref<FSNativeClass> FSLanguage::get_native_class_by_qualified(const StringName &p_qualified) const {
	const Ref<FSNativeClass> *nc = native_class_by_qualified.getptr(p_qualified);
	return nc ? *nc : Ref<FSNativeClass>();
}
```

- [ ] **Step 2: Split the init loop.** In `FSLanguage::init` (~line 3571-3579), change the per-class loop:

```cpp
	ClassDB::get_class_list(class_list);
	for (const StringName &class_name : class_list) {
		if (!ClassDB::is_class_exposed(class_name)) {
			continue;
		}
		StringName qualified = ClassDB::class_get_qualified_name(class_name);
		StringName ns = ClassDB::class_get_namespace(class_name);
		Ref<FSNativeClass> nc = memnew(FSNativeClass(class_name));
		native_class_by_qualified.insert(qualified, nc);
		// Only flat (empty-namespace) natives are global-by-default.
		if (ns == StringName()) {
			_add_global(class_name, nc);
		}
	}
```

(Verify the exact existing loop structure — `class_name` here is the map key, which is the qualified name for namespaced classes. `FSNativeClass(class_name)` takes the simple name; pass `ClassDB`... confirm `FSNativeClass`'s constructor wants the simple or qualified name. The constructor `FSNativeClass(const StringName &p_name)` at foundry_script.cpp:82 stores the name used for ClassDB lookups; for namespaced classes pass the qualified name so internal `ClassDB::get_method`/`instantiate` calls resolve correctly. Adjust: `memnew(FSNativeClass(qualified))` and keep `_add_global` keyed by the name scripts use — see Task 8 for how the analyzer references it.)

- [ ] **Step 3: Build and run the FS suite to confirm no regression.**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*FoundryScript*"`
Expected: existing FS tests pass (the only native removed from globals is the pilot `HTTPServer`, which no existing script references).

- [ ] **Step 4: Commit.**

```bash
git add modules/foundry_script/foundry_script.h modules/foundry_script/foundry_script.cpp
git commit -m "feat(foundry_script): Exclude namespaced natives from globals; add qualified map"
```

---

### Task 8: FS analyzer — consult ClassDB in resolution helpers + qualified identity

**Goal:** Make namespaced natives resolve through the existing `import`/namespace machinery by having the analyzer consult ClassDB alongside `ScriptServer`, and carry the qualified name as the native type identity.

**Files:**
- Modify: `modules/foundry_script/fs_analyzer.cpp` (`get_global_class_in_namespace` ~7997, `get_imported_global_class` ~8011, `get_namespace_global_class_from_type_chain` ~8040, `reduce_identifier` ~10621, `resolve_datatype` ~1973/2210)

**Acceptance Criteria:**
- [ ] After the `ScriptServer` miss, the resolution helpers call `ClassDB::class_get_in_namespace`; on hit they return the qualified name tagged as native.
- [ ] `reduce_identifier` and `resolve_datatype` produce a `NATIVE_CLASS`/`NATIVE` datatype whose `native_type` is the qualified name for namespaced natives.
- [ ] A bare reference to a namespaced native without import still fails (`class_exists` unchanged).

**Verify:** `python3 scripts/agent_build.py --backend ninja --test --case "*FoundryScript*"` → builds; behavioral fixtures in Task 11.

**Steps:**

- [ ] **Step 1: Extend `get_global_class_in_namespace`.** After the existing `ScriptServer::is_global_class` check fails, add a ClassDB fallback (~line 7997):

```cpp
	// Script-defined globals first (unchanged).
	if (ScriptServer::is_global_class(composed)) { /* existing body */ }
	// Then native namespaced classes.
	StringName native_qualified = ClassDB::class_get_in_namespace(p_namespace, p_class_name);
	if (native_qualified != StringName()) {
		r_global_class_name = native_qualified;
		r_is_native = true; // new out-param or sentinel — see note
		return true;
	}
```

If the helper's signature cannot carry "is_native", return the qualified name and have callers detect nativeness via `ClassDB::class_get_by_qualified_name`; choose the smaller change and keep the existing return contract. Add an `r_is_native` out-param only if callers need to distinguish (they do, to set `NATIVE_CLASS` vs `GLOBAL_CLASS` source) — add it and update call sites.

- [ ] **Step 2: Extend `get_imported_global_class`.** The existing loop already calls `get_global_class_in_namespace` per imported namespace, so the ClassDB fallback from Step 1 makes `import foundry.http.server` + bare `HTTPServer` resolve. Keep its ambiguity reporting unchanged (two native namespaces with the same class name still error).

- [ ] **Step 3: Extend `get_namespace_global_class_from_type_chain`.** For a dotted chain like `foundry.http.server.HTTPServer`, the existing longest-prefix logic walks `ScriptServer`; add a ClassDB pass that tries `ClassDB::class_get_in_namespace` for each prefix split.

- [ ] **Step 4: Set qualified identity in reduction.** In `reduce_identifier` (~line 10621), when a name resolves to a native via the import/namespace path, set:

```cpp
	p_identifier->source = FSParser::IdentifierNode::NATIVE_CLASS;
	p_identifier->set_datatype(make_native_meta_type(resolved_qualified_name));
```

In `resolve_datatype` (~line 2210), set `result.native_type = resolved_qualified_name` for namespaced natives. (`make_native_meta_type` at ~1616 already takes a name; passing the qualified name is correct since the qualified name is the canonical ClassDB key.)

**Verify:** the analyzer never calls the old bare-`class_exists` path for namespaced natives; they only resolve via import/chain.

- [ ] **Step 5: Build.**

Run: `python3 scripts/agent_build.py --backend ninja`
Expected: builds cleanly.

- [ ] **Step 6: Commit.**

```bash
git add modules/foundry_script/fs_analyzer.cpp
git commit -m "feat(foundry_script): Resolve namespaced natives via ClassDB in import/chain"
```

---

### Task 9: FS compiler — resolve qualified native_type from the qualified map

**Goal:** When a script's base type or a native reference carries a qualified `native_type`, the compiler pulls the right `FSNativeClass` from the qualified map.

**Files:**
- Modify: `modules/foundry_script/fs_compiler.cpp` (~line 4790-4803)

**Acceptance Criteria:**
- [ ] For a qualified `native_type`, the compiler resolves the `FSNativeClass` via `FSLanguage::get_native_class_by_qualified` (falling back to the existing global-map lookup for bare names).
- [ ] `extends HTTPServer` (after `import foundry.http.server`) compiles and sets the script's native base correctly.

**Verify:** `python3 scripts/agent_build.py --backend ninja --test --case "*FoundryScript*"` → builds; end-to-end fixture in Task 11.

**Steps:**

- [ ] **Step 1: Resolve the native base.** At ~line 4790-4803 where the compiler currently does `FSLanguage::get_singleton()->get_global_map()[base_type.native_type]`, wrap it to try the qualified map first:

```cpp
	StringName native_name = base_type.native_type;
	Ref<FSNativeClass> native_nc = FSLanguage::get_singleton()->get_native_class_by_qualified(native_name);
	if (native_nc.is_null()) {
		// Flat native: resolve through the existing global map.
		int native_idx = FSLanguage::get_singleton()->get_global_map()[native_name];
		native_nc = FSLanguage::get_singleton()->get_global_array()[native_idx];
	}
	p_script->native = native_nc;
```

(Preserve all existing surrounding logic — `_gdtype_from_datatype`, clears, etc. Only the lookup source changes.)

- [ ] **Step 2: Build.**

Run: `python3 scripts/agent_build.py --backend ninja`
Expected: builds cleanly.

- [ ] **Step 3: Commit.**

```bash
git add modules/foundry_script/fs_compiler.cpp
git commit -m "feat(foundry_script): Resolve qualified native base type from qualified map"
```

---

### Task 10: FS completion + GRAMMAR.md reachability note

**Goal:** Make namespaced natives discoverable in completion and document the reachability rule.

**Files:**
- Modify: `modules/foundry_script/fs_editor.cpp` (`_list_importable_namespaces` ~1465; type-name completion ~1768-1777)
- Modify: `modules/foundry_script/GRAMMAR.md` (§4.2 reachability)

**Acceptance Criteria:**
- [ ] `_list_importable_namespaces` includes native namespaces derived from ClassDB (e.g. `foundry.http.server`).
- [ ] After `import foundry.http.server`, member completion offers `HTTPServer`.
- [ ] `GRAMMAR.md` §4.2 states native classes are reachable under the same conditions as script classes, with native namespaces sourced from ClassDB. No token/keyword changes.

**Verify:** `python3 scripts/agent_build.py --backend ninja` → builds. (Completion exercised manually/fixture in Task 11/16.)

**Steps:**

- [ ] **Step 1: Add native namespaces to importable completion.** In `_list_importable_namespaces`, enumerate the distinct namespaces from `ClassDB::get_class_list` (`class_get_namespace`) and merge them with the script namespaces.

- [ ] **Step 2: Add namespace members to member completion.** In the type-completion path, when completing members of an imported native namespace, enumerate classes with that namespace via ClassDB and offer their simple names.

- [ ] **Step 3: Update the grammar.** In `modules/foundry_script/GRAMMAR.md` §4.2 (reachability rules), append:

```markdown
A native (ClassDB) class is reachable under the same conditions as a script
class: if it is global (empty namespace), in the same file/scope, in the same
named namespace, or reachable via an `import` of its namespace. Native
namespaces are sourced from ClassDB, not ScriptServer. A namespaced native is
not reachable by its bare name unless an explicit global alias is registered.
No new tokens or keywords are introduced by native namespacing.
```

- [ ] **Step 4: Build.**

Run: `python3 scripts/agent_build.py --backend ninja`
Expected: builds cleanly.

- [ ] **Step 5: Commit.**

```bash
git add modules/foundry_script/fs_editor.cpp modules/foundry_script/GRAMMAR.md
git commit -m "feat(foundry_script): Native namespace completion + GRAMMAR reachability note"
```

---

### Task 11: Foundry Script fixtures

**Goal:** Prove the script side end-to-end with checked-in fixtures and expected outputs.

**Files:**
- Create: `modules/foundry_script/tests/scripts/namespaced_native/import_extends.fs`
- Create: `modules/foundry_script/tests/scripts/namespaced_native/import_extends.out` (expected)
- Create: `modules/foundry_script/tests/scripts/namespaced_native/qualified_chain.fs` + `.out`
- Create: `modules/foundry_script/tests/scripts/namespaced_native/bare_without_import.fs` + `.out` (negative: resolution error)

**Acceptance Criteria:**
- [ ] `import_extends.fs`: `import foundry.http.server` then `extends HTTPServer`, instantiates, calls `start()` — analyzes and runs cleanly.
- [ ] `qualified_chain.fs`: uses `foundry.http.server.HTTPServer` directly — resolves.
- [ ] `bare_without_import.fs`: references `HTTPServer` with no import — produces a resolution error in the expected `.out`.

**Verify:** `./bin/foundry.* --headless test run --case "*namespaced_native*" --force-colors` → fixtures pass; regenerate with `test generate-fixtures modules/foundry_script/tests/scripts` if expected outputs need updating after the behavior lands.

**Steps:**

- [ ] **Step 1: Write `import_extends.fs`.**

```foundry
import foundry.http.server

extends HTTPServer

func _init() -> void:
    var server := HTTPServer.new()
    server.port = 9000
    server.start()
```

- [ ] **Step 2: Write `qualified_chain.fs`.**

```foundry
extends Node

func _ready() -> void:
    var server := foundry.http.server.HTTPServer.new()
    print(server.get_port())
```

- [ ] **Step 3: Write `bare_without_import.fs` (negative).**

```foundry
extends Node

func _ready() -> void:
    var server := HTTPServer.new()
```

- [ ] **Step 4: Generate the expected outputs.**

Run: `./bin/foundry.* --headless test generate-fixtures modules/foundry_script/tests/scripts`
Expected: `.out` files created; the negative fixture's `.out` contains a resolution error for `HTTPServer`.

- [ ] **Step 5: Run the fixtures.**

Run: `./bin/foundry.* --headless test run --case "*namespaced_native*" --force-colors`
Expected: import/qualified fixtures pass; the negative fixture matches its expected error output.

- [ ] **Step 6: Commit.**

```bash
git add modules/foundry_script/tests/scripts/namespaced_native
git commit -m "test(foundry_script): Namespaced native import/qualified/bare fixtures"
```

---

### Task 12: Scene — qualified type on disk + resolve_type_name at the choke point + MissingNode

**Goal:** Make scenes store and load namespaced native types correctly.

**Files:**
- Modify: `scene/resources/packed_scene.cpp` (instantiation choke point ~line 316; pack path `_parse_node` ~line 793; `MissingNode` ~line 327)

**Acceptance Criteria:**
- [ ] The packer writes the class's `qualified_name` for namespaced nodes (bare name for flat).
- [ ] At `ClassDB::instantiate(snames[n.type])` (~line 316), the name is resolved via `ClassDB::resolve_type_name` first.
- [ ] An unresolved/ambiguous type produces a `MissingNode` whose `original_class` is the qualified string.

**Verify:** `python3 scripts/agent_build.py --backend ninja --test --case "*Scene*" --case "*PackedScene*"` → existing scene tests pass; end-to-end scene fixture in Task 16.

**Steps:**

- [ ] **Step 1: Write the qualified type at pack time.** In `_parse_node` (~line 793), where the node's class name is added to the names table, store the `qualified_name` when the class has a namespace:

```cpp
	StringName stored_type = obj->get_class(); // get_class() returns the qualified name for namespaced classes (Task 13)
	// For flat classes get_class() already returns the simple name == canonical key.
	int type_idx = add_name(stored_type);
```

(Task 13 makes `get_class()` return the qualified name; if Task 13 has not landed yet, use `ClassDB::class_get_qualified_name(obj->get_class())` here instead to avoid ordering dependence.)

- [ ] **Step 2: Resolve at load time.** At the instantiation choke point (~line 316), change:

```cpp
	Object *obj = ClassDB::instantiate(snames[n.type]);
```

to resolve first:

```cpp
	StringName resolved = ClassDB::resolve_type_name(snames[n.type]);
	Object *obj = ClassDB::instantiate(resolved != StringName() ? resolved : snames[n.type]);
```

- [ ] **Step 3: Preserve the qualified name on MissingNode.** At ~line 327, the existing `MissingNode` path calls `set_original_class(snames[n.type])`. Since `snames[n.type]` is now the qualified string for namespaced types, it is preserved automatically. Verify no code strips it to a bare name between read and `set_original_class`.

- [ ] **Step 4: Build and run scene tests.**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*PackedScene*"`
Expected: existing scene tests pass.

- [ ] **Step 5: Commit.**

```bash
git add scene/resources/packed_scene.cpp scene/resources/packed_scene.h
git commit -m "feat(scene): Resolve namespaced types via resolve_type_name at instantiation"
```

---

### Task 13: Runtime identity policy — get_class() returns qualified name

**Goal:** Make the runtime identity of a namespaced native be its qualified name, with inheritance matching intact.

**Files:**
- Modify: `core/object/object.h` / `core/object/object.cpp` (`get_class`, `get_class_ptr`, `is_class`) — or wherever the ClassInfo name is surfaced
- Verify against: `core/object/class_db.cpp` (`is_parent_class`, `inherits_ptr` chain)

**Acceptance Criteria:**
- [ ] `memnew(HTTPServer)->get_class()` returns `foundry.http.server.HTTPServer`.
- [ ] `instance->is_class("Node")` and `instance->is_class("foundry.http.server.HTTPServer")` are both true.
- [ ] Flat classes are unchanged: `get_class()` on a `Node` still returns `Node`.

**Verify:** add a doctest assertion (extend Task 5/6 test) and `python3 scripts/agent_build.py --backend ninja --test --case "*ClassDBNamespace*"`.

**Steps:**

- [ ] **Step 1: Determine where `get_class()` resolves.** `Object::get_class()` typically returns the `ClassInfo::name` (the simple name) via the `_get_class_name` virtual / `_class_name` static. To return the qualified name for namespaced classes without disturbing the C++ type identity, override at the ClassDB lookup layer: provide `get_class()` that returns `ClassDB::class_get_qualified_name(_get_class_name())`. Locate the exact implementation (grep `String Object::get_class()`) and adjust it to look up the qualified name from ClassDB.

```cpp
String Object::get_class() const {
	// Returns the canonical (qualified) name; for flat classes this equals the simple name.
	return ClassDB::class_get_qualified_name(_get_class_name_static_for_this());
}
```

(Confirm the exact internal accessor used; preserve the `is_class_ptr` fast path that compares `class_ptr` — that path is unaffected since it compares pointers, not names.)

- [ ] **Step 2: Ensure `is_class` works for both qualified and inherited names.** `is_class` walks the `inherits_ptr` chain comparing names. Since the parent chain stores simple names (`Node`), and the qualified class's `is_class("foundry.http.server.HTTPServer")` compares against its own qualified identity, ensure `is_class` compares against `ClassInfo::qualified_name` for the class itself and `ClassInfo::name`/qualified for ancestors. Flat ancestors are keyed by simple==qualified, so they match either form. Adjust `is_class` (in class_db.cpp) to compare against the resolved qualified name of each link:

```cpp
bool ClassDB::is_parent_class(const StringName &p_class, const StringName &p_inherits) {
	// existing chain walk, but compare each link's qualified_name as well as name
}
```

(Keep the existing behavior for flat classes — they match on simple name which equals qualified. Add qualified comparison so a namespaced class's `is_class("foundry.http.server.HTTPServer")` resolves.)

- [ ] **Step 3: Add a doctest assertion.** Extend the Task 6 pilot test:

```cpp
TEST_CASE("[ClassDBNamespace] HTTPServer runtime identity") {
	Object *srv = ClassDB::instantiate("foundry.http.server.HTTPServer");
	CHECK(srv != nullptr);
	CHECK(srv->get_class() == StringName("foundry.http.server.HTTPServer"));
	CHECK(srv->is_class("Node") == true);
	CHECK(srv->is_class("foundry.http.server.HTTPServer") == true);
	memdelete(srv);
}
```

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*ClassDBNamespace*"`
Expected: PASS.

- [ ] **Step 4: Commit.**

```bash
git add core/object/object.cpp core/object/class_db.cpp tests/test_class_db_namespace.h
git commit -m "feat(core): get_class() returns qualified name; is_class resolves qualified+inherited"
```

---

### Task 14: Editor — Create Node dialog namespace grouping + qualified instantiation

**Goal:** Let users add a namespaced native node through the Create Node dialog.

**Files:**
- Modify: `editor/gui/create_dialog.cpp` (`_fill_type_list` ~line 83; confirm/instantiate ~line 672)

**Acceptance Criteria:**
- [ ] Namespaced classes appear in the dialog grouped under a namespace header (not flattened).
- [ ] Selecting and confirming creates the node and writes the qualified type into the scene.
- [ ] Filtering still works.

**Verify:** `python3 scripts/agent_build.py --backend ninja` → builds; manual editor verification via automation in Task 16.

**Steps:**

- [ ] **Step 1: Group namespaced classes in `_fill_type_list`.** Namespaced classes already appear via `ClassDB::get_class_list` (under their qualified keys). In `_fill_type_list`, split each qualified name into namespace + simple name; place namespaced classes under a tree header node for their namespace rather than at the flat root. Keep flat classes unchanged.

- [ ] **Step 2: Instantiate by qualified name.** At the confirm path (~line 672), `ClassDB::instantiate(type_name)` already accepts the qualified name (it's the canonical key). Ensure the `type_name` stored is the qualified form when the class is namespaced.

- [ ] **Step 3: Build.**

Run: `python3 scripts/agent_build.py --backend ninja`
Expected: builds cleanly.

- [ ] **Step 4: Commit.**

```bash
git add editor/gui/create_dialog.cpp
git commit -m "feat(editor): Group namespaced native classes in Create Node dialog"
```

---

### Task 15: Docs — namespace attribute + HTTPServer doc page

**Goal:** Document the namespace on the class reference and make help resolve qualified names.

**Files:**
- Modify: `doc/class.xsd` (add optional `namespace` attribute on `<class>`)
- Modify: `doc/classes/HTTPServer.xml` (or the doc generation path) — ensure `<class name="HTTPServer" namespace="foundry.http.server" inherits="Node">`
- Modify: the editor help lookup that resolves a class name to its doc page (locate via grep of the doc-class lookup; typically `EditorHelp` / `DocData`)

**Acceptance Criteria:**
- [ ] `doc/class.xsd` permits a `namespace` attribute on `<class>`.
- [ ] `HTTPServer`'s doc page carries `namespace="foundry.http.server"`.
- [ ] F1 / search on `foundry.http.server.HTTPServer` opens the page.

**Verify:** `pre-commit run xsd` (or the doc/xsd hook) and `python3 scripts/agent_build.py --backend ninja` → builds; manual help lookup in Task 16.

**Steps:**

- [ ] **Step 1: Add the attribute to the schema.** In `doc/class.xsd`, on the `<class>` element's attribute list, add:

```xml
<xs:attribute name="namespace" type="xs:string"/>
```

- [ ] **Step 2: Author the doc page.** Create `doc/classes/HTTPServer.xml`:

```xml
<class name="HTTPServer" namespace="foundry.http.server" inherits="Node" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="../class.xsd">
	<brief_description>
		A minimal HTTP server node (pilot for namespaced native classes).
	</brief_description>
	<description>
		HTTPServer lives in the [code]foundry.http.server[/code] namespace. Import it in Foundry Script with [code]import foundry.http.server[/code]. This is a pilot class; full HTTP semantics are not yet implemented.
	</description>
	<members>
		<member name="port" type="int" setter="set_port" getter="get_port" default="8080">
			The TCP port the server listens on.
		</member>
	</members>
	<methods>
		<method name="start">
			<return type="bool" />
			<description>
				Starts listening on [member port]. Returns [code]true[/code].
			</description>
		</method>
		<method name="stop">
			<return type="void" />
			<description>
				Stops the server.
			</description>
		</method>
	</methods>
</class>
```

- [ ] **Step 3: Make help resolve qualified names.** Locate the doc lookup (grep for where `<class name=` is matched against a queried name in `editor/doc_tools.*` / `EditorHelp`). Add a fallback: if a direct name match misses, try `class_get_by_qualified_name` / split the qualified name and match on namespace+name.

- [ ] **Step 4: Build and run the doc/xsd pre-commit hook.**

Run: `pre-commit run --all-files` (doc/xsd stages) and `python3 scripts/agent_build.py --backend ninja`
Expected: schema valid; builds cleanly.

- [ ] **Step 5: Commit.**

```bash
git add doc/class.xsd doc/classes/HTTPServer.xml
git commit -m "docs: Add namespace attribute + HTTPServer doc page"
```

---

### Task 16: End-to-end integration + final validation

**Goal:** Prove the whole pipeline works together and run the required strict validation build.

**Files:**
- Create: a test scene fixture under a test scratch project, or a doctest that builds a `PackedScene` with a namespaced node in memory
- All files from Tasks 1–15

**Acceptance Criteria:**
- [ ] A node of type `foundry.http.server.HTTPServer` loads from a scene, instantiates, and `get_class()` returns the qualified name.
- [ ] A scene referencing an unknown qualified type yields a `MissingNode` preserving the qualified string.
- [ ] Editor automation (Create Node dialog) can add an `HTTPServer` node (covered if a display is available; otherwise assert via the dialog population).
- [ ] The strict validation build passes: `python3 scripts/agent_build.py`.

**Verify:** `python3 scripts/agent_build.py` (full strict build + tests) → `[doctest] Status: SUCCESS!`.

**Steps:**

- [ ] **Step 1: Add an in-memory scene round-trip doctest.** In `tests/test_class_db_namespace.h` (or a new `tests/test_namespaced_scene.h` wired into `test_main.cpp`):

```cpp
TEST_CASE("[NamespacedNative] Scene round-trips a namespaced node") {
	// Build a node, pack it, instantiate, and check identity.
	HTTPServer *srv = memnew(HTTPServer);
	srv->set_name("Srv");
	PackedScene ps;
	Error err = ps.pack(srv);
	CHECK(err == OK);
	memdelete(srv);

	Node *instance = ps.instantiate(PackedScene::GEN_EDIT_STATE_INSTANCE);
	CHECK(instance != nullptr);
	CHECK(instance->get_class() == StringName("foundry.http.server.HTTPServer"));
	CHECK(instance->is_class("Node") == true);
	memdelete(instance);
}
```

- [ ] **Step 2: Run the focused integration tests.**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*NamespacedNative*" --case "*ClassDBNamespace*" --case "*namespaced_native*"`
Expected: all pass.

- [ ] **Step 3: Run the full strict validation build.**

Run: `python3 scripts/agent_build.py`
Expected: builds with `dev_mode=yes dev_build=yes tests=yes` and the full doctest suite reports SUCCESS.

- [ ] **Step 4: Commit.**

```bash
git add tests/
git commit -m "test: Namespaced native end-to-end scene round-trip"
```

---

## Self-Review

**Spec coverage:**
- §1 ClassDB identity model (fields, companion macro, qualified keying, alias map, accessors) → Tasks 1–4; behavioral test → Task 5. ✓
- §2 Foundry Script (exclude from globals, qualified map, 3 resolution helpers, qualified identity, completion, grammar) → Tasks 7–10. ✓
- §3 Scene (no format change, qualified on disk, `resolve_type_name` choke point, MissingNode) → Task 12. ✓
- §4 Docs/editor/runtime identity (`get_class()` qualified, `is_class`, inspector, doc namespace attr, help) → Tasks 13–15. ✓
- §5 Pilot class, testing, rollout → Tasks 6, 11, 16. ✓

**Placeholder scan:** No TBD/TODO. Where exact line numbers may have drifted, tasks say "verify against" the grounded location and to mirror the adjacent `HTTPRequest` block — that is a verification instruction, not a placeholder. The two spots that depend on confirming existing signatures (FSNativeClass constructor simple-vs-qualified name in Task 7; `Object::get_class` exact accessor in Task 13) carry an explicit instruction to confirm and adapt, because those signatures must be read at implementation time rather than guessed.

**Type consistency:** `qualified_name`, `namespace_path`, `register_namespace`, `class_register_global_alias`, `_resolve_by_any_name`, `resolve_type_name`, `class_get_in_namespace`, `class_get_by_qualified_name`, `class_get_qualified_name`, `class_get_namespace`, `native_class_by_qualified`, `get_native_class_by_qualified` — all used consistently across tasks.

**Known follow-ups (not in this plan, by design):** namespaced class inheriting another namespaced class (qualified `inherits`), cross-native same-name coexistence at the language level with two real natives, migration of existing flat classes, full HTTP semantics, editor inheritance-tree-by-namespace.
