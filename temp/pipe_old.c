#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <stdbool.h>

#define error(status, fmt) \
do { \
	fprintf(stderr, fmt); \
	exit(status);	\
} while (0)

int
main(int argc, const char *argv[])
{
	struct {
		HANDLE fdread;
		HANDLE fdwrite;
	} fdpair[2] = { {-1, -1}, {-1, -1} };
	SECURITY_ATTRIBUTES sec;
	HANDLE input, output;
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	char buf[1024];
	int i;

	printf("Lets begin\n");

	memset(&sec, 0, sizeof(sec));

	for (i = 0; i < 2; i++) {
		if (!CreatePipe(&fdpair[i].fdread, &fdpair[i].fdwrite, &sec, 0)) {
			printf("createpipe err\n");
			exit(1);
		}
	}

	printf("Pipe created\n");
	/*
	if (!SetStdHandle(STD_INPUT_HANDLE, fdpair[0].fdwrite) || 
	    !SetStdHandle(STD_OUTPUT_HANDLE, fdpair[1].fdread)) {
		printf("std handle error\n");
		exit(1);
	}
	*/


	memset(&si, 0, sizeof(si));
	memset(&si, 0, sizeof(si));
	si.hStdOutput = fdpair[0].fdwrite;
	si.hStdInput = fdpair[1].fdread;

	if (!CreateProcess(NULL,
			"cmd.exe",
			NULL,
			NULL,
			true,
			NORMAL_PRIORITY_CLASS,
			NULL,
			NULL,
			&si,
			&pi))
		error(1, "createproc error\n");

	printf("Process started\n");

	input = GetStdHandle(STD_INPUT_HANDLE);
	output = GetStdHandle(STD_OUTPUT_HANDLE);

	while (true) {
		int ret;
		if (!ReadFile(input, buf, sizeof(buf), &ret, NULL))
			error(1, "input read error\n");
		if (!WriteFile(fdpair[1].fdwrite, buf, ret, &ret, NULL))
			error(1, "pipe write error\n");

		printf("input sended\n");

		if (!ReadFile(fdpair[0].fdread, buf, sizeof(buf), &ret, NULL))
			error(1, "pipe read error\n");
		if (!WriteFile(output, buf, ret, &ret, NULL))
			error(1, "output write error\n");
	}
	
	return 0;
}
