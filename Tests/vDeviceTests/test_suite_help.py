import contextlib
import io
import os
from pathlib import Path
import runpy
import sys
import unittest
from unittest import mock

import SuiteManager


class SuiteHelpTests(unittest.TestCase):
    def test_help_lists_registered_tests_without_execution(self):
        script = str(Path(SuiteManager.__file__).resolve())
        suite = next(iter(SuiteManager.SUITES))
        first_test = SuiteManager.SUITES[suite]["tests"][0]
        for arguments in (
            ["-h"],
            ["--help"],
            [suite, "--help"],
            ["-t", suite, "--help"],
            [suite, "--test", first_test, "--help"],
        ):
            with self.subTest(arguments=arguments):
                output = io.StringIO()
                with mock.patch.object(sys, "argv", [script] + arguments), \
                        mock.patch.dict(os.environ, {}, clear=True), \
                        mock.patch("subprocess.Popen") as popen, \
                        mock.patch("time.sleep") as sleep, \
                        mock.patch("builtins.input") as prompt, \
                        mock.patch("importlib.import_module") as import_module, \
                        contextlib.redirect_stdout(output):
                    with self.assertRaises(SystemExit) as exited:
                        runpy.run_path(script, run_name="__main__")
                    self.assertEqual(dict(os.environ), {})

                self.assertEqual(exited.exception.code, 0)
                import_module.assert_not_called()
                popen.assert_not_called()
                sleep.assert_not_called()
                prompt.assert_not_called()
                help_text = output.getvalue()
                for detail in (
                    "usage:", "-t, --timing", "--test",
                    "Run all tests with timing", "No interactive prompts",
                    "full module name", "AUTO_ACTIVATE_PLUGINS=0",
                ):
                    self.assertIn(detail, help_text)
                actual = [
                    line.strip() for line in help_text.splitlines()
                    if line.strip().startswith("TCID")
                ]
                expected = [
                    name for config in SuiteManager.SUITES.values()
                    for name in config["tests"]
                ]
                self.assertEqual(actual, expected)


if __name__ == "__main__":
    unittest.main()
