# Contributing

Contributors follow the same issue, branch, review and validation process. This document defines that process; [AGENTS.md](AGENTS.md) provides source and operating guidance. The [build guide](docs/BUILD_WINDOWS.md) and [testing guide](docs/TESTING.md) describe the supported Windows procedures.

## Ground rules

Keep each change focused and independently reviewable. Write contribution text in English, without emojis, using one line per prose paragraph. Preserve unrelated work and use an owned checkout. Publish only reproducible project facts; keep local paths, credentials, private resources and investigation diaries out of public reports.

Non-trivial implementation starts from a triaged issue. Maintainer-directed trivial or repository-administration work may be issue-exempt; describe the narrow exception in the PR. Such an exception does not waive validation or review.

## Issues

Use exactly one type label:

| Label | Purpose |
| --- | --- |
| `type:bug` | Existing behavior contradicts the intended contract |
| `type:feature` | Add user-visible functionality |
| `type:task` | Maintain code, tools, dependencies, tests or documentation |
| `type:research` | Answer a bounded question and record a conclusion |
| `type:epic` | Coordinate a finite outcome through sub-issues |
| `type:tracker` | Maintain an inventory or observation record |

Research normally produces separate implementation issues. Epics are decomposed into reviewable leaves; trackers do not own implementation.

The canonical label names and descriptions are in [.github/labels.json](.github/labels.json). Every triaged leaf has exactly one priority and at least one area. An epic or tracker may omit them when cross-cutting. Discovery labels such as `good first issue` and `help wanted` are optional.

### Status and ownership

Use at most one status label. A triaged, actionable, unassigned issue has no status label.

| Label | Meaning |
| --- | --- |
| `status:needs-triage` | Classification, scope or a maintainer decision is missing |
| `status:in-progress` | One primary assignee has claimed the work and identified its branch or PR |
| `status:in-review` | The assignee has a reviewable PR or research conclusion |
| `status:blocked` | A hard dependency or named external condition prevents completion |
| `status:deferred` | Accepted work is unscheduled, with a stated reconsideration trigger |

Close completed issues as completed and abandoned or superseded work as not planned. Closed issues need no status label. Do not add separate done/cancelled states or use labels to duplicate GitHub's open/closed state. Confirm reproduction in the issue evidence rather than treating it as a workflow status. A research conclusion may be reviewed in its issue without creating a code PR.

### Priority and areas

Priority describes scheduling. P0 is exceptional urgency such as data corruption or an unusable supported build; P1 is committed next work or a severe common defect; P2 is accepted normal backlog; P3 is low-impact or longer-term work. Maintainers own P0/P1 decisions. Keep priority out of titles.

Apply every area genuinely affected. The available areas cover storage, memory, gameplay, AI, input, UI, audio, startup, rendering, builds, dependencies, tests, documentation and releases. Broad scope is a reason to consider decomposition, not to hide affected areas. Platform labels are unnecessary while the supported product is Windows-only.

### Titles and bodies

Use `<Area>: <concise problem or outcome>`, with canonical area capitalization and a maximum of 100 characters. Use `AI` and `UI` for those abbreviations; other area names use title case. Cross-cutting epics and trackers may use `Project`. Bug titles describe observed behavior; tasks and features describe an outcome; research titles use an investigation verb. Omit priority, status, identities, branch names and issue numbers.

Before work is claimed, the body defines the problem or goal, evidence, scope and non-goals, acceptance criteria, validation, and relevant compatibility or lifetime constraints. Use `Scope`, `Acceptance criteria` and `Validation plan` headings so contributors and checks can find the stable contract. New bug reporters may leave planning to triage.

Keep the body current when decisions change the contract. Comments record durable findings, decisions and final verification, using the smallest useful evidence. Sanitize diagnostic excerpts and avoid daily logs or unrelated machine setup problems.

### Relationships

Use native parent/sub-issue relationships for decomposition, with at most three levels: epic, optional work package, leaf. A leaf has one structural parent. Do not duplicate GitHub's child list as a second progress checklist in the body.

Use native blocked-by dependencies only for hard prerequisites; shared context or preferred sequencing is not blocking. Parentage alone does not establish a dependency. Avoid cycles. An external blocker belongs under `External blocker`, with an unblock condition and the decision or resource needed. Deferred work names its reconsideration trigger under `Reconsideration`.

Link contextual work without assigning false ownership. Close duplicates or superseded issues as not planned with a link to the original or successor. Promote actionable tracker entries to scoped leaf issues. Do not generate a speculative backlog simply to populate a tracker.

## Branches

`dev` is the integration branch; `master` is the stable release line and default branch. Normal work branches from the current `dev` tip and PRs target `dev`. Release promotion and narrowly authorized hotfixes target `master`. An imported or historical branch is not a precedent for new work.

Use `<prefix>/<issue-number>-<short-slug>`:

| Prefix | Use |
| --- | --- |
| `feature/` | Feature implementation |
| `fix/` | Bug correction |
| `task/` | Maintenance, tools, dependencies or tests |
| `docs/` | Documentation-only work |
| `research/` | A mergeable investigation |
| `epic/` | Integration-only umbrella for an epic |
| `release/` | Maintainer-approved release preparation |
| `hotfix/` | Maintainer-approved repair of the stable line |

Use lowercase ASCII and hyphenated words, with one slash and at most 80 characters. Aim for a short, meaningful slug and a name under 60 characters. Include the issue number for non-trivial work; an approved issue-exempt task may omit it. Release branches may use a release identifier. Do not encode contributor or tool identities, or work status, in branch prefixes.

Reuse an owned checkout for serial tasks and switch only after the working tree is settled. Parallel tasks require separate owned worktrees. Never modify another contributor's checkout. Remove a completed worktree through Git after preserving its commits; do not keep it for hypothetical later use.

## Pull requests and review

Use the same area title convention, with an imperative PR title preferably under 72 characters and never over 100. Link one primary leaf issue using `Issue: #<number>` and the Development relationship. A final umbrella integration PR uses `Epic: #<number>` instead; an issue-exempt PR explains the authorized scope under `Issue exception`. Apply relevant area/discovery labels; do not copy issue type, priority or status labels onto the PR. Explain the change and its compatibility effects, include actual validation, and use draft state while work remains incomplete.

Required CI and an independent external approval must apply to the current revision before merging. An author, their subagents or another account they control cannot provide that approval. Resolve blocking threads and re-request review after substantive changes. Owner overrides are for expressly authorized recovery, not a substitute for routine review.

Review correctness, evidence and architecture, including determinism, serialized formats, lifecycle and hot-path cost where relevant. Offer concrete corrections. Distinguish introduced regressions from unrelated existing defects, and distinguish demonstrated costs from speculative optimizations. A useful non-blocking suggestion can become a scoped follow-up; keep unrelated follow-ups separate rather than hiding them in an approved patch.

Only maintainers merge after the acceptance criteria and current checks are satisfied. For PRs targeting `dev` or an epic umbrella, the maintainer records the merged PR and closes the implementing leaf issue manually. GitHub closing keywords operate on the default branch; see [GitHub's linking guidance](https://docs.github.com/en/issues/tracking-your-work-with-issues/using-issues/linking-a-pull-request-to-an-issue). Research closes when its question is answered. Epics and trackers are not closed merely because a linked PR merged.

### Epics

Create an integration-only `epic/<number>-<slug>` umbrella from the current `dev` tip. Each leaf branches from that umbrella and its PR targets it. Do not implement directly on the umbrella or open one oversized PR for the epic. Native relationships express leaf ownership and prerequisites.

After independent review and required checks, merge a finished leaf into the umbrella and close that leaf only when its own acceptance criteria are met. The epic stays open. Resolve integration defects and required follow-ups, then validate the combined umbrella and propose its final PR into `dev`. Only that reviewed merge, together with the epic's integration criteria, completes the epic. A tracker has no implementation branch.

## Validation

Choose checks in proportion to the affected behavior: focused synthetic regressions, relevant Debug/Release standalone builds, compiler/ABI checks, both sides of new compile-time options, and local playtests where rendering, input or sound matter. Consider initialization failure and cleanup, not only successful operation. Linux testing is outside the supported scope.

Record commands or reproducible steps and results. If a check is unavailable, state the relevant coverage gap without publishing machine-specific details. Follow [the testing guide](docs/TESTING.md); compilation alone does not validate a battle. Documentation and metadata-only changes need appropriate checks, not a full game rebuild.

## Releases

A release is a maintainer-approved, independently reviewed promotion from `dev` to `master`, with validation of the integrated standalone build and package. Use dated release tags in `YYYY.MM.DD` form, adding `.N` for another release on the same date. Tags and merged PRs record delivery; do not keep implementing issues open until promotion.

Use epics for cross-cutting planning rather than a second milestone/status system. Publishing a release or tag requires explicit authorization and must exclude private game resources and development state.

## Policy checks

The validator uses Python 3.9 or newer, with no third-party packages. Local catalog, branch and PR checks make no GitHub requests:

```text
python -B .github/scripts/check-contribution-policy.py catalog
python -B .github/scripts/check-contribution-policy.py branch
python -B .github/scripts/check-contribution-policy.py pr --input <pr-snapshot.json>
python -B -m unittest discover -s .github/scripts -p "test_*.py"
```

An approved numberless administrative branch is checked with `branch --allow-no-issue <reason>`. The optional [pre-push hook](.githooks/README.md) provides early feedback; it is not a security boundary or proof of approval.

The following audit reads GitHub metadata through authenticated `gh` and never changes it:

```text
python -B .github/scripts/check-contribution-policy.py issues --repo OWNER/REPOSITORY
```

It paginates open issues, excludes PRs and closed history, and checks label cardinality, title/area agreement, stable contract sections, ownership and blocker evidence. Some facts, such as reviewer independence, whether acceptance criteria are sufficient, and whether a PR is genuinely ready, require maintainer judgment. If relationship data cannot be read, the audit reports the gap instead of claiming verification. Canonical metadata can also be audited from a private JSON snapshot using `issues --input <snapshot.json>`.

Exceptions must be narrow and maintainer-authorized. Do not weaken a global rule to accommodate an old issue, branch or tool, and do not automatically migrate remote metadata without approval.
