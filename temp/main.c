#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <process.h>

#define error(status, fmt) \
do { \
	fprintf(stderr, fmt); \
	exit(status);	\
} while (0)

#define PIPE_BUF 512
int
main(int argc, const char *argv[])
{
	int rc;
	struct {
		int fdread;
		int fdwrite;
	} fdpair[2] = { {-1, -1}, {-1, -1} };
	int sout, sin;
	HANDLE hproc;
	char buf[1024];
	int i;

	printf("Lets begin\n");

	for (i = 0; i < 2; i++) {
		rc = _pipe ((void *)&fdpair[i], PIPE_BUF, O_BINARY);
		if (rc == -1)
			error(1, "pipe error\n");
	}

	printf("Pipe created\n");

	// windows workaround
	sout = _dup(_fileno(stdout));
	sin = _dup(_fileno(stdin));

	if (_dup2(fdpair[0].fdwrite, _fileno(stdout)) != 0)
		error(1, "first dup2\n");
	if (_dup2(fdpair[1].fdread, _fileno(stdin)) != 0)
		error(1, "second dup2\n");

	hproc = (HANDLE)_spawnlp(P_NOWAIT, "cmd.exe", NULL);
	if (hproc == -1)
		error(1, "createproc error\n");

	// restore fds back
	_dup2(sout, _fileno(stdout));
	_dup2(sin, _fileno(stdin));

	printf("Process started\n");

	while (true) {
		int ret;

		ret = read(_fileno(stdin), buf, sizeof(buf));
		if (ret == -1)
			error(1, "input read error\n");
		ret = write(fdpair[1].fdwrite, buf, ret);
		if (ret == -1)
			error(1, "pipe write error\n");

		printf("input sended\n");

		ret = read(fdpair[0].fdread, buf, sizeof(buf));
		if (ret == -1)
			error(1, "pipe read error\n");
		ret = write(_fileno(stdout), buf, ret);
		if (ret == -1)
			error(1, "stdout write error\n");
	}
	
	return 0;
}
