//#include <stdio.h>
#include <psl1ght/lv2/net.h>
#include <io/pad.h>
#include "remoteprint.h"
#include "screen.h"
#include "rfb.h"

int start(void);

int main(int argc, const char* argv[])
{
	PadInfo padinfo;
	PadData paddata;
#if 1
	u32 * test_rectangle;
#endif
	int i, ret=0;

	ret = netInitialize();
	if (ret < 0)
		return ret;

#ifdef VERBOSE
	ret = remotePrintConnect();
	if (ret < 0)
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

	ret = start();

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
#ifdef VERBOSE
	remotePrintClose();
#endif
end:
	netDeinitialize();
	return ret;
}

int start(void)
{
	int ret, version, security_type;
	char * reason=NULL;
	
	ret = rfbConnect("192.168.1.86", -1);
	if (ret<0)
	{
		RPRINT("failed to connect to vncserver\n");
		goto end;
	}
	
	ret = rfbGetProtocolVersion();
	if (ret<0)
	{
		RPRINT("failed to get protocol version\n");
		goto close;
	}
	version = ret;
	ret = rfbSendProtocolVersion(RFB_003_003);
	if (ret<0)
	{
		RPRINT("failed to send protocol version\n");
		goto close;
	}
	ret = rfbGetSecurityType();
	if (ret<0)
	{
		RPRINT("failed to get security type\n");
		goto close;
	}
	security_type=ret;
	RPRINT("security type:%i\n", security_type);
	if (security_type==0)
	{
		ret = rfbGetString(reason);
		if (ret>0)
		{
			RPRINT("%s_n",reason);
			free(reason);
			goto close;
		}
	}

close:
	rfbClose();
end:
	return ret;
}

