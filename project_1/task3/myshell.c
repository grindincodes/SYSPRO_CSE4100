#include "myshell.h"
#include <errno.h>
#define MAXARGS 128

/*
 	project 1 - task 3 (run process background and signal handling, reaping)
	create example shell to learn about
	fork and signal, signal handling
	make job list, add, update(stopped), delete(killed -9), read(list)
	job list contains : pid, status, bg(0 or 1)
	have to carefully reap fg and bg
	by. Hyeonseok Kang
	project from CSE4100, Prof. Youngjae Kim, Sogang Univ.
*/

// command line parsing and executing
void eval(char *cmdline, int cmdnum, int (*fd)[2]);
int parseline(char* buf, char* argv[], int cmdnum);
int builtin_command(char* argv[]);
void parsepipe(char* cmdline, char** cmdptr);

// wrapper functions
void unix_error(char* msg);
pid_t Fork();
int Execve(char* cmd, char** argv);
int Dup2(int fd1, int fd2);
void Sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
void Sigemptyset(sigset_t *set);
void Sigaddset(sigset_t *set, int signum);
void Sigfillset(sigset_t *set);
void Sigdelset(sigset_t *set, int signum);

// signal handlers
void sigint_handler(int sig); // SIGINT
void sigint_handler_parent(int sig);
void sigchld_handler(int sig); // SIGCHLD
void sigtstp_handler(int sig); // ctrlz handler

// sio safe input output
static size_t sio_strlen(char s[]);
static void sio_reverse(char s[]);
ssize_t sio_puts(char s[]); /* Put string */
ssize_t sio_putl(long v);
// Sio: sio wrapper
ssize_t Sio_puts(char s[]);
void Sio_error(char s[]);
static void sio_ltoa(long v, char s[], int b);
ssize_t Sio_putl(long v);

// job handle functions
void init_jobs(); // set all pid to 0
void update(pid_t pid, int num); // update job state with num(0 suspended, 1 running)
void delete(pid_t pid);	// delete the job with pid
void addjob(pid_t pid); // add the job with pid
void show_jobs(); // show all jobs
void kill_job(char* jobstring); // send sigkill to job with num(%d) (only if there it is)
pid_t parse_job(char * jobstring);
void bgrun_stoppedjob(char * jobstring);
void fgrun(char* jobstring);
int find_jobnum(pid_t pid);

// global variables
pid_t pgid_bg;
pid_t pgid_fg;
volatile sig_atomic_t explicit_wait; // 0: false, 1: true fg wait flag
volatile sig_atomic_t fg_wait; // 0: false 1: true used in fg command
char fullcmdline[MAXSHOW]; // trim space and save all cmd including pipe excluding &
int group_flag; // 0: fg, 1: bg flag
pid_t last_pid; // foreground waiting pid

#define MAXJOB 30
#define SUS 0
#define RUN 1

typedef struct job {
	pid_t pid;
	int state; // 0: SUS 1: RUN
	int bg; // 0: fg, 1: bg
	char command[MAXSHOW]; // full command includling argv and pipe
}job;

// global job array
job jobs[30];
volatile int now;

int main(){
	char cmdline[MAXLINE];	// command line
	char* cmdptr[MAXPIPE]; // command pointer for parsing pipelined cmdline
	int fd[MAXPIPE][2];

	/*pgid_fg = getpid();pgid_bg = pgid_fg;*/

	group_flag = 0;
	explicit_wait = 0;
	init_jobs();
	while (1) {// shell needs to loop to read user command
				
		printf("CSE4100:P1-myshell> ");
		// read the command from stdin (func1)
		if (!fgets(cmdline, MAXLINE, stdin)) unix_error("error in read line");
		if (feof(stdin)) exit(0);
		
		// copy string before parsed by parsepipe()
		fullcmdline[0] = '\0';

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
		// install sigchld handler
		/* kernel send sigchld signal 1.when a child terminates, 2. stops 3. resumes from stop
		   can use waitpid to check status and reap
		   (we need to explicitly wait child to be terminated or stopped in fg command case) */
		if (signal(SIGCHLD, sigchld_handler) == SIG_ERR) unix_error("signal child handler install error.");		
	
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
	sigset_t mask, prev_mask, mask_all, mask_wait;
	
	strcpy(buf, cmdline);
	bg = parseline(buf, argv, cmdnum);   // parse line and build args, it returns whether bg or not (and build group_flag in task3)
	if (argv[0] == NULL) return; // ignore an empty line
	

	if (!builtin_command(argv)) {	//quit -> exit(0), & -> ignore, etc... -> run (builtin_command functionality)	
		// install sigint, sigtstp handlers before fork()
		if (signal(SIGINT, sigint_handler_parent) == SIG_ERR) unix_error("signal install error");	
		if (signal(SIGTSTP, sigtstp_handler) == SIG_ERR) unix_error("signal install error");

		Sigemptyset(&mask);
		Sigfillset(&mask_all);
              	Sigaddset(&mask, SIGCHLD);
		Sigfillset(&mask_wait);
                Sigdelset(&mask_wait, SIGCHLD);

		// block sigchld to avoid race associated with addjob
            	if (cmdnum == cmdcnt) Sigprocmask(SIG_BLOCK, &mask, &prev_mask);
		
		if ((pid = Fork()) == 0) {
			/* child part */
			
			// sigint ignore and just receive in parent
			// if bg -> SIG_IGN to sigint
			// if not bg -> SIG_DFL to sigint
			if (signal(SIGINT, sigint_handler) == SIG_ERR) unix_error("signal install error");
			// revert action to default for SIGCHLD
			if (signal(SIGCHLD, SIG_DFL) == SIG_ERR) unix_error("child handler reverting error.");
			// revert the blocked set of child process to previous state
			if (cmdnum == cmdcnt) Sigprocmask(SIG_SETMASK, &prev_mask, NULL);

			/* if it is pipelining, manuplate child fd with dup2(oldfd, newfd) */
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
	                for (int i=0; i<cmdcnt-1; i++){
        	                close(fd[i][0]);
				close(fd[i][1]);
               		}
			// execvp to execute command
			if (execvp(argv[0], argv) < 0) {
				delete(getpid()); // delete it if it was added
				unix_error("Command error");
			}
		}
		/* parent part */		

		/* parent have to explicitly wait for foreground job to terminate
		 in terms of pipelining(task2), parent needs to wait last child process */

		/* only one command in cmdline */
		if (cmdcnt == 1){
			Sigprocmask(SIG_BLOCK, &mask_all, NULL); // temporarily block all sigs
			addjob(pid);	// add newly created job
			if (!bg) {
				last_pid = pid;
				explicit_wait = 1;	// flag will be turned off after handle (handler do deletejob and reaping)
				sigsuspend(&mask_wait);	// wait for child(last_pid)
				Sigprocmask(SIG_SETMASK, &prev_mask, NULL); // unblock signals
			}
			else {
				Sigprocmask(SIG_SETMASK, &prev_mask, NULL); // unblock sigchld
				printf("%d %s", pid, cmdline);
			}

			// uninstall sigint_handler_parent (after child is in terminated/stopped state or bg state)
                        if (signal(SIGINT, sigint_handler) == SIG_ERR) unix_error("signal install error");
		}

		/* multiple commands in cmdline(pipeline case) */
		/* quite confusing, but
		add the job with full commandline and
		fg case: we need to explicitly wait for pipeline last command to terminate or stop
		bg case: just unblock
		in both cases : Make sure to reap all zombie processes in hadler
		*/
		else {
			/* on the function call of the last command */
			if (cmdnum == cmdcnt) {
				// close all parent fd (needed no more and to avoid suspending(hanging) caused by open fd)
				for (int i=0; i<cmdcnt-1; i++){
                               		close(fd[i][0]);
					close(fd[i][1]);
                        	}

                        	Sigprocmask(SIG_BLOCK, &mask_all, NULL); // temporarily block all sigs
				addjob(pid);
				if (!bg) { // if the last command is fg
					last_pid = pid;
					explicit_wait = 1; // flag will be turned off after handle (handler do deletejob and reaping)
					sigsuspend(&mask_wait); // wait for child(last_pid)
                                	Sigprocmask(SIG_SETMASK, &prev_mask, NULL); // unblock sigchld
				}
				else {	
					Sigprocmask(SIG_SETMASK, &prev_mask, NULL); // unblock sigchld
					printf("%d %s", pid, cmdline);
				}
				// uninstall sigint_handler_parent (after child is in terminated/stopped state or bg state)
                       		if (signal(SIGINT, sigint_handler) == SIG_ERR) unix_error("signal install error");
			}
		}
	}
	return;
}

// parse the command line and build argv array
int parseline(char* buf, char* argv[],int cmdnum) {
	char* delim;	// it points to first space delimeter
	int argc;	// argument count
	int bg;		// background or not
	char* temp;	

	buf[strlen(buf)-1] = ' '; // replace '/n' with ' '
	while (*buf && (*buf==' ')) buf++; // ignore leading spaces
	
	// find '&' from the end, and replace it to ' '
	temp = buf + (strlen(buf) -1); // start from last
	while (*temp && (*temp==' ')) temp--; // ignore following spaces
	if (*temp == '&') {
		bg = 1;
		*temp = ' ';
		if (temp == buf) {argc = 0; argv[argc]=NULL; return 1;}
		group_flag = 1; // sort job as bg
	}
	else {
		bg = 0;
		group_flag = 0; // sort job as fg
	}

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
	
	// strcat below needed for jobs array
	for (int i =0;i<argc;i++) {
		strcat(fullcmdline,argv[i]);
		strcat(fullcmdline, " ");
	}
	if (cmdcnt >1 && cmdnum <cmdcnt) {
		strcat(fullcmdline,"| ");
	}

	if (argc == 0) return 1; // ignore blank line -> return 1  
	
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
	if (!strcmp(argv[0], "jobs")) {
		show_jobs();
		return 1;
	}
	if (!strcmp(argv[0], "kill")) {
		kill_job(argv[1]);
		return 1;
	}
	if (!strcmp(argv[0], "bg")) {
		bgrun_stoppedjob(argv[1]);
                return 1;
	}
	if (!strcmp(argv[0], "fg")) {
		// sigint should not terminate myshell
		if (signal(SIGINT, sigint_handler_parent) == SIG_ERR) unix_error("signal install error");
		fgrun(argv[1]);
		if (signal(SIGINT, sigint_handler) == SIG_ERR) unix_error("signal install error");
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
	Sio_puts("\n");
	_exit(0);
}

// SIGINT handler for parent in foreground case (before reaping)
void sigint_handler_parent(int sig){
        Sio_puts("\n");
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
static void sio_reverse(char s[])
{
    int c, i, j;

    for (i = 0, j = strlen(s)-1; i < j; i++, j--) {
        c = s[i];
        s[i] = s[j];
        s[j] = c;
    }
}
static void sio_ltoa(long v, char s[], int b)
{
    int c, i = 0;

    do {
        s[i++] = ((c = (v % b)) < 10)  ?  c + '0' : c - 10 + 'a';
    } while ((v /= b) > 0);
    s[i] = '\0';
    sio_reverse(s);
}
ssize_t sio_putl(long v) /* Put long */
{
    char s[128];

    sio_ltoa(v, s, 10); /* Based on K&R itoa() */  //line:csapp:sioltoa
    return sio_puts(s);
}

// sio error
void sio_error(char s[]) /* Put error message and exit */
{
    sio_puts(s);
    _exit(1);                                      //line:csapp:sioexit
}

// Sio: sio wraaper function
ssize_t Sio_puts(char s[])
{
    ssize_t n;
  
    if ((n = sio_puts(s)) < 0)
	sio_error("Sio_puts error");
    return n;
}
ssize_t Sio_putl(long v)
{
    ssize_t n;

    if ((n = sio_putl(v)) < 0)
	sio_error("Sio_putl error");
    return n;
}
// Sio error
void Sio_error(char s[])
{
    Sio_puts(s);
    _exit(1); 
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

// ~task3~
/* 
	
	waitpid will wait(hang, suspend) untill child terminates 
	and reap allocated kernel memory of the child process.
	But, default action can be chnaged by some constants such as WNOHANG.
	And checking status is possible by status check macro.
*/	
/*	
	Whenever a child process change its state such as exit, crash, stop, resume,
	the kernel sends a SIGCHLD signal to the process's parent.
	...
	reference: https://web.stanford.edu/class/archive/cs/cs110/cs110.1196/static/lectures/07-Signals/lecture-07-signals.pdf
 */

// sigchld handler
void sigchld_handler(int sig){
	int olderrno = errno;
	int status;
	pid_t pid;
	sigset_t mask_all, prev;
	
	Sigfillset(&mask_all);
	
	// explicit wait case (fg)
	if (explicit_wait) {
		// waitpid will wait and check status, waitpid will hang until the last command of fg case ends
		// so, wait list is a certain child in fg (pid = last_pid)
		if ((pid = waitpid(last_pid, &status, WUNTRACED)) < 0) Sio_error("waitpid error in foreground wait\n");
		// ! masking
		Sigprocmask(SIG_BLOCK, &mask_all, &prev);
		if (WIFEXITED(status)) {// true if the child is normally terminated ...
			// delete job if pid in the list
			delete(pid);
		}
		else{ // not terminated normally ...
			if (WIFSTOPPED(status)) {
				// update job status as suspended (SUS)
				Sio_puts("\n");
				update(pid, SUS);
			}
			else if (WIFSIGNALED(status)) {
				delete(pid);
			}
		}
		explicit_wait = 0;
		// ! here unmasking needed
		Sigprocmask(SIG_SETMASK, &prev, NULL);
	}

	// waitpid will not hang and check status, update job status
	// wait list is every child process under parent and
	// waitpid should not hang because there can be running bg jobs
	while ((pid=waitpid(-1, &status, WNOHANG | WCONTINUED)) > 0){
        	// ! masking
		Sigprocmask(SIG_BLOCK, &mask_all, &prev);
		if (WIFEXITED(status)) { // normal exit
			// delete job if pid in the list
			delete(pid);
		}
		else if (WIFSIGNALED(status)) { // child was teminated by signal that is not caught by handler
			// delete job if pid in the list
			delete(pid);
		}
		else if (WIFCONTINUED(status)) { // child restarted from stopped state
			// update job status as running (RUN)
			int jobnum;
			update(pid, RUN);
			jobnum = find_jobnum(pid);
			Sio_puts("[");
			Sio_putl(jobnum);
			Sio_puts("] running ");
			Sio_puts(jobs[jobnum].command);
			Sio_puts("\n");
		}
		// ! here unmasking needed
		Sigprocmask(SIG_SETMASK, &prev, NULL);
	}
	// we don't need to check errno here, with WNOHANG

	errno = olderrno;
}

/* sigprocmask wrapper function */
void Sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    if (sigprocmask(how, set, oldset) < 0)
	unix_error("Sigprocmask error");
    return;
}
void Sigemptyset(sigset_t *set)
{
    if (sigemptyset(set) < 0)
	unix_error("Sigemptyset error");
    return;
}
void Sigaddset(sigset_t *set, int signum)
{
    if (sigaddset(set, signum) < 0)
	unix_error("Sigaddset error");
    return;
}
void Sigfillset(sigset_t *set)
{ 
    if (sigfillset(set) < 0)
	unix_error("Sigfillset error");
    return;
}
void Sigdelset(sigset_t *set, int signum)
{
    if (sigdelset(set, signum) < 0)
	unix_error("Sigdelset error");
    return;
}
/* ... end of sigprocmask wrapper*/

// SIGTSTP (when user press ctrl+z)
void sigtstp_handler(int sig){
/* only ctrl + z case 
   (ctrl z -> send sigtstp signal to every process in the foreground process group)
   the processes of fg group will receive signal,
   and sigtstp is caught and handled by this handler...

   But it seems like there are more in ctrl + z
   because just installing sigtstp_handler and
   doing nothing in this handler makes 
   just one child to stop... by tesing.
   So do nothing in this sigtstp handler 
*/
/*	
	Sio_puts("Reach here? yes ");
*/	
}

/* job handle functions
   (SUS,0: suspended, RUN,1: running)
   need to care about jobnow].pid, state, bg, command
   (job index == job num) index 0 is not used
   'now' is a index of available job 
   if 'now' reaches the end of array when addjob, then find the least available number
*/
void init_jobs() {
	for (int i =0;i<MAXJOB;i++)
		jobs[i].pid = 0;
	now = 1;
}
// update job state
void update(pid_t pid, int num) {
	// job status update
	for (int i =0; i<MAXJOB; i++) {
                if (jobs[i].pid == pid){
                        if (num == RUN) jobs[i].state = RUN;
			if (num == SUS) jobs[i].state = SUS;
                }
        }

}

// delete the job with pid
void delete(pid_t pid) {
	for (int i =0; i<MAXJOB; i++) {
		if (jobs[i].pid == pid){
			jobs[i].pid = 0;
			jobs[i].command[0] = '\0';	
		}
	}
	return;
}
// add the job with pid and with currentcmdline
void addjob(pid_t pid) {
	if (now >= MAXJOB) {
		now = 1;
		while(jobs[now].pid != 0) now++;
		if (now >= MAXJOB) unix_error("JOB list are fulled!\n");
	}

	if (jobs[now].pid == 0) {
		jobs[now].pid = pid;
		jobs[now].state = RUN;
		if (group_flag) jobs[now].bg = 1;
		else jobs[now].bg = 0;
		// copy string 
		strcpy(jobs[now].command,fullcmdline);
		now++;
	}

}
// list all jobs that are running or suspended
void show_jobs() {
	for (int i =1;i<MAXJOB;i++)
                if (jobs[i].pid != 0) {
			if (jobs[i].state == RUN) {
				printf("[%d] running %s\n",i, jobs[i].command);
			}
			else if (jobs[i].state == SUS){
				printf("[%d] suspended %s\n",i,jobs[i].command);
			}
		}
}
// kill a specific job with arg such as "%3"
void kill_job(char * jobstring) {
	pid_t pid;
	if ((pid = parse_job(jobstring))< 0) return;
	kill(pid, SIGKILL);
}
// re-run stopped job in bg
void bgrun_stoppedjob(char * jobstring) {
	pid_t pid;
	if ((pid = parse_job(jobstring))< 0) return;
	// make continue if it was stopped
	kill(pid, SIGCONT);
	sleep(1);
}
// run job in fg, if it was stopped job, continue
void fgrun(char* jobstring) {
	pid_t pid;
	int jobnum;

	sigset_t mask_wait;
	Sigfillset(&mask_wait);
        Sigdelset(&mask_wait, SIGCHLD);
	
	if ((pid = parse_job(jobstring))< 0) return;
	// make continue if it was stopped
	jobnum = find_jobnum(pid);
	if (jobs[jobnum].state == SUS){
		kill(pid, SIGCONT); // if it was stopped job, explicitly wait untill SIGCONT signal is received in child process and child re-run ...
				    // continued -> sigchld handler executed in parent! so we will wait until sigchld received in parent
		sleep(1);
		// running job is printed by sigchld handler...
		last_pid = pid;
	        explicit_wait = 1; // flag will be turned off after handle (handler do deletejob and reaping)
       		sigsuspend(&mask_wait); // wait for child(last_pid)
	}
	else {
		Sio_puts("[");
                Sio_putl(jobnum);
                Sio_puts("] running ");
                Sio_puts(jobs[jobnum].command);
                Sio_puts("\n");

		last_pid = pid;
		explicit_wait = 1;
		sigsuspend(&mask_wait);
	}
}
// find jobnum of the pid and return jobnum
int find_jobnum(pid_t pid){
	int jobnum;
	for (int i =1;i<MAXJOB;i++) {
		if (jobs[i].pid == pid) 
			return i;
	}
}
// jobstring parsing if jobnum is not on the list, return -1
pid_t parse_job(char * jobstring) {
	int jobnum;
	// parsing num and error catching
        if (jobstring[0] != '%') {
		fprintf(stderr, "jobstring arg should be %% with a digit (such as %%1)\n");
        	return -1;
	}
        else {
		if ((jobnum = atoi(jobstring+1)) == 0){
			// if it starts with alphanumeric character ...
			fprintf(stderr, "jobstring arg should be %% with a digit (such as %%1)\n");
                	return -1;
		}
	}

	// check if jobnum is valid on the job list
	if (jobnum >= MAXJOB) {
		fprintf(stderr, "Max job num is %d\n", (MAXJOB-1));
                return -1;
	}
	if (jobs[jobnum].pid == 0){
		fprintf(stderr, "No such job\n");
                return -1;
	}
	
	return jobs[jobnum].pid;
}
