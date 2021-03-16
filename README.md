# Shell

## Project written in C for "Operating Systems" classes

### Features

- Own implementation of `cp` and `grep`
- **<span style="font-family: Courier;"><span style="color:#BA4A4A">C</span><span style="color:#BABA4A">o</span><span style="color:#4ABA4A">l</span><span style="color:#4ABABA">o</span><span style="color:#4A4ABA">r</span><span style="color:#BA4ABA">s</span></span>** support
- **Quotes**: handles arguments inside `' ... '` and `" ... "` even if they are mixed up
- **History**: browse former commands using UP/DOWN arrow keys or print a whole list with `history` command
- **Autocompletion**:
  - enabled by TAB
  - searches through all available commands in the system
  - if typed text starts with `./` then iterates over files inside the
    current working directory

### Prerequisites

GNU/Linux OS with ncurses library

### Installing

`make`

### Running

`./shell`

### Clearing up

`make clean`
