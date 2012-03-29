#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <error.h>
#include <errno.h>
 
int main()
{
  while (1) {
    /* Fork the process as both the parent and the child. */
    int new_pid = fork();

    /* If failure, stop forking. */
    if (new_pid < 0) {
      error(0, errno, "fork failed parent_pid=%d", getppid());
      exit(EXIT_FAILURE);
    }
    /* If success, stop forking. */
    else if (new_pid > 0) {
      printf("fork succeeded parent_pid=%d child_pid=%d\n", getppid(), new_pid);
      exit(EXIT_FAILURE);
    }
  } 
}
