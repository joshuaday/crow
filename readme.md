crow
====

crow is a simple tool for Linux that effectively duplicates the 
functionality of xargs, but with a peculiar bit of syntax and
more appropriate defaults for casual use.

Any expression enclosed in -[ brackets like these ]- will be
evaluated, and each line of its output will be expanded as one
argument.  crow's command line arguments can be used 
at the beginning of any such expression.  These expression
brackets may be nested and they may be used in initial position
to generate command names (but be careful with this.)

By default, crow expects the first term it sees to be the name
of an executable, and treats the rest as its arguments.  This
means that it will ignore aliases and shell commands; use the -c
argument to pass it to the shell.

