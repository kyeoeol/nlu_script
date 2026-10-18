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
      Windows (MSVC) for every pull request and every push to `main`
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
- [x] With free Actions minutes in the public repository: CI runs again on
      push to `main` (runs there are grouped by commit, so a later merge
      never cancels an earlier one), macOS x86_64 wheels build natively on
      the `macos-15-intel` runner, and the workflow comments no longer
      mention private-repository minutes
- [x] Each match reports the script line of the rule that produced it
      (`Match::line`, `nlu_result_line`, Python `Match.line`)
- [x] Script test files: example sentences with their expected CLI output,
      run by `nlu_script_cli --test SCRIPT TESTS...` (failures show the
      script line behind each actual output; exit status 1 on failure).
      `examples/demo.test` covers the demo and runs in `ctest`
- [x] Decomposed Hangul (NFD conjoining jamo) is composed into syllables in
      input and scripts before matching; slot text and offsets still refer
      to the original bytes
- [x] Lint (`Script::lint()`, `nlu_script_cli --lint`): label pairs that match
      a common input, each with the shortest such input (searched over the
      automaton of all rules, with `x` at wildcard positions), and entities
      and variables no rule uses. It found a real gap in the demo:
      `account:close` excluded only the Korean product names, so
      `deposit 해지해주세요` matched both labels. The exclusion now uses
      `@{product}`
- [x] Korean particle pairs (`을/를`, `이/가`, `으로/로`, ...) pick the form
      that fits the preceding syllable's final consonant, also after
      entities and wildcards. Compiled as guard edges (epsilon edges that
      test the previous input character), which the lazy DFA, Pike VM and
      lint follow. The demo uses them instead of `::(을|를)`, so wrong pairs
      like `적금를` no longer match
- [x] Ranking: every match has a specificity (input characters matched by
      pattern text rather than wildcards), and `match_best()` keeps only the
      labels whose most specific rule scores highest (C++, C, Python, and
      `nlu_script_cli --best`, also with `--test`)
- [x] `KO_NUMBER` system entity: numbers in digits and Korean numerals
      (`35,000`, `3만 5천`, `삼만오천`, `1억 2천만`) with a normalized `value`
      attribute computed when the match is built (a slot normalizer). The
      demo's Korean transfer amount uses it
- [x] `# lint: allow-overlap A B ...` comments mark intended overlaps (`*`
      for every label); they are reported as allowed, and mistakes in such
      comments are findings. The demo marks its three, and `ctest` lints it
- [x] `DATETIME` system entity (Korean and English): relative days, weeks,
      months and years, weekdays, calendar dates, parts of the day and clock
      times, normalized into `day_offset`, `week_offset`, `weekday`, `month`,
      `day`, `part_of_day`, `hour` (24-hour when the part of day says),
      `minute` and more; digits are range-checked by the grammar. System
      entity grammars moved to `src/system_entities.cpp`. The demo has an
      `agent:callback` label that uses it
- [x] Compile errors: every error is reported at once (`CompileError::errors()`,
      all lines in `what()` and the C error message, Python
      `CompileError.errors`, `path:line:column:` lines from the CLI). Broken
      label and definition lines don't cascade, and rules using a broken
      definition are skipped
- [x] Fuzzing: `fuzz/fuzz_script.cpp` (compile, match, lint) and
      `fuzz/fuzz_match.cpp` (every feature and system entity), run for a minute
      each in CI with libFuzzer, ASan and UBSan, plus a standalone mutation
      driver for Apple clang. The first CI run found a stack overflow: an
      entity referring to itself (`@a: x @{a}`, also through an exclusion or
      another definition) recursed forever; entities now get the same
      self-reference error as variables, and groups nest at most 100 deep.
      Probing found that nested repetitions could take gigabytes (4 levels:
      9.7 GB), so rules are capped at 500,000 automaton states and scripts at
      10 million. System entities now use a linear-size `Chain` node (KO_NUMBER
      went from about 25,000 states per use to under 5,000)
- [x] `include path` lines split a script across files (relative to the
      including file, each file read once). Errors, matches, lint findings
      and lint comments carry the file their line is in (`file` fields; the
      C API's `nlu_result_file`; Python `Match.file` and `Diagnostic`)

## Next

- [ ] More system entities (currency, ordinals, native Korean numbers like
      하나/둘/세, English number words)
- [ ] `DATETIME`: ranges (`3시부터 5시까지`, `from 3 to 5pm`), durations
      (`2시간 뒤`, `in 2 hours`), weekday after a date (`3월 5일 월요일`), a time
      before a date (`3pm tomorrow`), and a helper that resolves the attributes
      against a reference time
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
- [ ] Full Unicode NFC beyond Hangul (e.g. `e` + U+0301 for `é`): needs the
      Unicode composition tables, which Hangul's algorithmic composition did
      not
- [ ] Typo tolerance (edit distance 1) via a Levenshtein automaton; costs
      DFA states, so opt-in per rule or label
- [ ] Lint: rules subsumed by another rule of the same label (dead rules)
- [ ] Lint from the C API and Python
- [ ] Lint with `match_best()` in mind: an overlap where one label is always
      more specific doesn't need attention when callers use `match_best()`
- [ ] Lint `--verbose` to list allowed overlaps too
- [ ] Particle pairs inside exclusions: needs exclusion character classes
      split by final consonant (today a compile error)
- [ ] A wildcard before an optional particle swallows a wrong particle
      (`<?>+?(을/를)` matches `청약통장를` with `를` in the wildcard); consider
      keeping particle syllables out of a wildcard's last character
- [ ] Explain mode for a non-matching input: the rule that consumed the
      longest prefix and where it stopped
- [ ] The lazy DFA cache resets completely at 20,000 states (`kMaxStates`),
      so throughput drops right after a reset; consider evicting part of it
- [ ] Python access to script test files (e.g. `Script.run_tests(path)`)
- [ ] Publish wheels to PyPI (e.g. on a version tag)
- [ ] If GitHub retires the Intel macOS runner (`macos-15-intel`), build the
      x86_64 wheel on the arm64 runner instead: cross-compile (pass
      `ARCHFLAGS` to CMake as `CMAKE_OSX_ARCHITECTURES`) and test under
      Rosetta 2

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
