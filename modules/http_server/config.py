def can_build(env, platform):
    return True


def configure(env):
    pass


def get_doc_classes():
    return [
        "foundry.http.server.HTTPRequest",
        "foundry.http.server.HTTPResponse",
        "foundry.http.server.HTTPServer",
    ]


def get_doc_path():
    return "doc_classes"
