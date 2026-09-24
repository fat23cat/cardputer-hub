#!/usr/bin/env bash

set -euo pipefail

CBM="${CBM_BIN:-codebase-memory-mcp}"

SCOPE="staged"
BASE_BRANCH="main"
OUTPUT=""
REINDEX=true

# Safety limits only. Symbols are NOT ranked.
MAX_SYMBOLS=500
MAX_DIFF_LINES=60000
TRACE_DEPTH=2
MAX_SNIPPET_LINES=400
MAX_TRACE_LINES=250
MAX_DIAGNOSTIC_OUTPUT_BYTES=3000

usage() {
  cat <<'EOF'
Usage:
  scripts/chatgpt-review.sh [options]

Options:
  --scope <all|unstaged|staged|branch>
      Changes to review.
      Default: staged

  --base <branch>
      Base branch for branch scope.
      Default: main

  --output <file>
      Output Markdown file.
      Default: .chatgpt/review.md

  --max-symbols <n>
      Safety limit for deep-inspected production symbols.
      Symbols are processed in file/source order, not ranked.
      Default: 500

  --max-diff-lines <n>
      Maximum number of diff lines included in the bundle.
      Default: 60000

  --trace-depth <1-5>
      Call graph traversal depth.
      Default: 2

  --no-reindex
      Skip explicit Codebase Memory reindex.

Examples:
  scripts/chatgpt-review.sh

  scripts/chatgpt-review.sh --scope staged

  scripts/chatgpt-review.sh --scope branch --base main

  scripts/chatgpt-review.sh --scope staged --max-symbols 80

  scripts/chatgpt-review.sh --scope all
EOF
}

is_positive_integer() {
  local value="$1"

  [[ "$value" =~ ^[0-9]+$ ]] || return 1
  (( 10#$value > 0 ))
}

is_code_file() {
  case "$1" in
    *.c|*.cc|*.cpp|*.cxx|*.h|*.hh|*.hpp|*.hxx|\
    *.py|*.js|*.jsx|*.ts|*.tsx|\
    *.java|*.kt|*.kts|\
    *.rs|*.go|*.cs|*.php|*.rb|*.swift|\
    *.sh|*.bash)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

is_test_file() {
  case "$1" in
    test/*|tests/*|test_python/*|*/test/*|*/tests/*)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --scope)
      SCOPE="$2"
      shift 2
      ;;

    --base)
      BASE_BRANCH="$2"
      shift 2
      ;;

    --output)
      OUTPUT="$2"
      shift 2
      ;;

    --max-symbols)
      MAX_SYMBOLS="$2"
      shift 2
      ;;

    --max-diff-lines)
      MAX_DIFF_LINES="$2"
      shift 2
      ;;

    --trace-depth)
      TRACE_DEPTH="$2"
      shift 2
      ;;

    --no-reindex)
      REINDEX=false
      shift
      ;;

    -h|--help)
      usage
      exit 0
      ;;

    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

case "$SCOPE" in
  all|unstaged|staged|branch)
    ;;
  *)
    echo "Invalid scope: $SCOPE" >&2
    exit 1
    ;;
esac

if ! is_positive_integer "$MAX_SYMBOLS"; then
  echo "--max-symbols must be a positive integer" >&2
  exit 1
fi

if ! is_positive_integer "$MAX_DIFF_LINES"; then
  echo "--max-diff-lines must be a positive integer" >&2
  exit 1
fi

if ! is_positive_integer "$TRACE_DEPTH" ||
   (( 10#$TRACE_DEPTH < 1 || 10#$TRACE_DEPTH > 5 )); then
  echo "--trace-depth must be between 1 and 5" >&2
  exit 1
fi

for cmd in git python3 "$CBM"; do
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "Required command not found: $cmd" >&2
    exit 1
  fi
done

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

REPO_NAME="$(basename "$ROOT")"

if [[ -z "$OUTPUT" ]]; then
  OUTPUT="$ROOT/.chatgpt/review.md"
elif [[ "$OUTPUT" != /* ]]; then
  OUTPUT="$ROOT/$OUTPUT"
fi

mkdir -p "$(dirname "$OUTPUT")"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo
echo "Repository: $REPO_NAME"
echo "Review scope: $SCOPE"
echo

# ------------------------------------------------------------
# Git review scope
# ------------------------------------------------------------

case "$SCOPE" in
  unstaged)
    DIFF_CMD=(git diff --find-renames)
    STAT_CMD=(git diff --stat)
    NAMES_CMD=(git diff --name-status)
    FILES_CMD=(git diff --name-only)
    ;;

  staged)
    DIFF_CMD=(git diff --cached --find-renames)
    STAT_CMD=(git diff --cached --stat)
    NAMES_CMD=(git diff --cached --name-status)
    FILES_CMD=(git diff --cached --name-only)
    ;;

  all)
    DIFF_CMD=(git diff HEAD --find-renames)
    STAT_CMD=(git diff HEAD --stat)
    NAMES_CMD=(git diff HEAD --name-status)
    FILES_CMD=(git diff HEAD --name-only)
    ;;

  branch)
    DIFF_CMD=(git diff "$BASE_BRANCH"...HEAD --find-renames)
    STAT_CMD=(git diff "$BASE_BRANCH"...HEAD --stat)
    NAMES_CMD=(git diff "$BASE_BRANCH"...HEAD --name-status)
    FILES_CMD=(git diff "$BASE_BRANCH"...HEAD --name-only)
    ;;
esac

"${DIFF_CMD[@]}" >"$TMP/diff.patch"
"${STAT_CMD[@]}" >"$TMP/stat.txt"
"${NAMES_CMD[@]}" >"$TMP/names.txt"
"${FILES_CMD[@]}" >"$TMP/files.txt"

# ------------------------------------------------------------
# Untracked files
# ------------------------------------------------------------

if [[ "$SCOPE" == "all" || "$SCOPE" == "unstaged" ]]; then
  git ls-files --others --exclude-standard \
    | grep -v '^\.chatgpt/' \
    >"$TMP/untracked.txt" || true

  while IFS= read -r file; do
    [[ -z "$file" ]] && continue

    echo "$file" >>"$TMP/files.txt"
    printf '??\t%s\n' "$file" >>"$TMP/names.txt"

    {
      echo
      echo "# Untracked file: $file"
      git diff --no-index -- /dev/null "$file" || true
    } >>"$TMP/diff.patch"

  done <"$TMP/untracked.txt"
else
  : >"$TMP/untracked.txt"
fi

grep -v '^\.chatgpt/' "$TMP/files.txt" >"$TMP/files.filtered" || true
mv "$TMP/files.filtered" "$TMP/files.txt"

sort -u "$TMP/files.txt" -o "$TMP/files.txt"

CHANGED_FILE_COUNT="$(wc -l <"$TMP/files.txt" | tr -d ' ')"

# ------------------------------------------------------------
# Ensure Git diff and current CBM source describe the same code
# ------------------------------------------------------------

SCOPE_SOURCE_STATUS="ok"

if [[ "$SCOPE" == "staged" ]]; then
  git diff --name-only | sort -u >"$TMP/unstaged-files.txt"

  comm -12 \
    <(sort -u "$TMP/files.txt") \
    "$TMP/unstaged-files.txt" \
    >"$TMP/scope-overlap.txt"

  if [[ -s "$TMP/scope-overlap.txt" ]]; then
    echo >&2
    echo "ERROR: staged review files have additional unstaged changes:" >&2
    echo >&2
    cat "$TMP/scope-overlap.txt" >&2
    echo >&2
    echo "Stage the latest changes or revert the unstaged edits first." >&2
    exit 1
  fi
fi

if [[ "$SCOPE" == "branch" ]]; then
  {
    git diff --name-only
    git diff --cached --name-only
  } | sort -u >"$TMP/working-tree-changes.txt"

  comm -12 \
    <(sort -u "$TMP/files.txt") \
    "$TMP/working-tree-changes.txt" \
    >"$TMP/scope-overlap.txt"

  if [[ -s "$TMP/scope-overlap.txt" ]]; then
    echo >&2
    echo "ERROR: branch-review files also contain uncommitted changes:" >&2
    echo >&2
    cat "$TMP/scope-overlap.txt" >&2
    echo >&2
    echo "Commit, stash, or revert them before branch review." >&2
    exit 1
  fi
fi

# ------------------------------------------------------------
# Classify files
#
# All changed code remains in Git diff.
#
# Deep inspection is intentionally limited to production/tooling
# code. Tests stay visible in the authoritative diff but do not
# consume deep-inspection slots with helpers such as character(),
# capture(), test fixture utilities, etc.
# ------------------------------------------------------------

: >"$TMP/code-files.txt"
: >"$TMP/deep-files.txt"
: >"$TMP/test-code-files.txt"

while IFS= read -r file; do
  [[ -z "$file" ]] && continue

  if ! is_code_file "$file"; then
    continue
  fi

  echo "$file" >>"$TMP/code-files.txt"

  if is_test_file "$file"; then
    echo "$file" >>"$TMP/test-code-files.txt"
    continue
  fi

  # Do not recursively deep-inspect the review tool itself.
  if [[ "$file" == "scripts/chatgpt-review.sh" ]]; then
    continue
  fi

  echo "$file" >>"$TMP/deep-files.txt"

done <"$TMP/files.txt"

sort -u "$TMP/code-files.txt" -o "$TMP/code-files.txt"
sort -u "$TMP/deep-files.txt" -o "$TMP/deep-files.txt"
sort -u "$TMP/test-code-files.txt" -o "$TMP/test-code-files.txt"

CODE_FILE_COUNT="$(wc -l <"$TMP/code-files.txt" | tr -d ' ')"
DEEP_FILE_COUNT="$(wc -l <"$TMP/deep-files.txt" | tr -d ' ')"
TEST_CODE_FILE_COUNT="$(wc -l <"$TMP/test-code-files.txt" | tr -d ' ')"

# ------------------------------------------------------------
# Refresh Codebase Memory
# ------------------------------------------------------------

INDEX_STATUS="not-requested"

if [[ "$REINDEX" == true ]]; then
  echo "Refreshing Codebase Memory index..."

  if "$CBM" cli index_repository \
      --repo-path "$ROOT" \
      >"$TMP/index.txt" 2>"$TMP/index.err"; then

    INDEX_STATUS="ok"
  else
    INDEX_STATUS="failed"

    echo >&2
    echo "ERROR: Codebase Memory reindex failed." >&2

    if [[ -s "$TMP/index.err" ]]; then
      cat "$TMP/index.err" >&2
    fi

    exit 1
  fi
else
  INDEX_STATUS="skipped"
fi

# ------------------------------------------------------------
# Resolve CBM project
# ------------------------------------------------------------

echo "Resolving Codebase Memory project..."

"$CBM" cli --json list_projects >"$TMP/projects.json"

PROJECT="$(
python3 - "$ROOT" "$TMP/projects.json" <<'PY'
import json
import os
import sys

target = os.path.realpath(sys.argv[1])

with open(sys.argv[2], encoding="utf-8") as f:
    data = json.load(f)


def walk(value):
    if isinstance(value, dict):
        if "root_path" in value and "name" in value:
            yield value

        for child in value.values():
            yield from walk(child)

    elif isinstance(value, list):
        for child in value:
            yield from walk(child)


for project in walk(data):
    root = project.get("root_path")

    if root and os.path.realpath(root) == target:
        print(project["name"])
        raise SystemExit(0)

raise SystemExit(1)
PY
)" || true

if [[ -z "$PROJECT" ]]; then
  echo "Could not find Codebase Memory project for repository." >&2
  exit 1
fi

echo "Project: $PROJECT"

# ------------------------------------------------------------
# Architecture
# ------------------------------------------------------------

echo "Reading architecture..."

if ! "$CBM" cli get_architecture \
    --project "$PROJECT" \
    >"$TMP/architecture.txt" 2>"$TMP/architecture.err"; then

  {
    echo "get_architecture failed"
    cat "$TMP/architecture.err"
  } >"$TMP/architecture.txt"
fi

# ------------------------------------------------------------
# Change impact
# ------------------------------------------------------------

echo "Analyzing change impact..."

if [[ "$SCOPE" == "branch" ]]; then
  "$CBM" cli detect_changes \
    --project "$PROJECT" \
    --scope branch \
    --base-branch "$BASE_BRANCH" \
    --depth 3 \
    >"$TMP/impact.txt" 2>"$TMP/impact.err" \
    || echo "detect_changes failed" >"$TMP/impact.txt"
else
  "$CBM" cli detect_changes \
    --project "$PROJECT" \
    --scope "$SCOPE" \
    --depth 3 \
    >"$TMP/impact.txt" 2>"$TMP/impact.err" \
    || echo "detect_changes failed" >"$TMP/impact.txt"
fi

CBM_CHANGED_COUNT="$(
  sed -n 's/^changed_files:[[:space:]]*//p' "$TMP/impact.txt" \
    | head -n 1 \
    | tr -d '\r' \
    || true
)"

IMPACT_SCOPE_STATUS="ok"

if [[ "$CBM_CHANGED_COUNT" =~ ^[0-9]+$ ]] &&
   (( CBM_CHANGED_COUNT != CHANGED_FILE_COUNT )); then

  IMPACT_SCOPE_STATUS="differs-from-git"

  echo >&2
  echo "WARNING: CBM detect_changes scope differs from Git review scope." >&2
  echo "Git changed files: $CHANGED_FILE_COUNT" >&2
  echo "CBM changed files: $CBM_CHANGED_COUNT" >&2
fi

# ------------------------------------------------------------
# Discover ALL Function/Method symbols in changed deep files.
#
# No ranking.
# No scoring.
# No guessing.
#
# Exact file pattern only.
# ------------------------------------------------------------

mkdir -p "$TMP/search"

: >"$TMP/search-diagnostics.txt"

SEARCH_INDEX=0

while IFS= read -r file; do
  [[ -z "$file" ]] && continue
  [[ ! -f "$file" ]] && continue

  for label in Function Method; do
    SEARCH_INDEX=$((SEARCH_INDEX + 1))

    RESULT="$TMP/search/$SEARCH_INDEX.json"
    ERROR="$TMP/search/$SEARCH_INDEX.err"

    echo "Resolving $label symbols: $file"

    if ! "$CBM" cli search_graph \
        --project "$PROJECT" \
        --file-pattern "$file" \
        --label "$label" \
        --limit 100 \
        --format json \
        >"$RESULT" 2>"$ERROR"; then

      {
        echo "SEARCH FAILED"
        echo "file=$file"
        echo "label=$label"

        if [[ -s "$ERROR" ]]; then
          echo
          echo "stderr:"
          cat "$ERROR"
        fi

        if [[ -s "$RESULT" ]]; then
          echo
          echo "stdout:"
          head -c "$MAX_DIAGNOSTIC_OUTPUT_BYTES" "$RESULT"
          echo
        fi

        echo
        echo "----------------------------------------"
      } >>"$TMP/search-diagnostics.txt"

      rm -f "$RESULT"
    fi
  done

done <"$TMP/deep-files.txt"

# ------------------------------------------------------------
# Parse CBM compact grouped JSON.
#
# Output:
#
# qualified_name
# name
# file
# label
# start_line
# end_line
# inbound
# outbound
#
# Sorted deterministically by:
#
#   file → source line → symbol name
# ------------------------------------------------------------

python3 \
  - "$TMP/search" "$TMP/deep-files.txt" \
  >"$TMP/symbols-all.tsv" <<'PY'

import json
import pathlib
import re
import sys

search_dir = pathlib.Path(sys.argv[1])
deep_files_path = pathlib.Path(sys.argv[2])

deep_files = {
    line.strip().replace("\\", "/").lstrip("./")
    for line in deep_files_path.read_text(encoding="utf-8").splitlines()
    if line.strip()
}

symbols = {}


def normalize_path(value):
    if not isinstance(value, str):
        return ""

    return value.replace("\\", "/").lstrip("./")


def resolve_file(value):
    actual = normalize_path(value)

    if actual in deep_files:
        return actual

    for expected in deep_files:
        if actual.endswith("/" + expected):
            return expected

    return None


def safe_int(value):
    try:
        return int(value or 0)
    except Exception:
        return 0


def parse_lines(value):
    if not isinstance(value, str):
        return 0, 0

    match = re.fullmatch(r"\s*(\d+)(?:-(\d+))?\s*", value)

    if not match:
        return 0, 0

    start = int(match.group(1))
    end = int(match.group(2) or start)

    return start, end


def add_symbol(
    qn,
    name,
    file_path,
    label,
    lines,
    inbound,
    outbound,
):
    resolved_file = resolve_file(file_path)

    if not resolved_file:
        return

    if not qn or not name:
        return

    start, end = parse_lines(lines)

    item = {
        "qualified_name": str(qn),
        "name": str(name),
        "file": resolved_file,
        "label": str(label or ""),
        "start": start,
        "end": end,
        "inbound": safe_int(inbound),
        "outbound": safe_int(outbound),
    }

    symbols[item["qualified_name"]] = item


for path in search_dir.glob("*.json"):
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        continue

    groups = data.get("groups")

    if not isinstance(groups, list):
        continue

    cols = data.get("cols")

    if not isinstance(cols, list):
        cols = ["name", "label", "lines", "in", "out"]

    indexes = {
        str(name): index
        for index, name in enumerate(cols)
    }

    name_index = indexes.get("name", 0)
    label_index = indexes.get("label", 1)
    lines_index = indexes.get("lines", 2)
    inbound_index = indexes.get("in", 3)
    outbound_index = indexes.get("out", 4)

    for group in groups:
        if not isinstance(group, dict):
            continue

        prefix = group.get("qn_prefix")
        file_path = group.get("file")
        rows = group.get("rows")

        if not isinstance(prefix, str):
            continue

        if not isinstance(rows, list):
            continue

        for row in rows:
            if not isinstance(row, list):
                continue

            if name_index >= len(row):
                continue

            name = row[name_index]

            if not isinstance(name, str) or not name:
                continue

            label = (
                row[label_index]
                if label_index < len(row)
                else ""
            )

            lines = (
                row[lines_index]
                if lines_index < len(row)
                else ""
            )

            inbound = (
                row[inbound_index]
                if inbound_index < len(row)
                else 0
            )

            outbound = (
                row[outbound_index]
                if outbound_index < len(row)
                else 0
            )

            add_symbol(
                qn=f"{prefix}.{name}",
                name=name,
                file_path=file_path,
                label=label,
                lines=lines,
                inbound=inbound,
                outbound=outbound,
            )


ordered = sorted(
    symbols.values(),
    key=lambda item: (
        item["file"],
        item["start"],
        item["name"],
    ),
)

for item in ordered:
    values = [
        item["qualified_name"],
        item["name"],
        item["file"],
        item["label"],
        str(item["start"]),
        str(item["end"]),
        str(item["inbound"]),
        str(item["outbound"]),
    ]

    values = [
        value.replace("\t", " ").replace("\n", " ")
        for value in values
    ]

    print("\t".join(values))
PY

SYMBOL_CANDIDATE_COUNT="$(
  wc -l <"$TMP/symbols-all.tsv" | tr -d ' '
)"

head -n "$MAX_SYMBOLS" \
  "$TMP/symbols-all.tsv" \
  >"$TMP/symbols-selected.tsv"

SYMBOL_COUNT="$(
  wc -l <"$TMP/symbols-selected.tsv" | tr -d ' '
)"

DEEP_CONTEXT_STATUS="ok"

if (( DEEP_FILE_COUNT > 0 && SYMBOL_CANDIDATE_COUNT == 0 )); then
  DEEP_CONTEXT_STATUS="incomplete"
elif (( SYMBOL_CANDIDATE_COUNT > MAX_SYMBOLS )); then
  DEEP_CONTEXT_STATUS="truncated"
fi

# ------------------------------------------------------------
# Symbol index
#
# Useful even if the safety cap truncates deep inspection.
# ------------------------------------------------------------

python3 - "$TMP/symbols-all.tsv" >"$TMP/symbol-index.md" <<'PY'
import sys

path = sys.argv[1]

current_file = None

with open(path, encoding="utf-8") as f:
    for line in f:
        parts = line.rstrip("\n").split("\t")

        if len(parts) < 8:
            continue

        qn, name, file_path, label, start, end, inbound, outbound = parts[:8]

        if file_path != current_file:
            if current_file is not None:
                print()

            print(f"### `{file_path}`")
            print()
            current_file = file_path

        location = start

        if end and end != start:
            location = f"{start}-{end}"

        print(
            f"- `{name}` — {label}, lines {location}, "
            f"in={inbound}, out={outbound}"
        )
PY

# ------------------------------------------------------------
# Deep inspect selected production symbols
# ------------------------------------------------------------

: >"$TMP/deep-context.md"

SYMBOL_NUMBER=0

while IFS=$'\t' read -r \
    QN NAME FILE LABEL START END INBOUND OUTBOUND; do

  [[ -z "$QN" ]] && continue

  SYMBOL_NUMBER=$((SYMBOL_NUMBER + 1))

  echo "Inspecting symbol $SYMBOL_NUMBER/$SYMBOL_COUNT: $NAME"

  SNIPPET="$TMP/snippet-$SYMBOL_NUMBER.txt"
  SNIPPET_ERROR="$TMP/snippet-$SYMBOL_NUMBER.err"

  TRACE="$TMP/trace-$SYMBOL_NUMBER.txt"
  TRACE_ERROR="$TMP/trace-$SYMBOL_NUMBER.err"

  if ! "$CBM" cli get_code_snippet \
      --project "$PROJECT" \
      --qualified-name "$QN" \
      --include-neighbors true \
      >"$SNIPPET" 2>"$SNIPPET_ERROR"; then

    {
      echo "get_code_snippet failed for $QN"

      if [[ -s "$SNIPPET_ERROR" ]]; then
        cat "$SNIPPET_ERROR"
      fi
    } >"$SNIPPET"
  fi

  if ! "$CBM" cli trace_path \
      --project "$PROJECT" \
      --function-name "$QN" \
      --direction both \
      --depth "$TRACE_DEPTH" \
      --limit 50 \
      >"$TRACE" 2>"$TRACE_ERROR"; then

    {
      echo "trace_path unavailable for $QN"

      if [[ -s "$TRACE_ERROR" ]]; then
        cat "$TRACE_ERROR"
      fi
    } >"$TRACE"
  fi

  {
    echo
    echo "## Symbol $SYMBOL_NUMBER: \`$NAME\`"
    echo
    echo "- Qualified name: \`$QN\`"
    echo "- File: \`$FILE\`"
    echo "- Label: \`$LABEL\`"
    echo "- Source lines: \`$START-$END\`"
    echo "- Inbound edges: \`$INBOUND\`"
    echo "- Outbound edges: \`$OUTBOUND\`"
    echo

    echo "### Source"
    echo
    echo '```text'

    head -n "$MAX_SNIPPET_LINES" "$SNIPPET"

    SNIPPET_LINES="$(wc -l <"$SNIPPET" | tr -d ' ')"

    if (( SNIPPET_LINES > MAX_SNIPPET_LINES )); then
      echo
      echo "... truncated after $MAX_SNIPPET_LINES lines ..."
    fi

    echo '```'
    echo

    echo "### Call graph"
    echo
    echo '```text'

    head -n "$MAX_TRACE_LINES" "$TRACE"

    TRACE_LINES="$(wc -l <"$TRACE" | tr -d ' ')"

    if (( TRACE_LINES > MAX_TRACE_LINES )); then
      echo
      echo "... truncated after $MAX_TRACE_LINES lines ..."
    fi

    echo '```'

  } >>"$TMP/deep-context.md"

done <"$TMP/symbols-selected.tsv"

# ------------------------------------------------------------
# Limit Git diff
# ------------------------------------------------------------

DIFF_LINES="$(wc -l <"$TMP/diff.patch" | tr -d ' ')"

head -n "$MAX_DIFF_LINES" \
  "$TMP/diff.patch" \
  >"$TMP/diff-limited.patch"

if (( DIFF_LINES > MAX_DIFF_LINES )); then
  {
    echo
    echo
    echo "# DIFF TRUNCATED"
    echo "# Original: $DIFF_LINES lines"
    echo "# Included: $MAX_DIFF_LINES lines"
  } >>"$TMP/diff-limited.patch"
fi

# ------------------------------------------------------------
# Metadata
# ------------------------------------------------------------

TIMESTAMP="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
BRANCH="$(git branch --show-current)"
HEAD_SHA="$(git rev-parse HEAD)"
CBM_VERSION="$("$CBM" --version 2>&1 | head -n 1 || true)"

# ------------------------------------------------------------
# Review bundle
# ------------------------------------------------------------

cat >"$OUTPUT" <<EOF
# ChatGPT Code Review Bundle

This bundle represents the actual repository state at generation time.

Use it to independently compare the implementation with the requirements
and architecture discussed in the conversation.

Do not assume the coding agent's implementation report is correct.

## Review protocol

Please independently verify:

- whether all requested requirements were implemented;
- whether implementation behavior matches the agreed design;
- whether existing architecture and abstractions were reused correctly;
- whether unnecessary parallel abstractions were introduced;
- whether callers/dependencies are affected unexpectedly;
- whether important error handling or edge cases are missing;
- whether tests cover the changed behavior.

Clearly distinguish:

1. confirmed findings visible from source;
2. plausible risks requiring runtime verification;
3. claims that cannot be proven from static analysis alone.

---

# Snapshot

Repository: \`$REPO_NAME\`

Generated UTC: \`$TIMESTAMP\`

Branch: \`$BRANCH\`

HEAD: \`$HEAD_SHA\`

Review scope: \`$SCOPE\`

Base branch: \`$BASE_BRANCH\`

Git changed files: \`$CHANGED_FILE_COUNT\`

Changed code files: \`$CODE_FILE_COUNT\`

Production/tooling files considered for deep inspection: \`$DEEP_FILE_COUNT\`

Changed test-code files kept in Git diff only: \`$TEST_CODE_FILE_COUNT\`

Codebase Memory project: \`$PROJECT\`

Codebase Memory version:

\`\`\`text
$CBM_VERSION
\`\`\`

Codebase Memory reindex: \`$INDEX_STATUS\`

Scope/source consistency: \`$SCOPE_SOURCE_STATUS\`

Codebase Memory impact scope: \`$IMPACT_SCOPE_STATUS\`

Production symbol candidates: \`$SYMBOL_CANDIDATE_COUNT\`

Deep-inspected symbols: \`$SYMBOL_COUNT\`

Deep Symbol Context status: \`$DEEP_CONTEXT_STATUS\`

---

# Scope Notes

The Git-selected change set is authoritative for this review.

Codebase Memory \`detect_changes\` is supplemental impact information.

Deep inspection intentionally uses a simple deterministic policy:

- discover all Function/Method symbols from changed non-test code files;
- order them by file and source line;
- inspect them all up to the safety limit;
- do not rank or score symbol importance.

Changed tests remain fully visible in the Git diff but are not deep-inspected by
default. Production call graphs may still expose affected tests as callers.

---

# Git status

\`\`\`text
$(git status --short)
\`\`\`

# Changed files in review scope

\`\`\`text
$(cat "$TMP/names.txt")
\`\`\`

# Production/tooling files used for deep inspection

\`\`\`text
$(cat "$TMP/deep-files.txt")
\`\`\`

# Test files kept in Git diff only

\`\`\`text
$(cat "$TMP/test-code-files.txt")
\`\`\`

# Diff statistics

\`\`\`text
$(cat "$TMP/stat.txt")
\`\`\`

---

# Codebase Memory — Change Impact

This section is supplemental. Git remains the authoritative review scope.

\`\`\`text
$(cat "$TMP/impact.txt")
\`\`\`

---

# Codebase Memory — Architecture

\`\`\`text
$(cat "$TMP/architecture.txt")
\`\`\`

---

# Production Symbol Index

All Function/Method symbols discovered in changed production/tooling files are
listed here, including symbols beyond the deep-inspection safety limit.

$(cat "$TMP/symbol-index.md")

---

# Deep Symbol Context

Symbols are processed deterministically in file/source order.

No importance ranking is applied.

Candidate symbols: **$SYMBOL_CANDIDATE_COUNT**

Deep-inspected symbols: **$SYMBOL_COUNT**

Safety limit: **$MAX_SYMBOLS**

Status: **$DEEP_CONTEXT_STATUS**

$(cat "$TMP/deep-context.md")

---

# Codebase Memory — Symbol Discovery Diagnostics

Unexpected search/parser failures only.

\`\`\`text
$(cat "$TMP/search-diagnostics.txt")
\`\`\`

---

# Actual Git Diff

This section comes directly from Git and is the authoritative implementation
change set for this review.

Total diff lines: \`$DIFF_LINES\`

Maximum included diff lines: \`$MAX_DIFF_LINES\`

\`\`\`diff
$(cat "$TMP/diff-limited.patch")
\`\`\`

---

# End of Review Bundle
EOF

# ------------------------------------------------------------
# Summary
# ------------------------------------------------------------

echo
echo "Review bundle generated:"
echo
echo "  $OUTPUT"
echo
echo
echo "Git changed files: $CHANGED_FILE_COUNT"
echo "Changed code files: $CODE_FILE_COUNT"
echo "Deep files: $DEEP_FILE_COUNT"
echo "Changed test-code files: $TEST_CODE_FILE_COUNT"
echo "Production symbol candidates: $SYMBOL_CANDIDATE_COUNT"
echo "Deep-inspected symbols: $SYMBOL_COUNT"
echo "Deep context: $DEEP_CONTEXT_STATUS"
echo "CBM impact scope: $IMPACT_SCOPE_STATUS"
echo "Diff: $DIFF_LINES lines"

if [[ "$SYMBOL_CANDIDATE_COUNT" -gt "$MAX_SYMBOLS" ]]; then
  echo
  echo "NOTE: deep symbol context was truncated by the safety limit."
  echo "The complete symbol index is still present in review.md."
fi

if [[ "$IMPACT_SCOPE_STATUS" != "ok" ]]; then
  echo
  echo "NOTE: CBM detect_changes included a different file set."
  echo "The Git-selected review scope remains authoritative."
fi

if [[ "$DEEP_CONTEXT_STATUS" == "incomplete" ]]; then
  echo
  echo "WARNING: no deep Codebase Memory symbols were resolved."
  echo "Inspect Symbol Discovery Diagnostics in review.md."
fi

echo
echo "Upload review.md to ChatGPT and ask for an independent"
echo "comparison against the implementation plan."
