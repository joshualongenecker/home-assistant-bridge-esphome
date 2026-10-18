#!/usr/bin/env bash
# Link the repo's factory skills into the agent skill directories.
# Idempotent: re-run after pulling a branch with new/changed skills.
#
#   OMP_SKILLS_DIR=/path CLAUDE_SKILLS_DIR=/path bash scripts/factory/setup_skills.sh
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

link_into() {
  local skill_dir="$1"
  mkdir -p "$skill_dir"
  for skill in skills/*/; do
    local name link target
    name="$(basename "$skill")"
    link="$skill_dir/$name"
    if [ -e "$link" ] && [ ! -L "$link" ]; then
      echo "skip: $link exists and is not a symlink (refusing to overwrite)" >&2
      continue
    fi
    target="$(realpath --relative-to="$skill_dir" "skills/$name")"
    ln -sfn "$target" "$link"
    echo "linked: $link -> skills/$name"
  done
}

link_into "${OMP_SKILLS_DIR:-.omp/skills}"
link_into "${CLAUDE_SKILLS_DIR:-.claude/skills}"

echo "Done. New skills appear in the next agent session."
