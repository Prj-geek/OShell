# OShell

OShell is a cross-platform, minimal command-line shell implemented in C++. It provides core shell functionalities including process execution, command parsing, PATH resolution, filesystem navigation, quote handling, and output redirection.

## Features

- Built-in Commands: Includes essential shell commands such as `cd`, `pwd`, `echo`, and `type`.
- Program Execution: Ability to launch and manage external programs.
- PATH Resolution: Automatic lookup of executables based on the system PATH environment variable.
- String Parsing: Robust handling of single and double quotes for complex arguments.
- I/O Redirection: Support for standard output (stdout) and standard error (stderr) redirection.
- Cross-Platform: Designed to run on both Linux and Windows operating systems.

## Build Instructions

This project uses CMake as its build system. A C++17 (or higher) compatible compiler is required.

1. Clone the repository:
   ```bash
   git clone https://github.com/Prj-geek/OShell.git
   cd OShell
   ```

2. Create a build directory and configure the project:
   ```bash
   cmake -B build
   ```

3. Build the executable:
   ```bash
   cmake --build build
   ```

The compiled executable will be located in the `build` directory (or `build\Debug` on Windows depending on your generator).

## Usage

Run the shell executable from your build directory to start an interactive session:

```bash
./build/shell
```

Example session:
```bash
$ echo "Hello World"
Hello World

$ pwd
/path/to/current/directory

$ type ls
ls is /bin/ls
```

## Technical Details

- Language: C++17
- Standard Library: Utilizes `std::filesystem` for modern, cross-platform path and directory management.
- OS APIs: Implements POSIX process APIs (`fork`, `execvp`, `waitpid`) for Linux, and Windows process APIs (`_spawnv`) for Windows environments.
