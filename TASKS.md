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
- [x] Explain mode: `Script::explain(text, limit)` (C API, Python,
      `nlu_script_cli --explain`) lists the rules that got furthest into an
      input, what they matched and what they expected next, from one pass
      over all rules
- [x] Python runs script test files: `Script.run_tests(path, best=False)`
      returns a `TestReport` with each failure's line, input, expected and
      actual output
- [x] Lint: dead rules (every input matched first by earlier rules of the
      label, or no input at all), found exactly in the same search; overlaps
      say which label `match_best()` always picks (`best_label`, proven with
      wildcard-free copies of the rules); `LintReport::findings(best)` gives
      the lines `--lint` prints, also from the C API (`nlu_script_lint`) and
      Python (`Script.lint`); the CLI adds `--lint --best` and `--verbose`
- [x] `DATETIME`: ranges (`end_` attributes, part of day carried across),
      durations (signed `*_offset`s), a weekday after a date, a time before a
      date; `resolve_datetime()` (C++, C, Python) resolves the attributes
      against the caller's `now` with a grain (Monday weeks, next occurrence
      for a weekday or date given alone)
- [x] System entities `EN_NUMBER` (English number words), `ORDINAL`
      (`첫 번째`, `셋째`, `twenty-first`, `21st`) and `MONEY` (`value` and
      `currency` from Korean, symbol, word and code forms); `KO_NUMBER` also
      reads native numbers up to 99; `TELNUM` also accepts international and
      North American formats
- [x] Normalization: full Unicode NFC (canonical decomposition, ordering and
      composition) and simple Unicode case folding, from tables generated by
      `tools/gen_unicode_tables.py` (Unicode 13 via Python's `unicodedata`);
      `option ignore-trailing-punctuation` and `option fold-width` lines at
      the top of a script
- [x] Particle pairs inside exclusions: exclusion character classes split
      unmentioned characters by final consonant, and wildcard edges can be
      limited to one such class
- [x] Typo tolerance: `[...]&` takes one edit (a letter added, left out or
      changed) in the group, compiled as a second copy of the group entered
      by the edit; typos count as wildcard characters for specificity
- [x] Search mode: `Script::search` (C API, Python, `--search`) finds the
      longest rule match at each position of free text, with byte spans
      (`Match::begin`/`end`, also set by `match()`), keeping ASCII words whole
- [x] DFA cache: bounded by states and bytes (`CacheOptions`, 200,000 states
      and 64 MiB by default), evicting the states unused since the last
      eviction instead of clearing everything, with `cache_stats()`, states
      indexed by hash (each set stored once) and optional per-thread caches.
      Profiling with `nlu_script_bench` at 50,000+ rules showed the cache
      thrashing: 2,000 distinct inputs need about 46,000 states, more than
      the old 20,000, and each refill rebuilt the large shared-prefix states.
      A warm `match()` went from 46 µs (50,000 rules) and 100 µs (100,000)
      to about 4 µs at any size; with the old cap, eviction alone gives 20
      and 38 µs. Four threads reach the same throughput with a shared cache
      as with per-thread ones (about 1.2 µs per match)
- [x] Saving and loading compiled scripts: `Script::save`/`load` (C API,
      Python, `nlu_script_cli --save`, and saved files accepted as scripts),
      a versioned varint format whose loading checks every count and index,
      fuzzed in CI (`fuzz_load`). At 50,000 rules loading takes about half
      as long as compiling (most of the rest is building the automaton in
      memory), and the file is about 20 MiB
- [x] An exclusion reaches the wildcard a following slot starts with
      (`\X <slot><?>*?</slot>`, nested slots too)

## Next

- [ ] Publish wheels to PyPI (e.g. on a version tag)
- [ ] If GitHub retires the Intel macOS runner (`macos-15-intel`), build the
      x86_64 wheel on the arm64 runner instead: cross-compile (pass
      `ARCHFLAGS` to CMake as `CMAKE_OSX_ARCHITECTURES`) and test under
      Rosetta 2

## Open questions

- [x] A wildcard before an optional particle swallows a wrong particle
      (`<?>+?(을/를)` matches `청약통장를`). Decided: keep it. Keeping
      particle-like syllables out of a wildcard's last character would also
      reject words that end in them (`마을`, `가을`); a required particle
      (`<?>+?을/를`) already rejects the wrong one. The README explains this

- [x] Exclusion semantics: the guide's second exclusion example (text that
      only starts with the excluded name, rejected by `\[<?>*? X] <?>*? …`)
      contradicted its first example. Decided: the implemented behavior is
      the spec. A wildcard is rejected only when the exclusion matches its
      whole text. Use `\[<?>*? X <?>*?]` to exclude
      text that contains `X`.
- [x] Should `#{form}` and `${action.method}` (answer templates in the chatbot
      builder) be part of this library, or stay with the response layer?
      Decided: they stay with the response layer. This library turns a
      sentence into labels, slots and normalized attributes (values,
      currencies, resolved dates); what to answer depends on dialogue state
      and the caller's own data and actions, which it doesn't know. The
      matches it returns are the input a template engine needs, and filling
      `#{form}` from a slot is a few lines in the caller.
- [x] The original description mentions the OpenFst library. The automaton is
      implemented in-house (no dependency). Decided: keep it in-house. The
      features OpenFst would bring are covered without it: one automaton for
      all rules with a lazy DFA, exclusions by product construction, lint by
      searching the determinized automaton, and ranking by `specificity`
      (`match_best()`) instead of weighted rules. It would add a large C++
      dependency to every wheel. Revisit only if weighted or probabilistic
      scoring becomes a requirement.
