# A contextual case shorthand on the right of an assignment resolves against the assignee's type,
# whether the assignee is a local, a member, or a member reached through a receiver.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


class Holder:
	var value: Result[int, String] = Result[int, String].Err("initial")


var member: Result[int, String] = Result[int, String].Err("start")


func test():
	var local: Result[int, String] = Result[int, String].Err("x")
	local = .Ok(1)
	print(local)

	member = .Ok(2)
	print(member)

	var holder := Holder.new()
	holder.value = .Ok(3)
	print(holder.value)
