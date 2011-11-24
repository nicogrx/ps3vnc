#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/net.h>
#include <arpa/inet.h>

#include "remoteprint.h"

#define PORT	5899

int rp_sock;

int remotePrintConnect(const char * ip)
{
	int ret;
	struct sockaddr_in server;

	rp_sock = netSocket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (rp_sock < 0) {
		ret = -1;
		goto end;
	}
	memset(&server, 0, sizeof(server));
	server.sin_len = sizeof(server);
	server.sin_family = AF_INET;
	inet_pton(AF_INET, ip, &server.sin_addr);
	server.sin_port = htons(PORT);
	ret = netConnect(rp_sock, (struct sockaddr*)&server, sizeof(server));
end:
	return ret;
}

void remotePrintClose(void)
{
	shutdown(rp_sock, SHUT_RDWR);
	close(rp_sock);
}

void remotePrint(const char * fmt, ...)
{
	va_list ap;
	char buffer[128];
#if 0
	time_t t;
	memset(buffer, 0, sizeof(buffer));
	t = time(NULL);
	sprintf(buffer, "%lu: ", t);
	netSend(rp_sock, buffer, strlen(buffer), 0);
#endif
	memset(buffer, 0, sizeof(buffer));
	va_start(ap, fmt);
	vsnprintf(buffer, sizeof(buffer)-1, fmt, ap);
	va_end(ap);
	netSend(rp_sock, buffer, strlen(buffer), 0);
}

int remoteSendBytes(unsigned char * bytes, int size)
{
	int ret=0;
	int bytes_to_write=size;

	while (bytes_to_write)
		{
			ret = netSend(rp_sock, bytes, bytes_to_write, 0);
			if (ret<0)
				break;
			bytes_to_write-=ret;
		}
	if (ret>=0)
		ret = size;
	return ret;
}

