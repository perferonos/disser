#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <event2/event.h>
#include <event2/thread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>

#define READ_BUFF_SIZE 128
#define WRITE_BUFF_SIZE ((READ_BUFF_SIZE)*8)
#define DEF_HOST "192.168.3.3"
#define DEF_PORT 12345

#define SETDTIME 20
#define SETDPER 30
#define SETNTIME 7
#define SETNPER 60

char medSett[100] = {0};

typedef struct client_info
{
	int id;
	int SBP; // Systollic blood pressure
	int DBP; // Dystollic blood pressure
	int PR; // Pulse rate
} client_info;

typedef struct connection_ctx_t {
    struct connection_ctx_t* next;
    struct connection_ctx_t* prev;

    evutil_socket_t fd;
    struct event_base* base;
    struct event* read_event;
    struct event* write_event;

    uint8_t read_buff[READ_BUFF_SIZE];
    uint8_t write_buff[WRITE_BUFF_SIZE];

    ssize_t read_buff_used;
    ssize_t write_buff_used;
} connection_ctx_t;

void on_accept(evutil_socket_t listen_sock, short flags, void* arg);
void on_read(evutil_socket_t fd, short flags, void* arg);
void on_string_received(evutil_socket_t sockfd, char* str, int len, connection_ctx_t* ctx);
void getSpans24(int dTime, int dPeriod, int nTime, int nPeriod);
void send_settings(evutil_socket_t sockfd);

void getSpans24(int dTime, int dPeriod, int nTime, int nPeriod)
{
	time_t timer = time(NULL);
	struct tm *timeCalc;
	timeCalc = localtime(&timer);
	for (int i = 0; i<100; i++) medSett[i] = 0;
	int fstper;
	int secper;
	int thirper;
	int dSpan = 60 / dPeriod;
	int nSpan = 60 / nPeriod;
	int Now = timeCalc->tm_hour;
	
	if (dTime < Now)
	{
		fstper = Now - dTime;
	}
	else
	{
		fstper = dTime - Now;
	}
	if (dTime != 0)
	{
		secper = nTime + 24 - dTime;
	}
	else
	{
		secper = nTime - dTime;
	}
	thirper = Now - nTime;
	sprintf(medSett, "%d.%d_%d.%d_%d.%d", dPeriod, fstper*dSpan, nPeriod, secper*nSpan, dPeriod, thirper*dSpan);
}

void send_settings(evutil_socket_t sockfd)
{
	getSpans24(SETDTIME, SETDPER, SETNTIME, SETNPER);
	if (send(sockfd, medSett, sizeof(medSett), 0) < 0)
		perror("Settings send error!\n");
}

char* get_id(char *msg)
{
    char buf[32] = {0};
    char *ret = "";
    for (int i = 0; msg[i] != ';'; i++)
    {
    	buf[i] = msg[i];
    }
    sprintf(ret, "%s", buf);
    return ret;
}

void on_close(connection_ctx_t* ctx) 
{
    printf("[%p] on_close called, fd = %d\n", ctx, ctx->fd);

    // remove ctx from the lined list
    ctx->prev->next = ctx->next;
    ctx->next->prev = ctx->prev;

    event_del(ctx->read_event);
    event_free(ctx->read_event);

    if(ctx->write_event) 
    {
        event_del(ctx->write_event);
        event_free(ctx->write_event);
    }

    close(ctx->fd);
    free(ctx);
}

void run(char* host, int port) 
{
    // allocate memory for a connection ctx (used as linked list head)
    connection_ctx_t* head_ctx = (connection_ctx_t*)malloc(sizeof(connection_ctx_t));
    if(!head_ctx)
        perror("malloc() failed");

    head_ctx->next = head_ctx;
    head_ctx->prev = head_ctx;
    head_ctx->write_event = NULL;
    head_ctx->read_buff_used = 0;
    head_ctx->write_buff_used = 0;

    // create a socket
    head_ctx->fd = socket(AF_INET, SOCK_STREAM, 0);
    if(head_ctx->fd < 0)
        perror("socket() failed");

    // make it nonblocking
    if(evutil_make_socket_nonblocking(head_ctx->fd) < 0)
        perror("evutil_make_socket_nonblocking() failed");

    // bind and listen
    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = inet_addr(host);
    if(bind(head_ctx->fd, (struct sockaddr*)&sin, sizeof(sin)) < 0)
        perror("bind() failed");

    if(listen(head_ctx->fd, 1000) < 0)
        perror("listen() failed");

    // create an event base
    struct event_base* base = event_base_new();
    if(!base)
        perror("event_base_new() failed");

    // create a new event
    struct event* accept_event = event_new(base, head_ctx->fd,
        EV_READ | EV_PERSIST, on_accept, (void*)head_ctx);
    if(!accept_event)
        perror("event_new() failed");

    head_ctx->base = base;
    head_ctx->read_event = accept_event;

    // schedule the execution of accept_event
    if(event_add(accept_event, NULL) < 0)
        perror("event_add() failed");

    // run the event dispatching loop
    if(event_base_dispatch(base) < 0)
        perror("event_base_dispatch() failed");

    // free allocated resources
    on_close(head_ctx);
    event_base_free(base);
}

void on_accept(evutil_socket_t listen_sock, short flags, void* arg) 
{
    connection_ctx_t* head_ctx = (connection_ctx_t*)arg;
    evutil_socket_t fd = accept(listen_sock, 0, 0);

    if(fd < 0)
        perror("accept() failed");

    // make in nonblocking
    if(evutil_make_socket_nonblocking(fd) < 0)
        perror("evutil_make_socket_nonblocking() failed");

    connection_ctx_t* ctx = (connection_ctx_t*)malloc(sizeof(connection_ctx_t));
    if(!ctx)
        perror("malloc() failed");

    // add ctx to the linked list
    ctx->prev = head_ctx;
    ctx->next = head_ctx->next;
    head_ctx->next->prev = ctx;
    head_ctx->next = ctx;

    ctx->base = head_ctx->base;

    ctx->read_buff_used = 0;
    ctx->write_buff_used = 0;

    printf("[%p] New connection! fd = %d\n", ctx, fd);

    ctx->fd = fd;
    ctx->read_event = event_new(ctx->base, fd, EV_READ | EV_PERSIST, on_read, (void*)ctx);
    if(!ctx->read_event)
        perror("event_new(... EV_READ ...) failed");

    if(event_add(ctx->read_event, NULL) < 0)
        perror("event_add(read_event, ...) failed");
}

void on_read(evutil_socket_t fd, short flags, void* arg) 
{
    connection_ctx_t* ctx = arg;

    printf("[%p] on_read called, fd = %d\n", ctx, fd);

    ssize_t bytes;
    for(;;) 
    {
        bytes = read(fd, ctx->read_buff + ctx->read_buff_used, READ_BUFF_SIZE - ctx->read_buff_used);
        if(bytes == 0) 
        {
            printf("[%p] client disconnected!\n", ctx);
            on_close(ctx);
            return;
        }

        if(bytes < 0) 
        {
            if(errno == EINTR)
                continue;

            printf("[%p] read() failed, errno = %d, closing connection.\n", ctx, errno);
            on_close(ctx);
            return;
        }

        break; // read() succeeded
    }

    ssize_t check_end = ctx->read_buff_used + bytes;
    ctx->read_buff_used = check_end;

    on_string_received(fd, (char*)ctx->read_buff, ctx->read_buff_used + bytes, ctx);

    if(ctx->read_buff_used == READ_BUFF_SIZE) 
    {
        printf("[%p] client sent a very long string, closing connection.\n", ctx);
        on_close(ctx);
    }
}

void on_string_received(evutil_socket_t sockfd, char* str, int len, connection_ctx_t* ctx) 
{
    char *id;
    char filename[128] = {0};

    printf("[%p] a complete string received: '%s', length = %d\n", ctx, str, len);
    if (strcasestr(str, "Ready"))
    {
    	send_settings(sockfd);
    	return;
    }

    id = get_id(str);
    sprintf(filename, "%s.txt", id);

    // Записываем данные в файл.
    FILE *fd = fopen(filename, "a+");
    if (fd < 0) 
    {
        perror("open");        
        return;
    }
    fprintf(fd, "%s\n", str);
    fclose(fd);
}

int main(int argc, char **argv) 
{
	int port;
	char *host = {0};
    evthread_use_pthreads();
    struct event_base *base = event_base_new();

    if (argc < 3)
    {
    	printf("Usage: ./serv2 <host> <port>\n");
    	printf("not enough arguments!\nusing default parameters: host 192.168.3.3, port 12345\n");
        port = DEF_PORT;
        host = DEF_HOST;
    }
    else
    {
        host = argv[1];
        port = atoi(argv[2]);
    }

    run(host, port);

    event_base_free(base);

    return 0;
}