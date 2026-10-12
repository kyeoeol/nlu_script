# nlu_script

Regex script for Natural Language Understanding.

- **Readable, reusable regex.** Large, complex rule sets stay manageable with
  labels, entities, variables and a compact pattern language designed for
  spoken-style Korean and English.
- **Slots with normalized attributes.**
  `wake me up at <cons_datetime hour="9" part_of_day="MORNING">nine o'clock in the morning</cons_datetime>`
- **Near-constant classification speed.** All rules are compiled into one
  automaton. The cost per character doesn't depend on how many rules there are.
  Slots are extracted only for the rules that matched.
- **C++17 core** with an **ANSI C interface** and a **Python 3.5+ package**.

```
control:up:volume:Turn up volume
translation:request:translation:Translate <cons_translation target="ja">good morning</cons_translation> in Japanese
```

## Build

```sh
cmake -S . -B build
cmake --build build
cd build && ctest --output-on-failure
```

Targets: `nlu_script` (shared library, C API), `nlu_script_static` (static
library, C++ API), `nlu_script_cli`, and the tests.

Try the demo script:

```sh
echo "wake me up at nine o'clock in the morning" | ./build/nlu_script_cli examples/demo.nlu
# alarm:set:datetime:wake me up at <cons_datetime hour="9" part_of_day="MORNING">nine o'clock in the morning</cons_datetime>
```

## Script files

A script is a list of labels, each followed by its indented patterns, plus
entity and variable definitions. Lines starting with `#` are comments.

```
# Domain entity: auto-captured as a slot named after the entity.
@메뉴: 치킨{value=chicken}, 피자{value=pizza}

# Values may also go on indented lines.
@drink:
    [coke|cola]{value=coke}
    sprite{value=sprite}

# Regex variable: inlined where referenced; no slot.
@var.숫자: [1|2|3|4|5|6|7|8|9]

order:food
    @{메뉴}::(을|를) [주문|배달] (해줘|해주세요)
    @{메뉴} 시켜줘

age
    @var.{숫자} @var.{숫자} [세|살] (입니다)
```

Labels can't contain spaces (any text is allowed otherwise, e.g.
`control:up:volume`). A label may appear more than once; its patterns are
merged. Definitions can be used before they are defined.

## Matching

A pattern must match the **whole** input. Before matching, the input is
normalized: whitespace runs collapse to one space, leading and trailing
whitespace is dropped, and ASCII letters are matched case-insensitively.
Slot text and offsets always refer to the original input.

`match()` returns one result per matching label, in the order the label's
first matching rule appears in the script.

## Pattern language

Special symbols (quote them, e.g. `"?"` or `"-"`, to match them literally):

```
( ) { } [ ] < > \ : + * ^ ~ = . | & - / `
```

### Spaces

A space in a pattern is optional in the input. No space means no space is
allowed. Tokens next to groups (`[ ]`, `( )`, …) may also be separated by a
space in the input unless they are joined with `::`.

```
안녕 하세요        -> 안녕하세요, 안녕 하세요
안녕하세요         -> 안녕하세요 (not 안녕 하세요)
주[라|세요]        -> 주라, 주 라, 주세요, 주 세요
주::[라|세요]      -> 주라, 주세요
```

`::` must touch both tokens: `주 ::[라]` is an error.

### Groups

| Syntax | Meaning | Example |
|---|---|---|
| `[a\|b]` | required, one of the alternatives | `[주문\|배달] [해줘\|해주세요]` |
| `(a\|b)` | optional | `안녕(하세요\|하신가\|하냐)` |
| `~[a, b]~` | all tokens, in any order (at most 5) | `~[계정, 생성]~방법` |
| `[k]^(a,b)` | `k` repeated `a` to `b` times | `[빨리]^(1,3)` |
| `[k]^<n` | `k` repeated 1 to `n-1` times | `[빨리]^<3` |
| `[k]^>n` | `k` repeated more than `n` times | `[빨리]^>2` |

Groups nest. Repetition counts are capped at 100.

### Wildcards and exclusions

| Syntax | Matches |
|---|---|
| `<?>` | any single character |
| `<?>*?` | any text, possibly empty (lazy) |
| `<?>+?` | any non-empty text (lazy) |
| `\X <?>*?` | the wildcard, unless its text is exactly matched by `X` |

```
\[김철수|홍길동] <?>*? 노래 틀어줘
  -> 아이유 노래 틀어줘, 홍길동이나 아이유 노래 틀어줘 (match)
  -> 홍길동 노래 틀어줘 (no match)
\[<?>*? 김철수|<?>*? 홍길동] <?>*? 노래 틀어줘
  -> 아이유나 홍길동 노래 틀어줘 (no match)
  -> 홍길동이나 아이유 노래 틀어줘 (match: the text does not end with 홍길동)
\[<?>*? 김철수 <?>*?|<?>*? 홍길동 <?>*?] <?>*? 노래 틀어줘
  -> 아이유나 홍길동 노래 틀어줘, 홍길동이나 아이유 노래 틀어줘 (no match)
```

The exclusion is tested against the wildcard's **whole** text. To exclude
text that merely contains a token, surround the token with `<?>*?`.

An exclusion must come right before a wildcard. Several exclusions may
precede one wildcard (`\X \Y <?>*?`). Wildcard text never starts or ends with a
space.

### References

| Syntax | Meaning |
|---|---|
| `@{name}` | domain entity: one of its values, captured as slot `<name>` |
| `@var.{name}` | regex variable: its pattern, inlined |
| `` `@{NAME} `` | system entity, captured as slot `<NAME>` |

System entities: `NUMBER` (digits) and `TELNUM` (Korean phone numbers such as
`010-1234-5678`, `02-123-4567`, `01012345678`).

### Slots and attributes

```
<name> pattern </name>          capture the matched text as slot `name`
{key=value, key2="a value"}     set attributes on the innermost enclosing slot
{slot.key=value}                set an attribute on slot `slot` anywhere in the rule
```

```
alarm
    wake me up at <cons_datetime>[nine{hour=9}|ten{hour=10}] o'clock</cons_datetime>
translation:request:translation
    translate <cons_translation><?>+?</cons_translation> in [japanese{cons_translation.target=ja}|korean{cons_translation.target=ko}]
```

An attribute assignment applies when the token right before it matches.
`{slot.key=value}` applies to the closest earlier `<slot>`, or if there is none,
to the next one. Unqualified assignments outside any slot are an error. Later
assignments of the same key win.

## C++ API

```cpp
#include "nlu_script/nlu_script.hpp"

nlu::Script script = nlu::Script::compile_file("rules.nlu");  // throws nlu::CompileError
for (const nlu::Match& m : script.match("Wake me up at nine o'clock")) {
  std::cout << m.label << ":" << m.tagged << "\n";
  for (const nlu::Slot& s : m.slots) { /* s.name, s.text, s.begin, s.end, s.attributes */ }
}
```

`Script::match` is thread-safe.

## C API

See [`include/nlu_script/nlu_script.h`](include/nlu_script/nlu_script.h).

```c
char* error = NULL; int line = 0, column = 0;
nlu_script* script = nlu_script_compile(source, &error, &line, &column);
nlu_result* result = nlu_script_match(script, "turn up volume");
for (size_t i = 0; i < nlu_result_count(result); ++i)
  printf("%s:%s\n", nlu_result_label(result, i), nlu_result_tagged(result, i));
nlu_result_free(result);
nlu_script_free(script);
```

## Python

```sh
pip install ./python        # builds the shared library with CMake
```

```python
import nlu_script

script = nlu_script.Script.from_file("rules.nlu")   # or nlu_script.Script(source)
for m in script.match(u"Translate good morning in Japanese"):
    print(m.label, m.tagged)
    for slot in m.slots:
        print(slot.name, slot.text, slot.begin, slot.end, slot.attributes)
```

Slot offsets in Python are character offsets. To use a library you built
yourself, set `NLU_SCRIPT_LIBRARY=/path/to/libnlu_script.so`.

## How it works

1. Each pattern is parsed into a syntax tree and compiled into a Thompson-style
   NFA. A soft join becomes an optional space edge. Exclusions are compiled as
   the product of the wildcard's DFA with the complement of the excluded
   pattern's DFA.
2. All rules share one NFA. A lazily built DFA (subset construction, cached
   per state and character) decides which rules accept the input.
3. For each accepting rule, a Pike VM replays the input over that rule's
   states. Groups are greedy and wildcards are lazy. The VM records slot
   boundaries and attribute assignments on the winning path.
