"""Exercise packaging hooks against temporary directories only."""
import pathlib
import subprocess
import tempfile
import unittest


class ScheduleMigration(unittest.TestCase):
    def test_arch_removal(self):
        repo = pathlib.Path(__file__).resolve().parents[1]
        for hook in ("arch/mx-cleanup.install", "mx-cleanup.install"):
            with self.subTest(hook=hook), tempfile.TemporaryDirectory() as root:
                root = pathlib.Path(root)
                for period in ("daily", "weekly", "monthly", "d"):
                    directory = root / "etc" / f"cron.{period}"
                    directory.mkdir(parents=True)
                    for name in ("mx-cleanup", "mx-cleanup.alice", "mx-cleanup-alice"):
                        (directory / name).touch()
                    (directory / "unrelated").touch()
                scripts = root / "usr/bin"
                scripts.mkdir(parents=True)
                for name in ("mx-cleanup-script", "mx-cleanup-script.alice",
                             "mx-cleanup-system-script"):
                    (scripts / name).touch()
                text = (repo / hook).read_text()
                text = text.replace("/etc/cron.", f"{root}/etc/cron.")
                text = text.replace("/usr/bin/", f"{root}/usr/bin/")
                subprocess.run(["sh", "-c", text + "\npost_remove\n"], check=True)
                self.assertFalse(list(root.glob("etc/cron.*/mx-cleanup*")))
                self.assertEqual(len(list(root.glob("etc/cron.*/unrelated"))), 4)
                self.assertFalse(list(scripts.iterdir()))

    def test_upgrade_and_removal(self):
        repo = pathlib.Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as root:
            root = pathlib.Path(root)
            scripts = root / "usr/bin"
            scripts.mkdir(parents=True)
            reboot_script = scripts / "mx-cleanup-script.alice.smith"
            reboot_script.write_text("#!/bin/sh\nexit 0\n")
            for period in ("daily", "weekly", "monthly", "d"):
                directory = root / "etc" / f"cron.{period}"
                directory.mkdir(parents=True)
                for name in ("alice.smith", "alice_2esmith", "u-123abc"):
                    old = directory / f"mx-cleanup.{name}"
                    content = ("@reboot root /usr/bin/mx-cleanup-script.alice.smith\n"
                               if period == "d" else
                               "#!/bin/sh\n# This file was created by MX Cleanup\nexit 0\n")
                    old.write_text(content)
                    old.chmod(0o644 if period == "d" else 0o755)
                (directory / "mx-cleanup.unrelated").write_text("unrelated\n")
                (directory / "mx-cleanup.existing").write_text(
                    "# This file was created by MX Cleanup\nold\n")
                (directory / "mx-cleanup-existing").write_text("new\n")

            def run_hook(name, argument):
                text = (repo / "debian" / name).read_text()
                text = text.replace("/etc/cron.", f"{root}/etc/cron.")
                # Only postrm deletes scripts; postinst matches literal references.
                if name == "postrm":
                    text = text.replace("/usr/bin/", f"{root}/usr/bin/")
                subprocess.run(["sh", "-c", text, name, argument], check=True)

            run_hook("postinst", "configure")
            run_hook("postinst", "configure")  # Idempotent, preserves newer entries.
            for period in ("daily", "weekly", "monthly", "d"):
                directory = root / "etc" / f"cron.{period}"
                for name in ("alice_2esmith", "alice_5f2esmith", "u-123abc"):
                    new = directory / f"mx-cleanup-{name}"
                    self.assertTrue(new.is_file())
                    self.assertEqual(new.stat().st_mode & 0o777,
                                     0o644 if period == "d" else 0o755)
                self.assertFalse((directory / "mx-cleanup.alice.smith").exists())
                self.assertEqual((directory / "mx-cleanup-existing").read_text(), "new\n")
                self.assertTrue((directory / "mx-cleanup.unrelated").exists())
            self.assertIn("/usr/bin/mx-cleanup-script.alice.smith",
                          (root / "etc/cron.d/mx-cleanup-alice_2esmith").read_text())
            self.assertTrue(reboot_script.exists())
            run_hook("postrm", "remove")
            self.assertFalse(list(root.glob("etc/cron.*/mx-cleanup*")))
            self.assertFalse(reboot_script.exists())


if __name__ == "__main__":
    unittest.main()
