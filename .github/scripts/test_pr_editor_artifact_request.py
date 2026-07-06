import json
import unittest

import pr_editor_artifact_request as request


class PrEditorArtifactRequestTest(unittest.TestCase):
    def test_requested_platforms_accepts_supported_editor_comments(self):
        self.assertEqual(request.requested_platforms("Build me a macOS editor"), ["macos"])
        self.assertEqual(request.requested_platforms("build me linux editor"), ["linux"])
        self.assertEqual(
            request.requested_platforms("Please build me a Linux editor and build me a macOS editor."),
            ["linux", "macos"],
        )
        self.assertEqual(
            request.requested_platforms("Build me a macOS editor and a linux editor."),
            ["macos", "linux"],
        )

    def test_requested_platforms_ignores_unsupported_or_unrelated_comments(self):
        self.assertEqual(request.requested_platforms("Build me a windows editor"), [])
        self.assertEqual(request.requested_platforms("The macOS editor build failed"), [])

    def test_has_write_access_accepts_write_level_permissions(self):
        for permission in ("write", "maintain", "admin"):
            with self.subTest(permission=permission):
                self.assertTrue(request.has_write_access(permission))

        for permission in ("", "none", "read", "triage"):
            with self.subTest(permission=permission):
                self.assertFalse(request.has_write_access(permission))

    def test_artifact_name_is_deterministic_for_pr_sha_and_platform(self):
        self.assertEqual(
            request.artifact_name(42, "0123456789abcdef0123456789abcdef01234567", "macos"),
            "pr-42-0123456789ab-macos-editor",
        )

    def test_build_plan_emits_authorized_matrix_for_requested_platforms(self):
        plan = request.build_plan(
            body="Build me a linux editor and build me a macOS editor",
            permission="write",
            is_pr=True,
            pr_number=42,
            head_sha="0123456789abcdef0123456789abcdef01234567",
            head_repo="cafecito-games/Foundry",
        )

        self.assertTrue(plan["request_found"])
        self.assertTrue(plan["requested"])
        self.assertTrue(plan["authorized"])
        self.assertEqual(plan["pr_short_sha"], "0123456789ab")
        self.assertEqual(plan["platforms_label"], "Linux and macOS")

        matrix = json.loads(plan["matrix"])
        self.assertEqual(
            matrix,
            {
                "include": [
                    {
                        "os": "linux",
                        "label": "Linux",
                        "runner": "ubuntu-22.04",
                        "artifact_name": "pr-42-0123456789ab-linux-editor",
                        "artifact_path": "bin/foundry.linuxbsd.editor.dev.x86_64",
                    },
                    {
                        "os": "macos",
                        "label": "macOS",
                        "runner": "macos-latest",
                        "artifact_name": "pr-42-0123456789ab-macos-editor",
                        "artifact_path": "bin/foundry.macos.editor.universal",
                    },
                ]
            },
        )

    def test_build_plan_marks_unauthorized_requests(self):
        plan = request.build_plan(
            body="Build me a linux editor",
            permission="read",
            is_pr=True,
            pr_number=42,
            head_sha="0123456789abcdef0123456789abcdef01234567",
            head_repo="cafecito-games/Foundry",
        )

        self.assertTrue(plan["request_found"])
        self.assertTrue(plan["requested"])
        self.assertFalse(plan["authorized"])
        self.assertEqual(json.loads(plan["matrix"]), {"include": []})

    def test_build_plan_ignores_matching_comments_outside_pull_requests(self):
        plan = request.build_plan(
            body="Build me a linux editor",
            permission="write",
            is_pr=False,
            pr_number=42,
            head_sha="0123456789abcdef0123456789abcdef01234567",
            head_repo="cafecito-games/Foundry",
        )

        self.assertTrue(plan["request_found"])
        self.assertFalse(plan["requested"])
        self.assertFalse(plan["authorized"])

    def test_find_existing_artifact_returns_non_expired_exact_name(self):
        artifacts = [
            {
                "id": 100,
                "name": "pr-42-0123456789ab-linux-editor",
                "expired": True,
                "workflow_run": {"id": 500},
            },
            {
                "id": 101,
                "name": "pr-42-0123456789ab-linux-editor-extra",
                "expired": False,
                "workflow_run": {"id": 501},
            },
            {
                "id": 102,
                "name": "pr-42-0123456789ab-linux-editor",
                "expired": False,
                "workflow_run": {"id": 502, "html_url": "https://github.com/cafecito-games/Foundry/actions/runs/502"},
            },
        ]

        existing = request.find_existing_artifact(
            artifacts,
            artifact_name="pr-42-0123456789ab-linux-editor",
            repository="cafecito-games/Foundry",
        )

        self.assertEqual(existing["id"], 102)
        self.assertEqual(
            existing["artifact_url"],
            "https://github.com/cafecito-games/Foundry/actions/runs/502/artifacts/102",
        )
        self.assertEqual(existing["run_url"], "https://github.com/cafecito-games/Foundry/actions/runs/502")


if __name__ == "__main__":
    unittest.main()
