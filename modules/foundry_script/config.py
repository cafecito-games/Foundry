def can_build(env, platform):
    env.module_add_dependencies("foundry_script", ["jsonrpc", "websocket"], True)
    return True


def configure(env):
    pass


def get_doc_classes():
    return [
        "@FoundryScript",
        "FoundryScript",
        "FSAnnotation",
        "FSSyntaxHighlighter",
        "FSTypeParameter",
    ]


def get_doc_path():
    return "doc_classes"
