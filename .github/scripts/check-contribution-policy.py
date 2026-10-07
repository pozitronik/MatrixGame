#!/usr/bin/env python3
"""Read-only checks for the contribution catalog, branches, PRs and issues.

Uses Python's standard library. Remote audits only invoke explicit GET requests
through gh; no command changes Git or GitHub state.
"""

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys


LABELS_PATH = Path(__file__).resolve().parents[1] / 'labels.json'
TOPIC_PREFIXES = {'feature', 'fix', 'task', 'docs', 'research', 'hotfix', 'epic'}
META_TYPES = {'type:epic'}
ACTIVE_STATUSES = {'status:in-progress', 'status:in-review'}
REPOSITORY_PATTERN = re.compile(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+')
PLACEHOLDERS = {'', 'none', 'n/a', 'not applicable', 'no response', 'tbd'}


def load_catalog(path=LABELS_PATH):
    rows = json.loads(Path(path).read_text(encoding='utf-8-sig'))
    if not isinstance(rows, list) or not rows:
        raise ValueError('label catalog must be a non-empty array')
    catalog = {}
    prefixes = set()
    for row in rows:
        if not isinstance(row, dict):
            raise ValueError('each label must be an object')
        name = row.get('name', '')
        if not isinstance(name, str) or not name or name != name.strip() or len(name) > 50:
            raise ValueError('invalid label name')
        if name in catalog:
            raise ValueError('duplicate label: ' + name)
        color = row.get('color', '')
        if not isinstance(color, str) or not re.fullmatch(r'[0-9A-Fa-f]{6}', color):
            raise ValueError('invalid label color: ' + name)
        description = row.get('description', '')
        if not isinstance(description, str) or not description.strip() or len(description) > 100:
            raise ValueError('invalid label description: ' + name)
        if name.startswith('area:'):
            prefix = row.get('title_prefix', '')
            if not re.fullmatch(r'[A-Z][A-Za-z -]*', prefix) or prefix in prefixes:
                raise ValueError('invalid or duplicate area title prefix: ' + name)
            prefixes.add(prefix)
        catalog[name] = row
    required = {
        'type': {'bug', 'feature', 'task', 'research', 'epic'},
        'status': {'needs-triage', 'in-progress', 'in-review', 'blocked', 'deferred'},
        'priority': {'P0', 'P1', 'P2', 'P3'},
    }
    for group, values in required.items():
        expected = {group + ':' + value for value in values}
        actual = {name for name in catalog if name.startswith(group + ':')}
        if actual != expected:
            raise ValueError('catalog does not match the supported ' + group + ' labels')
    return catalog


def validate_branch(name, exception_reason=None):
    errors = []
    if not isinstance(name, str) or not name:
        return ['branch name is empty; pass a name for a detached checkout']
    if len(name) > 80:
        errors.append('branch name exceeds 80 characters')
    if name in {'dev', 'master'}:
        return errors
    match = re.fullmatch(r'([a-z]+)/([a-z0-9]+(?:-[a-z0-9]+)*)', name)
    if not match or match[1] not in TOPIC_PREFIXES | {'release'}:
        return errors + ['use an allowed prefix and a lowercase hyphenated slug']
    prefix, slug = match.groups()
    numbered = bool(re.fullmatch(r'[1-9][0-9]*-[a-z0-9]+(?:-[a-z0-9]+)*', slug))
    if prefix != 'release' and not numbered:
        if prefix == 'epic' or not exception_reason or not exception_reason.strip():
            errors.append('an issue number is required; administrative exceptions must be explicit')
        elif re.match(r'[0-9]+-', slug):
            errors.append('issue numbers must be positive and have no leading zero')
    return errors


def label_names(item):
    return {label if isinstance(label, str) else label['name'] for label in item.get('labels', [])}


def title_area(title, catalog):
    if not isinstance(title, str):
        return None
    prefixes = {row['title_prefix']: name for name, row in catalog.items() if name.startswith('area:')}
    prefix, separator, _ = title.partition(': ')
    return prefixes.get(prefix) if separator else None


def validate_title(title, catalog, allow_project=False):
    if not isinstance(title, str):
        return ['title must be text']
    errors = []
    if len(title) > 100 or not title or title != title.strip() or '\n' in title or '\r' in title:
        errors.append('title must be one trimmed line of at most 100 characters')
    prefix, separator, outcome = title.partition(': ')
    if not separator or not outcome.strip() or (not title_area(title, catalog) and not (allow_project and prefix == 'Project')):
        errors.append('title must use a canonical <Area>: <problem or outcome> prefix')
    if title.endswith('.'):
        errors.append('title must not end with a period')
    if re.match(r'(?:\[[^\]]+\]|WIP\b)', outcome):
        errors.append('title must not encode priority or work status')
    return errors


def sections(body):
    """Find plain Markdown sections without treating fenced examples as headings."""
    if body is not None and not isinstance(body, str):
        raise ValueError('Markdown body must be text')
    found = {}
    current = None
    fence = None
    for line in (body or '').splitlines():
        marker = re.match(r'^\s*(`{3,}|~{3,})', line)
        if marker:
            char = marker[1][0]
            if fence == char:
                fence = None
            elif fence is None:
                fence = char
            continue
        if fence:
            continue
        heading = re.fullmatch(r'#{1,6}\s+(.+?)\s*#*\s*', line)
        if heading:
            current = heading[1].casefold().rstrip(':')
            found.setdefault(current, [])
        elif current:
            found[current].append(line)
    return {name: '\n'.join(lines).strip() for name, lines in found.items()}


def supplied(text):
    return text.strip().strip('_*').casefold() not in PLACEHOLDERS


def required_sections(body, names):
    content = sections(body)
    return ['missing stable contract section: ' + name for name in names if not supplied(content.get(name.casefold(), ''))]


def validate_issue(issue, catalog):
    if issue.get('state', 'open').casefold() != 'open' or issue.get('pull_request') is not None:
        return [], []
    errors, warnings = [], []
    labels = label_names(issue)
    unknown = sorted(labels - set(catalog))
    if unknown:
        errors.append('non-canonical labels: ' + ', '.join(unknown))
    groups = {prefix: sorted(name for name in labels if name.startswith(prefix + ':')) for prefix in ('type', 'status', 'priority', 'area')}
    if len(groups['type']) != 1:
        errors.append('exactly one type label is required')
    if len(groups['status']) > 1:
        errors.append('at most one status label is allowed')
    if len(groups['priority']) > 1:
        errors.append('at most one priority label is allowed')
    kind = groups['type'][0] if len(groups['type']) == 1 else None
    status = groups['status'][0] if len(groups['status']) == 1 else None
    errors += validate_title(issue.get('title', ''), catalog, allow_project=kind in META_TYPES)
    title_label = title_area(issue.get('title', ''), catalog)
    if groups['area'] and title_label and title_label not in labels:
        errors.append('title area must agree with at least one area label')
    if status != 'status:needs-triage':
        if kind not in META_TYPES:
            if len(groups['priority']) != 1:
                errors.append('triaged leaf issues require one priority')
            if not groups['area']:
                errors.append('triaged leaf issues require an area')
        errors += required_sections(issue.get('body'), ('Scope', 'Acceptance criteria', 'Validation plan'))
    assignees = issue.get('assignees', [])
    if len(assignees) > 1:
        errors.append('use one primary assignee')
    if status in ACTIVE_STATUSES and len(assignees) != 1:
        errors.append('active or review work requires one primary assignee')
    if status is None and assignees:
        errors.append('claimed work needs an explicit status; no status means available')
    content = sections(issue.get('body'))
    if status == 'status:deferred' and not supplied(content.get('reconsideration', '')):
        errors.append('deferred work requires a Reconsideration section')
    if status == 'status:blocked' and not supplied(content.get('external blocker', '')):
        blockers = issue.get('blocked_by')
        if blockers is None:
            warnings.append('native blocker data was not verified; inspect the dependency relationship')
        elif not any(item.get('state', 'open').casefold() == 'open' for item in blockers):
            errors.append('blocked work requires an open native dependency or an External blocker section')
    return errors, warnings


def ref_name(pr, name):
    value = pr.get(name + 'RefName', pr.get(name, ''))
    return value.get('ref', '') if isinstance(value, dict) else value


def validate_pr(pr, catalog):
    errors = validate_title(pr.get('title', ''), catalog, allow_project=True)
    labels = label_names(pr)
    if labels - set(catalog):
        errors.append('PR uses non-canonical labels')
    if any(name.startswith(('type:', 'status:', 'priority:')) for name in labels):
        errors.append('PRs must not copy issue type, status or priority labels')
    area = title_area(pr.get('title'), catalog)
    if area and any(name.startswith('area:') for name in labels) and area not in labels:
        errors.append('PR title area must agree with its area labels')
    base, head = ref_name(pr, 'base'), ref_name(pr, 'head')
    if base not in {'dev', 'master'} and not (base.startswith('epic/') and not validate_branch(base)):
        errors.append('PR must target dev, an epic umbrella, or authorized stable-line work')
    body = pr.get('body')
    if body is None:
        body = ''
    content = sections(body)
    exception = content.get('issue exception', '')
    errors += validate_branch(head, exception_reason=exception if supplied(exception) else None)
    if base == 'master' and head != 'dev' and not head.startswith(('release/', 'hotfix/')):
        errors.append('master accepts release promotion or maintainer-authorized release/hotfix work')
    if pr.get('isDraft', pr.get('draft', False)):
        return errors
    errors += required_sections(body, ('Summary', 'Design and compatibility', 'Validation', 'Documentation'))
    primary = re.findall(r'^Issue:\s*#([1-9][0-9]*)\s*$', body, flags=re.MULTILINE)
    epic = re.findall(r'^Epic:\s*#([1-9][0-9]*)\s*$', body, flags=re.MULTILINE)
    if len(primary) > 1 or len(epic) > 1 or (primary and epic):
        errors.append('use one primary issue or epic marker, without competing markers')
    branch_number = re.match(r'[^/]+/([1-9][0-9]*)-', head)
    if len(primary) == 1 and branch_number and primary != [branch_number[1]]:
        errors.append('primary issue and branch number disagree')
    if head.startswith('epic/') and base == 'dev':
        number = head.split('/')[1].split('-')[0]
        if epic != [number] or primary:
            errors.append('umbrella integration must link its epic, not a primary leaf')
    elif not supplied(exception) and head != 'dev' and not head.startswith('release/'):
        if len(primary) != 1:
            errors.append('ready leaf PR requires one primary Issue marker or an Issue exception section')
    return errors


def parse_pages(raw):
    """gh --paginate emits consecutive arrays; consume every page without a cap."""
    decoder = json.JSONDecoder()
    rows, offset = [], 0
    while offset < len(raw):
        while offset < len(raw) and raw[offset].isspace():
            offset += 1
        if offset == len(raw):
            break
        page, offset = decoder.raw_decode(raw, offset)
        if not isinstance(page, list):
            raise ValueError('expected an array for each API page')
        rows.extend(page)
    return rows


def read_api(endpoint):
    result = subprocess.run(['gh', 'api', '--method', 'GET', '--paginate', endpoint], check=True, capture_output=True, text=True, encoding='utf-8')
    return parse_pages(result.stdout)


def read_issues(repository):
    if not REPOSITORY_PATTERN.fullmatch(repository):
        raise ValueError('repository must be OWNER/REPOSITORY')
    issues = read_api('repos/' + repository + '/issues?state=open&per_page=100')
    warnings = []
    for issue in issues:
        if issue.get('pull_request') is not None or 'status:blocked' not in label_names(issue):
            continue
        try:
            number = int(issue['number'])
            issue['blocked_by'] = read_api('repos/' + repository + '/issues/' + str(number) + '/dependencies/blocked_by?per_page=100')
        except subprocess.CalledProcessError:
            warnings.append('could not read native dependencies for issue ' + str(issue['number']))
    return issues, warnings


def read_snapshot(path):
    raw = Path(path).read_bytes()
    encoding = 'utf-16' if raw.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig'
    return json.loads(raw.decode(encoding))


def findings(errors, warnings=()):
    for warning in warnings:
        print('warning: ' + warning)
    for error in errors:
        print('error: ' + error, file=sys.stderr)
    return 1 if errors else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    commands.add_parser('catalog')
    branch = commands.add_parser('branch')
    branch.add_argument('name', nargs='?')
    branch.add_argument('--allow-no-issue', metavar='REASON')
    pr = commands.add_parser('pr')
    pr.add_argument('--input', required=True, type=Path)
    issues = commands.add_parser('issues')
    source = issues.add_mutually_exclusive_group(required=True)
    source.add_argument('--repo')
    source.add_argument('--input', type=Path)
    args = parser.parse_args(argv)
    try:
        catalog = load_catalog()
        if args.command == 'catalog':
            print('validated ' + str(len(catalog)) + ' canonical labels')
            return 0
        if args.command == 'branch':
            name = args.name
            if name is None:
                name = subprocess.run(['git', 'branch', '--show-current'], check=True, capture_output=True, text=True).stdout.strip()
            result = findings(validate_branch(name, args.allow_no_issue))
            if not result:
                print('branch syntax verified' + ('; administrative exception requires maintainer authorization' if args.allow_no_issue else ''))
            return result
        if args.command == 'pr':
            snapshot = read_snapshot(args.input)
            if not isinstance(snapshot, dict):
                raise ValueError('PR snapshot must be an object')
            return findings(validate_pr(snapshot, catalog))
        rows, warnings = read_issues(args.repo) if args.repo else (read_snapshot(args.input), [])
        if not isinstance(rows, list):
            raise ValueError('issue snapshot must be an array')
        errors, count = [], 0
        for issue in rows:
            if not isinstance(issue, dict):
                raise ValueError('each issue snapshot entry must be an object')
            if issue.get('state', 'open').casefold() != 'open' or issue.get('pull_request') is not None:
                continue
            count += 1
            bad, gaps = validate_issue(issue, catalog)
            prefix = '#' + str(issue.get('number', '?')) + ': '
            errors.extend(prefix + text for text in bad)
            warnings.extend(prefix + text for text in gaps)
        result = findings(errors, warnings)
        print('audited ' + str(count) + ' open issues; remote state was not changed')
        return result
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        detail = getattr(error, 'stderr', None) or str(error)
        print('error: ' + detail.strip(), file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
