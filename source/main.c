#include <unistd.h>
#include <string.h>
#include <psl1ght/lv2/net.h>
#include <psl1ght/lv2/thread.h>
#include <io/pad.h>
#include <io/kb.h> 
#include <sysutil/video.h>
#include "remoteprint.h"
#include "screen.h"
#include "rfb.h"
#include "vncauth.h"
#define XK_MISCELLANY
#define XK_LATIN1
#include "keysymdef.h"
#include "mouse.h"

int handshake(char * password);
int authenticate(char * password);
int init(void);
int view(void);
unsigned short convertKeyCode(unsigned short keycode);
void handleInputEvents(u64 arg);
void handleMsgs(u64 arg);
void requestFrame(u64 arg);
int handleRectangle(void);

RFB_INFO rfb_info;
unsigned char input_msg[32];
unsigned char output_msg[32];
PadInfo padinfo;
PadData paddata;
unsigned char * raw_pixel_data = NULL;
unsigned char * old_raw_pixel_data = NULL;
int vnc_end;

volatile int frame_update_requested = 0;

int main(int argc, const char* argv[])
{
	int port, ret=0;
	const char ip[] = "192.168.1.86";
	//const char ip[] = "192.168.1.83";
	//const char ip[] = "192.168.1.5";
	char password[] = "nicogrx";

	u64 thread_arg = 0x1337;
	u64 priority = 1500;
	u64 retval;
	size_t stack_size = 0x1000;
	char *handle_input_name = "Handle input events";
	char *handle_msg_name = "Handle message";
	char *request_frame_name = "Request frame";
	sys_ppu_thread_t hie_id, hmsg_id, rf_id;

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

	old_raw_pixel_data = (unsigned char *)malloc(
		rfb_info.server_init_msg.framebuffer_width*
		rfb_info.server_init_msg.framebuffer_height*
		(rfb_info.server_init_msg.pixel_format.bits_per_pixel/8));

	if (old_raw_pixel_data == NULL)
	{
		RPRINT("unable to allocate old_raw_pixel_data array\n");
		ret=-1;
		goto clean;
	}

	ret = sys_ppu_thread_create(&hmsg_id, handleMsgs, thread_arg,
		priority, stack_size, THREAD_JOINABLE, handle_msg_name);
	ret = sys_ppu_thread_create(&hie_id, handleInputEvents, thread_arg,
		priority, stack_size, THREAD_JOINABLE, handle_input_name);
	ret = sys_ppu_thread_create(&rf_id, requestFrame, thread_arg,
		priority, stack_size, THREAD_JOINABLE, request_frame_name);
	
	ret = sys_ppu_thread_join(rf_id, &retval);
	RPRINT("join thread rf_id\n");
	ret = sys_ppu_thread_join(hie_id, &retval);
	RPRINT("join thread hie_id\n");
	ret = sys_ppu_thread_join(hmsg_id, &retval);
	RPRINT("join thread hmsg_id\n");
	
	if(raw_pixel_data!=NULL)
		free(raw_pixel_data);
	if(old_raw_pixel_data!=NULL)
		free(old_raw_pixel_data);

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
		rse->number_of_encodings = 2;
		int encoding_type[2] = {RFB_Raw, RFB_CopyRect};
		//rse->number_of_encodings = 1;
		//int encoding_type[1] = {RFB_Raw};
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

		if (!frame_update_requested)
		{
			frame_update_requested=1;
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
		}
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
					frame_update_requested=0;
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

					memcpy(old_raw_pixel_data, raw_pixel_data, 
						rfb_info.server_init_msg.framebuffer_width*
						rfb_info.server_init_msg.framebuffer_height*
						(rfb_info.server_init_msg.pixel_format.bits_per_pixel/8));

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

unsigned short convertKeyCode(unsigned short keycode)
{
	unsigned short symdef;

	switch(keycode)
	{
		case 0x8:
			symdef = XK_BackSpace;
			break;
		case 0x9:
			symdef = XK_Tab;
			break;
		case 0xa:
			symdef = XK_Return;
			break;
		case 0xb:
			symdef = XK_Clear;
			break;
		case 0xd:
			symdef = XK_Return;
			break;
		case 0x1b:
			symdef = XK_Escape;
			break;
		default:
			symdef=keycode;
	}
	return symdef;
}

//handle joystick events
void handleInputEvents(u64 arg)
{
	KbInfo kbinfo;
	KbData kbdata;
#define MAX_KEYPRESS 4
	unsigned short keycode[MAX_KEYPRESS];
	unsigned short old_keycode[MAX_KEYPRESS];
	int keycode_updated = 0;
	int i, j, k, ret=0;
	int pad_event=0;
	int buttons_state=0;
	int x=0;
	int y=0;
	int request_frame_update=0;

	RPRINT("handle input events\n");
	
	ioKbInit(MAX_KB_PORT_NUM);
	ioPadInit(7);
	memset(keycode, 0, sizeof(unsigned short)*MAX_KEYPRESS);
	memset(old_keycode, 0, sizeof(unsigned short)*MAX_KEYPRESS);
	
	for(i=0; i<MAX_KEYBOARDS; i++)
	{
		if(kbinfo.status[i])
		{
			ioKbSetCodeType(i, KB_CODETYPE_ASCII);
			break;
		}
	}

	while(!vnc_end)
	{
		// check keyboard input events
		ioKbGetInfo(&kbinfo);
		for(i=0; i<MAX_KEYBOARDS; i++)
		{
			if(!kbinfo.status[i])
				break;
			
			ioKbRead(i, &kbdata);
			if (!kbdata.nb_keycode)
				break;

			keycode_updated = 0;

			// check for new key pressed
			for(j=0; j<kbdata.nb_keycode;j++)
			{
				if (!kbdata.keycode[j])
					continue;
				
				request_frame_update=1;
				RPRINT("key pressed! code[%d]=%x\n", j, kbdata.keycode[j]);
				RFB_KEY_EVENT * rke = (RFB_KEY_EVENT *)output_msg;
				rke->down_flag = 1;
				rke->key = (unsigned int)convertKeyCode(kbdata.keycode[j]);
				ret = rfbSendMsg(RFB_KeyEvent, rke);
					
				for (k=0; k<MAX_KEYPRESS;k++)
				{
					if (keycode[k]==kbdata.keycode[j])
						break;
				}
				if (k==MAX_KEYPRESS) // key is not in keypress list, add it
				{
					// add new key in key pressed list
					for (k=0; k<MAX_KEYPRESS;k++)
					{
						if (keycode[k]==0)
						{
							RPRINT("add key %x in keycode[%d]\n", kbdata.keycode[j], k);
							keycode[k]=kbdata.keycode[j];
							keycode_updated=1;	
							break;
						}
					}
				} // if (k==MAX_KEYPRESS)
			}
			if (keycode_updated)
			{
				// save keypress list
				memcpy(old_keycode, keycode, 	sizeof(unsigned short)*MAX_KEYPRESS);
			}

			keycode_updated = 0;
			// check for key released
			for(k=0; k<MAX_KEYPRESS;k++)
			{
				if(old_keycode[k] == 0)
					continue;
				
				for(j=0;j<kbdata.nb_keycode;j++)
				{
					if (old_keycode[k] == kbdata.keycode[j])
						break;
				}
				if(j==kbdata.nb_keycode)
				{
					// key released
					request_frame_update=1;
					RPRINT("key released! code[%d]=%x\n", j, keycode[k]);
					RFB_KEY_EVENT * rke = (RFB_KEY_EVENT *)output_msg;
					rke->down_flag = 0;
					rke->key = (unsigned int)convertKeyCode(keycode[k]);
					ret = rfbSendMsg(RFB_KeyEvent, rke);

					keycode[k]=0;
					keycode_updated = 1;
				}
			}
			if (keycode_updated)
			{
				// save keypress list
				memcpy(old_keycode, keycode, 	sizeof(unsigned short)*MAX_KEYPRESS);
			}
			//ioKbClearBuf(i);
			break;
		}

		// check joystick input events
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

				if (paddata.BTN_SQUARE)
				{
					pad_event = 1;
					buttons_state |= M_LEFT;
				}
				else if (buttons_state & M_LEFT)
				{
					pad_event = 1;
					buttons_state &= ~M_LEFT;
				}

				if (paddata.BTN_CIRCLE)
				{
					pad_event = 1;
					buttons_state |= M_RIGHT;
				}
				else if (buttons_state & M_RIGHT)
				{
					pad_event = 1;
					buttons_state &= ~M_RIGHT;
				}

				if (paddata.BTN_L1)
				{
					pad_event = 1;
					buttons_state |= M_WHEEL_DOWN;
				}
				else if (buttons_state & M_WHEEL_DOWN)
				{
					pad_event = 1;
					buttons_state &= ~M_WHEEL_DOWN;
				}

				if (paddata.BTN_R1)
				{
					pad_event = 1;
					buttons_state |= M_WHEEL_UP;
				}
				else if (buttons_state & M_WHEEL_UP)
				{
					pad_event = 1;
					buttons_state &= ~M_WHEEL_UP;
				}

				if (paddata.BTN_LEFT)
				{
					pad_event = 1;
					if (x > 0)
						x-=1;
				}
			
				if (paddata.BTN_RIGHT)
				{
					pad_event = 1;
					if (x < res.width)
						x+=1;
				}
			
				if (paddata.BTN_UP)
				{
					pad_event = 1;
					if (y > 0)
						y-=1;
				}
			
				if (paddata.BTN_DOWN)
				{
					pad_event = 1;
					if (y < res.height)
						y+=1;
				}

				if (pad_event)
				{
					pad_event = 0;
					request_frame_update=1;
					RFB_POINTER_EVENT * rpe = (RFB_POINTER_EVENT *)output_msg;
					rpe->button_mask = buttons_state;
					rpe->x_position = x;
					rpe->y_position = y;
					ret = rfbSendMsg(RFB_PointerEvent, rpe);
				}
			}
			break;
		}
		
		if (vnc_end || (request_frame_update && !frame_update_requested))
		{
			frame_update_requested=1;
			request_frame_update=0;
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
		sys_ppu_thread_yield();
		usleep(5000);
	}
end:
	ioKbEnd();
	ioPadEnd();
	sys_ppu_thread_exit(0);
	return;
}

int	handleRectangle(void)
{
	int ret=0;
	int bpp, bpw, h, fb_bpw;
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
				unsigned char * src;
				unsigned char * dest;
				ret = rfbGetBytes((unsigned char *)&rci, sizeof(RFB_COPYRECT_INFO));
				if (ret<0)
				{
					RPRINT("failed to get RFB_COPYRECT_INFO\n");
					goto end;
				}
				
				bpp=rfb_info.server_init_msg.pixel_format.bits_per_pixel/8;
				bpw=rfbur.width*bpp;
				fb_bpw = rfb_info.server_init_msg.framebuffer_width*bpp;

				src = old_raw_pixel_data + fb_bpw * rci.src_y_position + rci.src_x_position * bpp;
				dest = raw_pixel_data + fb_bpw * rfbur.y_position + rfbur.x_position * bpp; 

				for(h = 0; h < rfbur.height; h++)
				{
					memcpy((void*)dest, (void*)src, bpw);
					src += fb_bpw; 
					dest+= fb_bpw;
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
