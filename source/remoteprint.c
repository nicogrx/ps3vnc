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

int rp_sock;

int remotePrintConnect(void)
{
	int ret;
	struct sockaddr_in server;
	ret = netInitialize();
	if (ret < 0)
		goto end;

	rp_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (rp_sock < 0) {
		ret = -1;
		goto end;
	}
	memset(&server, 0, sizeof(server));
	server.sin_len = sizeof(server);
	server.sin_family = AF_INET;
	inet_pton(AF_INET, IP, &server.sin_addr);
	server.sin_port = htons(PORT);
	ret = connect(rp_sock, (struct sockaddr*)&server, sizeof(server));
end:
	return ret;
}

void remotePrintClose(void)
{
	shutdown(rp_sock, SHUT_RDWR);
	close(rp_sock);
	netDeinitialize();
}

void remotePrint(const char * fmt, ...)
{
	va_list ap;
	char buffer[128];
	memset(buffer, 0, sizeof(buffer));
	va_start(ap, fmt);
	vsnprintf(buffer, sizeof(buffer)-1, fmt, ap);
	va_end(ap);
	write(rp_sock, buffer, strlen(buffer));
}

