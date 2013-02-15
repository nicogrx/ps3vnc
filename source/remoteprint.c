#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/net.h>
#include <arpa/inet.h>
#include <SDL/SDL.h>
#include "remoteprint.h"
#include "tick.h"

#define PORT	5899

int rp_sock;
SDL_mutex *remote_print_mutex;

#ifdef REMOTE_PRINT
int remotePrintConnect(const char * ip)
{
	int ret;
	struct sockaddr_in server;
	remote_print_mutex=SDL_CreateMutex();
	rp_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (rp_sock < 0) {
		ret = -1;
		goto end;
	}
	memset(&server, 0, sizeof(server));
	server.sin_len = sizeof(server);
	server.sin_family = AF_INET;
	inet_pton(AF_INET, ip, &server.sin_addr);
	server.sin_port = htons(PORT);
	ret = connect(rp_sock, (struct sockaddr*)&server, sizeof(server));
end:
	return ret;
}

void remotePrintClose(void)
{
	SDL_DestroyMutex(remote_print_mutex);
	shutdown(rp_sock, SHUT_RDWR);
	close(rp_sock);
}

void remotePrint(const char * fmt, ...)
{
	va_list ap;
	char buffer[128];
	
	SDL_LockMutex(remote_print_mutex);

	memset(buffer, 0, sizeof(buffer));
	sprintf(buffer, "ticks=%u > ", getTicks());
	send(rp_sock, buffer, strlen(buffer), 0);
	memset(buffer, 0, sizeof(buffer));
	va_start(ap, fmt);
	vsnprintf(buffer, sizeof(buffer)-1, fmt, ap);
	va_end(ap);
	send(rp_sock, buffer, strlen(buffer), 0);

	SDL_UnlockMutex(remote_print_mutex);
}
#else
int remotePrintConnect(const char * ip) { return 0; }
void remotePrintClose(void) {}
void remotePrint(const char * fmt, ...) {}
#endif
