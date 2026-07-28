# Foundry-Java Android Export Integration Design

**Status:** Approved

## Scope and fixed contract

Foundry-Java remains completely absent from ordinary Android exports. A project
opts in only through the versioned `foundry_java_registry_marker` Gradle
handoff. The only accepted marker is `registry-index-v2`; any other non-empty
value fails before Gradle is launched.

The opted-in application consumes the merged Foundry-Java Workstream 9
contract without reproducing it:

- plugin ID `games.cafecito.foundry.java`;
- plugin DSL `foundryJava.requestedAbis`;
- descriptor path
  `META-INF/foundry-java/modules/<module>.descriptor`, format 2;
- generated APK assets
  `assets/foundry_java/registry-index-v2.txt` and
  `assets/FoundryJava.foundryextension`;
- direct generated bootstrap
  `games.cafecito.foundry.generated.FoundryGeneratedBootstrap`;
- one binding AAR containing
  `jni/<abi>/libfoundry_java.so` for the selected Android ABIs; and
- strict, artifact-identified failures for malformed descriptors, mixed
  provenance, duplicate identities or payloads, and missing ABIs.

Foundry does not synthesize the fixed configuration, inspect Java classes or
manifests, or search for artifacts. Foundry-Android remains read-only.

## Alternatives considered

### Versioned explicit Gradle handoff (selected)

The exporter validates a small export-preset surface and passes sorted,
explicit properties into the existing source-template Gradle application. The
application conditionally resolves and applies the published plugin and gives
its dependency graph to the plugin. This keeps both Maven and offline/local
workflows deterministic while leaving validation and generated assets owned by
Foundry-Java.

### Embed Foundry-Java artifacts in `android_source.zip`

This makes one template self-contained but silently couples every ordinary
Foundry template to one binding version and violates the explicit-input
requirement. It is rejected.

### Scan `res://addons`, manifests, or classpaths

This would recreate the removed plugin discovery model and would make missing
or conflicting artifacts dependent on traversal order. It is rejected.

## Export-preset and Gradle handoff

The Android export preset adds these opt-in fields under
`gradle_build/foundry_java/`:

- `enabled`: boolean, default `false`;
- `gradle_plugin_maven`: one exact `group:artifact:version` coordinate;
- `gradle_plugin_local`: one local plugin JAR;
- `maven_repositories`: explicit repository URLs;
- `maven_artifacts`: exact dependency coordinates;
- `local_artifacts`: explicit regular JAR/AAR files.

When `enabled` is false, the other stored fields are inert and the exporter
passes no Foundry-Java property. This lets a user temporarily disable the
integration without deleting its explicit inputs. When enabled, Gradle builds
are mandatory, exactly one plugin source is required, and at least one explicit
application artifact is required. The exporter passes:

```text
foundry_java_registry_marker=registry-index-v2
foundry_java_gradle_plugin=<coordinate or absolute local JAR>
foundry_java_gradle_plugin_kind=maven|local
foundry_java_maven_repositories=<sorted encoded list>
foundry_java_maven_artifacts=<sorted encoded list>
foundry_java_local_artifacts=<sorted encoded list>
```

List values are sorted and joined with `|` after validation. Newlines, carriage
returns, `|` injection, blank entries, duplicates, dynamic Maven
versions, malformed coordinates, repository URLs other than HTTPS or absolute
local `file:///` URLs, credential-bearing or query/fragment repository URLs,
and missing or non-regular local inputs fail deterministically and name the
export option without echoing repository values. The exporter globalizes and
lexically simplifies each user-supplied path once, then uses that same normalized
path for symlink validation, regular-file checks, and opening. It rejects a
symlink in the final component or any user-controlled existing component of the
normalized path. On macOS, only the fixed OS-owned `/etc`, `/tmp`, and `/var`
aliases are accepted, and only when their link targets exactly match
`private/etc`, `private/tmp`, and `private/var`; every later component is still
checked. Only an accepted regular file with no user-controlled symlink traversal
is then canonicalized before the Gradle command is constructed. Maven and local
application artifacts may coexist; Maven and local plugin sources may not.

Repository URLs use ASCII URI syntax. Non-ASCII characters must be
percent-encoded. Malformed percent escapes and unsupported raw URI characters
are rejected before Gradle, and repository values are redacted from diagnostics,
verbose command logging, and captured Gradle output before it is displayed or
retained by the exporter.

The application build script adds the selected plugin artifact to its
`buildscript` classpath only when the marker is present, applies
`games.cafecito.foundry.java`, adds the explicit Maven/local application
artifacts as normal `implementation` dependencies, and sets
`foundryJava.requestedAbis` from the existing `export_enabled_abis` property.
Maven repositories are used only for the opted-in build. No coordinate,
repository, version, or adjacent-file convention is inferred.

## Source-template and ordinary-export isolation

`android_source.zip` continues to contain only the three in-tree Foundry host
AARs. It contains the conditional Gradle wiring but no Foundry-Java plugin,
binding, runtime, processor, module artifact, generated marker, or generated
configuration. The source-template inspector explicitly guards this ownership
boundary.

For ordinary exports, neither configuration nor dependency resolution mentions
Foundry-Java at execution time. Default and custom application IDs, debug and
release builds, native-library stripping, extension-library packaging, and
configuration-cache behavior remain unchanged.

## Failure behavior

The exporter rejects non-Gradle opt-in and incomplete or malformed handoffs
before invoking Gradle. Once Gradle owns the graph, the merged Foundry-Java
plugin remains the one validation authority. Its diagnostics must retain the
artifact path/coordinate, descriptor path, conflicting values, and requested
ABI as applicable.

An opted-in export with zero descriptors is an error even though the plugin
correctly emits no outputs: a successful Java-enabled Foundry export must
contain exactly one registry index and one byte-identical fixed configuration.
The merged Foundry-Java plugin exclusively validates the resolved Maven/local
dependency graph, descriptor contents and provenance, binding/configuration
counts, and requested ABI availability before it generates outputs. Foundry
does not duplicate that graph validator. For explicit local inputs, Foundry's
preflight rejects a forbidden `libfoundry_android.so` entry before Gradle. The
final application legitimately contains Foundry's own host library, so
final-output inspection cannot infer which input supplied it. After assembly,
Foundry inspects only the observable APK/AAB Foundry-Java contract and rejects:

- absent or duplicate generated index/configuration;
- anything other than one `libfoundry_java.so` for each requested ABI; and
- any `libfoundry_java.so` for an unrequested ABI.

The final inspector also streams the configuration, registry index, and each
requested ABI bridge through miniz to validate the declared uncompressed size
and CRC. This payload validation is bounded at 128 MiB per required entry and
512 MiB across required entries. Unrequested bridge payloads are not read
because their names already fail the exact ABI-set contract.

Broad handwritten keep rules and discovery fallbacks are prohibited through
static source/build-script contracts rather than inferred from final APK
contents.

## Test matrix

Tests are added in TDD order:

1. ordinary property absence and no binding/plugin/network activity;
2. real ordinary debug and release/custom-ID builds remain unchanged;
3. opt-in requires Gradle and a complete explicit source;
4. deterministic validation of coordinates, URLs, paths, duplicates, and
   plugin-source ambiguity;
5. staged explicit Maven debug/default-ID build;
6. fully explicit local plugin/binding/runtime/module build;
7. zero-descriptor opt-in rejection with no generated outputs;
8. exactly one byte-identical configuration and index; missing/duplicate
   rejection;
9. strict format-2 descriptor path/header/name validation;
10. duplicate module and registry diagnostics;
11. mixed API/generator/runtime/bridge provenance diagnostics;
12. missing, duplicate, and split bridge/configuration diagnostics;
13. `libfoundry_android.so` rejection with entry evidence;
14. empty, unsupported, and bridge-missing ABI rejection;
15. parameterized four-ABI debug selection preserving unrelated native
    libraries and strip policy;
16. custom-ID minified release, direct bootstrap/provider/trampoline retention,
    reproducibility, and configuration-cache reuse; and
17. final source-template plus command-first/API 36 default-debug and
    custom-minified-release acceptance.

Python contract tests cover static ownership, parsing, source-template
isolation, and deterministic artifact inspection. Real Gradle fixtures cover
Maven/local resolution, variant assets, ABI selection, debug/minified builds,
custom IDs, R8, and cache reuse. The Foundry strict build, focused exporter
tests, and full command-first test suite remain release gates.
