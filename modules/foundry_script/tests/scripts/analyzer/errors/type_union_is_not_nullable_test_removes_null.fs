# `null` passes `is T?` for every `T`, so a failed nullable test doubles as a null check: the tested
# alternative goes, and so does null. Normalization stores alternatives non-nullable, so the removal
# itself has to ignore the `?` and look only at the type the test names.
type MaybeScalar = int? | String


func classify(value: MaybeScalar) -> void:
	if value is not String?:
		var probe := value.no_such_member
		print(probe)
