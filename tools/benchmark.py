# MatrixGame - licensed under GPLv2 or any later version.
"""Run the engine benchmark workloads and compare two result files.

run      starts matrixgame_benchmarks in several fresh processes and writes one
         combined JSON file with the revision, build configuration, host and
         every timed sample.
compare  checks that two results describe the same workloads and work, then
         classifies each timing difference as a material regression, a material
         improvement, no material change or inconclusive.

Only the Python standard library is used.
"""

import argparse
import datetime
import json
import platform
import random
import re
import subprocess
import sys
import tempfile
from pathlib import Path

SCHEMA = 'matrixgame-benchmark-run/1'
PROJECT = Path(__file__).resolve().parent.parent
# Fields that must agree before timings are comparable.
IDENTITY_FIELDS = (
    ('build', 'compiler'),
    ('build', 'configuration'),
    ('build', 'architecture'),
    ('build', 'engine_heap_tracking'),
    ('cmake', 'build_type'),
    ('cmake', 'dll'),
    ('cmake', 'cheats'),
    ('host', 'processor'),
    ('host', 'logical_processors'),
)


class BenchmarkError(Exception):
    """A clear reason why a run or comparison cannot produce trustworthy output."""


def quantile(values, q):
    ordered = sorted(values)
    if not ordered:
        raise BenchmarkError('No samples.')
    position = q * (len(ordered) - 1)
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def describe(values):
    median = quantile(values, 0.5)
    return {
        'count': len(values),
        'min': min(values),
        'p10': quantile(values, 0.1),
        'median': median,
        'p90': quantile(values, 0.9),
        'max': max(values),
        'mad': quantile([abs(value - median) for value in values], 0.5),
    }


def read_cache(build_dir):
    cache = build_dir / 'CMakeCache.txt'
    if not cache.is_file():
        raise BenchmarkError(f'{cache} is missing; build the configuration first.')
    values = {}
    for line in cache.read_text(encoding='utf-8', errors='replace').splitlines():
        match = re.match(r'^([A-Za-z0-9_]+):[A-Z]+=(.*)$', line)
        if match:
            values[match.group(1)] = match.group(2)
    return {
        'build_directory': build_dir.name,
        'build_type': values.get('CMAKE_BUILD_TYPE'),
        'dll': values.get('MATRIXGAME_BUILD_DLL'),
        'cheats': values.get('MATRIXGAME_CHEATS'),
        'coverage': values.get('MATRIXGAME_COVERAGE', 'OFF'),
    }


def revision():
    def git(*arguments):
        result = subprocess.run(['git', '-C', str(PROJECT), *arguments], capture_output=True, text=True,
                                check=False)
        if result.returncode != 0:
            raise OSError(result.stderr.strip())
        return result.stdout.strip()
    try:
        return {'commit': git('rev-parse', 'HEAD'),
                'dirty': bool(git('status', '--porcelain', '--untracked-files=no'))}
    except OSError as error:
        return {'commit': None, 'dirty': None, 'error': str(error) or 'git is unavailable'}


def run(arguments):
    build_dir = Path(arguments.build_dir).resolve()
    cmake = read_cache(build_dir)
    if cmake['coverage'].upper() in ('ON', 'TRUE', '1') and not arguments.allow_instrumented:
        raise BenchmarkError('Coverage instrumentation distorts timings; use an ordinary build directory.')
    executable = Path(arguments.executable) if arguments.executable else build_dir / 'tests/matrixgame_benchmarks.exe'
    if not executable.is_file():
        raise BenchmarkError(f'{executable} is missing; build the tests with tools/test.ps1 first.')
    if not (executable.parent / 'd3dx9_43.dll').is_file():
        raise BenchmarkError(f'd3dx9_43.dll is missing beside {executable}; run tools/test.ps1 once to stage it.')

    options = ['--warmup', str(arguments.warmup), '--repetitions', str(arguments.repetitions)]
    if arguments.iterations:
        options += ['--iterations', str(arguments.iterations)]
    if arguments.validation_delay_us:
        options += ['--validation-delay-us', str(arguments.validation_delay_us)]
    for workload in arguments.workload or []:
        options += ['--workload', workload]

    processes = []
    with tempfile.TemporaryDirectory(prefix='benchmark-', dir=str(build_dir)) as temporary:
        for index in range(arguments.processes):
            output = Path(temporary) / f'process-{index}.json'
            command = [str(executable), *options, '--output', str(output)]
            result = subprocess.run(command, cwd=str(executable.parent), capture_output=True, text=True,
                                    check=False)
            sys.stdout.write(result.stdout)
            if result.returncode != 0:
                raise BenchmarkError(f'Benchmark process {index} failed with exit code {result.returncode}:\n'
                                     f'{result.stderr.strip()}')
            processes.append(json.loads(output.read_text(encoding='utf-8')))
    return combine(processes, {
        'revision': revision(),
        'cmake': cmake,
        'os': platform.platform(),
        'command': ['matrixgame_benchmarks', *options],
        'generated_utc': datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0).isoformat(),
    })


def combine(processes, context):
    """Merge per-process reports; every process must have done identical work."""
    first = processes[0]
    for index, report in enumerate(processes[1:], start=1):
        for section in ('build', 'host'):
            if report[section] != first[section]:
                raise BenchmarkError(f'Process {index} reported a different {section}.')
    workloads = []
    for position, workload in enumerate(first['workloads']):
        samples, medians, allocations = [], [], []
        for index, report in enumerate(processes):
            other = report['workloads'][position]
            for key in ('name', 'parameters', 'iterations_per_sample', 'work_per_sample', 'inspection'):
                if other[key] != workload[key]:
                    raise BenchmarkError(f'Workload {workload["name"]} differs in {key} between processes 0 and '
                                         f'{index}; the work is not repeatable.')
            samples += other['samples_ns']
            medians.append(other['statistics_ns']['median'])
            if other.get('allocations_per_sample'):
                allocations.append(other['allocations_per_sample'])
        stable = all(item['stable'] for item in allocations) and all(
            item['allocations'][0] == allocations[0]['allocations'][0] for item in allocations)
        workloads.append({
            'name': workload['name'],
            'description': workload['description'],
            'parameters': workload['parameters'],
            'iterations_per_sample': workload['iterations_per_sample'],
            'work_per_sample': workload['work_per_sample'],
            'inspection': workload['inspection'],
            'samples_ns': samples,
            'process_medians_ns': medians,
            'statistics_ns': describe(samples),
            'allocations_per_sample': {
                'stable': stable,
                'allocations': allocations[0]['allocations'][0],
                'reallocations': allocations[0]['reallocations'][0],
                'frees': allocations[0]['frees'][0],
                'requested_bytes': allocations[0]['requested_bytes'][0],
            } if allocations else None,
        })
    return {
        'schema': SCHEMA,
        **context,
        'build': first['build'],
        'host': first['host'],
        'runner': {**first['runner'], 'processes': len(processes)},
        'workloads': workloads,
    }


def bootstrap_ratio(base, candidate, resamples, confidence, seed):
    """Percentile bootstrap interval of median(candidate) / median(base)."""
    generator = random.Random(seed)
    ratios = []
    for _ in range(resamples):
        b = [generator.choice(base) for _ in base]
        c = [generator.choice(candidate) for _ in candidate]
        ratios.append(quantile(c, 0.5) / quantile(b, 0.5))
    tail = (1.0 - confidence) / 2.0
    return quantile(ratios, tail), quantile(ratios, 1.0 - tail)


def classify(low, high, threshold, separated):
    if low > 1.0 + threshold:
        return 'regression' if separated else 'inconclusive'
    if high < 1.0 - threshold:
        return 'improvement' if separated else 'inconclusive'
    if low >= 1.0 - threshold and high <= 1.0 + threshold:
        return 'no material change'
    return 'inconclusive'


def compare(base, candidate, threshold=0.05, confidence=0.95, resamples=2000, seed=78,
            allow_identity_mismatch=False, allow_parameter_change=False):
    """Return (rows, problems). Problems make timing differences meaningless."""
    problems = []
    for section, field in IDENTITY_FIELDS:
        left = base.get(section, {}).get(field)
        right = candidate.get(section, {}).get(field)
        if left != right and not allow_identity_mismatch:
            problems.append(f'{section}.{field} differs: {left!r} versus {right!r}')
    candidates = {workload['name']: workload for workload in candidate['workloads']}
    rows = []
    for workload in base['workloads']:
        other = candidates.get(workload['name'])
        if other is None:
            problems.append(f'{workload["name"]} is missing from the candidate')
            continue
        if workload['parameters'] != other['parameters'] and not allow_parameter_change:
            problems.append(f'{workload["name"]} parameters differ')
            continue
        if workload['iterations_per_sample'] != other['iterations_per_sample']:
            problems.append(f'{workload["name"]} iterations per sample differ')
            continue
        if workload['work_per_sample'] != other['work_per_sample']:
            problems.append(f'{workload["name"]} performed different work: {workload["work_per_sample"]} versus '
                            f'{other["work_per_sample"]}')
            continue
        low, high = bootstrap_ratio(workload['samples_ns'], other['samples_ns'], resamples, confidence, seed)
        separated = ((min(other['process_medians_ns']) > max(workload['process_medians_ns'])) if low > 1.0 else
                     (max(other['process_medians_ns']) < min(workload['process_medians_ns'])))
        base_median = quantile(workload['samples_ns'], 0.5)
        candidate_median = quantile(other['samples_ns'], 0.5)
        rows.append({
            'name': workload['name'],
            'base_median_ms': base_median / 1e6,
            'candidate_median_ms': candidate_median / 1e6,
            'change': candidate_median / base_median - 1.0,
            'interval': (low - 1.0, high - 1.0),
            'base_noise': describe(workload['samples_ns'])['mad'] / base_median,
            'candidate_noise': describe(other['samples_ns'])['mad'] / candidate_median,
            'processes_separated': separated,
            'verdict': classify(low, high, threshold, separated),
            'allocations': (
                (workload.get('allocations_per_sample') or {}).get('allocations'),
                (other.get('allocations_per_sample') or {}).get('allocations'),
            ),
        })
    return rows, problems


def compare_command(arguments):
    base = json.loads(Path(arguments.base).read_text(encoding='utf-8'))
    candidate = json.loads(Path(arguments.candidate).read_text(encoding='utf-8'))
    for name, report in (('base', base), ('candidate', candidate)):
        if report.get('schema') != SCHEMA:
            raise BenchmarkError(f'The {name} file is not a combined benchmark result ({SCHEMA}).')
    rows, problems = compare(base, candidate, arguments.threshold, arguments.confidence, arguments.resamples,
                             arguments.seed, arguments.allow_identity_mismatch, arguments.allow_parameter_change)
    print(f'Base: {base["revision"].get("commit")} | candidate: {candidate["revision"].get("commit")}')
    print(f'Material threshold: {arguments.threshold:.1%}; {arguments.confidence:.0%} bootstrap interval of the '
          f'median ratio; regressions also require separated process medians.')
    for row in rows:
        low, high = row['interval']
        allocations = row['allocations']
        allocation_text = '' if allocations[0] is None else f', allocations {allocations[0]} -> {allocations[1]}'
        print(f'{row["name"]}: {row["base_median_ms"]:.3f} -> {row["candidate_median_ms"]:.3f} ms '
              f'({row["change"]:+.1%}, interval {low:+.1%}..{high:+.1%}, noise {row["base_noise"]:.1%}/'
              f'{row["candidate_noise"]:.1%} MAD){allocation_text}: {row["verdict"]}')
    for name, report in (('base', base), ('candidate', candidate)):
        delay = report.get('runner', {}).get('validation_delay_us')
        if delay:
            print(f'Note: the {name} includes a validation delay of {delay} us per sample.')
    for problem in problems:
        print(f'Not comparable: {problem}')
    if problems:
        return 2
    return 1 if any(row['verdict'] == 'regression' for row in rows) else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    runner = commands.add_parser('run', help='Run the workloads in fresh processes')
    runner.add_argument('--build-dir', default=str(PROJECT / 'build/mingw-release-exe'))
    runner.add_argument('--executable',
                        help='Benchmark executable (default: <build-dir>/tests/matrixgame_benchmarks.exe)')
    runner.add_argument('--output', required=True, help='Combined result file to write')
    runner.add_argument('--processes', type=int, default=3)
    runner.add_argument('--warmup', type=int, default=3)
    runner.add_argument('--repetitions', type=int, default=15)
    runner.add_argument('--iterations', type=int, default=0, help='Override iterations per sample')
    runner.add_argument('--workload', action='append', help='Workload name; repeat to select several')
    runner.add_argument('--validation-delay-us', type=int, default=0,
                        help='Add a fixed delay to every sample to check the comparison')
    runner.add_argument('--allow-instrumented', action='store_true', help='Permit a coverage build')
    comparer = commands.add_parser('compare', help='Compare a candidate result with a base result')
    comparer.add_argument('base')
    comparer.add_argument('candidate')
    comparer.add_argument('--threshold', type=float, default=0.05, help='Material relative change (default 0.05)')
    comparer.add_argument('--confidence', type=float, default=0.95)
    comparer.add_argument('--resamples', type=int, default=2000)
    comparer.add_argument('--seed', type=int, default=78)
    comparer.add_argument('--allow-identity-mismatch', action='store_true')
    comparer.add_argument('--allow-parameter-change', action='store_true')
    arguments = parser.parse_args(argv)
    try:
        if arguments.command == 'run':
            if arguments.processes < 1 or arguments.repetitions < 1 or arguments.warmup < 0:
                parser.error('processes and repetitions must be positive and warmup non-negative')
            result = run(arguments)
            output = Path(arguments.output)
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(json.dumps(result, indent=1) + '\n', encoding='utf-8')
            print(f'Benchmark result: {arguments.output}')
            return 0
        return compare_command(arguments)
    except BenchmarkError as error:
        print(f'Benchmark error: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
