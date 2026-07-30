import re
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = REPO_ROOT / ".github/workflows/pr_platform_checks.yml"
PRE_COMMIT = REPO_ROOT / ".pre-commit-config.yaml"
PLATFORM_WORKFLOWS = [
    ".github/workflows/linux_builds.yml",
    ".github/workflows/macos_builds.yml",
    ".github/workflows/windows_builds.yml",
    ".github/workflows/android_builds.yml",
    ".github/workflows/ios_builds.yml",
    ".github/workflows/web_builds.yml",
]
PLATFORM_JOBS = ("linux", "macos", "windows", "android", "ios", "web")
PRE_COMMIT_HOOK_ID = "foundry-pr-platform-checks-workflow"
FINISH_IF = (
    "if: ${{ always() && needs.preflight.outputs.requested == 'true' && needs.preflight.outputs.authorized == 'true' }}"
)
# Reusable platform workflows always check out an explicit commit. `github.sha` pins
# the commit that triggered the run; `github.ref` resolves late, so a branch or
# `refs/pull/N/merge` that moves mid-run can hand different jobs different trees.
CANONICAL_CHECKOUT_REF = "ref: ${{ inputs.checkout-ref || github.sha }}"
CHECKOUT_REF_LINE = re.compile(r"^[ \t]*ref: .*inputs\.checkout-ref.*$", re.MULTILINE)
CHECKOUT_STEP = re.compile(r"^[ \t]*uses: actions/checkout@", re.MULTILINE)
FOREIGN_REPOSITORY = re.compile(r"^[ \t]*repository: ", re.MULTILINE)


class PrPlatformChecksWorkflowTest(unittest.TestCase):
    def test_workflow_gates_platform_jobs_on_authorized_request(self):
        workflow = WORKFLOW.read_text(encoding="utf-8")

        self.assertIn("on:\n  issue_comment:\n    types: [created]", workflow)
        self.assertIn("python3 .github/scripts/pr_platform_check_request.py plan", workflow)
        self.assertIn("Comment on unauthorized request", workflow)
        self.assertIn("Comment on accepted request", workflow)
        self.assertIn("platforms_label: ${{ steps.plan.outputs.platforms_label }}", workflow)
        self.assertLess(workflow.find("Comment on accepted request"), workflow.find("\n  linux:"))

        for job in PLATFORM_JOBS:
            with self.subTest(job=job):
                concurrency_group = f"group: pr-every-platform-{job}-${{{{ needs.preflight.outputs.pr_head_sha }}}}"
                job_if = (
                    "if: ${{ needs.preflight.outputs.authorized == 'true' "
                    f"&& needs.preflight.outputs.run_{job} == 'true' }}}}"
                )
                self.assertIn(f"{job}:", workflow)
                self.assertIn(f"run_{job}: ${{{{ steps.plan.outputs.run_{job} }}}}", workflow)
                self.assertIn(job_if, workflow)
                self.assertIn(concurrency_group, workflow)

    def test_workflow_calls_every_platform_with_pr_head_ref_checkout(self):
        workflow = WORKFLOW.read_text(encoding="utf-8")

        self.assertNotIn("pr_head_repo", workflow)
        self.assertNotIn("checkout-repository:", workflow)
        for platform_workflow in PLATFORM_WORKFLOWS:
            with self.subTest(platform_workflow=platform_workflow):
                call = f"uses: ./{platform_workflow}"
                self.assertIn(call, workflow)
                call_block = workflow[workflow.find(call) : workflow.find(call) + 300]
                self.assertIn("checkout-ref: ${{ needs.preflight.outputs.pr_head_sha }}", call_block)

    def test_workflow_posts_completion_comment_after_platform_jobs(self):
        workflow = WORKFLOW.read_text(encoding="utf-8")

        self.assertIn("finish:", workflow)
        self.assertIn("needs: [preflight, linux, macos, windows, android, ios, web]", workflow)
        self.assertIn(FINISH_IF, workflow)
        self.assertIn("PLATFORMS_LABEL: ${{ needs.preflight.outputs.platforms_label }}", workflow)
        for job in PLATFORM_JOBS:
            with self.subTest(job=job):
                self.assertIn(f"{job}: ${{{{ needs.{job}.result }}}}", workflow)
                self.assertIn(f"run_{job}: ${{{{ needs.preflight.outputs.run_{job} }}}}", workflow)

    def test_platform_workflows_accept_checkout_inputs(self):
        for relative_path in PLATFORM_WORKFLOWS:
            with self.subTest(relative_path=relative_path):
                workflow = (REPO_ROOT / relative_path).read_text(encoding="utf-8")
                self.assertNotIn("checkout-repository:", workflow)
                self.assertNotIn("repository: ${{ inputs.checkout-repository || github.repository }}", workflow)
                self.assertIn("checkout-ref:", workflow)
                self.assertIn(f"          {CANONICAL_CHECKOUT_REF}", workflow)

    def test_platform_workflows_pin_every_own_checkout_to_the_same_ref(self):
        """Every self-checkout uses the canonical ref, not just the first one."""

        for relative_path in PLATFORM_WORKFLOWS:
            with self.subTest(relative_path=relative_path):
                workflow = (REPO_ROOT / relative_path).read_text(encoding="utf-8")
                ref_lines = CHECKOUT_REF_LINE.findall(workflow)

                for ref_line in ref_lines:
                    self.assertEqual(CANONICAL_CHECKOUT_REF, ref_line.strip())

                # Steps that check out another repository pin their own ref, so only the
                # remaining checkouts are expected to consume `checkout-ref`.
                own_checkouts = len(CHECKOUT_STEP.findall(workflow)) - len(FOREIGN_REPOSITORY.findall(workflow))
                self.assertEqual(own_checkouts, len(ref_lines))

    def test_pre_commit_runs_the_platform_check_contract(self):
        pre_commit = PRE_COMMIT.read_text(encoding="utf-8")

        self.assertIn(f"- id: {PRE_COMMIT_HOOK_ID}", pre_commit)
        self.assertIn("entry: python .github/scripts/test_pr_platform_checks_workflow.py", pre_commit)

        trigger = re.compile(self._pre_commit_hook_files_pattern(pre_commit), re.VERBOSE)
        for relative_path in [
            *PLATFORM_WORKFLOWS,
            ".github/workflows/pr_platform_checks.yml",
            ".github/scripts/test_pr_platform_checks_workflow.py",
            ".pre-commit-config.yaml",
        ]:
            with self.subTest(relative_path=relative_path):
                self.assertRegex(relative_path, trigger)

    def _pre_commit_hook_files_pattern(self, pre_commit: str) -> str:
        """Return the literal `files:` regex the platform-check hook is configured with."""

        hook_start = pre_commit.index(f"- id: {PRE_COMMIT_HOOK_ID}")
        next_hook = pre_commit.find("\n      - id: ", hook_start)
        hook = pre_commit[hook_start : next_hook if next_hook != -1 else len(pre_commit)]

        lines = hook.splitlines()
        files_index = next(index for index, line in enumerate(lines) if line.strip() == "files: |")
        block = []
        for line in lines[files_index + 1 :]:
            if line.strip() and not line.startswith(" " * 10):
                break
            block.append(line.strip())
        return "".join(block)


if __name__ == "__main__":
    unittest.main()
