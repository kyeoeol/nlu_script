import os
import tempfile
import unittest

import nlu_script

SCRIPT = u"""
@메뉴: 치킨, 피자{value=pizza}

order
    @{메뉴} [주문|배달] (해줘)

alarm
    wake me up at <time>[nine{hour=9}|ten{hour=10}] o'clock</time>
"""


class ScriptTest(unittest.TestCase):
    def setUp(self):
        self.script = nlu_script.Script(SCRIPT)

    def tearDown(self):
        self.script.close()

    def test_rule_count(self):
        self.assertEqual(self.script.rule_count, 2)

    def test_match_returns_label_and_tagged_text(self):
        matches = self.script.match(u"Wake me up at ten o'clock")
        self.assertEqual([m.label for m in matches], [u"alarm"])
        self.assertEqual(matches[0].tagged, u"Wake me up at <time hour=\"10\">ten o'clock</time>")

    def test_no_match_returns_empty_list(self):
        self.assertEqual(self.script.match(u"good night"), [])

    def test_slot_offsets_are_character_offsets(self):
        text = u"피자 배달해줘"
        slot = self.script.match(text)[0].slots[0]
        self.assertEqual(slot.name, u"메뉴")
        self.assertEqual(slot.text, u"피자")
        self.assertEqual(text[slot.begin:slot.end], u"피자")
        self.assertEqual(slot.attributes, {u"value": u"pizza"})

    def test_context_manager_closes_script(self):
        with nlu_script.Script(u"t\n    hi\n") as script:
            self.assertEqual(len(script.match(u"hi")), 1)
        with self.assertRaises(ValueError):
            script.match(u"hi")


class CompileTest(unittest.TestCase):
    def test_compile_error_has_location(self):
        with self.assertRaises(nlu_script.CompileError) as caught:
            nlu_script.Script(u"greeting\n    hello [world\n")
        self.assertEqual(caught.exception.line, 2)
        self.assertGreater(caught.exception.column, 0)

    def test_from_file(self):
        handle, path = tempfile.mkstemp(suffix=".nlu")
        try:
            with os.fdopen(handle, "wb") as f:
                f.write(SCRIPT.encode("utf-8"))
            with nlu_script.Script.from_file(path) as script:
                self.assertEqual([m.label for m in script.match(u"치킨 주문")], [u"order"])
        finally:
            os.remove(path)

    def test_missing_file_raises_os_error(self):
        with self.assertRaises(OSError):
            nlu_script.Script.from_file("/nonexistent/script.nlu")


if __name__ == "__main__":
    unittest.main()
