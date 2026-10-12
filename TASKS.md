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

## Next

- [ ] Datetime extractor: a bundled script of datetime rules plus a
      normalizer (relative days like `day="+1"`, `minute`, `part_of_day`), with
      a short how-to
- [ ] More system entities (dates, times, currency, ordinals, Korean numerals)
- [ ] Unicode case folding beyond ASCII; optional full-width/half-width folding
- [ ] Save and load compiled scripts so large rule sets don't need recompiling
- [ ] Bound the lazy DFA cache by memory as well as by state count, and expose
      stats for it
- [ ] Profile full-match time at 50k+ rules (about 33 µs vs about 5 µs at 10
      rules; classification alone stays flat at about 1.3 µs)
- [ ] Search mode: find slots inside free text without `<?>*?` padding
- [ ] Let an exclusion reach a wildcard inside a following slot
      (`\X <slot><?>*?</slot>`); today the exclusion has to go inside the slot
- [ ] Python wheels / CI (GitHub Actions) for Linux, macOS, Windows
- [ ] Windows build check (C API export macros are in place but untested)

## Open questions

- [x] Exclusion semantics: the guide's second example (`홍길동이나 아이유 …` rejected
      by `\[<?>*? 홍길동] <?>*? …`) contradicted its first example. Decided:
      the implemented behavior is the spec. A wildcard is rejected only when
      the exclusion matches its whole text. Use `\[<?>*? X <?>*?]` to exclude
      text that contains `X`.
- [ ] Should `#{form}` and `${action.method}` (answer templates in the chatbot
      builder) be part of this library, or stay with the response layer?
- [ ] The original description mentions the OpenFst library. The automaton is
      implemented in-house (no dependency). Revisit if OpenFst interop or
      weighted rules are needed.
