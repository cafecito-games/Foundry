extends Node

func helper() -> int:
	return 1

func caller() -> int:
	return helper() + helper()
