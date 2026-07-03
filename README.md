# OShell

A small shell I wrote in C++ from scratch — no readline, no parsing library, just a REPL, a hand-rolled tokenizer, and a raw-mode input loop I built myself so I could get proper bash-style tab completion working.

Runs on Linux, macOS, and Windows off the same source, with the platform-specific bits (process spawning, raw terminal input, executable resolution, redirection) split behind `#ifdef _WIN32`.

## Why I built this

I wanted to actually understand what a shell is doing under the hood — parsing quoted strings, resolving `$PATH`, forking and redirecting file descriptors, and especially how tab completion works, since that's the part that always felt like magic. Turns out most of it is a REPL that reads raw keystrokes instead of full lines and reacts to `Tab` specially. Once that clicked, the rest of it — quoting, redirection, programmable completion — was mostly about matching bash's behavior closely enough that it feels normal to use.

## What it does

**Builtins:** `echo`, `pwd`, `cd` (absolute paths, relative paths, `~`/no-arg for home), `type`, `exit`, and `complete`.

**Running external programs:** resolves commands off `$PATH` by walking every directory in it, checks execute permissions on POSIX before treating a file as runnable, and on Windows will try `.exe`/`.cmd`/`.bat` suffixes if the bare name doesn't exist as-is. Processes are spawned with `fork`/`execvp`/`waitpid` on Linux/macOS and `_spawnv` on Windows.

**Quoting:** single quotes are fully literal — nothing inside them is special, not even a backslash. Double quotes preserve their content but still honor backslash escapes. Backslash escaping also works outside of any quotes. Quoted and unquoted chunks glue together into a single argument the way bash does, so `fo'o'bar` becomes `foobar`, and empty quotes (`''`) still produce an empty-string argument instead of being silently dropped — that one tripped me up for a bit before I added the explicit "has this token started" tracking.

**Redirection:** `>` / `1>`, `>>` / `1>>`, `2>`, `2>>` — both truncate and append variants. Builtins and external programs are redirected differently under the hood: builtins redirect by swapping out `std::cout`/`std::cerr`'s underlying stream buffer for the duration of the call and restoring it after, while external programs redirect at the file-descriptor level — `dup2` right after `fork` on POSIX, and manual handle juggling with `_dup`/`_dup2`/`SetStdHandle` around `_spawnv` on Windows.

**Tab completion** — this is the part I spent the most time on, and the reason the input loop isn't just `std::getline`. Neither platform's default terminal mode lets you intercept `Tab` mid-line, so I wrote a character-by-character reader for both POSIX (`termios` raw mode) and Windows (`_getch`) that builds up the buffer manually, handles backspace, and reacts specially whenever `Tab` comes in. When it does, it checks, in order:

1. Is there a registered completer for this command (via `complete -C`)? If so, run it as a subprocess with `COMP_LINE` and `COMP_POINT` set as environment variables, bash-style, and use whatever candidates it prints back on stdout.
2. Does the current word look like a path? If so, complete against that directory — nested paths included — and tack on a trailing `/` for directory matches.
3. Otherwise, once at least one space has been typed, complete filenames against the current working directory.
4. If it's the first word on the line, complete against a cached set of shell builtins plus everything found across `$PATH`.

Matching follows normal bash conventions: one match completes in place and adds a trailing space (or `/` for a directory, no space). Several matches that share a longer prefix than what's typed extend the input up to that shared prefix. Several matches with nothing more in common ring the terminal bell once, and listing them all only happens on a second consecutive `Tab` — same two-step bash does. No matches at all just rings the bell and leaves the line alone.

**Programmable completion:** a working chunk of bash's `complete -C` mechanism. You can register an external script as the completer for a command with `complete -C script.sh mycmd`, check what's registered with `complete -p mycmd`, or remove it with `complete -r mycmd`. The registered script gets called with the current word and previous word as arguments plus `COMP_LINE`/`COMP_POINT` as env vars, close enough to bash's contract that simple existing completion scripts can be reused without modification.

## A few implementation notes

- The tokenizer (`parse_args`) is a single pass over the input string with a small state machine (in-single-quote, in-double-quote, escaped) rather than a full grammar — it's not a complete POSIX shell parser, but it covers the quoting rules people actually rely on day to day.
- `build_completions()` walks `$PATH` fresh each time command completion is triggered rather than caching it once at startup, so newly installed binaries show up without restarting the shell — at the cost of a bit of completion latency on a very long `$PATH`.
- Redirect parsing (`extract_redirects`) strips the redirection tokens out of the argument list before the command itself ever sees them, so builtins and external programs don't need to know redirection exists — they just get clean args and inherit whatever stream swap or fd dup already happened.
- The longest-common-prefix logic for multi-match completion is a plain character-by-character comparison across all candidates, no tries or anything fancy — it didn't need to be fast, just correct.

## Building

Needs a C++17 compiler and CMake.

```bash
git clone https://github.com/Prj-geek/OShell.git
cd OShell
cmake -B build
cmake --build build
```

On Windows I build it through MSYS2 UCRT64:

```bash
cmake -B build -G "Ninja"
cmake --build build
```

One gotcha I ran into: if you build it under MSYS2 and then run the binary from a plain PowerShell prompt, you might get no output at all. It's a TTY detection / buffering mismatch between the two environments, not a bug in the shell itself — running it from the MSYS2 terminal directly avoids the issue.

## Usage

```
$ echo Hello World
Hello World

$ pwd
/home/user/OShell

$ echo hi > out.txt
$ cat out.txt
hi

$ echo more >> out.txt

$ type cd
cd is a shell builtin

$ type python3
python3 is /usr/bin/python3

$ complete -C /path/to/my_completer.sh mycmd
$ complete -p mycmd
complete -C '/path/to/my_completer.sh' mycmd

$ complete -r mycmd
```

Hit `Tab` while typing to complete commands, filenames, or whatever a registered completer hands back. Double-tap `Tab` to see the full list of matches when it can't narrow things down any further on its own.

## Known limitations

Since it's a personal project and not something I'm trying to make bash-complete, a few things are intentionally missing or simplified:
- No pipelines (`|`) — every command runs standalone.
- No background jobs, `&`, `fg`/`bg`, or job control.
- No environment variable expansion (`$VAR`) or globbing.
- The double-quote escaping rules are a simplification of bash's — backslash escapes any character inside double quotes here, whereas bash only treats a handful of characters as escapable there.

None of these bothered me enough to chase down, but they'd be the obvious next steps if I ever picked this back up.

## Status

Done — this was a project to actually understand how a shell works under the hood, not something I'm planning to publish or keep extending. No license attached; it's just here for reference.
