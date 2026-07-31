# A script that does not implement the `to_json` hook, so the marshal helper must report
# failure instead of silently producing a value.
extends RefCounted

var label: String = "no hook"
