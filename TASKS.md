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
- [x] Cleanup pass over the whole project: one subset-construction step
      (`Closure::step`), accepted-rule list and placeholder-character picker
      shared by the lazy DFA, search, explain, exclusion compiling and lint;
      one walk over the definitions rules reach; shared `starts_with`,
      `ends_with`, `set_attribute`, grain and numeral tables; C API match
      entry points and CLI error printing written once. Speed-ups with
      identical output: rules without slots or wildcards skip the Pike VM,
      whose log is an index arena instead of `shared_ptr`s (warm `match()` on
      `nlu_script_bench` about 2x faster, slot-heavy `DATETIME` matching about
      1.9x); normalization skips NFC and case-folding lookups for Hangul
      syllables and CJK ideographs; search builds each match over its span
      only; explain no longer allocates per rule per character; Python
      converts byte offsets once per call
- [x] Fixes from the code review: system entity attributes are read from
      the normalized text the grammar matched (`３만` with `option fold-width`,
      NFD `내일`); option lines are read in the same pass as the rest of the
      script, so any space may follow `option`; edges say whether they
      consume wildcard text (`Edge::wildcard`, save format version 2), so an
      excluded wildcard no longer counts as pattern text in `specificity` or
      in lint's wildcard-free copies; `resolve_datetime()` ignores values out
      of range and clamps `now`, so caller input can't overflow
- [x] One source for system entity vocabulary: `src/vocabulary.hpp` has one
      table per vocabulary (numerals, English number and ordinal words,
      currencies, relative days, weekdays, months, parts of the day, clock
      words, duration units, day counts), read by both the grammar and the
      value readers; native hours and counts come from the counting forms,
      and the readers' space-less and plural forms from the tables. Same
      compiled bytes and attributes as before on 7,661 inputs
- [x] Structured diagnostics in the C API: `nlu_script_compile_diagnose` /
      `nlu_script_compile_file_diagnose` return every compile error with its
      line, column, message and file (`nlu_diagnostics_*`). Python reads them
      instead of parsing the joined message with a regex, which split a file
      name containing `: line 3, column 4: ` in the wrong place
- [x] One script test runner: `Script::run_tests` / `run_tests_file` and
      `output_lines` moved from the CLI's `nlu_script_tools` library into the
      library (C API `nlu_script_run_tests`, `nlu_test_report_*`). Python's
      `run_tests` calls it instead of its own copy, which had drifted (any
      Unicode space counted as indentation), and its failures now carry the
      rule behind each actual line (`TestFailure.rules`)
- [x] `search()` runs on the shared lazy DFA (`LazyDfa::run` from each
      position) instead of determinizing from scratch on every call, so
      `cache_stats()` now counts search traffic too. A warm search of a
      sentence on `nlu_script_bench` went from 3.9 ms to 1.8 µs at 20,000
      rules and from 50 ms to 4.6 µs at 100,000; output is unchanged (CLI
      `--search`, plain, `--best` and `--explain` compared on 5,000 inputs)
- [x] Lazy DFA computes new transitions without the lock, a doubling batch
      at a time, takes the exclusive lock only to add them and goes back to
      the shared lock; a thread holding a state that eviction renumbered
      looks its set up again. The lock spins, then yields: libc++'s
      `std::shared_mutex` hands over through a condition variable, so readers
      still slept through each added state. A per-thread cache takes no
      lock, and `Closure` reuses its scratch buffers. On 4 threads at 10,000
      rules: a cache capped at 5,000 states went from 13.1 to 2.8 µs per
      match, a cold cache from 0.89 to 0.72 µs, and while one thread matched
      new inputs, three on cached ones went from 50 to 2.9 µs per match

- [x] The DFA cache lock sleeps once spinning and yielding haven't helped:
      waiters count themselves and wait on a condition variable that
      releases signal (C++17, so no `std::atomic::wait`). With 7 threads
      matching cached inputs while one evicts 400,000-state caches, process
      CPU went from 11.5 to 8.9 s (7.8 to 6.0 cores) at the same throughput;
      warm matching on 1 and 4 threads is unchanged (the sleeping state sits
      behind a pointer, since inside the lock it slowed cached matches by
      about 4%)
- [x] `loading_rejects_anything_but_a_saved_script` corrupted a 575 KB saved
      script (the `DATETIME` grammar) every 97 bytes: 13 s natively, about 5
      minutes under ThreadSanitizer, and the sections after the automaton
      were barely touched. It now corrupts every byte, and tries every
      truncation, of a 3 KB script with every section of the format (2.2 s
      under TSan; the whole suite 0.7 s natively, 13 s under TSan). That
      found a crash: a corrupt script whose slot actions don't nest made
      matching pop an empty slot stack, which it now skips
- [x] Data domain: customer support for credit card, debit card and payment
      apps instead of banking. The demo, its tests, the C++ and Python tests,
      the fuzz seed and the README use card and payment vocabulary (no
      savings, deposits or exchange rates); the demo has 29 labels
- [x] Persona data: 100 personas sampled from NVIDIA's
      Nemotron-Personas-Korea (CC BY 4.0) by `tools/fetch_personas.py`, and
      three inquiries per persona in their voice, in Korean and then in
      English (`examples/personas/ko.test`, `en.test`; the first of each
      persona written by GPT-6-Luna, the other two by Claude). The demo
      matches each of the 600 with exactly one label, and `ctest` runs them.
      Rules written from Claude's 200 got 89 of GPT-6-Luna's 100 Korean
      sentences and 73 of the English ones before they saw them. The fuzz
      corpus is seeded with the persona sentences too
- [x] English `DATETIME` reads `this month`, `next month` and `last month`
      (`month_offset`, like `이번 달`, `다음 달`, `지난달`) and a day of the
      month alone, `the 25th` (`day`, like `25일`). The demo's English
      `bill:due_date` rules now capture the date as the Korean one does, so
      ten English persona inquiries tag a `DATETIME` they didn't before
- [x] `MONEY` reads amounts in digits with a scale (`30k won`,
      `2.8 million won`, `$1.5k`, `USD 30k`: `k`, `thousand`, `million`,
      `billion`), with the whole value (`30000`, `2800000`)
- [x] A system entity followed by a wildcard takes its longest form, as it
      does without one: `` `@{MONEY} <?>*? `` tags `50 dollars` (it tagged
      `50 dollar` and left the `s` to the wildcard), `` `@{DATETIME} <?>*? ``
      `in 2 hours`, `` `@{KO_NUMBER} <?>*? `` `일곱` (it tagged `일`) and
      `3만 5천` (it tagged `3만 5`). The grammar tries a table's words longest
      first, native Korean numbers before numerals (일곱, 일흔 start with 일),
      and 5천 before the digit 5. Checked on 238 entity texts from the tests,
      the demo and the persona data. Groups in patterns still take their first
      alternative that lets the rule match
- [x] A slot's text never starts or ends with a space: a skipped optional
      group at its edge left the space beside it in the slot
      (`<s>a (b)</s> c` tagged `<s>a </s>c` in `a c`, `x::<s>(b) a</s>`
      tagged `x<s> a</s>` in `x a`). Slots are trimmed when the match is
      built, and a slot the skipped group left empty stays inside the slot
      around it. The demo's card variable says `@var.{brand_en} (card)`
      instead of spelling out both forms

## Next

- [ ] Lint is too slow for the demo: with its 80 rules, mostly `<?>*? word
      <?>*?` and many with system entities, each automaton state takes about
      50 ms (1.4 ms with the system entities stubbed out), and 1,000 states
      find no overlap yet. `ctest` lints a small script instead
- [ ] Publish wheels to PyPI (e.g. on a version tag)
- [ ] If GitHub retires the Intel macOS runner (`macos-15-intel`), build the
      x86_64 wheel on the arm64 runner instead: cross-compile (pass
      `ARCHFLAGS` to CMake as `CMAKE_OSX_ARCHITECTURES`) and test under
      Rosetta 2

## Open questions

- [x] A wildcard before an optional particle swallows a wrong particle
      (`<?>+?(을/를)` matches `멤버십를`). Decided: keep it. Keeping
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
