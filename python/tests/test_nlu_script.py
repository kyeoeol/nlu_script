import os
import tempfile
import unicodedata
import unittest

import nlu_script

SCRIPT = """
@통화: [달러|dollar]{code=USD}, [엔화|yen]{code=JPY}

exchange:rate
    @{통화} 환율 (알려줘)
    @{통화} exchange rate

card:lost
    report a lost <card>[credit{type=CREDIT}|debit{type=DEBIT}] card</card>
    <card>[신용{type=CREDIT}|체크{type=DEBIT}]카드</card> 분실 신고
"""


class ScriptTest(unittest.TestCase):
    def setUp(self):
        self.script = nlu_script.Script(SCRIPT)

    def tearDown(self):
        self.script.close()

    def test_rule_count(self):
        self.assertEqual(self.script.rule_count, 4)

    def test_match_returns_label_and_tagged_text(self):
        matches = self.script.match("Report a lost debit card")
        self.assertEqual([m.label for m in matches], ["card:lost"])
        self.assertEqual(matches[0].tagged, "Report a lost <card type=\"DEBIT\">debit card</card>")
        matches = self.script.match("체크카드 분실 신고")
        self.assertEqual([m.label for m in matches], ["card:lost"])
        self.assertEqual(matches[0].tagged, "<card type=\"DEBIT\">체크카드</card> 분실 신고")

    def test_match_reports_the_line_of_the_rule_that_matched(self):
        self.assertEqual(self.script.match("yen exchange rate")[0].line, 6)
        self.assertEqual(self.script.match("신용카드 분실 신고")[0].line, 10)

    def test_match_best_keeps_the_most_specific_labels(self):
        script = nlu_script.Script("card:lost\n    I lost my debit card\n"
                                   "report:any\n    I <?>+?\n")
        self.assertEqual([m.label for m in script.match("I lost my debit card")],
                         ["card:lost", "report:any"])
        best = script.match_best("I lost my debit card")
        self.assertEqual([(m.label, m.specificity) for m in best], [("card:lost", 16)])
        self.assertEqual(script.match_best("thank you"), [])

    def test_no_match_returns_empty_list(self):
        self.assertEqual(self.script.match("thank you"), [])

    def test_slot_offsets_are_character_offsets(self):
        for text, currency in [("엔화 환율 알려줘", "엔화"), ("yen exchange rate", "yen")]:
            slot = self.script.match(text)[0].slots[0]
            self.assertEqual(slot.name, "통화")
            self.assertEqual(slot.text, currency)
            self.assertEqual(text[slot.begin:slot.end], currency)
            self.assertEqual(slot.attributes, {"code": "JPY"})

    def test_decomposed_hangul_input_matches(self):
        text = unicodedata.normalize("NFD", "체크카드 분실 신고")
        slot = self.script.match(text)[0].slots[0]
        self.assertEqual(text[slot.begin:slot.end], unicodedata.normalize("NFD", "체크카드"))
        self.assertEqual(slot.attributes, {"type": "DEBIT"})

    def test_context_manager_closes_script(self):
        with nlu_script.Script("t\n    잔액 조회\n") as script:
            self.assertEqual(len(script.match("잔액조회")), 1)
        with self.assertRaises(ValueError):
            script.match("잔액조회")


class CompileTest(unittest.TestCase):
    def test_compile_error_has_location(self):
        with self.assertRaises(nlu_script.CompileError) as caught:
            nlu_script.Script("card:lost\n    [신용|체크카드 분실\n")
        self.assertEqual(caught.exception.line, 2)
        self.assertGreater(caught.exception.column, 0)

    def test_compile_error_lists_every_error(self):
        with self.assertRaises(nlu_script.CompileError) as caught:
            nlu_script.Script("card:lost\n    [신용|체크카드 분실\n    I lost my @{card}\n")
        self.assertEqual([line for line, _, _ in caught.exception.errors], [2, 3])
        self.assertEqual(caught.exception.line, 2)
        self.assertIn("@{card}", caught.exception.errors[1][2])

    def test_from_file(self):
        handle, path = tempfile.mkstemp(suffix=".nlu")
        try:
            with os.fdopen(handle, "wb") as f:
                f.write(SCRIPT.encode("utf-8"))
            with nlu_script.Script.from_file(path) as script:
                self.assertEqual([m.label for m in script.match("달러 환율")], ["exchange:rate"])
        finally:
            os.remove(path)

    def test_missing_file_raises_os_error(self):
        with self.assertRaises(OSError):
            nlu_script.Script.from_file("/nonexistent/script.nlu")


if __name__ == "__main__":
    unittest.main()
