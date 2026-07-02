def can_build(env, platform):
    env.module_add_dependencies("foundry_script", ["jsonrpc", "websocket"], True)
    return True


def get_opts(platform):
    from SCons.Variables import BoolVariable

    return [
        BoolVariable(
            "foundry_script_frontend",
            "Compile the Foundry Script front-end (tokenizer, parser, analyzer, compiler). Disable only for .fsb-only export-template builds.",
            True,
        ),
    ]


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
