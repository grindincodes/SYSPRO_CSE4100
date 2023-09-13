/*
	myshell.h 
	header file for definitions and prototype-functions for the CS:APP3e book
	with variables and functions in myshell
 */
#ifndef __MYSHELL_H__
#define __MYSHELL_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h> 
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <signal.h>
// ... add include if needed

#define	MAXLINE	 8192
#define MAXSHOW 150
#define MAXPIPE 10


extern char ** environ;

// command count
int cmdcnt;

#endif /* __MYSHELL_H__ */
