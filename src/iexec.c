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
#include <sys/types.h>
#include <pwd.h>
#include "iexec-help.h"
#include "iexec-help-nontty.h"

#define IEXEC_VERSION "1.1"

#define IEXEC_OPTION_UMASK 7001
#define IEXEC_OPTION_VERSION 7002

#define IEXEC_OPTION_RLIMIT_SOFT 8000
#define IEXEC_OPTION_RLIMIT_HARD 9000

#define IEXEC_RLIMIT_UNCHANGED -2

/** A string name for each limit constant (indexed by constant). */
const char *limit_names[] = {
  [RLIMIT_CPU] = "RLIMIT_CPU",
  [RLIMIT_FSIZE] = "RLIMIT_FSIZE",
  [RLIMIT_DATA] = "RLIMIT_DATA",
  [RLIMIT_STACK] = "RLIMIT_STACK",
  [RLIMIT_CORE] = "RLIMIT_CORE",
  [RLIMIT_RSS] = "RLIMIT_RSS",
  [RLIMIT_NOFILE] = "RLIMIT_NOFILE",
  [RLIMIT_AS] = "RLIMIT_AS",
  [RLIMIT_NPROC] = "RLIMIT_NPROC",
  [RLIMIT_MEMLOCK] = "RLIMIT_MEMLOCK",
  [RLIMIT_LOCKS] = "RLIMIT_LOCKS",
  [RLIMIT_SIGPENDING] = "RLIMIT_SIGPENDING",
  [RLIMIT_MSGQUEUE] = "RLIMIT_MSGQUEUE",
  [RLIMIT_NICE] = "RLIMIT_NICE",
  [RLIMIT_RTPRIO] = "RLIMIT_RTPRIO"
};

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
  /* Values to set soft resource limits for setrlimit/getrlimit. */
  long soft_limits[RLIMIT_NLIMITS];
  /* Values to set hard resource limits for setrlimit/getrlimit. */
  long hard_limits[RLIMIT_NLIMITS];

  char *username;       /** The effective uid to run as. 0 means do not change it. */
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
 * Changes to a different user.
 */

void iexec_change_user(const char *user) {
  struct passwd *pw = getpwnam(user);
  if (pw == 0) {
    error(0, errno, "could not find user %s", user);
    exit(EXIT_FAILURE);
  }
  if (setuid(pw->pw_uid)) {
    error(0, errno, "could not change to different user");
    exit(EXIT_FAILURE);
  }
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
 * Sets values in the iexec_config struct to sane values.
 *
 * /dev/null is used for standard input, output, and error.
 *
 * The working directory is left unchanged. No pid file is
 * used. No resource limits are changed.
 */
void iexec_config_set_default_values(iexec_config *config) {
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
  config->username = 0;
  for (int i = 0; i < RLIMIT_NLIMITS; i++) {
    config->soft_limits[i] = IEXEC_RLIMIT_UNCHANGED;
    config->hard_limits[i] = IEXEC_RLIMIT_UNCHANGED;
  }
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
  iexec_config_set_default_values(config);
  while (1) {
    /* Define the long options in a structure array.*/
    static struct option long_options[] = {
      {"close",                 required_argument, 0, 'c'},
      {"help",                  no_argument,       0, 'h'},
      {"keep-open",             no_argument,       0, 'k'},
      {"pid",                   required_argument, 0, 'p'},
      {"rlimit-cpu-hard",       required_argument, 0, IEXEC_OPTION_RLIMIT_HARD + RLIMIT_CPU},
      {"rlimit-fsize-hard",     required_argument, 0, IEXEC_OPTION_RLIMIT_HARD + RLIMIT_FSIZE},
      {"rlimit-data-hard",      required_argument, 0, IEXEC_OPTION_RLIMIT_HARD + RLIMIT_DATA},
      {"rlimit-stack-hard",     required_argument, 0, IEXEC_OPTION_RLIMIT_HARD + RLIMIT_STACK},
      {"rlimit-core-hard",      required_argument, 0, IEXEC_OPTION_RLIMIT_HARD + RLIMIT_CORE},
      {"rlimit-rss-hard",       required_argument, 0, IEXEC_OPTION_RLIMIT_HARD + RLIMIT_RSS},
      {"rlimit-nofile-hard",    required_argument, 0, IEXEC_OPTION_RLIMIT_HARD + RLIMIT_NOFILE},
      {"rlimit-nproc-hard",     required_argument, 0, IEXEC_OPTION_RLIMIT_HARD + RLIMIT_NPROC},
      {"rlimit-memlock-hard",   required_argument, 0, IEXEC_OPTION_RLIMIT_HARD + RLIMIT_MEMLOCK},
      {"rlimit-locks-hard",     required_argument, 0, IEXEC_OPTION_RLIMIT_HARD + RLIMIT_LOCKS},
      {"rlimit-sigpending-hard",required_argument, 0, IEXEC_OPTION_RLIMIT_HARD + RLIMIT_SIGPENDING},
      {"rlimit-msgqueue-hard",  required_argument, 0, IEXEC_OPTION_RLIMIT_HARD + RLIMIT_MSGQUEUE},
      {"rlimit-nice-hard",      required_argument, 0, IEXEC_OPTION_RLIMIT_HARD + RLIMIT_NICE},
      {"rlimit-rtprio-hard",    required_argument, 0, IEXEC_OPTION_RLIMIT_HARD + RLIMIT_RTPRIO},
      {"rlimit-cpu-soft",       required_argument, 0, IEXEC_OPTION_RLIMIT_SOFT + RLIMIT_CPU},
      {"rlimit-fsize-soft",     required_argument, 0, IEXEC_OPTION_RLIMIT_SOFT + RLIMIT_FSIZE},
      {"rlimit-data-soft",      required_argument, 0, IEXEC_OPTION_RLIMIT_SOFT + RLIMIT_DATA},
      {"rlimit-stack-soft",     required_argument, 0, IEXEC_OPTION_RLIMIT_SOFT + RLIMIT_STACK},
      {"rlimit-core-soft",      required_argument, 0, IEXEC_OPTION_RLIMIT_SOFT + RLIMIT_CORE},
      {"rlimit-rss-soft",       required_argument, 0, IEXEC_OPTION_RLIMIT_SOFT + RLIMIT_RSS},
      {"rlimit-nofile-soft",    required_argument, 0, IEXEC_OPTION_RLIMIT_SOFT + RLIMIT_NOFILE},
      {"rlimit-nproc-soft",     required_argument, 0, IEXEC_OPTION_RLIMIT_SOFT + RLIMIT_NPROC},
      {"rlimit-memlock-soft",   required_argument, 0, IEXEC_OPTION_RLIMIT_SOFT + RLIMIT_MEMLOCK},
      {"rlimit-locks-soft",     required_argument, 0, IEXEC_OPTION_RLIMIT_SOFT + RLIMIT_LOCKS},
      {"rlimit-sigpending-soft",required_argument, 0, IEXEC_OPTION_RLIMIT_SOFT + RLIMIT_SIGPENDING},
      {"rlimit-msgqueue-soft",  required_argument, 0, IEXEC_OPTION_RLIMIT_SOFT + RLIMIT_MSGQUEUE},
      {"rlimit-nice-soft",      required_argument, 0, IEXEC_OPTION_RLIMIT_SOFT + RLIMIT_NICE},
      {"rlimit-rtprio-soft",    required_argument, 0, IEXEC_OPTION_RLIMIT_SOFT + RLIMIT_RTPRIO},
      {"stdin",                 required_argument, 0, 'i'},
      {"stdout",                required_argument, 0, 'o'},
      {"stderr",                required_argument, 0, 'e'},
      {"umask",                 required_argument, 0, IEXEC_OPTION_UMASK},
      {"user",                  required_argument, 0, 'u'},
      {"version",               no_argument,       0, IEXEC_OPTION_VERSION},
      {"working-dir",           required_argument, 0, 'w'},
      {0, 0, 0, 0}
    };
    /* The option index which indicates the next option used. */
    int c = 0;

    /* A temporary pointer to an array of file descriptors. Used
       for safe realloc. */
    int *temp_fd_array = 0;
    
    /* Let's parse the next option. */
    c = getopt_long (argc, argv, "khp:i:o:e:w:c:u:",
                     long_options, &option_index);
    
    /* If its the end of the options, leave the loop. */
    if (c == -1) {
      break;
    }

    /* If a soft limit was specified, change the corresponding value in the soft_limits
     array.*/
    if (c >= IEXEC_OPTION_RLIMIT_SOFT && c < IEXEC_OPTION_RLIMIT_SOFT + RLIMIT_NLIMITS) {
      int limit_number = c - IEXEC_OPTION_RLIMIT_SOFT;
      int limit_value = atoi(optarg);
#ifdef IEXEC_DEBUG
      printf("setting %s_SOFT=%d\n", limit_names[limit_number], limit_value);
#endif
      config->soft_limits[limit_number] = limit_value;
      continue;
    }
    /* If a hard limit was specified, change the corresponding value in the hard_limits
     array.*/
    if (c >= IEXEC_OPTION_RLIMIT_HARD && c < IEXEC_OPTION_RLIMIT_HARD + RLIMIT_NLIMITS) {
      int limit_number = c - IEXEC_OPTION_RLIMIT_HARD;
      int limit_value = atoi(optarg);
#ifdef IEXEC_DEBUG
      printf("setting %s_HARD=%d\n", limit_names[limit_number], limit_value);
#endif
      config->hard_limits[limit_number] = limit_value;
      continue;
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
    case 'u':
      {
        int len = strnlen(optarg,255);
        config->username = malloc(len+1);
        strncpy(config->username, optarg, len);
        if (config->username == 0) {
          error(0, errno, "could not find space for username");
        }
      }
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
 * If the first file does not exist, it is first created.
 *
 * Returns a negative value if an error occured, 0 if the filenames refer to different
 * files and 1 if they refer to the same file.
 */
int same_file(const char *fn1, const char *fn2) {
  struct stat s1;
  struct stat s2;
  int r1 = stat(fn1, &s1);
  if (r1 < 0) {
    /** If the file does not exist, create it because it will be used
        anyway. */
    if (errno == ENOENT) {
      int tresult = creat(fn1, 0666);
      if (tresult < 0) {
        return tresult;
      }
      close(tresult);
      /** Now try stat() again. */
      r1 = stat(fn1, &s1);
    } else { /* An error other than ENOENT occured, propogate it downward. */
      return r1;
    }
  }
  /* Now try the stat on the second file. */
  int r2 = stat(fn2, &s2);
  if (r2 < 0) {
    return r2;
  }
  /* The files are the same iff their device and inode are the same. */
  return (s1.st_dev == s2.st_dev && s1.st_ino == s2.st_ino) ? 1 : 0; 
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

  /** For each resource limit, */
  for (int i = 0; i < RLIMIT_NLIMITS; i++) {
    /* If the soft_limit or hard_limit is specified as an option, process it. */
    const int soft_limit = config.soft_limits[i];
    const int hard_limit = config.hard_limits[i];
    if (soft_limit > IEXEC_RLIMIT_UNCHANGED || hard_limit > IEXEC_RLIMIT_UNCHANGED) {
      struct rlimit current_limit;
      getrlimit(i, &current_limit);
      /* If the soft limit was specified, check for validity, and then set it in the structure. */
      if (soft_limit > IEXEC_RLIMIT_UNCHANGED) {
        /* Check to see if the soft limit is valid. */
        if (current_limit.rlim_max != RLIM_INFINITY
            && soft_limit > current_limit.rlim_max) {
          error(0, 0, "specified %s_SOFT=%d exceeds %s_HARD=%d",
                limit_names[i], soft_limit, limit_names[i], (int)current_limit.rlim_max);
          exit(EXIT_FAILURE);
        }
        current_limit.rlim_cur = soft_limit;
      }
      /* If the hard limit was specified, set it in the structure. */
      if (hard_limit > IEXEC_RLIMIT_UNCHANGED) {
        current_limit.rlim_max = hard_limit;
      }
      /* If the soft limit option is greater than the hard limit, set
         the soft limit to the hard limit. */
      if (current_limit.rlim_max != RLIM_INFINITY
          && current_limit.rlim_cur > current_limit.rlim_max) {
        current_limit.rlim_cur = current_limit.rlim_max;
      }
      if (setrlimit(i, &current_limit) < 0) {
        if (soft_limit != IEXEC_RLIMIT_UNCHANGED) {
          error(0, errno, "error setting resource limit %s_SOFT=%d", limit_names[i], soft_limit);
        }
        if (hard_limit != IEXEC_RLIMIT_UNCHANGED) {
          error(0, errno, "error setting resource limit %s_HARD=%d", limit_names[i], hard_limit);
        }
        exit(EXIT_FAILURE);
      }
    }
  }

  /** Change the effective user id. */
  if (config.username != 0) {
    iexec_change_user(config.username);
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
    /** Try reopening standard input using the file specified -i. */
    result = freopen(config.use_stdin_file, "r", stdin);
    if (result == 0) {
      if (dup2(saved_stderr_fd, STDERR_FILENO) == STDERR_FILENO) {
        error(0, errno, "failed to redirect standard input");
      }
      exit(EXIT_FAILURE);
    }
    /** If both the standard output and standard error refer to the same
         file, truncate the file and use append mode in the later freopen
         calls. Otherwise, use write with truncate*/
    int outerr_same = same_file(config.use_stdout_file, config.use_stderr_file);
    const char *open_mode = "a";
    if (outerr_same < 0) { /*** An error occurred. */
      if (errno != ENOENT) {
        if (dup2(saved_stderr_fd, STDERR_FILENO) == STDERR_FILENO) {
          error(0, errno, "stat() error");
        }
        exit(EXIT_FAILURE);
      }
      open_mode = "w";
    } else if (outerr_same == 0) { /*** DIFFERENT: use write with truncate mode. */
      open_mode = "w";
    }
    /** Try reopening standard output using the file specified with -o. */
    result = freopen(config.use_stdout_file, open_mode, stdout);
    if (result == 0) {
      if (dup2(saved_stderr_fd, STDERR_FILENO) == STDERR_FILENO) {
        error(0, errno, "failed to redirect standard output");
      }
      exit(EXIT_FAILURE);
    }
    else if (outerr_same == 1) { /** Truncate the file. */
      if (ftruncate(1, 0) < 0) {
        if (dup2(saved_stderr_fd, STDERR_FILENO) == STDERR_FILENO) {
          error(0, errno, "truncate() error");
        }
      }
    }

    /** Try reopening standard error using the file specified with -e. */
    result = freopen(config.use_stderr_file, open_mode, stderr);
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
