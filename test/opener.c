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
    error(0, 0, "you must specify the number of file descriptors to open");
  }
  int n = atoi(argv[1]);
  for (int i = 1; i <= n; i++) {
    int fd = open("/dev/null", O_RDONLY);
    if (fd < 0) {
      error(0, errno, "i=%d opener failed opening file", i);
      exit(EXIT_FAILURE);
    }
    else {
      printf("i=%d opener succeeded fd=%d\n", i, fd);
    }
  }
  printf("Sleeping...\n");
  sleep(10);
}
