# Companion trait for retroactive_conformance_builtin. Builtin value types are retroactively conformed
# to this trait by an `extend` declaration that supplies required witnesses externally.
trait_name RtcBuiltinCounter

abstract func doubled() -> int

abstract func size() -> int
