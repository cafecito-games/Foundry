import unittest

import pr_platform_check_request as request


class PrPlatformCheckRequestTest(unittest.TestCase):
    def test_requested_platforms_accepts_every_platform_commands(self):
        self.assertEqual(
            request.requested_platforms("check every platform"),
            ["linux", "macos", "windows", "android", "ios", "web"],
        )
        self.assertEqual(
            request.requested_platforms("Please CHECK every PLATFORM before merge."),
            ["linux", "macos", "windows", "android", "ios", "web"],
        )
        self.assertEqual(
            request.requested_platforms("build every platform"),
            ["linux", "macos", "windows", "android", "ios", "web"],
        )

    def test_requested_platforms_accepts_specific_platform_commands(self):
        self.assertEqual(request.requested_platforms("check linux"), ["linux"])
        self.assertEqual(request.requested_platforms("Check LinuxBSD"), ["linux"])
        self.assertEqual(request.requested_platforms("check macOS"), ["macos"])
        self.assertEqual(request.requested_platforms("check mac editor and check web"), ["macos", "web"])
        self.assertEqual(
            request.requested_platforms("check windows, check android, check ios"),
            ["windows", "android", "ios"],
        )

    def test_requested_platforms_ignores_unsupported_or_unrelated_comments(self):
        self.assertEqual(request.requested_platforms("check playstation"), [])
        self.assertEqual(request.requested_platforms("check the linux platform"), [])
        self.assertEqual(request.requested_platforms("the platform build failed"), [])
        self.assertEqual(request.requested_platforms("build every platforms"), [])

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
        )

        self.assertTrue(plan["request_found"])
        self.assertTrue(plan["requested"])
        self.assertTrue(plan["authorized"])
        self.assertEqual(plan["pr_number"], "42")
        self.assertEqual(plan["pr_head_sha"], "0123456789abcdef0123456789abcdef01234567")
        self.assertEqual(plan["pr_short_sha"], "0123456789ab")
        self.assertEqual(plan["platforms_label"], "Linux, macOS, Windows, Android, iOS, and Web")
        for platform in ("linux", "macos", "windows", "android", "ios", "web"):
            with self.subTest(platform=platform):
                self.assertEqual(plan[f"run_{platform}"], "true")

    def test_build_plan_emits_authorized_specific_platform_outputs(self):
        plan = request.build_plan(
            body="check linux and check web",
            permission="write",
            is_pr=True,
            pr_number=42,
            head_sha="0123456789abcdef0123456789abcdef01234567",
        )

        self.assertTrue(plan["request_found"])
        self.assertTrue(plan["requested"])
        self.assertTrue(plan["authorized"])
        self.assertEqual(plan["platforms_label"], "Linux and Web")
        self.assertEqual(plan["run_linux"], "true")
        self.assertEqual(plan["run_web"], "true")
        for platform in ("macos", "windows", "android", "ios"):
            with self.subTest(platform=platform):
                self.assertEqual(plan[f"run_{platform}"], "false")

    def test_build_plan_marks_unauthorized_requests(self):
        plan = request.build_plan(
            body="check every platform",
            permission="read",
            is_pr=True,
            pr_number=42,
            head_sha="0123456789abcdef0123456789abcdef01234567",
        )

        self.assertTrue(plan["request_found"])
        self.assertTrue(plan["requested"])
        self.assertFalse(plan["authorized"])
        for platform in ("linux", "macos", "windows", "android", "ios", "web"):
            with self.subTest(platform=platform):
                self.assertEqual(plan[f"run_{platform}"], "false")

    def test_build_plan_ignores_matching_comments_outside_pull_requests(self):
        plan = request.build_plan(
            body="build every platform",
            permission="write",
            is_pr=False,
            pr_number=42,
            head_sha="0123456789abcdef0123456789abcdef01234567",
        )

        self.assertTrue(plan["request_found"])
        self.assertFalse(plan["requested"])
        self.assertFalse(plan["authorized"])


if __name__ == "__main__":
    unittest.main()
