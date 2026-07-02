def can_build(env, platform):
    env.module_add_dependencies("foundry_script", ["jsonrpc", "websocket"], True)
    return True


def get_opts(platform):
    from SCons.Variables import BoolVariable

    return [
        BoolVariable(
            "foundry_script_frontend",
            "Compile the Foundry Script front-end (tokenizer, parser, analyzer, compiler). Defaults to no for export-template targets; pass yes to opt in.",
            True,
        ),
    ]


def configure(env):
    from SCons.Script import ARGUMENTS

    # Export templates load precompiled .fsb only; omit the source front-end unless
    # the build explicitly passes foundry_script_frontend=yes (e.g. modding templates).
    if not env.editor_build and "foundry_script_frontend" not in ARGUMENTS:
        env["foundry_script_frontend"] = False


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
