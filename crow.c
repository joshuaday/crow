#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

extern char **environ;

typedef struct invocation_t {
	struct invocation_t *parent;

	int argc;
	char **argv;
	
	// private:
	char *buf;
	int argmax, bufidx, bufmax;
	char *argfree; // track whether each argument needs freeing

	int switchmode;
	int switch_quote; // in quotemode, all output from the invocation gets escaped and shoved into one big argument
	int double_close;
	int delimiter;
} invocation_t;


invocation_t *invocation_new(invocation_t *parent) {
	invocation_t *l = malloc(sizeof(*l));
	l->argc = 0;
	l->bufidx = 0;

	l->argmax = 32;
	l->bufmax = 64;

	l->argv = malloc(l->argmax * sizeof(*l->argv));
	l->argfree = malloc(l->argmax);
	l->buf = malloc(l->bufmax);
	
	l->parent = parent;
	l->switchmode = 1;

	l->delimiter = parent ? parent->delimiter : '\n';

	return l;
}

void invocation_free(invocation_t *l) {
	int i;
	for (i = 0; i < l->argc; i++) {
		if (l->argfree[i]) {
			free(l->argv[i]);
		}
	}
	free(l->argv);
	free(l->argfree);
	free(l->buf);
	l->argv = NULL;
	l->argfree = NULL;
	l->buf = NULL;
	free(l);
}

void invocation_append_word(invocation_t *l, char *arg, int force_copy) {
	if (arg == NULL) {
		if (l->bufidx == 0) return; // don't append empty words

		char *copy = malloc(l->bufidx + 1);
		if (copy == NULL) {
			// wow, really?
			exit(EXIT_FAILURE);
		}
		arg = memcpy(copy, l->buf, l->bufidx);
		arg[l->bufidx] = '\0';

		l->bufidx = 0;
		l->argfree[l->argc] = 1;
	} else if (force_copy) {
		int len = strlen(arg);
		char *copy = malloc(len + 1);
		if (copy == NULL) {
			// wow, really?
			exit(EXIT_FAILURE);
		}
		
		arg = memcpy(copy, arg, len);
		arg[len] = '\0';

		l->argfree[l->argc] = 1;
	} else {
		l->argfree[l->argc] = 0;
	}

	if (l->argc + 1 >= l->argmax) {
		char **copy = realloc(l->argv, 2 * l->argmax * sizeof(*l->argv));
		if (copy == NULL) {
			// really?
			invocation_free(l);
			exit(EXIT_FAILURE);
		} else {
			l->argv = copy;
		}

		char *argfree_copy = realloc(l->argfree, 2 * l->argmax);
		if (argfree_copy == NULL) {
			invocation_free(l);
			exit(EXIT_FAILURE);
		} else {
			l->argfree = argfree_copy;
		}

		l->argmax *= 2;
	}

	l->argv[l->argc++] = arg;
	l->argv[l->argc] = NULL;
}

void invocation_append_char(invocation_t *l, int ch) {
	if (l->bufidx + 1 >= l->bufmax) {
		char *copy = realloc(l->buf, 2 * l->bufmax);
		if (copy == NULL) {
			free(l->buf);
			exit(EXIT_FAILURE);
		}
		l->buf = copy;
		l->bufmax *= 2;
	}

	l->buf[l->bufidx++] = ch;
}

void invocation_quote(invocation_t *l) {
	if (l->parent == NULL) {
		int i;
		for (i = 0; i < l->argc - 1; i++) {
			printf("%s ", l->argv[i]);
		}
		if (i < l->argc) {
			printf("%s", l->argv[i]);
		}
	} else {
		int i, j;
		for (i = 0; i < l->argc; i++) {
			char *arg = l->argv[i];
			// invocation_append_char(l, '\'');
			for (j = 0; arg[j] != '\0'; j++) {
				int ch = arg[j];
				if (0 && ch == '\'') {
					invocation_append_char(l, ch);
					invocation_append_char(l, '\\');
					invocation_append_char(l, ch);
					invocation_append_char(l, ch);
				} else {
					invocation_append_char(l, ch);
				}
			}
		
			// invocation_append_char(l, '\'');
			if (i < l->argc - 1) {
				invocation_append_char(l, ' ');
			}
		}
		invocation_append_word(l->parent, l->buf, 1);
	}
}

void invocation_execute(invocation_t *l) {
	if (l->argc < 1) {
		return;
	}

	if (l->switch_quote) {
		invocation_quote(l);
		return;
	}

	fflush(stdout);

	pid_t cpid;
	invocation_t *parent = l->parent;
	if (l->parent == NULL) {
		if ((cpid = fork()) == -1) {
			perror("fork");
			exit(EXIT_FAILURE);
		}

		if (cpid) {
			wait(NULL);
		} else {
			// become the selected command
			execvpe(l->argv[0], l->argv, environ);
			perror(l->argv[0]);
			exit(EXIT_FAILURE);
		}
	} else {
		int pipefd[2];

		if (pipe(pipefd) == -1) {
			perror("pipe");
			exit(EXIT_FAILURE);
		}

		if ((cpid = fork()) == -1) {
			perror("fork");
			exit(EXIT_FAILURE);
		}

		if (cpid) {
			// close write end
			char ch;
			close(pipefd[1]);

			// pass each line as a separate word
			while (read(pipefd[0], &ch, 1) > 0) {
				if (ch == l->delimiter) {
					invocation_append_word(parent, NULL, 1);
				} else {
					invocation_append_char(parent, ch);
				}
			}

			invocation_append_word(parent, NULL, 1);
			
			close(pipefd[0]);
			wait(NULL);
		} else {
			// close read end
			close(pipefd[0]); 

			// attach the write end to stdout
			dup2(pipefd[1], STDOUT_FILENO);

			// become the selected command
			execvpe(l->argv[0], l->argv, environ);
			perror(l->argv[0]);
			
			_exit(EXIT_FAILURE);
		}
	}
}

int unwind_one(invocation_t **handle) {
	// execute and pop!
	int repeat = 1;

	while (repeat) {
		repeat = (*handle)->double_close;

		invocation_t *child = *handle;
		*handle = child->parent;

		invocation_execute(child);
		invocation_free(child);
	}
}

int help( ) {
	printf("%s",
		"Usage: crow [options] expansion\n"
		"\n"
		"Options:\n"
		"-c   run expansion as a shell command\n"
		"-q   pass arguments as a single quoted argument rather than separately\n"
		"-n   expect newline as a delimiter (default)\n"
		"-0   expect nul instead of newline as a delimiter\n"
		"--   escape character for -\n"
		"\n"
		"Expansion:\n"
		"Any command contained within delimiters like these -[ example ]-\n"
		"will be executed and each line of its standard output transformed into\n"
		"a single argument.  The result will be executed, with the first word as\n"
		"a command.  Expansions are fully reentrant.  Spaces are required around\n"
		"-[ and ]- or they will be ignored.  Arguments may be used within expansions.\n"
		"\n"
		"Examples:\n"
		"Play all ogg files:\n"
		"\tcrow vlc -[ locate .ogg ]-\n"
		"Play all ogg files, even if some have newlines in their names:\n"
		"\tcrow vlc -[ -0 locate -0 .ogg ]-\n"
		"\n"
	);
}

int main(int argc, char *argv[]) {
	invocation_t *handle = invocation_new(NULL);

	if (argc == 1) {
		help();
	}
	
	int i;
	for (i = 1; i < argc; i++) {
		char *arg = argv[i];
		if (handle->switchmode && arg[0] == '-' && arg[1] != '[') {
			if (arg[2] == '\0') {
				if (arg[1] == '-') {
					handle->switchmode = 0;
				} else if (arg[1] == 'q') {
					handle->switch_quote = 1;
				} else if (arg[1] == '0') {
					handle->delimiter = '\0';
				} else if (arg[1] == 'n') {
					handle->delimiter = '\n';
				} else if (arg[1] == 'c') {
					if (!handle->switch_quote) {
						char *shell = getenv("SHELL");
						if (shell != NULL) {
							invocation_append_word(handle, getenv("SHELL"), 1);
							invocation_append_word(handle, "-c", 0);
							// now, the rest of the command will be quoted
							handle = invocation_new(handle);
							handle->switch_quote = 1;
							handle->double_close = 1;
						}
					}
				}
			}
		} else {
			handle->switchmode = 0;

			if (arg[0] == '-' && arg[1] == '[' && arg[2] == '\0') {
				handle = invocation_new(handle);
			} else if (handle->parent != NULL && arg[0] == ']' && arg[1] == '-' && arg[2] == '\0') {
				unwind_one(&handle);
			} else {
				invocation_append_word(handle, arg, 0);
			}
		}
	}

	unwind_one(&handle);

	if (handle != NULL) {
		fprintf(stderr, "Expected ]-\n");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

