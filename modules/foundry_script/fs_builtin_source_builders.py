"""Embeds the Foundry Script sources under `builtin/` into the binary at build time."""

import os

import methods

BUILTIN_PATH_PREFIX = "foundry://builtin/"


def escape_line(line):
    escaped = line.replace("\\", "\\\\").replace('"', '\\"').replace("\t", "\\t")
    return f'"{escaped}\\n"'


def make_builtin_sources(target, source, env):
    entries = []

    for filepath in sorted(str(node) for node in source):
        with open(filepath, "r", encoding="utf-8") as file:
            text = file.read()
        if not text.endswith("\n"):
            text += "\n"
        # A string literal per source line keeps the generated file diffable and stays well
        # under the minimum literal length every compiler must support.
        literal = "\n\t\t\t".join(escape_line(line) for line in text.split("\n")[:-1])
        path = BUILTIN_PATH_PREFIX + os.path.basename(filepath)
        entries.append(f'\t{{ "{path}",\n\t\t\t{literal} }},')

    entry_string = "\n".join(entries)

    with methods.generated_wrapper(str(target[0])) as file:
        file.write(f"""\
struct FSBuiltinSourceEntry {{
	const char *path;
	const char *source;
}};

inline constexpr int FS_BUILTIN_SOURCE_COUNT = {len(entries)};

inline constexpr FSBuiltinSourceEntry FS_BUILTIN_SOURCES[FS_BUILTIN_SOURCE_COUNT] = {{
{entry_string}
}};
""")
