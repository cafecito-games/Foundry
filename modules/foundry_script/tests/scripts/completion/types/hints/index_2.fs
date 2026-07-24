const A = preload("res://completion/class_a.notest.fs")

class LocalInnerClass:
    const AInner = preload("res://completion/class_a.notest.fs")
    class InnerInnerClass:
        const AInnerInner = preload("res://completion/class_a.notest.fs")
        enum InnerInnerInnerEnum:
            pass
        class InnerInnerInnerClass:
            pass
    enum InnerInnerEnum:
        pass

enum TestEnum:
	pass

var test_var: LocalInnerClass.InnerInnerClass.➡
