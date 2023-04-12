#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>

#define MAXDATASIZE 100
#define PORT "3845" // порт для подключения пользователей
#define BACKLOG 10 // размер очереди ожидающих подключений
#define KEYWORD "Ready"

char medSett[100];
int setDTime, setDPer, setNTime, setNPer;

void getSpans24(int dTime, int dPeriod, int nTime, int nPeriod)
{
	const time_t *timer = time(NULL);
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

void sigchld_handler(int s)
{
 	while(waitpid(-1, NULL, WNOHANG) > 0);
}

// получить sockaddr, IPv4 или IPv6:
void *get_in_addr(struct sockaddr *sa)
{
 	if (sa->sa_family == AF_INET) {
 		return &(((struct sockaddr_in*)sa)->sin_addr);
 	}
 	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}



int main(void)
{
 	int sockfd, new_fd; // слушать на sock_fd, новое подключение на new_fd
 	struct addrinfo hints, *servinfo, *p;
 	struct sockaddr_storage their_addr; // адресная информация подключившегося
 	socklen_t sin_size;
 	struct sigaction sa;
 	int yes=1;
 	char s[INET6_ADDRSTRLEN], buf[MAXDATASIZE];
 	int rv, numbytes;

 	memset(&hints, 0, sizeof hints);
 	hints.ai_family = AF_UNSPEC;
 	hints.ai_socktype = SOCK_STREAM;
 	hints.ai_flags = AI_PASSIVE; // использовать мой IP
	
	setDTime = 20;
	setNTime = 7;
	setDPer = 30;
	setNPer = 60;

 	if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
 		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
 		return 1;
 	}

 // цикл по всем результатам и связывание с первым возможным
 	for(p = servinfo; p != NULL; p = p->ai_next) {

 		if ((sockfd = socket(p->ai_family, p->ai_socktype,p->ai_protocol)) == -1) {
 			perror("server: socket");
 			continue;
 		}

 		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
 			perror("setsockopt");
 			exit(1);
 		}

 		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
 			close(sockfd);
 			perror("server: bind");
 			continue;
 		}

 		break;
 		}

 	if (p == NULL) { 
		fprintf(stderr, "server: failed to bind\n");
 		return 2;
 	}

 	freeaddrinfo(servinfo); // со структурой закончили
 	if (listen(sockfd, BACKLOG) == -1) {
 		perror("listen");
 		exit(1);
 	}

 	sa.sa_handler = sigchld_handler; // жатва всех мёртвых процессов
 	sigemptyset(&sa.sa_mask);
 	sa.sa_flags = SA_RESTART;

 	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
 		perror("sigaction");
 		exit(1);
 	}

 	printf("server: waiting for connections…\n");
 	while(1) { // главный цикл accept()
 		sin_size = sizeof their_addr;
 		new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
 		if (new_fd == -1) {
 			perror("accept");
 			continue;
 		}

 	inet_ntop(their_addr.ss_family,
 	get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
 	printf("server: got connection from %s\n", s);



	 if ((numbytes = recv(new_fd, buf, MAXDATASIZE-1, 0)) == -1) {
		perror("recv");
		exit(1);
	}
	
	buf[numbytes] = '\0';
	printf("server: received message: %s\n",buf);
	
	if (strstr(buf, KEYWORD) != NULL)
	{
		getSpans24(setDTime, setDPer, setNTime, setNPer);
		printf("sending...\n");
		printf("%s\n", medSett);
		if (!fork()) { // это порождённые процесс
			close(sockfd); // его слушать не нужно
			if (send(new_fd, medSett, sizeof(medSett), 0) == -1)
				perror("send");
				close(new_fd);
				exit(0);
			}
		close(new_fd); // родителю это не нужно
	}
	}
 	return 0;
} 