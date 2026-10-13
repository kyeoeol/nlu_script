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

Try the demo script, which handles customer-support inquiries for a banking
or payment app:

```sh
printf '%s\n' "I lost my debit card" "마스터 체크카드를 분실했어요" \
  | ./build/nlu_script_cli examples/demo.nlu
# card:lost:I lost my <card type="DEBIT">debit card</card>
# card:lost:<card brand="MASTERCARD" type="DEBIT">마스터 체크카드</card>를 분실했어요
```

Input with no matching label prints `-`.

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
Slot text and offsets always refer to the original input.

Matching is language-neutral: Korean, English and mixed input (`OTP 재발급`)
follow the same rules. Punctuation in the input is not stripped, so
`is my card blocked?` only matches a pattern that ends in `"?"`.

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

### Groups

| Syntax | Meaning | Example |
|---|---|---|
| `[a\|b]` | required, one of the alternatives | `[송금\|결제] 취소해줘`, `[cancel\|refund] my payment` |
| `(a\|b)` | optional | `환불(해줘\|해주세요)`, `check my balance (please\|now)` |
| `~[a, b]~` | all tokens, in any order (at most 5) | `~[카드, 재발급]~ 신청`, `~[reset, password]~` |
| `[k]^(a,b)` | `k` repeated `a` to `b` times | `[ㅠ]^(1,3)`, `[help]^(1,3)` |
| `[k]^<n` | `k` repeated 1 to `n-1` times | `[정말]^<3 급해요`, `[very]^<3 urgent` |
| `[k]^>n` | `k` repeated more than `n` times | `[제발]^>2`, `[please]^>2` |

Groups nest. Repetition counts are capped at 100.

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

System entities: `NUMBER` (digits) and `TELNUM` (Korean phone numbers such as
`010-9876-5432`, `031-765-4321`, `01098765432`). They work the same in
Korean and English sentences (``change my phone number to `@{TELNUM}``);
`TELNUM` does not yet recognize other countries' number formats.

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
  std::cout << m.label << ":" << m.tagged << "\n";
  for (const nlu::Slot& s : m.slots) { /* s.name, s.text, s.begin, s.end, s.attributes */ }
}
```

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
  printf("%s:%s\n", nlu_result_label(result, i), nlu_result_tagged(result, i));
nlu_result_free(result);
nlu_script_free(script);
```

## Python

```sh
pip install ./python        # builds the shared library with CMake
```

CI also builds wheels for Linux (manylinux x86_64), macOS (arm64) and
Windows (x64) and tests each one on every supported CPython. A wheel is
tagged `py3-none-<platform>` because the library is loaded with ctypes, so
one wheel serves every Python 3 version. Download them from the `wheels-*`
artifacts of a CI run.

```python
import nlu_script

script = nlu_script.Script.from_file("rules.nlu")   # or nlu_script.Script(source)
for m in script.match("엄마한테 3만 원 보내줘"):
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
