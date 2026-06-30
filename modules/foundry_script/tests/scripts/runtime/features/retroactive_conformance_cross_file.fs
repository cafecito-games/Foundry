# The `extend RtcPanel uses RtcMeasurable` conformance is declared in a SEPARATE file that this
# fixture `preload`s, rather than inline. This proves a retroactive conformance declared cross-file is
# visible to the using code: the witness dispatches through the trait type (via a trait-typed local, a
# trait-typed parameter, and a generic bound `T: RtcMeasurable`), and `is`/`as` against the
# externally-conformed trait both work. Regression for the cross-file ordering bug: the declaring
# file must be raised to `INTERFACE_SOLVED` and registered before this file's body resolution consults
# `is`/`as`/assignment against RtcMeasurable.
const _Conformance = preload("rtc_runtime_panel_measurable.notest.fs")


func via_param(m: RtcMeasurable) -> int:
	return m.size()


func via_bound[T: RtcMeasurable](value: T) -> int:
	return value.size()


func test() -> void:
	var panel := RtcPanel.new()
	var m: RtcMeasurable = panel
	print(m.size())
	print(via_param(panel))
	print(via_bound(panel))
	print(panel is RtcMeasurable)
	var as_measurable := panel as RtcMeasurable
	print(as_measurable.size())
