from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = REPO_ROOT / ".github/workflows/pr_platform_checks.yml"
PLATFORM_WORKFLOWS = [
    ".github/workflows/linux_builds.yml",
    ".github/workflows/macos_builds.yml",
    ".github/workflows/windows_builds.yml",
    ".github/workflows/android_builds.yml",
    ".github/workflows/ios_builds.yml",
    ".github/workflows/web_builds.yml",
]
FINISH_IF = (
    "if: ${{ always() && needs.preflight.outputs.requested == 'true' "
    "&& needs.preflight.outputs.authorized == 'true' }}"
)


class PrPlatformChecksWorkflowTest(unittest.TestCase):
    def test_workflow_gates_platform_jobs_on_authorized_request(self):
        workflow = WORKFLOW.read_text(encoding="utf-8")

        self.assertIn("on:\n  issue_comment:\n    types: [created]", workflow)
        self.assertIn("python3 .github/scripts/pr_platform_check_request.py plan", workflow)
        self.assertIn("Comment on unauthorized request", workflow)
        self.assertIn("Comment on accepted request", workflow)
        self.assertIn("platforms_label: ${{ steps.plan.outputs.platforms_label }}", workflow)
        self.assertLess(workflow.find("Comment on accepted request"), workflow.find("\n  linux:"))

        for job in ("linux", "macos", "windows", "android", "ios", "web"):
            with self.subTest(job=job):
                concurrency_group = f"group: pr-every-platform-{job}-${{{{ needs.preflight.outputs.pr_head_sha }}}}"
                job_if = f"if: ${{{{ needs.preflight.outputs.run_{job} == 'true' }}}}"
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
        for job in ("linux", "macos", "windows", "android", "ios", "web"):
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
                self.assertIn("ref: ${{ inputs.checkout-ref || github.ref }}", workflow)


if __name__ == "__main__":
    unittest.main()
