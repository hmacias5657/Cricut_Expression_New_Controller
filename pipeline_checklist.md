# Pipeline Checklist Template

Copy this into each work unit's plan. Check off phases as they pass.

## Intake
- [ ] User request: <paste>
- [ ] Clear? [yes | no → ask, wait]
- [ ] Affected projects: [list]
- [ ] Size: [trivial | small | medium | large]
- [ ] Dependency graph: [independent | serial chain]

## Execute

### Trivial Pipeline (§0)
- [ ] Code
- [ ] Build (`pio run`)
- [ ] Commit

### Small Pipeline (§0)
- [ ] Explore (if unfamiliar)
- [ ] Code
- [ ] Build (`pio run`)
- [ ] Test (`pio test`)
- [ ] Commit

### Medium/Large Pipeline (§2)
- [ ] Clarify (if needed)
- [ ] Explore (parallel if multiple projects)
- [ ] Plan (decompose into work units)
- [ ] Code
- [ ] Build (`pio run`)
- [ ] Test Author
- [ ] Test Execute (`pio test`)
- [ ] Verify (Level 1: compile, Level 2: lint)
- [ ] Commit
- [ ] Docs (CHANGELOG, README if user-facing)
- [ ] Report

## Gate Results
- Build:  [pass | fail → back to coder]
- Test:   [pass | fail → back to coder]
- Verify: [pass | fail → back to coder]
- Commit: [pass | fail → manual intervention]