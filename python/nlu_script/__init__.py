"""Regex script for natural language understanding.

Python binding over the nlu_script C interface (loaded with ctypes).

    >>> import nlu_script
    >>> script = nlu_script.Script("agent:connect\\n    talk to an agent (please)\\n")
    >>> [m.label for m in script.match("Talk to an agent")]
    ['agent:connect']

The shared library is looked up in this order: the NLU_SCRIPT_LIBRARY
environment variable, the package directory, then the system library path.
"""
import collections
import ctypes
import ctypes.util
import datetime
import itertools
import os
import sys

__all__ = ["CompileError", "DateTimeRange", "Diagnostic", "Explanation", "LintFinding",
           "LintReport", "Match", "Script", "Slot", "TestFailure", "TestReport", "resolve_datetime"]
__version__ = "0.1.0"


def _library_name():
    if sys.platform == "win32":
        return "nlu_script.dll"
    if sys.platform == "darwin":
        return "libnlu_script.dylib"
    return "libnlu_script.so"


def _load_library():
    candidates = []
    if os.environ.get("NLU_SCRIPT_LIBRARY"):
        candidates.append(os.environ["NLU_SCRIPT_LIBRARY"])
    candidates.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), _library_name()))
    found = ctypes.util.find_library("nlu_script")
    if found:
        candidates.append(found)
    for path in candidates:
        if os.path.isabs(path) and not os.path.exists(path):
            continue
        try:
            return ctypes.CDLL(path)
        except OSError:
            continue
    raise ImportError(
        "cannot find the nlu_script shared library; build it with CMake and "
        "set NLU_SCRIPT_LIBRARY to its path")


_lib = _load_library()


def _declare(name, restype, *argtypes):
    function = getattr(_lib, name)
    function.restype = restype
    function.argtypes = list(argtypes)
    return function


_size = ctypes.c_size_t
_ptr = ctypes.c_void_p
_compile = _declare("nlu_script_compile_diagnose", _ptr, ctypes.c_char_p, _ptr)
_compile_file = _declare("nlu_script_compile_file_diagnose", _ptr, ctypes.c_char_p, _ptr)
_diagnostics_free = _declare("nlu_diagnostics_free", None, _ptr)
_diagnostics_text = _declare("nlu_diagnostics_text", ctypes.c_char_p, _ptr)
_diagnostics_count = _declare("nlu_diagnostics_count", _size, _ptr)
_diagnostics_line = _declare("nlu_diagnostics_line", ctypes.c_int, _ptr, _size)
_diagnostics_column = _declare("nlu_diagnostics_column", ctypes.c_int, _ptr, _size)
_diagnostics_message = _declare("nlu_diagnostics_message", ctypes.c_char_p, _ptr, _size)
_diagnostics_file = _declare("nlu_diagnostics_file", ctypes.c_char_p, _ptr, _size)
_script_free = _declare("nlu_script_free", None, _ptr)
_save = _declare("nlu_script_save", _ptr, _ptr, ctypes.POINTER(_size))
_load = _declare("nlu_script_load", _ptr, ctypes.c_char_p, _size, _ptr)
_rule_count = _declare("nlu_script_rule_count", _size, _ptr)
_match = _declare("nlu_script_match", _ptr, _ptr, ctypes.c_char_p)
_match_best = _declare("nlu_script_match_best", _ptr, _ptr, ctypes.c_char_p)
_search = _declare("nlu_script_search", _ptr, _ptr, ctypes.c_char_p)
_result_begin = _declare("nlu_result_begin", _size, _ptr, _size)
_result_end = _declare("nlu_result_end", _size, _ptr, _size)
_result_free = _declare("nlu_result_free", None, _ptr)
_result_count = _declare("nlu_result_count", _size, _ptr)
_result_label = _declare("nlu_result_label", ctypes.c_char_p, _ptr, _size)
_result_tagged = _declare("nlu_result_tagged", ctypes.c_char_p, _ptr, _size)
_result_line = _declare("nlu_result_line", ctypes.c_int, _ptr, _size)
_result_specificity = _declare("nlu_result_specificity", ctypes.c_int, _ptr, _size)
_result_file = _declare("nlu_result_file", ctypes.c_char_p, _ptr, _size)
_slot_count = _declare("nlu_result_slot_count", _size, _ptr, _size)
_slot_name = _declare("nlu_result_slot_name", ctypes.c_char_p, _ptr, _size, _size)
_slot_text = _declare("nlu_result_slot_text", ctypes.c_char_p, _ptr, _size, _size)
_slot_begin = _declare("nlu_result_slot_begin", _size, _ptr, _size, _size)
_attribute_count = _declare("nlu_result_slot_attribute_count", _size, _ptr, _size, _size)
_attribute_key = _declare("nlu_result_slot_attribute_key", ctypes.c_char_p, _ptr, _size, _size,
                          _size)
_attribute_value = _declare("nlu_result_slot_attribute_value", ctypes.c_char_p, _ptr, _size,
                            _size, _size)
_explain = _declare("nlu_script_explain", _ptr, _ptr, ctypes.c_char_p, _size)
_explanation_free = _declare("nlu_explanation_free", None, _ptr)
_explanation_count = _declare("nlu_explanation_count", _size, _ptr)
_explanation_label = _declare("nlu_explanation_label", ctypes.c_char_p, _ptr, _size)
_explanation_line = _declare("nlu_explanation_line", ctypes.c_int, _ptr, _size)
_explanation_file = _declare("nlu_explanation_file", ctypes.c_char_p, _ptr, _size)
_explanation_matched = _declare("nlu_explanation_matched", _size, _ptr, _size)
_explanation_complete = _declare("nlu_explanation_complete", ctypes.c_int, _ptr, _size)
_explanation_expected_count = _declare("nlu_explanation_expected_count", _size, _ptr, _size)
_explanation_expected = _declare("nlu_explanation_expected", ctypes.c_char_p, _ptr, _size,
                                 _size)
_lint = _declare("nlu_script_lint", _ptr, _ptr, ctypes.c_int)
_lint_free = _declare("nlu_lint_free", None, _ptr)
_lint_count = _declare("nlu_lint_count", _size, _ptr)
_lint_kind = _declare("nlu_lint_kind", ctypes.c_char_p, _ptr, _size)
_lint_file = _declare("nlu_lint_file", ctypes.c_char_p, _ptr, _size)
_lint_line = _declare("nlu_lint_line", ctypes.c_int, _ptr, _size)
_lint_message = _declare("nlu_lint_message", ctypes.c_char_p, _ptr, _size)
_lint_allowed = _declare("nlu_lint_allowed", _size, _ptr)
_lint_complete = _declare("nlu_lint_complete", ctypes.c_int, _ptr)
_run_tests = _declare("nlu_script_run_tests", _ptr, _ptr, ctypes.c_char_p, ctypes.c_char_p,
                      ctypes.c_int, _ptr)
_tests_free = _declare("nlu_test_report_free", None, _ptr)
_tests_passed = _declare("nlu_test_report_passed", _size, _ptr)
_tests_failed = _declare("nlu_test_report_failed", _size, _ptr)
_tests_line = _declare("nlu_test_report_line", ctypes.c_int, _ptr, _size)
_tests_input = _declare("nlu_test_report_input", ctypes.c_char_p, _ptr, _size)
_tests_expected_count = _declare("nlu_test_report_expected_count", _size, _ptr, _size)
_tests_expected = _declare("nlu_test_report_expected", ctypes.c_char_p, _ptr, _size, _size)
_tests_actual_count = _declare("nlu_test_report_actual_count", _size, _ptr, _size)
_tests_actual = _declare("nlu_test_report_actual", ctypes.c_char_p, _ptr, _size, _size)
_tests_actual_line = _declare("nlu_test_report_actual_line", ctypes.c_int, _ptr, _size, _size)
_tests_actual_file = _declare("nlu_test_report_actual_file", ctypes.c_char_p, _ptr, _size,
                              _size)
class _DateTime(ctypes.Structure):
    _fields_ = [("year", ctypes.c_int), ("month", ctypes.c_int), ("day", ctypes.c_int),
                ("hour", ctypes.c_int), ("minute", ctypes.c_int)]


_resolve = _declare("nlu_resolve_datetime", ctypes.c_int, ctypes.POINTER(ctypes.c_char_p),
                    ctypes.POINTER(ctypes.c_char_p), _size, ctypes.POINTER(_DateTime),
                    ctypes.POINTER(_DateTime), ctypes.POINTER(_DateTime),
                    ctypes.POINTER(ctypes.c_char_p))
_string_free = _declare("nlu_string_free", None, _ptr)


def _decode(data):
    return data.decode("utf-8")


def _char_offsets(data):
    """For each byte offset into UTF-8 `data`, the number of characters before it."""
    starts = (int((byte & 0xC0) != 0x80) for byte in data)  # not a continuation byte
    return list(itertools.accumulate(starts, initial=0))


Diagnostic = collections.namedtuple("Diagnostic", "line column message file")


class CompileError(ValueError):
    """Raised for syntax and reference errors. `errors` lists every error as a
    Diagnostic(line, column, message, file), ordered by file and position
    (`file` is empty for the main script compiled from a string); `line` and
    `column` (1-based) locate the first."""

    def __init__(self, message, line, column, errors=None):
        super().__init__(message)
        self.message = message
        self.line = line
        self.column = column
        self.errors = errors if errors is not None else [Diagnostic(line, column, message, "")]


class Slot(collections.namedtuple("Slot", "name text begin end attributes")):
    """A captured span. `begin` and `end` are character offsets into the input."""

    __slots__ = ()


class Match(collections.namedtuple("Match",
                                    "label slots tagged line specificity file begin end")):
    """A matched label. `line` and `file` locate the rule that matched (`file`
    is empty for a script compiled from a string); `specificity` counts the
    input characters, not counting spaces, that the rule matched with pattern
    text rather than wildcards; `begin` and `end` are character offsets of the
    matched text (the whole input for match(), a span for search())."""

    __slots__ = ()


class Explanation(collections.namedtuple("Explanation",
                                          "label line file matched complete expected")):
    """How far one rule got into an input, from Script.explain(). `matched` is
    a character offset into the input; `expected` lists what the rule could have
    taken next ("c", "space", "any character")."""

    __slots__ = ()


LintFinding = collections.namedtuple("LintFinding", "kind file line message")
LintReport = collections.namedtuple("LintReport", "findings allowed complete")
TestReport = collections.namedtuple("TestReport", "passed failed failures")


class TestFailure(collections.namedtuple("TestFailure", "line input expected actual rules")):
    """A script test case whose output differs: the line of its input sentence,
    the input, the expected and actual output lines, and for each actual line
    the (file, line) of the rule behind it (none for "-"; `file` is empty for a
    script compiled from a string)."""

    __slots__ = ()


DateTimeRange = collections.namedtuple("DateTimeRange", "start end grain")


def resolve_datetime(attributes, now):
    """Resolves the attributes of a DATETIME slot (a dict) against `now`, a
    datetime.datetime or datetime.date in the caller's time zone. Returns a
    DateTimeRange whose `start` and `end` (None unless the slot is a range) are
    datetimes when the text gives a time and dates otherwise; `grain` is the
    finest unit the text gave ("year" ... "minute")."""
    keys = (ctypes.c_char_p * len(attributes))(*[k.encode("utf-8") for k in attributes])
    values = (ctypes.c_char_p * len(attributes))(
        *[v.encode("utf-8") for v in attributes.values()])
    hour = getattr(now, "hour", -1)
    minute = getattr(now, "minute", -1)
    reference = _DateTime(now.year, now.month, now.day, hour, minute)
    start, end = _DateTime(), _DateTime()
    grain = ctypes.c_char_p()
    has_end = _resolve(keys, values, len(attributes), ctypes.byref(reference), ctypes.byref(start),
                       ctypes.byref(end), ctypes.byref(grain))

    def convert(value):
        if value.hour < 0:
            return datetime.date(value.year, value.month, value.day)
        return datetime.datetime(value.year, value.month, value.day, value.hour,
                                 max(value.minute, 0))

    return DateTimeRange(convert(start), convert(end) if has_end else None,
                         _decode(grain.value))


def _compile_with(function, argument):
    diagnostics = ctypes.c_void_p()
    handle = function(argument, ctypes.byref(diagnostics))
    if handle:
        return handle
    if not diagnostics.value:
        raise MemoryError("nlu_script_compile failed")
    try:
        message = _decode(_diagnostics_text(diagnostics))
        errors = [Diagnostic(_diagnostics_line(diagnostics, i), _diagnostics_column(diagnostics, i),
                             _decode(_diagnostics_message(diagnostics, i)),
                             _decode(_diagnostics_file(diagnostics, i)))
                  for i in range(_diagnostics_count(diagnostics))]
    finally:
        _diagnostics_free(diagnostics)
    if errors:
        raise CompileError(message, errors[0].line, errors[0].column, errors)
    raise OSError(message)


class Script:
    """A compiled script. Matching is thread-safe."""

    def __init__(self, source):
        self._handle = _compile_with(_compile, source.encode("utf-8"))

    @classmethod
    def from_file(cls, path):
        script = cls.__new__(cls)
        script._handle = _compile_with(_compile_file, os.fsencode(path))
        return script

    @classmethod
    def load(cls, data):
        """Loads a script saved by save(); raises ValueError for other bytes."""
        error = ctypes.c_void_p()
        handle = _load(data, len(data), ctypes.byref(error))
        if not handle:
            message = _decode(ctypes.string_at(error.value)) if error.value else "invalid data"
            _string_free(error)
            raise ValueError(message)
        script = cls.__new__(cls)
        script._handle = handle
        return script

    def save(self):
        """The compiled script as bytes, which load() reads back without
        compiling again."""
        size = _size()
        data = _save(self._checked(), ctypes.byref(size))
        if not data:
            raise MemoryError("nlu_script_save failed")
        try:
            return ctypes.string_at(data, size.value)
        finally:
            _string_free(data)

    @property
    def rule_count(self):
        return _rule_count(self._checked())

    def match(self, text):
        """Returns a list of Match, one per matching label, in script order."""
        return self._run(_match, text)

    def search(self, text):
        """Finds rules inside free text (see Script::search in C++): a list of
        Match, each for a span of `text`."""
        return self._run(_search, text)

    def match_best(self, text):
        """Like match(), but keeps only the labels whose most specific rule has
        the highest specificity."""
        return self._run(_match_best, text)

    def _run(self, function, text):
        data = text.encode("utf-8")
        result = function(self._checked(), data)
        if not result:
            raise MemoryError("nlu_script_match failed")
        try:
            offsets = _char_offsets(data)
            return [self._read_match(result, i, offsets) for i in range(_result_count(result))]
        finally:
            _result_free(result)

    def explain(self, text, limit=3):
        """For debugging rules: the `limit` rules that got furthest into `text`,
        as a list of Explanation, longest match first."""
        data = text.encode("utf-8")
        result = _explain(self._checked(), data, limit)
        if not result:
            raise MemoryError("nlu_script_explain failed")
        try:
            offsets = _char_offsets(data)
            return [Explanation(
                _decode(_explanation_label(result, i)), _explanation_line(result, i),
                _decode(_explanation_file(result, i)),
                offsets[_explanation_matched(result, i)],
                bool(_explanation_complete(result, i)),
                [_decode(_explanation_expected(result, i, j))
                 for j in range(_explanation_expected_count(result, i))])
                for i in range(_explanation_count(result))]
        finally:
            _explanation_free(result)

    def lint(self, best=False):
        """Lints the script (as `nlu_script_cli --lint`) and returns a
        LintReport: `findings` (LintFinding(kind, file, line, message), kind one
        of "overlap", "dead-rule", "unused", "directive"), how many overlaps
        `# lint: allow-overlap` comments allow, and whether the search was
        complete. With `best`, overlaps that match_best() resolves are left out."""
        result = _lint(self._checked(), 1 if best else 0)
        if not result:
            raise MemoryError("nlu_script_lint failed")
        try:
            findings = [LintFinding(_decode(_lint_kind(result, i)), _decode(_lint_file(result, i)),
                                    _lint_line(result, i), _decode(_lint_message(result, i)))
                        for i in range(_lint_count(result))]
            return LintReport(findings, _lint_allowed(result), bool(_lint_complete(result)))
        finally:
            _lint_free(result)

    def run_tests(self, path, best=False):
        """Runs a script test file (as `nlu_script_cli --test`) and returns a
        TestReport. With `best`, cases expect match_best() output. Raises
        ValueError ("path:line: ...") for a malformed file."""
        with open(path, "rb") as f:
            tests = f.read()
        error = ctypes.c_void_p()
        report = _run_tests(self._checked(), tests, os.fsencode(path), 1 if best else 0,
                            ctypes.byref(error))
        if not report:
            message = _decode(ctypes.string_at(error.value)) if error.value else None
            _string_free(error)
            if message is None:
                raise MemoryError("nlu_script_run_tests failed")
            raise ValueError(message)
        try:
            failures = []
            for i in range(_tests_failed(report)):
                count = _tests_actual_count(report, i)
                # "-" (nothing matched) has no rule: its line is 0.
                rules = [(_decode(_tests_actual_file(report, i, j)),
                          _tests_actual_line(report, i, j))
                         for j in range(count) if _tests_actual_line(report, i, j)]
                failures.append(TestFailure(
                    _tests_line(report, i), _decode(_tests_input(report, i)),
                    [_decode(_tests_expected(report, i, j))
                     for j in range(_tests_expected_count(report, i))],
                    [_decode(_tests_actual(report, i, j)) for j in range(count)], rules))
            return TestReport(_tests_passed(report), len(failures), failures)
        finally:
            _tests_free(report)

    def close(self):
        handle, self._handle = getattr(self, "_handle", None), None
        if handle:
            _script_free(handle)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def __del__(self):
        self.close()

    def _checked(self):
        if not self._handle:
            raise ValueError("script is closed")
        return self._handle

    @staticmethod
    def _read_match(result, i, offsets):
        slots = []
        for j in range(_slot_count(result, i)):
            attributes = {_decode(_attribute_key(result, i, j, k)):
                          _decode(_attribute_value(result, i, j, k))
                          for k in range(_attribute_count(result, i, j))}
            text = _decode(_slot_text(result, i, j))
            begin = offsets[_slot_begin(result, i, j)]
            slots.append(Slot(_decode(_slot_name(result, i, j)), text, begin, begin + len(text),
                              attributes))
        begin, end = offsets[_result_begin(result, i)], offsets[_result_end(result, i)]
        return Match(_decode(_result_label(result, i)), slots, _decode(_result_tagged(result, i)),
                     _result_line(result, i), _result_specificity(result, i),
                     _decode(_result_file(result, i)), begin, end)
