"""Recognize emitted sanitizer diagnostics, not ordinary sanitizer/config names."""

import re


def sanitizer_diagnostic(raw):
    return re.search(
        r"(?m)^(?:"
        r"(?:==\d+==)?\s*(?:ERROR|WARNING|SUMMARY): "
        r"(?:AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|"
        r"MemorySanitizer|ThreadSanitizer)\b|"
        r"AddressSanitizer:DEADLYSIGNAL\b|"
        r"[^\n]+:\d+(?::\d+)?: runtime error: )",
        raw,
    )
