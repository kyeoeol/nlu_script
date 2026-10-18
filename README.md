# nlu_script

Regex script for Natural Language Understanding.

- **Readable, reusable regex.** Large, complex rule sets stay manageable with
  labels, entities, variables and a compact pattern language designed for
  spoken-style Korean and English.
- **Slots with normalized attributes.**
  `report a lost <card brand="VISA" type="CREDIT">Visa credit card</card>`
- **Near-constant classification speed.** All rules are compiled into one
  automaton. The cost per character doesn't depend on how many rules there are.
  Slots are extracted only for the rules that matched.
- **C++17 core** with an **ANSI C interface** and a **Python 3.5+ package**.

```
agent:connect:talk to an agent
agent:connect:상담원 연결해주세요
transfer:send:Send <amount currency="USD">50</amount> dollars to <recipient>Alex</recipient>
transfer:send:<recipient>엄마</recipient>한테 <amount currency="KRW">3만</amount> 원 보내줘
```

## Build

```sh
cmake -S . -B build
cmake --build build
cd build && ctest --output-on-failure
```

With a multi-config generator such as Visual Studio, pick the configuration
when building and testing: `cmake --build build --config Release` and
`ctest -C Release --output-on-failure`.

Targets: `nlu_script` (shared library, C API), `nlu_script_static` (static
library, C++ API), `nlu_script_cli`, and the tests.

The parser and matcher are fuzzed in CI (`fuzz/`, libFuzzer with
AddressSanitizer and UndefinedBehaviorSanitizer). To fuzz locally with clang:

```sh
cmake -S . -B build-fuzz -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DNLU_SCRIPT_BUILD_FUZZERS=ON -DNLU_SCRIPT_BUILD_TESTS=OFF \
  "-DCMAKE_CXX_FLAGS=-fsanitize=fuzzer-no-link,address,undefined" \
  "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined"
cmake --build build-fuzz --target fuzz_script fuzz_match
./build-fuzz/fuzz_match -max_total_time=60 corpus/
```

Apple clang has no libFuzzer; `fuzz/standalone.cpp` is a small mutation
driver to link with a fuzz target instead (see its header comment).

Try the demo script, which handles customer-support inquiries for a banking
or payment app:

```sh
printf '%s\n' "I lost my debit card" "마스터 체크카드를 분실했어요" \
  | ./build/nlu_script_cli examples/demo.nlu
# card:lost:I lost my <card type="DEBIT">debit card</card>
# card:lost:<card brand="MASTERCARD" type="DEBIT">마스터 체크카드</card>를 분실했어요
```

Input with no matching label prints `-`. With `--best`, only the most
specific labels are printed (see [Matching](#matching)); `--test --best` runs
test files the same way.

## Testing scripts

Keep example sentences next to a script, each followed by the indented output
expected for it (exactly what `nlu_script_cli` prints), and run them with
`--test`:

```
# Lines starting with # are comments.
I lost my debit card
    card:lost:I lost my <card type="DEBIT">debit card</card>
thank you
    -
```

```sh
./build/nlu_script_cli --test examples/demo.nlu examples/demo.test
# 32 passed, 0 failed
```

When an input matches several labels, list one line per label in script
order. `-` expects no match. Input lines must not be indented, and trailing
whitespace on them is ignored. Each failure shows the test's location, the
expected lines and the actual ones, with the script line of the rule behind
each actual line:

```
rules.test:2: I lost my debit card
  expected: agent:connect:I lost my debit card
  actual:   card:lost:I lost my <card type="DEBIT">debit card</card>  (rules.nlu:22)
```

The exit status is 0 when every case passes and 1 otherwise, so the command
can run in CI.

## Linting scripts

`--lint` reports labels that match the same input, and entities and
variables that no rule uses:

```sh
./build/nlu_script_cli --lint rules.nlu
# rules.nlu:32: transfer:send and payment:refund (line 41) both match "send x won to 결제 환불해줘"
# 1 finding
```

Each overlap names the first rule of each label that matches, and gives the
shortest input that both match (normalized, so in lowercase), with `x`
standing in for any text a wildcard accepts. Read each one and decide whether
a label needs an exclusion, or whether `match_best()` should pick between
them. The search explores the automaton of all rules, up to 100,000 states.
If it stops there, the output says that more overlaps may exist.

Some overlaps are intended, and wildcards produce many. Mark them with a
comment anywhere in the script:

```
# lint: allow-overlap transfer:send payment:refund account:close
# lint: allow-overlap report:any *
```

The first line allows overlaps between any two of the listed labels; `*`
allows every overlap of the other labels on its line. Allowed overlaps are
counted but not listed. An unknown label or command in a `# lint:` comment is
a finding itself; such comments never affect matching.

The exit status is 1 when there are findings, so `--lint` can run in CI; the
demo script is linted that way in `ctest`.

## Script files

A script is a list of labels, each followed by its indented patterns, plus
entity and variable definitions. Lines starting with `#` are comments.

```
# Domain entity: auto-captured as a slot named after the entity.
# One value can list its Korean and English words.
@currency: [달러|dollar]{code=USD}, [엔화|yen]{code=JPY}

# Values may also go on indented lines.
@card:
    [신용카드|credit card]{type=CREDIT}
    [체크카드|debit card]{type=DEBIT}

# Regex variable: inlined where referenced; no slot.
@var.digit: [0|1|2|3|4|5|6|7|8|9]

exchange:rate
    (오늘) @{currency} 환율 (알려줘|얼마예요)
    (what is the) @{currency} exchange rate

card:reissue
    끝자리 @var.{digit}@var.{digit}@var.{digit}@var.{digit} @{card} 재발급 (해주세요)
    reissue my @{card} ending in @var.{digit}@var.{digit}@var.{digit}@var.{digit}
```

Labels can't contain spaces (any text is allowed otherwise, e.g.
`account:password:reset`). A label may appear more than once; its patterns are
merged. Definitions can be used before they are defined.

## Matching

A pattern must match the **whole** input. Before matching, the input is
normalized: whitespace runs collapse to one space, leading and trailing
whitespace is dropped, and ASCII letters are matched case-insensitively.
Decomposed Hangul (NFD, as in text copied from macOS file names) is composed
into syllables first, in the input and in scripts alike, so it matches the
precomposed form. Slot text and offsets always refer to the original input.

Matching is language-neutral: Korean, English and mixed input (`OTP 재발급`)
follow the same rules. Punctuation in the input is not stripped, so
`is my card blocked?` only matches a pattern that ends in `"?"`.

`match()` returns one result per matching label, in the order the label's
first matching rule appears in the script. Each result carries the script
line of that rule, so you can tell which pattern produced it.

Each result also has a **specificity**: the number of input characters, not
counting spaces, that the rule matched with pattern text (literals, entities,
system entities) rather than wildcards. When several labels match,
`match_best()` keeps only the labels whose most specific rule scores
highest, and each of those results comes from that rule. Ties keep every
such label, in script order.

```
card:lost      I lost my [credit|debit] card    -> specificity 16
report:any     I <?>+?                          -> specificity 1
input "I lost my debit card": match() -> card:lost, report:any; match_best() -> card:lost
```

This often replaces an exclusion. `@{product}(을/를) 해지` beats
`<?>+?(을/를) 해지` for `적금을 해지` without the general rule having to
exclude product names.

## Pattern language

Special symbols (quote them, e.g. `"?"` or `"-"`, to match them literally;
`/` also writes a particle pair like `을/를`):

```
( ) { } [ ] < > \ : + * ^ ~ = . | & - / `
```

### Spaces

A space in a pattern is optional in the input. No space means no space is
allowed. Tokens next to groups (`[ ]`, `( )`, …) may also be separated by a
space in the input unless they are joined with `::`.

```
잔액 조회            -> 잔액조회, 잔액 조회
잔액조회             -> 잔액조회 (not 잔액 조회)
log in               -> login, log in
login                -> login (not log in)
환불[해줘|해주세요]   -> 환불해줘, 환불 해줘, 환불해주세요, 환불 해주세요
환불::[해줘|해주세요] -> 환불해줘, 환불해주세요
sign[in|up]          -> signin, sign in, signup, sign up
[account|card]::(s)  -> account, accounts, card, cards (not card s)
```

`::` keeps a particle on its word: `[계좌|카드]::가 압류됐어요` matches
`계좌가 압류됐어요` (seized) but not `계좌 가압류됐어요` (provisionally
seized), and `[log|sign]::in page is down` matches `login page is down` but
not `log in page is down`.

`::` must touch both tokens: `환불 ::[해줘]` is an error.

### Particles

A Korean particle pair such as `을/를` matches the form that fits the
syllable right before it: the first form after a final consonant (받침), the
second after a vowel. Either order works (`을/를` or `를/을`). The particle
always stays on its word, with no space before it, and can be optional:
`(을/를)`.

```
[적금|카드]을/를 해지     -> 적금을 해지, 카드를 해지 (not 적금를, 카드을, 적금 을)
<?>+?이/가 만기됐어요     -> 청약이 만기됐어요, 정기예금이 만기됐어요
@{product}(을/를) 해지    -> 적금 해지, 적금을 해지 (not 적금를 해지)
[서울|부산]으로/로 송금    -> 서울로 송금, 부산으로 송금 (a final ㄹ takes 로)
```

Pairs: 은/는, 이/가, 을/를, 과/와, 아/야, 이나/나, 이랑/랑, 이라고/라고,
이에요/예요, 으로/로, 으로서/로서, 으로써/로써. After anything other than a
Hangul syllable, such as `OTP` or a phone number, either form matches,
because the right one depends on how it is read. Particle pairs can't be
used inside an exclusion.

### Groups

| Syntax | Meaning | Example |
|---|---|---|
| `[a\|b]` | required, one of the alternatives | `[송금\|결제] 취소해줘`, `[cancel\|refund] my payment` |
| `(a\|b)` | optional | `환불(해줘\|해주세요)`, `check my balance (please\|now)` |
| `~[a, b]~` | all tokens, in any order (at most 5) | `~[카드, 재발급]~ 신청`, `~[reset, password]~` |
| `[k]^(a,b)` | `k` repeated `a` to `b` times | `[ㅠ]^(1,3)`, `[help]^(1,3)` |
| `[k]^<n` | `k` repeated 1 to `n-1` times | `[정말]^<3 급해요`, `[very]^<3 urgent` |
| `[k]^>n` | `k` repeated more than `n` times | `[제발]^>2`, `[please]^>2` |

Groups nest. Repetition counts are capped at 100. Since nested repetitions
multiply, a rule may compile to at most 500,000 automaton states and a whole
script to 10 million (about 1.3 GB); past that, compiling reports an error
instead of exhausting memory.

### Wildcards and exclusions

| Syntax | Matches |
|---|---|
| `<?>` | any single character |
| `<?>*?` | any text, possibly empty (lazy) |
| `<?>+?` | any non-empty text (lazy) |
| `\X <?>*?` | the wildcard, unless its text is exactly matched by `X` |

```
\[적금|예금] <?>+? 해지
  -> 청약통장 해지, 청약통장이랑 적금 해지, 적금이랑 청약통장 해지 (match)
  -> 적금 해지 (no match)
\[<?>*? 적금|<?>*? 예금] <?>+? 해지
  -> 청약통장이랑 적금 해지 (no match)
  -> 적금이랑 청약통장 해지 (match: the text does not end with 적금)
\[<?>*? 적금 <?>*?|<?>*? 예금 <?>*?] <?>+? 해지
  -> 청약통장이랑 적금 해지, 적금이랑 청약통장 해지, 정기예금 상품 해지 (no match)

close my \[savings|checking] <?>+? account
  -> close my brokerage account, close my joint savings account (match)
  -> close my savings account (no match)
close my \[<?>*? savings <?>*?|<?>*? checking <?>*?] <?>+? account
  -> close my joint savings account, close my savings and brokerage account (no match)
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

System entities: `NUMBER` (digits), `TELNUM` (Korean phone numbers such as
`010-9876-5432`, `031-765-4321`, `01098765432`), `KO_NUMBER` (numbers in
digits and Korean numerals) and `DATETIME` (dates and times), described
below. They work the same in Korean and
English sentences (``change my phone number to `@{TELNUM}``); `TELNUM` does
not yet recognize other countries' number formats.

`KO_NUMBER` matches `35000`, `35,000`, `3만 5천`, `3만5천`, `삼만오천`,
`2,500만`, `1억 2천만` and the like (units 십, 백, 천, 만, 억, 조, with
sino-Korean digits 일 to 구 or Arabic digits). Its slot gets the number as a
`value` attribute:

```
<amount>`@{KO_NUMBER}</amount> 원 보내줘
  "삼만 오천 원 보내줘" -> <amount><KO_NUMBER value="35000">삼만 오천</KO_NUMBER></amount> 원 보내줘
```

Values of 10^18 or more match without a `value`. Native Korean numbers
(하나, 둘, 세) are not recognized.

`DATETIME` matches dates and times in Korean and English and describes them
with attributes relative to now, which the caller resolves against its own
clock and time zone:

```
`@{DATETIME}::(에) 전화 주세요
  "내일 오후 3시에 전화 주세요"
  -> <DATETIME day_offset="+1" part_of_day="PM" hour="15">내일 오후 3시</DATETIME>에 전화 주세요
call me back (at) `@{DATETIME}
  "call me back next friday morning"
  -> call me back <DATETIME week_offset="+1" weekday="FRI" part_of_day="MORNING">next friday morning</DATETIME>
```

| Written | Attributes |
|---|---|
| 오늘, 내일, 모레, 글피, 어제, 그제, 그저께; today, tomorrow, yesterday, the day after tomorrow | `day_offset` (`0`, `+1`, `-1`, ...) |
| 이번/다음/지난 주, 담주; this/next/last week | `week_offset` |
| 월요일 ... 일요일; monday ... sunday, next monday | `weekday` (`MON` ... `SUN`), `week_offset` |
| 2026년, 3월, 5일, 삼월 십오일; march 5th, 5th of march, dec 31 | `year`, `month`, `day` |
| 올해, 내년, 작년; 이번/다음/지난 달 | `year_offset`, `month_offset` |
| 새벽, 아침, 오전, 낮, 점심, 오후, 저녁, 밤; morning, afternoon, evening, night, tonight, am, pm | `part_of_day` (`DAWN`, `MORNING`, `AM`, `DAY`, `NOON`, `PM`, `EVENING`, `NIGHT`) |
| 3시, 세 시, 열두시, 15시, 30분, 반, 정오, 자정; 3pm, 3:30 pm, 15:30, 3 o'clock, noon, midnight | `hour`, `minute` |

A date comes before a time (`다음 주 월요일 저녁 7시 반`, `tomorrow at 3pm`).
`hour` is on a 24-hour clock when the part of the day settles it (오후 3시
gives 15, 밤 11시 23, 밤 1시 1, 12am 0); otherwise it is the hour as
written (세 시 gives 3). Digits are checked against the calendar and clock
(13월, 32일, 25시 and 3시 60분 don't match); numerals out of range (십삼월)
match without that attribute. Prepositions and particles around the entity
(`at`, `on`, `에`) belong to the pattern.

### Slots and attributes

```
<name> pattern </name>          capture the matched text as slot `name`
{key=value, key2="a value"}     set attributes on the innermost enclosing slot
{slot.key=value}                set an attribute on slot `slot` anywhere in the rule
```

```
card:lost
    report a lost <card>([visa{brand=VISA}|mastercard{brand=MASTERCARD}]) [credit{type=CREDIT}|debit{type=DEBIT}] card</card>
    <card>([비자{brand=VISA}|마스터{brand=MASTERCARD}]) [신용{type=CREDIT}|체크{type=DEBIT}]카드</card>::(를) 분실했어요
transfer:send
    send <amount><?>+?</amount> [dollars{amount.currency=USD}|won{amount.currency=KRW}] to <recipient><?>+?</recipient>
    <recipient><?>+?</recipient>::[한테|에게] <amount><?>+?</amount> [달러{amount.currency=USD}|원{amount.currency=KRW}] 보내줘
```

An attribute assignment applies when the token right before it matches.
`{slot.key=value}` applies to the closest earlier `<slot>`, or if there is none,
to the next one. Unqualified assignments outside any slot are an error. Later
assignments of the same key win. Attributes are listed in the order they were
first assigned.

## C++ API

```cpp
#include "nlu_script/nlu_script.hpp"

nlu::Script script = nlu::Script::compile_file("rules.nlu");  // throws nlu::CompileError
for (const nlu::Match& m : script.match("I lost my debit card")) {
  std::cout << m.label << ":" << m.tagged << "\n";  // also m.line, m.specificity
  for (const nlu::Slot& s : m.slots) { /* s.name, s.text, s.begin, s.end, s.attributes */ }
}
```

`script.match_best(text)` keeps only the most specific labels.
`Script::lint()` returns the same checks as `--lint` as an `nlu::LintReport`
(overlaps, unused definitions, and whether the search was complete).

Compiling reports every error in the script at once: `CompileError::errors()`
lists them by position (`line`, `column`, `message`), and `what()` has one per
line. A broken label or definition line hides the lines indented under it, so
one mistake is reported once. The C API returns the same lines in its error
message, Python's `CompileError.errors` as `(line, column, message)` tuples,
and the CLI prints each as `rules.nlu:3:5: message`.

`Script::match` is thread-safe, and concurrent calls run in parallel. A call
waits only while another thread adds a state to the shared DFA cache, which
mostly happens while the cache warms up.

## C API

See [`include/nlu_script/nlu_script.h`](include/nlu_script/nlu_script.h).

```c
char* error = NULL; int line = 0, column = 0;
nlu_script* script = nlu_script_compile(source, &error, &line, &column);
nlu_result* result = nlu_script_match(script, "상담원 연결해주세요");
for (size_t i = 0; i < nlu_result_count(result); ++i)
  printf("%s:%s (line %d, specificity %d)\n", nlu_result_label(result, i),
         nlu_result_tagged(result, i), nlu_result_line(result, i),
         nlu_result_specificity(result, i));
nlu_result_free(result);
nlu_script_free(script);
```

## Python

```sh
pip install ./python        # builds the shared library with CMake
```

The `Wheels` workflow builds wheels for Linux (manylinux and musllinux, x86_64
and aarch64), macOS (arm64 and x86_64) and Windows (x64), and tests each one
on every supported CPython. It runs on demand (Actions > Wheels > Run
workflow) and on pull requests that change the packaging. A wheel is tagged
`py3-none-<platform>` because the library is loaded with ctypes, so one wheel
serves every Python 3 version. Download them from the run's `wheels-*`
artifacts, which are kept for 7 days.

```python
import nlu_script

script = nlu_script.Script.from_file("rules.nlu")   # or nlu_script.Script(source)
for m in script.match("엄마한테 3만 원 보내줘"):
    print(m.label, m.tagged, m.line, m.specificity)
best = script.match_best("엄마한테 3만 원 보내줘")    # only the most specific labels
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
   per state and character) decides which rules accept the input. Threads
   share the cache: following cached transitions takes a shared lock, and
   only adding a new transition takes the exclusive lock.
3. For each accepting rule, a Pike VM replays the input over that rule's
   states. Groups are greedy and wildcards are lazy. The VM records slot
   boundaries and attribute assignments on the winning path.

## Background

nlu_script started from the pattern language described in the NAVER Cloud
Platform chatbot guide,
[nlu_script syntax](https://guide-gov.ncloud-docs.com/docs/chatbot-chatbot-3-7),
and from the talk
[in this video](https://youtu.be/cPpUrH2Y3aw?list=PLsFtzQAC8dDfk-KmvmagozbYtQ2Fmpmpi&t=745).
This is an independent implementation. The examples and tests use this
project's own Korean and English data, modeled on customer-support inquiries
to banking and payment apps.

## License

[MIT](LICENSE)
