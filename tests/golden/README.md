# Golden tests

Each case pins the exact output of a command: the files it writes, what it
prints, and its exit code. A change in generated output shows up as a diff in
review instead of a silent drift. See `golden.h` for the full contract.

## A case

```
tests/golden/<group>/<case>/
    args.txt         one command-line argument per line; ${IN} and ${OUT}
                     become the relative paths "in" and "out"
    input/           copied to <work>/in
    existing/        optional: copied to <work>/out before the run
    expected/        the tree that must exist under <work>/out afterwards
    stdout.txt       expected stdout      (absent = must be empty)
    stderr.txt       expected stderr      (absent = must be empty)
    exit_code.txt    expected exit code   (absent = 0)
    notes.md         why this case exists (ignored by the runner)
    requires.txt     optional: the optional features of the build the case needs, one a line
                     (yaml, libclang); where one is missing the case is skipped
```

The command runs in-process with a fresh temporary directory as its working
directory, so every path it prints is the relative one from `args.txt` and the
goldens are identical wherever the repository is checked out. Each case runs
twice, in separate directories, and the two results must be identical.

The output must also follow the conventions of everything this tool writes:
LF line endings, a final newline, no trailing whitespace in files, and no
absolute paths anywhere.

## Updating

```console
$ cmake --build build --target update-golden     # or: MT_UPDATE_GOLDEN=1 ctest -R golden
$ git diff tests/golden                          # read it before committing
```

Never edit `expected/`, `stdout.txt`, `stderr.txt` or `exit_code.txt` by hand.
Updating refuses to record output that is non-deterministic or breaks a
convention. Files are marked `-text` in `.gitattributes` so git never rewrites
their line endings.

To add a case: create the directory with `args.txt` and its inputs, run the
update, and review what it recorded.
