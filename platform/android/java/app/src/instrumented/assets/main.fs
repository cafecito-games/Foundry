extends Node2D

var _test_bridge: JavaClass

func _ready():
	_test_bridge = JavaClassWrapper.wrap(
			"games.cafecito.foundry.game.test.FoundryAppInstrumentedTestBridge"
	)
	if _test_bridge == null:
		printerr("Couldn't resolve the instrumented test bridge")
		get_tree().quit()
		return
	_test_bridge.notifyMainLoopStarted()


func _process(_delta: float) -> void:
	if _test_bridge == null:
		return

	var test_label: String = _test_bridge.takeRequestedTest()
	if not test_label.is_empty():
		_launch_tests(test_label)

	var quit_on_go_back: int = _test_bridge.takeRequestedQuitOnGoBack()
	if quit_on_go_back >= 0:
		get_tree().quit_on_go_back = quit_on_go_back == 1
		_test_bridge.notifyQuitOnGoBackApplied()


func _exit_tree() -> void:
	if _test_bridge != null:
		_test_bridge.notifyEngineTerminating()


func _launch_tests(test_label: String) -> void:
	var test_instance: BaseTest = null
	match test_label:
		"javaclasswrapper_tests":
			test_instance = JavaClassWrapperTests.new()
		"file_access_tests":
			test_instance = FileAccessTests.new()

	if test_instance:
		test_instance.__reset_tests()
		test_instance.run_tests()
		var incomplete_tests = test_instance._test_started - test_instance._test_completed
		_test_bridge.onTestsCompleted(
				test_label,
				test_instance._test_completed,
				test_instance._test_assert_failures + incomplete_tests
		)
	else:
		_test_bridge.onTestsFailed(test_label, "Unable to launch tests")
