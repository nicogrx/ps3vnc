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

int rfb_sock;

int rfbConnect(const char * ip, int port)
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

int rfbGetBytes(unsigned char * bytes, int size)
{
	int ret=0;
	int bytes_to_read=size;
	while (bytes_to_read)
		{
			ret = read(rfb_sock, bytes, bytes_to_read);
			if (ret<0)
				break;
			bytes_to_read-=ret;
		}
	if (ret>=0)
	{
		ret = size;
#ifdef VERBOSE
		{
			int i;
			RPRINT("bytes received:\n");
			for (i=0;i<size;i++)
			{
				RPRINT("%x,", bytes[i]);
			}
			RPRINT("\n");
		}
#endif
	}
	return ret;
}

int rfbSendBytes(unsigned char * bytes, int size)
{
	int ret=0;
	int bytes_to_write=size;

#ifdef VERBOSE
	{
		int i;
		RPRINT("bytes to send:\n");
		for (i=0;i<size;i++)
		{
			RPRINT("%x,", bytes[i]);
		}
		RPRINT("\n");
	}
#endif

	while (bytes_to_write)
		{
			ret = write(rfb_sock, bytes, bytes_to_write);
			if (ret<0)
				break;
			bytes_to_write-=ret;
		}
	if (ret>=0)
		ret = size;
	return ret;
}

int rfbGetProtocolVersion(void)
{
	int ret=-1;
	char value[13];
	value[12]='\0';
	ret=rfbGetBytes((unsigned char*)value, 12);
	if (ret<0)
	{
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
	ret = rfbSendBytes((unsigned char*)value, 12);
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

	ret = rfbGetBytes(&number,1);
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
	ret = rfbGetBytes(types, (int)number);
	if (ret<0)
	{
		free(types);
	}
end:
	return ret;
}	

int rfbSendSecurityType(unsigned char type) // version 3.7 onwards
{
	int ret;
	ret = rfbSendBytes(&type, 1);
	return ret;
}

int rfbGetSecurityType(void) // version 3.3
{
	int ret;
	unsigned int type;
	ret = rfbGetBytes((unsigned char*)&type, 4);
	if (ret<0)
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
	ret = rfbGetBytes((unsigned char*)&result, 4);
	if (ret<0)
	{
		goto end;
	}
	ret = (int)result;
end:
	return ret;
}

int rfbGetSecurityChallenge(unsigned char * challenge) 
{
	int ret;
	ret = rfbGetBytes(challenge , 16);
	return ret;
}

int rfbSendSecurityChallenge(unsigned char * challenge) 
{
	int ret;
	ret = rfbSendBytes(challenge , 16);
	return ret;
}

int rfbSendClientInit(unsigned char flag) 
{
	int ret;
	if (flag != RFB_NOT_SHARED && flag!=RFB_SHARED)
		return -1;
	ret = rfbSendBytes(&flag,1);
	return ret;
}

int rfbGetServerInitMsg(RFB_SERVER_INIT_MSG * server_init_msg)
{
	int ret;
	ret = rfbGetBytes((unsigned char*)server_init_msg, sizeof(RFB_SERVER_INIT_MSG));
	if (ret!=sizeof(RFB_SERVER_INIT_MSG))
		ret=-1;
	return ret;
}

int rfbSendMsg(unsigned int msg_type, void * data)
{
	int ret = 0;
	switch (msg_type)
	{
		case RFB_SetPIxelFormat:
			{
				RFB_SET_PIXEL_FORMAT* rspf;
				rspf = (RFB_SET_PIXEL_FORMAT*)data;
				rspf->msg_type = (unsigned char)RFB_SetPIxelFormat;
			}	
			ret = rfbSendBytes((unsigned char*)data, sizeof(RFB_SET_PIXEL_FORMAT));
		break;
		
		case RFB_SetEncodings:
			{
				RFB_SET_ENCODINGS * rse;
				rse = (RFB_SET_ENCODINGS*)data;
				rse->msg_type = (unsigned char)RFB_SetEncodings;
			
				ret = rfbSendBytes((unsigned char*)data, 4);
				if (ret<0)
					goto end;
				ret = rfbSendBytes((unsigned char*)(rse->encoding_type),(int)rse->number_of_encodings);
			}
		break;
		
		case RFB_FramebufferUpdateRequest:
			{
				RFB_FRAMEBUFFER_UPDATE_REQUEST * rfbur;
				rfbur = (RFB_FRAMEBUFFER_UPDATE_REQUEST*)data;
				rfbur->msg_type = (unsigned char)RFB_FramebufferUpdateRequest;
			}
			ret = rfbSendBytes((unsigned char*)data, sizeof(RFB_FRAMEBUFFER_UPDATE_REQUEST));
		break;
		
		case RFB_KeyEvent:
			{
				RFB_KEY_EVENT* rke;
				rke = (RFB_KEY_EVENT*) data;
				rke->msg_type = (unsigned char)RFB_KeyEvent;
			}
			ret = rfbSendBytes((unsigned char*)data, sizeof(RFB_KEY_EVENT));
		break;
		
		case RFB_PointerEvent :
			{
				RFB_POINTER_EVENT* rpe;
				rpe = (RFB_POINTER_EVENT*) data;
				rpe->msg_type = (unsigned char)RFB_PointerEvent;
			}
			ret = rfbSendBytes((unsigned char*)data, sizeof(RFB_POINTER_EVENT));
		break;
		
		case RFB_ClientCutText:
			{
				RFB_CLIENT_CUT_TEXT * rcct;
				rcct = (RFB_CLIENT_CUT_TEXT*)data;
				rcct->msg_type = (unsigned char)RFB_ClientCutText;
			
				ret = rfbSendBytes((unsigned char*)data, 8);
				if (ret<0)
					goto end;
				ret = rfbSendBytes((unsigned char*)(rcct->text),(int)rcct->length);
			}
		break;
		
		default:
			ret = -1;
			RPRINT("unknown client to server msg type:%d\n", msg_type);
	}
end:
	return ret;
}

int rfbGetMsg(void * data)
{
	int ret = 0;
	unsigned char msg_type;
	ret = rfbGetBytes(&msg_type, 1);
	if (ret<0)
		goto end;
	switch ((int)msg_type)
	{
		case RFB_FramebufferUpdate:
			RPRINT("received msg RFB_FramebufferUpdate from server\n");
			{
				RFB_FRAMEBUFFER_UPDATE * rfbu;
				rfbu = (RFB_FRAMEBUFFER_UPDATE *)data;
				ret = rfbGetBytes(((unsigned char *)rfbu)+1, sizeof(RFB_FRAMEBUFFER_UPDATE)-1);
			}
			break;

		case RFB_SetColourMapEntries:
			RPRINT("received msg RFB_SetColourMapEntries from server\n");
			{
				RFB_SET_COLOUR_MAP_ENTRIES * rsmpe;
				rsmpe = (RFB_SET_COLOUR_MAP_ENTRIES *)data;
				ret = rfbGetBytes(((unsigned char *)rsmpe)+1, sizeof(RFB_SET_COLOUR_MAP_ENTRIES)-1);
			}
			break;
			
		case RFB_Bell:
			RPRINT("received msg RFB_Bell from server\n");
			ret = RFB_Bell;
			break;

		case RFB_ServerCutText:
			RPRINT("received msg RFB_ServerCutText from server\n");
			{
				RFB_SERVER_CUT_TEXT * rsct;
				rsct = (RFB_SERVER_CUT_TEXT *)data;
				ret = rfbGetBytes(((unsigned char *)rsct)+1, sizeof(RFB_SERVER_CUT_TEXT)-1);
			}
			break;
		
		default:
			ret=-1;
			RPRINT("unknown msg type received from server:%d\n", msg_type);
			break;
	}
end:
	return ret;
}

int rfbGetRectangleInfo(void * data)
{
	int ret;
	RFB_FRAMEBUFFER_UPDATE_RECTANGLE * rfbur;
	rfbur = (RFB_FRAMEBUFFER_UPDATE_RECTANGLE *)data;
	ret = rfbGetBytes(((unsigned char *)rfbur), sizeof(RFB_FRAMEBUFFER_UPDATE_RECTANGLE));
	return ret;
}
