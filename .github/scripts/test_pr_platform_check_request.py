import unittest

import pr_platform_check_request as request


class PrPlatformCheckRequestTest(unittest.TestCase):
    def test_request_detection_accepts_every_platform_commands(self):
        self.assertTrue(request.is_every_platform_request("check every platform"))
        self.assertTrue(request.is_every_platform_request("Please CHECK every PLATFORM before merge."))
        self.assertTrue(request.is_every_platform_request("build every platform"))

    def test_request_detection_ignores_unrelated_comments(self):
        self.assertFalse(request.is_every_platform_request("check the linux platform"))
        self.assertFalse(request.is_every_platform_request("the platform build failed"))

    def test_has_write_access_accepts_write_level_permissions(self):
        for permission in ("write", "maintain", "admin"):
            with self.subTest(permission=permission):
                self.assertTrue(request.has_write_access(permission))

        for permission in ("", "none", "read", "triage"):
            with self.subTest(permission=permission):
                self.assertFalse(request.has_write_access(permission))

    def test_build_plan_emits_authorized_pr_metadata(self):
        plan = request.build_plan(
            body="check every platform",
            permission="write",
            is_pr=True,
            pr_number=42,
            head_sha="0123456789abcdef0123456789abcdef01234567",
            head_repo="cafecito-games/Foundry",
        )

        self.assertTrue(plan["request_found"])
        self.assertTrue(plan["requested"])
        self.assertTrue(plan["authorized"])
        self.assertEqual(plan["pr_number"], "42")
        self.assertEqual(plan["pr_head_sha"], "0123456789abcdef0123456789abcdef01234567")
        self.assertEqual(plan["pr_short_sha"], "0123456789ab")
        self.assertEqual(plan["pr_head_repo"], "cafecito-games/Foundry")

    def test_build_plan_marks_unauthorized_requests(self):
        plan = request.build_plan(
            body="check every platform",
            permission="read",
            is_pr=True,
            pr_number=42,
            head_sha="0123456789abcdef0123456789abcdef01234567",
            head_repo="cafecito-games/Foundry",
        )

        self.assertTrue(plan["request_found"])
        self.assertTrue(plan["requested"])
        self.assertFalse(plan["authorized"])

    def test_build_plan_ignores_matching_comments_outside_pull_requests(self):
        plan = request.build_plan(
            body="build every platform",
            permission="write",
            is_pr=False,
            pr_number=42,
            head_sha="0123456789abcdef0123456789abcdef01234567",
            head_repo="cafecito-games/Foundry",
        )

        self.assertTrue(plan["request_found"])
        self.assertFalse(plan["requested"])
        self.assertFalse(plan["authorized"])


if __name__ == "__main__":
    unittest.main()
