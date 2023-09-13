/* 
	create simple stock trade server
	to learn about thread-based conccurent server,
	using semaphore, thread pooling with shared buffer(Synchronization),
	and stock table synchronization.
	
	by. Hyeonseok Kang
	project from CSE4100, Prof. Youngjae Kim, Sogang Univ.
	base code from Computer Systems(A programmer's perspective, 3rd Edition), Randal E. Bryant, David R. O'Hallaron
 */ 
#include "csapp.h"
#define SBUFSIZE 1000
#define NTHREADS 1000

typedef struct _item {
	int id;
	int amount;
	int price;
	int readcnt;	// readcnt now
	sem_t mutex;	// mutex lock for each stock item
	sem_t w;	// mutex write lock for each 
	struct _item *left;
	struct _item *right;
}item;

typedef struct {
	int *buf;	// buffer array
	int n;		// max slot num
	int front;	// buf[(front+1)%n] is first item
	int rear;	// buf[rear%n] is last item
	sem_t mutex;	// mutex lock for sbuf
	sem_t slots;	// available slots counting
	sem_t items;	// available items counting
}sbuf_t;

item *root = NULL;
FILE* gfp;

int client_cnt;
sbuf_t sbuf;	// shred buffer of connected descriptors
sem_t mutex_cnt;

void echo(int connfd); // not used

// tree insert, save tree to files, read all nodes, search one node ...
// thread-safe showInOrder(show)!
void insert(item* stock);
void saveInOrder(item *stock);
void showInOrder_t(item* stock, char* buf);
item* search(int id, item* stock);

//sbuf package functions
void sbuf_init(sbuf_t *sp, int n);
void sbuf_deinit(sbuf_t *sp);
void sbuf_insert(sbuf_t *sp, int item);
int sbuf_remove(sbuf_t *sp);

// thread routine
void* thread(void *vargp);
void trade_connect(int connfd);

int parse_command(char* buf, int* id, int* amount);
void sigint_handler(int sig);

int main(int argc, char **argv) 
{
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;  /* Enough space for any address */  //line:netp:echoserveri:sockaddrstorage
    // char client_hostname[MAXLINE], client_port[MAXLINE];
    FILE * fp;
    int id, amount, price, i;
    item* temp;
    pthread_t tid;
	
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

    Sem_init(&mutex_cnt, 0, 1);
    sbuf_init(&sbuf, SBUFSIZE);  // initialize sbuf for connfd
	for (i=0;i<NTHREADS;i++) {
		// create n worker threads
		Pthread_create(&tid, NULL, thread, NULL);
	}
    if (signal(SIGINT, sigint_handler) == SIG_ERR) unix_error("signal install error"); // valid ctrl + c only when it has no client    

    while (1) {
		clientlen = sizeof(struct sockaddr_storage); 
		connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
		sbuf_insert(&sbuf, connfd);	// insert connfd to buffer
		P(&mutex_cnt);
        	client_cnt++;
        	V(&mutex_cnt);
    }
    // no more thread client and ctrl+c to save and exit
}

// thread routine
void* thread(void *vargp){
	Pthread_detach(Pthread_self());
	while (1) {
		int connfd = sbuf_remove(&sbuf);
		trade_connect(connfd);	// each thread service like echo server
		P(&mutex_cnt);
		client_cnt--;
		V(&mutex_cnt);
		Close(connfd);
	}
}

// show and trade stocks by user request, it needs mutex lock, read lock for each node(write and read)
void trade_connect(int connfd){
	int n;
	char buf[MAXLINE], outbuf[MAXLINE];
	rio_t rio;
	// static pthread_once_t once = PTHREAD_ONCE_INIT;
	int cmd, id, amount;
	item* stock;

	//Pthread_once(&once, init_echo_cnt);	// pthread init function, set semaphore
	Rio_readinitb(&rio, connfd);	// Rio init
	

	// lock print byte, lock node-writing when it is being read, lock node when write ...
        // reader and writer problem -> make read and write of tree thread-safe!
	while ((n=Rio_readlineb(&rio, buf, MAXLINE)) != 0) {
		printf("server received %d bytes\n", n);
		// Rio_written response according to result of user-command 
		// do command parsing
        	cmd = parse_command(buf, &id, &amount);
		outbuf[0] = '\0';
       		if (cmd == 4) { //exit
           		return;
       		}
       		if (cmd == 1) { //show
            		showInOrder_t(root, outbuf);
			Rio_writen(connfd, outbuf, MAXLINE);
        	}
		if (cmd == 2) { //buy
			stock = search(id, root);
			if (stock) {
				// we need to lock the whole code block to ensure write sync
				P(&stock->w);
				// write
				if (stock->amount > amount){ // success
					stock->amount = stock->amount - amount;
					strcpy(outbuf, "[buy] success\n");
					Rio_writen(connfd, outbuf, MAXLINE);
				}
				else {
					strcpy(outbuf, "Not enough left stock\n");
					Rio_writen(connfd, outbuf, MAXLINE);
				}
				V(&stock->w);
			}
			else {
				strcpy(outbuf, "No such stock in server\n");
                                Rio_writen(connfd, outbuf, MAXLINE);
			}
		}
		if (cmd == 3) {
			stock = search(id, root);
			if (stock) { // search complete
				P(&stock->w);
				stock->amount = stock->amount + amount;
				strcpy(outbuf, "[sell] success\n");
				Rio_writen(connfd, outbuf, MAXLINE);
				V(&stock->w);
			}
			else { // search failed
				strcpy(outbuf, "No such stock in server\n");
				Rio_writen(connfd, outbuf, MAXLINE);
			}
		
		}
	}
	//EOF detected, get out of the loop
	return;
}

// insert data to the tree before accept clients
void insert(item* stock){
	item *temp = NULL;
	item *parent;
	temp = root;
	
	stock -> readcnt = 0;
    	stock -> left = NULL;
    	stock -> right = NULL;
	Sem_init(&(stock->mutex), 0, 1);
	Sem_init(&(stock->w), 0, 1);
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

// use their own buffer
void saveInOrder(item *stock) {
	// inorder traverse and save only if there is no peer thread
	if (stock == NULL) return;

	saveInOrder(stock->left);
	fprintf(gfp, "%d %d %d\n", stock->id, stock->amount, stock->price);
	saveInOrder(stock->right);
}



void showInOrder_t(item* stock, char *outbuf){
    // thread safe version
    if (stock == NULL) return;

    showInOrder_t(stock->left, outbuf);

    char temp[50];
    P(&stock->mutex);
    stock->readcnt++;
    if (stock->readcnt == 1) // first in of read
        P(&stock->w);
    V(&stock->mutex);
    
    //read
    sprintf(temp, "%d %d %d\n", stock->id, stock->amount, stock->price);
    strcat(outbuf, temp);    

    P(&stock->mutex);
    stock->readcnt--;
    if (stock->readcnt == 0) // last out of read
        V(&stock->w);
    V(&stock->mutex);

    showInOrder_t(stock->right, outbuf);
}

// find the stock with id(key)
// id is not changed, so don't have to make thread-safe
item* search(int id, item* stock) {
    if (!stock) return NULL; // return if stock is NULL
    
    if (id == stock->id) return stock;

    // if id is bigger than the id of the current stock, go right
    if (id > stock->id) return search(id, stock->right);
    else return search(id, stock->left);
}


// command parsing
int parse_command(char* buf, int* id, int* amount) {
	char* p = buf;
	int i, cmd;
	char *idptr, *amountptr;

	for (i=0;i<2;i++) {
		while (*p && (*p != ' ')) p++; // move to space character
	// ex buy 1 2	
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
    if (client_cnt == 0){
	gfp = fopen("stock.txt","w");
        saveInOrder(root);
        fclose(gfp);
        exit(0);
    }
}
/* sbuf package functions */
void sbuf_init(sbuf_t *sp, int n){
	sp->buf = Calloc(n, sizeof(int)); // buffer holds n integer items
	sp->n = n;
	sp->front = sp->rear = 0;	// empty at first
	Sem_init(&sp->mutex, 0, 1);	// lock mutex
	Sem_init(&sp->slots, 0, n);
	Sem_init(&sp->items, 0, 0);	// n slots, 0 items at first
}
void sbuf_deinit(sbuf_t *sp){
	Free(sp->buf);
}
void sbuf_insert(sbuf_t *sp, int item){
	P(&sp->slots);	// wait available slot
	P(&sp->mutex);	// lock buffer
	sp->buf[(++sp->rear)%(sp->n)] = item;
	V(&sp->mutex);	// unlock
	V(&sp->items);	// announce available item
}
int sbuf_remove(sbuf_t *sp){
	int item;
	P(&sp->items);  // wait available item
        P(&sp->mutex);  // lock buffer
        item = sp->buf[(++sp->front)%(sp->n)];
        V(&sp->mutex);  // unlock
        V(&sp->slots);  // announce available item
	return item;
}
/* end sbuf */
