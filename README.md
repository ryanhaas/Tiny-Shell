# Tiny-Shell
A tiny shell program for my operating systems course.
Allows for a single program to be thrown to the background.

### Commands
- 'quit' = Quit the shell, return control back to executing shell
- 'fg' = Bring the suspended program to the foreground
- '<PROGRAM> &' = run <PROGRAM> in the background

### Key Bindings
- 'Ctrl-C' = Send SIGINT signal to executing program
- 'Ctrl-Z' = Send SIGTSTP signal to executing program
