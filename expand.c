/*
 * Do filename expansion with the shell.
 */

#define EXPAND_BUF	2048

#include "sysincludes.h"
#include "mtools.h"

const char *expand(const char *input, char *ans)
{
	int pipefd[2];
	int pid;
	int last;
	char buf[256];
	int status;

	if (input == NULL)
		return(NULL);
	if (*input == '\0')
		return("");
					/* any thing to expand? */
	if (!strpbrk(input, "$*(){}[]\\?`~")) {
		strcpy(ans, input);
		return(ans);
	}
					/* popen an echo */
	sprintf(buf, "echo %s", input);
	
	if(pipe(pipefd)) {
		perror("Could not open expand pipe");
		exit(1);
	}
	switch((pid=fork())){
		case -1:
			perror("Could not fork");
			exit(1);
			break;
		case 0: /* the son */
			close(pipefd[0]);
			destroy_privs();
			close(1);
			dup(pipefd[1]);
			close(pipefd[1]);
			execl("/bin/sh", "sh", "-c", buf, 0);
			break;
		default:
			close(pipefd[1]);
			break;
	}
	last=read(pipefd[0], ans, EXPAND_BUF);
	kill(pid,9);
	wait(&status);
	if(last<0) {
		perror("Pipe read error");
		exit(1);
	}
	if(last)
		ans[last-1] = '\0';
	else
		strcpy(ans, input);
	return ans;
}
