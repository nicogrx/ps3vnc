#include <psl1ght/lv2/net.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "remoteprint.h"

#define IP		"192.168.1.86"
#define PORT	4000

int remotePrintConnect(int * sockfd)
{
	int ret;
	struct sockaddr_in server;
	ret = netInitialize();
	if (ret < 0)
		goto end;

	*sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (*sockfd < 0) {
		ret = *sockfd;
		goto end;
	}
	memset(&server, 0, sizeof(server));
	server.sin_len = sizeof(server);
	server.sin_family = AF_INET;
	inet_pton(AF_INET, IP, &server.sin_addr);
	server.sin_port = htons(PORT);

	ret = connect(*sockfd, (struct sockaddr*)&server, sizeof(server));
end:
	return ret;
}

void remotePrintClose(int sockfd)
{
	shutdown(sockfd, SHUT_RDWR);
	close(sockfd);
	netDeinitialize();
}

void remotePrint(int sockfd, const char * fmt, ...)
{
	va_list ap;
	char buffer[128];
	memset(buffer, 0, sizeof(buffer));

	va_start(ap, fmt);
	vsnprintf(buffer, sizeof(buffer)-1, fmt, ap);
	va_end(ap);
	write(sockfd, buffer, strlen(buffer));
}

