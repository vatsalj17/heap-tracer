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
	const char* shim_path = "/usr/local/lib/shim.so";
	if (access(shim_path, F_OK) == -1) {
		fprintf(stderr, "error: shim library not found in '%s'.\n", shim_path);
		return EXIT_FAILURE;
	}
	const char* var = "LD_PRELOAD";
	setenv(var, shim_path, 1);
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
