import subprocess
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VERSION_SCRIPT = ROOT / "scripts" / "next_release_version.py"
RELEASE_WORKFLOW = ROOT / ".github" / "workflows" / "release.yml"


class NextReleaseVersionTest(unittest.TestCase):
    def next_version(self, branch: str, tags: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(VERSION_SCRIPT), branch],
            input=tags,
            text=True,
            capture_output=True,
        )

    def released_version(self, branch: str, tags: str) -> str:
        result = subprocess.run(
            [sys.executable, str(VERSION_SCRIPT), branch],
            input=tags,
            text=True,
            capture_output=True,
            check=True,
        )
        return result.stdout.strip()

    def test_first_release_starts_at_0_1_0(self) -> None:
        self.assertEqual(self.released_version("feat/004-automatic-releases", ""), "0.1.0")

    def test_feature_branch_increments_minor_version(self) -> None:
        tags = "v0.1.9\nv0.2.0\nv0.1.10\nnot-a-release\n"
        self.assertEqual(self.released_version("feat/009-launcher", tags), "0.3.0")

    def test_fix_branch_increments_patch_version(self) -> None:
        self.assertEqual(self.released_version("fix/010-crash", "v1.7.4\n"), "1.7.5")

    def test_breaking_branch_increments_major_version(self) -> None:
        self.assertEqual(self.released_version("breaking/011-storage", "v1.7.4\n"), "2.0.0")

    def test_non_release_branch_returns_skip_status(self) -> None:
        result = self.next_version("docs/012-readme", "v1.7.4\n")
        self.assertEqual(result.returncode, 3)
        self.assertEqual(result.stdout, "")


class ReleaseWorkflowTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.workflow = RELEASE_WORKFLOW.read_text()

    def test_successful_main_push_ci_triggers_release(self) -> None:
        self.assertIn("workflow_run:", self.workflow)
        self.assertIn('workflows: ["CI"]', self.workflow)
        self.assertIn("github.event.workflow_run.conclusion == 'success'", self.workflow)
        self.assertIn("github.event.workflow_run.event == 'push'", self.workflow)
        self.assertIn("github.event.workflow_run.head_branch == 'main'", self.workflow)

    def test_release_uses_the_validated_commit(self) -> None:
        self.assertIn("github.event.workflow_run.head_sha", self.workflow)

    def test_release_uses_the_merged_pull_request_branch(self) -> None:
        self.assertIn("commits/${VALIDATED_COMMIT}/pulls", self.workflow)
        self.assertIn("steps.release.outputs.should_release == 'true'", self.workflow)

    def test_only_two_published_releases_are_retained(self) -> None:
        self.assertIn("if ((release_number > 2)); then", self.workflow)
        self.assertIn("preserving its Git tag", self.workflow)


if __name__ == "__main__":
    unittest.main()
