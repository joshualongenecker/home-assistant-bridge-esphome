#!/usr/bin/env python3
"""Build adjudication worklists from a comment_policy.py audit.

Groups comments into slices of roughly `--per-slice` comments, keeping each file
whole (a file larger than that is split into `--chunk`-line ranges) so an
adjudicating agent reads each source file once.

Comments whose audit `why` is one of the trusted priors are skipped: banners,
empty markers, docstrings and tool directives need no judgment.

Usage:
  scripts/comment_policy.py audit --out /tmp/comments.jsonl
  scripts/comment_slice.py --audit /tmp/comments.jsonl --out-dir /tmp/slices
"""

import argparse
import collections
import json
import os

TRUSTED = (
    "separator or structural banner",
    "empty comment marker",
    "docstring: API documentation",
    "tool directive",
)


def main():
    ap = argparse.ArgumentParser(prog="comment_slice.py")
    ap.add_argument("--audit", required=True)
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--per-slice", type=int, default=180)
    ap.add_argument("--chunk", type=int, default=600)
    args = ap.parse_args()

    rows = [json.loads(l) for l in open(args.audit, encoding="utf-8")]
    by_file = collections.defaultdict(list)
    for i, r in enumerate(rows):
        if r["why"] in TRUSTED or r["category"] == "doc":
            continue
        by_file[r["file"]].append(i)

    units = []
    for f, idxs in sorted(by_file.items()):
        if len(idxs) > args.per_slice:
            groups = collections.defaultdict(list)
            for i in idxs:
                groups[rows[i]["start"] // args.chunk].append(i)
            for k in sorted(groups):
                units.append((f, groups[k]))
        else:
            units.append((f, idxs))

    os.makedirs(args.out_dir, exist_ok=True)
    slices, cur, n = [], [], 0
    for f, idxs in units:
        if n and n + len(idxs) > args.per_slice:
            slices.append(cur)
            cur, n = [], 0
        cur.append((f, idxs))
        n += len(idxs)
    if cur:
        slices.append(cur)

    total = 0
    for si, s in enumerate(slices):
        path = os.path.join(args.out_dir, f"s{si:02d}.txt")
        with open(path, "w", encoding="utf-8") as fh:
            for f, idxs in s:
                fh.write(f"\n## FILE {f}\n")
                for i in idxs:
                    r = rows[i]
                    text = " \\n ".join(r["text"].split("\n")).strip()
                    fh.write(f"id={i} {r['start']}-{r['end']} "
                             f"[{r['category']}/{r['why'][:22]}] {text}\n")
                    total += 1
    skipped = len(rows) - total
    print(f"{len(slices)} slices, {total} comments to adjudicate, "
          f"{skipped} skipped as trusted priors -> {args.out_dir}")


if __name__ == "__main__":
    main()
