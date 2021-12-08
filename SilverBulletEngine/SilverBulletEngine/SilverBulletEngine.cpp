﻿// SilverBulletEngine.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#undef UNICODE

#define WIN32_LEAN_AND_MEAN

//#include <iostream>
//#include <sys/types.h>
//#include <winsock2.h>
//#include <winsock.h>
//#include <ws2tcpip.h>
//#include <Windows.h>
//#include <thread>
//#include <cstdio>
//#include <stdlib.h>
//#include <stdio.h>
//#include <fcntl.h>
//#include <errno.h>
//#include <string.h>
//#include <fstream>
//#include <event.h>
//#include <signal.h>
//#include <stdarg.h>

//#include "WorkQueue.h"

//using namespace std;

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <event.h>
#include <event2/event.h>
#include <event2/bufferevent.h>
#include <thread>

#define SERVER_PORT 5555

#define CONNECTION_BACKLOG 8

#define SOCKET_READ_TIMEOUT_SECONDS 10

#define SOCKET_WRITE_TIMEOUT_SECONDS 10

#define NUM_THREADS 8

#define errorOut(...) {\
	fprintf(stderr, "%s:%d: %s():\t", __FILE__, __LINE__, __FUNCTION__);\
}

//#pragma comment (lib, "Ws2_32.lib")


#pragma region  LogicDemo1
 
struct mPoint
{
	int x; 
	int y;
};

// Message
struct uMsg
{
	int type;
	char name[64];
	char text[512]; // text msg
	mPoint *m_point;
};

// Client
struct clientInfo 
{
	evutil_socket_t fd;
	sockaddr_in saddr;
	uMsg msg;
};

struct client {
	evutil_socket_t fd;

	struct event_base *evbase;

	struct bufferevent *buf_ev;

	struct evbuffer *output_buffer;
};


// client chain node
typedef struct userClientNode
{
	client cInfo;
	userClientNode *next;
} *ucnode_t;

userClientNode *listHead;
userClientNode *lp;

static struct event_base *evbase_accept;

#pragma endregion


#pragma region chain node logic

// Insert Client to chain
userClientNode *insertNode(userClientNode *head, SOCKET client, struct event_base *evbase, struct bufferevent *buf_ev, struct evbuffer *output_buffer)
{
	userClientNode *newNode = new userClientNode();
	newNode->cInfo.fd = client;
	newNode->cInfo.evbase = evbase;
	newNode->cInfo.buf_ev = buf_ev;
	newNode->cInfo.output_buffer = output_buffer;
	userClientNode *p = head;

	if (p == nullptr)
	{	
		head = newNode;
	}
	else {
		while (p->next != nullptr)
		{
			p = p->next;
		}
		p->next = newNode;
	}

	return head;
}

// Delete node 
userClientNode *deleteNode(userClientNode *head, SOCKET client)
{
	userClientNode *p = head;
	if (p == nullptr) 
	{
		return head;
	}

	if (p->cInfo.fd == client)
	{
		head = p->next;
		delete p;
		return head;
	}

	while (p->next != nullptr && p->next->cInfo.fd != client)
	{
		p = p->next;
	}

	if (p->next == nullptr)
	{
		return head;
	}

	userClientNode *deleteNode = p->next;
	p->next = deleteNode->next;
	delete deleteNode;
	return head;
}


static void closeClient(client *cli) {
	if (cli != NULL) {
		if (cli->fd >= 0) {
			evutil_closesocket(cli->fd);
			cli->fd = -1;
		}
	}
}

void buffered_on_read(struct bufferevent *bev, void *arg) {

	#define MAX_LINE    256
	char line[MAX_LINE + 1];
	int n;

	evutil_socket_t fd = bufferevent_getfd(bev);

	while (n = bufferevent_read(bev, line, MAX_LINE), n > 0) {
		line[n] = '\0';
		printf("fd=%u, read line: %s\n", fd, line);

		bufferevent_write(bev, line, n);
	}

	client *cli = (client *)arg;
	char data[4096];
	int nbytes;

	while ((nbytes = EVBUFFER_LENGTH(bev->input)) > 0) {
		if (nbytes > 4096) nbytes = 4096;
		evbuffer_remove(bev->input, data, nbytes);
		evbuffer_add(cli->output_buffer, data, nbytes);
	}

	if (bufferevent_write_buffer(bev, cli->output_buffer)) {
		errorOut("Error sending data to client on fd %d\n", cli->fd);
		//closeClient(cli);
	}
}

void buffered_on_write(struct bufferevent *bev, void *arg) {

}

void buffered_on_error(struct bufferevent *bev, short what, void *ctx) {
	//closeClient((client_t *)ctx);
}
 

// Ready to be accept
void on_accept(int fd, short ev, void *arg) 
{
	struct event_base *base = (struct event_base *)arg;
	evutil_socket_t fd;
	struct sockaddr_in client_addr;
	socklen_t client_len = sizeof(client_addr);

	
	fd = accept(fd, (struct sockaddr *)&client_addr, &client_len);
	if (fd < 0) 
	{
		perror("accept failed");
		return;
	}

	if (fd > FD_SETSIZE)
	{
		perror("client_fd > FD_SETSIZE\n");
		return;
	}

	if (evutil_make_socket_nonblocking(fd) < 0)
	{
		perror("failed to set client socket to non-blocking");
		return;
	}

	// Create a client object
	client *cInfo = new client();
	int len_client = sizeof(sockaddr);
	if (cInfo == NULL)
	{
		perror("failed to allocate memory for client state");
		evutil_closesocket(fd);
		return;
	}

	cInfo->fd = fd;

	printf("ACCEPT: fd = %u\n", cInfo->fd);

	if ((cInfo->output_buffer = evbuffer_new()) == NULL) {
		perror("client output buffer allocation failed");
		//closeAndFreeClient(client);
		return;
	}


	if ((cInfo->evbase = event_base_new()) == NULL) {
		perror("client event_base creation failed");
		//closeAndFreeClient(client);
		return;
	}

	// Create buffer event 
	struct bufferevent *bev = bufferevent_socket_new(base, cInfo->fd, BEV_OPT_CLOSE_ON_FREE);
	if (bev == NULL)
	{
		perror("client bufferevent creation failed");
		return;
	}
	bufferevent_setcb(bev, buffered_on_read, buffered_on_write, buffered_on_error, arg);
	cInfo->buf_ev = bev;
	bufferevent_base_set(cInfo->evbase, cInfo->buf_ev);
	bufferevent_settimeout(cInfo->buf_ev, SOCKET_READ_TIMEOUT_SECONDS, SOCKET_WRITE_TIMEOUT_SECONDS);
	bufferevent_enable(cInfo->buf_ev, EV_READ | EV_WRITE | EV_PERSIST);


}

int runServer() {

	evutil_socket_t listenfd;
	struct sockaddr_in listen_addr;
	struct event ev_accept;

	// Create our listening socket 
	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	if (listenfd < 0) {
		perror("listen failed");
		return 1;
	}

	evutil_make_listen_socket_reuseable(listenfd);

	memset(&listen_addr, 0, sizeof(listen_addr));
	listen_addr.sin_family = AF_INET;
	listen_addr.sin_addr.s_addr = INADDR_ANY;
	listen_addr.sin_port = htons(SERVER_PORT);

	if (bind(listenfd, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
		perror("bind failed");
		return 1;
	}

	if (listen(listenfd, CONNECTION_BACKLOG) < 0) {
		perror("listen failed");
		return 1;
	} 

	printf("Listening....");

	if (evutil_make_socket_nonblocking(listenfd) < 0) {
		perror("failed to set server socket to non-blocking");
		return 1;
	}
	
	if ((evbase_accept = event_base_new()) == NULL) {
		perror("Unable to create socket accept event base");
		evutil_closesocket(listenfd);
		return 1;
	}

	event_set(&ev_accept, listenfd, EV_READ | EV_PERSIST, on_accept, (void *)evbase_accept);
	event_base_set(evbase_accept, &ev_accept);
	event_add(&ev_accept, NULL);

	// Init list
	listHead = new userClientNode();
	listHead->next = nullptr;
	lp = listHead;

	printf("Server running. \n");

	// Start the event loop
	event_base_dispatch(evbase_accept);

	event_base_free(evbase_accept);

	evbase_accept = NULL;
	
	evutil_closesocket(listenfd);

	printf("Server shutdown.\n");

	return 0;
}


void killServer() {
	fprintf(stdout, "Stopping socket listener event loop.\n");
	if (event_base_loopexit(evbase_accept, NULL) < 0) {
		perror("Error shutting down server");
	}
	fprintf(stdout, "Stopping workers.\n");
}

static void sigHandler(int signal) {
	fprintf(stdout, "Received signal %d.  Shutting down.\n", signal);
	killServer();
}

int main() {
	return runServer();
}