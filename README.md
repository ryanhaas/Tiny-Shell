# Tiny-Shell
A tiny shell program for my operating systems course.
Allows for a single program to be thrown to the background.

### Running the program
On a Linux machine just call `./tsh`

### Commands
- `quit` = Quit the shell, return control back to executing shell
- `fg` = Bring the suspended program to the foreground
- `<PROGRAM> &` = run <PROGRAM> in the background
  - Need path to built in programs, for example to run the `echo` command you would need to run `/bin/echo mytext`

### Key Bindings
- `Ctrl-C` = Send SIGINT signal to executing program
- `Ctrl-Z` = Send SIGTSTP signal to executing program
