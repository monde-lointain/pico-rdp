---
name: rescuing-ci-failures
description: Use when a GitHub Actions run is red on the current branch and the goal is to pull failed-step logs, triage, fix, verify locally, push, and iterate until green. Bounded loop with per-job error-signature dedup, iteration cap, local-verify-first, and category gate. Lean inline state (no helper scripts, no committed state).
---

# Rescuing CI Failures

When the `ci.yml` GitHub Actions run is red on the current branch, this skill
drives a bounded loop: resolve the run for the current HEAD → pull failed-job
logs → triage one job → dispatch a fix subagent → verify the fix locally → push
→ wait for the next run → repeat. The loop is hard-bounded by an iteration cap,
per-job same-signature dedup, and a category gate, so it cannot run away.

This is the lean adaptation of `sts2-ai`'s skill: same algorithm and stop
conditions, but **no helper script and no committed state** — iteration state
lives inline (TodoWrite + an uncommitted `/tmp/ci-rescue-<branch>.json` scratch
file). Unlike sts2-ai, this project reproduces the two ubuntu jobs **locally**,
so fixes are verified locally before paying the slow GHA round-trip.

## When to invoke

- A push comes back red on GitHub Actions.
- The user says "fix CI" / "make CI green" / "the build's broken".
- `/ci-rescue` slash command is the standard entry point.

## Preflight

```bash
gh auth status >/dev/null 2>&1 || { echo "gh not authed"; exit 12; }
git rev-parse --is-inside-work-tree >/dev/null || exit 12
branch="$(git rev-parse --abbrev-ref HEAD)"
```

Two **baseline-match preconditions** — the fix must be built against the code the
failing log actually describes:

1. **Clean tree.** `git status --porcelain` must be empty. Otherwise the
   subagent's commit would mix with the user's WIP — halt and ask the user to
   stash/commit first.
2. **HEAD matches the run.** Only triage a run whose `headSha == HEAD` (see
   run resolution). A run behind HEAD describes stale code.

**Branch == main is a warn, not a halt** (this project commits directly to main;
main is unprotected). On main, AskUserQuestion to confirm before proceeding —
note that CI red on main is usually an incident, not a routine rescue — and allow
opt-in. Push target is the existing upstream (`origin/<branch>`).

## Inline state

No helper script. Maintain a TodoWrite list for the loop, plus an uncommitted
scratch file `/tmp/ci-rescue-<branch>.json`:

```json
{
  "status": "running",
  "branch": "<branch>",
  "max_iterations": 6,
  "auto_push": false,
  "iteration_count": 0,
  "last_signature_by_job": { "linux-clang": "sha256:…" },
  "attempted_fixes": [
    { "iteration": 1, "job": "linux-clang", "category": "compiler-error",
      "summary": "…", "commit_sha": "…", "signature": "sha256:…" }
  ]
}
```

- `status` is an **already-running guard**: a second `/ci-rescue` invocation that
  finds `status == "running"` for this branch **resumes** rather than starting a
  parallel loop.
- `last_signature_by_job` is keyed **per job** — dedup is per job (see below).
- **Durability is bounded:** `/tmp` is the sole dedup memory across a context
  compaction; a reboot clears it and the loop resets. This is the accepted
  trade-off for keeping no committed state.
- **On green:** set `status: idle` (or delete the file) so a later invocation
  starts fresh.

## Algorithm

1. **Read state.** If `/tmp/ci-rescue-<branch>.json` `status == "running"` and the
   branch matches, resume; else init (`max_iterations=6`, `auto_push=false` unless
   overridden by `/ci-rescue` args).

2. **Resolve the run.** Filter by workflow so a future second workflow can't be
   grabbed:

   ```bash
   gh run list --branch "$branch" --workflow ci.yml --limit 1 \
     --json databaseId,status,conclusion,headSha,workflowName
   ```

   **Staleness guard.** The newest run can be *behind* local HEAD. If
   `run.headSha != HEAD`, the failure does not reflect current code — do NOT
   triage it:
   - HEAD unpushed → push it (with confirm) and wait for its run.
   - A run for HEAD merely in-flight → wait.

   Only triage a run whose `headSha == HEAD`.

3. **Branch on `conclusion`:**
   - `success` → set `status: idle`; done (exit 0).
   - `failure` → step 4.
   - `cancelled` → **project-specific:** ci.yml sets `cancel-in-progress: true`,
     so our own consecutive pushes mark older runs `cancelled`. A `cancelled` run
     is **benign and ignored** if a newer run exists for a later head SHA
     (re-resolve to the newest). Only escalate `cancelled` when it is the run for
     the *current* HEAD with no successor (a genuine cancel / runner abort).
   - `null` with `status` `in_progress`/`queued`/`pending` → **wait** (see Waiting).

4. **Pull failed-job logs via the REST API.** `gh run view --log[-failed]` is
   unreliable (exits 0 with empty output for matrix/multi-job/older runs). Use the
   per-job API path:

   ```bash
   repo="$(gh repo view --json nameWithOwner --jq .nameWithOwner)"
   run_id="<resolved id>"
   out="/tmp/ci-rescue-$run_id.log"; : > "$out"

   mapfile -t failed_jobs < <(gh run view "$run_id" --json jobs \
     --jq '.jobs[] | select(.conclusion=="failure") | "\(.databaseId)\t\(.name)"')

   if [[ ${#failed_jobs[@]} -eq 0 ]]; then
     echo "conclusion=failure but no failed jobs → workflow-level failure"; exit 12
   fi

   for line in "${failed_jobs[@]}"; do
     job_id="${line%%$'\t'*}"; name="${line#*$'\t'}"
     printf '\n========== JOB %s — %s ==========\n\n' "$job_id" "$name" >> "$out"
     gh api "repos/$repo/actions/jobs/$job_id/logs" >> "$out" 2>&1
   done
   ```

   API logs are timestamp-prefixed (`2026-05-16T13:54:37.4679598Z ##[error]…`);
   downstream regexes must tolerate the prefix (the signature step strips it).

5. **Select ONE job** (one job per iteration — see Why one job). When several
   matrix legs fail, pick by deterministic order so the fix can be verified fast:
   `linux-clang` → `pico-arm-rdp_core` → `windows-msvc` → `macos-appleclang`.
   Triage **that job's log section only**.

6. **Triage** the selected job into one category + compute its error signature.
   See Triage and Error signature.

7. **Dedup check** (per job): if `signature == last_signature_by_job[job]` →
   escalate `same-error-twice`; AskUserQuestion ("the prior fix didn't change this
   job's failure — review needed").

8. **Cap check:** if `iteration_count >= max_iterations` → escalate
   `iteration-cap`.

9. **Category gate:**
   - `transient` → `gh run rerun --failed "$run_id"` once (no code change, no
     iteration consumed); re-resolve at step 2. If it recurs identically, demote
     to `unactionable` and escalate.
   - `unactionable` → escalate with the matched pattern.
   - `conformance` → at most ONE fix attempt, then escalate to the user with the
     diff (likely a deep correctness bug). On escalation, the user decides whether
     the loop continues on the other still-failing jobs or stops.
   - `compiler-error` / `test-failure` → step 10.

10. **Dispatch the fix subagent** (see Subagent dispatch).

11. **Verify the subagent committed:**
    ```bash
    head_after="$(git rev-parse HEAD)"
    [[ "$head_after" != "$head_before" ]] || { echo "subagent did not commit"; exit 13; }
    git diff --quiet HEAD~1 HEAD && { echo "empty commit"; exit 13; }
    ```

12. **Verify locally (before push).** If the job is locally reproducible, run its
    mapped command and require green. See Local verification. win/mac legs are not
    locally reproducible — skip, and rely on the next CI run.

13. **Confirm push.** If `auto_push == true` AND the job is locally verified →
    push. Otherwise (always for win/mac) → AskUserQuestion with the diff summary;
    on rejection, escalate `divr`.

14. **Push** `git push`; capture the new `head_sha`.

15. **Record iteration** into the scratch file: append to `attempted_fixes`, set
    `last_signature_by_job[job] = signature`, bump `iteration_count`.

16. **Wait for the new run to appear**, then loop to step 2.

## Why one job per iteration

Aligns with `[[superpowers:systematic-debugging]]` — one hypothesis at a time.
`fail-fast: false` means matrix legs fail independently, so multiple jobs can red
in one run; each is consumed by a separate iteration. Fixing one job and exposing
a *different* failure in another job is expected progress, not a regression.

## Waiting

ETA rules of thumb for this project:

- `pico-arm-rdp_core` ≈ 5–8 min → foreground `gh run watch "$run_id" --exit-status`.
- `linux-clang` ≈ 10–20 min (builds LLVM + the orthodoxy plugin from source, plus
  SDL3/ImGui/gtest FetchContent) → do NOT block; `ScheduleWakeup` (≥270s)
  re-entering `/ci-rescue`, which reads `status: running` and resumes.
- windows/macos vary → `ScheduleWakeup`.

## Triage

Classify the **selected job** by the **failed STEP name first**, then by pattern.
The REST API log delineates steps (`##[group]Run …` / step headers). Keying on the
step disambiguates look-alike text — an orthodoxy *violation* (in the `Build`
step) versus an orthodoxy *plugin-build* failure (in the
`Install … Orthodoxy plugin` step) read similarly but need opposite handling.

| Failed step | Likely category |
|---|---|
| `Install … Orthodoxy plugin` (Linux) | `unactionable` (upstream orthodoxy master broke — not our code) |
| `Install SDL build deps` / `Fetch pico-sdk` / `Install ARM toolchain` | `transient` (network/apt/git fetch) |
| `Configure` / `Build` / `Build rdp_core` | `compiler-error` (incl. orthodoxy violations, ARM ILP32 width) |
| `Test` | `test-failure` or `conformance` |

Within the step, apply patterns (first match wins). API lines carry a timestamp
prefix; patterns are substrings/regexes that tolerate it.

| Pattern | Category |
|---|---|
| `\[orthodoxy::[a-z-]+\]` / `is not allowed \[orthodoxy::` | `compiler-error` (orthodoxy violation — **rewrite to the C subset**, never suppress) |
| `error:` / `fatal error:` / `ld:` / `undefined reference` / ILP32 `int`/`int32_t` width | `compiler-error` |
| conformance oracle mismatch in `tests/conformance/` (side-by-side diff vs Angrylion) | `conformance` |
| `\[  FAILED  \]` (gtest) / ctest `Failed` / `tests failed` | `test-failure` |
| FetchContent/network/`apt`/`git clone` failure, `timed out`, `429`, `no space left`, `connection refused`, runner quota | `transient` |
| orthodoxy-plugin **build** failure (in the Install step) / anything unclassifiable | `unactionable` |

Default → `unactionable`.

Note: `make format` / `clang-tidy` run **nowhere** in CI and there are **no commit
hooks**, so formatting/tidy is never a CI failure mode — ignore it in triage.

## Error signature

Deterministic SHA256 over a normalized failure fingerprint, computed over the
**selected job's** log section. Stored **per job** in `last_signature_by_job[job]`.

```bash
# API logs are timestamp-prefixed; strip first so anchored regexes work and
# timestamps don't pollute the fingerprint.
stripped="$(sed -E 's/^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9:.Z-]+ //' "$job_log")"

failed_markers="$(printf '%s\n' "$stripped" \
  | grep -E '^##\[error\]|\[  FAILED  \]|is not allowed \[orthodoxy::|error:|undefined reference' \
  | sort -u)"

fingerprint="$(printf '%s\n' "$stripped" \
  | grep -B5 -A0 -E '^##\[error\]|error:|\[orthodoxy::|\[  FAILED  \]|undefined reference|ctest.*Failed' \
  | sed -E 's/:[0-9]+:[0-9]+:/:LINE:COL:/g; s/0x[0-9a-fA-F]+/0xHEX/g' \
  | sort -u)"

signature="sha256:$(printf '%s\n%s' "$failed_markers" "$fingerprint" | sha256sum | cut -d' ' -f1)"
```

The `LINE:COL` / `0xHEX` normalizations prevent trivial off-by-one diffs from
defeating dedup. Dedup compares against `last_signature_by_job[job]`: fixing
`linux-clang` and exposing a new `macos` failure is NOT a hit; re-failing
`linux-clang` identically IS.

## Failed-job → local-repro mapping

| Failed job | Local command (verify the fix BEFORE push) |
|---|---|
| `linux-clang` | `make reconfig PRESET=host OPTIONS="-DCMAKE_CXX_COMPILER=clang++-22 -DRDP_REQUIRE_ORTHODOXY=ON"` → `cmake --build build-host --config Debug --parallel` → `ctest --preset host --build-config Debug --output-on-failure` |
| `pico-arm-rdp_core` | `export PICO_SDK_PATH=~/development/repos/pico-sdk` → `cmake --preset pico` → `cmake --build build-pico --target rdp_core --parallel` |
| `windows-msvc`, `macos-appleclang` | Not reproducible from this Linux host — read the log, reason, confirm the blind edit with the user, let the next CI run be the test |

## Local verification

Run the mapped command; require it green before pushing. Caveats:

- **Compiler pin.** The `host` preset pins plain `clang++`; CI overrides it. Pin
  `clang++-22` (the version the plugin is built against) or orthodoxy violations
  won't reproduce faithfully. The cached `build-host/` was configured with plain
  `clang++`, so a changed compiler errors — use `make reconfig` (rm -rf +
  re-preset) or a dedicated `build-rescue/` dir.
- **`make test` is a no-op under `PRESET=pico`** — verify the pico job via the
  direct `cmake --build build-pico --target rdp_core`, never `make test`.
- **Local-green is advisory, not authoritative.** Local clang/plugin/dep versions
  differ from CI (which rebuilds the plugin from master each run). A local pass
  gates the push; if CI then reds with the *same* signature, that's the
  `same-error-twice` escalation (likely an env divergence to surface).

## Subagent dispatch

Dispatch one general-purpose subagent per iteration. Model: **Sonnet 4.6** (these
are bounded mechanical fixes). Do NOT use `isolation: "worktree"` — the fix must
land on the user's branch directly.

Prompt template (fill the angle-bracketed slots):

```
CI failure on branch <branch> at <head_sha>, job <job_name>.

Failed log: /tmp/ci-rescue-<run_id>.log (focus on the "<job_name>" section)
Workflow: ci.yml
Triaged category: <category>
Iteration: <n> of <max>

Prior attempted fixes FOR THIS JOB (do NOT repeat — the signature must change):
- <prior fix summaries for this job only>

Your job:
1. Read the failed log section for this job.
2. Diagnose root cause. Apply [[superpowers:systematic-debugging]] — confirm the
   hypothesis with a code read before editing.
3. Apply a MINIMAL fix. Do NOT broaden scope, refactor unrelated code, or add
   unrequested features.
4. Commit on the current branch: "fix(ci): <one-line summary>".
5. DO NOT PUSH.

Return:
- The commit SHA (git rev-parse HEAD).
- A one-line summary of the change.
- If you cannot fix this mechanically, return the literal token "DIVR" + rationale.

Constraints:
- Respect Orthodox C++ (project CLAUDE.md). An orthodoxy violation
  ("is not allowed [orthodoxy::<rule>]") MUST be fixed by rewriting to the C
  subset — NEVER silence it with a `HERESY(<rule>)` suppression comment (that
  would pass CI while defeating the rule; the canary forbids exactly this).
- Do not edit .github/workflows/* unless the log shows a workflow YAML bug.
- Do not amend prior commits. Stay on the current branch; do not switch or rebase.
```

## Stop conditions and exit codes

| Code | Meaning | Action |
|---|---|---|
| 0 | Green — CI passes | `status: idle` |
| 10 | Iteration cap reached | escalate `iteration-cap` |
| 11 | Same per-job signature as prior attempt | escalate `same-error-twice` |
| 12 | Unactionable, preflight failure, or workflow-level failure (no failed jobs) | escalate `unactionable` / `gh-auth-failure` / `workflow-level-failure` |
| 13 | Fix subagent returned `DIVR` or user rejected the diff | escalate `divr` |

On any escalation, AskUserQuestion to surface the scratch-file contents (path +
last 2 attempted fixes + escalation reason) so the user can extend iterations,
switch strategy, or stop.

The user diff-confirm (when `auto_push=false`) is the real safety gate: there is
no `verifying-subagent-claims` skill here, so the inline check (HEAD changed +
non-empty diff) is the only mechanical guard against an irrelevant or over-broad
subagent edit.

## Cross-references

- [[superpowers:systematic-debugging]] — the fix subagent must use it.
- `.github/workflows/ci.yml` — the workflow this skill rescues.
- `cmake/Orthodoxy.cmake`, `.orthodoxy.yml`, `tests/orthodoxy_canary/` —
  orthodoxy enforcement (violations read `is not allowed [orthodoxy::<rule>]`).
- `docs/setup-llvm.md` — local orthodoxy toolchain (clang++-22 + plugin).
- `Makefile`, `CMakePresets.json` — build/test/repro commands.
- `.claude/commands/ci-rescue.md` — slash-command entry.
