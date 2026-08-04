"""Structural contracts over the CI workflow graph.

Every assertion here goes through `workflow_graph`, which parses YAML and exposes
jobs, dependency edges, step order, matrices, and inputs. Nothing in this module
reads workflow files as text, so reformatting a workflow cannot break a contract
and cannot hide a broken one either.

`actionlint` is the primary gate on workflow *syntax*; these contracts cover the
graph properties `actionlint` cannot know about.
"""

from __future__ import annotations

import re
import shlex
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / ".github/scripts"))

import workflow_graph  # noqa: E402

WORKFLOWS = REPO_ROOT / ".github/workflows"
PRE_COMMIT_CONFIG = REPO_ROOT / ".pre-commit-config.yaml"

PLATFORMS = ("linux", "macos", "windows", "android", "ios", "web")
PLATFORM_WORKFLOWS = {platform: f"{platform}_builds.yml" for platform in PLATFORMS}

# Reusable platform workflows always check out an explicit commit. `github.sha` pins
# the commit that triggered the run; `github.ref` resolves late, so a branch or
# `refs/pull/N/merge` that moves mid-run can hand different jobs different trees.
CANONICAL_CHECKOUT_REF = "${{ inputs.checkout-ref || github.sha }}"

WORKFLOW_GRAPH_HOOK = "foundry-workflow-graph-contracts"

ANDROID_ABI_ARCHITECTURES = {
    "armeabi-v7a": "arm32",
    "arm64-v8a": "arm64",
    "x86": "x86_32",
    "x86_64": "x86_64",
}
ANDROID_BUILD_TYPES = {
    "debug": {
        "target": "template_debug",
        "production": "no",
        "dev_mode": "no",
        "dev_build": "no",
        "debug_symbols": "no",
    },
    "dev": {
        "target": "template_debug",
        "production": "no",
        "dev_mode": "yes",
        "dev_build": "yes",
        "debug_symbols": "yes",
    },
    "release": {
        "target": "template_release",
        "production": "yes",
        "dev_mode": "no",
        "dev_build": "no",
        "debug_symbols": "no",
    },
}
ANDROID_TEMPLATE_OUTPUTS = (
    "bin/android_debug.apk",
    "bin/android_dev.apk",
    "bin/android_release.apk",
    "bin/android_source.zip",
    "bin/foundry-debug.aar",
    "bin/foundry-dev.aar",
    "bin/foundry-release.aar",
)
# The Android runtime host lives in this repository. Any reference to the retired
# standalone runtime would reintroduce a second, external build authority.
FORBIDDEN_EXTERNAL_COUPLING = (
    "cafecito-games/Foundry-Android",
    "foundry_android_runtime",
    "android_runtime_build.py",
    "foundryAndroidSource",
    "foundryAndroidFetch",
    "foundryNativeBundle",
    "foundryRuntimeScratch",
    "foundry-native.zip",
    "standalone-runtime",
)
FORBIDDEN_RELEASE_PUBLICATION = (
    ":lib:publish",
    "closeAndReleaseSonatypeStagingRepository",
    "OSSRH_USERNAME",
    "OSSRH_PASSWORD",
    "SONATYPE_STAGING_PROFILE_ID",
    "publish-publication",
)


def scons_flag(value: Any) -> str:
    """Render a matrix value the way SCons receives it.

    YAML 1.1 reads bare `yes`/`no` as booleans, and the Android matrices spell
    those flags both bare and quoted. The flag a build actually receives is the
    same either way, so contracts compare the rendered form.
    """

    if isinstance(value, bool):
        return "yes" if value else "no"
    return str(value)


def load_workflow(name: str) -> workflow_graph.Workflow:
    return workflow_graph.load(WORKFLOWS / name)


def shell_commands(script: str) -> list[list[str]]:
    """Tokenize the non-comment commands in a parsed workflow step."""

    return [shlex.split(line) for line in script.splitlines() if line.strip() and not line.lstrip().startswith("#")]


def local_hook(hook_id: str) -> dict[str, Any]:
    """Return the `repo: local` pre-commit hook with the given id."""

    config = workflow_graph.load(PRE_COMMIT_CONFIG).document
    for repository in config["repos"]:
        for hook in repository.get("hooks") or []:
            if hook.get("id") == hook_id:
                return dict(hook)
    raise AssertionError(f"`.pre-commit-config.yaml` declares no hook {hook_id!r}.")


def remote_hook(repository_url: str, hook_id: str) -> tuple[dict[str, Any], dict[str, Any]]:
    """Return the `(repo, hook)` pair for a hook sourced from an external repository."""

    config = workflow_graph.load(PRE_COMMIT_CONFIG).document
    for repository in config["repos"]:
        if repository.get("repo") != repository_url:
            continue
        for hook in repository.get("hooks") or []:
            if hook.get("id") == hook_id:
                return dict(repository), dict(hook)
    raise AssertionError(f"`.pre-commit-config.yaml` declares no {hook_id!r} hook from {repository_url!r}.")


class WorkflowContractTestCase(unittest.TestCase):
    def assert_hook_covers(self, hook: dict[str, Any], relative_paths: tuple[str, ...]) -> None:
        trigger = re.compile(str(hook["files"]), re.VERBOSE)
        for relative_path in relative_paths:
            with self.subTest(relative_path=relative_path):
                self.assertRegex(relative_path, trigger)

    def keyed_cells(
        self,
        workflow: workflow_graph.Workflow,
        job: str,
        dimensions: tuple[str, ...],
    ) -> dict[tuple[Any, ...], dict[str, Any]]:
        """Key a job's matrix cells, rejecting duplicates.

        Two cells with the same key would schedule two builds that publish the same
        artifact name, and keying them into a mapping would otherwise hide the second.
        """

        cells = workflow.matrix_cells(job)
        keys = [tuple(cell[dimension] for dimension in dimensions) for cell in cells]
        self.assertEqual(len(keys), len(set(keys)), f"job {job!r} declares duplicate matrix cells")
        return dict(zip(keys, cells))

    def assert_no_scalar_contains(self, workflow: workflow_graph.Workflow, fragments: tuple[str, ...]) -> None:
        scalars = list(workflow.scalars())
        for fragment in fragments:
            with self.subTest(fragment=fragment):
                self.assertEqual([], [scalar for scalar in scalars if fragment in scalar])


class KeyedCellsTests(WorkflowContractTestCase):
    """The duplicate-cell guard the matrix contracts rely on."""

    def keyed_cells_for(self, cells: str) -> dict[tuple[Any, ...], dict[str, Any]]:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fixture.yml"
            path.write_text(
                "on: workflow_dispatch\njobs:\n  build:\n    strategy:\n      matrix:\n        include:\n" + cells,
                encoding="utf-8",
            )
            return self.keyed_cells(workflow_graph.load(path), "build", ("abi",))

    def test_distinct_cells_are_keyed_by_their_dimensions(self) -> None:
        cells = self.keyed_cells_for("          - abi: arm64-v8a\n          - abi: x86_64\n")
        self.assertEqual({("arm64-v8a",), ("x86_64",)}, set(cells))

    def test_a_duplicate_cell_fails_the_contract(self) -> None:
        with self.assertRaises(AssertionError):
            self.keyed_cells_for("          - abi: arm64-v8a\n          - abi: arm64-v8a\n")


class PrPlatformChecksWorkflowTests(WorkflowContractTestCase):
    workflow: workflow_graph.Workflow

    @classmethod
    def setUpClass(cls) -> None:
        cls.workflow = load_workflow("pr_platform_checks.yml")

    def test_the_workflow_runs_on_new_issue_comments(self) -> None:
        self.assertEqual({"types": ["created"]}, self.workflow.trigger("issue_comment"))

    def test_preflight_plans_the_request_with_the_tested_planner(self) -> None:
        planner = self.workflow.step_run("preflight", "Plan requested platform checks")
        self.assertIn(".github/scripts/pr_platform_check_request.py plan", planner)

    def test_preflight_publishes_a_run_decision_for_every_platform(self) -> None:
        outputs = self.workflow.outputs("preflight")
        self.assertEqual("${{ steps.plan.outputs.platforms_label }}", outputs["platforms_label"])
        for platform in PLATFORMS:
            with self.subTest(platform=platform):
                self.assertEqual(f"${{{{ steps.plan.outputs.run_{platform} }}}}", outputs[f"run_{platform}"])
        # The head repository is deliberately not an output: platform jobs only ever
        # build this repository at an explicit commit.
        self.assertNotIn("pr_head_repo", outputs)

    def test_platform_jobs_are_gated_on_an_authorized_request(self) -> None:
        for platform in PLATFORMS:
            with self.subTest(platform=platform):
                self.assertEqual(
                    "${{ needs.preflight.outputs.authorized == 'true' "
                    f"&& needs.preflight.outputs.run_{platform} == 'true' }}}}",
                    self.workflow.job_if(platform),
                )
                self.assertEqual(("preflight",), self.workflow.needs(platform))

    def test_platform_jobs_declare_per_platform_concurrency_groups(self) -> None:
        for platform in PLATFORMS:
            with self.subTest(platform=platform):
                self.assertEqual(
                    {
                        "group": f"pr-every-platform-{platform}-${{{{ needs.preflight.outputs.pr_head_sha }}}}",
                        "cancel-in-progress": False,
                    },
                    self.workflow.concurrency(platform),
                )

    def test_every_platform_workflow_call_passes_checkout_ref(self) -> None:
        for platform, relative_name in PLATFORM_WORKFLOWS.items():
            with self.subTest(platform=platform):
                self.assertEqual(f"./.github/workflows/{relative_name}", self.workflow.uses(platform))
                self.assertEqual(
                    {"checkout-ref": "${{ needs.preflight.outputs.pr_head_sha }}"},
                    self.workflow.with_inputs(platform),
                )

    def test_acceptance_comment_precedes_the_platform_jobs(self) -> None:
        self.assertLess(
            self.workflow.step_index("notify", "Comment on unauthorized request"),
            self.workflow.step_index("notify", "Comment on accepted request"),
        )
        for platform in PLATFORMS:
            with self.subTest(platform=platform):
                self.assertLess(self.workflow.job_index("notify"), self.workflow.job_index(platform))

    def test_the_notify_job_answers_only_requests(self) -> None:
        self.assertEqual("${{ needs.preflight.outputs.requested == 'true' }}", self.workflow.job_if("notify"))
        self.assertEqual(
            "${{ needs.preflight.outputs.authorized != 'true' }}",
            self.workflow.step_if("notify", "Comment on unauthorized request"),
        )
        self.assertEqual(
            "${{ needs.preflight.outputs.authorized == 'true' }}",
            self.workflow.step_if("notify", "Comment on accepted request"),
        )

    def test_the_finish_job_reports_every_platform_result(self) -> None:
        self.assertEqual(("preflight", *PLATFORMS), self.workflow.needs("finish"))
        self.assertEqual(
            "${{ always() && needs.preflight.outputs.requested == 'true' "
            "&& needs.preflight.outputs.authorized == 'true' }}",
            self.workflow.job_if("finish"),
        )
        environment = self.workflow.job_env("finish")
        self.assertEqual("${{ needs.preflight.outputs.platforms_label }}", environment["PLATFORMS_LABEL"])
        for platform in PLATFORMS:
            with self.subTest(platform=platform):
                self.assertEqual(f"${{{{ needs.{platform}.result }}}}", environment[platform])
                self.assertEqual(
                    f"${{{{ needs.preflight.outputs.run_{platform} }}}}",
                    environment[f"run_{platform}"],
                )


class PlatformCheckoutPinningTests(WorkflowContractTestCase):
    """Every platform build checks out the exact commit that triggered the run.

    `github.ref` resolves late, so a branch or `refs/pull/N/merge` that moves
    mid-run can hand different jobs different trees. Regression guard for the fix
    that pinned platform checkouts to the triggering commit.
    """

    def test_every_platform_workflow_accepts_a_checkout_ref_input(self) -> None:
        for platform, relative_name in PLATFORM_WORKFLOWS.items():
            with self.subTest(platform=platform):
                inputs = load_workflow(relative_name).workflow_call_inputs()
                self.assertIn("checkout-ref", inputs)
                # A platform build never targets a foreign repository.
                self.assertNotIn("checkout-repository", inputs)

    def test_every_platform_checkout_pins_the_triggering_commit(self) -> None:
        for platform, relative_name in PLATFORM_WORKFLOWS.items():
            workflow = load_workflow(relative_name)
            own_checkouts = 0
            for job_name, step in workflow.all_steps():
                uses = str(step.get("uses") or "")
                if not uses.startswith("actions/checkout@"):
                    continue
                inputs = step.get("with") or {}
                if "repository" in inputs:
                    # Steps that check out another repository pin their own ref.
                    continue
                own_checkouts += 1
                with self.subTest(platform=platform, job=job_name, step=step.get("name")):
                    self.assertEqual(CANONICAL_CHECKOUT_REF, inputs.get("ref"))
            with self.subTest(platform=platform):
                self.assertGreater(own_checkouts, 0, f"{relative_name} checks out nothing")


class PrEditorArtifactsWorkflowTests(WorkflowContractTestCase):
    workflow: workflow_graph.Workflow

    @classmethod
    def setUpClass(cls) -> None:
        cls.workflow = load_workflow("pr_editor_artifacts.yml")

    def test_editor_artifact_comment_precedes_the_pull_request_checkout(self) -> None:
        existing = self.workflow.step_index("build", "Comment on existing artifact")
        started = self.workflow.step_index("build", "Comment on started artifact build")
        checkout = self.workflow.step_index("build", "Checkout pull request head")
        self.assertLess(existing, started)
        self.assertLess(started, checkout)

    def test_the_build_comment_and_checkout_are_skipped_for_a_cached_artifact(self) -> None:
        for step_name in (
            "Comment on started artifact build",
            "Checkout pull request head",
        ):
            with self.subTest(step=step_name):
                self.assertEqual(
                    "${{ steps.existing.outputs.found != 'true' }}",
                    self.workflow.step_if("build", step_name),
                )
        self.assertEqual(
            "${{ steps.existing.outputs.found == 'true' }}",
            self.workflow.step_if("build", "Comment on existing artifact"),
        )

    def test_the_build_job_checks_out_the_pull_request_head_commit(self) -> None:
        inputs = self.workflow.step_with("build", "Checkout pull request head")
        self.assertEqual("${{ needs.preflight.outputs.pr_head_repo }}", inputs["repository"])
        self.assertEqual("${{ needs.preflight.outputs.pr_head_sha }}", inputs["ref"])


class ReleaseIosWorkflowTests(WorkflowContractTestCase):
    workflow: workflow_graph.Workflow

    @classmethod
    def setUpClass(cls) -> None:
        cls.workflow = load_workflow("release.yml")

    def test_ios_build_matrix_covers_every_device_and_simulator_cell(self) -> None:
        self.assertIs(False, self.workflow.strategy("build-ios")["fail-fast"])
        self.assertEqual("iOS ${{ matrix.name }}", self.workflow.job_name("build-ios"))

        cells = {key[0]: cell for key, cell in self.keyed_cells(self.workflow, "build-ios", ("name",)).items()}
        self.assertEqual(
            {"release-ios-device", "release-ios-simulator", "debug-ios-device", "debug-ios-simulator"},
            set(cells),
        )
        for name, cell in cells.items():
            with self.subTest(cell=name):
                self.assertEqual(name, cell["cache-name"])
                self.assertEqual(
                    "template_release" if name.startswith("release-") else "template_debug",
                    cell["target"],
                )
                # Simulator cells build a second x86_64 slice; device cells do not.
                self.assertEqual(
                    "arch=x86_64 ios_simulator=yes" if name.endswith("-simulator") else "",
                    cell["extra-scons-flags"],
                )

        # The matrix compiles slices only; the combined bundle is assembled downstream.
        for cell in self.workflow.matrix_cells("build-ios"):
            for value in cell.values():
                self.assertNotIn("generate_bundle=yes", str(value))
        self.assertNotIn("Compilation (debug simulator x86_64) + bundle", self.workflow.step_names("build-ios"))
        self.assertEqual(
            "${{ matrix.cache-name }}",
            self.workflow.step_with("build-ios", "Upload artifact")["name"],
        )

    def test_ios_assembly_depends_on_the_build_matrix(self) -> None:
        self.assertEqual(("build-ios",), self.workflow.needs("assemble-ios"))
        download = self.workflow.step("assemble-ios", "Download iOS template libraries")
        self.assertEqual("actions/download-artifact@v8", download["uses"])
        self.assertEqual("*-ios-*", download["with"]["pattern"])
        self.assertIn(
            "misc/scripts/package_ios_templates.py",
            self.workflow.step_run("assemble-ios", "Assemble iOS template"),
        )
        self.assertEqual("release-ios", self.workflow.step_with("assemble-ios", "Upload artifact")["name"])

    def test_release_package_depends_on_assembly_not_the_build_matrix(self) -> None:
        needs = self.workflow.needs("package")
        self.assertIn("assemble-ios", needs)
        self.assertNotIn("build-ios", needs)


class ReleaseContainerWorkflowTests(WorkflowContractTestCase):
    workflow: workflow_graph.Workflow

    @classmethod
    def setUpClass(cls) -> None:
        cls.workflow = load_workflow("release.yml")

    def test_publish_container_is_gated_on_a_published_non_draft_release(self) -> None:
        self.assertEqual(("resolve", "build-linux", "publish"), self.workflow.needs("publish-container"))
        self.assertEqual(
            "${{ !cancelled() && needs.resolve.result == 'success' "
            "&& needs.build-linux.result == 'success' "
            "&& needs.resolve.outputs.draft == 'false' "
            "&& needs.publish.outputs.github_release_published == 'true' }}",
            self.workflow.job_if("publish-container"),
        )

    def test_publish_container_serializes_per_channel(self) -> None:
        concurrency = self.workflow.concurrency("publish-container")
        self.assertEqual("release-container-${{ needs.resolve.outputs.channel }}", concurrency["group"])
        self.assertIs(False, concurrency["cancel-in-progress"])

    def test_publish_container_passes_version_and_revision_build_args(self) -> None:
        for step_name in ("Build local smoke image", "Publish release image"):
            with self.subTest(step=step_name):
                build_args = self.workflow.step_with("publish-container", step_name)["build-args"]
                self.assertEqual(
                    [
                        "FOUNDRY_VERSION=${{ needs.resolve.outputs.release_version }}",
                        "FOUNDRY_REVISION=${{ github.sha }}",
                    ],
                    [entry for entry in build_args.splitlines() if entry.strip()],
                )

    def test_publish_container_delegates_its_decisions_to_tested_scripts(self) -> None:
        self.assertIn(
            ".github/scripts/resolve_channel_tag_freshness.sh",
            self.workflow.step_run("publish-container", "Check moving alias freshness"),
        )
        self.assertIn(
            ".github/scripts/verify_headless_image.sh",
            self.workflow.step_run("publish-container", "Verify headless image"),
        )


class AndroidRuntimeWorkflowTests(WorkflowContractTestCase):
    android: workflow_graph.Workflow
    android_java: workflow_graph.Workflow
    release: workflow_graph.Workflow

    @classmethod
    def setUpClass(cls) -> None:
        cls.android = load_workflow("android_builds.yml")
        cls.android_java = load_workflow("android_java_check.yml")
        cls.release = load_workflow("release.yml")

    def assert_native_matrix(
        self,
        workflow: workflow_graph.Workflow,
        job: str,
        artifact_prefix: str,
    ) -> None:
        cells = self.keyed_cells(workflow, job, ("build_type", "abi"))
        self.assertEqual(
            {(build_type, abi) for build_type in ANDROID_BUILD_TYPES for abi in ANDROID_ABI_ARCHITECTURES},
            set(cells),
        )
        for (build_type, abi), cell in cells.items():
            with self.subTest(build_type=build_type, abi=abi):
                self.assertEqual(
                    {
                        "build_type": build_type,
                        "abi": abi,
                        "arch": ANDROID_ABI_ARCHITECTURES[abi],
                        **ANDROID_BUILD_TYPES[build_type],
                        "artifact_name": f"{artifact_prefix}{build_type}-{abi}",
                    },
                    {key: scons_flag(value) for key, value in cell.items()},
                )

        compile_step = workflow.step_with(job, "Compile exact native cell")
        self.assertEqual("android", compile_step["platform"])
        self.assertEqual("${{ matrix.target }}", compile_step["target"])
        self.assertIs(True, compile_step["preserve-source-tree"])
        scons_flags = workflow_graph.normalize_expression(compile_step["scons-flags"])
        if "${{ env.SCONS_FLAGS }}" in scons_flags:
            # The release workflow factors the shared flags into a job-level env var.
            job_flags = workflow_graph.normalize_expression(workflow.job_env(job)["SCONS_FLAGS"])
            scons_flags = scons_flags.replace("${{ env.SCONS_FLAGS }}", job_flags)
        self.assertIn("tests=no", scons_flags)
        self.assertIn("swappy=yes", scons_flags)
        for dimension in ("arch", "production", "dev_mode", "dev_build", "debug_symbols"):
            with self.subTest(dimension=dimension):
                self.assertIn(f"{dimension}=${{{{ matrix.{dimension} }}}}", scons_flags)

        staging = workflow.step_run(job, "Stage native cell with provenance")
        self.assertIn("bin/android-native/${ENGINE_REVISION}/${{ matrix.build_type }}/${{ matrix.abi }}", staging)
        for library in ("libc++_shared.so", "libfoundry_android.so", "provenance.json"):
            with self.subTest(library=library):
                self.assertIn(library, staging)

        upload = workflow.step_with(job, "Upload native cell")
        self.assertEqual("${{ matrix.artifact_name }}", upload["name"])
        self.assertEqual("${{ runner.temp }}/android-native-artifact", upload["path"])

    def assert_in_tree_assembly(
        self,
        workflow: workflow_graph.Workflow,
        job: str,
        native_job: str,
        artifact_pattern: str,
    ) -> None:
        self.assertIn(native_job, workflow.needs(job))
        download = workflow.step_with(job, "Download every native cell")
        self.assertEqual(artifact_pattern, download["pattern"])
        self.assertIs(True, download["merge-multiple"])

        assembly = workflow.step_run(job, "Assemble and verify in-tree host")
        self.assertIn("./gradlew --no-daemon generateFoundryTemplates", assembly)
        self.assertIn("-PfoundryNativeRoot=", assembly)
        for output in ANDROID_TEMPLATE_OUTPUTS:
            with self.subTest(output=output):
                self.assertIn(output, assembly)

    def test_android_native_matrix_covers_every_abi_and_build_type(self) -> None:
        self.assert_native_matrix(self.android, "build-android-native", "android-native-")
        self.assertEqual(("validate-runtime-contracts",), self.android.needs("build-android-native"))

    def test_android_assembly_runs_the_in_tree_gradle_host(self) -> None:
        self.assert_in_tree_assembly(
            self.android,
            job="assemble-android",
            native_job="build-android-native",
            artifact_pattern="android-native-*",
        )
        self.assertEqual(
            "android-templates-assembled",
            self.android.step_with("assemble-android", "Upload assembled Android templates")["name"],
        )

    def test_runtime_contract_validation_runs_the_host_side_suites(self) -> None:
        validation = self.android.step_run("validate-runtime-contracts", "Validate Android runtime contracts")
        for module in (
            "tests.python_build.test_android_runtime_contract",
            "tests.python_build.test_android_runtime_build",
            "tests.python_build.test_android_jni_contract",
            "tests.python_build.test_android_host_contract",
            "tests.python_build.test_android_native_staging",
            "tests.python_build.test_android_gradle_behavioral",
            "tests.python_build.test_android_gradle_runtime_contract",
            "tests.python_build.test_android_device_acceptance",
            "misc/checks/check_file_policy.py",
            "misc/checks/check_extension_api_naming.py",
        ):
            with self.subTest(module=module):
                self.assertIn(module, validation)

    def test_instrumented_assets_are_compiled_by_an_exact_host_editor(self) -> None:
        self.assertEqual(("validate-runtime-contracts",), self.android.needs("compile-instrumented-assets"))
        self.assertEqual(
            CANONICAL_CHECKOUT_REF,
            self.android.step_with("compile-instrumented-assets", "Checkout Foundry")["ref"],
        )
        build = self.android.step_with("compile-instrumented-assets", "Build exact host editor")
        self.assertEqual("linuxbsd", build["platform"])
        self.assertEqual("editor", build["target"])
        self.assertIn("tests=no", build["scons-flags"].split())

        export = self.android.step_run("compile-instrumented-assets", "Export compiled instrumented assets")
        for fragment in (
            "cp -R platform/android/java/app/src/instrumented/assets",
            "project export",
            '--preset "Android Instrumented Assets"',
            "--mode pack",
            "prepare-compiled-assets",
            "inspect-compiled-assets",
        ):
            with self.subTest(fragment=fragment):
                self.assertIn(fragment, export)
        self.assertEqual(
            "android-instrumented-assets",
            self.android.step_with("compile-instrumented-assets", "Upload compiled instrumented assets")["name"],
        )

    def test_foundry_java_export_validation_is_an_independent_gate(self) -> None:
        # Nothing gates the Foundry-Java export matrix, so it reports independently
        # of the native build.
        self.assertFalse(self.android.has_needs("validate-foundry-java-export"))
        checkout = self.android.step_with("validate-foundry-java-export", "Checkout exact Foundry-Java")
        self.assertEqual("cafecito-games/Foundry-Java", checkout["repository"])
        self.assertEqual("${{ steps.foundry-java-pin.outputs.commit }}", checkout["ref"])
        self.assertIs(False, checkout["persist-credentials"])
        self.assertEqual(
            "17",
            str(self.android.step_with("validate-foundry-java-export", "Set up Java 17")["java-version"]),
        )

    def test_command_first_exports_follow_assembly_and_the_host_editor(self) -> None:
        self.assertEqual(
            ("validate-foundry-java-export", "assemble-android", "compile-instrumented-assets"),
            self.android.needs("foundry-java-command-first"),
        )
        self.assertEqual(
            "linuxbsd-editor-foundry-java-command-first",
            self.android.step_with("compile-instrumented-assets", "Upload command-first editor")["name"],
        )
        self.assertEqual(
            "linuxbsd-editor-foundry-java-command-first",
            self.android.step_with("foundry-java-command-first", "Download command-first editor")["name"],
        )
        self.assertEqual(
            "foundry-java-command-first-exports",
            self.android.step_with("foundry-java-command-first", "Upload command-first APKs and evidence")["name"],
        )

    def test_device_acceptance_runs_assembled_runtime_on_api_36(self) -> None:
        self.assertEqual(
            ("assemble-android", "compile-instrumented-assets", "foundry-java-command-first"),
            self.android.needs("device-acceptance"),
        )
        self.assertEqual(
            "${{ always() && needs.assemble-android.result == 'success' "
            "&& needs.compile-instrumented-assets.result == 'success' }}",
            self.android.job_if("device-acceptance"),
        )
        self.assertEqual(120, self.android.job_key("device-acceptance", "timeout-minutes"))
        for step_name, artifact in (
            ("Download assembled Android templates", "android-templates-assembled"),
            ("Download compiled instrumented assets", "android-instrumented-assets"),
            ("Download command-first Foundry-Java APKs", "foundry-java-command-first-exports"),
        ):
            with self.subTest(step=step_name):
                self.assertEqual(artifact, self.android.step_with("device-acceptance", step_name)["name"])

        emulator = self.android.step_run("device-acceptance", "Install Android emulator")
        self.assertIn("system-images;android-36;default;x86_64", emulator)
        self.assertIn("sudo chmod 666 /dev/kvm", emulator)
        self.assertIn(
            "sys.boot_completed",
            self.android.step_run("device-acceptance", "Wait for observable emulator boot"),
        )
        acceptance = self.android.step_run("device-acceptance", "Run source-template device acceptance")
        for fragment in (
            "android_device_acceptance.py source-template",
            "--compiled-assets",
            "--serial emulator-5554",
            "--process-timeout 120",
        ):
            with self.subTest(fragment=fragment):
                self.assertIn(fragment, acceptance)

    def test_android_java_check_calls_the_authoritative_reusable_workflow(self) -> None:
        self.assertEqual("./.github/workflows/android_builds.yml", self.android_java.uses("android"))
        self.assertEqual(
            {"checkout-ref": "${{ github.event.pull_request.head.sha || github.sha }}"},
            self.android_java.with_inputs("android"),
        )
        # A second build authority would start here, so the job must stay a pure call.
        self.assertNotIn("steps", self.android_java.job("android"))

    def test_android_native_builds_preserve_clean_source_identity(self) -> None:
        action = workflow_graph.load(REPO_ROOT / ".github/actions/foundry-build/action.yml")
        self.assertIn("preserve-source-tree", action.action_inputs())

    def test_release_uses_the_complete_matrix_and_in_tree_assembly_gate(self) -> None:
        self.assert_native_matrix(self.release, "build-android-native", "release-android-native-")
        self.assert_in_tree_assembly(
            self.release,
            job="assemble-android",
            native_job="build-android-native",
            artifact_pattern="release-android-native-*",
        )
        self.assertEqual(
            "release-android-template-debug",
            self.release.step_with("assemble-android", "Upload debug Android template")["name"],
        )
        release_upload = self.release.step_with("assemble-android", "Upload release Android template and source")
        self.assertEqual("release-android-template-release", release_upload["name"])
        self.assertEqual(["bin/android_release.apk", "bin/android_source.zip"], release_upload["path"].split())

        needs = self.release.needs("package")
        self.assertIn("assemble-android", needs)
        self.assertNotIn("build-android", needs)
        self.assertNotIn("build-android-native", needs)

    def test_workflows_have_no_standalone_runtime_coupling(self) -> None:
        for name, workflow in (
            ("android_builds.yml", self.android),
            ("android_java_check.yml", self.android_java),
            ("release.yml", self.release),
        ):
            with self.subTest(workflow=name):
                self.assert_no_scalar_contains(workflow, FORBIDDEN_EXTERNAL_COUPLING)

    def test_the_release_workflow_publishes_no_android_library(self) -> None:
        self.assert_no_scalar_contains(self.release, FORBIDDEN_RELEASE_PUBLICATION)


class FullSuiteDisplayWorkflowTests(WorkflowContractTestCase):
    FULL_SUITES = (
        ("linux_builds.yml", "build-linux"),
        ("sanitizer_tests.yml", "sanitizer-tests"),
    )

    def test_every_full_suite_runs_with_a_virtual_display(self) -> None:
        for workflow_name, job_name in self.FULL_SUITES:
            workflow = load_workflow(workflow_name)
            with self.subTest(workflow=workflow_name):
                setup_commands = shell_commands(workflow.step_run(job_name, "Setup dependencies"))
                install_command = next(command for command in setup_commands if "install" in command)
                self.assertIn("xvfb", install_command)

                unit_test_commands = shell_commands(workflow.step_run(job_name, "Unit tests"))
                test_command = next(
                    command
                    for command in unit_test_commands
                    if any(left == "test" and right == "run" for left, right in zip(command, command[1:]))
                )
                self.assertEqual(["xvfb-run", "-a"], test_command[:2])
                self.assertLess(test_command.index("--headless"), test_command.index("test"))


class MacosBuildsWorkflowTests(WorkflowContractTestCase):
    def test_macos_ci_runs_the_local_input_system_alias_regressions(self) -> None:
        """macOS aliases `/tmp` and `/var`, so path validation needs a macOS runner."""

        workflow = load_workflow("macos_builds.yml")
        step_name = "Foundry-Java macOS local input path aliases"
        self.assertEqual("matrix.target == 'editor'", workflow.step_if("build-macos", step_name))
        command = workflow.step_run("build-macos", step_name)
        for regression in (
            "test_command_first_export_accepts_macos_system_temp_alias_for_local_inputs",
            "test_command_first_export_rejects_user_symlinks_below_macos_system_temp_alias",
            "test_command_first_export_rejects_user_symlink_after_macos_alias_parent_component",
        ):
            with self.subTest(regression=regression):
                self.assertIn(f"FoundryJavaExporterContractTests.{regression}", command)


class PreCommitRegistrationTests(WorkflowContractTestCase):
    def test_one_hook_runs_every_workflow_graph_contract(self) -> None:
        hook = local_hook(WORKFLOW_GRAPH_HOOK)
        self.assertIn("test_workflow_graph", hook["entry"])
        self.assertIs(False, hook["pass_filenames"])
        self.assert_hook_covers(
            hook,
            (
                ".pre-commit-config.yaml",
                ".github/actions/foundry-build/action.yml",
                ".github/scripts/workflow_graph.py",
                ".github/scripts/tests/test_workflow_graph.py",
                ".github/scripts/tests/test_workflow_graph_contracts.py",
                ".github/workflows/android_builds.yml",
                ".github/workflows/android_java_check.yml",
                ".github/workflows/ios_builds.yml",
                ".github/workflows/linux_builds.yml",
                ".github/workflows/macos_builds.yml",
                ".github/workflows/pr_editor_artifacts.yml",
                ".github/workflows/pr_platform_checks.yml",
                ".github/workflows/release.yml",
                ".github/workflows/web_builds.yml",
                ".github/workflows/windows_builds.yml",
            ),
        )

    def test_actionlint_gates_every_workflow_and_composite_action(self) -> None:
        repository, hook = remote_hook("https://github.com/rhysd/actionlint", "actionlint")
        self.assertRegex(str(repository["rev"]), r"^v\d+\.\d+\.\d+$")
        trigger = re.compile(str(hook["files"]))
        for relative_path in (
            ".github/workflows/release.yml",
            ".github/workflows/android_builds.yml",
            ".github/workflows/static_checks.yml",
        ):
            with self.subTest(relative_path=relative_path):
                self.assertRegex(relative_path, trigger)
        # actionlint parses workflow files only; a composite action has no `jobs:`
        # section and is rejected as a malformed workflow, so it stays out of scope.
        self.assertNotRegex(".github/actions/foundry-build/action.yml", trigger)
        self.assertNotRegex(".pre-commit-config.yaml", trigger)

    def test_the_foundry_java_export_hook_runs_the_contract_classes(self) -> None:
        hook = local_hook("foundry-java-android-export")
        self.assertEqual(
            [
                "tests.python_build.test_android_foundry_java_export.FoundryJavaPinTests",
                "tests.python_build.test_android_foundry_java_export.FoundryJavaSourceTemplateResourceTests",
                "tests.python_build.test_android_foundry_java_export.FoundryJavaFinalArtifactInspectorTests",
            ],
            hook["args"],
        )

    def test_the_release_container_hook_runs_the_extracted_helper_tests(self) -> None:
        hook = local_hook("foundry-release-container-scripts")
        self.assertIn("unittest discover -s .github/scripts/tests", hook["entry"])
        self.assert_hook_covers(
            hook,
            (
                ".github/scripts/resolve_container_alias.py",
                ".github/scripts/resolve_channel_tag_freshness.sh",
                ".github/scripts/verify_container_version_metadata.py",
                ".github/scripts/verify_headless_image.sh",
                ".github/scripts/tests/test_resolve_container_alias.py",
            ),
        )

    def test_the_tmlanguage_hooks_cover_every_input_they_reconcile(self) -> None:
        drift = local_hook("foundry-foundryscript-tmlanguage-drift")
        self.assertIn("check_foundryscript_tmlanguage.py", drift["entry"])
        self.assertIs(False, drift["pass_filenames"])
        self.assert_hook_covers(
            drift,
            (
                "misc/checks/check_foundryscript_tmlanguage.py",
                "modules/foundry_script/GRAMMAR.md",
                "modules/foundry_script/fs_tokenizer.cpp",
                "modules/foundry_script/grammar/tmlanguage_builder.py",
                "modules/foundry_script/grammar/patterns/keyword_scopes.json",
                "modules/foundry_script/grammar/patterns/lexical.json",
                "modules/foundry_script/grammar/patterns/constructs.json",
            ),
        )

        tokenization = local_hook("foundry-foundryscript-tmlanguage")
        self.assertEqual(["tests.python_build.test_foundryscript_tmlanguage"], tokenization["args"])
        # Python `re` cannot evaluate these patterns, so the real engine is a hard dependency.
        self.assertEqual(["onigurumacffi"], tokenization["additional_dependencies"])
        self.assert_hook_covers(
            tokenization,
            (
                "modules/foundry_script/grammar/tmlanguage_builder.py",
                "modules/foundry_script/grammar/fixtures/highlighting_sample.fs",
                "tests/python_build/textmate_tokenizer.py",
                "tests/python_build/test_foundryscript_tmlanguage.py",
            ),
        )


class ReleaseTmlanguageWorkflowTests(WorkflowContractTestCase):
    """The generated grammar has to reach packaging and the published release."""

    workflow: workflow_graph.Workflow

    @classmethod
    def setUpClass(cls) -> None:
        cls.workflow = load_workflow("release.yml")

    def test_the_grammar_is_generated_before_the_build_matrix(self) -> None:
        # `resolve` needs nothing, so a broken generator fails in seconds.
        self.assertIs(False, self.workflow.has_needs("resolve"))
        generate = "Generate FoundryScript TextMate grammar"
        self.assertLess(
            self.workflow.step_index("resolve", "Resolve version and publish mode"),
            self.workflow.step_index("resolve", generate),
        )
        run = self.workflow.step_run("resolve", generate)
        self.assertIn("modules/foundry_script/grammar/tmlanguage_builder.py", run)
        self.assertIn("foundryscript-tmlanguage-${VERSION}.json", run)
        self.assertEqual(
            "${{ steps.resolve.outputs.release_version }}",
            self.workflow.step_key("resolve", generate, "env")["VERSION"],
        )

    def test_the_grammar_is_uploaded_under_its_own_artifact_name(self) -> None:
        upload = self.workflow.step("resolve", "Upload FoundryScript TextMate grammar")
        self.assertEqual("./.github/actions/upload-artifact", upload["uses"])
        self.assertEqual("release-tmlanguage", upload["with"]["name"])
        self.assertEqual("tmlanguage/*", upload["with"]["path"])

    def test_packaging_copies_the_grammar_into_the_published_asset_set(self) -> None:
        self.assertIn("resolve", self.workflow.needs("package"))
        assemble = self.workflow.step_run("package", "Assemble release assets")
        self.assertIn("cp artifacts/release-tmlanguage/foundryscript-tmlanguage-*.json dist/", assemble)
        self.assertEqual("dist/*", self.workflow.step_with("package", "Upload release assets")["path"])

    def test_publication_uploads_every_packaged_asset(self) -> None:
        self.assertEqual(("resolve", "package"), self.workflow.needs("publish"))
        download = self.workflow.step("publish", "Download release assets")
        self.assertEqual("release-assets", download["with"]["name"])
        self.assertEqual("dist", download["with"]["path"])
        self.assertEqual("dist/*", self.workflow.step_with("publish", "Publish GitHub Release")["files"])


if __name__ == "__main__":
    unittest.main()
