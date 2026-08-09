# Editor Documentation Cache Upgrade Design

## Problem

Editor documentation caches are binary `Resource` files. When an older Foundry build encounters a
cache written by a newer binary resource format, `ResourceLoader::load()` reports a hard error before
`EditorHelp` can fall back to regeneration. The global cache is eventually overwritten and the
project script cache is deleted and regenerated, but users see alarming startup errors for data that
is disposable by design.

Regenerating the native documentation cache also exposes a separate parser bug. The class reference
schema permits an empty self-closing `<tutorials />` element, but `DocTools::_load()` enters the
tutorial child loop without checking whether the element is empty. It then treats the following
`<methods>` element as an invalid tutorial child.

## Design

Before loading either documentation cache, `EditorHelp` will ask `ResourceLoader::get_resource_type()`
whether the file is a readable `Resource`. The binary loader's recognition path checks the resource
header and returns an empty type for unsupported format or engine versions without emitting the hard
load error. An unreadable cache will be removed and regenerated through the existing fallback path.
This behavior remains scoped to disposable editor documentation caches; ordinary resource loads keep
their existing diagnostics.

`DocTools::_load()` will treat an empty `<tutorials />` element as a valid tutorials section containing
zero links. Non-empty tutorial sections retain the existing parsing and validation behavior.

## Error Handling

Failure to remove an incompatible cache will use the existing file-removal diagnostic. If a cache
passes the compatibility probe but fails during full deserialization, the existing corruption error
and regeneration behavior remains intact. This preserves useful diagnostics for actual damage while
making expected cross-version invalidation silent.

## Tests

A `DocTools` unit test will load a class document containing `<tutorials />` followed by `<methods>` and
assert that loading succeeds and the method remains present.

An editor documentation-cache unit test will create a binary `Resource`, alter its resource-format
header to a future version, invoke the cache compatibility/discard helper, and assert that the file is
removed. A current-format cache will be retained. Tests will use the shared test scratch directory.

Focused editor tests will run first, followed by the repository's native strict validation build and
the relevant test cases before the pull request is opened.
