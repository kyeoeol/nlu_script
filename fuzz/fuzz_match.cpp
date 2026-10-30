// Fuzz target: matches the input against a script that uses every feature
// of the pattern language and every system entity.
#include <cstddef>
#include <cstdint>
#include <string>

#include "nlu_script/nlu_script.hpp"

namespace {

const char* const kScript = R"(option ignore-trailing-punctuation
option fold-width
@service: [구독|subscription]{value=subscription}, [멤버십|membership]{value=membership}
@var.digit: [0|1|2|3|4|5|6|7|8|9]
# lint: allow-overlap any:text *

card:lost
    <card>([비자{brand=VISA}|마스터{brand=MASTERCARD}]) [신용{type=CREDIT}|체크{type=DEBIT}]카드</card>(을/를) 분실했어요
    I lost my <card>[credit{type=CREDIT}|debit{type=DEBIT}] card</card>
card:cancel
    <card>\[<?>*? @{service} <?>*?] <?>+?</card>(을/를) 해지 (해주세요)
    ~[reset, password]~ [please]^(1,3)
transfer:send
    <recipient><?>+?</recipient>::[한테|에게] <amount>`@{KO_NUMBER}</amount> 원 보내줘
    send `@{NUMBER} dollars to <recipient><?>+?</recipient>
agent:callback
    `@{DATETIME}::(에) 전화 주세요
    call me back (at) `@{DATETIME}
account:phone
    휴대폰 번호 `@{TELNUM}(으로/로) 변경해주세요
    call `@{TELNUM}
payment:amount
    [I lost my debit card]&
    [분실 신고]& (할게요)
    `@{MONEY} 결제
    pay `@{MONEY} (for the `@{ORDINAL} item)
    `@{EN_NUMBER} items
    끝자리 @var.{digit}@var.{digit} <?> [ㅠ]^>1
any:text
    <?>*? "?"
)";

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  static const nlu::Script script = nlu::Script::compile(kScript);
  if (size > 1024) return 0;
  const std::string text(reinterpret_cast<const char*>(data), size);
  script.match(text);
  script.match_best(text);
  return 0;
}
