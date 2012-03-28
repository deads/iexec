/**
 * File:     iexec
 * Purpose:  This program implements a command line utility to execute
 *           programs as daemon process using setsid().
 *
 * Author:   Damian Eads
 */

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <getopt.h>
#include <error.h>
#include <locale.h>
#include <libintl.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "iexec-help.h"
#include "iexec-help-nontty.h"

#define IEXEC_VERSION "1.1"

#define IEXEC_OPTION_UMASK 8765
#define IEXEC_OPTION_VERSION 8766
#define IEXEC_OPTION_RLIMIT_SOFT_NOFILE 8767
#define IEXEC_OPTION_RLIMIT_SOFT_NPROC 8768
#define IEXEC_OPTION_RLIMIT_HARD_NOFILE 8769
#define IEXEC_OPTION_RLIMIT_HARD_NPROC 8770

#define IEXEC_RLIMIT_UNCHANGED -2

/**
 * Holds parsed parameters passed to the iexec utility.
 */

typedef struct iexec_config {
  int umask;            /** The umask to set for the daemon (-1 to not reset) */
  int keep_open;        /** Whether to keep stdin, stdout, stderr file
                           descriptors open. */
  int *fds_to_close;    /** The file descriptors to close prior to exec. */
  int num_fds_to_close; /** The number of file descriptors to close prior to exec. */
  char *use_stdin_file; /** The pathname to use for standard input. This
                            is ignored when keep_open is true.*/
  char *use_stdout_file;/** The pathname to use for standard output. This
                            is ignored when keep_open is true.*/
  char *use_stderr_file;/** The pathname to use for standard error. This
                            is ignored when keep_open is true.*/
  char *use_pid_file;   /** The pathname to use for the pid file. 
                            (0 = ignore)*/
  char *use_working_dir; /** The working directory of the daemonized program. */
  int remaining_argc;   /** The remaining argument count after iexec
                            arguments. */
  char **remaining_argv;/** The remaining argument values after iexec
                            arguments. */
  int rlimit_soft_nofile;   /** The soft limit on RLIMIT_FD. */
  int rlimit_soft_nproc;/** The soft limit on RLIMIT_NOFILE. */
  int rlimit_hard_nofile;   /** The hard limit on RLIMIT_NOFILE. */
  int rlimit_hard_nproc;/** The hard limit on RLIMIT_NOFILE. */
} iexec_config;

/**
 * Prints out the usage/help message to standard error.
 */
void usage(int use_stderr) {
  int fd = use_stderr ? 2 : 1;
  FILE *file = use_stderr ? stderr : stdout;
  if (isatty(fd)) {
    fprintf(file, "%.*s", iexec_txt_len, iexec_txt);
  }
  else {
    fprintf(file, "%.*s", iexec_nontty_txt_len, iexec_nontty_txt);
  }
}

/**
 * Prints out the version of iexec to standard output.
 */
void version() {
  printf("%s\n", IEXEC_VERSION);
}

/**
 * Sets the close-on-exit flag on the file descriptor.
 *
 * Taken from http://www.gnu.org/s/libc/manual/html_node/Descriptor-Flags.html
 */
int set_cloexec_flag(int desc) {
	int oldflags = fcntl(desc, F_GETFD, 0);
	/* If reading the flags failed, return error indication now. */
	if (oldflags < 0)
		return oldflags;
	/* Set just the flag we want to set. */
	oldflags |= FD_CLOEXEC;
	/* Store modified flag word in the descriptor. */
	return fcntl(desc, F_SETFD, oldflags);
}


/**
 * Parse the command line options.
 *
 * @param argc   The command line argument count.
 * @param argv   The command line argument array.
 * @param config The configuration to store parsed option values in.
 */

void parse_options(int argc, char **argv, iexec_config *config) {
  int option_index = 0;

  /* Write default values to the configuration.*/
  config->keep_open = 0;
  config->umask = -1;
  config->use_stdin_file = "/dev/null";
  config->use_stdout_file = "/dev/null";
  config->use_stderr_file = "/dev/null";
  config->use_pid_file = 0;
  config->use_working_dir = 0;
  config->fds_to_close = 0;
  config->num_fds_to_close = 0;
  config->rlimit_soft_nofile = IEXEC_RLIMIT_UNCHANGED;
  config->rlimit_soft_nproc = IEXEC_RLIMIT_UNCHANGED;
  config->rlimit_hard_nofile = IEXEC_RLIMIT_UNCHANGED;
  config->rlimit_hard_nproc = IEXEC_RLIMIT_UNCHANGED;
  while (1) {
    /* Define the long options in a structure array.*/
    static struct option long_options[] = {
      {"help",             no_argument,       0, 'h'},
      {"keep-open",        no_argument,       0, 'k'},
      {"close",            required_argument, 0, 'c'},
      {"pid",              required_argument, 0, 'p'},
      {"rlimit-soft-nofile",   required_argument, 0, IEXEC_OPTION_RLIMIT_SOFT_NOFILE},
      {"rlimit-soft-nproc",required_argument, 0, IEXEC_OPTION_RLIMIT_SOFT_NPROC},
      {"rlimit-hard-nofile",   required_argument, 0, IEXEC_OPTION_RLIMIT_HARD_NOFILE},
      {"rlimit-hard-nproc",required_argument, 0, IEXEC_OPTION_RLIMIT_HARD_NPROC},
      {"stdin",            required_argument, 0, 'i'},
      {"stdout",           required_argument, 0, 'o'},
      {"stderr",           required_argument, 0, 'e'},
      {"umask",            required_argument, 0, IEXEC_OPTION_UMASK},
      {"version",          no_argument,       0, IEXEC_OPTION_VERSION},
      {"working-dir",      required_argument, 0, 'w'},
      {0, 0, 0, 0}
    };
    /* The option index which indicates the next option used. */
    int c = 0;

    /* A temporary pointer to an array of file descriptors. Used
       for safe realloc. */
    int *temp_fd_array = 0;
    
    /* Let's parse the next option. */
    c = getopt_long (argc, argv, "khp:i:o:e:w:c:",
                     long_options, &option_index);
    
    /* If its the end of the options, leave the loop. */
    if (c == -1) {
      break;
    }

    /* Let's display the usage, set a configuration value, or flag
       depending on which option was just parsed. */
    switch (c) {
    case 0:
      /* If this option set a flag, do nothing else now. */
      break;
    case 'k':
      config->keep_open = 1;
      break;
    case 'c':
      /* If the -c option is specified, add the pid to an array. */
      config->num_fds_to_close++;
      temp_fd_array = realloc(config->fds_to_close, sizeof(int) * config->num_fds_to_close);
      /* If there is not enough memory to resize the array, free
         what we have and exit. */
      if (temp_fd_array == 0) {
        error(0, errno, "realloc failed");
        free(config->fds_to_close);
        exit(EXIT_FAILURE);
      }

      /** If the allocation was successful, update the array pointer. */
      config->fds_to_close = temp_fd_array;
      config->fds_to_close[config->num_fds_to_close-1] = atoi(optarg);
      break;
    case 'h':
      usage(0);
      exit(EXIT_SUCCESS);
      break;
    case 'p':
      config->use_pid_file = optarg;
      break;
    case 'i':
      config->use_stdin_file = optarg;
      break;
    case 'o':
      config->use_stdout_file = optarg;
      break;
    case 'e':
      config->use_stderr_file = optarg;
      break;
    case IEXEC_OPTION_UMASK:
      config->umask = atoi(optarg);
      break;
    case IEXEC_OPTION_VERSION:
      version(0);
      exit(EXIT_SUCCESS);
      break;
    case IEXEC_OPTION_RLIMIT_SOFT_NOFILE:
      config->rlimit_soft_nofile = atoi(optarg);
      break;
    case IEXEC_OPTION_RLIMIT_SOFT_NPROC:
      config->rlimit_soft_nproc = atoi(optarg);
      break;
    case IEXEC_OPTION_RLIMIT_HARD_NOFILE:
      config->rlimit_hard_nofile = atoi(optarg);
      break;
    case IEXEC_OPTION_RLIMIT_HARD_NPROC:
      config->rlimit_hard_nproc = atoi(optarg);
      break;
    case 'w':
      config->use_working_dir = optarg;
      break;
    case '?':
      usage(1);
      exit(EXIT_FAILURE);
      /* getopt_long already printed an error message. */
      break;        
    default:
      exit(EXIT_SUCCESS);
    }
  }
  /** The remaining arguments begin after the last iexec option
      and its option argument (if applicable). It should always be positive
      (ie iexec is never included as an argument).*/
  config->remaining_argv = argv + optind;
  config->remaining_argc = argc - optind;
  return;
}

/**
 * The main function.
 *
 * @param argc   The command line argument count.
 * @param argv   The command line argument array.
 */

int main(int argc, char **argv) {
  pid_t child_pid, session_pid;   /* The child pid and session pid. */
  iexec_config config;            /* The configuration. */
  int k;                          /* Looper variable. */

  /** Parse the options into the configuration. */
  parse_options(argc, argv, &config);

  /** If there are no remaining arguments after the options, print an error,
      and exit since a program must be provided. */
  if (config.remaining_argc == 0) {
    error(0, 0, "a program and its arguments are required!");
    exit(EXIT_FAILURE);
  }

  /** Save standard error's file desciptor in case setsid() or execvp() fails. */
  int saved_stderr_fd = 2;

  /** Set the soft or hard limit of the number of file descriptors
      created by the calling process and its children. Unfortunately,
      we can't count the number of file descriptors cumulatively
      across all descendents. */
  if (config.rlimit_soft_nofile != IEXEC_RLIMIT_UNCHANGED
      || config.rlimit_hard_nofile != IEXEC_RLIMIT_UNCHANGED) {
    struct rlimit fdlimit;
    /* Get the current limits. */
    getrlimit(RLIMIT_NOFILE, &fdlimit);

    /* If the soft limit was specified, set it in the structure. */
    if (config.rlimit_soft_nofile != IEXEC_RLIMIT_UNCHANGED) {
      /* Check to see if the soft limit is valid. */
      if (fdlimit.rlim_max != RLIM_INFINITY
          && config.rlimit_soft_nofile > fdlimit.rlim_max) {
        error(0, 0, "file descriptor limit specified with --rlimit-soft-nofile=%d exceeds the hard maximum %d",  config.rlimit_soft_nofile, (int)fdlimit.rlim_max);
        exit(EXIT_FAILURE);
      }

      fdlimit.rlim_cur = config.rlimit_soft_nofile;
    }
    /* If the hard limit was specified, set it in the structure. */
    if (config.rlimit_hard_nofile != IEXEC_RLIMIT_UNCHANGED) {
      fdlimit.rlim_max = config.rlimit_hard_nofile;
    }
    /* If the soft limit option is greater than the hard limit, set
       the soft limit to the hard limit. */
    if (fdlimit.rlim_max != RLIM_INFINITY
        && fdlimit.rlim_cur > fdlimit.rlim_max) {
      fdlimit.rlim_cur = fdlimit.rlim_max;
    }
    setrlimit(RLIMIT_NOFILE, &fdlimit);
  }

  /** Set the hard limit of the number of processes that can be
      running under the effective uid of the calling process. Child
      processes inherit this limit. */
  if (config.rlimit_soft_nproc != IEXEC_RLIMIT_UNCHANGED
      || config.rlimit_hard_nproc != IEXEC_RLIMIT_UNCHANGED) {
    struct rlimit nproclimit;
    getrlimit(RLIMIT_NPROC, &nproclimit);
    /** If the soft limit option is specified, set the corresponding
        value in the structure. */
    if (config.rlimit_soft_nproc != IEXEC_RLIMIT_UNCHANGED) {
      if (nproclimit.rlim_max != RLIM_INFINITY                  /* hard max is not infinite */
          && config.rlimit_soft_nproc > nproclimit.rlim_max     /* soft max option is greater than current hard max. */) {
        error(0, 0, "process count limit specified with --rlimit-soft-nproc=%d exceeds the hard maximum %d",  config.rlimit_soft_nproc, (int)nproclimit.rlim_max);
        exit(EXIT_FAILURE);
      }
      nproclimit.rlim_cur = config.rlimit_soft_nproc;
    }
    /** If the hard limit option is specified, set the corresponding
        value in the structure. */
    if (config.rlimit_hard_nproc != IEXEC_RLIMIT_UNCHANGED) {
      nproclimit.rlim_max = config.rlimit_hard_nproc;
    }
    /* If the soft limit option is greater than the current/new hard
       limit, set the soft limit to the hard limit. */
    if (nproclimit.rlim_max != RLIM_INFINITY
        && nproclimit.rlim_cur > nproclimit.rlim_max) {
      nproclimit.rlim_cur = nproclimit.rlim_max;
    }
    setrlimit(RLIMIT_NPROC, &nproclimit);
  }

  
  /** If the stdin,stderr,stdout file descriptors are not to be kept
      open (ie they are to be closed) then check if the paths provided
      with -i, -o, and -e exist and have the right permissions. */
  if (!config.keep_open) {
    if (access(config.use_stdin_file, R_OK) != 0 && errno != ENOENT) {
      error(0, errno, "file specified with -i (%s) is not readable", config.use_stdin_file);
      exit(EXIT_FAILURE);
    }
    if (access(config.use_stdout_file, W_OK) != 0 && errno != ENOENT) {
      error(0, errno, "file specified with -o (%s) is not writable", config.use_stdout_file);
      exit(EXIT_FAILURE);
    }
    if (access(config.use_stderr_file, W_OK) != 0 && errno != ENOENT) {
      error(0, errno, "file specified with -e (%s) is not writable", config.use_stdout_file);
      exit(EXIT_FAILURE);
    }
    saved_stderr_fd = dup(STDERR_FILENO);
    /** Otherwise, save away standard error in case we need it later. */
    if (saved_stderr_fd >= 0) {
      if (set_cloexec_flag(saved_stderr_fd) != 0) {
        error(0, errno, "cannot arrange for stderr file descriptor to close before execvp()");
      }
    }
  }

  /** Before doing thing else, eg closing/reopening file descriptors,
      change directory to working directory if appropriate. */
  if (config.use_working_dir != 0) {
    if (chdir(config.use_working_dir) < 0) {
      error(0, errno, "unable to change directory to `%s'", config.use_working_dir);
      exit(EXIT_FAILURE);
    }
  }

  /** Close the file descriptor descriptors inherited from
      the parent that were specified with the -C option. */
  for (k = 0; k < config.num_fds_to_close; k++) {
    /** Close the file. */
    int fd_close_status = close(config.fds_to_close[k]);

    /** If the close was not successful, for security
        reasons, exit. */
    if (fd_close_status < 0) {
      error(0, errno, "unable to close file descriptor %d",
            config.fds_to_close[k]);
      exit(EXIT_FAILURE);
    }
  }

  /** If the file descriptors were not supposed to be kept open, close
      them and reopen them according to the options -i, -o, and -e.*/
  if (!config.keep_open) {
    FILE *result = 0;
    /** Try closing standard in. */
    if (close(STDIN_FILENO) < 0) {
      error(0, errno, "unable to close stdin");
      exit(EXIT_FAILURE);
    }
    /** Try closing standard out. */
    if (close(STDOUT_FILENO) < 0) {
      error(0, errno, "unable to close stdout");
      exit(EXIT_FAILURE);
    }
    /** Try closing standard error. */
    if (close(STDERR_FILENO) < 0) {
      if (dup2(saved_stderr_fd, STDERR_FILENO) == STDERR_FILENO) {
        error(0, errno, "failed to close standard error");
      }
      exit(EXIT_FAILURE);
    }
    /** Try reopening standard error input the file specified -i. */
    result = freopen(config.use_stdin_file, "r", stdin);
    if (result == 0) {
      if (dup2(saved_stderr_fd, STDERR_FILENO) == STDERR_FILENO) {
        error(0, errno, "failed to redirect standard input");
      }
      exit(EXIT_FAILURE);
    }
    /** Try reopening standard output using the file specified with -o. */
    result = freopen(config.use_stdout_file, "w", stdout);
    if (result == 0) {
      if (dup2(saved_stderr_fd, STDERR_FILENO) == STDERR_FILENO) {
        error(0, errno, "failed to redirect standard output");
      }
      exit(EXIT_FAILURE);
    }
    /** Try reopening standard error using the file specified with -e. */
    result = freopen(config.use_stderr_file, "w", stderr);
    if (result == 0) {
      if (dup2(saved_stderr_fd, STDERR_FILENO) == STDERR_FILENO) {
        error(0, errno, "failed to redirect standard error");
      }
      exit(EXIT_FAILURE);
    }
  }

  /** Fork to create a child process. */
  child_pid = fork();

  /** If I am the child process, set the mask (if appropriate option is
      set), create a new session with setsid(), open and close file descriptors
      depending on the options set, and then execvp() */
  if (child_pid == 0) {
    /** If the umask option was specified. */
    if (config.umask >= 0) {
      umask(config.umask);
    }
    /** Create a new session and make the child the process group
        leader and session leader. */
    session_pid = setsid();

    /** If there was an error creating the new session, print an error
        and exit. */
    if (session_pid < 0) {
      if (dup2(saved_stderr_fd, STDERR_FILENO) == STDERR_FILENO) {
        error(0, errno, "setsid() failed");
      }
      exit(EXIT_FAILURE);
    }

    /* Run the desired comand in the new session. */
    if (execvp(config.remaining_argv[0], config.remaining_argv) == -1) {
      int saved_errno = errno;
      if (dup2(saved_stderr_fd, STDERR_FILENO) == STDERR_FILENO) {
        error(0, saved_errno, "execvp() on `%s' failed", config.remaining_argv[0]);
      }
      exit(EXIT_FAILURE);
    }
  }

  /** If the forking was unsuccessful, return an error message, and exit. */
  if (child_pid < 0) {
    int saved_errno = errno;
    if (dup2(saved_stderr_fd, STDERR_FILENO) == STDERR_FILENO) {
      error(0, saved_errno, "fork() failed");
    }
    exit(EXIT_FAILURE);
  }

  /** If I am the parent, I have the child's pid. Write it to the pid
      file and forget the pid.*/
  if (child_pid) {

    /** If the pid file was specified, write it to the file.*/
    if (config.use_pid_file != 0) {
      if (access(config.use_pid_file, W_OK) != 0 && errno != ENOENT) {
        int saved_errno = errno;
        if (dup2(saved_stderr_fd, STDERR_FILENO) == STDERR_FILENO) {
          error(0, saved_errno, "file specified with -p (%s) is not writable", config.use_pid_file);
        }
        exit(EXIT_FAILURE);
      }

      FILE *pid_file = fopen(config.use_pid_file, "w");

      /** If an error occurred when trying to open the pid_file, return it. */
      if (pid_file == 0) {
        int saved_errno = errno;
        error(0, saved_errno, "unable to write pid file `%s'", config.use_pid_file);
        exit(EXIT_FAILURE);
      }
      /** Write the pid to the pid file. */
      int nc = fprintf(pid_file, "%d\n", child_pid);
      if (nc == 0) {
        int saved_errno = errno;
        if (dup2(saved_stderr_fd, STDERR_FILENO) == STDERR_FILENO) {
          error(0, saved_errno, "unable to write data to pid file `%s'", config.use_pid_file);
        }
        exit(EXIT_FAILURE);        
      }

      /** Close the pid file. */
      fclose(pid_file);
    }
    /** Forget the pid and exit. */
    child_pid = 0;
    exit(EXIT_SUCCESS);
  }

  /** We should never get here but the compiler expects a return in main(). */
  return 0;
}
