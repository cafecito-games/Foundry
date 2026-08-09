# A root-discard of a Coroutine[void] async call is a deliberate fire-and-forget launch: there is no
# result to lose, so MISSING_AWAIT stays silent and no warning suppressor is needed.
async func fire_and_forget() -> void:
	pass

func test() -> void:
	fire_and_forget()
