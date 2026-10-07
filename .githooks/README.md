# Local Git hooks

The optional pre-push hook checks destination branch names using the contribution policy. Enable it in a contributor's own clone with:

```text
git config core.hooksPath .githooks
```

It allows deletion of historical branches and does not inspect or change GitHub state. It requires Python 3.9 or newer. A maintainer-authorized numberless administrative branch may use the narrowly scoped `CONTRIBUTION_ISSUE_EXCEPTION` environment variable containing the exception's reason for that push.

Local hooks can be bypassed; review and repository branch protection enforce the merge policy. Installing the hook or setting an exception does not authorize a push.
