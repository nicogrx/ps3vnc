//#include <stdio.h>
#include <psl1ght/lv2/net.h>
#include <io/pad.h>
#include "remoteprint.h"
#include "screen.h"
#include "rfb.h"
#include "vncauth.h"

int handshake(char * password);
int authenticate(char * password);

RFB_INFO rfb_info;

int main(int argc, const char* argv[])
{
	PadInfo padinfo;
	PadData paddata;
#if 1
	u32 * test_rectangle;
#endif
	int i, port, ret=0;
	const char ip[] = "192.168.1.86";
	char password[] = "nicogrx";

	ret = netInitialize();
	if (ret < 0)
		return ret;

#ifdef VERBOSE
	ret = remotePrintConnect();
	if (ret<0)
		goto end;
#endif
	RPRINT("PS3 Vnc viewer started!\n");
	
	ioPadInit(7);
	initScreen();
#if 1
	test_rectangle = malloc(50*50*4);
	for(i=0;i<(50*50);i++)
	{
		test_rectangle[i]=0x00FF0000; //RED
	}
	drawRectangleToScreen((const u32*)test_rectangle, 50, 50, 100, 100);
	free(test_rectangle);
	updateScreen();
#endif

	for(port=5900;port<5930;port++)
	{
		ret = rfbConnect(ip,port);
		if (ret>=0)
		{
			RPRINT("connected to serveur %s:%i\n", ip, port);
			break;
		}
	}
	if (ret<0)
	{
		RPRINT("failed to connect to server %s\n", ip);
		goto clean_rp;
	}

	ret = handshake(password);
	if (ret<0)
		goto clean;
	RPRINT("handshake OK\n");

	while(1)
	{
		ioPadGetInfo(&padinfo);
		for(i=0; i<MAX_PADS; i++)
		{
			if(padinfo.status[i])
			{
				ioPadGetData(i, &paddata);
				if(paddata.BTN_CROSS) {
					RPRINT("PS3 Vnc viewer end\n");
					goto clean;
				}
			}		
		}
	}

clean:
	rfbClose();
clean_rp:
#ifdef VERBOSE
	remotePrintClose();
#endif
end:
	netDeinitialize();
	return ret;
}

int handshake(char * password)
{
	int ret;
	char * reason=NULL;

	ret = rfbGetProtocolVersion();
	if (ret<0)
	{
		RPRINT("failed to get protocol version\n");
		goto end;
	}
	rfb_info.version = ret;
	ret = rfbSendProtocolVersion(RFB_003_003);
	if (ret<0)
	{
		RPRINT("failed to send protocol version\n");
		goto end;
	}
	ret = rfbGetSecurityType();
	if (ret<0)
	{
		RPRINT("failed to get security type\n");
		goto end;
	}
	rfb_info.security_type=ret;
	RPRINT("security type:%i\n", rfb_info.security_type);
	switch(rfb_info.security_type)
	{
		case RFB_SEC_TYPE_VNC_AUTH:
			// authentication is needed
			ret = authenticate(password);
			if (ret!=RFB_SEC_RESULT_OK)
			{
				RPRINT("failed to authenticate, security result:%i\n", ret);
				ret = -1;
			}
			break;
		case RFB_SEC_TYPE_NONE:
			//switch to init phase
			ret=0;
			RPRINT("no authentication needed\n");
			break;	
		case RFB_SEC_TYPE_INVALID: 
			ret = rfbGetString(reason);
			if (ret>0)
			{
				RPRINT("%s_n",reason);
				free(reason);
			}
			ret = -1;
			break;
		default:
			RPRINT("security type is not supported\n");
	}

end:
	return ret;
}

int authenticate(char * password)
{
	int ret;
	unsigned char challenge[16];
	ret = rfbGetSecurityChallenge(challenge);
	if (ret<0)
		goto end;
	vncEncryptBytes(challenge, password);
	ret = rfbSendSecurityChallenge(challenge); 
	if (ret<0)
		goto end;
	ret = rfbGetSecurityResult();
end:
	return ret;
}

