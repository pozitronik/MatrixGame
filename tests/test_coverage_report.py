# MatrixGame - licensed under GPLv2 or any later version.
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from unittest.mock import patch

PROJECT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("coverage_report", PROJECT / "tools/coverage_report.py")
coverage = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(coverage)


def document(path, lines, functions=(), branches=None, working_directory=""):
    """A synthetic gcov JSON document for one source file."""
    return {
        "format_version": "1",
        "current_working_directory": working_directory,
        "files": [{
            "file": path,
            "functions": [{"demangled_name": name, "start_line": line, "start_column": 1, "execution_count": count}
                          for name, line, count in functions],
            "lines": [{"line_number": number, "count": count,
                       "branches": (branches or {}).get(number, [])} for number, count in lines],
        }],
    }


class CoverageReportTests(unittest.TestCase):
    def setUp(self):
        base = PROJECT / "build/coverage-tests"
        base.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=base)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_paths_are_classified_by_area_and_exclusion(self):
        expected = {
            "MatrixGame/src/MatrixLogic.cpp": "engine",
            "MatrixGame/src/Audio/Audio.cpp": "engine",
            "MatrixLib/Base/CRC32.cpp": "base",
            "MatrixLib/3G/Math3D.hpp": "graphics",
            "MatrixLib/Bitmap/FilePNG.cpp": "bitmap",
            "tests/base_tests.cpp": "tests",
            "ThirdParty/CMakeLists.txt": "third_party",
            "build/mingw-debug-exe-coverage/zlib/include/zlib.h": "third_party",
            ".tools/winlibs-13.2.0/mingw32/include/c++/13.2.0/vector": "toolchain",
            None: "toolchain",
            "cmake/FindDIRECTX9.cmake": "other",
        }
        for path, area in expected.items():
            self.assertEqual(coverage.classify(path), area, path)

    def test_external_absolute_paths_are_toolchain(self):
        outside = str(self.root.parent.parent / "toolchain/include/vector")
        self.assertIsNone(coverage.normalize(outside, "", self.root))
        inside = str(self.root / "MatrixLib/Base/CRC32.cpp")
        self.assertEqual(coverage.normalize(inside, "", self.root), "MatrixLib/Base/CRC32.cpp")
        self.assertEqual(coverage.normalize("MatrixLib\\Base\\CRC32.cpp", "", self.root), "MatrixLib/Base/CRC32.cpp")

    def test_translation_units_merge_by_line_and_function(self):
        header = "MatrixLib/Base/CBuf.hpp"
        documents = [
            (document(header, [(10, 0), (11, 0)], [("Base::CBuf::Len()", 10, 0)],
                      {10: [{"count": 0}, {"count": 0}, {"count": 3, "throw": True}]}), False),
            (document(header, [(10, 4), (12, 0)], [("Base::CBuf::Len()", 10, 4)],
                      {10: [{"count": 1}, {"count": 0}]}), True),
        ]
        files = coverage.merge(documents, self.root)
        totals = files[header].totals()
        self.assertEqual(totals["lines"], 3)
        self.assertEqual(totals["lines_covered"], 1)
        self.assertEqual(totals["functions"], 1)
        self.assertEqual(totals["functions_covered"], 1)
        # The exception edge is ignored; equal branch lists add element-wise.
        self.assertEqual(totals["branches"], 2)
        self.assertEqual(totals["branches_covered"], 1)
        self.assertTrue(files[header].with_data)

    def test_summary_separates_production_from_exclusions(self):
        documents = [
            (document("MatrixGame/src/MatrixLogic.cpp", [(1, 1), (2, 0), (3, 0), (4, 0)],
                      [("CMatrixMapLogic::Takt(int)", 1, 1), ("CMatrixMapLogic::Clear()", 3, 0)]), True),
            (document("MatrixLib/3G/Math3D.cpp", [(5, 0)]), False),
            (document("tests/base_tests.cpp", [(1, 1)]), True),
            (document(".tools/winlibs/include/vector", [(1, 1)]), True),
        ]
        groups, production, entries = coverage.summarize(coverage.merge(documents, self.root))
        self.assertEqual(groups["engine"]["lines"], 4)
        self.assertEqual(groups["engine"]["lines_covered"], 1)
        self.assertEqual(groups["engine"]["line_percent"], 25.0)
        self.assertEqual(groups["graphics"]["files_with_data"], 0)
        self.assertEqual(groups["tests"]["lines"], 1)
        self.assertEqual(groups["toolchain"]["lines"], 1)
        self.assertEqual(production["lines"], 5)
        self.assertEqual(production["lines_covered"], 1)
        engine = next(entry for entry in entries if entry["area"] == "engine")
        self.assertEqual(engine["uncovered_lines"], "2-4")
        self.assertEqual([item["count"] for item in engine["functions_detail"]], [1, 0])
        self.assertEqual({entry["area"] for entry in entries}, {"engine", "graphics"})

    def test_uncompiled_production_files_are_listed(self):
        (self.root / "MatrixLib/Base").mkdir(parents=True)
        (self.root / "MatrixLib/Base/Used.cpp").write_text("int used;\n")
        (self.root / "MatrixLib/Base/Unused.cpp").write_text("int unused;\n")
        (self.root / "MatrixLib/Base/Types.hpp").write_text("struct Types;\n")
        files = coverage.merge([(document("MatrixLib/Base/Used.cpp", [(1, 1)]), True)], self.root)
        sources, headers = coverage.uninstrumented(self.root, files)
        self.assertEqual([item["path"] for item in sources], ["MatrixLib/Base/Unused.cpp"])
        self.assertEqual([item["path"] for item in headers], ["MatrixLib/Base/Types.hpp"])

    def test_object_paths_map_back_to_sources(self):
        build = self.root / "build"
        note = build / "MatrixGame/CMakeFiles/MatrixGameInternal.dir/src/Logic/MatrixRule.cpp.gcno"
        self.assertEqual(coverage.object_source(note, build), "MatrixGame/src/Logic/MatrixRule.cpp")
        note = build / "tests/CMakeFiles/matrixgame_path_tests.dir/__/MatrixGame/src/ExecutablePath.cpp.gcno"
        self.assertEqual(coverage.object_source(note, build), "MatrixGame/src/ExecutablePath.cpp")

    def test_branch_merge_never_loses_executed_counts(self):
        self.assertEqual(coverage.merge_branches([1, 0], [0, 0, 2]), [1, 0, 2])
        self.assertEqual(coverage.merge_branches([0, 5, 1], [2]), [2, 5, 1])
        self.assertEqual(coverage.merge_branches([], [0, 3]), [0, 3])
        header = "MatrixLib/Base/CBuf.hpp"
        documents = [
            (document(header, [(10, 1)], branches={10: [{"count": 0}, {"count": 4}]}), True),
            (document(header, [(10, 1)], branches={10: [{"count": 0}, {"count": 0}, {"count": 0}]}), True),
        ]
        totals = coverage.merge(documents, self.root)[header].totals()
        self.assertEqual(totals["branches"], 3)
        self.assertEqual(totals["branches_covered"], 1)

    def test_only_notes_of_test_objects_are_measured(self):
        build = self.root / "build"
        library = build / "MatrixLib/CMakeFiles/MatrixLib.dir/Base"
        game = build / "MatrixGame/CMakeFiles/MatrixGame.dir/src"
        for directory in (library, game):
            directory.mkdir(parents=True)
        (library / "CRC32.cpp.gcno").write_bytes(b"")
        (library / "CRC32.cpp.obj").write_bytes(b"")
        (library / "Removed.cpp.gcno").write_bytes(b"")
        (game / "MatrixGame.cpp.gcno").write_bytes(b"")
        ninja = self.root / "ninja.exe"
        ninja.write_bytes(b"")
        listing = "MatrixLib/CMakeFiles/MatrixLib.dir/Base/CRC32.cpp.obj\nzlib/lib/libzsd.a\n"
        reply = subprocess.CompletedProcess([], 0, stdout=listing, stderr="")
        with patch.object(coverage.subprocess, "run", return_value=reply) as run:
            measured, ignored = coverage.measured_notes(build, {"CMAKE_GENERATOR": "Ninja"}, str(ninja))
        self.assertIn("matrixgame_tests", run.call_args[0][0])
        self.assertEqual([note.name for note in measured], ["CRC32.cpp.gcno"])
        self.assertEqual(sorted(note.name for note in ignored), ["MatrixGame.cpp.gcno", "Removed.cpp.gcno"])

    def test_gcov_problems_name_an_accurate_remedy(self):
        note = self.root / "CRC32.cpp.gcno"
        newer = subprocess.CompletedProcess([], 0, stdout="", stderr="MatrixLib/Base/CRC32.cpp:source file is newer "
                                            "than notes file 'CRC32.cpp.gcno'\n")
        with patch.object(coverage.subprocess, "run", return_value=newer):
            with self.assertRaisesRegex(coverage.CoverageError, "Sources changed after the instrumented build"):
                list(coverage.run_gcov("gcov.exe", [note], self.root, []))
        stale = subprocess.CompletedProcess([], 0, stdout="", stderr="CRC32.cpp.gcda:stamp mismatch with notes file\n")
        with patch.object(coverage.subprocess, "run", return_value=stale):
            with self.assertRaisesRegex(coverage.CoverageError, "different build"):
                list(coverage.run_gcov("gcov.exe", [note], self.root, []))
        broken = subprocess.CompletedProcess([], 0, stdout="{not json\n", stderr="")
        with patch.object(coverage.subprocess, "run", return_value=broken):
            with self.assertRaisesRegex(coverage.CoverageError, "malformed JSON"):
                list(coverage.run_gcov("gcov.exe", [note], self.root, []))

    def test_malformed_junit_report_fails_clearly(self):
        build = self.root / "build"
        build.mkdir()
        (build / "test-results.xml").write_text("<testsuite tests=")
        with self.assertRaisesRegex(coverage.CoverageError, "not a readable JUnit report"):
            coverage.test_results(build)
        (build / "test-results.xml").write_text('<testsuite tests="2" failures="0" errors="0" skipped="1"/>')
        self.assertEqual(coverage.test_results(build), {"tests": 2, "failures": 0, "errors": 0, "skipped": 1})

    def test_line_ranges_are_compact(self):
        self.assertEqual(coverage.ranges([7, 1, 2, 3, 5]), "1-3,5,7")
        self.assertEqual(coverage.ranges([]), "")

    def run_main(self, *arguments):
        output, errors = io.StringIO(), io.StringIO()
        with redirect_stdout(output), redirect_stderr(errors):
            status = coverage.main(list(arguments))
        return status, output.getvalue(), errors.getvalue()

    def coverage_build(self, enabled=True):
        build = self.root / "build"
        build.mkdir()
        (build / "CMakeCache.txt").write_text(
            "CMAKE_BUILD_TYPE:STRING=Debug\nMATRIXGAME_COVERAGE:BOOL=%s\n" % ("ON" if enabled else "OFF"))
        return build

    def test_missing_inputs_fail_clearly(self):
        status, _, errors = self.run_main("--build-dir", str(self.root / "absent"), "--gcov", "gcov.exe")
        self.assertEqual(status, 1)
        self.assertIn("does not exist", errors)

        build = self.coverage_build(enabled=False)
        status, _, errors = self.run_main("--build-dir", str(build), "--gcov", "gcov.exe")
        self.assertEqual(status, 1)
        self.assertIn("not a coverage build", errors)

    def test_missing_gcov_and_data_fail_clearly(self):
        build = self.coverage_build()
        status, _, errors = self.run_main("--build-dir", str(build), "--gcov", str(self.root / "gcov.exe"))
        self.assertEqual(status, 1)
        self.assertIn("gcov was not found", errors)

    def test_missing_compiler_fails_clearly(self):
        build = self.coverage_build()
        gcov = self.root / "gcov.exe"
        gcov.write_bytes(b"")
        version = subprocess.CompletedProcess([], 0, stdout="gcov 13.2.0\n", stderr="")
        with patch.object(coverage.subprocess, "run", return_value=version):
            status, _, errors = self.run_main("--build-dir", str(build), "--gcov", str(gcov),
                                              "--compiler", str(self.root / "missing-g++.exe"))
        self.assertEqual(status, 1)
        self.assertIn("missing-g++.exe was not found", errors)

    def test_query_reads_an_existing_report(self):
        report = {"files": [{"path": "MatrixLib/Base/CRC32.cpp", "lines": 2, "lines_covered": 1, "functions": 1,
                             "functions_covered": 1, "uncovered_lines": "9",
                             "functions_detail": [{"name": "Base::CalcCRC32(void const*, int)", "line": 8,
                                                   "count": 7}]}]}
        path = self.root / "coverage.json"
        path.write_text(json.dumps(report))
        status, output, _ = self.run_main("--report", str(path), "--query", "CalcCRC32")
        self.assertEqual(status, 0)
        self.assertIn("executed 7 time(s)", output)
        status, _, errors = self.run_main("--report", str(path), "--query", "Absent")
        self.assertEqual(status, 1)
        self.assertIn("No file or function matches", errors)


if __name__ == "__main__":
    unittest.main()
