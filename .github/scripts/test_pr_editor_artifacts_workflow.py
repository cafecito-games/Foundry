from pathlib import Path
import unittest


WORKFLOW_PATH = Path(__file__).resolve().parents[1] / "workflows" / "pr_editor_artifacts.yml"


class PrEditorArtifactsWorkflowTest(unittest.TestCase):
    def test_build_job_comments_when_artifact_build_starts(self):
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")

        existing_comment = workflow.find("- name: Comment on existing artifact")
        start_comment = workflow.find("- name: Comment on started artifact build")
        checkout_pr_head = workflow.find("- name: Checkout pull request head")

        self.assertNotEqual(existing_comment, -1)
        self.assertNotEqual(start_comment, -1)
        self.assertNotEqual(checkout_pr_head, -1)
        self.assertLess(existing_comment, start_comment)
        self.assertLess(start_comment, checkout_pr_head)

        start_comment_block = workflow[start_comment:checkout_pr_head]
        self.assertIn("if: ${{ steps.existing.outputs.found != 'true' }}", start_comment_block)
        self.assertIn("Building the ${LABEL} editor", start_comment_block)
        self.assertIn("Workflow run: ${RUN_URL}", start_comment_block)


if __name__ == "__main__":
    unittest.main()
