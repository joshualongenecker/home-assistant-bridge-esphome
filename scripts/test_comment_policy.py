"""Tests for scripts/comment_policy.py, the comment ratchet gate.

The gate blocks CI and rewrites source files, so its two dangerous properties are
pinned here: comment removal never changes code, and classification keeps the
buckets that drive deletion honest.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import comment_policy as cp


def _spans(text, lang):
    return [text[s:e] for s, e in cp._comment_offsets(text, lang)]


def test_comment_offsets_locate_line_block_and_inline_comments():
    text = 'int f(int a /*self*/) {  // trailing\n  return a; }\n'
    assert _spans(text, "cpp") == ["/*self*/", "// trailing"]


def test_comment_offsets_ignore_markers_inside_literals():
    text = 'const char* s = "http://example.com/#anchor";  // real\n'
    assert _spans(text, "cpp") == ["// real"]
    text = 'x = "# not a comment"\n"""doc # still doc\nmore"""\ny = 1  # real\n'
    assert _spans(text, "py") == ["# real"]


def test_removing_comments_leaves_code_identical():
    text = ('int f(int a /*self*/, int b)  // trailing note\n'
            '{\n'
            '  /* block\n'
            '   * comment */\n'
            '  return a + b;  // sum\n'
            '}\n')
    removed = cp._remove_comments(text, cp._comment_offsets(text, "cpp"))
    assert cp._code_only("f.cpp", removed) == cp._code_only("f.cpp", text)


def test_comment_only_lines_leave_no_indentation_behind():
    text = 'int main(void)\n{\n  /* a block\n   * comment */\n  f();\n}\n'
    removed = cp._remove_comments(text, cp._comment_offsets(text, "cpp"))
    assert removed == 'int main(void)\n{\n  f();\n}\n'


def test_inline_comment_keeps_the_rest_of_the_line():
    text = 'static int f(int a /*self*/) { return a; }\n'
    removed = cp._remove_comments(text, cp._comment_offsets(text, "cpp"))
    assert removed == 'static int f(int a ) { return a; }\n'


def test_code_only_ignores_comments_but_not_changed_code():
    before = 'int f(void) { return 1; }  // one\n'
    same = 'int f(void) { return 1; }\n'
    changed = 'int f(void) { return 2; }\n'
    assert cp._code_only("f.cpp", before) == cp._code_only("f.cpp", same)
    assert cp._code_only("f.cpp", before) != cp._code_only("f.cpp", changed)


def test_collapse_blanks_absorbs_lines_left_by_removal():
    text = 'a;\n\n\n// gone\n\n\nb;\n'
    removed = cp._remove_comments(text, cp._comment_offsets(text, "cpp"))
    assert cp._collapse_blanks(removed) == 'a;\n\nb;\n'


def test_classify_separates_directives_deferrals_citations_and_code():
    index = {"cache", "entry", "find", "lock", "publish", "arena", "issue"}

    def cat(text):
        c = cp.Comment("x.cpp", 1, 1, text, "line")
        return cp.classify(c, [text], index)[0]

    assert cat("// NOLINTNEXTLINE(clang-analyzer)") == "directive"
    assert cat("// TODO: make the arena growable") == "excuse"
    assert cat("// see issue #203 for the reproduction") == "decision_record"
    assert cat("// must not hold the lock across the publish") == "decision_record"
    assert cat("// erd_cache_reset(self);") == "dead_code"
    assert cat("// ---------------------------------------------------") == "tautology"
    assert cat("// init cache") == "tautology"


def test_classify_only_calls_a_comment_stale_on_evidence():
    index = {"registry", "copy", "rec"}

    def cat(text):
        c = cp.Comment("x.cpp", 1, 1, text, "line")
        return cp.classify(c, [text], index)[0]

    assert cat("// erd_cache_poll_next() was removed in rec 9") == "stale"
    assert cat("// a new ERD means the quiet timer restarts") != "stale"


def test_extract_counts_a_block_comment_once_with_its_line_span():
    text = '/* one\n * two */\nint x;\n// three\n'
    comments = cp.extract("a.cpp", text)
    assert [(c.start, c.end) for c in comments] == [(1, 2), (4, 4)]


def test_python_extraction_ignores_hash_lines_inside_mid_line_templates():
    text = ('header = f"""\n'
            '#ifndef ERD_LISTS_H\n'
            '#define ERD_LISTS_H\n'
            '#endif\n'
            '"""\n'
            '# real\n')
    assert [(a, b, t) for a, b, t, _k in cp.extract_py(text)] == [(6, 6, "# real")]


def test_python_extraction_reports_docstrings_as_their_own_kind():
    text = ('"""Module doc.\n'
            '\n'
            'More.\n'
            '"""\n'
            '\n'
            'def f():\n'
            '    """Body doc."""\n'
            '    return 1  # note\n')
    kinds = [(a, b, k) for a, b, _t, k in cp.extract_py(text)]
    assert (1, 4, "docstring") in kinds
    assert (7, 7, "docstring") in kinds
    assert (8, 8, "trailing") in kinds


def test_collapse_blanks_keeps_whitespace_inside_string_templates():
    text = ('# note\n'
            'header = f"""\n'
            'line   \n'
            '\n'
            'tail\n'
            '"""\n'
            'code = 1   # trailing\n')
    mask = cp._string_line_mask(text, "py")
    assert mask[2] and mask[3], "template lines must be masked"
    reduced = cp._remove_comments(text, [(0, 5), (len(text) - 9, len(text) - 1)])
    out = cp._collapse_blanks(reduced, mask)
    assert "line   \n\n" in out, "template whitespace is program output"
    assert not out.startswith("# note")
    assert "code = 1" in out and "# trailing" not in out


def test_cpp_string_mask_marks_raw_string_lines():
    text = ('const char* k = R"md(a\n'
            'b   \n'
            ')md";\n')
    mask = cp._string_line_mask(text, "cpp")
    assert mask[0] and mask[1] and mask[2]
    assert not cp._string_line_mask("int x;  // c\n", "cpp")[0]
