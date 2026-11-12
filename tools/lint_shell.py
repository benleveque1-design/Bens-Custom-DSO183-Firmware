#!/usr/bin/env python3
"""Lint a shell command before it is executed.

Why: twice now a generated command was mangled - a Python fragment leaked into
the shell text (a triple-quote followed by a stray `if False else None`) - and the call died, or worse,
ran half of what was intended.  Every shell command that touches the instrument
is piped through this linter first; if the lint fails, the command is NOT run.

Checks:
  1. Python fragments that have leaked into shell text before
  2. unbalanced quotes on any line
  3. unbalanced heredoc markers
  4. the command must parse as shell (`bash -n`)

usage: lint_shell.py <file>
       some-command | lint_shell.py -
"""
import re
import subprocess
import sys

FRAGMENT = re.compile(r'""",|"""|if False\b|\belse None\s*$')
HEREDOC = re.compile(r"<<-?\s*[A-Za-z_][A-Za-z0-9_]*\s*$")


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "-"
    data = sys.stdin.read() if src == "-" else open(src, encoding="utf-8", errors="replace").read()

    problems = []

    for n, line in enumerate(data.splitlines(), 1):
        if FRAGMENT.search(line):
            problems.append("line %d: a Python fragment leaked into the shell command: %r" % (n, line))
        s = line.replace("'", "")
        d = line.replace('"', "")
        if len(s) % 2:
            problems.append("line %d: unbalanced quote: %r" % (n, line))
        if len(d) % 2:
            problems.append("line %d: unbalanced double quote: %r" % (n, line))

    opens = sum(1 for line in data.splitlines()
                if HEREDOC.search(line.replace(chr(39), "").replace(chr(34), "")))
    if opens % 2:
        problems.append("unbalanced heredoc marker (%d)" % opens)

    r = subprocess.run(["bash", "-n", "-"], input=data, capture_output=True, text=True)
    if r.returncode != 0:
        problems.append("does not parse as shell:\n" + r.stderr.strip())

    if problems:
        print("LINT FAIL:")
        for p in problems:
            print("  " + p)
        print("LINT: command NOT executed")
        return 1
    print("LINT OK: %d bytes, parses clean" % len(data))
    return 0


if __name__ == "__main__":
    sys.exit(main())
