# MatrixGame - licensed under GPLv2 or any later version.
"""Summarize gcov coverage of a MATRIXGAME_COVERAGE build by production area.

The collector runs the pinned MinGW gcov on every coverage note (.gcno) in the
build directory, merges the per-object JSON reports by source file and writes
build/<name>/coverage/coverage.json plus coverage-summary.md. Only the Python
standard library is used.
"""

import argparse
import datetime
import json
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path, PurePosixPath

SCHEMA = 'matrixgame-coverage/1'

# Production areas, in report order. Paths are relative to the source root.
AREAS = (
    ('engine', 'MatrixGame/src/'),
    ('base', 'MatrixLib/Base/'),
    ('graphics', 'MatrixLib/3G/'),
    ('bitmap', 'MatrixLib/Bitmap/'),
)
EXCLUDED = ('tests', 'third_party', 'toolchain', 'other')
SOURCE_SUFFIXES = {'.c', '.cpp'}
HEADER_SUFFIXES = {'.h', '.hpp'}
GCOV_BATCH = 64
MISSING_DATA = re.compile(r"cannot open data file, assuming not executed", re.IGNORECASE)
NO_FUNCTIONS = re.compile(r"^(.*[.]gcno):no functions found$", re.IGNORECASE)


class CoverageError(Exception):
    """A clear, user-facing reason why no trustworthy report can be produced."""


def classify(path):
    """Return the area or exclusion category of a normalized source path."""
    if path is None or path.startswith('.tools/'):
        return 'toolchain'
    for area, prefix in AREAS:
        if path.startswith(prefix):
            return area
    if path.startswith('tests/'):
        return 'tests'
    if path.startswith(('ThirdParty/', 'build/')):
        return 'third_party'
    return 'other'


def normalize(path, working_directory, source_root):
    """Map a gcov file name to a source-root-relative POSIX path, or None if outside it."""
    text = path.replace('\\', '/')
    candidate = Path(text)
    if not candidate.is_absolute() and not re.match(r'^[A-Za-z]:/', text):
        # The build maps the source root to an empty prefix, so relative names
        # are already source-relative. Fall back to the compiler directory.
        if (source_root / text).exists() or not working_directory:
            return str(PurePosixPath(text))
        candidate = Path(working_directory) / text
    try:
        relative = os.path.relpath(os.path.normpath(str(candidate)), str(source_root))
    except ValueError:
        return None
    relative = relative.replace('\\', '/')
    if relative.startswith('../') or relative == '..':
        return None
    return relative


class FileCoverage:
    def __init__(self, path):
        self.path = path
        self.lines = {}
        self.functions = {}
        self.branches = {}
        self.with_data = False

    def add(self, record, has_data):
        self.with_data = self.with_data or has_data
        for line in record.get('lines', []):
            number = line['line_number']
            self.lines[number] = self.lines.get(number, 0) + line.get('count', 0)
            counts = [branch.get('count', 0) for branch in line.get('branches', []) if not branch.get('throw')]
            if counts:
                previous = self.branches.get(number)
                if previous is None or len(counts) > len(previous):
                    self.branches[number] = counts
                elif len(counts) == len(previous):
                    self.branches[number] = [a + b for a, b in zip(previous, counts)]
        for function in record.get('functions', []):
            key = (function.get('start_line', 0), function.get('start_column', 0))
            name = function.get('demangled_name') or function.get('name', '')
            entry = self.functions.setdefault(key, {'name': name, 'line': key[0], 'count': 0})
            entry['count'] += function.get('execution_count', 0)

    def totals(self):
        branch_total = sum(len(counts) for counts in self.branches.values())
        branch_covered = sum(1 for counts in self.branches.values() for count in counts if count > 0)
        return {
            'lines': len(self.lines),
            'lines_covered': sum(1 for count in self.lines.values() if count > 0),
            'functions': len(self.functions),
            'functions_covered': sum(1 for entry in self.functions.values() if entry['count'] > 0),
            'branches': branch_total,
            'branches_covered': branch_covered,
        }


def ranges(numbers):
    """Compress sorted line numbers into 'a-b' ranges."""
    result = []
    start = previous = None
    for number in sorted(numbers):
        if start is None:
            start = previous = number
        elif number == previous + 1:
            previous = number
        else:
            result.append(str(start) if start == previous else f'{start}-{previous}')
            start = previous = number
    if start is not None:
        result.append(str(start) if start == previous else f'{start}-{previous}')
    return ','.join(result)


def merge(documents, source_root):
    """Merge gcov JSON documents (with a data-present flag) into per-file coverage."""
    files = {}
    for document, has_data in documents:
        working_directory = document.get('current_working_directory', '')
        for record in document.get('files', []):
            path = normalize(record['file'], working_directory, source_root)
            key = path if path is not None else '<external>/' + record['file'].replace('\\', '/')
            coverage = files.get(key)
            if coverage is None:
                coverage = files[key] = FileCoverage(path)
            coverage.add(record, has_data)
    return files


def empty_totals():
    return {'files': 0, 'files_with_data': 0, 'lines': 0, 'lines_covered': 0, 'functions': 0,
            'functions_covered': 0, 'branches': 0, 'branches_covered': 0}


def percent(covered, total):
    return round(100.0 * covered / total, 2) if total else None


def summarize(files):
    groups = {name: empty_totals() for name, _ in AREAS}
    groups.update({name: empty_totals() for name in EXCLUDED})
    entries = []
    for key in sorted(files):
        coverage = files[key]
        category = classify(coverage.path)
        totals = coverage.totals()
        group = groups[category]
        group['files'] += 1
        group['files_with_data'] += 1 if coverage.with_data else 0
        for name, value in totals.items():
            group[name] += value
        if category in dict(AREAS):
            uncovered = [number for number, count in coverage.lines.items() if count == 0]
            entries.append({
                'path': coverage.path,
                'area': category,
                'linked_into_executed_test': coverage.with_data,
                **totals,
                'uncovered_lines': ranges(uncovered),
                'functions_detail': [
                    {'name': entry['name'], 'line': entry['line'], 'count': entry['count']}
                    for _, entry in sorted(coverage.functions.items())
                ],
            })
    for group in groups.values():
        group['line_percent'] = percent(group['lines_covered'], group['lines'])
        group['function_percent'] = percent(group['functions_covered'], group['functions'])
        group['branch_percent'] = percent(group['branches_covered'], group['branches'])
    production = empty_totals()
    for name, _ in AREAS:
        for field in production:
            production[field] += groups[name][field]
    production['line_percent'] = percent(production['lines_covered'], production['lines'])
    production['function_percent'] = percent(production['functions_covered'], production['functions'])
    production['branch_percent'] = percent(production['branches_covered'], production['branches'])
    return groups, production, entries


def uninstrumented(source_root, files):
    """Production files on disk that no compiled object in this build describes."""
    seen = {coverage.path for coverage in files.values() if coverage.path}
    sources, headers = [], []
    for area, prefix in AREAS:
        root = source_root / prefix
        if not root.is_dir():
            continue
        for item in sorted(root.rglob('*')):
            if not item.is_file():
                continue
            relative = item.relative_to(source_root).as_posix()
            if relative in seen:
                continue
            if item.suffix.lower() in SOURCE_SUFFIXES:
                sources.append({'path': relative, 'area': area})
            elif item.suffix.lower() in HEADER_SUFFIXES:
                headers.append({'path': relative, 'area': area})
    return sources, headers


def read_cache(build_dir):
    cache = build_dir / 'CMakeCache.txt'
    if not cache.is_file():
        raise CoverageError(f'{cache} is missing; configure the build with tools/build.ps1 -Coverage.')
    values = {}
    for line in cache.read_text(encoding='utf-8', errors='replace').splitlines():
        match = re.match(r'^([A-Za-z0-9_]+):[A-Z]+=(.*)$', line)
        if match:
            values[match.group(1)] = match.group(2)
    if values.get('MATRIXGAME_COVERAGE', '').upper() not in ('ON', 'TRUE', '1'):
        raise CoverageError(f'{build_dir} is not a coverage build (MATRIXGAME_COVERAGE is not ON).')
    return values


def tool_version(executable, argument):
    try:
        result = subprocess.run([str(executable), argument], capture_output=True, text=True, check=False)
    except OSError as error:
        raise CoverageError(f'Cannot run {executable}: {error}') from error
    if result.returncode != 0:
        raise CoverageError(f'{executable} {argument} failed: {result.stderr.strip()}')
    return result.stdout.strip().splitlines()[0] if result.stdout.strip() else ''


def version_number(text):
    match = re.search(r'(\d+\.\d+\.\d+)\s*$', text) or re.search(r'(\d+\.\d+\.\d+)', text)
    return match.group(1) if match else ''


def revision(source_root):
    def git(*arguments):
        result = subprocess.run(['git', '-C', str(source_root), *arguments], capture_output=True, text=True,
                                check=False)
        if result.returncode != 0:
            raise OSError(result.stderr.strip())
        return result.stdout.strip()
    try:
        commit = git('rev-parse', 'HEAD')
        dirty = bool(git('status', '--porcelain', '--untracked-files=no'))
    except OSError as error:
        return {'commit': None, 'dirty': None, 'error': str(error) or 'git is unavailable'}
    return {'commit': commit, 'dirty': dirty}


def test_results(build_dir):
    report = build_dir / 'test-results.xml'
    if not report.is_file():
        return None
    root = ElementTree.parse(report).getroot()
    suite = root if root.tag == 'testsuite' else root.find('testsuite')
    if suite is None:
        return None
    return {name: int(suite.get(name, '0')) for name in ('tests', 'failures', 'errors', 'skipped')}


def object_source(note, build_dir):
    """Recover the source path of a CMake object from its note path, if possible."""
    parts = note.relative_to(build_dir).parts
    for index in range(1, len(parts)):
        if parts[index].endswith('.dir') and parts[index - 1] == 'CMakeFiles':
            pieces = ['..' if piece == '__' else piece for piece in parts[:index - 1] + parts[index + 1:]]
            path = PurePosixPath(os.path.normpath(os.path.join(*pieces)).replace(os.sep, '/')).as_posix()
            return path[:-len('.gcno')] if path.endswith('.gcno') else path
    return None


def same_file(first, second):
    return os.path.normcase(os.path.abspath(first)) == os.path.normcase(os.path.abspath(second))


def run_gcov(gcov, notes, source_root, empty):
    """Yield (document, has_data) for each note file; reject stale or unreadable data.

    Notes of translation units that compiled to no functions are appended to ``empty``.
    """
    for start in range(0, len(notes), GCOV_BATCH):
        batch = notes[start:start + GCOV_BATCH]
        command = [str(gcov), '--json-format', '--stdout', '--branch-probabilities', *map(str, batch)]
        result = subprocess.run(command, cwd=str(source_root), capture_output=True, text=True, check=False)
        problems = []
        skipped = []
        for line in result.stderr.splitlines():
            match = NO_FUNCTIONS.match(line.strip())
            if match:
                skipped.append(match.group(1))
            elif line.strip() and not MISSING_DATA.search(line):
                problems.append(line)
        if result.returncode != 0 or problems:
            detail = '\n'.join(problems[:10]) or f'exit code {result.returncode}'
            raise CoverageError(f'gcov rejected the coverage data:\n{detail}\nRebuild and rerun the tests.')
        documents = [json.loads(line) for line in result.stdout.splitlines() if line.strip()]
        if len(documents) != len(batch):
            raise CoverageError(f'gcov returned {len(documents)} reports for {len(batch)} note files.')
        for note, document in zip(batch, documents):
            if any(same_file(str(note), path) for path in skipped):
                empty.append(note)
            yield document, note.with_suffix('.gcda').is_file()


def collect(arguments):
    source_root = Path(arguments.source_root).resolve()
    build_dir = Path(arguments.build_dir).resolve()
    if not build_dir.is_dir():
        raise CoverageError(f'Build directory {build_dir} does not exist.')
    cache = read_cache(build_dir)
    gcov = Path(arguments.gcov)
    if not gcov.is_file():
        raise CoverageError(f'gcov was not found at {gcov}; coverage needs the pinned MinGW toolchain.')
    gcov_version = tool_version(gcov, '--version')
    compiler = Path(arguments.compiler) if arguments.compiler else gcov.with_name('g++.exe')
    compiler_version = tool_version(compiler, '-dumpfullversion') if compiler.is_file() else ''
    if compiler_version and version_number(gcov_version) != compiler_version:
        raise CoverageError(f'gcov {version_number(gcov_version)} does not match compiler {compiler_version}.')

    notes = sorted(build_dir.rglob('*.gcno'))
    if not notes:
        raise CoverageError(f'No coverage notes (.gcno) below {build_dir}; build with tools/build.ps1 -Coverage.')
    data = list(build_dir.rglob('*.gcda'))
    if not data:
        raise CoverageError(f'No coverage data (.gcda) below {build_dir}; run tools/test.ps1 -Coverage first.')

    empty_notes = []
    files = merge(run_gcov(gcov, notes, source_root, empty_notes), source_root)
    groups, production, entries = summarize(files)
    if production['files_with_data'] == 0 or production['lines_covered'] == 0:
        raise CoverageError('The coverage data contains no executed production code.')
    without_code = sorted({path for path in (object_source(note, build_dir) for note in empty_notes)
                           if path and classify(path) in dict(AREAS)})
    sources, headers = uninstrumented(source_root, files)
    sources = [item for item in sources if item['path'] not in without_code]

    report = {
        'schema': SCHEMA,
        'generated_utc': datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0).isoformat(),
        'revision': revision(source_root),
        'configuration': {
            'build_directory': build_dir.name,
            'build_type': cache.get('CMAKE_BUILD_TYPE'),
            'dll': cache.get('MATRIXGAME_BUILD_DLL'),
            'cheats': cache.get('MATRIXGAME_CHEATS'),
            'coverage': cache.get('MATRIXGAME_COVERAGE'),
        },
        'toolchain': {'compiler': compiler_version or None, 'gcov': gcov_version},
        'tests': {'results': test_results(build_dir), 'filter': arguments.test_filter or None,
                  'excluded_labels': arguments.excluded_label or []},
        'inputs': {'notes': len(notes), 'data_files': len(data)},
        'denominator': (
            'Executable lines, functions and non-exception branches that gcov reports for production files '
            'in every object compiled by this build. Objects that no executed test linked count as '
            'unexecuted. Header lines are merged across translation units.'
        ),
        'exclusions': {
            'tests': 'tests/: test drivers, fixtures and benchmarks',
            'third_party': 'ThirdParty/, dependency builds and generated build-tree headers',
            'toolchain': 'compiler, runtime and SDK headers outside the source tree',
            'other': 'any other source-tree path outside the production areas',
        },
        'areas': {name: groups[name] for name, _ in AREAS},
        'production': production,
        'excluded': {name: groups[name] for name in EXCLUDED},
        'compiled_without_code': without_code,
        'uninstrumented_sources': sources,
        'uninstrumented_headers': headers,
        'files': entries,
    }
    output = Path(arguments.output_dir) if arguments.output_dir else build_dir / 'coverage'
    output.mkdir(parents=True, exist_ok=True)
    (output / 'coverage.json').write_text(json.dumps(report, indent=1) + '\n', encoding='utf-8')
    summary = render(report)
    (output / 'coverage-summary.md').write_text(summary, encoding='utf-8')
    print(summary, end='')
    print(f'Coverage report: {output / "coverage.json"}')
    return report


def cell(value):
    return '-' if value is None else f'{value:.2f}%'


def render(report):
    revision_info = report['revision']
    commit = revision_info.get('commit') or 'unknown'
    if revision_info.get('dirty'):
        commit += ' (uncommitted changes)'
    configuration = report['configuration']
    tests = report['tests']['results']
    lines = [
        '# Coverage summary',
        '',
        f'- Revision: {commit}',
        f'- Configuration: {configuration["build_type"]}, DLL {configuration["dll"]}, cheats {configuration["cheats"]}',
        f'- Toolchain: GCC {report["toolchain"]["compiler"]}; {report["toolchain"]["gcov"]}',
        f'- Tests: ' + (f'{tests["tests"]} run, {tests["failures"]} failed, {tests["errors"]} errors'
                        if tests else 'no JUnit report found')
        + (f' (filter `{report["tests"]["filter"]}`)' if report['tests']['filter'] else '')
        + (f'; excluded labels: {", ".join(report["tests"]["excluded_labels"])}'
           if report['tests'].get('excluded_labels') else ''),
        f'- Inputs: {report["inputs"]["notes"]} note files, {report["inputs"]["data_files"]} data files',
        f'- Denominator: {report["denominator"]}',
        '',
        '| Area | Files (with data) | Lines | Line coverage | Functions | Function coverage | Branch coverage |',
        '| --- | --- | --- | --- | --- | --- | --- |',
    ]

    def row(name, group):
        return (f'| {name} | {group["files"]} ({group["files_with_data"]}) | '
                f'{group["lines_covered"]}/{group["lines"]} | {cell(group["line_percent"])} | '
                f'{group["functions_covered"]}/{group["functions"]} | {cell(group["function_percent"])} | '
                f'{cell(group["branch_percent"])} |')

    for name, group in report['areas'].items():
        lines.append(row(name, group))
    lines.append(row('production', report['production']))
    lines += ['', 'Excluded from the production totals:', '',
              '| Category | Files | Lines | Line coverage |', '| --- | --- | --- | --- |']
    for name, group in report['excluded'].items():
        lines.append(f'| {name} | {group["files"]} | {group["lines_covered"]}/{group["lines"]} | '
                     f'{cell(group["line_percent"])} |')
    without_code = report['compiled_without_code']
    lines += ['', f'Compiled sources without executable code: {len(without_code)}']
    lines += [f'- {path}' for path in without_code]
    sources = report['uninstrumented_sources']
    lines += ['', f'Production sources not compiled into this build: {len(sources)}']
    lines += [f'- {item["path"]}' for item in sources]
    lines += [f'Production headers without executable code in this build: {len(report["uninstrumented_headers"])}',
              '', 'Branch totals exclude exception edges and are approximate where headers are merged.', '']
    return '\n'.join(lines)


def query(arguments):
    if arguments.report:
        report_path = Path(arguments.report)
    else:
        report_path = Path(arguments.build_dir) / 'coverage' / 'coverage.json'
    if not report_path.is_file():
        raise CoverageError(f'{report_path} is missing; collect coverage first.')
    report = json.loads(report_path.read_text(encoding='utf-8'))
    pattern = re.compile(arguments.query)
    matched = 0
    for entry in report['files']:
        if pattern.search(entry['path']):
            matched += 1
            print(f'{entry["path"]}: lines {entry["lines_covered"]}/{entry["lines"]}, '
                  f'functions {entry["functions_covered"]}/{entry["functions"]}; '
                  f'unexecuted lines {entry["uncovered_lines"] or "none"}')
        for function in entry['functions_detail']:
            if pattern.search(function['name']):
                matched += 1
                print(f'{entry["path"]}:{function["line"]}: {function["name"]}: executed {function["count"]} time(s)')
    if not matched:
        raise CoverageError(f'No file or function matches {arguments.query!r}.')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', help='Coverage build directory, for example build/mingw-debug-exe-coverage')
    parser.add_argument('--source-root', default=str(Path(__file__).resolve().parent.parent))
    parser.add_argument('--gcov', help='gcov executable from the toolchain that compiled the build')
    parser.add_argument('--compiler', help='g++ executable used to check the gcov version')
    parser.add_argument('--output-dir', help='Report directory (default: <build-dir>/coverage)')
    parser.add_argument('--test-filter', help='CTest selection recorded in the report')
    parser.add_argument('--excluded-label', action='append', help='CTest label excluded from the run')
    parser.add_argument('--query', help='Print files and functions matching a regular expression from a report')
    parser.add_argument('--report', help='Existing coverage.json to query')
    arguments = parser.parse_args(argv)
    try:
        if arguments.query:
            if not arguments.report and not arguments.build_dir:
                parser.error('--query needs --report or --build-dir')
            query(arguments)
        else:
            if not arguments.build_dir or not arguments.gcov:
                parser.error('collection needs --build-dir and --gcov')
            collect(arguments)
    except CoverageError as error:
        print(f'Coverage error: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
