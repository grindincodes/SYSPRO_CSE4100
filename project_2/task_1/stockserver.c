/* 
 * echoserveri.c - An iterative echo server 
 */ 
/* $begin echoserverimain */
#include "csapp.h"

typedef struct _item {
	int id;
	int amount;
	int price;
	int readcnt;
	struct _item *left;
	struct _item *right;
}item;

typedef struct {
	int maxfd;
	fd_set ready_set;
	fd_set read_set;
	int nready;
	int maxi;
	int clientfd[FD_SETSIZE];
	rio_t clientrio[FD_SETSIZE];
}pool;

item *root = NULL;
FILE* gfp;

int client_cnt;

void echo(int connfd);
void insert(item* stock);
void saveInOrder(item *stock);
void showInOrder(item* stock, char* outbuf);
item* search(int id, item* stock);
void init_pool(int listenfd, pool *p);
void add_client(int connfd, pool *p);
void check_clients(pool *p);
int parse_command(char* buf, int* id, int* amount);
void sigint_handler(int sig);

int main(int argc, char **argv) 
{
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;  /* Enough space for any address */  //line:netp:echoserveri:sockaddrstorage
    // char client_hostname[MAXLINE], client_port[MAXLINE];
    FILE * fp;
    int id, amount, price;
    static pool pool;
    item* temp;
	
    client_cnt = 0;

    if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(0);
    }
    
    // read table from stock.txt and load to tree
    fp = fopen("stock.txt", "r");
    while(fscanf(fp, "%d %d %d", &id, &amount, &price) != EOF ){
    		temp = (item*)malloc(sizeof(item));
		temp -> id = id;
		temp -> amount = amount;
		temp -> price = price;
		insert(temp);
    }
    fclose(fp);

    listenfd = Open_listenfd(argv[1]);

    init_pool(listenfd, &pool);// initialize connfd pool

    if (signal(SIGINT, sigint_handler) == SIG_ERR) unix_error("signal install error"); // valid ctrl + c only when it has no client

    while (1) {
	pool.ready_set = pool.read_set; // copy readset to inspect
	pool.nready = Select(pool.maxfd+1, &pool.ready_set, NULL, NULL, NULL);
	
	if (FD_ISSET(listenfd, &pool.ready_set)) { // if listening descriptor is ready,
		clientlen = sizeof(struct sockaddr_storage);
		connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
		add_client(connfd, &pool);
	}
        // Getnameinfo((SA *) &clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
        // printf("Connected to (%s, %s)\n", client_hostname, client_port);
	
	// check clients and read one line from each connfd being ready
	check_clients(&pool);
    }

    exit(0); // exit that is never reached
}
/* $end echoserverimain */

void insert(item* stock){
	item *temp = NULL;
	item *parent;
	temp = root;
	
	stock -> readcnt = 0;
        stock -> left = NULL;
        stock -> right = NULL;
	if (root == NULL){
		root = stock;
	}
	else {
		while(1) {
			if((temp->id) < (stock->id)){ //if insert-item is bigger than temp->id
                        	parent = temp;
				temp = temp->right;
				if (temp == NULL) {
                                        parent -> right = stock;
					break;
                                }
			}
			else {
				parent = temp;
				temp = temp->left;
				if (temp == NULL) {
					parent -> left = stock;
					break;
				}
			}
		}
		
	}

}

void saveInOrder(item *stock) {
	// inorder traverse and save
	
	if (stock == NULL) return;

	saveInOrder(stock->left);
	fprintf(gfp, "%d %d %d\n", stock->id, stock->amount, stock->price);
	saveInOrder(stock->right);
}

// build outbuf by in-order traversal for show command
void showInOrder(item* stock, char *outbuf) {
	if (stock == NULL) return;
    	showInOrder(stock->left, outbuf);
	
	char temp[50];
    	sprintf(temp, "%d %d %d\n", stock->id, stock->amount, stock->price);
	strcat(outbuf, temp);

    	showInOrder(stock->right, outbuf);
}

// find the stock with id(key)
item* search(int id, item* stock) {
	if (!stock) return NULL; // return if stock is NULL
	
	if (id == stock->id) return stock;
	
	// if id is bigger than the id of the current stock, go right
	if (id > stock->id) return search(id, stock->right);
	else return search(id, stock->left);
}


// initialize connfd pool and add set on listenfd to listen
void init_pool(int listenfd, pool* p){
	int i;
	p->maxi = -1;
	for (i=0; i<FD_SETSIZE; i++) {
		p->clientfd[i] = -1;
	}
	p->maxfd=listenfd;
	FD_ZERO(&p->read_set);
	FD_SET(listenfd, &p->read_set); // add listenfd to read_set
}

// add connfd to connfd pool
void add_client(int connfd, pool *p){
	int i;
	p->nready--;
	for (i=0; i<FD_SETSIZE; i++) { // find a slot
		if (p->clientfd[i] < 0) { // that is available
			// add connfd to the pool
			p->clientfd[i] = connfd;
			Rio_readinitb(&p->clientrio[i], connfd);
			FD_SET(connfd, &p->read_set); // add connfd to read_set
			if (connfd > p->maxfd) p->maxfd = connfd; // update largest descriptor
			if (i > p->maxi) p->maxi = i;  // update pool high water mark
			client_cnt++;
			break;
		}
	}
	if (i == FD_SETSIZE) app_error("add_client error: Too many clients");
}

// check clients which have sent message, read and handle the command, and echo(respond)
void check_clients(pool *p) {
	int i, connfd, n;
	char buf[MAXLINE], outbuf[MAXLINE];
	rio_t rio;
	int cmd, id, amount;
	item* stock;


	for (i=0; (i <= p->maxi) && (p->nready > 0); i++) {
		connfd = p->clientfd[i];
		rio = p->clientrio[i];

		// check if the descriptor is ready
		if ((connfd > 0) && (FD_ISSET(connfd, &p->ready_set))) {
			p->nready--;

			if ((n = Rio_readlineb(&rio, buf, MAXLINE))!= 0) {
				printf("Server received %d bytes\n", n);
				// do command parsing
				cmd = parse_command(buf, &id, &amount);
				
				outbuf[0] = '\0';
				if (cmd == 4) { //exit
					// disconnect and remove connfd from readset
					Close(connfd);
					FD_CLR(connfd, &p->read_set);
					p->clientfd[i] = -1;
					client_cnt--;
				}
				if (cmd == 1) { //show
					showInOrder(root, outbuf);
					Rio_writen(connfd, outbuf, MAXLINE);
				}
				if (cmd == 2) { //buy
					stock = search(id, root); // find and return corresponding stock
					if (stock) { // search complete
						if (stock -> amount < amount) {
							strcpy(outbuf, "Not enough left stock\n");
							Rio_writen(connfd, outbuf, MAXLINE);
						}
						else{
							stock->amount = stock->amount - amount;
							strcpy(outbuf, "[buy] success\n");
							Rio_writen(connfd, outbuf, MAXLINE);
						}
					}
					else { // search failed
                                                strcpy(outbuf, "No such stock in server\n");
                                                Rio_writen(connfd, outbuf, MAXLINE);
					}
				}
				if (cmd == 3) { //sell
					stock = search(id, root); // find and return corresponding stock
					if (stock) { // search complete
						stock->amount = stock->amount + amount;
						strcpy(outbuf, "[sell] success\n");
						Rio_writen(connfd, outbuf, MAXLINE);
					}
					else { // search failed
						strcpy(outbuf, "No such stock in server\n");
						Rio_writen(connfd, outbuf, MAXLINE);
					}
				}

			}
			else { // EOF detected, remove connfd from readset
				Close(connfd);
                                FD_CLR(connfd, &p->read_set);
                                p->clientfd[i] = -1;
				client_cnt--;
			}
		}
	}
}

int parse_command(char* buf, int* id, int* amount) {
	char* p = buf;
	int i, cmd;
	char *idptr, *amountptr;

	for (i=0;i<2;i++) {
		while (*p != ' ') p++; // move to space character
		
		// replace it with null character and move 1 step forward
		*p = '\0';
		p++;
		if (i == 0) idptr = p;
		if (i == 1) amountptr = p;
	}
        if (!strcmp(buf, "show")) cmd = 1;
        if (!strcmp(buf, "buy")) {
                cmd = 2;
                *id = atoi(idptr);
		*amount = atoi(amountptr);
        }
        if (!strcmp(buf, "sell")) {
		cmd = 3;
		*id = atoi(idptr);
		*amount = atoi(amountptr);
	}
        if (!strcmp(buf, "exit")) cmd = 4;

	return cmd;
}

// sigint handler that save data to the file
void sigint_handler(int sig) {
    // ctrl + c to check no more connection remained and if it is, save and terminate
    // save the tree comtents to the stock.txt
    gfp = fopen("stock.txt","w");
    saveInOrder(root);
    fclose(gfp);
    exit(0);
}
