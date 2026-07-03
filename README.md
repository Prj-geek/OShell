# OShell

A cross-platform command-line shell built in C++ featuring process execution, command parsing, PATH resolution, filesystem navigation, quote handling, and stdout/stderr redirection.

## Features

- Built-in commands (`cd`, `pwd`, `echo`, `type`)
- External program execution
- PATH-based executable lookup
- Single and double quote parsing
- Stdout and stderr redirection
- Linux and Windows support

## Technologies

- C++17
- std::filesystem
- POSIX process APIs (`fork`, `execvp`, `waitpid`)
- Windows process APIs (`_spawnv`)
