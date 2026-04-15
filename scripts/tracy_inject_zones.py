#!/usr/bin/env python3
"""
tracy_inject_zones.py  –  Build-time ZoneScoped injector for TracyLite.

Parses C/C++ source files and injects a `ZoneScoped;` macro at the top of
every qualifying function body.  The result is written to an output file
(or stdout) so the original source is never mutated.

Designed to be called from CMake as a pre-compile custom command:

    python tracy_inject_zones.py [OPTIONS] <input.cpp> -o <output.cpp>

The injected macro expands to a zero-overhead compile-time source location
(static constexpr), avoiding all the runtime costs of /Gh + _penter:
  - No SymFromAddr                       - No mutex
  - No hash-map lookup                   - No inlining prevention
  - Full file/line information preserved

Heuristic parser – NOT a full C++ parser.  Works well for typical code;
may skip or misplace in exotic corner cases (template meta-programming,
deeply nested lambdas, etc.).  Always review the diff for new code bases.

Usage:
    # Single file
    python tracy_inject_zones.py src/Foo.cpp -o build/gen/Foo.cpp

    # Dry-run (show what would change)
    python tracy_inject_zones.py src/Foo.cpp --dry-run

    # Exclude short functions (< N lines)
    python tracy_inject_zones.py src/Foo.cpp -o out.cpp --min-lines 3

    # Skip specific function names
    python tracy_inject_zones.py src/Foo.cpp -o out.cpp --skip main,operator==
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from dataclasses import dataclass, field
from typing import List, Optional, Set

# ── Configuration ─────────────────────────────────────────────────────────────

ZONE_MACRO = "ZoneScoped;"

# Patterns that indicate a line already has Tracy instrumentation.
ALREADY_INSTRUMENTED = re.compile(
    r"\b(ZoneScoped|ZoneScopedN|ZoneScopedC|ZoneScopedNC"
    r"|ZoneNamed|ZoneNamedN|ZoneNamedC|ZoneNamedNC"
    r"|ZoneNamedLite|ZoneNamedLiteN|ZoneNamedLiteC|ZoneNamedLiteNC"
    r"|TRACYLITE_ZONE)\b"
)

# Lines that look like preprocessor directives, comments-only, or blank.
SKIP_LINE = re.compile(r"^\s*(#|//|/\*|\*|$)")

# Heuristic: a line that opens a function body.
# Matches:  `ReturnType FuncName( args ) {`   or   `) {`  or  `) const {`
# Also handles constructors with initializer lists: `) : member(x) {`
FUNC_HEAD = re.compile(
    r"""
    ^[^;#]*                      # no semicolons or preprocessor
    \)                           # closing paren of parameter list
    \s*
    (?:const\s*)?                # optional const
    (?:noexcept(?:\(.*?\))?\s*)? # optional noexcept
    (?:override\s*)?             # optional override
    (?:final\s*)?                # optional final
    (?:->.*?)?                   # optional trailing return type
    \s*\{                        # opening brace
    """,
    re.VERBOSE,
)

# Things that are NOT function definitions even if they match FUNC_HEAD.
NOT_FUNCTION = re.compile(
    r"^\s*(?:"
    r"if\b|else\b|for\b|while\b|do\b|switch\b|catch\b|try\b"
    r"|return\b|throw\b|case\b|default\s*:"
    r"|namespace\b|class\b|struct\b|enum\b|union\b"
    r"|using\b|typedef\b|static_assert\b"
    r"|#"
    r")"
)

# Lambda: `[...](...) { ... }`  or  `[...]{ ... }`
LAMBDA_OPEN = re.compile(r"\[.*?\]\s*(?:\(.*?\))?\s*(?:mutable\s*)?(?:->.*?)?\s*\{")

# Very small brace blocks that are likely initializers or single expressions.
SINGLE_LINE_BODY = re.compile(r"\{[^{}]*\}\s*;?\s*$")

# ── Data structures ──────────────────────────────────────────────────────────

@dataclass
class Injection:
    """Records where to inject and what was detected."""
    line_number: int        # 0-based index of the `{` line
    func_name: str          # best-effort function name
    indent: str             # whitespace prefix to use

@dataclass
class Options:
    min_lines: int = 1
    skip_functions: Set[str] = field(default_factory=set)
    macro: str = ZONE_MACRO
    dry_run: bool = False
    verbose: bool = False

# ── Core logic ───────────────────────────────────────────────────────────────

def _guess_function_name(line: str) -> str:
    """Try to extract a function name from a definition line."""
    # Find the last `(` before `{` — this is the parameter list of the
    # function being defined, not of a previous function in the context.
    brace = line.rfind("{")
    search_region = line[:brace] if brace >= 0 else line
    idx = search_region.rfind("(")
    if idx < 0:
        return "<unknown>"
    prefix = search_region[:idx].strip()
    # Last token before `(` is usually the function name (possibly qualified)
    tokens = prefix.split()
    if tokens:
        name = tokens[-1]
        # Strip pointer/reference decorators
        name = name.lstrip("*&")
        return name if name else "<unknown>"
    return "<unknown>"


def _detect_body_length(lines: List[str], open_brace_idx: int) -> int:
    """Count lines in the function body (between matched braces)."""
    depth = 0
    count = 0
    for i in range(open_brace_idx, len(lines)):
        for ch in lines[i]:
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    return count
        count += 1
    return count


def _is_inside_string_or_comment(line: str, brace_pos: int) -> bool:
    """Very rough check: is the `{` at brace_pos inside a string or comment?"""
    in_string = False
    quote_char = ""
    i = 0
    while i < brace_pos:
        ch = line[i]
        if in_string:
            if ch == "\\" and i + 1 < brace_pos:
                i += 2
                continue
            if ch == quote_char:
                in_string = False
        else:
            if ch in ('"', "'"):
                in_string = True
                quote_char = ch
            elif ch == "/" and i + 1 < brace_pos:
                nxt = line[i + 1]
                if nxt == "/":
                    return True  # rest of line is comment
                if nxt == "*":
                    # block comment – simplified, assume same line
                    end = line.find("*/", i + 2)
                    if end < 0 or end >= brace_pos:
                        return True
                    i = end + 2
                    continue
        i += 1
    return in_string


def find_injections(lines: List[str], opts: Options) -> List[Injection]:
    """Scan source lines and return injection points."""
    injections: List[Injection] = []
    i = 0
    n = len(lines)

    # Track brace depth to skip nested class/struct/namespace bodies at depth.
    # We only inject at top-level function definitions.
    while i < n:
        line = lines[i]
        stripped = line.strip()

        # Skip preprocessor, blank, comment-only lines.
        if SKIP_LINE.match(stripped):
            i += 1
            continue

        # Skip control-flow / keyword lines.
        if NOT_FUNCTION.match(stripped):
            # If it has an opening brace, skip to matching close.
            if "{" in stripped and not _is_inside_string_or_comment(line, line.index("{")):
                body_len = _detect_body_length(lines, i)
                i += max(body_len, 1)
            else:
                i += 1
            continue

        # Check if this line (possibly with continuation above) looks like
        # a function definition opening.
        # Collect up to 5 preceding non-blank lines for multi-line signatures.
        # STOP at `}` boundaries to avoid crossing into a previous function body.
        context = stripped
        lookback = 0
        j = i - 1
        while j >= 0 and lookback < 5:
            prev = lines[j].strip()
            if not prev or SKIP_LINE.match(prev):
                j -= 1
                continue
            # Stop if we hit a line that closes a previous scope.
            if prev == "}" or prev.endswith("};"): # NOLINT
                break
            context = prev + " " + context
            lookback += 1
            j -= 1

        brace_pos = stripped.rfind("{")
        if brace_pos < 0 or _is_inside_string_or_comment(stripped, brace_pos):
            i += 1
            continue

        # Reject single-line bodies like `int x() { return 0; }`
        if SINGLE_LINE_BODY.search(stripped):
            i += 1
            continue

        # Check for lambda
        if LAMBDA_OPEN.search(stripped):
            i += 1
            continue

        # Now test the heuristic
        if not FUNC_HEAD.search(context):
            i += 1
            continue

        # Extract function name
        func_name = _guess_function_name(context)

        # Check skip list
        if func_name in opts.skip_functions:
            i += 1
            continue

        # Already instrumented?  Peek at the next few non-blank lines.
        already = False
        for k in range(i + 1, min(i + 4, n)):
            if ALREADY_INSTRUMENTED.search(lines[k]):
                already = True
                break
        if already:
            i += 1
            continue

        # Check minimum body length
        body_len = _detect_body_length(lines, i)
        if body_len < opts.min_lines:
            i += 1
            continue

        # Determine indentation: use the indentation of the next non-blank
        # line, or fall back to current line indent + 4 spaces.
        indent = ""
        for k in range(i + 1, min(i + 5, n)):
            nxt = lines[k]
            if nxt.strip():
                indent = re.match(r"^(\s*)", nxt).group(1)
                break
        if not indent:
            cur_indent = re.match(r"^(\s*)", line).group(1)
            indent = cur_indent + "    "

        injections.append(Injection(
            line_number=i,
            func_name=func_name,
            indent=indent,
        ))

        # Skip past this function body to avoid injecting into nested
        # function-like constructs.
        i += max(body_len, 1)
        continue

    return injections


def apply_injections(
    lines: List[str],
    injections: List[Injection],
    macro: str,
) -> List[str]:
    """Return new list of lines with injections applied."""
    result: List[str] = []
    inject_set = {inj.line_number: inj for inj in injections}

    for i, line in enumerate(lines):
        result.append(line)
        if i in inject_set:
            inj = inject_set[i]
            result.append(f"{inj.indent}{macro}\n")

    return result

# ── CLI ───────────────────────────────────────────────────────────────────────

def main() -> int:
    parser = argparse.ArgumentParser(
        description="Inject ZoneScoped into C++ function bodies for TracyLite.",
    )
    parser.add_argument("input", help="Input .cpp file")
    parser.add_argument("-o", "--output", help="Output file (default: stdout)")
    parser.add_argument("--dry-run", action="store_true",
                        help="Print injection points without modifying")
    parser.add_argument("--min-lines", type=int, default=1,
                        help="Skip functions shorter than N body lines (default: 1)")
    parser.add_argument("--skip", default="",
                        help="Comma-separated function names to skip")
    parser.add_argument("--macro", default=ZONE_MACRO,
                        help=f"Macro to inject (default: {ZONE_MACRO})")
    parser.add_argument("-v", "--verbose", action="store_true")

    args = parser.parse_args()

    input_path = Path(args.input)
    if not input_path.exists():
        print(f"error: {input_path} not found", file=sys.stderr)
        return 1

    skip_funcs = set(f.strip() for f in args.skip.split(",") if f.strip())

    opts = Options(
        min_lines=args.min_lines,
        skip_functions=skip_funcs,
        macro=args.macro,
        dry_run=args.dry_run,
        verbose=args.verbose,
    )

    lines = input_path.read_text(encoding="utf-8", errors="replace").splitlines(keepends=True)
    injections = find_injections(lines, opts)

    if args.dry_run or args.verbose:
        print(f"Found {len(injections)} injection point(s) in {input_path}:",
              file=sys.stderr)
        for inj in injections:
            print(f"  line {inj.line_number + 1}: {inj.func_name}",
                  file=sys.stderr)

    if args.dry_run:
        return 0

    result = apply_injections(lines, injections, opts.macro)
    text = "".join(result)

    if args.output:
        out_path = Path(args.output)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(text, encoding="utf-8")
        if args.verbose:
            print(f"Wrote {out_path}", file=sys.stderr)
    else:
        sys.stdout.write(text)

    return 0


if __name__ == "__main__":
    sys.exit(main())
