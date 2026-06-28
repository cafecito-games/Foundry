const A = preload("res://completion/class_a.notest.fs")

class LocalInnerClass:
    class InnerInnerClass:
        pass
    enum InnerInnerEnum {}

var test_var: LocalInnerClass.➡
