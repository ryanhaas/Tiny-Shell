/*
 * tsh - A tiny shell program
 *
 * Filename   : tsh.c
 * Author     : Ryan Haas
 * Course     : CSCI 380.01
 * Assignment : Program 2, Shell Lab
 * Description: Implement a basic shell program capable of forking off and running
 *              processes/programs and intercepting signals and passing those signals
 *              to the process. Capable of running jobs in the background and having
 *              one suspended process.
 *
 * I handled signals by pretty much having the SIGCHLD handler do all of the work. The other
 * SIGINT and SIGTSTP signal handlers basically just intercepted the signals and sent them to 
 * the running process, which the SIGCHLD handler would then deal with. Firstly I had the SIGCHLD 
 * handler save the errno to in the event it got set during execution, and then reset errno to the
 * one I saved. Then I waited on any child and looped until waitpid didn't detect a change anymore. I
 * passed WNOHANG and WUNTRACED. I obviously passed WNOHANG so it wouldn't stall the shell if the
 * process was meant to run in the background (as the stalling would be done in waitfg). I also passes 
 * WUNTRACED so the child handler would be able to detect changes to processes that got stopped by SIGTSTP. I 
 * then outputed the correct/relevant information. I waited for a foreground job by essentially infinitely
 * looping until a change in the child was detected using sigsuspend.
 */

/*
 *******************************************************************************
 * INCLUDE DIRECTIVES
 *******************************************************************************
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 *******************************************************************************
 * TYPE DEFINITIONS
 *******************************************************************************
 */

typedef void handler_t (int);

/*
 *******************************************************************************
 * PREPROCESSOR DEFINITIONS
 *******************************************************************************
 */

// max line size 
#define MAXLINE 1024 
// max args on a command line 
#define MAXARGS 128

/*
 *******************************************************************************
 * GLOBAL VARIABLES
 *******************************************************************************
 */

// defined in libc
extern char** environ;   

// command line prompt 
char prompt[] = "tsh> ";

// for composing sprintf messages
char sbuf[MAXLINE];

// PID of the foreground process
volatile pid_t g_runningPid = 0;
// PID of the suspended process
volatile pid_t g_suspendedPid = 0; 

// Signal blockers
sigset_t mask;
sigset_t prev;

/*
 *******************************************************************************
 * FUNCTION PROTOTYPES
 *******************************************************************************
 */

int
parseline (const char* cmdline, char**argv);

void
sigint_handler (int sig);

void
sigtstp_handler (int sig);

void
sigchld_handler (int sig);

void
sigquit_handler (int sig);

void
unix_error (char* msg);

void
app_error (char* msg);

handler_t*
Signal (int signum, handler_t* handler);

void
eval(const char* input);

int
builtin_cmd(char** args);

void
waitfg(const pid_t pid);

pid_t
Fork();

/*
 *******************************************************************************
 * MAIN
 *******************************************************************************
 */

int
main (int argc, char** argv)
{
  /* Redirect stderr to stdout */
  dup2 (1, 2);

  /* Install signal handlers */
  Signal (SIGINT, sigint_handler);   /* ctrl-c */
  Signal (SIGTSTP, sigtstp_handler); /* ctrl-z */
  Signal (SIGCHLD, sigchld_handler); /* Terminated or stopped child */
  Signal (SIGQUIT, sigquit_handler); /* quit */

  /* TODO -- shell goes here*/

  // Continuously loop until error or got quit command
  while(1)
  {

    // Create char array for input
    char input[MAXLINE];

    // Check if input is EOF/ctrl-d
    if(feof(stdin))
      break;

    // Get input from user and evaluate
    printf(prompt);

    // Make sure fgets is not null
    if(fgets(input, MAXLINE, stdin) == NULL)
      break;

    eval(input);

    // Flush output
    fflush(stdout);
  }

  /* Quit */
  exit (0);
}

/*
 * eval - Evaluate the user's input
 * 
 * Return 0 if quit command was received
 */
void
eval(const char* input)
{
  // Create array to hold arguments and parse input
  char* args[MAXARGS];
  int bg = parseline(input, args);

  // Check if line is blank
  if(args[0] == NULL)
    return;

  // Check if command is built in, if so then run command
  //    from builtin_cmd and continue loop
  if(builtin_cmd(args))
    return;

  // Block SIGCHLD
  sigemptyset(&mask);
  sigaddset(&mask, SIGCHLD);
  sigprocmask(SIG_BLOCK, &mask, &prev);

  // If command is not builtin then fork a new process and wait
  g_runningPid = Fork();

  // Check if this is the child process
  if(g_runningPid == 0)
  {
    // Set process group
    setpgid(0, 0);

    // Unblock SIGCHLD
    sigprocmask(SIG_SETMASK, &prev, NULL);

    // Run the given command
    int ret = execve(args[0], args, environ);
    // Check if exec failed
    if(ret < 0)
    {
      fprintf(stderr, "%s: Command not found\n", *args);
      exit(1);
    }
  }

  // Unblock signal
  sigprocmask(SIG_SETMASK, &prev, NULL);

  // Wait for the process to finish if it is not running in the background
  if(!bg)
    waitfg(g_runningPid);
  else {
    printf("(%d) %s", g_runningPid, input);
    fflush(stdout);
  }
}

/*
 * builtin_cmd - Check if the entered command is built in, if it is
 *      then run the built in command.
 *
 * Return 1 if command is built in, 0 otherwise
 */
int
builtin_cmd(char** args)
{
  // Check if received exit command
  if(strcmp(args[0], "quit") == 0)
    exit(0);

  // Check if received fg command
  if(strcmp(args[0], "fg") == 0)
  {
    // Check to make sure there is a suspended process
    if(g_suspendedPid > 0)
    {
      // Set the running process equal to the suspended and reset the suspended PID
      g_runningPid = g_suspendedPid;
      g_suspendedPid = 0;

      // Continue bg process
      kill(-g_runningPid, SIGCONT);
      waitfg(g_runningPid);
    }

    return 1;
  }

  return 0;
}

/*
 * Fork - fork a new process and check if an error occurred
 *
 * Returns the PID of the child process
 */
pid_t
Fork()
{
  // Fork the process
  pid_t child = fork();

  // Check if the fork failed
  if(child < 0)
  {
    fprintf(stderr, "fork error (%s) -- quitting\n", strerror(errno));
    exit(-1);
  }

  return child;
}

/*
 * waitfg - Wait for the given processes completion
 */
void
waitfg(const pid_t pid)
{
  // Wait until a signal is received
  while(g_runningPid != 0)
  {
    sigsuspend(&prev);
  }
}

/*
*  parseline - Parse the command line and build the argv array.
*
*  Characters enclosed in single quotes are treated as a single
*  argument.
*
*  Returns true if the user has requested a BG job, false if
*  the user has requested a FG job.
*/
int
parseline (const char* cmdline, char** argv)
{
  static char array[MAXLINE]; /* holds local copy of command line*/
  char* buf = array;          /* ptr that traverses command line*/
  char* delim;                /* points to first space delimiter*/
  int argc;                   /* number of args*/
  int bg;                     /* background job?*/

  strcpy (buf, cmdline);
  buf[strlen (buf) - 1] = ' ';  /* replace trailing '\n' with space*/
  while (*buf && (*buf == ' ')) /* ignore leading spaces*/
    buf++;

  /* Build the argv list*/
  argc = 0;
  if (*buf == '\'')
  {
    buf++;
    delim = strchr (buf, '\'');
  }
  else
  {
    delim = strchr (buf, ' ');
  }

  while (delim)
  {
    argv[argc++] = buf;
    *delim = '\0';
    buf = delim + 1;
    while (*buf && (*buf == ' ')) /* ignore spaces*/
      buf++;

    if (*buf == '\'')
    {
      buf++;
      delim = strchr (buf, '\'');
    }
    else
    {
      delim = strchr (buf, ' ');
    }
  }
  argv[argc] = NULL;

  if (argc == 0) /* ignore blank line*/
    return 1;

  /* should the job run in the background?*/
  if ((bg = (*argv[argc - 1] == '&')) != 0)
  {
    argv[--argc] = NULL;
  }
  return bg;
}

/*
 *******************************************************************************
 * SIGNAL HANDLERS
 *******************************************************************************
 */

/*
*  sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
*      a child job terminates (becomes a zombie), or stops because it
*      received a SIGSTOP or SIGTSTP signal. The handler reaps all
*      available zombie children, but doesn't wait for any other
*      currently running children to terminate.
*/
void
sigchld_handler (int sig)
{
  // Preserve the errno
  int olderr = errno;

  // Store status and PID information
  int wstatus;
  pid_t waitID;

  // Loop indefinitely to check for zombie children and reap
  while((waitID = waitpid(-1, &wstatus, WNOHANG|WUNTRACED)) > 0)
  {
    // Make sure process exited normally, if not then inform
    if(WIFSIGNALED(wstatus)) {
      printf("Job (%d) terminated by signal %d\n", waitID, WTERMSIG(wstatus));
      fflush(stdout);
    }
    else if(WIFSTOPPED(wstatus)) {
      printf("Job (%d) stopped by signal %d\n", waitID, WSTOPSIG(wstatus));
      fflush(stdout);
      g_suspendedPid = g_runningPid;
    }

    // Reset running PID
    g_runningPid = 0;
  }

  // Reset errno
  errno = olderr;

  return;
}

/*
 *  sigint_handler - The kernel sends a SIGINT to the shell whenever the
 *     user types ctrl-c at the keyboard.  Catch it and send it along
 *     to the foreground job.
 */
void
sigint_handler (int sig)
{
  // Make sure there is a foreground job
  if(g_runningPid > 0)
  {
    // Send SIGINT to foreground process
    kill(-g_runningPid, SIGINT);
  }

  return;
}

/*
 *  sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *      the user types ctrl-z at the keyboard. Catch it and suspend the
 *      foreground job by sending it a SIGTSTP.
 */
void
sigtstp_handler (int sig)
{
  // Make sure there is a foreground job
  if(g_runningPid > 0)
  {
    // Send SIGTSTP to foreground
    kill(-g_runningPid, SIGTSTP);
  }

  return;
}

/*
 *******************************************************************************
 * HELPER ROUTINES
 *******************************************************************************
 */


/*
 * unix_error - unix-style error routine
 */
void
unix_error (char* msg)
{
  fprintf (stdout, "%s: %s\n", msg, strerror (errno));
  exit (1);
}

/*
*  app_error - application-style error routine
*/
void
app_error (char* msg)
{
  fprintf (stdout, "%s\n", msg);
  exit (1);
}

/*
*  Signal - wrapper for the sigaction function
*/
handler_t*
Signal (int signum, handler_t* handler)
{
  struct sigaction action, old_action;

  action.sa_handler = handler;
  sigemptyset (&action.sa_mask); /* block sigs of type being handled*/
  action.sa_flags = SA_RESTART;  /* restart syscalls if possible*/

  if (sigaction (signum, &action, &old_action) < 0)
    unix_error ("Signal error");
  return (old_action.sa_handler);
}

/*
*  sigquit_handler - The driver program can gracefully terminate the
*     child shell by sending it a SIGQUIT signal.
*/
void
sigquit_handler (int sig)
{
  printf ("Terminating after receipt of SIGQUIT signal\n");
  exit (1);
}
