#!/usr/bin/env python3

import os
import sys

HEADER_KIND_FOUNDRY = "foundry"
HEADER_KIND_INHERITED = "inherited"

HEADER_BORDER = "/**************************************************************************/"
HEADER_INNER_WIDTH = len(HEADER_BORDER) - 4


def _comment_line(text="", align="left"):
    if align == "center":
        inner = text.center(HEADER_INNER_WIDTH)
    elif text:
        inner = f" {text}".ljust(HEADER_INNER_WIDTH)
    else:
        inner = " " * HEADER_INNER_WIDTH
    return f"/*{inner}*/"


def _license_lines():
    return [
        _comment_line(),
        _comment_line("Permission is hereby granted, free of charge, to any person obtaining"),
        _comment_line("a copy of this software and associated documentation files (the"),
        _comment_line('"Software"), to deal in the Software without restriction, including'),
        _comment_line("without limitation the rights to use, copy, modify, merge, publish,"),
        _comment_line("distribute, sublicense, and/or sell copies of the Software, and to"),
        _comment_line("permit persons to whom the Software is furnished to do so, subject to"),
        _comment_line("the following conditions:"),
        _comment_line(),
        _comment_line("The above copyright notice and this permission notice shall be"),
        _comment_line("included in all copies or substantial portions of the Software."),
        _comment_line(),
        _comment_line('THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,'),
        _comment_line("EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF"),
        _comment_line("MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT."),
        _comment_line("IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY"),
        _comment_line("CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,"),
        _comment_line("TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE"),
        _comment_line("SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE."),
    ]


def generate_copyright_header(filename, header_kind=HEADER_KIND_FOUNDRY):
    fsingle = os.path.basename(filename.strip())

    if header_kind == HEADER_KIND_INHERITED:
        lines = [
            HEADER_BORDER,
            _comment_line(f" {fsingle}"),
            HEADER_BORDER,
            _comment_line("This file is part of:", "center"),
            _comment_line("GODOT ENGINE", "center"),
            _comment_line("https://godotengine.org", "center"),
            HEADER_BORDER,
            _comment_line("Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md)."),
            _comment_line("Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur."),
        ]
    else:
        lines = [
            HEADER_BORDER,
            _comment_line(f" {fsingle}"),
            HEADER_BORDER,
            _comment_line("This file is part of:", "center"),
            _comment_line("FOUNDRY ENGINE", "center"),
            _comment_line("A fork of the Godot Engine (https://godotengine.org)", "center"),
            _comment_line("https://www.cafecito.games", "center"),
            HEADER_BORDER,
            _comment_line("Copyright (c) 2026-present Cafecito Games LLC."),
        ]

    return "\n".join([*lines, *_license_lines(), HEADER_BORDER]) + "\n"


def _detect_header_kind(header_text):
    if "Cafecito Games" in header_text or "FOUNDRY ENGINE" in header_text or "Foundry Engine" in header_text:
        return HEADER_KIND_FOUNDRY
    if "Godot Engine contributors" in header_text or "Juan Linietsky" in header_text:
        return HEADER_KIND_INHERITED
    return HEADER_KIND_FOUNDRY


def _split_existing_header(contents):
    lines = contents.splitlines(keepends=True)
    index = 0

    while index < len(lines) and lines[index].strip() == "":
        index += 1

    if index >= len(lines) or "/**********" not in lines[index]:
        return HEADER_KIND_FOUNDRY, "".join(lines[index:])

    header_start = index
    while index < len(lines) and lines[index].startswith("/*"):
        index += 1

    header_text = "".join(lines[header_start:index])
    if index < len(lines) and lines[index].strip() == "":
        index += 1

    return _detect_header_kind(header_text), "".join(lines[index:])


def update_copyright_header(path):
    with open(path, "r", encoding="utf-8") as fileread:
        contents = fileread.read()

    header_kind, body = _split_existing_header(contents)
    text = generate_copyright_header(path, header_kind)
    if body:
        text += "\n" + body

    with open(path, "w", encoding="utf-8", newline="\n") as filewrite:
        filewrite.write(text)


def main(argv):
    if not argv:
        print("Invalid usage of copyright_headers.py, it should be called with a path to one or multiple files.")
        return 1

    for fname in argv:
        update_copyright_header(fname.strip())

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
