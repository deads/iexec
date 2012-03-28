#include <stdio.h>
#include <stdlib.h>
#include <error.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char **argv) {
  if (argc != 2) {
    error(0, 0, "you must specify the number of processes to fork!");
  }
  int n = atoi(argv[1]);
  for (int i = 1; i <= n; i++) {
    int pid = fork();
    if (pid < 0) {
      error(0, errno, "i=%d failed forking!", i);
      exit(EXIT_FAILURE);
    }
    else if (pid == 0) { /* child. */
      sleep(1000);
      exit(EXIT_SUCCESS);
    }
    else { /* parent */
      printf("i=%d forking succeeded pid=%d\n", i, pid);
    }
  }
}
