const A = preload("res://completion/class_a.notest.fs")

class LocalInnerClass:
    const AInner = preload("res://completion/class_a.notest.fs")
    class InnerInnerClass:
        const AInnerInner = preload("res://completion/class_a.notest.fs")
        enum InnerInnerInnerEnum {}
        class InnerInnerInnerClass:
            pass
    enum InnerInnerEnum {}

enum TestEnum {}

var test_var: LocalInnerClass.InnerInnerClass.➡
