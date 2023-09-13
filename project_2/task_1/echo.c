/*
 * echo - read and echo text lines until client closes connection
 */
/* $begin echo */
#include "csapp.h"

void echo(int connfd) 
{
    int n; 
    char buf[MAXLINE]; 
    rio_t rio;

    Rio_readinitb(&rio, connfd);
    while((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) {
	printf("server received %d bytes\n", n);
	// parse the command and do whatever we need before send Rio_written
	Rio_writen(connfd, buf, n);
    }
}
/* $end echo */

