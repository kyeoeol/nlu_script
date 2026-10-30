import datetime
import os
import tempfile
import unicodedata
import unittest

import nlu_script

SCRIPT = """
@리워드: [포인트|points]{type=POINTS}, [마일리지|miles]{type=MILES}

reward:points
    @{리워드} 잔액 (알려줘)
    @{리워드} balance

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
        self.assertEqual(self.script.match("miles balance")[0].line, 6)
        self.assertEqual(self.script.match("신용카드 분실 신고")[0].line, 10)

    def test_match_best_keeps_the_most_specific_labels(self):
        script = nlu_script.Script("card:lost\n    I lost my debit card\n"
                                   "report:any\n    I <?>+?\n")
        self.assertEqual([m.label for m in script.match("I lost my debit card")],
                         ["card:lost", "report:any"])
        best = script.match_best("I lost my debit card")
        self.assertEqual([(m.label, m.specificity) for m in best], [("card:lost", 16)])
        self.assertEqual(script.match_best("thank you"), [])

    def test_explain_shows_how_far_rules_got(self):
        found = self.script.explain("체크카드 해지")
        self.assertEqual([(e.label, e.line, e.complete) for e in found], [("card:lost", 10, False)])
        # `matched` is a character offset, like slot offsets.
        self.assertEqual("체크카드 해지"[:found[0].matched], "체크카드 ")
        self.assertEqual(found[0].expected, ["분"])

    def test_run_tests_checks_expected_output(self):
        handle, path = tempfile.mkstemp(suffix=".test")
        try:
            with os.fdopen(handle, "w", encoding="utf-8") as f:
                f.write("# cases\n"
                        "Report a lost debit card\n"
                        "    card:lost:Report a lost <card type=\"DEBIT\">debit card</card>\n"
                        "마일리지 잔액\n"
                        "    card:lost:마일리지 잔액\n"
                        "thank you\n"
                        "    -\n")
            report = self.script.run_tests(path)
            self.assertEqual((report.passed, report.failed), (2, 1))
            failure = report.failures[0]
            self.assertEqual((failure.line, failure.input), (4, "마일리지 잔액"))
            self.assertEqual(failure.expected, ["card:lost:마일리지 잔액"])
            self.assertEqual(failure.actual, ["reward:points:<리워드 type=\"MILES\">마일리지</리워드> 잔액"])
            # The rule behind each actual line, as `nlu_script_cli --test` shows it.
            self.assertEqual(failure.rules, [("", 5)])
        finally:
            os.remove(path)

    def test_run_tests_rejects_malformed_files(self):
        handle, path = tempfile.mkstemp(suffix=".test")
        try:
            with os.fdopen(handle, "w", encoding="utf-8") as f:
                f.write("    -\n")
            with self.assertRaises(ValueError) as caught:
                self.script.run_tests(path)
            self.assertIn(":1:", str(caught.exception))
        finally:
            os.remove(path)

    def test_lint_reports_findings(self):
        script = nlu_script.Script("card:lost\n    I lost my <?>+?\n    I lost my card\n"
                                   "report:any\n    I <?>+?\n"
                                   "# lint: allow-overlap report:any *\n")
        report = script.lint()
        self.assertEqual([(f.kind, f.line) for f in report.findings], [("dead-rule", 3)])
        self.assertEqual(report.allowed, 1)
        self.assertTrue(report.complete)

    def test_resolve_datetime(self):
        script = nlu_script.Script("t\n    `@{DATETIME}\n")
        now = datetime.datetime(2026, 10, 21, 14, 20)
        slot = script.match("내일 오후 3시")[0].slots[0]
        resolved = nlu_script.resolve_datetime(slot.attributes, now)
        self.assertEqual(resolved.start, datetime.datetime(2026, 10, 22, 15, 0))
        self.assertIsNone(resolved.end)
        self.assertEqual(resolved.grain, "hour")
        slot = script.match("내일부터 모레까지")[0].slots[0]
        resolved = nlu_script.resolve_datetime(slot.attributes, now.date())
        self.assertEqual((resolved.start, resolved.end),
                         (datetime.date(2026, 10, 22), datetime.date(2026, 10, 23)))

    def test_search_finds_rules_in_free_text(self):
        text = "그럼 마일리지 잔액 알려주시고 신용카드 분실 신고도 할게요"
        found = self.script.search(text)
        self.assertEqual([(m.label, text[m.begin:m.end]) for m in found],
                         [("reward:points", "마일리지 잔액"), ("card:lost", "신용카드 분실 신고")])

    def test_save_and_load(self):
        data = self.script.save()
        self.assertIsInstance(data, bytes)
        with nlu_script.Script.load(data) as loaded:
            self.assertEqual(loaded.match("체크카드 분실 신고")[0].tagged,
                             self.script.match("체크카드 분실 신고")[0].tagged)
        with self.assertRaises(ValueError):
            nlu_script.Script.load(data[:10])

    def test_no_match_returns_empty_list(self):
        self.assertEqual(self.script.match("thank you"), [])

    def test_slot_offsets_are_character_offsets(self):
        for text, reward in [("마일리지 잔액 알려줘", "마일리지"), ("miles balance", "miles")]:
            slot = self.script.match(text)[0].slots[0]
            self.assertEqual(slot.name, "리워드")
            self.assertEqual(slot.text, reward)
            self.assertEqual(text[slot.begin:slot.end], reward)
            self.assertEqual(slot.attributes, {"type": "MILES"})

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
        self.assertEqual([e.line for e in caught.exception.errors], [2, 3])
        self.assertEqual(caught.exception.line, 2)
        self.assertIn("@{card}", caught.exception.errors[1].message)
        self.assertEqual(caught.exception.errors[1].file, "")

    def test_compile_errors_are_read_field_by_field(self):
        # The included file's name looks like the location part of an error.
        directory = tempfile.mkdtemp()
        try:
            main = os.path.join(directory, "main.nlu")
            with open(main, "w", encoding="utf-8") as f:
                f.write("include x: line 3, column 4: y.nlu\ncard\n    [unclosed\n")
            with self.assertRaises(nlu_script.CompileError) as caught:
                nlu_script.Script.from_file(main)
            errors = caught.exception.errors
            self.assertEqual([(e.file, e.line, e.column) for e in errors],
                             [(main, 1, 9), (main, 3, errors[1].column)])
            self.assertIn("x: line 3, column 4: y.nlu", errors[0].message)
            self.assertEqual((caught.exception.line, caught.exception.column), (1, 9))
        finally:
            os.remove(main)
            os.rmdir(directory)

    def test_from_file(self):
        handle, path = tempfile.mkstemp(suffix=".nlu")
        try:
            with os.fdopen(handle, "wb") as f:
                f.write(SCRIPT.encode("utf-8"))
            with nlu_script.Script.from_file(path) as script:
                matches = script.match("포인트 잔액")
                self.assertEqual([m.label for m in matches], ["reward:points"])
                self.assertEqual(matches[0].file, path)
        finally:
            os.remove(path)

    def test_include_errors_name_their_file(self):
        directory = tempfile.mkdtemp()
        try:
            broken = os.path.join(directory, "broken.nlu")
            with open(broken, "w", encoding="utf-8") as f:
                f.write("x\n    [unclosed\n")
            main = os.path.join(directory, "main.nlu")
            with open(main, "w", encoding="utf-8") as f:
                f.write("include broken.nlu\n")
            with self.assertRaises(nlu_script.CompileError) as caught:
                nlu_script.Script.from_file(main)
            self.assertEqual(caught.exception.errors[0].file, broken)
            self.assertEqual(caught.exception.errors[0].line, 2)
        finally:
            for name in os.listdir(directory):
                os.remove(os.path.join(directory, name))
            os.rmdir(directory)

    def test_missing_file_raises_os_error(self):
        with self.assertRaises(OSError):
            nlu_script.Script.from_file("/nonexistent/script.nlu")


if __name__ == "__main__":
    unittest.main()
