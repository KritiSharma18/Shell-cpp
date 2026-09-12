# Custom Unix Shell (C++)

A POSIX-compliant command-line shell built from scratch in C++.

## Features
- Built-in commands: `cd`, `pwd`, `echo`, `type`, `exit`
- PATH-based executable resolution for running external programs
- Directory navigation: absolute paths, relative paths, and `~` (home directory)
- I/O redirection: `>`, `>>`, `2>`, `2>>`
- Multi-stage pipelines (`cmd1 | cmd2 | cmd3`) supporting both built-ins and external programs
- Process execution via `fork`/`execv`, with proper parent-child synchronization using `waitpid`

## Build & Run
```bash
cmake -B build
cmake --build build
./your_program.sh
```

## Why I built this
I wanted to strengthen my systems-programming fundamentals — process management, file descriptors, and OS-level concepts — alongside my existing AI/ML and full-stack project work.
