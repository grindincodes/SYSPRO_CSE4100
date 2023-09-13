#include "myshell.h"
#include <errno.h>
#define MAXARGS 128

/*
	project 1 - task 1 (execute command in child process, building my shell)
	create example shell to learn about
	fork and signal, signal handling
	by. Hyeonseok Kang
	project from CSE4100, Prof. Youngjae Kim, Sogang Univ.
*/

// command line parsing and executing
void eval(char *cmdline);
int parseline(char* buf, char* argv[]);
int builtin_command(char* argv[]);


// wrapper functions
void unix_error(char* msg);
pid_t Fork();
int Execve(char* cmd, char** argv);

// signal handlers

void sigint_handler(int sig);
void sigint_handler_parent(int sig);

// sio safe input output
static size_t sio_strlen(char s[]);
ssize_t sio_puts(char s[]); /* Put string */



int main(){
	char cmdline[MAXLINE];	// command line
	
	while (1) {// shell needs to loop to read user command
		printf("CSE4100:P1-myshell> ");
		
		// read the command from stdin (func1)
		if (!fgets(cmdline, MAXLINE, stdin)) unix_error("error in read line");
		if (feof(stdin)) exit(0);
		
		// parse input string to command line args(func2) and
		// execute the command by forking a child process and return (func3)
		// only care about foreground process in task 1.
		eval(cmdline);
		 
	}	
}

// parse user input and execute the command
void eval(char *cmdline) {
	char* argv[MAXARGS]; // argument list for execvp()
	char buf[MAXLINE];   // holds modified command line after parseline()
	int bg;		     // bg flag, bg 1-> background job 0 -> foreground job
	pid_t pid;
	
	strcpy(buf, cmdline);
	bg = parseline(buf, argv);
	if (argv[0] == NULL) return; // ignore an empty line
	
	if (!builtin_command(argv)) {
		// install sig handler before fork(), so that unwanted flow will not happen.
		if (signal(SIGINT, sigint_handler_parent) == SIG_ERR) unix_error("signal install error");	

		if ((pid = Fork()) == 0) {// exit -> exit(0), & -> ignore, the others -> run (builtin_command functionality)
			// child process run the command
			
			// rollback sigint handling
			if (signal(SIGINT, sigint_handler) == SIG_ERR) unix_error("signal install error");

			// execvp to execute command
			if (execvp(argv[0], argv) < 0) unix_error("Command error");	
		}
		// parent code below
		if (!bg) { // foreground case 
			// parent waits for foreground job to terminate
			int status;
			if (waitpid(pid, &status, 0)< 0){
				unix_error("Waitfg: waitpid error");
			}
		}
		else {
			printf("%d %s", pid, cmdline);
		}
		// set back sigint handler to default
                if (signal(SIGINT, sigint_handler) == SIG_ERR) unix_error("signal install error");
	}
	return;
}

// parse the command line and build argv array
int parseline(char* buf, char* argv[]) {
	char* delim;	// it points to first space delimeter
	int argc;	// argument count
	int bg;		// background or not
	
	
	buf[strlen(buf)-1] = ' '; // replace '/n' with ' '
	while (*buf && (*buf==' ')) buf++; // ignore leading spaces
	
	// build argv list
	argc = 0;
	
	while ((delim = strchr(buf, ' '))) {
		argv[argc++] = buf;
		*delim = '\0';
		buf = delim + 1;
		// ignore spaces
		while (*buf && (*buf == ' ')) buf++;
		
		// if " or ' is included, include space character and ignore ' or "
		if ((*buf == '\"') || (*buf == '\'')) {
			do {
				char temp = *buf;
				argv[argc++] = ++buf; // ignore '
				delim = strchr(buf, temp); // find next " or '
				*(delim) = '\0';	// replace ' or " with null character
				buf = delim + 2;
				while (*buf && (*buf == ' ')) buf++;
			} while ((*buf == '\"') || (*buf == '\''));
		}
	}
	argv[argc] = NULL;
	
	if (argc == 0) return 1; // ignore blank line -> return 1  
	
	// bg or fg
	if ((bg = (*argv[argc-1] == '&')) != 0)
		// if it is bg case
		argv[--argc] = NULL;
	
	return bg;
}

// check if the first argument is a builtin command, and if it is, run it and return 1
int builtin_command(char **argv){
	if (!strcmp(argv[0], "exit"))	exit(0); // exit the program
	
	if (!strcmp(argv[0], "&")) return 1; 	 // ignore singletone &
	
	if (!strcmp(argv[0], "cd")) {
		
		if (chdir(argv[1]) < 0)
			fprintf(stderr, "%s\n", strerror(errno));
		return 1;
	}
	return 0; // not a builtin command
}

// unix error print 
void unix_error(char *msg) /* Unix-style error */
{
    fprintf(stderr, "%s: %s\n", msg, strerror(errno));
    exit(0);
}

// fork wrapper function Fork
pid_t Fork(){
	pid_t pid;
	
	if ((pid=fork()) < 0)
		unix_error("Fork error");
	return pid;
}

int Execve(char* cmd, char ** argv) {
	 if (execve(cmd, argv, environ) < 0) {
         	printf("%s: Command not found.\n", argv[0]);
                exit(0);
         }
}

// default SIGINT handler
void sigint_handler(int sig){
	// sio_puts("signal caught");
	sio_puts("\n");
	_exit(0);
}

// temporary SIGINT handler for parent to ignore ctrl+c while child process is on running state.
void sigint_handler_parent(int sig){
	// printf("%d",getpid());
        // sio_puts("signal caught");
        sio_puts("\n");
}


// sio
static size_t sio_strlen(char s[])
{
    int i = 0;

    while (s[i] != '\0')
        ++i;
    return i;
}
// sio
ssize_t sio_puts(char s[]) /* Put string */
{
    return write(STDOUT_FILENO, s, sio_strlen(s)); //line:csapp:siostrlen
}

