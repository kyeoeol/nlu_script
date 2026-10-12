"""Regex script for natural language understanding.

Python binding over the nlu_script C interface (loaded with ctypes).

    >>> import nlu_script
    >>> script = nlu_script.Script(u"agent:connect\\n    talk to an agent (please)\\n")
    >>> [m.label for m in script.match(u"Talk to an agent")]
    ['agent:connect']

The shared library is looked up in this order: the NLU_SCRIPT_LIBRARY
environment variable, the package directory, then the system library path.
"""
import ctypes
import ctypes.util
import os
import sys

__all__ = ["CompileError", "Match", "Script", "Slot"]
__version__ = "0.1.0"


def _library_names():
    if sys.platform == "win32":
        return ["nlu_script.dll"]
    if sys.platform == "darwin":
        return ["libnlu_script.dylib"]
    return ["libnlu_script.so"]


def _load_library():
    candidates = []
    if os.environ.get("NLU_SCRIPT_LIBRARY"):
        candidates.append(os.environ["NLU_SCRIPT_LIBRARY"])
    here = os.path.dirname(os.path.abspath(__file__))
    candidates.extend(os.path.join(here, name) for name in _library_names())
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
_c_char_pp = ctypes.POINTER(ctypes.c_char_p)
_c_int_p = ctypes.POINTER(ctypes.c_int)


def _declare(name, restype, *argtypes):
    function = getattr(_lib, name)
    function.restype = restype
    function.argtypes = list(argtypes)
    return function


_size = ctypes.c_size_t
_ptr = ctypes.c_void_p
_compile = _declare("nlu_script_compile", _ptr, ctypes.c_char_p, ctypes.c_void_p, _c_int_p, _c_int_p)
_compile_file = _declare("nlu_script_compile_file", _ptr, ctypes.c_char_p, ctypes.c_void_p,
                         _c_int_p, _c_int_p)
_script_free = _declare("nlu_script_free", None, _ptr)
_rule_count = _declare("nlu_script_rule_count", _size, _ptr)
_match = _declare("nlu_script_match", _ptr, _ptr, ctypes.c_char_p)
_result_free = _declare("nlu_result_free", None, _ptr)
_result_count = _declare("nlu_result_count", _size, _ptr)
_result_label = _declare("nlu_result_label", ctypes.c_char_p, _ptr, _size)
_result_tagged = _declare("nlu_result_tagged", ctypes.c_char_p, _ptr, _size)
_slot_count = _declare("nlu_result_slot_count", _size, _ptr, _size)
_slot_name = _declare("nlu_result_slot_name", ctypes.c_char_p, _ptr, _size, _size)
_slot_begin = _declare("nlu_result_slot_begin", _size, _ptr, _size, _size)
_slot_end = _declare("nlu_result_slot_end", _size, _ptr, _size, _size)
_attribute_count = _declare("nlu_result_slot_attribute_count", _size, _ptr, _size, _size)
_attribute_key = _declare("nlu_result_slot_attribute_key", ctypes.c_char_p, _ptr, _size, _size,
                          _size)
_attribute_value = _declare("nlu_result_slot_attribute_value", ctypes.c_char_p, _ptr, _size,
                            _size, _size)
_string_free = _declare("nlu_string_free", None, ctypes.c_void_p)


def _decode(data):
    return data.decode("utf-8")


class CompileError(ValueError):
    """Raised for syntax and reference errors; `line` and `column` are 1-based."""

    def __init__(self, message, line, column):
        ValueError.__init__(self, message)
        self.message = message
        self.line = line
        self.column = column


class Slot(object):
    """A captured span. `begin` and `end` are character offsets into the input."""

    __slots__ = ("name", "text", "begin", "end", "attributes")

    def __init__(self, name, text, begin, end, attributes):
        self.name = name
        self.text = text
        self.begin = begin
        self.end = end
        self.attributes = attributes

    def __repr__(self):
        return "Slot(name=%r, text=%r, begin=%d, end=%d, attributes=%r)" % (
            self.name, self.text, self.begin, self.end, self.attributes)


class Match(object):
    __slots__ = ("label", "slots", "tagged")

    def __init__(self, label, slots, tagged):
        self.label = label
        self.slots = slots
        self.tagged = tagged

    def __repr__(self):
        return "Match(label=%r, tagged=%r, slots=%r)" % (self.label, self.tagged, self.slots)


def _compile_with(function, argument):
    error = ctypes.c_void_p()
    line = ctypes.c_int()
    column = ctypes.c_int()
    handle = function(argument, ctypes.byref(error), ctypes.byref(line), ctypes.byref(column))
    if handle:
        return handle
    message = _decode(ctypes.string_at(error.value)) if error.value else "unknown error"
    _string_free(error)
    if line.value > 0:
        raise CompileError(message, line.value, column.value)
    raise OSError(message)


class Script(object):
    """A compiled script. Matching is thread-safe."""

    def __init__(self, source):
        self._handle = _compile_with(_compile, source.encode("utf-8"))

    @classmethod
    def from_file(cls, path):
        script = cls.__new__(cls)
        script._handle = _compile_with(_compile_file, os.fsencode(path))
        return script

    @property
    def rule_count(self):
        return _rule_count(self._checked())

    def match(self, text):
        """Returns a list of Match, one per matching label, in script order."""
        data = text.encode("utf-8")
        result = _match(self._checked(), data)
        if not result:
            raise MemoryError("nlu_script_match failed")
        try:
            return [self._read_match(result, i, data) for i in range(_result_count(result))]
        finally:
            _result_free(result)

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
    def _read_match(result, i, data):
        slots = []
        for j in range(_slot_count(result, i)):
            begin = _slot_begin(result, i, j)
            end = _slot_end(result, i, j)
            attributes = {}
            for k in range(_attribute_count(result, i, j)):
                attributes[_decode(_attribute_key(result, i, j, k))] = _decode(
                    _attribute_value(result, i, j, k))
            char_begin = len(_decode(data[:begin]))
            slots.append(Slot(_decode(_slot_name(result, i, j)), _decode(data[begin:end]),
                              char_begin, char_begin + len(_decode(data[begin:end])), attributes))
        return Match(_decode(_result_label(result, i)), slots, _decode(_result_tagged(result, i)))
