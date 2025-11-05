#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char** argv) {
#ifdef __linux__
	if (argc < 2) {
		printf("usage: %s <executable>\n", argv[0]);
		return EXIT_FAILURE;
	}
	const char* dir = dirname(argv[0]);
	const char* shim = "shim.so";
	char path[100];
	snprintf(path, 100, "%s/%s", dir, shim);
	if (access(path, F_OK) == -1) {
		fprintf(stderr, "error: shim library '%s' not found in '%s'.\n", shim, dir);
		return EXIT_FAILURE;
	}
	const char* var = "LD_PRELOAD";
	setenv(var, path, 1);
	pid_t child = fork();
	int status;
	if (child == 0) {
		execvp(argv[1], argv + 1);
	} else {
		waitpid(child, &status, WUNTRACED);
	}
	return 0;
#else 
    printf("Unsupported Platform\n");
    return 1;
#endif
}
