#!/usr/bin/env python3
"""
Leverage 'wake -g' to create a tags file

The main weakness of this approach is that it uses line numbers
instead of regular expressions to find tags. Meaning frequent re-runs
to keep it up to date. Also, this only finds globals.

make sure that wake-db exists
in the top of your workspace:
usage:
waketags.py
"""

import subprocess
import sys
from pathlib import Path
from re import compile


HEADER = """
!_TAG_FILE_FORMAT       2       /extended format; --format=1 will not append ;" to lines/
!_TAG_FILE_SORTED       1       /0=unsorted, 1=sorted, 2=foldcase/
!_TAG_PROGRAM_AUTHOR    Chris Stillson  /stillson@sifive.com
!_TAG_PROGRAM_NAME      waketags.py
!_TAG_PROGRAM_VERSION   1.0     //
""".strip()

FILE_RE_STR = r'(?P<sig>.*) = \<(?P<file>.*)\>$'
FILE_RE = compile(FILE_RE_STR)


def get_wake_globals():
    proc = subprocess.run(['wake', '-g'], capture_output=True, text=True)

    return proc.stdout.splitlines(keepends=False)


"""
example line:
tempty: Tree a => Boolean = <../usr/local/share/wake/lib/core/tree.wake:58:[12-31]>

1. up to colon is the tag
2. tag may be two words, i.e 'unary +', so if it is take the second
3. next split the signature and file information
4. get the file name and line number. if line is a range, take the first
   number
5. create the tagfile line and print into a string buffer
6. sort the string buffer
7. print the tagsfile header and tags into new tags file
"""

if __name__ == '__main__':
    OUTPUT = []

    for a_line in get_wake_globals():
        try:
            tag, rest = a_line.split(':', 1)
        except ValueError:
            print('badline: %s' % a_line, file=sys.stderr)
            continue

        try:
            descr, newtag = tag.split(" ", 1)
        except ValueError:
            pass
        else:
            tag = newtag

        spec = FILE_RE.match(rest)
        file_spec = spec.group('file').strip()
        signature = spec.group('sig').strip()
        file_name, file_line, location = file_spec.split(':')
        if file_line.startswith('['):
            file_line = file_line.split('-')[0][1:]

        OUTPUT.append((tag, file_name, file_line))

    TAGFILE = Path('tags').open(mode='w')
    print(HEADER, file=TAGFILE)
    for out_line in sorted(OUTPUT):
        print("%s\t%s\t%s" % out_line, file=TAGFILE)
