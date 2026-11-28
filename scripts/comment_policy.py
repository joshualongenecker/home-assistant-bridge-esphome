#!/usr/bin/env python3
"""Comment policy gate and triage helper.

Comments are banned in first-party sources; the only durable home for rationale
is docs/adr/ and docs/spec/. The ban is enforced with a ratchet: the committed
baseline (scripts/comment_baseline.json) records the per-file comment count, and
`check` fails if any file exceeds its baseline. Deleting comments lowers the
baseline; adding comments never does.

Scope: git-tracked first-party sources. Excluded:
  - submodules (lib/)
  - vendored third-party code (miniz*)
  - generated files (see scripts/check_generated_sync.sh) — their comments come
    from the generators, so they are fixed in the generator, not by hand.

Subcommands:
  count                 per-file and total comment counts for the current tree
  baseline [--write]    report drift against scripts/comment_baseline.json, or
                        rewrite it with the current counts
  check                 ratchet gate: fail if any file is above its baseline
  audit [--out F]       JSONL manifest: every comment with a heuristic
                        classification (see CATEGORY list)
  delete --audit F --categories C[,C]   delete the audited comments in those
                                        categories, string- and block-aware

Categories (see CATEGORY): tautology, stale, excuse, decision_record, dead_code,
directive, unresolved. `directive` is exempt from the ban: sanitizer/formatter
markers are functional, not prose.

Usage:
  scripts/comment_policy.py check
  scripts/comment_policy.py audit --out /tmp/comments.jsonl
  scripts/comment_policy.py delete --audit /tmp/comments.jsonl \
      --categories tautology,stale,dead_code
"""

import argparse
import collections
import json
import os
import re
import subprocess
import sys

BASELINE_PATH = "scripts/comment_baseline.json"

# Third-party code carried in-tree; its comments belong to upstream.
VENDORED = re.compile(r"(^|/)miniz")
# Produced by scripts/generate_erd_lists.py / scripts/ha_discovery/run_pipeline.py.
GENERATED = {
    "components/geappliances_bridge/erd_lists.h",
    "components/geappliances_bridge/erd_lists.cpp",
    "components/geappliances_bridge/appliance_api_feature_lists.h",
    "components/geappliances_bridge/appliance_api_feature_lists.cpp",
    "components/geappliances_bridge/appliance_type_map.h",
    "components/geappliances_bridge/ha_discovery_data.h",
    "components/geappliances_bridge/ha_discovery_data.cpp",
}
GENERATED_PREFIXES = ("ha_discovery/",)
GENERATED_SUFFIXES = ("appliance_api_erd_definitions_processed.json",)

CPP_EXTS = {".cpp", ".hpp", ".h", ".c"}
PY_EXTS = {".py"}
HASH_EXTS = {".sh"}
SPECIAL_NAMES = {"Makefile", "makefile", "GNUmakefile"}

CATEGORY = ("directive", "doc", "decision_record", "excuse", "stale",
            "dead_code", "tautology", "unresolved")

# Functional markers: never deleted, never counted against the ratchet.
DIRECTIVE = re.compile(
    r"""^\s*(?:
        NOLINT(?:NEXTLINE)?
      | LCOV_EXCL_(?:LINE|START|STOP|BEGIN|END)
      | GCOV_EXCL_(?:LINE|START|STOP)
      | clang-format\s+(?:on|off)
      | fmt:\s*(?:on|off)
      | type:\s*ignore
      | noqa
      | pylint:\s*disable
      | pragma:\s*no\s*cover
      | ruff:\s*noqa
      | mypy:\s*ignore
      | coding[:=]
      | go:\s*(?:build|generate|embed|sum)
      | spdx-?license-?identifier
      | static\s+analysis
      | coverity\[[^\]]*\]
      | NOLINT\(
    )""",
    re.IGNORECASE | re.VERBOSE,
)

# Commented-out code: not prose at all, and the compiler already owns it.
DEAD_CODE = re.compile(
    r"""^\s*(?:
        (?:if|for|while|switch|return|else)\b[^\n]*[{;]
      | [\w\]\)>*\s]+\((?:[^()]|\([^()]*\))*\)\s*(?:;|\{)
      | [#](?:include|define|ifdef|ifndef|endif|undef)\b
      | (?:import|from|print|def|class)\b[^\n]*:
    )\s*$""",
    re.VERBOSE,
)

# A comment that names a defect or defers work without owning it.
EXCUSE = re.compile(
    r"""(?ix)\b(
        todo | fixme | hack | kludge | workaround | wip | xxx
      | temporary(?:ly)? | for\s+now | right\s+now | currently\s+only
      | should\s+really | ideally\s+we | proper(?:ly)?\s+implement
      | simpl(?:e|ified|ification) | naive | quick\s+fix | band-?aid
      | magic\s+number | hard-?coded | placeholder | stub(?:bed)?\s+out
      | not\s+ideal | good\s+enough | ignore\s+for\s+now
      | future\s+work | later | extend\s+this | add\s+(?:a\s+)?more
      | assumes?\b | assumes\s+that | we\s+assume
      | known\s+issue | known\s+limitation | broken | wrong | bug
    )\b""",
)
# Explicit rationale, a hard constraint, or a measured fact.
DECISION = re.compile(
    r"""(?ix)\b(
        because | in\s+order\s+to | to\s+avoid | to\s+prevent | so\s+that
      | otherwise | must\s+not | do\s+not | don't | cannot | can\s+never
      | invariant | contract | load-?bearing | sentinel | deliberately
      | intentionally | chose | chosen | instead\s+of | rather\s+than
      | measured | observed | reproduced | regression | protects
      | pinned | on-?wire | broadcast\s+address | core\s*[01]\b
      | priority\s+\d | watchdog | fragmentation | largest\s+free\s+block
      | see\s+(?:the\s+)?(?:spec|docs|adr|issue|pr|notes|module|README)
      | why: | note\s*: | important\s*: | warning\s*: | caveat\s*:
      | never\s+(?:be\s+)?(?:removed|freed|reused|cached|shared)
      | (?:load|heap)\s+(?:cost|pressure) | out\s+of\s+the\s+gate
    )\b""",
)

# Reference to an issue/PR/commit: pushes an excuse towards a decision record.
CITATION = re.compile(r"(?:#\d{2,6}|\b[0-9a-f]{7,40}\b|issue\s*#?\d|pull\s*request|https?://|commit\s+[0-9a-f]{7,}|\badr\b)", re.IGNORECASE)

# Words that only make sense against an older revision.
STALE_WORDS = re.compile(
    r"""(?ix)\b(
        previously | formerly | used\s+to\s+be | in\s+the\s+past
      | no\s+longer | not\s+used\s+anymore | unused | dead\s+code
      | leftover | obsolete | deprecated | will\s+be\s+replaced
      | to\s+be\s+implemented | tbd | to\s+do | removed\s+in | was\s+renamed
      | old\s+(?:behaviou?r|code|implementation|version|approach)
    )\b""",
)

BANNER_PATTERNS = [
    re.compile(r"(?ix)^\s*[-=*#/ \t]+$"),
    re.compile(r"(?ix)^\s*[-=*#]{3,}\s*(?:[A-Za-z][\w ()/,.-]{0,60})?\s*[-=*#]{3,}\s*$"),
    re.compile(r"(?ix)^\s*[-=*#]{3,}\s*$"),
    re.compile(r"(?ix)^\s*(?:given|when|then)\b\s*$"),
    re.compile(r"(?ix)^\s*(?:section|region|endregion)\b[\w:()<>.,_ -]*$"),
]

# Verb-then-noun comments that just narrate the line below them. Disqualified
# by a subordinate clause: "Store X" restates code, "Store X so that Y can Z"
# records why.
RESTATE_PATTERNS = [
    re.compile(r"""(?ix)^\s*(?:end\s+of|end)\s+(?:the\s+)?
        (?:file|block|function|method|class|namespace|struct|loop|if|for|
         while|switch|constructor|destructor|section|here|main|this)\s*[.!]?\s*$"""),
    re.compile(r"(?ix)^\s*(?:constructor|destructor|getter|setter|accessor|entry\s+point|helper|wrapper)\s*(?:for|of)?\s*[\w:()<>.,' -]*$"),
    re.compile(r"(?ix)^\s*(?:the\s+)?(?:set|get|init|initialize|cleanup|clean\s+up|destroy|free|alloc(?:ate)?|print|log(?:ging)?|check|validate|return|update|handle|process|parse|store|save|load|call|invoke|define|declare|include|increment|decrement|reset|copy|move|convert|cast|compare|iterate|loop|register|unregister|dispatch|emit|publish|subscribe|close|open|start|stop|begin|end)\b[\w:()<>.,_ -]*$"),
    re.compile(r"(?ix)^\s*(?:test|tests?\s+for|test\s+group)\s+[:\-]?\s*[\w ()/,._-]*$"),
]

SUBORDINATE = re.compile(
    r"(?i)\b(so\s+that|because|since|which|while|when|after|before|unless|"
    r"otherwise|note|caution|must|should|will|would|that|and|so|if|e\.g\.|i\.e\.)\b")

STOPWORDS = set("""a an the this that these those it its is are was were be been being
of to in for on with and or as by at from into about than then there here which who whom
we you they i he she do does did not no so if then when while also just very more most
using use used uses will would can could should may might must have has had all any each
per some such only same two three first second next last new old""".split())

IDENT = re.compile(r"[A-Za-z_][A-Za-z_0-9]*")
CODE_IDENT = re.compile(r"\b[A-Za-z_][A-Za-z_0-9]*\b")


def is_generated(path):
    if path in GENERATED:
        return True
    if path.startswith(GENERATED_PREFIXES):
        return True
    return path.endswith(GENERATED_SUFFIXES)


def scope_files():
    files = subprocess.run(["git", "ls-files"], check=True, capture_output=True,
                           text=True).stdout.split("\n")
    out = []
    for f in files:
        if not f or f.startswith("lib/"):
            continue
        if VENDORED.search(f):
            continue
        if is_generated(f):
            continue
        base = os.path.basename(f)
        ext = os.path.splitext(f)[1]
        if ext in CPP_EXTS or ext in PY_EXTS or ext in HASH_EXTS or base in SPECIAL_NAMES:
            out.append(f)
    return sorted(out)


def lang_of(path):
    base = os.path.basename(path)
    if base in SPECIAL_NAMES:
        return "make"
    ext = os.path.splitext(path)[1]
    if ext in CPP_EXTS:
        return "cpp"
    if ext in PY_EXTS:
        return "py"
    return "sh"


def _lines_of(text, start, end):
    return text.count("\n") + 1


class Comment:
    __slots__ = ("file", "start", "end", "text", "kind", "category", "why")

    def __init__(self, file, start, end, text, kind):
        self.file = file
        self.start = start
        self.end = end
        self.text = text
        self.kind = kind  # line | block | trailing | docstring

    def as_dict(self):
        return {"file": self.file, "start": self.start, "end": self.end,
                "kind": self.kind, "text": self.text, "category": self.category,
                "why": self.why}


def extract_cpp(text):
    """String-aware scan for // and /* */ comments. Returns (start,end,text)."""
    out = []
    i, line, n = 0, 1, len(text)
    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            i += 1
        elif c in "\"'":
            q = c
            i += 1
            while i < n and text[i] != q:
                if text[i] == "\\":
                    i += 1
                elif text[i] == "\n":
                    line += 1
                i += 1
            i += 1
        elif c == "\\" and i + 1 < n and text[i + 1] == "\n":
            line += 1
            i += 2
        elif c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            j = n if j == -1 else j
            out.append((line, line, text[i:j]))
            i = j
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            start = line
            j = text.find("*/", i + 2)
            j = n if j == -1 else j + 2
            seg = text[i:j]
            end = start + seg.count("\n")
            out.append((start, end, seg))
            line = end
            i = j
        else:
            i += 1
    return out


def extract_hash(text, only_full_line):
    out = []
    for idx, raw in enumerate(text.split("\n"), 1):
        stripped = raw.lstrip()
        if not stripped.startswith("#"):
            continue
        if idx == 1 and (stripped.startswith("#!") or stripped.startswith("#!")):
            continue
        if only_full_line:
            out.append((idx, idx, raw[raw.index("#"):].rstrip()))
        else:
            # strip string literals before looking for a trailing '#'
            cut = _strip_strings(raw).find("#")
            if cut != -1:
                out.append((idx, idx, raw[cut:].rstrip()))
    return out


def extract_py(text):
    """(start, end, text, kind) for Python.

    Comments and string literals come from one scanner, so a `#` inside a
    multi-line string that starts mid-line (`header = f\"\"\"`) is neither counted
    nor deletable. Docstrings are the triple-quoted spans that start a line.
    """
    comments, strings = _py_spans(text)
    starts = _line_starts(text)
    out = []
    for s, e in strings:
        first = _line_of(s, starts)
        if text[starts[first - 1]:s].strip():
            continue
        out.append((first, _line_of(e - 1, starts), text[s:e], "docstring"))
    for s, e in comments:
        ln = _line_of(s, starts)
        kind = "line" if not text[starts[ln - 1]:s].strip() else "trailing"
        out.append((ln, ln, text[s:e].rstrip(), kind))
    out.sort(key=lambda r: r[0])
    return out


def _balanced_inline(stripped, q):
    return stripped.count(q) >= 2 and len(stripped) > len(q)


def _strip_strings(line):
    """Blank literal contents, preserving every character offset."""
    out = list(line)
    i, n = 0, len(line)
    while i < n:
        c = line[i]
        if c in "\"'":
            q = c
            j = i + 1
            while j < n and line[j] != q:
                if line[j] == "\\":
                    j += 1
                j += 1
            for k in range(i + 1, min(j, n)):
                out[k] = " "
            i = j + 1
        else:
            i += 1
    return "".join(out)


def extract(path, text):
    lang = lang_of(path)
    raw = []
    if lang == "cpp":
        raw = [(a, b, t, "auto") for a, b, t in extract_cpp(text)]
    elif lang == "py":
        raw = extract_py(text)
    else:
        raw = [(a, b, t, "line") for a, b, t in extract_hash(text, False)]
    lines = text.split("\n")
    comments = []
    for a, b, t, kind in raw:
        if kind == "auto":
            kind = "block"
            if a == b:
                code_before = lines[a - 1][: lines[a - 1].find(t)] if t in lines[a - 1] else ""
                kind = "trailing" if code_before.strip() else "line"
        elif kind == "line":
            code_before = lines[a - 1][: lines[a - 1].index(t)] if t in lines[a - 1] else ""
            kind = "trailing" if code_before.strip() else "line"
        comments.append(Comment(path, a, b, t, kind))
    return comments


def _body(comment):
    t = comment.text
    if t.startswith("/*"):
        t = t[2:]
        if t.endswith("*/"):
            t = t[:-2]
    elif t.startswith("//"):
        t = t[2:]
    elif t.startswith("#"):
        t = t[1:]
    else:
        for q in ('"""', "'''"):
            if t.startswith(q):
                t = t[3:]
                if t.endswith(q):
                    t = t[:-3]
                break
    return t.strip()


def content_words(s):
    words = set()
    for w in re.findall(r"[A-Za-z_][A-Za-z_0-9]*", s.lower()):
        for part in re.split(r"[_]+", w):
            if part and part not in STOPWORDS and len(part) > 1:
                words.add(part)
    return words


def context_tokens(lines, c, window=2):
    toks = set()
    lo = max(0, c.start - 1 - window)
    hi = min(len(lines), c.end + window)
    for i in range(lo, hi):
        if c.start - 1 <= i < c.end:
            continue
        for w in IDENT.findall(lines[i]):
            for part in re.split(r"[_]+", w.lower()):
                if part and len(part) > 1:
                    toks.add(part)
    # also tokens on the comment's own line that are not the comment
    for i in range(c.start - 1, c.end):
        seg = lines[i]
        body = _body(c)
        if body and body in seg:
            seg = seg.replace(body, " ")
        for w in IDENT.findall(seg):
            for part in re.split(r"[_]+", w.lower()):
                if part and len(part) > 1:
                    toks.add(part)
    return toks


CODE_SHAPED = re.compile(
    r"""^(?:
        [a-z][a-z0-9]*(?:_[a-z0-9]+)+          # snake_case
      | [a-z][a-z0-9]*[A-Z][A-Za-z0-9]*        # camelCase
      | [A-Z][A-Z0-9]*(?:_[A-Z0-9]+)+          # SCREAMING_SNAKE
      | [A-Za-z_][A-Za-z0-9]*[0-9][A-Za-z0-9]* # contains a digit
    )$""",
    re.VERBOSE,
)


def dangling_idents(body, all_tokens):
    """Code-shaped identifiers named nowhere in the tree: dead references."""
    out = set()
    for tok in CODE_IDENT.findall(body):
        if not CODE_SHAPED.match(tok):
            continue
        if tok.lower() in all_tokens:
            continue
        out.add(tok)
    return out


def classify(c, lines, all_tokens):
    body = _body(c)
    if not body:
        return "tautology", "empty comment marker"
    if DIRECTIVE.match(body):
        return "directive", "tool directive"
    if c.kind == "docstring":
        return "doc", "docstring: API documentation"
    if "\n" not in body and len(body) <= 200 and DEAD_CODE.match(body):
        return "dead_code", "commented-out code"

    cited = CITATION.search(body)
    dec = DECISION.search(body)
    exc = EXCUSE.search(body)
    stale = STALE_WORDS.search(body)
    words = content_words(body)

    for pat in BANNER_PATTERNS:
        if pat.match(body):
            return "tautology", "separator or structural banner"
    if not cited and not dec and not SUBORDINATE.search(body) and len(words) <= 8:
        for pat in RESTATE_PATTERNS:
            if pat.match(body):
                return "tautology", "narrates the adjacent code"

    if 0 < len(words) <= 10 and not dec and not cited and not re.search(r"\d", body):
        if not (words - context_tokens(lines, c)):
            return "tautology", "vocabulary is only the adjacent code"

    if not dec and not cited:
        dead = dangling_idents(body, all_tokens)
        if dead:
            return "stale", "names absent identifier: " + ", ".join(sorted(dead)[:3])
    if stale and not dec:
        return "stale", f"speaks of an older revision ({stale.group(0)})"
    if cited:
        return "decision_record", f"citation ({cited.group(0)})"
    if dec:
        return "decision_record", f"rationale or constraint ({dec.group(0)})"
    if exc:
        return "excuse", f"defers or excuses work ({exc.group(0)})"
    if 0 < len(words) <= 3:
        return "tautology", "label-only comment"
    return "unresolved", "no signal"

def build_ident_index():
    """Every identifier-shaped token in the repo: sources, submodules, docs.

    Used only to decide whether a comment names something that no longer
    exists, so it must cover the submodule APIs and the docs too.
    """
    toks = set()
    seen = set()
    roots = ["lib", "docs", "ha_discovery", ".github", "scripts", "components",
             "test"]
    walk = []
    for r in roots:
        for dirpath, dirnames, filenames in os.walk(r):
            dirnames[:] = [d for d in dirnames
                           if d not in {".git", "build", "build-tsan", ".esphome",
                                        ".venv", "__pycache__", ".espressif"}]
            for fn in filenames:
                if os.path.splitext(fn)[1] in (".h", ".hpp", ".c", ".cpp", ".py",
                                               ".md", ".json", ".jsonl", ".yaml",
                                               ".yml", ".txt"):
                    walk.append(os.path.join(dirpath, fn))
    for f in walk:
        if f in seen:
            continue
        seen.add(f)
        try:
            if os.path.getsize(f) > 8_000_000:
                continue
            text = open(f, encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        words = IDENT.findall(text)
        toks.update(w.lower() for w in words)
        for w in words:
            for part in w.lower().split("_"):
                if len(part) > 1:
                    toks.add(part)
    return toks


def audit(files):
    index = build_ident_index()
    rows = []
    for f in files:
        text = open(f, encoding="utf-8", errors="replace").read()
        lines = text.split("\n")
        for c in extract(f, text):
            cat, why = classify(c, lines, index)
            c.category = cat
            c.why = why
            rows.append(c)
    return rows


def counts(files):
    out = {}
    for f in files:
        text = open(f, encoding="utf-8", errors="replace").read()
        cs = extract(f, text)
        effective = 0
        for c in cs:
            body = _body(c)
            if body and DIRECTIVE.match(body):
                continue
            if c.kind == "docstring":
                continue
            effective += 1
        out[f] = {"comments": len(cs), "counted": effective,
                  "lines": sum(c.end - c.start + 1 for c in cs)}
    return out


def cmd_count(args):
    files = scope_files()
    cs = counts(files)
    total_c = sum(v["counted"] for v in cs.values())
    total_l = sum(v["lines"] for v in cs.values())
    if args.json:
        print(json.dumps({"files": cs, "total_counted": total_c,
                          "total_comment_lines": total_l}, indent=2))
    else:
        for f, v in sorted(cs.items(), key=lambda kv: -kv[1]["lines"]):
            if v["lines"]:
                print(f"{v['lines']:6} {v['counted']:5} {f}")
        print(f"TOTAL comment lines {total_l}  counted comments {total_c}  files {len(files)}")
    return 0


def load_baseline():
    if not os.path.exists(BASELINE_PATH):
        return None
    with open(BASELINE_PATH, encoding="utf-8") as fh:
        return json.load(fh)


def cmd_baseline(args):
    files = scope_files()
    cs = counts(files)
    if args.write:
        payload = {"version": 1,
                   "note": "Per-file ratchet for scripts/comment_policy.py check. "
                           "Lower it as comments are removed; never raise it.",
                   "files": {f: v["counted"] for f, v in cs.items() if v["counted"]}}
        with open(BASELINE_PATH, "w", encoding="utf-8") as fh:
            json.dump(payload, fh, indent=2, sort_keys=True)
            fh.write("\n")
        print(f"wrote {BASELINE_PATH}: {sum(payload['files'].values())} counted comments")
        return 0
    base = load_baseline()
    if base is None:
        print(f"no baseline at {BASELINE_PATH}; run: {sys.argv[0]} baseline --write")
        return 1
    regress = []
    for f, v in cs.items():
        prev = base["files"].get(f, 0)
        if v["counted"] > prev:
            regress.append((f, prev, v["counted"]))
    if regress:
        for f, prev, now in regress:
            print(f"REGRESSION {f}: {prev} -> {now}")
        return 1
    print("baseline drift OK (counts at or below baseline)")
    return 0


def cmd_check(args):
    files = scope_files()
    cs = counts(files)
    base = load_baseline()
    if base is None:
        print(f"FAIL: no comment baseline at {BASELINE_PATH}. "
              f"Run: scripts/comment_policy.py baseline --write", file=sys.stderr)
        return 1
    regress, improved = [], []
    for f, v in cs.items():
        prev = base["files"].get(f, 0)
        if v["counted"] > prev:
            regress.append((f, prev, v["counted"]))
        elif v["counted"] < prev:
            improved.append((f, prev, v["counted"]))
    if regress:
        print("FAIL: comments added past the ratchet baseline.", file=sys.stderr)
        for f, prev, now in regress:
            print(f"  {f}: {prev} -> {now} (+{now - prev})", file=sys.stderr)
        print("\nRationale does not live in source. Put it in docs/adr/NNNN-*.md "
              "(with the file+symbol it explains) or docs/spec/<module>.md.",
              file=sys.stderr)
        print("To lower the baseline after removing comments: "
              "scripts/comment_policy.py baseline --write", file=sys.stderr)
        return 1
    if improved:
        print("NOTE: baseline is above current counts; tighten it with "
              "scripts/comment_policy.py baseline --write")
        for f, prev, now in improved:
            print(f"  {f}: {prev} -> {now}")
    print(f"OK: comment ratchet satisfied ({sum(cs[f]['counted'] for f in cs)} counted comments)")
    return 0


def cmd_audit(args):
    rows = audit(scope_files())
    out = open(args.out, "w", encoding="utf-8") if args.out else sys.stdout
    for r in rows:
        out.write(json.dumps(r.as_dict()) + "\n")
    if args.out:
        tally = collections.Counter(r.category for r in rows)
        lines = collections.Counter()
        for r in rows:
            lines[r.category] += r.end - r.start + 1
        print(f"wrote {args.out}: {len(rows)} comments")
        for cat in CATEGORY:
            print(f"  {cat:16} {tally[cat]:5} comments {lines[cat]:6} lines")
    return 0


def _py_spans(text, triple=True):
    """(comment_spans, string_spans) as character offsets for Python-like text.

    A triple-quoted literal may open mid-line; the scanner tracks it across
    lines so neither its contents nor its closing delimiter are mistaken for a
    comment. `triple=False` is for shell and Make text, where `\"\"\"` is not a
    string opener.
    """
    comments, strings = [], []
    off = 0
    in_str = None
    in_str_start = 0
    for line in text.split("\n"):
        i = 0
        if in_str:
            j = line.find(in_str)
            if j == -1:
                off += len(line) + 1
                continue
            strings.append((in_str_start, off + j + 3))
            in_str = None
            i = j + 3
        while i < len(line):
            c = line[i]
            if triple and line[i:i + 3] in ('"""', "'''"):
                q = line[i:i + 3]
                j = line.find(q, i + 3)
                if j == -1:
                    in_str, in_str_start = q, off + i
                    break
                strings.append((off + i, off + j + 3))
                i = j + 3
            elif c in "\"'":
                q = c
                j = i + 1
                while j < len(line) and line[j] != q:
                    if line[j] == "\\":
                        j += 1
                    j += 1
                i = j + 1
            elif c == "#":
                if off == 0 and line.startswith("#!"):
                    break
                comments.append((off + i, off + len(line)))
                break
            else:
                i += 1
        off += len(line) + 1
    return comments, strings


def _comment_offsets(text, lang):
    """(start, end) character offsets of every comment, string-aware."""
    spans = []
    if lang == "cpp":
        i, n = 0, len(text)
        while i < n:
            c = text[i]
            if c in "\"'":
                q = c
                i += 1
                while i < n and text[i] != q:
                    if text[i] == "\\":
                        i += 1
                    i += 1
                i += 1
            elif c == "/" and i + 1 < n and text[i + 1] == "/":
                j = text.find("\n", i)
                j = n if j == -1 else j
                spans.append((i, j))
                i = j
            elif c == "/" and i + 1 < n and text[i + 1] == "*":
                j = text.find("*/", i + 2)
                j = n if j == -1 else j + 2
                spans.append((i, j))
                i = j
            else:
                i += 1
        return spans
    comments, _strings = _py_spans(text, lang == "py")
    spans.extend(comments)
    return spans


def _line_starts(text):
    starts = [0]
    for i, c in enumerate(text):
        if c == "\n":
            starts.append(i + 1)
    return starts


def _line_of(off, starts):
    lo, hi = 0, len(starts)
    while lo < hi:
        mid = (lo + hi) // 2
        if starts[mid] <= off:
            lo = mid + 1
        else:
            hi = mid
    return lo


def _remove_comments(text, remove):
    """Drop the given comment spans; a line whose only content was the comment
    disappears with its newline, an inline comment leaves its line intact."""
    out = []
    pos = 0
    for s, e in remove:
        line_start = text.rfind("\n", 0, s) + 1
        if text[line_start:s].strip() == "":
            out.append(text[pos:line_start])
            line_end = text.find("\n", e)
            line_end = len(text) if line_end == -1 else line_end
            if text[e:line_end].strip() == "":
                pos = line_end + 1
            else:
                pos = e
        else:
            out.append(text[pos:s])
            pos = e
    out.append(text[pos:])
    return "".join(out)

def _cpp_spans(text):
    """Character offsets of string and character literals in C/C++ text,
    including raw strings R"delim(...)". Comments are ignored on purpose."""
    spans = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == 'R' and text[i + 1:i + 2] == '"':
            opener = text.find("(", i + 2)
            if opener != -1:
                delim = ")" + text[i + 2:opener] + '"'
                end = text.find(delim, opener + 1)
                if end != -1:
                    spans.append((i, end + len(delim)))
                    i = end + len(delim)
                    continue
        if c in '"\'':
            q, j = c, i + 1
            while j < n:
                if text[j] == '\\':
                    j += 2
                    continue
                if text[j] == q or text[j] == "\n":
                    break
                j += 1
            spans.append((i, min(j + 1, n)))
            i = j + 1
            continue
        i += 1
    return spans


def _string_line_mask(text, lang):
    """True for lines whose content lives inside a string literal: their
    whitespace is program output, not formatting, and must survive."""
    if lang == "cpp":
        spans = _cpp_spans(text)
    elif lang in ("py", "sh", "make"):
        spans = _py_spans(text, triple=(lang == "py"))[1]
    else:
        spans = []
    starts, mask = [], [False] * (text.count("\n") + 1)
    off = 0
    for line in text.split("\n"):
        starts.append(off)
        off += len(line) + 1
    for s, e in spans:
        for ln in range(_line_of(s, starts) - 1, _line_of(e - 1, starts)):
            if 0 <= ln < len(mask):
                mask[ln] = True
    return mask


def _collapse_blanks(text, mask=None):
    """Drop blank lines left behind by a deletion. Lines inside a string
    literal are output text: they are never rstripped and never dropped."""
    out = []
    for idx, l in enumerate(text.split("\n")):
        if mask and idx < len(mask) and mask[idx]:
            out.append(l)
            continue
        if l.strip() == "" and (not out or out[-1].strip() == ""):
            continue
        out.append(l if l.strip() == "" else l.rstrip())
    while out and out[0].strip() == "":
        out.pop(0)
    new = "\n".join(out)
    if text.endswith("\n") and not new.endswith("\n"):
        new += "\n"
    return new


def cmd_delete(args):
    wanted = set(args.categories.split(","))
    unknown = wanted - set(CATEGORY)
    if unknown:
        print(f"unknown categories: {','.join(sorted(unknown))}", file=sys.stderr)
        return 2
    ids = None
    if args.ids:
        with open(args.ids, encoding="utf-8") as fh:
            ids = {int(x) for x in fh.read().split() if x.strip()}
    all_recs = []
    with open(args.audit, encoding="utf-8") as fh:
        for n, line in enumerate(fh):
            rec = json.loads(line)
            rec["_idx"] = n
            all_recs.append(rec)

    # Contiguous full-line comments are one unit: the block is deleted only when
    # every comment in it is selected, so a kept rationale is never half-deleted.
    groups = collections.defaultdict(list)
    for rec in sorted(all_recs, key=lambda r: (r["file"], r["start"])):
        g = groups[rec["file"]]
        if (g and g[-1][-1]["kind"] != "trailing" and g[-1][-1]["kind"] != "docstring"
                and rec["kind"] in ("line", "block")
                and g[-1][-1]["end"] + 1 == rec["start"]):
            g[-1].append(rec)
        else:
            g.append([rec])

    by_file = collections.defaultdict(list)
    partial = 0
    for f, gs in groups.items():
        for g in gs:
            sel = [r for r in g
                   if r["category"] in wanted and (ids is None or r["_idx"] in ids)]
            if not sel:
                continue
            if len(sel) != len(g):
                partial += 1
                continue
            for r in sel:
                by_file[f].append((r["start"], r["end"], r["text"]))
    if partial:
        print(f"NOTE {partial} comment blocks left intact: mixed selection "
              f"(a block is deleted whole or not at all)")
    removed = 0
    files_changed = 0
    lines_before = lines_after = 0
    for f, sel in sorted(by_file.items()):
        text = open(f, encoding="utf-8").read()
        lang = lang_of(f)
        starts = _line_starts(text)
        targets = {a for a, _b, _t in sel}
        drop = [(s, e) for (s, e) in _comment_offsets(text, lang)
                if _line_of(s, starts) in targets]
        reduced = _remove_comments(text, drop)
        new = _collapse_blanks(reduced, _string_line_mask(reduced, lang))
        removed += len(drop)
        lines_before += text.count("\n")
        lines_after += new.count("\n")
        if new != text:
            with open(f, "w", encoding="utf-8") as fh:
                fh.write(new)
            files_changed += 1
    print(f"deleted {removed} comments ({lines_before - lines_after} lines shed) "
          f"in {files_changed} files")
    return 0


def cmd_merge(args):
    rows = [json.loads(l) for l in open(args.audit, encoding="utf-8")]
    import glob as _glob
    files = sorted(f for pat in args.verdicts for f in _glob.glob(pat))
    if not files:
        print(f"no verdict files match {args.verdicts}", file=sys.stderr)
        return 1
    errors = []
    seen = {}
    for vf in files:
        for lineno, line in enumerate(open(vf, encoding="utf-8"), 1):
            line = line.strip()
            if not line:
                continue
            try:
                v = json.loads(line)
            except json.JSONDecodeError as exc:
                errors.append(f"{vf}:{lineno}: bad JSON ({exc})")
                continue
            i = v.get("id")
            if not isinstance(i, int) or i < 0 or i >= len(rows):
                errors.append(f"{vf}:{lineno}: id {i} out of range")
                continue
            cat = v.get("category")
            if cat not in CATEGORY:
                errors.append(f"{vf}:{lineno}: unknown category {cat!r}")
                continue
            if i in seen:
                errors.append(f"{vf}:{lineno}: duplicate id {i} (also in {seen[i]})")
                continue
            seen[i] = vf
            rows[i]["category"] = cat
            rows[i]["adr"] = v.get("adr")
            rows[i]["issue"] = v.get("issue")
            rows[i]["note"] = v.get("note")
            rows[i]["adjudicated"] = True
    for i, r in enumerate(rows):
        if not r.get("adjudicated"):
            continue
        if r.get("category") == "decision_record" and not r.get("adr"):
            errors.append(f"audit line {i}: decision_record without adr "
                          f"({r['file']}:{r['start']})")
        if r.get("category") == "excuse" and not r.get("issue"):
            errors.append(f"audit line {i}: excuse without issue "
                          f"({r['file']}:{r['start']})")
    if errors:
        for e in errors[:60]:
            print("ERROR " + e, file=sys.stderr)
        print(f"{len(errors)} merge errors; nothing written", file=sys.stderr)
        return 1
    trusted = ("separator or structural banner", "empty comment marker",
               "docstring: API documentation", "tool directive")
    missing = [i for i, r in enumerate(rows)
               if not r.get("adjudicated") and r["why"] not in trusted]
    for i in missing:
        # An unverified heuristic must never drive a deletion. Keep the guess for
        # reporting, but make the row ineligible for --categories selection.
        rows[i]["prior"] = rows[i]["category"]
        rows[i]["category"] = "unresolved"
    if missing:
        print(f"WARN {len(missing)} comments unadjudicated: prior moved to "
              f"'prior', category set to unresolved (first ids {missing[:10]})",
              file=sys.stderr)
    out = open(args.out, "w", encoding="utf-8")
    for r in rows:
        out.write(json.dumps(r) + "\n")
    tally = collections.Counter(r["category"] for r in rows)
    lines = collections.Counter()
    for r in rows:
        lines[r["category"]] += r["end"] - r["start"] + 1
    print(f"merged {len(rows)} comments from {len(files)} verdict files -> {args.out}")
    for cat in CATEGORY:
        if tally[cat]:
            print(f"  {cat:16} {tally[cat]:5} comments {lines[cat]:6} lines")
    return 0


def cmd_clusters(args):
    rows = [json.loads(l) for l in open(args.audit, encoding="utf-8")]
    os.makedirs(args.out_dir, exist_ok=True)
    groups = collections.defaultdict(list)
    for i, r in enumerate(rows):
        if r["category"] == "decision_record":
            groups[r.get("adr") or "unsorted"].append((i, r))
    for slug, items in sorted(groups.items()):
        path = os.path.join(args.out_dir, slug.replace("/", "_") + ".md")
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(f"# Decision records for {slug}\n\n")
            fh.write(f"{len(items)} comments, "
                     f"{sum(r['end'] - r['start'] + 1 for _, r in items)} lines.\n\n")
            for i, r in items:
                fh.write(f"## id={i} {r['file']}:{r['start']}-{r['end']}\n\n")
                fh.write("```\n" + r["text"] + "\n```\n\n")
        print(f"{len(items):4} {path}")
    return 0


def cmd_verify(args):
    """Prove the working tree differs from a ref only in comments."""
    targets = args.only if args.only else scope_files()
    bad = []
    for f in targets:
        old = subprocess.run(["git", "show", f"{args.ref}:{f}"], capture_output=True,
                             text=True)
        if old.returncode != 0:
            continue
        try:
            new = open(f, encoding="utf-8").read()
        except OSError:
            continue
        if _code_only(f, old.stdout) != _code_only(f, new):
            bad.append(f)
    if bad:
        print(f"FAIL: non-comment content changed since {args.ref} in:", file=sys.stderr)
        for f in bad:
            print(f"  {f}", file=sys.stderr)
        return 1
    print(f"OK: only comments changed since {args.ref} "
          f"(code identical, ignoring blank lines)")
    return 0


def _code_only(path, text):
    lang = lang_of(path)
    blanked = _blank_cpp(text) if lang == "cpp" else _blank_hash(text, lang_of(path))
    # Whitespace runs are collapsed: deleting an inline comment (a `/*self*/`
    # parameter name, say) necessarily changes the spacing around it.
    return [" ".join(l.split()) for l in blanked.split("\n") if l.strip()]


def _blank_cpp(text):
    """Replace every comment with spaces, keeping all other characters."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            q = c
            out.append(c)
            i += 1
            while i < n and text[i] != q:
                if text[i] == "\\":
                    out.append(text[i])
                    i += 1
                    if i < n:
                        out.append(text[i])
                        i += 1
                    continue
                out.append(text[i])
                i += 1
            if i < n:
                out.append(text[i])
                i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            i += 2
            out.append("  ")
            while i < n:
                if text[i] == "*" and i + 1 < n and text[i + 1] == "/":
                    i += 2
                    out.append("  ")
                    break
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            continue
        out.append(c)
        i += 1
    return "".join(out)


def _blank_hash(text, lang):
    """Replace `#` comments with spaces; docstrings stay (they are not comments)."""
    lines = text.split("\n")
    out = []
    in_doc = None
    for idx, line in enumerate(lines):
        scan = line
        if in_doc:
            if in_doc in scan:
                in_doc = None
            out.append(line)
            continue
        buf = []
        i, n = 0, len(scan)
        while i < n:
            c = scan[i]
            if lang == "py" and scan[i:i + 3] in ('"""', "'''"):
                q = scan[i:i + 3]
                j = scan.find(q, i + 3)
                if j == -1:
                    in_doc = q
                    buf.append(scan[i:])
                    i = n
                    break
                buf.append(scan[i:j + 3])
                i = j + 3
                continue
            if c in "\"'":
                q = c
                buf.append(c)
                i += 1
                while i < n and scan[i] != q:
                    if scan[i] == "\\":
                        buf.append(scan[i])
                        i += 1
                        if i < n:
                            buf.append(scan[i])
                            i += 1
                        continue
                    buf.append(scan[i])
                    i += 1
                if i < n:
                    buf.append(scan[i])
                    i += 1
                continue
            if c == "#":
                if idx == 0 and line.startswith("#!") and lang == "py":
                    buf.append(scan[i:])
                    i = n
                    break
                i = n
                continue
            buf.append(c)
            i += 1
        out.append("".join(buf))
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(prog="comment_policy.py", description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("count")
    p.add_argument("--json", action="store_true")
    p.set_defaults(fn=cmd_count)
    p = sub.add_parser("baseline")
    p.add_argument("--write", action="store_true")
    p.set_defaults(fn=cmd_baseline)
    p = sub.add_parser("check")
    p.set_defaults(fn=cmd_check)
    p = sub.add_parser("audit")
    p.add_argument("--out")
    p.set_defaults(fn=cmd_audit)
    p = sub.add_parser("delete")
    p.add_argument("--audit", required=True)
    p.add_argument("--categories", default="tautology,stale,dead_code")
    p.add_argument("--ids", help="file of audit-manifest line numbers to delete")
    p.set_defaults(fn=cmd_delete)
    p = sub.add_parser("merge")
    p.add_argument("--audit", required=True)
    p.add_argument("--verdicts", nargs="+", required=True)
    p.add_argument("--out", required=True)
    p.set_defaults(fn=cmd_merge)
    p = sub.add_parser("clusters")
    p.add_argument("--audit", required=True)
    p.add_argument("--out-dir", required=True)
    p.set_defaults(fn=cmd_clusters)
    p = sub.add_parser("verify-code-identical")
    p.add_argument("--ref", default="HEAD")
    p.add_argument("--only", nargs="*")
    p.set_defaults(fn=cmd_verify)
    args = ap.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
