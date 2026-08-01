## Documentation comment for the sample class.
# Ordinary comment.
@tool
@cafecito.test.timeout(5.0)
namespace Sample.Highlighting
import Sample.Support

class_name GrammarSample extends Node uses Printable

const LIMIT = 0xFF_00
const MASK = 0b1010_1010
const RATIO = 1.5e-3
const RAW = r"C:\path\no\escapes"
const RAW_QUOTE = r"a \" b \\ c"
const NAME = &"unique_name"
const PATH = ^"res://scene.tscn"
const DOC = """
Triple-quoted body with a # that is not a comment.
"""
const OTHER = '''single triple'''

var health: int = 100
var scores: Array[int] = []
var int = 0
int += 1
print(int)
var label := "escaped \n \u00e9 text"
var continued := "first part \
second part"
var annotation := "contextual word used as an identifier"
var extend := 1
var async := 2
var targets := 3
var get := 4

signal damaged(amount: int)

annotation Timeout(seconds: float) targets METHOD, CLASS:
	pass

annotation Marker targets CLASS, METHOD:
	pass

annotation Multiline(
	seconds: float
) targets METHOD, CLASS:
	pass

extend Sample.Support.Helper uses Printable:
	pass

func remainder(left: int, right: int) -> int:
	var tight := left %right
	for step in 1..2:
		health += step
	return left % right

func first(pair: Tuple[int, int]) -> int:
	return pair.0

async func fetch(path: String) -> Coroutine[int]:
	var node := $Player/%Weapon/Barrel
	var other := %Weapon
	var quoted := $"Player Two"
	var mixed := $Player/"Weapon Slot"/%Barrel
	var triple_double := $"""Some
Node"""
	var triple_single := $'''Some
Node'''
	var rooted_triple_double := $Root/"""Some
Node"""
	var rooted_triple_single := $Root/'''Some
Node'''
	var toggled := not %Weapon.visible
	if node != null and not other.is_queued_for_deletion():
		await node.ready
	match path:
		"a" when true:
			return 1
		_:
			return 0

func get(key: String) -> int:
	return health

func set(key: String, value: int) -> void:
	health = value

func use_accessors() -> void:
	get("health")
	set("health", 1)

var scaled: float:
	get:
		return health * TAU / PI
	set(value):
		health = int(value if value < INF else NAN)

var armor: int:
	get():
		return health

var speed: int:
	get = read_speed
	set = write_speed

var values = {
	"builtin": int,
	"native": Node,
	"project": Player,
	"qualified": Game.Player,
}

var factories = {
	"build": func(value: int) -> Player:
		return Player.new(),
}

var python_entries = {
	get(): 1,
	get: 2,
	set(value): 3,
}

var lua_entries = {
	get = read_value,
	set = write_value,
}
