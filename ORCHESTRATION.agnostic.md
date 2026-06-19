# Subagent Orchestration Framework (project-agnostic core)

A reusable framework for decomposing software-engineering tasks into (optionally
parallel) subagent work, with model-selection guidance, right-sized pipelines and
mandatory incremental commits.

This document is **project-agnostic**. Anything specific to a concrete codebase —
build commands, language checks, version schemes, per-project paths, audit logs —
lives in the companion annexes:

- **`PROJECTS.md`** — per-project registry (commands, paths, conventions, ladders).
- **`LESSONS.md`** — portfolio-specific lessons and anti-patterns.

> Conventions used below: `<convention-file>` = the repo's agent/convention doc
> (e.g. `AGENTS.md`, `CLAUDE.md`, `CONTRIBUTING.md`); `<build-cmd>` / `<test-cmd>` /
> `<lint-cmd>` come from the project's entry in `PROJECTS.md`. Wherever a command,
> filename or check appears as a placeholder, resolve it from `PROJECTS.md` — never
> hardcode it here.

---

## 0. Right-Sizing the Pipeline (read first)

The full pipeline is a **ceiling, not a floor**. Match ceremony to task size so a
one-line fix doesn't pay for nine phases.

| Task size | Pipeline to run |
|-----------|-----------------|
| **Trivial** (typo, comment, one-line, config value) | code → quick check (build/syntax) → commit. No envelope, no subagents. |
| **Small** (single function/module, one project) | explore (if unfamiliar) → code → build → test → commit → docs (if user-facing). Inline, usually a single agent. |
| **Medium/Large** (multi-module or multi-project, parallelizable) | Full pipeline (§2) with role subagents and the artifact envelope (§3). |

Spawn a **subagent only when it adds parallelism or isolation**. Mechanical steps
(build, lint, `git add/commit`) are cheaper run **inline** than as their own agent —
spawning an agent to run one command usually costs more than the command. Use the
role *concept* (§1) to pick the right rigor; use a separate *agent* only when work
is genuinely independent or needs an isolated context.

---

## 1. Role Definitions

Every task decomposes into these roles (concepts, not necessarily separate agents).
Match model strength to cognitive load.

| Role | Cognitive Load | Best For | Mechanical? | Allowed tools (least privilege) |
|------|---------------|----------|-------------|--------------------------------|
| **explore** | Medium | Understanding current state | Partially | Read-only: read/search/list. No write, no exec. |
| **planner (orchestrator)** | High | Decomposition, sequencing, dependency analysis | No | Orchestration only (spawn/coordinate). No write, no exec. |
| **coder** | High | Implementation following conventions | No | Read + edit/write; scoped exec if needed. No `git push`/destructive. |
| **builder** | Low | Compiling/packaging | Yes (run inline) | Exec **only** the project's build command(s). |
| **test author** | High | Designing and writing test files | No | Read + edit to author tests; exec test runner for validation. |
| **test executor** | Low | Running test suites, reporting results | Yes (run inline) | Exec **only** the test runner; read-only otherwise. |
| **verifier** | Low | Cross-module consistency, lint, import checks | Yes (run inline) | Read-only + static lint/typecheck commands. No write. |
| **git** | Low | Staging & committing | Yes (run inline) | Exec **only** `git add/status/diff/commit` (never `push`/`reset --hard`). |
| **docs** | Medium | Convention-file / CHANGELOG / README updates | Partially | Read + edit/write to docs files only. |

### Least-Privilege Permissions (reduce permission prompts safely)

Each role/subagent should be granted **only** the tools above, and the harness should
**pre-approve the exact mechanical commands** so long sessions don't re-prompt every
few minutes. Two levers:

1. **Scope tools per subagent** — a `git` agent gets only git Bash; a `verifier`
   gets read-only; a `builder` gets only the build command. Narrow scope = fewer
   surprise prompts and safer with weaker/local models.
2. **Persistent allowlist** — register the concrete repeating commands (build, test,
   lint, `git add/commit/status/diff`) in the harness's permission allowlist so they
   run without prompting. Read/search tools are usually allow-by-default.

**Always keep prompting (never allowlist):** `git push`, `git reset --hard`, `rm -rf`,
force flags, package publish, deploy. Pair the rule "weakest model for mechanical work"
with "tightest permissions for mechanical work".

> Concrete per-project allow-rules (e.g. `Bash(pio run:*)`, `Bash(pytest:*)`) live in
> `PROJECTS.md` → "Allowlist por proyecto", not here.

### Model Selection Principle

Use the **weakest model that still does the job correctly**. Reserve strong
reasoning for planning, coding and test debugging.

| Role | Model Tier | Why |
|------|-----------|-----|
| explore | Medium | Read & summarize, not create |
| planner | Strong | Wrong decomposition wastes all downstream work |
| coder | Strong | Code quality drives build/test outcome |
| builder | Light | Mechanical command execution |
| test author | Strong | Test design requires reasoning about edge cases |
| test executor | Light | Mechanical command execution |
| verifier | Light | Mechanical pattern checks |
| git | Light | Fully scriptable |
| docs | Medium | Prose needs readability |

### Verification Ladder (mechanism)

The verifier runs a two-level ladder **defined per language in `PROJECTS.md`**:

- **Level 1 — Always:** language-intrinsic checks that need no extra tooling
  (e.g. imports resolve, signatures match, structure intact).
- **Level 2 — If tooling exists:** linters/type-checkers/static analysis the
  project already configures.

Keep the *concrete* per-language checklists in `PROJECTS.md` so this core stays
language-neutral.

---

## 2. The Pipeline (per work unit)

For Medium/Large units. Trivial/Small units use the reduced flow in §0.

```
0. CLARIFY      ← if ambiguous, ask before exploring
1. EXPLORE      ← only if unfamiliar with the project
2. PLAN         ← orchestrator decomposes & sequences (serial by nature)
        │
        ├──────── parallel across independent units ────────┐
        ▼                                                    ▼
3. CODE          (one coder per project / per independent module)
4. BUILD         (after its coder)
5. TEST AUTHOR   (after its build)
5b. TEST EXECUTE (after test author)
6. VERIFY+LINT   (after its tests; ladder from PROJECTS.md)
        └───────────────── all branches join ───────────────┘
                              ▼
7. COMMIT        (per project, after verify passes)
8. DOCS          (after commit)
9. REPORT        (orchestrator → user)
```

| Phase | Owner | Gate | Parallelism |
|-------|-------|------|-------------|
| 0 Clarify | orchestrator | Ask if ambiguous | 1 |
| 1 Explore | explore | Before planning | Across projects (read-only) |
| 2 Plan | orchestrator | Before any coder | 1 |
| 3 Code | coder | — | 1 per project / per independent module |
| 4 Build | builder | After its coder | All builds in parallel |
| 5a Test Author | test-author | After its build | All suites in parallel |
| 5b Test Executor | test-executor | After test-author | All suites in parallel |
| 6 Verify | verifier | After its tests | All in parallel |
| 7 Commit | git | After verify passes | Per project |
| 8 Docs | docs | After commit | In parallel |
| 9 Report | orchestrator | After docs | 1 |

### Phase 0 — Clarify

If the request is ambiguous, **stop and ask** before exploring: which project(s),
expected vs observed behavior, acceptance criteria, constraints, priority.

---

## 3. Artifact Envelope (inter-phase handoff)

For Medium/Large units, each phase appends to a shared envelope so downstream
agents have full context without re-reading the project. (Skip for Trivial/Small.)

```
ArtifactEnvelope:
  project:        <name>
  branch:         <git branch>
  head_before:    <sha before changes>
  head_after:     <sha after changes>        # set by git phase
  files_changed:  [paths]
  diff_summary:   <short description>

  explore:  { status, findings }
  coder:    { status, files[], conventions }
  builder:  { status, command, output(last lines), errors }
  test-author:  { status, files_created[], test_count, coverage_areas[] }
  test-executor: { status, command, total, passed, failed, skipped, failures[], duration }
  verifier: { status, checks[], issues[] }
  git:      { status, commit_hash, staged_files[] }
  docs:     { status, files_updated[] }
```

The orchestrator passes the envelope into each subagent; the subagent returns it
augmented.

---

## 4. Parallelism Rules

| Scenario | Parallel? | Rationale |
|----------|-----------|-----------|
| Different projects | ✅ | Independent codebases/repos/build systems |
| Independent modules, same project | ✅ | Only if no shared structures change (confirm via explore) |
| Same file / tightly coupled | ❌ | Merge-conflict risk → single coder |
| Build → Test → Verify (same project) | ❌ | Sequential dependency |
| Multiple coders, same project | ❌ | Conflicts guaranteed |
| Coder + Docs, same project | ❌ | Docs must reflect actual changes |
| Explore across projects | ✅ | Read-only |

When in doubt: **serial within a project, parallel across projects.**

### Sibling / Mirror Sync Protocol (when applicable)

When two codebases mirror the same logic (ports, language twins, shared spec):

```
1. Designate one as MASTER (more mature/active).
2. Change MASTER first → build → test → commit → docs.
3. Replicate to SLAVE, adapting only platform/API differences.
4. Build → test → commit → docs in SLAVE.
5. SLAVE commit references MASTER: "(port from master: <sha>)".
6. Keep shared invariants IDENTICAL (list them in PROJECTS.md per pair);
   `diff` them before committing the slave.
```

The concrete invariants and project pairs live in `PROJECTS.md`.

---

## 5. Gate Rules

```
ambiguous request          → STOP, ask, wait
explore fails              → STOP, report, ask for guidance
coder done                 → build
build fails                → STOP → back to coder
build ok                   → test
test fails                 → STOP → back to coder
test ok                    → verify
verify fails               → STOP → back to coder
verify ok                  → commit
commit fails (conflict/hook)→ STOP → manual intervention
commit ok                  → docs → REPORT
```

No phase proceeds until the current one passes; parallel branches join only when
all finish.

---

## 6. Mandatory Incremental Commit Policy

Every successful change is committed immediately — incremental, revertable history.

| Rule | Detail |
|------|--------|
| **Trigger** | Build + Test + Verify pass (for projects without tests, build/syntax success is the gate) |
| **Scope** | Stage tracked-modified files + new source files explicitly produced. |
| **Never stage** | Build artifacts & junk: `__pycache__`, `.DS_Store`, `*.bin`, `*.elf`, `build/`, `dist/`, `.pio/`, `node_modules/`, and anything in `.gitignore`. |
| **Pre-existing untracked files** | Leave unstaged; warn the orchestrator, don't block. |
| **Pre-commit hooks** | If the repo has hooks, run them first; on failure, report — never `--no-verify`. |
| **Push** | Never, unless the user asks. |
| **Amend** | Never; always a fresh commit. |
| **Branch** | If on the default branch, create a feature branch first (project policy may vary). |

**Message format:**

```
<project-or-scope>: <imperative verb> <what changed>

- <file>: <what and why>
- <file>: <what and why>
```

---

## 7. Versioning (capability — apply by artifact type)

Versioning rigor should **scale with deployability**, not be forced on everything.

| Artifact type | Versioning expectation |
|---------------|------------------------|
| **Deployable binary/firmware/installer** | Full: SemVer + monotonic build counter, injected at build, shown in filename + runtime; `-dev`/`-<branch>` off main. |
| **Published package/library** | SemVer (and registry version); build counter optional. |
| **Service / web app** | SemVer or release tag + git short hash; surface in a `/version` or About. |
| **Script / one-off / internal tool** | Lightweight: git short hash or a single version constant. Build counter optional. |

**Principles (when versioning applies):**
- Single source of truth for the version (one file/string), injected at build —
  not duplicated across macros.
- Prefer a **monotonic build counter** over timestamps (comparable).
- Make the running version **identifiable** (filename and/or runtime/about/`--version`).
- Distinguish dev from release builds.

Concrete injection mechanisms per build system live in `PROJECTS.md`.

---

## 8. Error Recovery & Timeouts

| Error | Action |
|-------|--------|
| Build failure | Report output → back to coder or ask. |
| Test regression | Report failing tests + diff; never commit red. |
| Git conflict | Report conflicted files; don't auto-resolve — ask. |
| Dirty repo at start | `git status` first; if dirty, ask before proceeding. |

**Timeouts** are ceilings (defaults; tune per project in `PROJECTS.md`): explore ~2m,
coder ~10m, build ~5m, test ~5m, verify ~2m, git ~1m. On overrun: report partial
state and ask to continue/abort.

**Rollback:** single bad commit `git revert <sha>`; range `git revert <old>..<new>`;
discard uncommitted `git reset --hard <known-good>`.

---

## 9. Speed Principles

1. Right-size first (§0) — don't over-orchestrate small work.
2. Explore once, cache results for the whole session.
3. Fail fast — build/test before spending verify/commit effort.
4. Parallel at the project level; single coder per project.
5. Mechanical steps inline; subagents only for real parallelism/isolation.
6. Strong models for creative work, light models for mechanical.
7. Commit small and atomic; never push without asking.

---

## 10. Orchestrator Task Template

```
## Intake
User request: <paste>
Clear? [yes | no → ask, wait]
Affected projects: [list]
Size: [trivial | small | medium | large]   → pick pipeline (§0)
Dependency graph: [independent | mirror/ports | shared libs]

## Execute
- [ ] Clarify (if needed) → wait
- [ ] Explore (parallel, if unfamiliar): branch, dirty files, commands, key files
- [ ] Plan: work units, parallel vs serial, assignments
- [ ] Code → Build → Test → Verify  (parallel across units)
- [ ] Commit (per project) → Docs
- [ ] Report: what changed, build/test status, commits
```

---

## 11. Annex Pointers

- Per-project commands, paths, conventions, language ladders, sync pairs, version
  injection → **`PROJECTS.md`**.
- Audited mistakes and anti-patterns from real projects → **`LESSONS.md`**.

Keep this core free of project names, absolute paths and language-specific checks.
If you find one creeping in, move it to an annex.
