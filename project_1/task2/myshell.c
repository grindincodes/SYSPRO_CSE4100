#include "myshell.h"
#include <errno.h>
#define MAXARGS 128

/*
 	project 1 - task 2
	create example shell to learn about
	fork and signal, signal handling
	by. Hyeonseok Kang
	project from CSE4100, Prof. Youngjae Kim, Sogang Univ.
*/

// command line parsing and executing
void eval(char *cmdline, int cmdnum, int (*fd)[2]);
int parseline(char* buf, char* argv[]);
int builtin_command(char* argv[]);
void parsepipe(char* cmdline, char** cmdptr);


// wrapper functions
void unix_error(char* msg);
pid_t Fork();
int Execve(char* cmd, char** argv);
int Dup2(int fd1, int fd2);

// signal handlers
void sigint_handler(int sig); // SIGINT
void sigint_handler_parent(int sig);

// sio safe input output
static size_t sio_strlen(char s[]);
ssize_t sio_puts(char s[]); /* Put string */


int main(){
	char cmdline[MAXLINE];	// command line
	char* cmdptr[MAXPIPE]; // command pointer for parsing pipelined cmdline
	int fd[MAXPIPE][2];
	
	while (1) {// shell needs to loop to read user command
				
		printf("CSE4100:P1-myshell> ");
		// read the command from stdin (func1)
		if (!fgets(cmdline, MAXLINE, stdin)) unix_error("error in read line");
		if (feof(stdin)) exit(0);
		
		// parse input string to command line args(func2) and
		// execute the command by forking a child process and return (func3)
		// ~ task 2 pipeline
		// if there are pipelines, replace them with \0 so that we can pass each pointer for each command line
		parsepipe(cmdline, cmdptr);
		// cmdcnt default-> 1, cmdcnt++ for each pipeline
		
		
		// make all pipelines as many as needed (10 at max)
		if (cmdcnt > 11) unix_error("pipes can be at most 10.");
		for (int i=0; i<cmdcnt-1; i++){
			if (pipe(fd[i]) < 0) {  // open pipe
				unix_error("PIPE error");
				exit(1);
			}
		}
		for (int i=0; i<cmdcnt; i++) {
			eval(cmdptr[i], i+1, fd);  // if  cmdcnt > 1, manipulate fd with dup() function, to pipeline commands
		}
		
	}	
}

// parse user input and execute the command
void eval(char *cmdline, int cmdnum, int (*fd)[2]) {
	// cmdnum used for call count
	char* argv[MAXARGS]; // argument list for execvp()
	char buf[MAXLINE];   // holds modified command line after parseline()
	int bg;		     // bg flag, bg 1-> background job 0 -> foreground job
	pid_t pid;
	
	strcpy(buf, cmdline);
	bg = parseline(buf, argv);   // parse line and build args, it returns whether bg or not
	if (argv[0] == NULL) return; // ignore an empty line

	if (!builtin_command(argv)) {	//quit -> exit(0), & -> ignore, etc... -> run (builtin_command functionality)	
		// install sig handler before fork(), so that unwanted flow will not happen.
		if (signal(SIGINT, sigint_handler_parent) == SIG_ERR) unix_error("signal install error");

		if ((pid = Fork()) == 0) {
			// child process run the command
			
			// rollback sigint handling
			if (signal(SIGINT, sigint_handler) == SIG_ERR) unix_error("signal install error");	

			/* if it is pipelining, manuplate child fd with dup2(oldfd, newfd)
			dup2 -> file discriptor newfd is adjusted so that it refers to the same open fd as oldfd.
			If newfd was open, it is first closed and reused. (... on linux man page)
		       	Here, do things such as set stdout to pipe set stdin from pipe
			pipe fd[..][0] -> read end, fd[..][1] -> write end */
			if (cmdcnt > 1){
				if (cmdnum == 1) { // the first child
					Dup2(fd[0][1], STDOUT_FILENO); // set stdout of first command to pipe1
				}
				else if (cmdnum < cmdcnt) { // middle ones
					Dup2(fd[cmdnum-2][0], STDIN_FILENO);  // set stdin come from the pipe
					Dup2(fd[cmdnum-1][1], STDOUT_FILENO); // set stdout go to the pipe
				}
				else { // the last one
					Dup2(fd[cmdnum-2][0], STDIN_FILENO);  // set stdin come from pipe
				}
			}
			// close all fd 		
			// ... Or we can implement it like using pipe() as needed and closing fd as not needed ->  more simple code
	                for (int i=0; i<cmdcnt-1; i++){
        	                close(fd[i][0]);
				close(fd[i][1]);
               		}
			// execvp to execute command
			if (execvp(argv[0], argv) < 0)
				unix_error("Command error");
		}	
		
		// parent waits for foreground job to terminate
		// in terms of pipelining(task2), parent needs to wait last child process 
		if (cmdcnt == 1){
			// no pipeline case
			if (!bg) { // foreground case
				int status;
				if (waitpid(pid, &status, 0)< 0){
					unix_error("Waitfg: waitpid error");
				}
			}
			else {	// bg
				printf("%d %s", pid, cmdline);
			}
			// set signal handler back to default (after child is terminated)
                        if (signal(SIGINT, sigint_handler) == SIG_ERR) unix_error("signal install error");
		}
		else {	
			// pipeline case
			if (cmdnum == cmdcnt) { // on the last function call (after fork last child)
				int status;
				// close all parent fd (needed no more and to avoid suspending(hanging) caused by open fd)
				for (int i=0; i<cmdcnt-1; i++){
                               		close(fd[i][0]);
					close(fd[i][1]);
                        	}
				
				if (!bg) { // if the last command is fg
					// wait for the last child and reaping
	                                if (waitpid(pid, &status, 0) < 0) unix_error("Waitfg: waitpid error");
        	                        for (int i=0; i<cmdcnt-1; i++) wait(NULL);      // reap all remaining children
				}
				else { // bg
					printf("%d %s", pid, cmdline);
				}
				// set signal handler back to default (after child is terminated)
	                        if (signal(SIGINT, sigint_handler) == SIG_ERR) unix_error("signal install error");
			}
		}
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

// SIGINT handler for parent in foreground case (before reaping)
// if user put 'cat' command alone, it seems that the parent process executes text echoing...?
// (same pid as myshell) so it needs another sigint_handler 
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

// ~ task 2 ~
// find pipelines and replace them with \0 character and count total cmdcnt
void parsepipe(char * cmdline, char ** cmdptr){
	cmdcnt = 1;		// default command count	
	cmdptr[0] = cmdline;	// first command

	if (!strchr(cmdline, '|')) return; // no pipeline

	// default command count = 1
	char* buf = cmdline;
	// find |
	while (buf = strchr(buf, '|')){
		// if there is, replace it and step forward by 1
		*buf = '\0';
		buf++;
		cmdptr[cmdcnt] = buf;
		cmdcnt++;
	}
}
// dup2 wrapper function
int Dup2(int fd1, int fd2){
	int rc;
	if ((rc=dup2(fd1,fd2)) < 0) unix_error("dup2 error");
	return rc;
}
