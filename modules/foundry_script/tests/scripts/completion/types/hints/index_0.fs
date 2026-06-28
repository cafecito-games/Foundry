const A = preload("res://completion/class_a.notest.fs")

class LocalInnerClass:
    const AInner = preload("res://completion/class_a.notest.fs")
    enum LocalInnerInnerEnum {}
    class LocalInnerInnerClass:
        pass

enum LocalInnerEnum {}

var test_var: A➡
