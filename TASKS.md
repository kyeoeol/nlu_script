# Tasks

## Done

- [x] Pattern language: soft spaces, `::`, `[ ]`, `( )`, `|`, `~[ ]~`, `[ ]^`,
      quoted literals, reserved special symbols
- [x] Wildcards `<?>`, `<?>*?`, `<?>+?` and exclusions `\X`
- [x] Domain entities `@{ }`, regex variables `@var.{ }`, system entities
      `` `@{ } `` (`NUMBER`, `TELNUM`)
- [x] Slots `<name>…</name>` with attributes `{key=value}` / `{slot.key=value}`
- [x] Script format: labels, indented patterns, definitions, comments
- [x] Compile errors with line and column
- [x] Single shared automaton: lazy DFA for classification, Pike VM for slots
- [x] C++17 API, ANSI C API, Python package (ctypes), CLI
- [x] Tests: C++ behavior tests, C API test, Python tests (all run by `ctest`)
- [x] Pike VM cost bounded to the matched rule's states (match time no longer
      grows with total rule count)
- [x] Examples, demo and tests use this project's own data (customer-support
      inquiries to banking and payment apps) instead of the guide's, and cover
      Korean and English side by side
- [x] README credits the NCP chatbot guide and the talk the project started from
- [x] Simplification pass: one epsilon-closure implementation shared by the
      lazy DFA and exclusion compilation (its visit counter is now unsigned,
      so it can't overflow), definition values parsed in one place, per-rule
      data kept in one struct, Python `Slot`/`Match` as namedtuples
- [x] Concurrent `match()` calls classify in parallel: cached DFA transitions
      are read under a shared lock, and only new transitions take the
      exclusive lock (classification on 4 threads: about 3x one thread's
      throughput, up from about 1.6x)
- [x] Static and shared libraries are built from one CMake `OBJECT` library,
      so each source compiles once
- [x] CI (GitHub Actions): CMake build and `ctest` on Linux, macOS and
      Windows (MSVC) for every pull request
- [x] Windows build: MSVC compiles with `/utf-8` so UTF-8 string literals
      survive; README notes `--config` / `-C` for multi-config generators
- [x] Python wheels in CI (cibuildwheel): `py3-none` wheels for Linux
      (manylinux x86_64), macOS (arm64) and Windows (x64, static C++
      runtime), each tested on CPython 3.9 to 3.15
- [x] One `CI passed` check that summarizes every CI job, to require before
      merging into `main` (and to auto-merge on)
- [x] Wheel platforms: Linux aarch64 (native arm64 runner) and musllinux
      (x86_64 and aarch64)
- [x] CI stays within the free Actions minutes: CI runs on pull requests
      only (not again on push to `main`), a new push cancels the run it
      replaces, wheels move to their own workflow (on demand, or when the
      packaging changes) with artifacts kept 7 days, and the workflow token
      is read-only
- [x] MIT license: `LICENSE` at the root, `license="MIT"` in `setup.py`, and
      the license file copied into the Python package so wheels carry it
- [x] `main` ruleset (the repository is public now): pull request required,
      `CI passed` required and branch up to date, no force pushes or
      deletions, no bypass. Checked on a throwaway PR (#11) with a failing
      test: `CI passed` failed rather than being skipped, and the merge was
      blocked
- [x] Repository settings: auto-merge allowed, merged branches deleted,
      workflows from every external contributor's pull request wait for
      approval, workflow token read-only
- [x] Security: private vulnerability reporting, Dependabot alerts and
      security updates, secret scanning with push protection, CodeQL default
      setup (C/C++, Python, Actions; first scan found nothing)
- [x] Dependabot version updates for GitHub Actions, weekly and grouped
      (`.github/dependabot.yml`)

## Next

- [ ] Datetime extractor: a bundled script of datetime rules plus a
      normalizer (relative days like `day="+1"`, `minute`, `part_of_day`), with
      a short how-to
- [ ] More system entities (dates, times, currency, ordinals, Korean numerals)
- [ ] `TELNUM` for non-Korean formats (e.g. `+1 555-123-4567`,
      `(555) 123-4567`); English sentences work today, but only with Korean
      numbers
- [ ] Optionally ignore trailing punctuation in input (`are you there?`)
      instead of requiring `"?"` in every pattern
- [ ] Unicode case folding beyond ASCII; optional full-width/half-width folding
- [ ] Save and load compiled scripts so large rule sets don't need recompiling
- [ ] Bound the lazy DFA cache by memory as well as by state count, and expose
      stats for it
- [ ] Profile full-match time at 50k+ rules (about 33 µs vs about 5 µs at 10
      rules; classification alone stays flat at about 1.3 µs)
- [ ] Concurrent classification still contends on the shared lock's cache
      line; per-thread DFA caches would remove it at the cost of memory per
      thread
- [ ] Search mode: find slots inside free text without `<?>*?` padding
- [ ] Let an exclusion reach a wildcard inside a following slot
      (`\X <slot><?>*?</slot>`); today the exclusion has to go inside the slot
- [ ] macOS x86_64 wheels (cross-compile on the arm64 runner and test under
      Rosetta 2; needs `ARCHFLAGS` passed to CMake as
      `CMAKE_OSX_ARCHITECTURES`). Left out for now: macOS minutes count 10x
      against the private repository's free Actions minutes
- [ ] Publish wheels to PyPI (e.g. on a version tag)
- [ ] The repository is public, so standard runners no longer use paid
      minutes: revisit what was cut to save them (CI on push to `main`,
      macOS x86_64 wheels) and drop "private repository" from the workflow
      comments

## Open questions

- [x] Exclusion semantics: the guide's second exclusion example (text that
      only starts with the excluded name, rejected by `\[<?>*? X] <?>*? …`)
      contradicted its first example. Decided: the implemented behavior is
      the spec. A wildcard is rejected only when the exclusion matches its
      whole text. Use `\[<?>*? X <?>*?]` to exclude
      text that contains `X`.
- [ ] Should `#{form}` and `${action.method}` (answer templates in the chatbot
      builder) be part of this library, or stay with the response layer?
- [ ] The original description mentions the OpenFst library. The automaton is
      implemented in-house (no dependency). Revisit if OpenFst interop or
      weighted rules are needed.
