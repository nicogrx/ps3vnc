#include <psl1ght/lv2/net.h>
#include <io/pad.h>
#include <sysutil/video.h>

#include "remoteprint.h"
#include "screen.h"
#include "rfb.h"
#include "vncauth.h"

int handshake(char * password);
int authenticate(char * password);
int init(void);

RFB_INFO rfb_info;
unsigned char input_msg[32];
unsigned char output_msg[32];

int main(int argc, const char* argv[])
{
	PadInfo padinfo;
	PadData paddata;
	int i, port, ret=0;
	const char ip[] = "192.168.1.86";
	char password[] = "nicogrx";
#if 1
	u32 * test_rectangle;
#endif

	ret = netInitialize();
	if (ret < 0)
		return ret;

#ifdef VERBOSE
	ret = remotePrintConnect();
	if (ret<0)
		goto end;
#endif
	RPRINT("start PS3 Vnc viewer\n");
	
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

	ret = init();
	if (ret<0)
		goto clean;

	RPRINT("Init OK\n");

	

	if (rfb_info.server_name_string!=NULL)
		free(rfb_info.server_name_string);

	while(1)
	{
		ioPadGetInfo(&padinfo);
		for(i=0; i<MAX_PADS; i++)
		{
			if(padinfo.status[i])
			{
				ioPadGetData(i, &paddata);
				if(paddata.BTN_CROSS) {
					RPRINT("end PS3 Vnc viewer\n");
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

int init(void)
{
	int ret;
	ret = rfbSendClientInit(RFB_NOT_SHARED);
	if (ret<0)
	{
		RPRINT("failed to send client init msg\n");
		goto end;
	}
	ret =rfbGetServerInitMsg(&(rfb_info.server_init_msg));
	if (ret<0)
	{
		RPRINT("failed to get server init msg\n");
		goto end;
	}
	RPRINT("framebuffer_width:%i\nframebuffer_height:%i\n",
		rfb_info.server_init_msg.framebuffer_width,
		rfb_info.server_init_msg.framebuffer_height);
	RPRINT("\nPIXEL FORMAT:\n\n");
	RPRINT("bits_per_pixel:%i\ndepth:%i\nbig_endian_flag:%i\ntrue_colour_flag:%i\n",
		rfb_info.server_init_msg.pixel_format.bits_per_pixel,
		rfb_info.server_init_msg.pixel_format.depth,
		rfb_info.server_init_msg.pixel_format.big_endian_flag,
		rfb_info.server_init_msg.pixel_format.true_colour_flag);
	RPRINT("red_max:%i\ngreen_max:%i\nblue_max:%i\n",
		rfb_info.server_init_msg.pixel_format.red_max,
		rfb_info.server_init_msg.pixel_format.green_max,
		rfb_info.server_init_msg.pixel_format.blue_max);
	RPRINT("red_shift:%i\ngreen_shift:%i\nblue_shift:%i\n",
		rfb_info.server_init_msg.pixel_format.red_shift,
		rfb_info.server_init_msg.pixel_format.green_shift,
		rfb_info.server_init_msg.pixel_format.blue_shift);

	ret = rfbGetString(rfb_info.server_name_string);
	if (ret<0)
	{
		RPRINT("failed to get server name string\n");
		goto end;
	}
	RPRINT("server name:%s\n",rfb_info.server_name_string);

	// check width & height
	if (rfb_info.server_init_msg.framebuffer_width>res.width ||
			rfb_info.server_init_msg.framebuffer_height>res.height)
	{
		RPRINT("cannot handle frame size: with=%i, height=%i\n",
			rfb_info.server_init_msg.framebuffer_width,
			rfb_info.server_init_msg.framebuffer_height);
		ret=-1;
		goto end;
	}
	
	// FIXME: in theory, every pixel format should be supported ...
	// at least, all bpp&depth
	// moreover, should be possible to send prefered PIXEL FORMAT to server
	// instead of just existing...

	// check bpp & depth
	if (rfb_info.server_init_msg.pixel_format.bits_per_pixel!=32 ||
			rfb_info.server_init_msg.pixel_format.depth!=24)
	{
		RPRINT("cannot handle bpp=%d, depth=%d\n",
		rfb_info.server_init_msg.pixel_format.bits_per_pixel,
		rfb_info.server_init_msg.pixel_format.depth);
		ret=-1;
		goto end;

	}

	// check colour mode
	if (rfb_info.server_init_msg.pixel_format.true_colour_flag!=1)
	{
		RPRINT("cannot handle colour map\n");
		ret=-1;
		goto end;
	}

	// now send supported encodings formats
	{
		RFB_SET_ENCODINGS * rse = (RFB_SET_ENCODINGS *)input_msg;
		rse->number_of_encodings = 2;
		int encoding_type[2] = {RFB_Raw, RFB_CopyRect};	
		rse->encoding_type = encoding_type;
		ret = rfbSendMsg(RFB_SetEncodings, rse);
	}

end:
	return ret;
}
