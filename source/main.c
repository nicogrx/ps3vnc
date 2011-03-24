#include <unistd.h>
#include <psl1ght/lv2/net.h>
#include <psl1ght/lv2/thread.h>
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
void handlePadEvents(u64 arg);
void handleMsgs(u64 arg);
void requestFrame(u64 arg);
int handleRectangle(void);

RFB_INFO rfb_info;
unsigned char input_msg[32];
unsigned char output_msg[32];
PadInfo padinfo;
PadData paddata;
unsigned char * raw_pixel_data = NULL;
int vnc_end;

int main(int argc, const char* argv[])
{
	int port, ret=0;
	const char ip[] = "192.168.1.86";
	//const char ip[] = "192.168.1.5";
	char password[] = "nicogrx";

	u64 thread_arg = 0x1337;
	u64 priority = 1500;
	u64 retval;
	size_t stack_size = 0x1000;
	char *hpe_name = "Handle Pad Event";
	char *handle_msg_name = "Handle Message";
	char *request_frame_name = "Request Frame";
	sys_ppu_thread_t hpe_id, hmsg_id, rf_id;

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

	raw_pixel_data = (unsigned char *)malloc(
		rfb_info.server_init_msg.framebuffer_width*
		rfb_info.server_init_msg.framebuffer_height*
		(rfb_info.server_init_msg.pixel_format.bits_per_pixel/8));
	if (raw_pixel_data == NULL)
	{
		RPRINT("unable to allocate raw_pixel_data array\n");
		ret=-1;
		goto clean;
	}

	ret = sys_ppu_thread_create(&hpe_id, handlePadEvents, thread_arg,
		priority, stack_size, THREAD_JOINABLE, hpe_name);
	ret = sys_ppu_thread_create(&hmsg_id, handleMsgs, thread_arg,
		priority, stack_size, THREAD_JOINABLE, handle_msg_name);
	ret = sys_ppu_thread_create(&rf_id, requestFrame, thread_arg,
		priority, stack_size, THREAD_JOINABLE, request_frame_name);
	
	ret = sys_ppu_thread_join(hpe_id, &retval);
	ret = sys_ppu_thread_join(hmsg_id, &retval);
	ret = sys_ppu_thread_join(rf_id, &retval);
	
	if(raw_pixel_data!=NULL)
		free(raw_pixel_data);

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
			{
				int l;
				ret = rfbGetBytes((unsigned char*)&l, 4);
				if (ret<0)
					goto end;

				reason = (char*)malloc(l+1);
				if (reason == NULL)
				{
					RPRINT("failed to allocate %d bytes\n", l);
					ret = -1;
					goto end;
				}
				ret = rfbGetBytes((unsigned char*)(reason), l);
				if (ret<0)
				{
					free(reason);
					goto end;
				}
				reason[l]='\0';
				RPRINT("%s\n",reason);
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

	if ( rfb_info.server_init_msg.name_length!=0)
	{
		int l = rfb_info.server_init_msg.name_length;
		rfb_info.server_name_string = NULL;
		rfb_info.server_name_string = (char*)malloc(l+1);
		if (rfb_info.server_name_string == NULL)
		{
			RPRINT("failed to allocate %d bytes\n", l+1);
			ret = -1;
			goto end;
		}
		ret = rfbGetBytes((unsigned char*)(rfb_info.server_name_string), l);
		if (ret<0)
		{
			free(rfb_info.server_name_string);
			goto end;
		}
		rfb_info.server_name_string[l]='\0';
		RPRINT("server name:%s\n",rfb_info.server_name_string);
	}

	RPRINT("framebuffer_width:%i\nframebuffer_height:%i\n",
		rfb_info.server_init_msg.framebuffer_width,
		rfb_info.server_init_msg.framebuffer_height);
	RPRINT("PIXEL FORMAT:\n");
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
		/*rse->number_of_encodings = 2;
		int encoding_type[2] = {RFB_Raw, RFB_CopyRect};*/
		rse->number_of_encodings = 1;
		int encoding_type[1] = {RFB_Raw};
		rse->encoding_type = encoding_type;
		ret = rfbSendMsg(RFB_SetEncodings, rse);
	}

end:
	return ret;
}

void requestFrame(u64 arg)
{
	int ret = 0;

	while(!vnc_end)
	{
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
		sys_ppu_thread_yield();
		usleep(500000);
	}
	sys_ppu_thread_exit(0);
	return;
}

// handle incoming msgs and render screen 
void handleMsgs(u64 arg)
{
	int i, ret;
	while(!vnc_end) // main loop
	{
		//handle server msgs
		RPRINT("waiting for server message\n");
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
					// FIXME big_endian_flag must be handled !
					waitFlip();
					drawRectangleToScreen((unsigned int*)raw_pixel_data,
					(unsigned int)rfb_info.server_init_msg.framebuffer_width,
					(unsigned int)rfb_info.server_init_msg.framebuffer_height,
					0, 0, 1);
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
		sys_ppu_thread_yield();
		
	} // end main loop

end:
	sys_ppu_thread_exit(0);
	vnc_end=1;
	return;
}

//handle joystick events
void handlePadEvents(u64 arg)
{
	int i, ret=0, event=0;
	static int buttons_state=0;
	static int x=0;
	static int y=0;

	RPRINT("handle pad events\n");
	ioPadInit(7);
	while(!vnc_end)
	{
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
					vnc_end=1;
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
						x-=2;
				}
			
				if (paddata.BTN_RIGHT)
				{
					event = 1;
					if (x < res.width)
						x+=2;
				}
			
				if (paddata.BTN_UP)
				{
					event = 1;
					if (y > 0)
						y-=2;
				}
			
				if (paddata.BTN_DOWN)
				{
					event = 1;
					if (y < res.height)
						y+=2;
				}

				if (event)
				{
					rpe->button_mask = buttons_state;
					rpe->x_position = x;
					rpe->y_position = y;
					ret = rfbSendMsg(RFB_PointerEvent, rpe);
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
						goto end;
				}
			
				break;
			}
		}
		sys_ppu_thread_yield();
		usleep(20000);
	}
end:
	vnc_end=1;
	sys_ppu_thread_exit(0);
	return;
}

int	handleRectangle(void)
{
	int ret=0;
	int bpp, bpw, h;
	unsigned char * start;
	RFB_FRAMEBUFFER_UPDATE_RECTANGLE rfbur;
		
	ret = rfbGetRectangleInfo(&rfbur);
	if (ret<0)
		goto end;

	RPRINT("update rectangle\nx_position:%d\ny_position:%d\nwidth:%d\nheight:%d\nencoding_type:%d\n",
		rfbur.x_position,
		rfbur.y_position,
		rfbur.width,
		rfbur.height,
		rfbur.encoding_type);

	switch (rfbur.encoding_type)
	{
		case RFB_Raw:	
			//get array of pixels
			
			bpp=rfb_info.server_init_msg.pixel_format.bits_per_pixel/8;
			bpw=rfbur.width*bpp;
			start = raw_pixel_data +
				rfbur.y_position*rfb_info.server_init_msg.framebuffer_width*bpp +
				rfbur.x_position*bpp;

			for(h = 0; h < rfbur.height; h++)
			{
				ret = rfbGetBytes(start, bpw);
				if (ret<0)
				{
					RPRINT("failed to get line of %d pixels\n", bpw);
					goto end;
				}
				start+=rfb_info.server_init_msg.framebuffer_width*bpp;
			}

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
