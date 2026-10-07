"""Contract tests for contribution checks; no network or game assets are used."""

import contextlib
import copy
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock


SCRIPT = Path(__file__).with_name('check-contribution-policy.py')
SPEC = importlib.util.spec_from_file_location('contribution_policy', SCRIPT)
POLICY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(POLICY)
CATALOG = POLICY.load_catalog()
TEMP_ROOT = SCRIPT.parents[2] / '.codex' / 'temp'
CONTRACT = '## Scope\nNested configurations.\n## Acceptance criteria\nValues survive a round trip.\n## Validation plan\nRun a synthetic regression.\n'
PR_CONTRACT = '## Summary\nPreserve values.\n## Design and compatibility\nFormat unchanged.\n## Validation\nSynthetic test passes.\n## Documentation\nContract documented.\n'


def issue(**changes):
    result = {'number': 42, 'state': 'open', 'title': 'Storage: preserve nested values', 'body': CONTRACT, 'labels': ['type:bug', 'priority:P1', 'area:storage'], 'assignees': []}
    result.update(changes)
    return result


def pr(**changes):
    result = {'title': 'Storage: preserve nested values', 'body': 'Issue: #42\n' + PR_CONTRACT, 'baseRefName': 'dev', 'headRefName': 'fix/42-preserve-nested-values', 'labels': ['area:storage'], 'isDraft': False}
    result.update(changes)
    return result


class BranchContracts(unittest.TestCase):
    def test_integration_stable_and_numbered_work(self):
        for name in ['dev', 'master', 'fix/42-round-trip', 'epic/52-stable-game', 'release/2026-10-07']:
            with self.subTest(name=name):
                self.assertEqual([], POLICY.validate_branch(name))

    def test_reject_ambiguous_or_personal_branch_syntax(self):
        for name in ['', 'chore/setup', 'codex/42-work', 'fix/0-game', 'fix/042-game', 'fix/42-Game', 'fix/42-game/nested', 'feature/game', 'fix/42-' + 'x' * 80]:
            with self.subTest(name=name):
                self.assertTrue(POLICY.validate_branch(name))

    def test_exception_is_explicit_and_cannot_create_an_unnumbered_epic(self):
        self.assertTrue(POLICY.validate_branch('docs/contribution-policy'))
        self.assertEqual([], POLICY.validate_branch('docs/contribution-policy', 'Authorized repository administration'))
        self.assertTrue(POLICY.validate_branch('epic/game', 'Administration'))
        self.assertTrue(POLICY.validate_branch('fix/042-game', 'Administration'))


class IssueContracts(unittest.TestCase):
    def test_available_backlog_uses_no_status(self):
        self.assertEqual(([], []), POLICY.validate_issue(issue(), CATALOG))

    def test_new_report_does_not_require_a_reporter_to_plan_implementation(self):
        self.assertEqual(([], []), POLICY.validate_issue(issue(labels=['type:bug', 'status:needs-triage'], body='A minimal reproduction.'), CATALOG))

    def test_closed_history_and_prs_are_excluded(self):
        for value in [issue(state='closed', title='Old title', labels=[]), issue(pull_request={'url': 'example'}, title='PR', labels=[])]:
            self.assertEqual(([], []), POLICY.validate_issue(value, CATALOG))

    def test_type_status_and_priority_are_not_ambiguous(self):
        for extra in ['type:task', 'priority:P2']:
            self.assertTrue(POLICY.validate_issue(issue(labels=issue()['labels'] + [extra]), CATALOG)[0])
        labels = issue()['labels'] + ['status:in-progress', 'status:in-review']
        self.assertTrue(POLICY.validate_issue(issue(labels=labels), CATALOG)[0])

    def test_active_work_has_exactly_one_owner(self):
        labels = issue()['labels'] + ['status:in-progress']
        self.assertTrue(POLICY.validate_issue(issue(labels=labels), CATALOG)[0])
        self.assertEqual(([], []), POLICY.validate_issue(issue(labels=labels, assignees=[{'login': 'contributor'}]), CATALOG))
        self.assertTrue(POLICY.validate_issue(issue(labels=labels, assignees=[{}, {}]), CATALOG)[0])
        self.assertTrue(POLICY.validate_issue(issue(assignees=[{}]), CATALOG)[0])

    def test_leaf_contract_is_required_before_work_is_claimed(self):
        self.assertTrue(POLICY.validate_issue(issue(body='A proposed fix.'), CATALOG)[0])
        self.assertTrue(POLICY.validate_issue(issue(labels=['type:task', 'area:storage']), CATALOG)[0])

    def test_blocked_state_requires_an_open_dependency_or_external_condition(self):
        labels = issue()['labels'] + ['status:blocked']
        self.assertTrue(POLICY.validate_issue(issue(labels=labels, blocked_by=[]), CATALOG)[0])
        self.assertTrue(POLICY.validate_issue(issue(labels=labels, blocked_by=[{'state': 'closed'}]), CATALOG)[0])
        self.assertEqual(([], []), POLICY.validate_issue(issue(labels=labels, blocked_by=[{'state': 'open'}]), CATALOG))
        body = CONTRACT + '## External blocker\nA required SDK distribution decision is pending.\n'
        self.assertEqual(([], []), POLICY.validate_issue(issue(labels=labels, body=body, blocked_by=[]), CATALOG))

    def test_unverified_native_data_is_reported_as_a_gap(self):
        errors, warnings = POLICY.validate_issue(issue(labels=issue()['labels'] + ['status:blocked']), CATALOG)
        self.assertEqual([], errors)
        self.assertTrue(warnings)

    def test_deferral_has_a_reconsideration_trigger(self):
        labels = issue()['labels'] + ['status:deferred']
        self.assertTrue(POLICY.validate_issue(issue(labels=labels), CATALOG)[0])
        body = CONTRACT + '## Reconsideration\nRevisit after the supported renderer contract is established.\n'
        self.assertEqual(([], []), POLICY.validate_issue(issue(labels=labels, body=body), CATALOG))

    def test_meta_issue_can_be_cross_cutting(self):
        self.assertEqual(([], []), POLICY.validate_issue(issue(title='Project: stabilize standalone battles', labels=['type:epic']), CATALOG))

    def test_title_and_area_agree(self):
        self.assertTrue(POLICY.validate_issue(issue(labels=['type:bug', 'priority:P1', 'area:audio']), CATALOG)[0])
        for title in ['[P0] Storage failure', 'Storage: [P0] lose values', 'Storage: preserve values.', 'storage: preserve values', 'Storage: one\ntwo']:
            self.assertTrue(POLICY.validate_issue(issue(title=title), CATALOG)[0])

    def test_fenced_examples_cannot_supply_missing_contract_sections(self):
        body = '```markdown\n' + CONTRACT + '```\n'
        self.assertTrue(POLICY.validate_issue(issue(body=body), CATALOG)[0])


class PullRequestContracts(unittest.TestCase):
    def test_ready_leaf_has_matching_branch_and_primary_issue(self):
        self.assertEqual([], POLICY.validate_pr(pr(), CATALOG))
        self.assertTrue(POLICY.validate_pr(pr(body='Issue: #43\n' + PR_CONTRACT), CATALOG))

    def test_draft_may_have_an_incomplete_body(self):
        self.assertEqual([], POLICY.validate_pr(pr(body='', isDraft=True), CATALOG))
        self.assertTrue(POLICY.validate_pr(pr(body=''), CATALOG))

    def test_stable_line_is_reserved_for_integration_or_authorized_repairs(self):
        self.assertTrue(POLICY.validate_pr(pr(baseRefName='master'), CATALOG))
        self.assertEqual([], POLICY.validate_pr(pr(baseRefName='master', headRefName='dev', body=PR_CONTRACT), CATALOG))
        self.assertEqual([], POLICY.validate_pr(pr(baseRefName='master', headRefName='hotfix/42-preserve-values'), CATALOG))

    def test_umbrella_integration_has_an_epic_marker_and_leaf_prs_target_the_umbrella(self):
        self.assertEqual([], POLICY.validate_pr(pr(headRefName='epic/52-stable-game', body='Epic: #52\n' + PR_CONTRACT), CATALOG))
        self.assertTrue(POLICY.validate_pr(pr(headRefName='epic/52-stable-game'), CATALOG))
        self.assertEqual([], POLICY.validate_pr(pr(baseRefName='epic/52-stable-game'), CATALOG))

    def test_exception_does_not_hide_competing_primary_markers(self):
        body = '## Issue exception\nAuthorized repository administration.\n' + PR_CONTRACT
        self.assertEqual([], POLICY.validate_pr(pr(headRefName='docs/contribution-policy', body=body), CATALOG))
        self.assertTrue(POLICY.validate_pr(pr(body='Issue: #42\nIssue: #43\n' + body), CATALOG))

    def test_issue_status_priority_and_type_do_not_belong_on_prs(self):
        for label in ['type:bug', 'status:in-review', 'priority:P1']:
            self.assertTrue(POLICY.validate_pr(pr(labels=['area:storage', label]), CATALOG))


class InputAndReadOnlyContracts(unittest.TestCase):
    def test_pagination_has_no_single_page_or_thousand_item_limit(self):
        first = [{'number': number} for number in range(1100)]
        second = [{'number': 2000}]
        self.assertEqual(first + second, POLICY.parse_pages(json.dumps(first) + '\n' + json.dumps(second)))
        with self.assertRaises(ValueError):
            POLICY.parse_pages('{"message":"not an array"}')

    def test_remote_audit_uses_only_get_and_skips_pr_dependency_requests(self):
        values = [issue(labels=issue()['labels'] + ['status:blocked']), issue(number=43, pull_request={'url': 'example'})]
        with mock.patch.object(POLICY, 'read_api', side_effect=[values, [{'state': 'open'}]]) as api:
            result, gaps = POLICY.read_issues('example/game')
        self.assertEqual([], gaps)
        self.assertEqual([{'state': 'open'}], result[0]['blocked_by'])
        self.assertEqual(2, api.call_count)
        with mock.patch.object(POLICY.subprocess, 'run', return_value=mock.Mock(stdout='[]')) as run:
            POLICY.read_api('repos/example/game/issues?state=open')
        self.assertEqual(['gh', 'api', '--method', 'GET', '--paginate', 'repos/example/game/issues?state=open'], run.call_args.args[0])

    def test_invalid_repository_never_reaches_gh(self):
        with mock.patch.object(POLICY, 'read_api') as api, self.assertRaises(ValueError):
            POLICY.read_issues('example/game?other=value')
        api.assert_not_called()

    def test_local_catalog_and_snapshots_support_windows_encoding(self):
        TEMP_ROOT.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=TEMP_ROOT) as directory:
            path = Path(directory) / 'snapshot.json'
            for encoding in ['utf-8-sig', 'utf-16']:
                path.write_text(json.dumps(pr()), encoding=encoding)
                self.assertEqual(pr(), POLICY.read_snapshot(path))
            with mock.patch.object(POLICY.subprocess, 'run') as run, contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(0, POLICY.main(['catalog']))
                self.assertEqual(0, POLICY.main(['pr', '--input', str(path)]))
            run.assert_not_called()

    def test_duplicate_or_incomplete_catalogs_are_rejected(self):
        TEMP_ROOT.mkdir(parents=True, exist_ok=True)
        rows = list(copy.deepcopy(CATALOG).values())
        with tempfile.TemporaryDirectory(dir=TEMP_ROOT) as directory:
            path = Path(directory) / 'labels.json'
            for bad in [rows + [rows[0]], [row for row in rows if row['name'] != 'type:bug']]:
                path.write_text(json.dumps(bad), encoding='utf-8')
                with self.assertRaises(ValueError):
                    POLICY.load_catalog(path)

    def test_github_event_validation_uses_the_untrusted_pr_as_data(self):
        TEMP_ROOT.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=TEMP_ROOT) as directory:
            path = Path(directory) / 'event.json'
            for payload, result in [({'pull_request': pr()}, 0), ({'pull_request': pr(title='Invalid title')}, 1), ({}, 2)]:
                path.write_text(json.dumps(payload), encoding='utf-8')
                with mock.patch.object(POLICY.subprocess, 'run') as run, contextlib.redirect_stderr(io.StringIO()), contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(result, POLICY.main(['pr', '--input', str(path), '--event']))
                run.assert_not_called()

    def test_malformed_local_snapshots_fail_without_network_or_a_traceback(self):
        TEMP_ROOT.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=TEMP_ROOT) as directory:
            path = Path(directory) / 'snapshot.json'
            for command, data in [('pr', []), ('issues', ['not an issue']), ('pr', pr(body={})), ('issues', [issue(body={})])]:
                path.write_text(json.dumps(data), encoding='utf-8')
                with mock.patch.object(POLICY.subprocess, 'run') as run, contextlib.redirect_stderr(io.StringIO()), contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(2, POLICY.main([command, '--input', str(path)]))
                run.assert_not_called()


if __name__ == '__main__':
    unittest.main()
