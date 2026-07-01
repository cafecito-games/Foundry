extends SceneTree

var _done := false

func _initialize() -> void:
	var label := OS.get_environment("PERF_LABEL")
	if label == "":
		label = "?"
	print("[PERF][begin] label=", label)
	_run(label)
	_done = true

func _process(_delta: float) -> bool:
	return _done

func _list_fs(dir_path: String) -> Array:
	var out := []
	var d := DirAccess.open(dir_path)
	if d == null:
		return out
	d.list_dir_begin()
	var fn := d.get_next()
	while fn != "":
		if not d.current_is_dir() and fn.ends_with(".fs"):
			out.append(dir_path.path_join(fn))
		fn = d.get_next()
	d.list_dir_end()
	out.sort()
	return out

func _run(label: String) -> void:
	# --- 1. Cold script compile (ResourceLoader on .fs, cache ignored) ---
	var scripts := _list_fs("res://scripts")
	var t0 := Time.get_ticks_usec()
	for p in scripts:
		ResourceLoader.load(p, "Script", ResourceLoader.CACHE_MODE_IGNORE_DEEP)
	var t1 := Time.get_ticks_usec()
	var sc: int = scripts.size() if scripts.size() > 0 else 1
	print("[PERF][script_cold] label=%s count=%d total_ms=%.3f per_ms=%.4f" % [label, scripts.size(), (t1 - t0) / 1000.0, (t1 - t0) / 1000.0 / sc])

	# --- 2. Warm script load (cache reuse) for contrast ---
	t0 = Time.get_ticks_usec()
	for p in scripts:
		ResourceLoader.load(p, "Script", ResourceLoader.CACHE_MODE_REUSE)
	t1 = Time.get_ticks_usec()
	print("[PERF][script_warm] label=%s count=%d total_ms=%.3f per_ms=%.4f" % [label, scripts.size(), (t1 - t0) / 1000.0, (t1 - t0) / 1000.0 / sc])

	# --- 3. Scene load + instantiate ---
	var manifest_txt := FileAccess.get_file_as_string("res://bench_manifest.txt")
	var scenes := manifest_txt.split("\n", false)

	var flat_load_us := 0
	var flat_inst_us := 0
	var flat_count := 0
	var flat_nodes := 0
	var nested_load_us := 0
	var nested_inst_us := 0

	for path in scenes:
		var is_nested := path.ends_with("nested.tscn")
		# Cold load (deep ignore -> also re-reads scripts/subscenes)
		var a := Time.get_ticks_usec()
		var res: Resource = ResourceLoader.load(path, "PackedScene", ResourceLoader.CACHE_MODE_IGNORE_DEEP)
		var b := Time.get_ticks_usec()
		var packed := res as PackedScene
		if packed == null:
			print("[PERF][error] failed to load ", path)
			continue
		# Instantiate
		var c := Time.get_ticks_usec()
		var inst: Node = packed.instantiate()
		var e := Time.get_ticks_usec()
		var n := 0
		if inst != null:
			n = 1 + _count_descendants(inst)
			inst.free()
		if is_nested:
			nested_load_us += (b - a)
			nested_inst_us += (e - c)
			print("[PERF][nested] label=%s nodes=%d load_ms=%.3f inst_ms=%.3f" % [label, n, (b - a) / 1000.0, (e - c) / 1000.0])
		else:
			flat_load_us += (b - a)
			flat_inst_us += (e - c)
			flat_count += 1
			flat_nodes += n

	if flat_count > 0:
		print("[PERF][flat_cold] label=%s scenes=%d total_nodes=%d load_ms=%.3f inst_ms=%.3f load_per_scene_ms=%.4f inst_per_scene_ms=%.4f" % [label, flat_count, flat_nodes, flat_load_us / 1000.0, flat_inst_us / 1000.0, flat_load_us / 1000.0 / flat_count, flat_inst_us / 1000.0 / flat_count])

	# Warm scene load pass (cache reuse): scripts + subscenes already cached,
	# so this isolates scene-parse + instantiate cost from cold script compile.
	var warm_flat_load_us := 0
	var warm_flat_count := 0
	for path in scenes:
		if path.ends_with("nested.tscn"):
			continue
		var a := Time.get_ticks_usec()
		var res: Resource = ResourceLoader.load(path, "PackedScene", ResourceLoader.CACHE_MODE_REUSE)
		var b := Time.get_ticks_usec()
		if res == null:
			continue
		warm_flat_load_us += (b - a)
		warm_flat_count += 1
	if warm_flat_count > 0:
		print("[PERF][flat_warm] label=%s scenes=%d load_ms=%.3f load_per_scene_ms=%.4f" % [label, warm_flat_count, warm_flat_load_us / 1000.0, warm_flat_load_us / 1000.0 / warm_flat_count])
	print("[PERF][end] label=", label)

func _count_descendants(node: Node) -> int:
	var total := 0
	for i in node.get_child_count():
		var child := node.get_child(i)
		total += 1 + _count_descendants(child)
	return total
