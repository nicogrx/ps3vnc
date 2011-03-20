#include <psl1ght/lv2/net.h>
#include <io/pad.h>
#include <sysutil/video.h>
#include "remoteprint.h"
#include "screen.h"
#include "rfb.h"
#include "vncauth.h"
#include "keysymdef.h"
#include "mouse.h"

int handshake(char * password);
int authenticate(char * password);
int init(void);
int view(void);
int handlePadEvents(void);
int handleRectangle(void);

RFB_INFO rfb_info;
unsigned char input_msg[32];
unsigned char output_msg[32];
PadInfo padinfo;
PadData paddata;

int main(int argc, const char* argv[])
{
	int port, ret=0;
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
	RPRINT("start PS3 Vnc viewer\n");
	
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

	initScreen();
	ret = init();
	if (ret<0)
		goto clean;
	RPRINT("Init OK\n");

	ret = view(); // will loop	until exit condition is reached

	if (rfb_info.server_name_string!=NULL)
		free(rfb_info.server_name_string);

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
		RFB_SET_ENCODINGS * rse = (RFB_SET_ENCODINGS *)output_msg;
		rse->number_of_encodings = 2;
		int encoding_type[2] = {RFB_Raw, RFB_CopyRect};	
		rse->encoding_type = encoding_type;
		ret = rfbSendMsg(RFB_SetEncodings, rse);
	}

end:
	return ret;
}

// infinite loop
// - request frame update
// - handle incoming msgs and render screen 
int view(void)
{
	int i, ret;
	ioPadInit(7);

	while(1) // main loop
	{
		//handle joystick events
		RPRINT("handle pad events\n");
		ret = handlePadEvents();
		if (ret<0)
			break;

		// request framebuffer update
		RPRINT("request framebuffer update\n");
		RFB_FRAMEBUFFER_UPDATE_REQUEST * rfbur = (RFB_FRAMEBUFFER_UPDATE_REQUEST *)output_msg;
		rfbur->incremental = 1;
		rfbur->x_position = 0;
		rfbur->y_position = 0;
		rfbur->width = rfb_info.server_init_msg.framebuffer_width;
		rfbur->height = rfb_info.server_init_msg.framebuffer_height;
		ret = rfbSendMsg(RFB_FramebufferUpdateRequest, rfbur);
		if (ret<0)
			break;

		//handle server msgs
		RPRINT("get server message\n");
		ret = rfbGetMsg(input_msg);
		if (ret<0)
			break;
		
		switch (input_msg[0])
		{
			case RFB_FramebufferUpdate:
				{
					RFB_FRAMEBUFFER_UPDATE * rfbu = (RFB_FRAMEBUFFER_UPDATE *)input_msg;
					for (i=0;i<rfbu->number_of_rectangles;i++)
					{
						ret = handleRectangle();
						if (ret<0)
							goto end;
					}
					//render screen
					RPRINT("render screen\n");
					updateScreen();
				}		
				break;
			case RFB_Bell:
				break;
			case RFB_ServerCutText:
			case RFB_SetColourMapEntries:
			default:
				RPRINT("cannot handle msg type:%d\n", input_msg[0]);
				ret=-1;
				goto end;
		}

		
	} // end main loop

end:
	return ret;
}


int handlePadEvents(void)
{
	int i, ret=0, event=0;
	static int buttons_state=0;
	static int x=0;
	static int y=0;

	// first check joystick input events
	ioPadGetInfo(&padinfo);
	for(i=0; i<MAX_PADS; i++)
	{
		if(padinfo.status[i])
		{
			ioPadGetData(i, &paddata);
			if(paddata.BTN_TRIANGLE)
			{
				RPRINT("exit on user request\n");
				ret = -1;
				goto end;
			}

			RFB_POINTER_EVENT * rpe = (RFB_POINTER_EVENT *)output_msg;
			
			if (paddata.BTN_SQUARE)
			{
				event = 1;
				buttons_state |= M_LEFT;
			}
			else if (buttons_state & M_LEFT)
			{
				event = 1;
				buttons_state &= ~M_LEFT;
			}

			if (paddata.BTN_CIRCLE)
			{
				event = 1;
				buttons_state |= M_RIGHT;
			}
			else if (buttons_state & M_RIGHT)
			{
				event = 1;
				buttons_state &= ~M_RIGHT;
			}

			if (paddata.BTN_L1)
			{
				event = 1;
				buttons_state |= M_WHEEL_DOWN;
			}
			else if (buttons_state & M_WHEEL_DOWN)
			{
				event = 1;
				buttons_state &= ~M_WHEEL_DOWN;
			}

			if (paddata.BTN_R1)
			{
				event = 1;
				buttons_state |= M_WHEEL_UP;
			}
			else if (buttons_state & M_WHEEL_UP)
			{
				event = 1;
				buttons_state &= ~M_WHEEL_UP;
			}

			if (paddata.BTN_LEFT)
			{
				event = 1;
				if (x > 0)
					x-=1;
			}
			
			if (paddata.BTN_RIGHT)
			{
				event = 1;
				if (x < res.width)
					x+=1;
			}
			
			if (paddata.BTN_UP)
			{
				event = 1;
				if (y > 0)
					y-=1;
			}
			
			if (paddata.BTN_DOWN)
			{
				event = 1;
				if (y < res.height)
					y+=1;
			}

			if (event)
			{
				rpe->button_mask = buttons_state;
				rpe->x_position = x;
				rpe->y_position = y;
				ret = rfbSendMsg(RFB_PointerEvent, rpe);
			}
			
			break;
		}
	}

end:
	return ret;
}

int	handleRectangle(void)
{
	int ret=0;
	unsigned int * pixel_data = NULL;
	RFB_FRAMEBUFFER_UPDATE_RECTANGLE rfbur;
		
	ret = rfbGetRectangleInfo(&rfbur);
	if (ret<0)
		goto end;

	RPRINT("update rectangle\nx_position:%d\ny_position:%d\nwidth:%d\nheight:%d\nencoding_type:%d\n",
		rfbur.x_position,
		rfbur.x_position,
		rfbur.width,
		rfbur.height,
		rfbur.encoding_type);

	switch (rfbur.encoding_type)
	{
		case RFB_Raw:
			pixel_data = (unsigned int *)malloc(rfbur.width*rfbur.height*
				rfb_info.server_init_msg.pixel_format.bits_per_pixel);
			if (pixel_data == NULL)
			{
				RPRINT("unable to allocate pixel_data\n");
				ret=-1;
				goto end;
			}
			// FIXME big_endian_flag must be handled !!!
			drawRectangleToScreen((const unsigned int*)pixel_data,
			(unsigned int)rfbur.width,
			(unsigned int)rfbur.height,
			(unsigned int)rfbur.x_position,
			(unsigned int)rfbur.y_position);
			free(pixel_data);
			break;
		case RFB_CopyRect:
			{
				RFB_COPYRECT_INFO rci;
				ret = rfbGetBytes((unsigned char *)&rci, sizeof(RFB_COPYRECT_INFO));
				if (ret<0)
				{
					RPRINT("failed to get RFB_COPYRECT_INFO\n");
					goto end;
				}
				pixel_data = getCurrentFrameBuffer()+rci.src_y_position*res.width
					+rci.src_x_position;
				drawRectangleToScreen((const unsigned int*)pixel_data,
				(unsigned int)rfbur.width,
				(unsigned int)rfbur.height,
				(unsigned int)rfbur.x_position,
				(unsigned int)rfbur.y_position);
			}
			break;
		default:
			RPRINT("unsupported encoding type:%d\n", rfbur.encoding_type);
			ret = -1;
			goto end;
	}

end:
	return ret;
}
