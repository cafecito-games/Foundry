/**************************************************************************/
/*  foundry_script.h                                                      */
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

#include "fs_function.h"

#include "core/debugger/engine_debugger.h"
#include "core/debugger/script_debugger.h"
#include "core/doc_data.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/script_language.h"
#include "core/templates/rb_set.h"

class FoundryScript;
class FSParser;
class FSAnalyzer;

class FSNativeClass : public RefCounted {
	FOUNDRY_CLASS(FSNativeClass, RefCounted);

	StringName name;

protected:
	bool _get(const StringName &p_name, Variant &r_ret) const;
	static void _bind_methods();

public:
	_FORCE_INLINE_ const StringName &get_name() const { return name; }
	Variant _new();
	Object *instantiate();
	virtual Variant callp(const StringName &p_method, const Variant **p_args, int p_argcount, Callable::CallError &r_error) override;
	FSNativeClass(const StringName &p_name);
};

// Read-only descriptor for a single generic type parameter declared on a class, returned by
// `FoundryScript.get_type_parameter_list()`. Each instance is an immutable snapshot of the reflected data.
class FSTypeParameter : public RefCounted {
	FOUNDRY_CLASS(FSTypeParameter, RefCounted);

	friend class FoundryScript;

	StringName name;
	int index = -1;
	StringName scope = StringName("class");
	bool _has_bound = false;
	Dictionary bound;

protected:
	static void _bind_methods();

public:
	StringName get_parameter_name() const { return name; }
	int get_index() const { return index; }
	StringName get_scope() const { return scope; }
	bool is_bounded() const { return _has_bound; }
	Dictionary get_bound() const { return bound; }
};

class FSSpecializedClassHandle : public RefCounted {
	FOUNDRY_CLASS(FSSpecializedClassHandle, RefCounted);

	Ref<FoundryScript> script;
	Vector<ContainerType> type_arguments;

protected:
	bool _get(const StringName &p_name, Variant &r_ret) const;
	static void _bind_methods();

public:
	const Ref<FoundryScript> &get_specialized_script() const { return script; }
	const Vector<ContainerType> &get_type_arguments() const { return type_arguments; }
	String get_type_name() const;
	bool is_assignable_to_native_type(const StringName &p_native_type) const;
	bool _equals(const Variant &p_other) const;
	int64_t _hash_code() const;
	virtual Variant callp(const StringName &p_method, const Variant **p_args, int p_argcount,
			Callable::CallError &r_error) override;

	static Ref<FSSpecializedClassHandle> create(const Ref<FoundryScript> &p_script,
			const Vector<ContainerType> &p_type_arguments);
};

#ifdef TESTS_ENABLED
namespace FSTests {
class TestFSTraitReflectionAccessor;
class TestFSGenericReflectionAccessor;
class TestFSLanguageGlobalsAccessor;
class TestFSBytecodeScriptAccessor;
} //namespace FSTests
#endif // TESTS_ENABLED

class FoundryScript : public Script {
	FOUNDRY_CLASS(FoundryScript, Script);
	friend class FSSpecializedClassHandle;
	bool tool = false;
	bool valid = false;
	bool reloading = false;
	// True when this script graph was reconstructed from serialized compiled bytecode (`.fsb`)
	// instead of compiled from source; such a script has no source to re-parse.
	bool compiled_binary = false;
	bool _is_abstract = false;
	bool _is_final = false;
	bool _is_trait_type = false;
	StringName trait_type_name;

	// How a class-type-parameter member resolves its reified type argument, precomputed at compile time
	// by mapping the member's declaring-class parameter down the `extends`/specialization chain to the
	// leaf class (see docs/superpowers/specs/2026-06-25-reified-type-argument-bindings-design.md).
	struct TypeArgumentBinding {
		enum Kind {
			NONE, // Not a type-parameter member.
			FIXED, // Bound to a concrete argument by an `extends Base[int]` specialization in the chain.
			OPEN, // Still open at the leaf; resolved against the instance's reified `type_arguments`.
		};
		Kind kind = NONE;
		// Valid when kind == FIXED. Stored as a FSDataType (not a baked ContainerType) so it follows
		// the same local-class handling as member `data_type` — a local class is held by raw pointer, not a
		// strong Ref — avoiding reference cycles (e.g. CRTP `class Node extends Box[Node]`). A temporary
		// ContainerType is materialized only at validation time.
		FSDataType fixed;
		// Valid when kind == FIXED: the fixing argument still references an open type parameter
		// (`extends Box[Array[T]]`), so `fixed` is the analyzer-erased container and does not carry the
		// argument's concrete reification. The leaf-to-base projection treats such a slot as carrying no
		// invariance evidence (it cannot validate the dependent argument soundly), keeping the existing
		// gradual acceptance instead of a spurious rejection.
		bool fixed_is_dependent = false;
		// The member is `Type[T]`, so resolved writes must validate class handles for the reified `T`
		// instead of instances of `T`.
		bool is_type_handle = false;
		int leaf_ordinal = -1; // Index into the leaf instance's `type_arguments`; valid when kind == OPEN.
	};

	struct MemberInfo {
		int index = 0;
		StringName setter;
		StringName getter;
		FSDataType data_type;
		PropertyInfo property_info;
		// When this member is typed as a class generic parameter (e.g. `value: T` in `class Box[T]`),
		// how its reified type argument resolves for instances of the owning (leaf) class. `NONE` for
		// ordinary, non-parameter members. Re-resolved through the `extends` chain when a subclass
		// inherits the member, so a base member fixed by `extends Base[int]` validates as `int`.
		TypeArgumentBinding type_argument_binding;
	};

public:
	// A generic type parameter declared on this class, e.g. `T` in `class Box[T]` or
	// `K`/`V` in `class_name Pair[K, V: RefCounted]`. Surfaced through runtime reflection.
	struct TypeParameter {
		StringName name;
		int index = -1;
		bool has_bound = false;
		PropertyInfo bound; // Upper bound type; only meaningful when `has_bound` is true.
	};

	// An abstract method requirement a class inherits through the traits it (transitively)
	// `uses` but does not flatten into its own `member_functions`. Surfaced so a dynamic
	// proxy of the type can intercept the method (`return_type` drives return coercion) and
	// enumerate it (`method_info`).
	struct AbstractTraitRequirement {
		FSDataType return_type;
		MethodInfo method_info;
	};

	// Resolved passive annotation metadata persisted by the compiler. Covers both user-declared custom
	// annotations and Godot's built-in annotations (`@export`, `@rpc`, `@onready`, `@tool`, ...), with
	// `is_builtin` discriminating the two. The data is runtime-safe (no AST pointers): argument values
	// are copied as plain Variants. Reading it is a reflection operation and adds no per-call execution
	// cost.
	struct AnnotationUsage {
		StringName name; // Short name, without "@": "timeout", "export_range".
		StringName qualified_name; // Canonical declaration identity: "cafecito.test.timeout"; the bare name for built-ins.
		Array args; // Positional argument values in source order.
		Dictionary kwargs; // Named argument values keyed by parameter name. Always empty for built-ins (positional only).
		bool is_builtin = false; // True for a Godot built-in annotation; false for a user-declared custom annotation.
	};

private:
	struct ClearData {
		RBSet<FSFunction *> functions;
		RBSet<Ref<Script>> scripts;
		void clear() {
			functions.clear();
			scripts.clear();
		}
	};

	friend class FSInstance;
	friend class FSFunction;
	friend class FSAnalyzer;
	friend class FSCompiler;
	friend class FSBytecodeExporter;
	friend class FSBytecodeLoader;
	friend class FSDocGen;
	friend class FSLambdaCallable;
	friend class FSLambdaSelfCallable;
	friend class FSLanguage;
	friend struct FSUtilityFunctionsDefinitions;
#ifdef TESTS_ENABLED
	friend class FSTests::TestFSTraitReflectionAccessor;
	friend class FSTests::TestFSGenericReflectionAccessor;
	friend class FSTests::TestFSBytecodeScriptAccessor;
#endif // TESTS_ENABLED

	Ref<FSNativeClass> native;
	Ref<FoundryScript> base;
	FoundryScript *_owner = nullptr; //for subclasses

	// Members are just indices to the instantiated script.
	HashMap<StringName, MemberInfo> member_indices; // Includes member info of all base FoundryScript classes.
	HashSet<StringName> members; // Only members of the current class.
	// Type-argument bindings indexed by member slot (parallel to instance `members`), so a direct
	// member-store opcode can resolve a `T`-typed member's reified argument from the leaf script
	// without a name lookup. Populated after `member_indices` is finalized.
	Vector<TypeArgumentBinding> member_type_argument_bindings;
	// How each ancestor class or applied trait's type parameters resolve for instances of this (leaf)
	// class, keyed by the ancestor/trait FoundryScript and indexed by that script's parameter ordinal. Lets
	// `create_proxy[T]` (OPCODE_GET_TYPE_PARAMETER), compiled once in a base, materialize `T`'s bound
	// script for a derived instance whose base was specialized (`Mock extends Base[Greeter]`), and lets
	// runtime Type[GenericTrait[T]] checks project specialized class handles through trait uses.
	// Ancestor keys are raw pointers kept alive by the `base` chain; trait keys are kept alive by the
	// parser/cache lifetime used for resolved trait metadata.
	HashMap<FoundryScript *, Vector<TypeArgumentBinding>> type_parameter_bindings_by_ancestor;

	// Only static variables of the current class.
	HashMap<StringName, MemberInfo> static_variables_indices;
	Vector<Variant> static_variables; // Static variable values.

	HashMap<StringName, Variant> constants;
	HashMap<StringName, FSFunction *> member_functions;
	// Compiled witness functions for retroactive conformances (`extend Target uses Trait: ...`) this
	// script declares. They are NOT this class's own methods (they dispatch on the *target* instance's
	// layout); the script owns them solely for lifetime and frees them on reload/unload. The global
	// `FSConformanceRegistry` borrows these pointers for runtime dispatch.
	Vector<FSFunction *> witness_functions;
	// Strong references to the target scripts the witnesses above were compiled against. A witness's
	// `_script` is a raw pointer to its target; holding the target alive here for as long as the
	// declaring script (and its witnesses) live prevents a dangling script during dispatch. The target
	// never references the declaring script, so this introduces no reference cycle.
	Vector<Ref<Script>> witness_target_scripts;
	// Registry key under which this script's runtime witnesses were registered, so they can be dropped
	// from the registry before the owned `FSFunction`s are freed. Empty when none were registered.
	String registered_conformance_source;
	HashMap<StringName, Ref<FoundryScript>> subclasses;
	HashMap<StringName, MethodInfo> _signals;
	// Direct trait identities recorded for this script. Transitive script-inheritance traits are computed at query time.
	Vector<StringName> script_trait_list;
	// Abstract requirements contributed by the traits this class (transitively) uses but does not
	// flatten into `member_functions` (abstract trait members are contracts, not bodies). Keyed by
	// method name, so a dynamic proxy can intercept every method in `T`'s contract — including
	// requirements inherited through a `uses` chain — coerce the handler's return (`return_type`),
	// and enumerate them (`method_info`). Names also defined as a real `member_function` (concrete or
	// own abstract) are excluded, so a compiled function always takes precedence.
	HashMap<StringName, AbstractTraitRequirement> abstract_trait_requirements;
	// Generic type parameters declared directly on this class (`class Box[T]`). Empty for non-generic classes.
	Vector<TypeParameter> type_parameters;
	Dictionary rpc_config;

	// Passive annotation metadata resolved by the analyzer and persisted by the compiler. Holds both
	// user-declared custom annotations and Godot's built-in annotations (tagged via `is_builtin`), in
	// source order. Class annotations are direct-only; method/variable/signal/constant tables include
	// concrete trait-flattened members.
	Vector<AnnotationUsage> class_annotations;
	HashMap<StringName, Vector<AnnotationUsage>> method_annotations;
	HashMap<StringName, Vector<AnnotationUsage>> variable_annotations;
	HashMap<StringName, Vector<AnnotationUsage>> signal_annotations;
	HashMap<StringName, Vector<AnnotationUsage>> constant_annotations;
	// Parameter annotations keyed by owner declaration name, then parameter name.
	HashMap<StringName, HashMap<StringName, Vector<AnnotationUsage>>> method_parameter_annotations;
	HashMap<StringName, HashMap<StringName, Vector<AnnotationUsage>>> signal_parameter_annotations;

public:
	struct LambdaInfo {
		int capture_count;
		bool use_self;
	};

private:
	HashMap<FSFunction *, LambdaInfo> lambda_info;

public:
	class UpdatableFuncPtr {
		friend class FoundryScript;

		FSFunction *ptr = nullptr;
		FoundryScript *script = nullptr;
		List<UpdatableFuncPtr *>::Element *list_element = nullptr;

	public:
		FSFunction *operator->() const { return ptr; }
		operator FSFunction *() const { return ptr; }

		UpdatableFuncPtr(FSFunction *p_function);
		~UpdatableFuncPtr();
	};

private:
	// List is used here because a ptr to elements are stored, so the memory locations need to be stable
	List<UpdatableFuncPtr *> func_ptrs_to_update;
	Mutex func_ptrs_to_update_mutex;

	void _recurse_replace_function_ptrs(const HashMap<FSFunction *, FSFunction *> &p_replacements) const;

#ifdef TOOLS_ENABLED
	// For static data storage during hot-reloading.
	HashMap<StringName, MemberInfo> old_static_variables_indices;
	Vector<Variant> old_static_variables;
	void _save_old_static_data();
	void _restore_old_static_data();

	HashMap<StringName, int> member_lines;
	HashMap<StringName, Variant> member_default_values;
	List<PropertyInfo> members_cache;
	HashMap<StringName, Variant> member_default_values_cache;
	Ref<FoundryScript> base_cache;
	HashSet<ObjectID> inheriters_cache;
	bool source_changed_cache = false;
	bool placeholder_fallback_enabled = false;
	void _update_exports_values(HashMap<StringName, Variant> &values, List<PropertyInfo> &propnames);

	StringName doc_class_name;
	DocData::ClassDoc doc;
	Vector<DocData::ClassDoc> docs;
	bool docs_generated = false;
	void _add_doc(const DocData::ClassDoc &p_doc);
	void _clear_doc();
	// Generates documentation lazily (only when first requested by an editor surface).
	void _ensure_documentation();
#endif

	FSFunction *initializer = nullptr; // Direct pointer to `new()`/`_init()` member function, faster to locate.

	FSFunction *implicit_initializer = nullptr; // `@implicit_new()` special function.
	FSFunction *implicit_ready = nullptr; // `@implicit_ready()` special function.
	FSFunction *static_initializer = nullptr; // `@static_initializer()` special function.

	Error _static_init();
	void _static_default_init(); // Initialize static variables with default values based on their types.

	// Re-links a bytecode-backed (`.fsb`) script from its compiled binary on disk; the parse-based
	// reload path never applies to such scripts.
	Error _reload_from_compiled_binary();

	RBSet<Object *> instances;
	bool destructing = false;
	bool clearing = false;
	//exported members
	String source;
	Vector<uint8_t> binary_tokens;
	String path;
	bool path_valid = false; // False if using default path.
	StringName local_name; // Inner class identifier or `class_name`.
	StringName global_name; // Qualified `class_name`.
	String fully_qualified_name;
	String simplified_icon_path;
	SelfList<FoundryScript> script_list;

	SelfList<FSFunctionState>::List pending_func_states;

	FSFunction *_super_constructor(FoundryScript *p_script);
	void _super_implicit_constructor(FoundryScript *p_script, FSInstance *p_instance, Callable::CallError &r_error);
	FSInstance *_create_instance(const Variant **p_args, int p_argcount, Object *p_owner, Callable::CallError &r_error, const Vector<ContainerType> *p_type_arguments = nullptr);

	String _get_debug_path() const;

#ifdef TOOLS_ENABLED
	HashSet<PlaceHolderScriptInstance *> placeholders;
	//void _update_placeholder(PlaceHolderScriptInstance *p_placeholder);
	virtual void _placeholder_erased(PlaceHolderScriptInstance *p_placeholder) override;
	// p_reload_parser/p_reload_analyzer: when a caller (reload()) has already parsed and analyzed
	// the current source, they are reused to populate the export cache for this script instead of
	// re-parsing and re-analyzing it. They only apply to this script, not to inheriters.
	void _update_exports_down(bool p_base_exports_changed, FSParser *p_reload_parser = nullptr, FSAnalyzer *p_reload_analyzer = nullptr);
#endif

#ifdef DEBUG_ENABLED
	HashMap<ObjectID, List<Pair<StringName, Variant>>> pending_reload_state;
#endif

	bool _update_exports(bool *r_err = nullptr, bool p_recursive_call = false, PlaceHolderScriptInstance *p_instance_to_update = nullptr, bool p_base_exports_changed = false, FSParser *p_reload_parser = nullptr, FSAnalyzer *p_reload_analyzer = nullptr);

	void _save_orphaned_subclasses();

	void _get_script_property_list(List<PropertyInfo> *r_list, bool p_include_base) const;
	void _get_script_method_list(List<MethodInfo> *r_list, bool p_include_base) const;
	void _get_script_signal_list(List<MethodInfo> *r_list, bool p_include_base) const;
	void _get_script_trait_list(List<StringName> *r_list, HashSet<StringName> &r_seen, bool p_include_base) const;

protected:
	bool _get(const StringName &p_name, Variant &r_ret) const;
	bool _set(const StringName &p_name, const Variant &p_value);
	void _get_property_list(List<PropertyInfo> *p_properties) const;

	Variant callp(const StringName &p_method, const Variant **p_args, int p_argcount, Callable::CallError &r_error) override;

	static void _bind_methods();

public:
	static String debug_get_script_name(const Ref<Script> &p_script);

	static String canonicalize_path(const String &p_path);
	_FORCE_INLINE_ static bool is_canonically_equal_paths(const String &p_path_a, const String &p_path_b) {
		return canonicalize_path(p_path_a) == canonicalize_path(p_path_b);
	}

	_FORCE_INLINE_ StringName get_local_name() const { return local_name; }

	void clear();

	// Cancels all functions of the script that are are waiting to be resumed after using await.
	void cancel_pending_functions(bool warn);

	virtual bool is_valid() const override { return valid; }
	bool is_compiled_binary() const { return compiled_binary; }
	// True while this script is mid-reload/mid-link (set on `reload()` entry, cleared on exit,
	// including on failure). A dependency cycle publishes an invalid-but-error-free shell during
	// this window; callers use this to tell a legitimate mid-cycle shell apart from an invalid
	// script left in the cache by a previously failed load.
	bool is_reloading() const { return reloading; }

	bool inherits_script(const Ref<Script> &p_script) const override;

	FoundryScript *find_class(const String &p_qualified_name);
	bool has_class(const FoundryScript *p_script);
	FoundryScript *get_root_script();
	bool is_root_script() const { return _owner == nullptr; }
	String get_fully_qualified_name() const { return fully_qualified_name; }
	const HashMap<StringName, Ref<FoundryScript>> &get_subclasses() const { return subclasses; }
	const HashMap<StringName, Variant> &get_constants() const { return constants; }
	const HashSet<StringName> &get_members() const { return members; }
	const FSDataType &get_member_type(const StringName &p_member) const {
		CRASH_COND(!member_indices.has(p_member));
		return member_indices[p_member].data_type;
	}
	const Ref<FSNativeClass> &get_native() const { return native; }

	_FORCE_INLINE_ const HashMap<StringName, FSFunction *> &get_member_functions() const { return member_functions; }
	_FORCE_INLINE_ const HashMap<StringName, AbstractTraitRequirement> &get_abstract_trait_requirements() const { return abstract_trait_requirements; }

	// Passive annotation metadata (custom and built-in). Class annotations are direct-only; method/variable tables
	// cover this script's own and concrete trait-flattened members. Inherited (base-chain) annotations
	// are resolved at reflection time, not stored here.
	_FORCE_INLINE_ const Vector<AnnotationUsage> &get_class_annotations() const { return class_annotations; }
	_FORCE_INLINE_ const HashMap<StringName, Vector<AnnotationUsage>> &get_method_annotations() const { return method_annotations; }
	_FORCE_INLINE_ const HashMap<StringName, Vector<AnnotationUsage>> &get_variable_annotations() const { return variable_annotations; }
	_FORCE_INLINE_ const HashMap<StringName, Vector<AnnotationUsage>> &get_signal_annotations() const { return signal_annotations; }
	_FORCE_INLINE_ const HashMap<StringName, Vector<AnnotationUsage>> &get_constant_annotations() const { return constant_annotations; }
	_FORCE_INLINE_ const HashMap<StringName, HashMap<StringName, Vector<AnnotationUsage>>> &get_method_parameter_annotations() const { return method_parameter_annotations; }
	_FORCE_INLINE_ const HashMap<StringName, HashMap<StringName, Vector<AnnotationUsage>>> &get_signal_parameter_annotations() const { return signal_parameter_annotations; }
	_FORCE_INLINE_ const HashMap<StringName, MethodInfo> &get_signals() const { return _signals; }
	_FORCE_INLINE_ const HashMap<FSFunction *, LambdaInfo> &get_lambda_info() const { return lambda_info; }

	_FORCE_INLINE_ const FSFunction *get_implicit_initializer() const { return implicit_initializer; }
	_FORCE_INLINE_ const FSFunction *get_implicit_ready() const { return implicit_ready; }
	_FORCE_INLINE_ const FSFunction *get_static_initializer() const { return static_initializer; }

	virtual bool has_script_signal(const StringName &p_signal) const override;
	virtual void get_script_signal_list(List<MethodInfo> *r_signals) const override;
	virtual bool has_script_trait(const StringName &p_trait) const override;
	virtual void get_script_trait_list(List<StringName> *r_traits) const override;

	// Generic reflection: declared type parameters of this class and their optional bounds.
	const Vector<TypeParameter> &get_type_parameters() const { return type_parameters; }
	bool is_generic() const { return !type_parameters.is_empty(); }
	TypedArray<FSTypeParameter> _get_type_parameter_list() const;

	bool is_tool() const override { return tool; }
	bool is_abstract() const override { return _is_abstract; }
	bool is_final() const { return _is_final; }
	bool is_trait_type() const override { return _is_trait_type; }
	StringName get_trait_type_name() const override { return trait_type_name; }
	bool project_type_arguments_onto_base(const Ref<Script> &p_base, const Vector<ContainerType> &p_leaf_type_arguments, Vector<ContainerType> &r_type_arguments, Vector<bool> &r_argument_bound) const override;
	Ref<FoundryScript> get_base() const;

	const HashMap<StringName, MemberInfo> &debug_get_member_indices() const { return member_indices; }
	const HashMap<StringName, FSFunction *> &debug_get_member_functions() const; //this is debug only
	StringName debug_get_member_by_index(int p_idx) const;
	StringName debug_get_static_var_by_index(int p_idx) const;

	Variant _new(const Variant **p_args, int p_argcount, Callable::CallError &r_error);
	// Like `_new`, but binds reified type arguments (e.g. the `int` in `Box[int].new()`) onto the
	// created instance so generic instances carry their concrete type arguments at runtime.
	Variant _new_specialized(const Variant **p_args, int p_argcount, const Vector<ContainerType> &p_type_arguments, Callable::CallError &r_error);
	virtual bool can_instantiate() const override;

	virtual Ref<Script> get_base_script() const override;
	virtual StringName get_global_name() const override;

	virtual StringName get_instance_base_type() const override; // this may not work in all scripts, will return empty if so
	virtual ScriptInstance *instance_create(Object *p_this) override;
	virtual PlaceHolderScriptInstance *placeholder_instance_create(Object *p_this) override;
	virtual bool instance_has(const Object *p_this) const override;

	virtual bool has_source_code() const override;
	virtual String get_source_code() const override;
	virtual void set_source_code(const String &p_code) override;
	virtual void update_exports() override;

#ifdef TOOLS_ENABLED
	virtual StringName get_doc_class_name() const override { return doc_class_name; }
	virtual Vector<DocData::ClassDoc> get_documentation() const override {
		const_cast<FoundryScript *>(this)->_ensure_documentation();
		return docs;
	}
	virtual String get_class_icon_path() const override;
#endif // TOOLS_ENABLED

	virtual Error reload(bool p_keep_state = false) override;

	virtual void set_path_cache(const String &p_path) override;
	virtual void set_path(const String &p_path, bool p_take_over = false) override;
	String get_script_path() const;
	Error load_source_code(const String &p_path);

	void set_binary_tokens_source(const Vector<uint8_t> &p_binary_tokens);
	const Vector<uint8_t> &get_binary_tokens_source() const;
	Vector<uint8_t> get_as_binary_tokens() const;

	bool get_property_default_value(const StringName &p_property, Variant &r_value) const override;

	virtual void get_script_method_list(List<MethodInfo> *p_list) const override;
	virtual bool has_method(const StringName &p_method) const override;
	virtual bool has_static_method(const StringName &p_method) const override;

	virtual int get_script_method_argument_count(const StringName &p_method, bool *r_is_valid = nullptr) const override;

	virtual MethodInfo get_method_info(const StringName &p_method) const override;

	virtual void get_script_property_list(List<PropertyInfo> *p_list) const override;

	virtual ScriptLanguage *get_language() const override;

	virtual int get_member_line(const StringName &p_member) const override {
#ifdef TOOLS_ENABLED
		if (member_lines.has(p_member)) {
			return member_lines[p_member];
		}
#endif
		return -1;
	}

	virtual void get_constants(HashMap<StringName, Variant> *p_constants) override;
	virtual void get_members(HashSet<StringName> *p_members) override;

	virtual const Variant get_rpc_config() const override;

	void unload_static() const;

#ifdef TOOLS_ENABLED
	virtual bool is_placeholder_fallback_enabled() const override { return placeholder_fallback_enabled; }
#endif

	FoundryScript();
	~FoundryScript();
};

// Read-only structured descriptor for a single resolved custom annotation usage, returned by the
// FoundryScript reflection APIs. Each instance is an independent snapshot of the compiled metadata: the
// argument accessors return copies, so callers cannot mutate the script's stored annotation data.
class FSAnnotation : public RefCounted {
	FOUNDRY_CLASS(FSAnnotation, RefCounted);

	StringName name; // Short name, without "@": "timeout", "export_range".
	StringName qualified_name; // Canonical declaration identity: "cafecito.test.timeout"; the bare name for built-ins.
	Array args; // Positional argument values in source order.
	Dictionary kwargs; // Named argument values keyed by parameter name.
	bool builtin = false; // True for a Godot built-in annotation; false for a user-declared custom annotation.

protected:
	static void _bind_methods();

public:
	StringName get_annotation_name() const { return name; }
	StringName get_qualified_name() const { return qualified_name; }
	// Deep copies so callers never receive (and so cannot mutate) the compiled script's stored values.
	Array get_arguments() const { return args.duplicate(true); }
	Dictionary get_named_arguments() const { return kwargs.duplicate(true); }
	// True when this descriptor wraps a Godot built-in annotation (e.g. `@export`), letting consumers
	// filter built-ins apart from user-declared custom annotations.
	bool is_builtin() const { return builtin; }

	// Builds an independent descriptor snapshot from compiled annotation metadata. The usage's
	// argument containers are deep copied so the descriptor never aliases the script's stored data.
	static Ref<FSAnnotation> from_usage(const FoundryScript::AnnotationUsage &p_usage);
};

// Read-only structured descriptor for a single reflected FoundryScript method, returned by the FoundryScript
// reflection APIs as a typed alternative to the loosely-keyed method descriptor Dictionary. It mirrors
// the entries of `Object.get_method_list()` (name, arguments, return value, default arguments, flags)
// and additionally carries the method's passive annotations. Each instance is an independent snapshot;
// the accessors return copies, so callers cannot mutate the script's compiled metadata.
class FSMethodDescriptor : public RefCounted {
	FOUNDRY_CLASS(FSMethodDescriptor, RefCounted);

	MethodInfo method_info;
	TypedArray<FSAnnotation> annotations;
	HashMap<StringName, TypedArray<FSAnnotation>> parameter_annotations;
	// Whether the reflected method is a FoundryScript declaration. Only FoundryScript descriptors carry an
	// `annotations` key in their Dictionary form, matching `FSReflection.get_methods()`, which
	// omits the key for native (non-FoundryScript) methods that have no passive annotations.
	bool fs_member = true;

protected:
	static void _bind_methods();

public:
	StringName get_method_name() const { return method_info.name; }
	// Parameter descriptors as PropertyInfo Dictionaries, in declaration order.
	TypedArray<Dictionary> get_arguments() const;
	// The return value's PropertyInfo as a Dictionary.
	Dictionary get_return_value() const;
	// Trailing default argument values, in declaration order.
	Array get_default_arguments() const;
	int64_t get_flags() const { return method_info.flags; }
	// Passive annotations applied to the method, as FSAnnotation descriptors, in source order.
	// Returns a fresh array so the caller cannot mutate the descriptor's stored annotations.
	TypedArray<FSAnnotation> get_annotations() const { return annotations.duplicate(); }
	// The equivalent loosely-keyed descriptor Dictionary, matching `FSReflection.get_methods()`
	// entries: an `Object.get_method_list()` Dictionary, plus an `annotations` key for FoundryScript methods.
	Dictionary to_dictionary() const;

	// Builds a descriptor from a method's MethodInfo and its already-resolved annotation descriptors.
	// `p_fs_member` is false for native (non-FoundryScript) methods, which omit the Dictionary's
	// `annotations` key.
	// `p_parameter_annotations` maps each argument name to its passive annotation descriptors.
	// Only FoundryScript methods populate this map; native methods pass an empty map.
	static Ref<FSMethodDescriptor> create(const MethodInfo &p_method_info, const TypedArray<FSAnnotation> &p_annotations, bool p_fs_member = true, const HashMap<StringName, TypedArray<FSAnnotation>> &p_parameter_annotations = HashMap<StringName, TypedArray<FSAnnotation>>());
};

// Read-only structured descriptor for a single reflected FoundryScript member variable, returned by the
// FoundryScript reflection APIs as a typed alternative to the loosely-keyed property descriptor Dictionary.
// It mirrors the entries of `Object.get_property_list()` (name, type, class name, hint, hint string,
// usage) and additionally carries the variable's passive annotations. Each instance is an independent
// snapshot; the accessors return copies, so callers cannot mutate the script's compiled metadata.
class FSPropertyDescriptor : public RefCounted {
	FOUNDRY_CLASS(FSPropertyDescriptor, RefCounted);

	PropertyInfo property_info;
	TypedArray<FSAnnotation> annotations;
	// Whether the reflected variable is a FoundryScript declaration. Only FoundryScript descriptors carry an
	// `annotations` key in their Dictionary form, matching `FSReflection.get_properties()`, which
	// omits the key for native (non-FoundryScript) properties that have no passive annotations.
	bool fs_member = true;

protected:
	static void _bind_methods();

public:
	StringName get_property_name() const { return property_info.name; }
	int64_t get_property_type() const { return property_info.type; }
	StringName get_property_class_name() const { return property_info.class_name; }
	int64_t get_property_hint() const { return property_info.hint; }
	String get_property_hint_string() const { return property_info.hint_string; }
	int64_t get_property_usage() const { return property_info.usage; }
	// Passive annotations applied to the variable, as FSAnnotation descriptors, in source order.
	// Returns a fresh array so the caller cannot mutate the descriptor's stored annotations.
	TypedArray<FSAnnotation> get_annotations() const { return annotations.duplicate(); }
	// The equivalent loosely-keyed descriptor Dictionary, matching `FSReflection.get_properties()`
	// entries: an `Object.get_property_list()` Dictionary, plus an `annotations` key for FoundryScript variables.
	Dictionary to_dictionary() const;

	// Builds a descriptor from a variable's PropertyInfo and its already-resolved annotation descriptors.
	// `p_fs_member` is false for native (non-FoundryScript) variables, which omit the Dictionary's
	// `annotations` key.
	static Ref<FSPropertyDescriptor> create(const PropertyInfo &p_property_info, const TypedArray<FSAnnotation> &p_annotations, bool p_fs_member = true);
};

class FSInstance : public ScriptInstance {
	friend class FoundryScript;
	friend class FSFunction;
	friend class FSLambdaCallable;
	friend class FSLambdaSelfCallable;
	friend class FSCompiler;
	friend class FSCache;
	friend struct FSUtilityFunctionsDefinitions;

	ObjectID owner_id;
	Object *owner = nullptr;
	Ref<FoundryScript> script;
#ifdef DEBUG_ENABLED
	HashMap<StringName, int> member_indices_cache; //used only for hot script reloading
#endif
	Vector<Variant> members;
	// Reified type arguments bound at construction (e.g. the `int` in `Box[int].new()`). Empty for
	// instances of non-generic classes or generic classes instantiated without explicit arguments.
	Vector<ContainerType> type_arguments;

	SelfList<FSFunctionState>::List pending_func_states;

	void _call_implicit_ready_recursively(FoundryScript *p_script);

public:
	virtual Object *get_owner() { return owner; }

	const Vector<ContainerType> &get_type_arguments() const { return type_arguments; }

	virtual void get_reified_type_arguments(Vector<ContainerType> &r_type_arguments) const { r_type_arguments = type_arguments; }

	virtual bool set(const StringName &p_name, const Variant &p_value);
	virtual bool get(const StringName &p_name, Variant &r_ret) const;
	virtual void get_property_list(List<PropertyInfo> *p_properties) const;
	virtual Variant::Type get_property_type(const StringName &p_name, bool *r_is_valid = nullptr) const;
	virtual void validate_property(PropertyInfo &p_property) const;

	virtual bool property_can_revert(const StringName &p_name) const;
	virtual bool property_get_revert(const StringName &p_name, Variant &r_ret) const;

	virtual void get_method_list(List<MethodInfo> *p_list) const;
	virtual bool has_method(const StringName &p_method) const;

	virtual int get_method_argument_count(const StringName &p_method, bool *r_is_valid = nullptr) const;

	virtual Variant callp(const StringName &p_method, const Variant **p_args, int p_argcount, Callable::CallError &r_error);

	Variant debug_get_member_by_index(int p_idx) const { return members[p_idx]; }

	virtual void notification(int p_notification, bool p_reversed = false);
	String to_string(bool *r_valid);

	virtual Ref<Script> get_script() const;

	virtual ScriptLanguage *get_language();

	void set_path(const String &p_path);

	void reload_members();

	virtual const Variant get_rpc_config() const;

	FSInstance() {}
	~FSInstance();
};

class FSReflection;
class FSNamespace;

class FSLanguage : public ScriptLanguage {
	friend class FSFunctionState;
#ifdef TESTS_ENABLED
	friend class FSTests::TestFSLanguageGlobalsAccessor;
#endif // TESTS_ENABLED

	static FSLanguage *singleton;

	bool finishing = false;

	Variant *_global_array = nullptr;
	Vector<Variant> global_array;
	HashMap<StringName, int> globals;
	HashMap<StringName, Variant> named_globals;
	Vector<int> global_array_empty_indexes;

	// Read-only reflection singletons exposed as the `foundry.reflection` surface.
	// Held by `named_globals`; these member refs keep them addressable and are
	// cleared in finish().
	Ref<FSReflection> reflection_singleton;
	Ref<FSNamespace> namespace_singleton;

	struct CallLevel {
		Variant *stack = nullptr;
		FSFunction *function = nullptr;
		FSInstance *instance = nullptr;
		int *ip = nullptr;
		int *line = nullptr;
		CallLevel *prev = nullptr; // Reverse linked list (stack).
	};

	static thread_local int _debug_parse_err_line;
	static thread_local String _debug_parse_err_file;
	static thread_local String _debug_error;

	static thread_local CallLevel *_call_stack;
	static thread_local uint32_t _call_stack_size;
	uint32_t _debug_max_call_stack = 0;

	bool track_call_stack = false;
	bool track_locals = false;
#ifdef TOOLS_ENABLED
	bool compiling_for_export = false;
#endif

	static CallLevel *_get_stack_level(uint32_t p_level);

	void _add_global(const StringName &p_name, const Variant &p_value);
	void _remove_global(const StringName &p_name);

	String _get_global_class_name(const String &p_path, String *r_base_type, String *r_icon_path, bool *r_is_abstract, bool *r_is_tool, bool *r_is_trait, bool *r_is_enum, LocalVector<String> &r_visited) const;

	// Cross-file index of custom annotation declarations, keyed by canonical identity
	// ("<namespace>.<name>", or "<name>" in the global namespace). Each entry tracks the
	// distinct source paths that declare it so duplicate canonical identities and
	// annotation-only namespaces can be discovered for import resolution.
	HashMap<StringName, Vector<String>> global_annotations;
	mutable Mutex annotation_index_mutex;

	friend class FSInstance;

	Mutex mutex;

	friend class FoundryScript;

	SelfList<FoundryScript>::List script_list;
	friend class FSFunction;

	SelfList<FSFunction>::List function_list;
#ifdef DEBUG_ENABLED
	bool profiling;
	bool profile_native_calls;
	uint64_t script_frame_time;
#endif

	HashMap<String, ObjectID> orphan_subclasses;

#ifdef TOOLS_ENABLED
	void _extension_loaded(const Ref<FoundryExtension> &p_extension);
	void _extension_unloading(const Ref<FoundryExtension> &p_extension);
#endif

public:
	bool debug_break(const String &p_error, bool p_allow_continue = true);
	bool debug_break_parse(const String &p_file, int p_line, const String &p_error);

	_FORCE_INLINE_ void enter_function(CallLevel *call_level, FSInstance *p_instance, FSFunction *p_function, Variant *p_stack, int *p_ip, int *p_line) {
		if (!track_call_stack) {
			return;
		}

#ifdef DEBUG_ENABLED
		ScriptDebugger *script_debugger = EngineDebugger::get_script_debugger();
		if (script_debugger != nullptr && script_debugger->get_lines_left() > 0 && script_debugger->get_depth() >= 0) {
			script_debugger->set_depth(script_debugger->get_depth() + 1);
		}
#endif

		if (unlikely(_call_stack_size >= _debug_max_call_stack)) {
			_debug_error = vformat("Stack overflow (stack size: %s). Check for infinite recursion in your script.", _debug_max_call_stack);

#ifdef DEBUG_ENABLED
			if (script_debugger != nullptr) {
				script_debugger->debug(this);
			}
#endif

			return;
		}

		call_level->prev = _call_stack;
		_call_stack = call_level;
		call_level->stack = p_stack;
		call_level->instance = p_instance;
		call_level->function = p_function;
		call_level->ip = p_ip;
		call_level->line = p_line;
		_call_stack_size++;
	}

	_FORCE_INLINE_ void exit_function() {
		if (!track_call_stack) {
			return;
		}

#ifdef DEBUG_ENABLED
		ScriptDebugger *script_debugger = EngineDebugger::get_script_debugger();
		if (script_debugger && script_debugger->get_lines_left() > 0 && script_debugger->get_depth() >= 0) {
			script_debugger->set_depth(script_debugger->get_depth() - 1);
		}
#endif

		if (unlikely(_call_stack_size == 0)) {
#ifdef DEBUG_ENABLED
			if (script_debugger) {
				_debug_error = "Stack Underflow (Engine Bug)";
				script_debugger->debug(this);
			} else {
				ERR_PRINT("Stack underflow! (Engine Bug)");
			}
#else // !DEBUG_ENABLED
			ERR_PRINT("Stack underflow! (Engine Bug)");
#endif
			return;
		}

		_call_stack_size--;
		_call_stack = _call_stack->prev;
	}

	virtual Vector<StackInfo> debug_get_current_stack_info() override {
		Vector<StackInfo> csi;
		csi.resize(_call_stack_size);
		CallLevel *cl = _call_stack;
		uint32_t idx = 0;
		while (cl) {
			csi.write[idx].line = *cl->line;
			if (cl->function) {
				csi.write[idx].func = cl->function->get_name();
				csi.write[idx].file = cl->function->get_script()->get_script_path();
			}
			idx++;
			cl = cl->prev;
		}
		return csi;
	}

	struct {
		StringName _init;
		StringName _static_init;
		StringName _notification;
		StringName _set;
		StringName _get;
		StringName _get_property_list;
		StringName _validate_property;
		StringName _property_can_revert;
		StringName _property_get_revert;
		StringName _script_source;

	} strings;

	_FORCE_INLINE_ bool should_track_call_stack() const { return track_call_stack; }
	_FORCE_INLINE_ bool should_track_locals() const { return track_locals; }
#ifdef TOOLS_ENABLED
	// The compiled-bytecode export compiles release-profile scripts without call-stack tracking
	// (no OPCODE_LINE emission); it saves and restores this flag around the export.
	_FORCE_INLINE_ void set_track_call_stack(bool p_track_call_stack) { track_call_stack = p_track_call_stack; }
	// While set, the compiler emits STORE_GLOBAL (with a masked, loader-rebaked operand) for
	// autoload singletons instead of the editor-session STORE_NAMED_GLOBAL fallback, matching how
	// game runtimes register autoloads in the global array. Scoped to each compiled-bytecode
	// .fs export (set for the compile/serialize, then cleared with an immediate recompile).
	_FORCE_INLINE_ bool is_compiling_for_export() const { return compiling_for_export; }
	_FORCE_INLINE_ void set_compiling_for_export(bool p_compiling_for_export) { compiling_for_export = p_compiling_for_export; }
#endif // TOOLS_ENABLED
	_FORCE_INLINE_ int get_global_array_size() const { return global_array.size(); }
	_FORCE_INLINE_ Variant *get_global_array() { return _global_array; }
	_FORCE_INLINE_ const HashMap<StringName, int> &get_global_map() const { return globals; }
	_FORCE_INLINE_ const HashMap<StringName, Variant> &get_named_globals_map() const { return named_globals; }
	// These two functions should be used when behavior needs to be consistent between in-editor and running the scene
	bool has_any_global_constant(const StringName &p_name) { return named_globals.has(p_name) || globals.has(p_name); }
	Variant get_any_global_constant(const StringName &p_name);

	// The reflection surface singletons registered by init(); null before init() and after finish().
	// Defined out of line because only forward declarations of the types are visible here.
	Ref<FSReflection> get_reflection_singleton() const;
	Ref<FSNamespace> get_namespace_singleton() const;

	_FORCE_INLINE_ static FSLanguage *get_singleton() { return singleton; }

	virtual String get_name() const override;

	/* LANGUAGE FUNCTIONS */
	virtual void init() override;
	virtual String get_type() const override;
	virtual String get_extension() const override;
	virtual void finish() override;

	/* EDITOR FUNCTIONS */
	virtual Vector<String> get_reserved_words() const override;
	virtual Vector<String> get_reserved_global_names() const override;
	// Whether `p_name` is one of this language's reserved built-in named globals (e.g.
	// the `godot` reflection namespace). Such a name wins over a project autoload of the
	// same name, so the global stays reachable even if project.foundry defines one.
	bool is_reserved_global_name(const StringName &p_name) const;
	virtual bool is_control_flow_keyword(const String &p_keywords) const override;
	virtual Vector<String> get_comment_delimiters() const override;
	virtual Vector<String> get_doc_comment_delimiters() const override;
	virtual Vector<String> get_string_delimiters() const override;
	virtual bool is_using_templates() override;
	virtual Ref<Script> make_template(const String &p_template, const String &p_class_name, const String &p_base_class_name) const override;
	virtual Vector<ScriptTemplate> get_built_in_templates(const StringName &p_object) override;
	virtual bool validate(const String &p_script, const String &p_path = "", List<String> *r_functions = nullptr, List<ScriptLanguage::ScriptError> *r_errors = nullptr, List<ScriptLanguage::Warning> *r_warnings = nullptr, HashSet<int> *r_safe_lines = nullptr) const override;
	virtual Script *create_script() const override;
	virtual bool supports_builtin_mode() const override;
	virtual bool supports_documentation() const override;
	virtual bool can_inherit_from_file() const override { return true; }
	virtual int find_function(const String &p_function, const String &p_code) const override;
	virtual String make_function(const String &p_class, const String &p_name, const PackedStringArray &p_args) const override;
	virtual Error complete_code(const String &p_code, const String &p_path, Object *p_owner, List<ScriptLanguage::CodeCompletionOption> *r_options, bool &r_forced, String &r_call_hint) override;
#ifdef TOOLS_ENABLED
	virtual Error lookup_code(const String &p_code, const String &p_symbol, const String &p_path, Object *p_owner, LookupResult &r_result) override;
#endif
	virtual String _get_indentation() const;
	virtual void auto_indent_code(String &p_code, int p_from_line, int p_to_line) const override;
#ifdef TOOLS_ENABLED
	virtual bool format_code(const String &p_code, const String &p_path, String &r_formatted_code, String *r_error_message = nullptr) const override;
#endif
	virtual void add_global_constant(const StringName &p_variable, const Variant &p_value) override;
	virtual void add_named_global_constant(const StringName &p_name, const Variant &p_value) override;
	virtual void remove_named_global_constant(const StringName &p_name) override;

	/* DEBUGGER FUNCTIONS */

	virtual String debug_get_error() const override;
	virtual int debug_get_stack_level_count() const override;
	virtual int debug_get_stack_level_line(int p_level) const override;
	virtual String debug_get_stack_level_function(int p_level) const override;
	virtual String debug_get_stack_level_source(int p_level) const override;
	virtual void debug_get_stack_level_locals(int p_level, List<String> *p_locals, List<Variant> *p_values, int p_max_subitems = -1, int p_max_depth = -1) override;
	virtual void debug_get_stack_level_members(int p_level, List<String> *p_members, List<Variant> *p_values, int p_max_subitems = -1, int p_max_depth = -1) override;
	virtual ScriptInstance *debug_get_stack_level_instance(int p_level) override;
	virtual void debug_get_globals(List<String> *p_globals, List<Variant> *p_values, int p_max_subitems = -1, int p_max_depth = -1) override;
	virtual String debug_parse_stack_level_expression(int p_level, const String &p_expression, int p_max_subitems = -1, int p_max_depth = -1) override;

	virtual void reload_all_scripts() override;
	virtual void reload_scripts(const Array &p_scripts, bool p_soft_reload) override;
#ifdef DEBUG_ENABLED
	// Connected to ProjectSettings::settings_changed. When a strict analysis flag changes it
	// invalidates the FoundryScript cache and reloads scripts so the live session re-reports them under
	// the new flags. A no-op when no analysis-affecting setting changed.
	static void _on_settings_changed();
#endif // DEBUG_ENABLED
	virtual void reload_tool_script(const Ref<Script> &p_script, bool p_soft_reload) override;

	virtual void frame() override;

	virtual void get_public_functions(List<MethodInfo> *p_functions) const override;
	virtual void get_public_constants(List<Pair<String, Variant>> *p_constants) const override;
	virtual void get_public_annotations(List<MethodInfo> *p_annotations) const override;

	virtual void profiling_start() override;
	virtual void profiling_stop() override;
	virtual void profiling_set_save_native_calls(bool p_enable) override;
	void profiling_collate_native_call_data(bool p_accumulated);

	virtual int profiling_get_accumulated_data(ProfilingInfo *p_info_arr, int p_info_max) override;
	virtual int profiling_get_frame_data(ProfilingInfo *p_info_arr, int p_info_max) override;

	/* LOADER FUNCTIONS */

	virtual void get_recognized_extensions(List<String> *p_extensions) const override;

	/* GLOBAL CLASSES */

	virtual bool handles_global_class_type(const String &p_type) const override;
	virtual String get_global_class_name(const String &p_path, String *r_base_type = nullptr, String *r_icon_path = nullptr, bool *r_is_abstract = nullptr, bool *r_is_tool = nullptr, bool *r_is_trait = nullptr, bool *r_is_enum = nullptr) const override;

	/* CUSTOM ANNOTATION INDEX */

	// Parse `p_path` (without running the analyzer, mirroring `get_global_class_name`) and
	// collect the canonical identities of every root-level custom annotation declaration.
	void get_global_annotations(const String &p_path, List<StringName> *r_annotations) const;
	// Replace every annotation declaration indexed for `p_path` with `p_annotations`, dropping any
	// previously registered for that path. Refreshes the index from a freshly parsed file.
	void replace_global_annotations(const String &p_path, const List<StringName> &p_annotations);
	// Re-extract a file's annotation declarations from disk and refresh the index. Called by the
	// editor file-system scan (see `ScriptLanguage::update_global_class_annotations`).
	virtual void update_global_class_annotations(const String &p_search_path, const String &p_target_path) override;
#ifdef TOOLS_ENABLED
	// Drops cached parsers for p_path and its transitive dependents, then re-parses any
	// client-managed LSP documents in that affected set so diagnostics refresh after a
	// dependency edit (save or external disk change).
	void notify_disk_source_changed(const String &p_path);
#endif
	// Register a custom annotation declaration under its canonical identity for `p_path`.
	void add_global_annotation(const StringName &p_qualified_name, const String &p_path);
	// Drop every annotation declaration previously registered for `p_path`.
	void remove_global_annotations_by_path(const String &p_path);
	void clear_global_annotations();
	// True when the canonical identity is declared by at least one indexed file.
	bool is_global_annotation(const StringName &p_qualified_name) const;
	// True when the canonical identity is declared by two or more distinct files.
	bool is_duplicated_global_annotation(const StringName &p_qualified_name) const;
	// Source path of the file that declares the canonical identity, or an empty string when it is
	// unknown. When several files declare it (a duplicate identity), the first indexed path wins.
	String get_global_annotation_path(const StringName &p_qualified_name) const;
	// True when any indexed annotation declaration lives in `p_namespace`.
	bool namespace_has_annotations(const String &p_namespace) const;
	// Append every indexed canonical annotation identity. Used by editor tooling (completion and
	// go-to-definition) to enumerate annotations visible through the current namespace or imports.
	void get_global_annotation_list(List<StringName> *r_annotations) const;

	void add_orphan_subclass(const String &p_qualified_name, const ObjectID &p_subclass);
	Ref<FoundryScript> get_orphan_subclass(const String &p_qualified_name);

	Ref<FoundryScript> get_script_by_fully_qualified_name(const String &p_name);

	FSLanguage();
	~FSLanguage();
};

class ResourceFormatLoaderFoundryScript : public ResourceFormatLoader {
	FOUNDRY_SOFTCLASS(ResourceFormatLoaderFoundryScript, ResourceFormatLoader);

public:
	virtual Ref<Resource> load(const String &p_path, const String &p_original_path = "", Error *r_error = nullptr, bool p_use_sub_threads = false, float *r_progress = nullptr, CacheMode p_cache_mode = CACHE_MODE_REUSE) override;
	virtual void get_recognized_extensions(List<String> *p_extensions) const override;
	virtual bool handles_type(const String &p_type) const override;
	virtual String get_resource_type(const String &p_path) const override;
	virtual void get_dependencies(const String &p_path, List<String> *p_dependencies, bool p_add_types = false) override;
	virtual void get_classes_used(const String &p_path, HashSet<StringName> *r_classes) override;
};

class ResourceFormatSaverFoundryScript : public ResourceFormatSaver {
	FOUNDRY_SOFTCLASS(ResourceFormatSaverFoundryScript, ResourceFormatSaver);

public:
	virtual Error save(const Ref<Resource> &p_resource, const String &p_path, uint32_t p_flags = 0) override;
	virtual void get_recognized_extensions(const Ref<Resource> &p_resource, List<String> *p_extensions) const override;
	virtual bool recognize(const Ref<Resource> &p_resource) const override;
};
