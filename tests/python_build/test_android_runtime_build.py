from __future__ import annotations

import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]

REMOVED_PATHS = (
    "platform/android/foundry_android_runtime.json",
    "platform/android/android_runtime_build.py",
    "platform/android/android_native_bundle.py",
)
REQUIRED_PATHS = (
    "platform/android/android_native_contract.py",
    "platform/android/android_native_staging.py",
    "platform/android/java/lib/build.gradle",
    "platform/android/java/lib/src/main/AndroidManifest.xml",
)
PRODUCTION_FILES = (
    "platform/android/android_source_template.py",
    "platform/android/detect.py",
    "platform/android/platform_android_builders.py",
    "platform/android/java/build.gradle",
    "platform/android/java/lib/build.gradle",
    "platform/android/ANDROID_RUNTIME.md",
    ".github/workflows/android_builds.yml",
    ".github/workflows/android_java_check.yml",
    ".github/workflows/release.yml",
)
FORBIDDEN_FRAGMENTS = (
    "Foundry-Android",
    "foundry_android_runtime",
    "foundryAndroidSource",
    "foundryAndroidFetch",
    "foundryNativeBundle",
    "foundryRuntimeScratch",
    "foundry_native_bundle",
    "foundry_runtime_scratch",
    "foundry-native.zip",
    "WS2_REMOVE_ANDROID_RUNTIME_COMPAT_BRIDGE",
)


class AndroidRuntimeOwnershipTests(unittest.TestCase):
    def test_standalone_pin_resolver_and_bundle_tools_are_absent(self) -> None:
        present = [path for path in REMOVED_PATHS if (REPO_ROOT / path).exists()]
        self.assertEqual([], present)

    def test_in_tree_host_and_native_validation_paths_exist(self) -> None:
        missing = [path for path in REQUIRED_PATHS if not (REPO_ROOT / path).is_file()]
        self.assertEqual([], missing)

    def test_production_build_ci_release_and_docs_have_no_standalone_input(self) -> None:
        matches: list[str] = []
        for relative_path in PRODUCTION_FILES:
            contents = (REPO_ROOT / relative_path).read_text(encoding="utf-8")
            for fragment in FORBIDDEN_FRAGMENTS:
                if fragment in contents:
                    matches.append(f"{relative_path}: {fragment}")
        self.assertEqual([], matches)

    def test_gradle_builds_and_packages_the_in_tree_host(self) -> None:
        root_build = (REPO_ROOT / "platform/android/java/build.gradle").read_text(encoding="utf-8")
        app_build = (REPO_ROOT / "platform/android/java/app/build.gradle").read_text(encoding="utf-8")
        library_build = (REPO_ROOT / "platform/android/java/lib/build.gradle").read_text(encoding="utf-8")

        self.assertIn('dependsOn ":lib:assembleTemplate${capitalizedTarget}"', root_build)
        self.assertIn('implementation project(":lib")', app_build)
        self.assertIn("foundryNativeRoot", library_build)
        self.assertIn("--native-root", library_build)
        self.assertIn("--local-root", library_build)

    def test_internal_host_is_not_published(self) -> None:
        gradle = "\n".join(
            (REPO_ROOT / path).read_text(encoding="utf-8")
            for path in (
                "platform/android/java/build.gradle",
                "platform/android/java/lib/build.gradle",
            )
        )
        release = (REPO_ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")
        for fragment in (
            "maven-publish",
            "MavenPublication",
            ":lib:publish",
            "closeAndReleaseSonatypeStagingRepository",
        ):
            self.assertNotIn(fragment, gradle)
            self.assertNotIn(fragment, release)


if __name__ == "__main__":
    unittest.main()
