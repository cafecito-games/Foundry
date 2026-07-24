const A = preload("res://completion/class_a.notest.fs")

class LocalInnerClass:
    class InnerInnerClass:
        pass
    enum InnerInnerEnum:
        pass

var test_var: LocalInnerClass.➡
