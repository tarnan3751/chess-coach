**What and why**

<!-- The change in two or three sentences, and the issue it addresses. -->

**Checklist**

- [ ] `tools/gates.sh` passed on the last commit; its last lines are pasted below.
- [ ] Every commit is a Conventional Commit (`type(scope): Summary`, sentence case, no trailing
      period, at most 72 characters), one logical change each, rebased on `main`.
- [ ] No trailers in any commit message (no `Signed-off-by`, no `Co-authored-by`).
- [ ] The docs the change makes false are updated in the same change (`CLAUDE.md`,
      `docs/database.md`, `docs/reference/`).
- [ ] A visible change comes with a UI test, and its screenshots from `tools/ui_tests.sh` are
      attached.
- [ ] A new dependency is named in `NOTICE` with its licence text under `legal/`.

**Gate**

```
<the last lines of tools/gates.sh>
```

CONTRIBUTING.md gives the rules behind each line.
