import contextlib
import importlib.util
import io
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


SCRIPT = Path(__file__).parents[1] / "APPLaunch" / "bin" / "appstore.py"
SPEC = importlib.util.spec_from_file_location("appstore_backend", SCRIPT)
assert SPEC and SPEC.loader
appstore = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(appstore)


APP = {
    "id": "app-id",
    "share_code": "demo",
    "title": "Demo",
    "version": "1.2.0",
    "download": {
        "type": "deb",
        "package": "demo-package",
        "url": "https://example.invalid/demo.deb",
        "md5": "0" * 32,
    },
}


class PackageTransactionTests(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.state_dir = Path(self.temp_dir.name)
        self.env = mock.patch.dict(
            appstore.os.environ,
            {"M5APPSTORE_STATE_DIR": str(self.state_dir)},
        )
        self.env.start()
        appstore._DPKG_STATUS_CACHE = None

    def tearDown(self):
        self.env.stop()
        self.temp_dir.cleanup()

    def capture(self, function, *args, **kwargs):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            result = function(*args, **kwargs)
        return result, output.getvalue()

    def test_helper_desktop_repair_failure_is_nonfatal_warning(self):
        completed = subprocess.CompletedProcess(["apt-get"], 0, "installed\n", "")
        with (
            mock.patch.object(appstore.os, "geteuid", return_value=0),
            mock.patch.object(appstore, "deb_dependencies_satisfied", return_value=True),
            mock.patch.object(appstore.shutil, "which", return_value="/usr/bin/apt-get"),
            mock.patch.object(appstore, "run_package_helper_command", return_value=completed),
            mock.patch.object(
                appstore,
                "repair_desktop_as_root",
                side_effect=RuntimeError("desktop unavailable"),
            ),
        ):
            result, output = self.capture(
                appstore.package_helper,
                "install",
                "/tmp/demo.deb",
                desktop="/usr/share/APPLaunch/applications/demo.desktop",
            )

        self.assertEqual(result, 0)
        self.assertIn("installed", output)
        self.assertIn("WARNING\tdesktop repair failed: desktop unavailable", output)
        self.assertNotIn("ERROR\t", output)

    def test_uninstall_removes_target_without_global_dpkg_repair(self):
        completed = subprocess.CompletedProcess(
            ["dpkg", "--remove", "demo-package"], 0, "removed\n", ""
        )
        with (
            mock.patch.object(appstore.os, "geteuid", return_value=0),
            mock.patch.object(appstore.shutil, "which", return_value="/usr/bin/dpkg"),
            mock.patch.object(appstore, "run_package_helper_command", return_value=completed) as run,
        ):
            result, output = self.capture(
                appstore.package_helper, "uninstall", "demo-package"
            )

        self.assertEqual(result, 0)
        self.assertEqual(run.call_args.args[0], ["dpkg", "--remove", "demo-package"])
        self.assertIn("removed", output)

    def test_install_targets_deb_without_global_dpkg_repair(self):
        completed = subprocess.CompletedProcess(
            ["dpkg", "--install", "/tmp/demo.deb"], 0, "installed\n", ""
        )
        with (
            mock.patch.object(appstore.os, "geteuid", return_value=0),
            mock.patch.object(appstore, "deb_dependencies_satisfied", return_value=True),
            mock.patch.object(appstore.shutil, "which", return_value="/usr/bin/dpkg"),
            mock.patch.object(appstore, "run_package_helper_command", return_value=completed) as run,
        ):
            result, output = self.capture(
                appstore.package_helper, "install", "/tmp/demo.deb"
            )

        self.assertEqual(result, 0)
        self.assertEqual(run.call_args.args[0], ["dpkg", "--install", "/tmp/demo.deb"])
        self.assertIn("installed", output)

    def test_install_uses_apt_for_missing_dependencies_when_dpkg_is_clean(self):
        completed = subprocess.CompletedProcess(["apt-get"], 0, "installed\n", "")
        with (
            mock.patch.object(appstore.os, "geteuid", return_value=0),
            mock.patch.object(appstore, "deb_dependencies_satisfied", return_value=False),
            mock.patch.object(appstore, "dpkg_audit_detail", return_value=""),
            mock.patch.object(appstore.shutil, "which", return_value="/usr/bin/tool"),
            mock.patch.object(appstore, "run_package_helper_command", return_value=completed) as run,
        ):
            result, _ = self.capture(appstore.package_helper, "install", "/tmp/demo.deb")

        self.assertEqual(result, 0)
        self.assertEqual(run.call_args.args[0], ["apt-get", "-y", "install", "/tmp/demo.deb"])

    def test_install_rejects_missing_dependencies_before_unpack_when_dpkg_is_dirty(self):
        appstore.write_json(appstore.pending_package_path(), {
            "transaction_id": "tx-preflight", "action": "install",
            "app_id": "app-id", "helper_completed": False,
        })
        with (
            mock.patch.object(appstore.os, "geteuid", return_value=0),
            mock.patch.object(appstore, "deb_dependencies_satisfied", return_value=False),
            mock.patch.object(appstore, "dpkg_audit_detail", return_value="flint is half configured"),
            mock.patch.object(appstore.shutil, "which", return_value="/usr/bin/tool"),
            mock.patch.object(appstore, "run_package_helper_command") as run,
        ):
            result, output = self.capture(
                appstore.package_helper, "install", "/tmp/demo.deb",
                transaction_id="tx-preflight",
                pending_path_value=str(appstore.pending_package_path()),
            )

        self.assertEqual(result, 1)
        run.assert_not_called()
        self.assertFalse(appstore.pending_package_path().exists())
        self.assertIn("unfinished packages", output)

    def test_empty_deb_dependencies_do_not_require_dpkg_checkbuilddeps(self):
        def which(name):
            return "/usr/bin/dpkg-deb" if name == "dpkg-deb" else None

        empty = subprocess.CompletedProcess(["dpkg-deb"], 0, "", "")
        with (
            mock.patch.object(appstore.shutil, "which", side_effect=which),
            mock.patch.object(appstore.subprocess, "run", return_value=empty) as run,
        ):
            self.assertTrue(appstore.deb_dependencies_satisfied("/tmp/demo.deb"))

        self.assertEqual(run.call_count, 2)

    def test_dependency_checker_uses_all_deb_dependency_fields(self):
        results = [
            subprocess.CompletedProcess(["dpkg-deb"], 0, "base-files\n", ""),
            subprocess.CompletedProcess(["dpkg-deb"], 0, "libc6 (>= 2.36) | libc6.1\n", ""),
            subprocess.CompletedProcess(["dpkg-checkbuilddeps"], 0, "", ""),
        ]
        with (
            mock.patch.object(appstore.shutil, "which", return_value="/usr/bin/tool"),
            mock.patch.object(appstore.subprocess, "run", side_effect=results) as run,
        ):
            self.assertTrue(appstore.deb_dependencies_satisfied("/tmp/demo.deb"))

        self.assertEqual(
            run.call_args.args[0][0:3],
            ["dpkg-checkbuilddeps", "-d", "base-files, libc6 (>= 2.36) | libc6.1"],
        )

    def test_dependency_checker_rejects_unreadable_control_fields(self):
        failed = subprocess.CompletedProcess(["dpkg-deb"], 2, "", "bad archive")
        with (
            mock.patch.object(appstore.shutil, "which", return_value="/usr/bin/tool"),
            mock.patch.object(appstore.subprocess, "run", return_value=failed),
        ):
            self.assertFalse(appstore.deb_dependencies_satisfied("/tmp/bad.deb"))

    def test_failed_uninstall_with_unchanged_state_discards_pending(self):
        appstore.write_json(appstore.pending_package_path(), {
            "transaction_id": "tx-remove-fail", "action": "uninstall",
            "app_id": "app-id", "package": "demo-package",
            "previously_installed": True, "previous_version": "1.2.0",
            "helper_completed": False,
        })
        failed = subprocess.CompletedProcess(["dpkg"], 1, "dependency blocks removal\n", "")
        with (
            mock.patch.object(appstore.os, "geteuid", return_value=0),
            mock.patch.object(appstore.shutil, "which", return_value="/usr/bin/dpkg"),
            mock.patch.object(appstore, "run_package_helper_command", return_value=failed),
            mock.patch.object(appstore, "package_dpkg_status", return_value=("ii", "1.2.0")),
        ):
            result, output = self.capture(
                appstore.package_helper, "uninstall", "demo-package",
                transaction_id="tx-remove-fail",
                pending_path_value=str(appstore.pending_package_path()),
            )

        self.assertEqual(result, 1)
        self.assertFalse(appstore.pending_package_path().exists())
        self.assertIn("installed package state unchanged", output)

    def test_failed_helper_with_incomplete_baseline_retains_pending(self):
        appstore.write_json(appstore.pending_package_path(), {
            "transaction_id": "tx-incomplete", "action": "install",
            "app_id": "app-id", "package": "demo-package",
            "helper_completed": False,
        })
        failed = subprocess.CompletedProcess(["dpkg"], 1, "install failed\n", "")
        with (
            mock.patch.object(appstore.os, "geteuid", return_value=0),
            mock.patch.object(appstore, "deb_dependencies_satisfied", return_value=True),
            mock.patch.object(appstore.shutil, "which", return_value="/usr/bin/dpkg"),
            mock.patch.object(appstore, "run_package_helper_command", return_value=failed),
            mock.patch.object(appstore, "package_dpkg_status", return_value=("absent", "")),
        ):
            result, _ = self.capture(
                appstore.package_helper, "install", "/tmp/demo.deb",
                transaction_id="tx-incomplete",
                pending_path_value=str(appstore.pending_package_path()),
            )

        self.assertEqual(result, 1)
        self.assertTrue(appstore.pending_package_path().exists())

    def test_write_json_preserves_existing_mode(self):
        path = self.state_dir / "owned.json"
        path.write_text("{}")
        path.chmod(0o640)
        appstore.write_json(path, {"updated": True})
        self.assertEqual(path.stat().st_mode & 0o777, 0o640)

    def test_dpkg_query_error_is_not_treated_as_package_absent(self):
        failed = subprocess.CompletedProcess(
            ["dpkg-query"], 2, "", "dpkg database read error"
        )
        with (
            mock.patch.object(appstore.shutil, "which", return_value="/usr/bin/dpkg-query"),
            mock.patch.object(appstore.subprocess, "run", return_value=failed),
        ):
            self.assertEqual(appstore.package_dpkg_status("demo-package"), ("unknown", ""))

    def test_package_timeout_terminates_maintenance_script_process_group(self):
        child_code = (
            "import subprocess,time; "
            "p=subprocess.Popen(['sleep','30']); print(p.pid, flush=True); time.sleep(30)"
        )
        with mock.patch.object(appstore, "PACKAGE_COMMAND_TIMEOUT_SECONDS", 0.1):
            with self.assertRaises(subprocess.TimeoutExpired) as raised:
                appstore.run_package_helper_command(
                    [sys.executable, "-c", child_code], os.environ.copy()
                )
        child_pid = int(str(raised.exception.stdout).strip().splitlines()[0])
        stat_path = Path(f"/proc/{child_pid}/stat")
        try:
            state = stat_path.read_text().split()[2]
        except (FileNotFoundError, ProcessLookupError):
            state = "gone"
        self.assertIn(state, ("gone", "Z", "X"))

    def test_reconcile_install_uses_actual_version_and_clears_pending(self):
        appstore.write_json(
            appstore.pending_package_path(),
            {
                "action": "install",
                "app_id": "demo",
                "package": "demo-package",
                "expected_version": "1.2.0",
                "deb_path": "/cache/demo.deb",
            },
        )
        with (
            mock.patch.object(appstore, "find_app", return_value=APP),
            mock.patch.object(appstore, "package_state", return_value=(True, "1.2.3-1")),
            mock.patch.object(appstore, "update_installed_record") as update_record,
        ):
            result, output = self.capture(appstore.reconcile_pending_package_job, True)

        self.assertTrue(result)
        update_record.assert_called_once_with(APP, "demo-package", "1.2.3-1", "/cache/demo.deb")
        self.assertFalse(appstore.pending_package_path().exists())
        self.assertIn("PACKAGE_RESULT\tinstall\tdemo\tdemo-package\t1.2.3-1", output)

    def test_reconcile_unapplied_install_clears_stale_state(self):
        pending = {
            "action": "upgrade",
            "app_id": "demo",
            "package": "demo-package",
            "expected_version": "1.2.0",
            "deb_path": "/cache/demo.deb",
        }
        appstore.write_json(appstore.pending_package_path(), pending)
        with (
            mock.patch.object(appstore, "find_app", return_value=APP),
            mock.patch.object(appstore, "package_state", return_value=(False, "")),
            mock.patch.object(appstore, "update_installed_record") as update_record,
        ):
            result, output = self.capture(appstore.reconcile_pending_package_job, True)

        self.assertTrue(result)
        update_record.assert_not_called()
        self.assertFalse(appstore.pending_package_path().exists())
        self.assertIn("WARNING\tupgrade was not applied", output)
        self.assertIn("PACKAGE_RESULT\tupgrade", output)

    def test_reconcile_unapplied_uninstall_restores_record(self):
        appstore.write_json(
            appstore.pending_package_path(),
            {"action": "uninstall", "app_id": "demo", "package": "demo-package"},
        )
        with (
            mock.patch.object(appstore, "find_app", return_value=APP),
            mock.patch.object(appstore, "package_state", return_value=(True, "1.2.0")),
            mock.patch.object(appstore, "update_installed_record") as update_record,
        ):
            result, output = self.capture(appstore.reconcile_pending_package_job, True)

        self.assertTrue(result)
        update_record.assert_called_once_with(APP, "demo-package", "1.2.0", "")
        self.assertFalse(appstore.pending_package_path().exists())
        self.assertIn("WARNING\tuninstall was not applied", output)

    def test_finalize_verification_failure_retains_pending(self):
        pending = {
            "schema_version": 2,
            "transaction_id": "tx-finalize-fail",
            "action": "install",
            "app_id": "demo",
            "package": "demo-package",
            "deb_path": "/cache/demo.deb",
            "helper_completed": True,
        }
        appstore.write_json(appstore.pending_package_path(), pending)
        with (
            mock.patch.object(appstore, "find_app", return_value=APP),
            mock.patch.object(appstore, "package_state", return_value=(False, "")),
        ):
            result, output = self.capture(
                appstore.finalize_package_job, "install", "demo", "tx-finalize-fail"
            )

        self.assertEqual(result, 1)
        self.assertTrue(appstore.pending_package_path().exists())
        self.assertIn("ERROR\tpackage is not installed after install", output)

    def test_package_files_query_failure_is_not_silently_accepted(self):
        completed = subprocess.CompletedProcess(
            ["dpkg-query", "-L", "demo-package"], 1, "", "package missing"
        )
        with (
            mock.patch.object(appstore.shutil, "which", return_value="/usr/bin/dpkg-query"),
            mock.patch.object(appstore.subprocess, "run", return_value=completed),
        ):
            with self.assertRaisesRegex(RuntimeError, "package missing"):
                appstore.package_files("demo-package")

    def test_reconcile_interrupted_upgrade_with_old_version_clears_pending(self):
        appstore.write_json(
            appstore.pending_package_path(),
            {
                "schema_version": 2,
                "transaction_id": "tx1",
                "action": "upgrade",
                "app_id": "demo",
                "package": "demo-package",
                "previously_installed": True,
                "previous_version": "1.1.0",
                "expected_package_version": "1.2.0",
                "expected_version": "1.2.0",
                "helper_completed": False,
            },
        )
        with (
            mock.patch.object(appstore, "find_app", return_value=APP),
            mock.patch.object(appstore, "package_state", return_value=(True, "1.1.0")),
            mock.patch.object(appstore, "package_dpkg_status", return_value=("ii", "1.1.0")),
        ):
            result, output = self.capture(appstore.reconcile_pending_package_job, True)

        self.assertTrue(result)
        self.assertFalse(appstore.pending_package_path().exists())
        self.assertIn("installed package state unchanged", output)

    def test_reconcile_interrupted_new_install_clears_pending(self):
        appstore.write_json(appstore.pending_package_path(), {
            "schema_version": 2, "transaction_id": "tx-install-not-started",
            "action": "install", "app_id": "app-id", "package": "demo-package",
            "previously_installed": False, "previous_version": "",
            "expected_package_version": "1.2.0", "helper_completed": False,
            "app_snapshot": APP,
        })
        with (
            mock.patch.object(appstore, "find_app", return_value=APP),
            mock.patch.object(appstore, "package_state", return_value=(False, "")),
            mock.patch.object(appstore, "package_dpkg_status", return_value=("absent", "")),
        ):
            result, output = self.capture(appstore.reconcile_pending_package_job, True)

        self.assertTrue(result)
        self.assertFalse(appstore.pending_package_path().exists())
        self.assertIn("cleared stale transaction", output)
        self.assertNotIn("PACKAGE_RESULT", output)

    def test_reconcile_interrupted_uninstall_with_old_version_clears_pending(self):
        appstore.write_json(appstore.pending_package_path(), {
            "schema_version": 2, "transaction_id": "tx-uninstall-not-started",
            "action": "uninstall", "app_id": "app-id", "package": "demo-package",
            "previously_installed": True, "previous_version": "1.2.0",
            "helper_completed": False, "app_snapshot": APP,
        })
        with (
            mock.patch.object(appstore, "find_app", return_value=APP),
            mock.patch.object(appstore, "package_state", return_value=(True, "1.2.0")),
            mock.patch.object(appstore, "package_dpkg_status", return_value=("ii", "1.2.0")),
        ):
            result, output = self.capture(appstore.reconcile_pending_package_job, True)

        self.assertTrue(result)
        self.assertFalse(appstore.pending_package_path().exists())
        self.assertIn("cleared stale transaction", output)

    def test_reconcile_interrupted_install_with_unknown_status_retains_pending(self):
        appstore.write_json(appstore.pending_package_path(), {
            "schema_version": 2, "transaction_id": "tx-install-unknown",
            "action": "install", "app_id": "app-id", "package": "demo-package",
            "previously_installed": False, "previous_version": "",
            "expected_package_version": "1.2.0", "helper_completed": False,
            "app_snapshot": APP,
        })
        with (
            mock.patch.object(appstore, "find_app", return_value=APP),
            mock.patch.object(appstore, "package_state", return_value=(False, "")),
            mock.patch.object(appstore, "package_dpkg_status", return_value=("unknown", "")),
        ):
            result, output = self.capture(appstore.reconcile_pending_package_job, True)

        self.assertFalse(result)
        self.assertTrue(appstore.pending_package_path().exists())
        self.assertIn("outcome is not known", output)

    def test_reconcile_incomplete_baseline_retains_pending(self):
        appstore.write_json(appstore.pending_package_path(), {
            "schema_version": 2, "transaction_id": "tx-install-incomplete",
            "action": "install", "app_id": "app-id", "package": "demo-package",
            "expected_package_version": "1.2.0", "helper_completed": False,
            "app_snapshot": APP,
        })
        with (
            mock.patch.object(appstore, "find_app", return_value=APP),
            mock.patch.object(appstore, "package_state", return_value=(False, "")),
            mock.patch.object(appstore, "package_dpkg_status", return_value=("absent", "")),
        ):
            result, output = self.capture(appstore.reconcile_pending_package_job, True)

        self.assertFalse(result)
        self.assertTrue(appstore.pending_package_path().exists())
        self.assertIn("outcome is not known", output)

    def test_reconcile_unknown_action_retains_pending(self):
        appstore.write_json(appstore.pending_package_path(), {
            "schema_version": 2, "transaction_id": "tx-invalid-action",
            "action": "repair", "app_id": "app-id", "package": "demo-package",
            "previously_installed": False, "previous_version": "",
            "helper_completed": False, "app_snapshot": APP,
        })

        result, output = self.capture(appstore.reconcile_pending_package_job, True)

        self.assertFalse(result)
        self.assertTrue(appstore.pending_package_path().exists())
        self.assertIn("invalid and was retained", output)

    def test_reconcile_recovers_uninstall_when_receipt_was_lost(self):
        appstore.write_json(appstore.pending_package_path(), {
            "schema_version": 2, "transaction_id": "tx-uninstall-recover",
            "action": "uninstall", "app_id": "app-id", "package": "demo-package",
            "previously_installed": True, "previous_version": "1.2.0",
            "helper_completed": False, "app_snapshot": APP,
        })
        with (
            mock.patch.object(appstore, "find_app", return_value=APP),
            mock.patch.object(appstore, "package_state", return_value=(False, "")),
        ):
            result, output = self.capture(appstore.reconcile_pending_package_job, True)

        self.assertTrue(result)
        self.assertFalse(appstore.pending_package_path().exists())
        self.assertIn("recovered uninstall from verified package state", output)
        self.assertIn("PACKAGE_RESULT\tuninstall", output)

    def test_reconcile_recovers_new_install_when_receipt_was_lost(self):
        appstore.write_json(appstore.pending_package_path(), {
            "schema_version": 2, "transaction_id": "tx-install-recover",
            "action": "install", "app_id": "app-id", "package": "demo-package",
            "previously_installed": False, "previous_version": "",
            "expected_package_version": "1.2.0", "helper_completed": False,
            "app_snapshot": APP,
        })
        with (
            mock.patch.object(appstore, "find_app", return_value=APP),
            mock.patch.object(appstore, "package_state", return_value=(True, "1.2.0")),
            mock.patch.object(appstore, "package_dpkg_status", return_value=("ii", "1.2.0")),
            mock.patch.object(appstore, "update_installed_record"),
        ):
            result, output = self.capture(appstore.reconcile_pending_package_job, True)

        self.assertTrue(result)
        self.assertFalse(appstore.pending_package_path().exists())
        self.assertIn("recovered install from verified package state", output)
        self.assertIn("PACKAGE_RESULT\tinstall", output)

    def test_reconcile_retains_same_version_reinstall_when_receipt_was_lost(self):
        appstore.write_json(appstore.pending_package_path(), {
            "schema_version": 2, "transaction_id": "tx-reinstall-recover",
            "action": "reinstall", "app_id": "app-id", "package": "demo-package",
            "previously_installed": True, "previous_version": "1.2.0",
            "expected_package_version": "1.2.0", "helper_completed": False,
            "app_snapshot": APP,
        })
        with (
            mock.patch.object(appstore, "find_app", return_value=APP),
            mock.patch.object(appstore, "package_state", return_value=(True, "1.2.0")),
            mock.patch.object(appstore, "package_dpkg_status", return_value=("ii", "1.2.0")),
            mock.patch.object(appstore, "update_installed_record") as update_record,
        ):
            result, output = self.capture(appstore.reconcile_pending_package_job, True)

        self.assertFalse(result)
        update_record.assert_not_called()
        self.assertTrue(appstore.pending_package_path().exists())
        self.assertIn("outcome is not known", output)
        self.assertNotIn("PACKAGE_RESULT", output)

    def test_reconcile_retains_same_version_install_when_receipt_was_lost(self):
        appstore.write_json(appstore.pending_package_path(), {
            "schema_version": 2, "transaction_id": "tx-install-same-version",
            "action": "install", "app_id": "app-id", "package": "demo-package",
            "previously_installed": True, "previous_version": "1.2.0",
            "expected_package_version": "1.2.0", "helper_completed": False,
            "app_snapshot": APP,
        })
        with (
            mock.patch.object(appstore, "find_app", return_value=APP),
            mock.patch.object(appstore, "package_state", return_value=(True, "1.2.0")),
            mock.patch.object(appstore, "package_dpkg_status", return_value=("ii", "1.2.0")),
        ):
            result, output = self.capture(appstore.reconcile_pending_package_job, True)

        self.assertFalse(result)
        self.assertTrue(appstore.pending_package_path().exists())
        self.assertIn("outcome is not known", output)

    def test_reconcile_retains_same_version_upgrade_when_receipt_was_lost(self):
        appstore.write_json(appstore.pending_package_path(), {
            "schema_version": 2, "transaction_id": "tx-upgrade-same-version",
            "action": "upgrade", "app_id": "app-id", "package": "demo-package",
            "previously_installed": True, "previous_version": "1.2.0",
            "expected_package_version": "1.2.0", "helper_completed": False,
            "app_snapshot": APP,
        })
        with (
            mock.patch.object(appstore, "find_app", return_value=APP),
            mock.patch.object(appstore, "package_state", return_value=(True, "1.2.0")),
            mock.patch.object(appstore, "package_dpkg_status", return_value=("ii", "1.2.0")),
        ):
            result, output = self.capture(appstore.reconcile_pending_package_job, True)

        self.assertFalse(result)
        self.assertTrue(appstore.pending_package_path().exists())
        self.assertIn("outcome is not known", output)

    def test_prepare_rejects_existing_transaction(self):
        appstore.write_json(appstore.pending_package_path(), {"app_id": "other"})
        with mock.patch.object(appstore, "find_app", return_value=APP):
            result, output = self.capture(appstore.prepare_package_job, "install", "demo")
        self.assertEqual(result, 1)
        self.assertIn("another package transaction is pending", output)

    def test_prepare_allows_uninstall_recovery_after_failed_install(self):
        appstore.write_json(appstore.pending_package_path(), {
            "schema_version": 2, "transaction_id": "tx-partial",
            "action": "install", "app_id": "app-id", "package": "demo-package",
            "helper_failed": True, "helper_completed": False,
        })
        with (
            mock.patch.object(appstore, "find_app", return_value=APP),
            mock.patch.object(appstore, "package_state", return_value=(False, "")),
        ):
            result, output = self.capture(
                appstore.prepare_package_job, "uninstall", "app-id"
            )

        self.assertEqual(result, 0)
        pending = appstore.pending_package_job()
        self.assertEqual(pending.get("action"), "uninstall")
        self.assertNotEqual(pending.get("transaction_id"), "tx-partial")
        self.assertIn("PACKAGE_JOB\tuninstall\tdemo-package", output)

    def test_uninstall_prefers_recorded_package_name(self):
        appstore.write_json(
            appstore.installed_path(),
            {"app-id": {"package": "old-package", "version": "1.0"}},
        )
        with (
            mock.patch.object(appstore, "find_app", return_value=APP),
            mock.patch.object(appstore, "package_state", return_value=(True, "1.0")),
        ):
            result, output = self.capture(appstore.prepare_package_job, "uninstall", "app-id")
        self.assertEqual(result, 0)
        self.assertIn("PACKAGE_JOB\tuninstall\told-package", output)

    def test_reconcile_uses_snapshot_when_registry_app_disappears(self):
        pending = {
            "schema_version": 2,
            "transaction_id": "tx2",
            "action": "install",
            "app_id": "app-id",
            "package": "demo-package",
            "helper_completed": True,
            "expected_package_version": "1.2.0",
            "app_snapshot": APP,
        }
        appstore.write_json(appstore.pending_package_path(), pending)
        with (
            mock.patch.object(appstore, "find_app", return_value=None),
            mock.patch.object(appstore, "package_state", return_value=(True, "1.2.0")),
            mock.patch.object(appstore, "update_installed_record") as update_record,
        ):
            result, _ = self.capture(appstore.reconcile_pending_package_job, True)
        self.assertTrue(result)
        update_record.assert_called_once()

    def test_finalize_uses_snapshot_when_registry_app_disappears(self):
        pending = {
            "schema_version": 2,
            "transaction_id": "tx3",
            "action": "install",
            "app_id": "app-id",
            "package": "demo-package",
            "helper_completed": True,
            "expected_package_version": "1.2.0",
            "app_snapshot": APP,
        }
        appstore.write_json(appstore.pending_package_path(), pending)
        with (
            mock.patch.object(appstore, "find_app", return_value=None),
            mock.patch.object(appstore, "package_state", return_value=(True, "1.2.0")),
            mock.patch.object(appstore, "update_installed_record"),
        ):
            result, output = self.capture(
                appstore.finalize_package_job, "install", "app-id", "tx3"
            )
        self.assertEqual(result, 0, output)

    def test_v2_install_without_helper_receipt_is_not_guessed_successful(self):
        pending = {
            "schema_version": 2,
            "transaction_id": "tx4",
            "action": "install",
            "app_id": "app-id",
            "package": "demo-package",
            "previously_installed": False,
            "helper_completed": False,
            "app_snapshot": APP,
        }
        appstore.write_json(appstore.pending_package_path(), pending)
        with (
            mock.patch.object(appstore, "find_app", return_value=APP),
            mock.patch.object(appstore, "package_state", return_value=(True, "1.2.0")),
            mock.patch.object(appstore, "package_dpkg_status", return_value=("ii", "1.2.0")),
            mock.patch.object(appstore, "update_installed_record") as update_record,
        ):
            result, output = self.capture(appstore.reconcile_pending_package_job, True)
        self.assertFalse(result)
        update_record.assert_not_called()
        self.assertTrue(appstore.pending_package_path().exists())
        self.assertIn("outcome is not known", output)

    def test_finalize_rejects_missing_transaction(self):
        with mock.patch.object(appstore, "find_app", return_value=APP):
            result, output = self.capture(
                appstore.finalize_package_job, "install", "app-id", "missing"
            )
        self.assertEqual(result, 1)
        self.assertIn("no pending package transaction", output)

    def test_upgrade_requires_version_change(self):
        pending = {
            "schema_version": 2,
            "transaction_id": "tx-upgrade",
            "action": "upgrade",
            "app_id": "app-id",
            "package": "demo-package",
            "previous_version": "1.1.0",
            "expected_package_version": "1.2.0",
            "helper_completed": True,
            "app_snapshot": APP,
        }
        appstore.write_json(appstore.pending_package_path(), pending)
        with (
            mock.patch.object(appstore, "find_app", return_value=APP),
            mock.patch.object(appstore, "package_state", return_value=(True, "1.1.0")),
            mock.patch.object(appstore, "update_installed_record") as update_record,
        ):
            result, output = self.capture(
                appstore.finalize_package_job, "upgrade", "app-id", "tx-upgrade"
            )
        self.assertEqual(result, 1)
        update_record.assert_not_called()
        self.assertIn("expected package version", output)

    def test_failed_transaction_can_be_retried(self):
        deb_path = self.state_dir / "demo.deb"
        deb_path.write_bytes(b"deb")
        appstore.write_json(appstore.pending_package_path(), {
            "schema_version": 2, "transaction_id": "tx-retry", "action": "install",
            "app_id": "app-id", "package": "demo-package", "deb_path": str(deb_path),
            "helper_completed": False,
        })
        with (
            mock.patch.object(appstore, "find_app", return_value=APP),
            mock.patch.object(appstore, "deb_file_version", return_value="1.2.0"),
            mock.patch.object(appstore, "deb_file_package", return_value="demo-package"),
        ):
            result, output = self.capture(appstore.prepare_package_job, "install", "app-id")
        self.assertEqual(result, 0)
        self.assertIn(f"PACKAGE_JOB\tinstall\t{deb_path}", output)
        self.assertIn("tx-retry", output)
        self.assertEqual(
            appstore.pending_package_job().get("expected_package_version"), "1.2.0"
        )

    def test_finalize_is_idempotent_after_startup_reconcile(self):
        appstore.write_json(appstore.pending_package_path(), {
            "schema_version": 2, "transaction_id": "tx-recovered", "action": "install",
            "app_id": "app-id", "package": "demo-package", "helper_completed": True,
            "expected_package_version": "1.2.0",
            "app_snapshot": APP,
        })
        with (
            mock.patch.object(appstore, "find_app", return_value=APP),
            mock.patch.object(appstore, "package_state", return_value=(True, "1.2.0")),
            mock.patch.object(appstore, "update_installed_record"),
        ):
            reconciled, _ = self.capture(appstore.reconcile_pending_package_job, True)
            result, output = self.capture(
                appstore.finalize_package_job, "install", "app-id", "tx-recovered"
            )
        self.assertTrue(reconciled)
        self.assertEqual(result, 0)
        self.assertIn("PACKAGE_RESULT\tinstall", output)

    def test_normal_finalize_is_idempotent(self):
        appstore.write_json(appstore.pending_package_path(), {
            "schema_version": 2, "transaction_id": "tx-normal", "action": "install",
            "app_id": "app-id", "package": "demo-package", "helper_completed": True,
            "expected_package_version": "1.2.0", "app_snapshot": APP,
        })
        with (
            mock.patch.object(appstore, "find_app", return_value=APP),
            mock.patch.object(appstore, "package_state", return_value=(True, "1.2.0")),
            mock.patch.object(appstore, "update_installed_record"),
        ):
            first, _ = self.capture(
                appstore.finalize_package_job, "install", "app-id", "tx-normal"
            )
            second, output = self.capture(
                appstore.finalize_package_job, "install", "app-id", "tx-normal"
            )
        self.assertEqual(first, 0)
        self.assertEqual(second, 0)
        self.assertIn("PACKAGE_RESULT\tinstall", output)

    def test_retry_rejects_deb_with_migrated_package_name(self):
        missing_deb = self.state_dir / "missing.deb"
        downloaded = self.state_dir / "new.deb"
        downloaded.write_bytes(b"deb")
        appstore.write_json(appstore.pending_package_path(), {
            "schema_version": 2, "transaction_id": "tx-migrated", "action": "install",
            "app_id": "app-id", "package": "old-package", "deb_path": str(missing_deb),
            "helper_completed": False, "app_snapshot": APP,
        })
        with (
            mock.patch.object(appstore, "find_app", return_value=APP),
            mock.patch.object(appstore, "download_deb", return_value=downloaded),
            mock.patch.object(appstore, "deb_file_package", return_value="new-package"),
        ):
            result, output = self.capture(appstore.prepare_package_job, "install", "app-id")
        self.assertEqual(result, 1)
        self.assertNotIn("PACKAGE_JOB", output)
        self.assertIn("does not match pending transaction", output)

    def test_reconcile_retains_completed_install_when_package_is_missing(self):
        appstore.write_json(appstore.pending_package_path(), {
            "schema_version": 2, "transaction_id": "tx-missing", "action": "install",
            "app_id": "app-id", "package": "demo-package", "helper_completed": True,
            "expected_package_version": "1.2.0", "app_snapshot": APP,
        })
        with (
            mock.patch.object(appstore, "find_app", return_value=APP),
            mock.patch.object(appstore, "package_state", return_value=(False, "")),
        ):
            result, output = self.capture(appstore.reconcile_pending_package_job, True)
        self.assertFalse(result)
        self.assertTrue(appstore.pending_package_path().exists())
        self.assertFalse(appstore.completed_package_path().exists())
        self.assertNotIn("PACKAGE_RESULT", output)

    def test_reconcile_retains_completed_uninstall_when_package_remains(self):
        appstore.write_json(appstore.pending_package_path(), {
            "schema_version": 2, "transaction_id": "tx-remains", "action": "uninstall",
            "app_id": "app-id", "package": "demo-package", "helper_completed": True,
            "app_snapshot": APP,
        })
        with (
            mock.patch.object(appstore, "find_app", return_value=APP),
            mock.patch.object(appstore, "package_state", return_value=(True, "1.2.0")),
            mock.patch.object(appstore, "update_installed_record"),
        ):
            result, output = self.capture(appstore.reconcile_pending_package_job, True)
        self.assertFalse(result)
        self.assertTrue(appstore.pending_package_path().exists())
        self.assertFalse(appstore.completed_package_path().exists())
        self.assertNotIn("PACKAGE_RESULT", output)


if __name__ == "__main__":
    unittest.main()
