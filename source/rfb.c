#include <psl1ght/lv2/net.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "rfb.h"
#include "remoteprint.h"

#define DEFAULT_PORT 5901
int rfb_sock;

int rfbConnect(char * ip, int port)
{
	int ret;
	struct sockaddr_in server;

	rfb_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (rfb_sock < 0) {
		ret = -1;
		goto end;
	}
	memset(&server, 0, sizeof(server));
	server.sin_len = sizeof(server);
	server.sin_family = AF_INET;
	inet_pton(AF_INET, ip, &server.sin_addr);

	if (port == -1)
	{
		port = DEFAULT_PORT;
	}
	server.sin_port = htons(port);
	ret = connect(rfb_sock, (struct sockaddr*)&server, sizeof(server));
end:
	return ret;
}

void rfbClose(void)
{
	shutdown(rfb_sock, SHUT_RDWR);
	close(rfb_sock);
}

int rfbGetByte(unsigned char * byte)
{
	int ret;
	ret = read(rfb_sock, byte, 1);
	if (ret!=1)
	{
		ret =-1;
	}
	return ret;
}

int rfbSendByte(unsigned char byte)
{
	int ret;
	ret = write(rfb_sock, &byte, 1);
	if (ret!=1)
	{
		ret =-1;
	}
	return ret;
}

int rfbGetU32(unsigned int * u32)
{
	int ret;
	ret = read(rfb_sock, u32, 4);
	if (ret!=4)
	{
		ret =-1;
	}
	return ret;
}

int rfbGetString(char * string)
{
	int ret;
	unsigned int length;
	ret = rfbGetU32(&length);
	if (ret==-1)
	{
		goto end;
	}
	string = (char*) malloc((int)length+1);
	memset(string, 0, (int)length+1);
	ret = read(rfb_sock, string, (int)length);
	if (ret!=(int)length)
	{
		free(string);
		ret = -1;
	}
end:
	return ret;
}

int rfbGetProtocolVersion(void)
{
	int ret=-1;
	char value[13];
	value[12]='\0';
	ret=read(rfb_sock, value, 12);
	if (ret != 12)
	{
		ret = -1;
		goto end;
	}
	RPRINT("get server protocol version:%s\n",value);
	if(strcmp(value,"RFB 003.003\n")==0)
	{
		ret=RFB_003_003;
	}
	else if (strcmp(value,"RFB 003.007\n")==0)
	{
		ret=RFB_003_007;
	}
	else if (strcmp(value,"RFB 003.008\n")==0)
	{
		ret=RFB_003_008;
	}
end:
	return ret;
}

int rfbSendProtocolVersion(int version)
{
	int ret=0;
	char value[13];
	switch (version)
	{
		case RFB_003_003:
			strcpy(value,"RFB 003.003\n");
			break;
		case RFB_003_007:
			strcpy(value,"RFB 003.007\n");
			break;
		case RFB_003_008:
			strcpy(value,"RFB 003.008\n");
			break;
		default:
			ret=-1;
			RPRINT("unknown protocol version:%d\n", value);
			goto end;
	}
	ret = write(rfb_sock, value, 12);
	if (ret != 12)
	{
		ret = -1;
		goto end;
	}
	RPRINT("send server protocol version:%s\n",value);
end:
	return ret;
}

int rfbGetSecurityTypes(unsigned char * types) // version 3.7 onwards
{
	int ret;
	unsigned char number;

	ret = rfbGetByte(&number);
	if (ret==-1)
	{
		goto end;
	}
	if (number == 0) // server cannot support desired protocol version
	{
		ret = 0;
		goto end;
	}
	types = (unsigned char*) malloc((int)number);
	ret = read(rfb_sock, types, (int)number);
	if (ret!=(int)number)
	{
		free(types);
		ret = -1;
	}
end:
	return ret;
}	

int rfbSendSecurityType(unsigned char type) // version 3.7 onwards
{
	int ret;
	ret = rfbSendByte(type);
	return ret;
}

int rfbGetSecurityType(void) // version 3.3
{
	int ret;
	unsigned int type;
	ret = rfbGetU32(&type);
	if (ret == -1)
	{
		goto end;
	}
	ret = (int)type;
end:
	return ret;
}

int rfbGetSecurityResult(void)
{
	int ret;
	unsigned int result;
	ret = rfbGetU32(&result);
	if (ret == -1)
	{
		goto end;
	}
	ret = (int)result;
end:
	return ret;
}


