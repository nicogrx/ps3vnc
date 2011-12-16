#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <net/net.h>
#include <sys/thread.h>
#include <sys/mutex.h>

#ifdef PAD_ENABLED
#include <io/pad.h>
#endif
#ifdef MOUSE_ENABLED
#include <io/mouse.h>
#endif
#include <io/kb.h>

#include "rfb.h"
#include "vncauth.h"

#include "rsxutil.h"
#include "screen.h"

#define XK_MISCELLANY
#define XK_LATIN1
#include "keysymdef.h"
#include "mouse.h"

#include "psprint.h"
#include "tick.h"

// functions prototypes
static int handshake(char * password);
static int authenticate(char * password);
static int init(void);
static unsigned short convertKeyCode(unsigned short keycode);
static void handleInputEvents(void * arg);
#ifdef PAD_ENABLED
static void	vibratePad(void);
int padAnalogDelta(int value);
#endif
static void handleMsgs(void * arg);
static int handleRectangle(void);
static int HandleRRERectangles(const RFB_FRAMEBUFFER_UPDATE_RECTANGLE *, int, int, int);
static void pstermRefresh(void * arg);

// globals
RFB_INFO rfb_info;
unsigned char input_msg[32];
unsigned char output_msg[32];
unsigned char * raw_pixel_data = NULL;
unsigned char * old_raw_pixel_data = NULL;
int vnc_end;
unsigned char draw_mode;
volatile int frame_update_requested;

unsigned int psprint_end;

sys_mutex_t display_mutex;
sys_mutex_attr_t display_mutex_attr;

#define CONFIG_FILE "/dev_hdd0/tmp/vncconfig.txt"

int main(int argc, const char* argv[])
{
	int port, ret=0;
	u64 retval;
	sys_ppu_thread_t hie_id, hmsg_id;
	u64 priority = 1500;
	void *thread_arg = (void*)0x1337;
	size_t stack_size = 0x1000;
	char *handle_input_name = "Handle input events";
	char *handle_msg_name = "Handle message";
	char *psterm_refresh_name = "PSTerm refresh";
	sys_ppu_thread_t psterm_refresh_id;
	psfont.char_width=8;
	psfont.char_height=16;
	psterm.width=80;
	psterm.height=65;
	psterm.bg_color=0x00;
	psterm.fg_color=0xFFFFFF00;
	frame_update_requested=0;

	#define MAX_CHARS 128
	char server_ip[MAX_CHARS];
	char password[MAX_CHARS];
	FILE * pf;

	startTicks();

	ret = netInitialize();
	if (ret < 0)
		return ret;
	
	initDisplay();

#ifdef REMOTE_PRINT
	ret = remotePrintConnect("192.168.1.4");
	if (ret<0)
		goto end0;
#endif
	
	memset(&display_mutex_attr, 0, sizeof(sys_mutex_attr_t));
  display_mutex_attr.attr_protocol  = SYS_MUTEX_PROTOCOL_PRIO;
  display_mutex_attr.attr_recursive = SYS_MUTEX_ATTR_NOT_RECURSIVE;
  display_mutex_attr.attr_pshared   = SYS_MUTEX_ATTR_PSHARED;
  display_mutex_attr.attr_adaptive  = SYS_MUTEX_ATTR_NOT_ADAPTIVE;
	ret = sysMutexCreate(&display_mutex,&display_mutex_attr);
	if (ret!=0)
		goto end1;
	
	ret = PSTermInit(&psterm, &psfont);
	if (ret<0)
		goto end2;
	
	psprint_end=0;
	ret = sysThreadCreate(&psterm_refresh_id, pstermRefresh, thread_arg,
		priority, stack_size, THREAD_JOINABLE, psterm_refresh_name);

	memset(server_ip, 0, MAX_CHARS);
	memset(password, 0, MAX_CHARS);

	PSPRINT("Read configuration file: %s... ", CONFIG_FILE);
	pf=NULL;
	pf = fopen(CONFIG_FILE, "r");

	if (pf==NULL)
	{
		PSPRINT("failed!\n\n");
		PSPRINT("Please, create text file :%s\n", CONFIG_FILE);
		PSPRINT("Then, fill it with your vnc server addresss and password:\n");
		PSPRINT("xxx.xxx.xxx.xxx\nyour_password\n");
		RPRINT("failed to open configuration file\n");
		vnc_end=1;
		sleep(15);
		goto end2;
	}
	fscanf(pf, "%s\n", server_ip);
	fscanf(pf, "%s\n", password);
	fclose(pf);

	RPRINT("server ip address = %s\n", server_ip);
	RPRINT("password = %s\n", password);
	
	PSPRINT("done\n\n");
	PSPRINT("server ip address = %s\n", server_ip);
	PSPRINT("password = %s\n", password);

	PSPRINT("connecting to %s... ", server_ip);

	for(port=5900;port<5930;port++)
	{
		ret = rfbConnect(server_ip,port);
		if (ret>=0)
		{
			RPRINT("connected to serveur %s:%i\n", server_ip, port);
			break;
		}
	}
	if (ret<0)
	{
		RPRINT("failed to connect to server %s\n", server_ip);
		PSPRINT("failed!\n\n");
		vnc_end=1;
		sleep(15);	
		goto end3;
	}

	PSPRINT("done\n\n");
	
	RPRINT("handshake ...");
	PSPRINT("handshake ...");
	
	ret = handshake(password);
	if (ret<0)
	{
		PSPRINT("KO\n");
		RPRINT("KO\n");
		vnc_end=1;
		sleep(15);
		goto end4;
	}

	PSPRINT("OK\n");
	RPRINT("OK\n");
	
	RPRINT("Init ... ");
	PSPRINT("Init ... ");

	ret = init();
	if (ret<0)
	{
		PSPRINT("KO\n");
		RPRINT("KO\n");
		vnc_end=1;
		sleep(15);
		goto end4;
	}
	
	PSPRINT("OK\n");
	RPRINT("OK\n");

	psprint_end=1;
	ret = sysThreadJoin(psterm_refresh_id, &retval);
	RPRINT("join thread psterm_refresh_id\n");
	
	raw_pixel_data = (unsigned char *)malloc(
		rfb_info.server_init_msg.framebuffer_width*
		rfb_info.server_init_msg.framebuffer_height*
		(rfb_info.server_init_msg.pixel_format.bits_per_pixel/8));

	if (raw_pixel_data == NULL)
	{
		RPRINT("unable to allocate raw_pixel_data array\n");
		ret=-1;
		goto end4;
	}

	old_raw_pixel_data = (unsigned char *)malloc(
		rfb_info.server_init_msg.framebuffer_width*
		rfb_info.server_init_msg.framebuffer_height*
		(rfb_info.server_init_msg.pixel_format.bits_per_pixel/8));

	if (old_raw_pixel_data == NULL)
	{
		RPRINT("unable to allocate old_raw_pixel_data array\n");
		ret=-1;
		goto end4;
	}
	ret = sysThreadCreate(&hmsg_id, handleMsgs, thread_arg,
		priority, stack_size, THREAD_JOINABLE, handle_msg_name);
	ret = sysThreadCreate(&hie_id, handleInputEvents, thread_arg,
		priority, stack_size, THREAD_JOINABLE, handle_input_name);
	
	ret = sysThreadJoin(hie_id, &retval);
	RPRINT("join thread hie_id\n");
	
	if(raw_pixel_data!=NULL)
		free(raw_pixel_data);
	if(old_raw_pixel_data!=NULL)
		free(old_raw_pixel_data);

	if (rfb_info.server_name_string!=NULL)
		free(rfb_info.server_name_string);

end4:
	rfbClose();
end3:
	PSTermDestroy(&psterm);
end2:
	sysMutexDestroy(display_mutex);
end1:
#ifdef REMOTE_PRINT
	remotePrintClose();
end0:
#endif
	closeDisplay();
	netDeinitialize();
	return ret;
}

static int handshake(char * password)
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

static int authenticate(char * password)
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

static int init(void)
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
	
	// check bpp & depth
	// FIXME: in theory, every pixel format should be supported ...
	// moreover, should be possible to send prefered PIXEL FORMAT to server
	if (rfb_info.server_init_msg.pixel_format.bits_per_pixel==32 &&	rfb_info.server_init_msg.pixel_format.depth==24)
	{
		draw_mode=DS_MODE_32BPP;
	}
	else if (rfb_info.server_init_msg.pixel_format.bits_per_pixel==16 && rfb_info.server_init_msg.pixel_format.depth==16)
	{
		draw_mode=DS_MODE_16BPP;
	}
	else
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
		rse->number_of_encodings = 3;
		int encoding_type[3] = {RFB_RRE, RFB_CopyRect, RFB_Raw};
		rse->encoding_type = encoding_type;
		ret = rfbSendMsg(RFB_SetEncodings, rse);
	}

end:
	return ret;
}

// handle incoming msgs and render screen 
static void handleMsgs(void * arg)
{
	int i, ret;

	RFB_FRAMEBUFFER_UPDATE_REQUEST * rfbur = (RFB_FRAMEBUFFER_UPDATE_REQUEST *)output_msg;
	rfbur->incremental = 1;
	rfbur->x_position = 0;
	rfbur->y_position = 0;
	rfbur->width = rfb_info.server_init_msg.framebuffer_width;
	rfbur->height = rfb_info.server_init_msg.framebuffer_height;
	ret = rfbSendMsg(RFB_FramebufferUpdateRequest, rfbur);
	if (ret<0)
		goto end;
	frame_update_requested=1;
	RPRINT("requested initial framebuffer update\n");

	while(!vnc_end) // main loop
	{
		//handle server msgs
		ret = rfbGetMsg(input_msg);
		if (ret<=0)
		{
				//RPRINT("rfbGetMsg failed, retrying in 10 ms...\n");
				usleep(10000);
				continue;
		}
		
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
					frame_update_requested=0;
				 
					RPRINT("draw rectangle to screen\n");
					waitFlip();
					if(draw_mode == DS_MODE_16BPP)
					{
						draw16bppRectangleToScreen((unsigned short *)raw_pixel_data,
						(unsigned int)rfb_info.server_init_msg.framebuffer_width,
						(unsigned int)rfb_info.server_init_msg.framebuffer_height,
						0, 0);
					}
					else if (draw_mode == DS_MODE_32BPP)
					{
						draw32bppRectangleToScreen((unsigned int*)raw_pixel_data,
						(unsigned int)rfb_info.server_init_msg.framebuffer_width,
						(unsigned int)rfb_info.server_init_msg.framebuffer_height,
						0, 0);
					}
					RPRINT("update display\n");
					updateDisplay();
				 
					if (!frame_update_requested)
					{
						frame_update_requested=1;
						// request framebuffer update
						RFB_FRAMEBUFFER_UPDATE_REQUEST * rfbur = (RFB_FRAMEBUFFER_UPDATE_REQUEST *)output_msg;
						rfbur->incremental = 1;
						rfbur->x_position = 0;
						rfbur->y_position = 0;
						rfbur->width = rfb_info.server_init_msg.framebuffer_width;
						rfbur->height = rfb_info.server_init_msg.framebuffer_height;
						ret = rfbSendMsg(RFB_FramebufferUpdateRequest, rfbur);
						if (ret<0)
							goto end;
						RPRINT("requested framebuffer update after rendering screen\n");
					}
				
					RPRINT("START_B: memcpy raw_pixel_data\n");
					memcpy(old_raw_pixel_data, raw_pixel_data, 
						rfb_info.server_init_msg.framebuffer_width*
						rfb_info.server_init_msg.framebuffer_height*
						(rfb_info.server_init_msg.pixel_format.bits_per_pixel/8));
					RPRINT("END_B: memcpy raw_pixel_data\n");
						
				}		
				break;
			case RFB_Bell:
#ifdef PAD_ENABLED			
				vibratePad();
#endif
				break;
			case RFB_ServerCutText:
				{
					RFB_SERVER_CUT_TEXT * rsct = (RFB_SERVER_CUT_TEXT *)input_msg;
					char * text = NULL;
					text = (char*)malloc(rsct->length+1);
					if (text==NULL)
					{
						RPRINT("cannot allocate %u bytes to get cut text buffer\n", rsct->length);
						ret=-1;
						goto end;
					}
					ret = rfbGetBytes((unsigned char*)text, rsct->length);
					if (ret<0)
					{
						free(text);
						goto end;
					}
					//do something with cut text!
					free(text);
				}
				break;
			case RFB_SetColourMapEntries:
			default:
				RPRINT("cannot handle msg type:%d\n", input_msg[0]);
				ret=-1;
				goto end;
		}
	} // end main loop

end:
	vnc_end=1;
	sysThreadExit(0);
}

static unsigned short convertKeyCode(unsigned short keycode)
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
		case 0xd:
			symdef = XK_Return;
			break;
		case 0xb:
			symdef = XK_Clear;
			break;
		case 0x8029:
			symdef = XK_Escape;
			break;
		case 0x8050:
			symdef = XK_Left;
			break;
		case 0x8052:
			symdef = XK_Up;
			break;
		case 0x804f:
			symdef = XK_Right;
			break;
		case 0x8051:
			symdef = XK_Down;
			break;

		default:
			symdef=keycode;
	}
	return symdef;
}

//handle joystick events
static void handleInputEvents(void * arg)
{
	KbInfo kbinfo;
	KbData kbdata;
#define MAX_KEYPRESS 4
	unsigned short keycode[MAX_KEYPRESS];
	unsigned short old_keycode[MAX_KEYPRESS];
	int keycode_updated = 0;
	int i, j, k, ret=0;
#ifdef PAD_ENABLED
	int pad_event=0;
	padInfo padinfo;
	padData paddata;
#endif
#ifdef MOUSE_ENABLED
	int mouse_event=0;
	mouseInfo mouseinfo;
	mouseData mousedata;
#endif
	int buttons_state=0;
	int x=0;
	int y=0;
	int request_frame_update=0;

	RPRINT("handle input events\n");
	
	ioKbInit(MAX_KB_PORT_NUM);
#ifdef PAD_ENABLED	
	ioPadInit(7);
#endif
#ifdef MOUSE_ENABLED
	ioMouseInit(2);
#endif

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
#ifdef PAD_ENABLED
		// check joystick input events
		ioPadGetInfo(&padinfo);
		for(i=0; i<MAX_PADS; i++)
		{
			if(padinfo.status[i])
			{
				ioPadGetData(i, &paddata);
				if(paddata.BTN_L3)
				{
					RPRINT("exit on user request\n");
					vnc_end=1;
				}

				if (paddata.BTN_CROSS)
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

				if (paddata.BTN_R1)
				{
					pad_event = 1;
					buttons_state |= M_WHEEL_DOWN;
				}
				else if (buttons_state & M_WHEEL_DOWN)
				{
					pad_event = 1;
					buttons_state &= ~M_WHEEL_DOWN;
				}

				if (paddata.BTN_L1)
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

				//RPRINT("ANA_L_H:%u, ANA_L_V:%u\n", paddata.ANA_L_H, paddata.ANA_L_V);
				if (paddata.ANA_L_H<122)
				{
					pad_event = 1;
					int new_x;
					int dx = padAnalogDelta(122-paddata.ANA_L_H);
					new_x = x-dx;
					if (new_x > 0 && new_x < res.width)
						x=new_x;
				}
				else if(paddata.ANA_L_H>132)
				{
					pad_event = 1;
					int new_x;
					int dx = padAnalogDelta(paddata.ANA_L_H-132);
					new_x = x+dx;
					if (new_x > 0 && new_x < res.width)
						x=new_x;
				}
				if (paddata.ANA_L_V<122)
				{
					pad_event = 1;
					int new_y;
					int dy = padAnalogDelta(122-paddata.ANA_L_V);
					new_y = y-dy;
					if (new_y > 0 && new_y < res.height)
						y=new_y;
				}
				else if(paddata.ANA_L_V>132)
				{
					pad_event = 1;
					int new_y;
					int dy = padAnalogDelta(paddata.ANA_L_V-132);
					new_y = y+dy;
					if (new_y > 0 && new_y < res.height)
						y=new_y;
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
#endif
#ifdef MOUSE_ENABLED
		// check joystick input events
		ioMouseGetInfo(&mouseinfo);
		for(i=0; i<MAX_MICE; i++)
		{
			if(mouseinfo.status[0])
			{
				ioMouseGetData(i, &mousedata);
				if (!mousedata.update)
				{
					break;	
				}
				
				/*RPRINT("MouseDATA buttons:%u, x_axis:%i, y_axis:%i, wheel:%i, tilt:%i\n",
					mousedata.buttons,
					mousedata.x_axis,
					mousedata.y_axis,
					mousedata.wheel,
					mousedata.tilt );
				*/
				
				if (mousedata.buttons==4)
				{
					RPRINT("exit on user request\n");
					vnc_end=1;
				}

				if (mousedata.buttons==1)
				{
					mouse_event = 1;
					buttons_state |= M_LEFT;
				}
				else if (buttons_state & M_LEFT)
				{
					mouse_event = 1;
					buttons_state &= ~M_LEFT;
				}

				if (mousedata.buttons==2)
				{
					mouse_event = 1;
					buttons_state |= M_RIGHT;
				}
				else if (buttons_state & M_RIGHT)
				{
					mouse_event = 1;
					buttons_state &= ~M_RIGHT;
				}

				if (mousedata.wheel==-1)
				{
					mouse_event = 1;
					buttons_state |= M_WHEEL_DOWN;
				}
				else if (buttons_state & M_WHEEL_DOWN)
				{
					mouse_event = 1;
					buttons_state &= ~M_WHEEL_DOWN;
				}

				if (mousedata.wheel==1)
				{
					mouse_event = 1;
					buttons_state |= M_WHEEL_UP;
				}
				else if (buttons_state & M_WHEEL_UP)
				{
					mouse_event = 1;
					buttons_state &= ~M_WHEEL_UP;
				}

				if (mousedata.x_axis!=0)
				{
					mouse_event = 1;
					int new_x;
					new_x = x+mousedata.x_axis;
					if (new_x > 0 && new_x < res.width)
						x+=mousedata.x_axis;
				}
			
				if (mousedata.y_axis!=0)
				{
					mouse_event = 1;
					int new_y;
					new_y = y+mousedata.y_axis;
					if (new_y > 0 && new_y < res.height)
						y+=mousedata.y_axis;
				}
			
				if (mouse_event)
				{
					mouse_event = 0;
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
#endif

		if (vnc_end || (request_frame_update && !frame_update_requested))
		{
			frame_update_requested=1;
			request_frame_update=0;
			// request framebuffer update
			RFB_FRAMEBUFFER_UPDATE_REQUEST * rfbur = (RFB_FRAMEBUFFER_UPDATE_REQUEST *)output_msg;
			rfbur->incremental = 1;
			rfbur->x_position = 0;
			rfbur->y_position = 0;
			rfbur->width = rfb_info.server_init_msg.framebuffer_width;
			rfbur->height = rfb_info.server_init_msg.framebuffer_height;
			ret = rfbSendMsg(RFB_FramebufferUpdateRequest, rfbur);
			if (ret<0)
				goto end;
			RPRINT("requested framebuffer update due to input event\n");
		}
		usleep(2000);
	}
end:
	ioKbEnd();
#ifdef PAD_ENABLED
	ioPadEnd();
#endif
#ifdef MOUSE_ENABLED
	ioMouseEnd();
#endif
	sysThreadExit(0);
}


int padAnalogDelta(int value){
	int ret=0;

	if (value<5)
		ret=0;
	else if (value<30)
		ret=1;
	else if (value<60)
		ret=2;
	else
		ret=3;
	return ret;
}

#ifdef PAD_ENABLED
static void	vibratePad(void)
{
	padActParam actparam;
	actparam.small_motor = 1;
	actparam.large_motor = 0;
	ioPadSetActDirect(0, &actparam);
	usleep(500000);
	actparam.small_motor = 0;
	ioPadSetActDirect(0, &actparam);
}
#endif

static int handleRectangle(void)
{
	int ret=0;
	int bpp, bpw, rfb_bpw;
	RFB_FRAMEBUFFER_UPDATE_RECTANGLE rfbur;
	
	RPRINT("START_F:handleRectangle\n");

	ret = rfbGetRectangleInfo(&rfbur);
	if (ret<0)
		goto end;
	
	RPRINT("update rectangle: encoding_type:%d, width:%d, height:%d, x_position:%d, y_position:%d\n",
		rfbur.encoding_type,
		rfbur.width,
		rfbur.height,
		rfbur.x_position,
		rfbur.y_position);

	bpp=rfb_info.server_init_msg.pixel_format.bits_per_pixel/8;
	bpw=rfbur.width*bpp;
	rfb_bpw = rfb_info.server_init_msg.framebuffer_width*bpp;

	switch (rfbur.encoding_type)
	{
		case RFB_Raw:
			{
				RPRINT("START_B:RFB_Raw\n");
				unsigned char * dest;
				int h;
				dest = raw_pixel_data + rfbur.y_position*rfb_bpw + rfbur.x_position*bpp;
				for(h = 0; h < rfbur.height; h++)
				{
					ret = rfbGetBytes(dest, bpw);
					if (ret<0)
					{
						RPRINT("failed to get line of %d pixels\n", bpw);
						goto end;
					}
					dest+=rfb_bpw;
				}
				RPRINT("END_B:RFB_Raw\n");
			}
			break;

		case RFB_CopyRect:
			{
				RPRINT("START_B:RFB_CopyRect\n");
				RFB_COPYRECT_INFO rci;
				unsigned char * src;
				unsigned char * dest;
				int h;
				ret = rfbGetBytes((unsigned char *)&rci, sizeof(RFB_COPYRECT_INFO));
				if (ret<0)
				{
					RPRINT("failed to get RFB_COPYRECT_INFO\n");
					goto end;
				}
				
				src = old_raw_pixel_data + rfb_bpw * rci.src_y_position + rci.src_x_position * bpp;
				dest = raw_pixel_data + rfb_bpw * rfbur.y_position + rfbur.x_position * bpp; 

				for(h = 0; h < rfbur.height; h++)
				{
					memcpy((void*)dest, (void*)src, bpw);
					src += rfb_bpw; 
					dest+= rfb_bpw;
				}
				RPRINT("END_B:RFB_CopyRect\n");
			}
			break;
		case RFB_RRE:
			{
				ret=HandleRRERectangles(&rfbur, bpp, bpw, rfb_bpw);
			}
			break;
		default:
			RPRINT("unsupported encoding type:%d\n", rfbur.encoding_type);
			ret = -1;
			goto end;
	}

end:
	RPRINT("END_F:handleRectangle\n");
	return ret;
}

static int HandleRRERectangles(const RFB_FRAMEBUFFER_UPDATE_RECTANGLE * rfbur,
	int bpp, int bpw, int rfb_bpw)
{
	int ret=0;
	unsigned char header[8];
	unsigned char subrect_info[12];
	unsigned int nb_sub_rectangles;
	int sr, w, h;
	unsigned char * dest;
	RFB_RRE_SUBRECT_INFO * rrsi;

	unsigned short * tmp_pshort;
	unsigned int * tmp_pint;
	
	RPRINT("START_F:HandleRRERectangles\n");

	ret = rfbGetBytes(header, 4 + bpp);
	if (ret<0)
	{
		RPRINT("failed to get header\n");
		goto end;
	}
	
	tmp_pint = (unsigned int *)header;
	nb_sub_rectangles = *tmp_pint;

	dest = raw_pixel_data + rfbur->y_position*rfb_bpw + rfbur->x_position*bpp;

	RPRINT("%u sub-rectangles to draw\n", nb_sub_rectangles);

	switch (bpp)
	{
		case 2: // 16 bits per pixel
			{
				unsigned short bg_pixel_value;
				unsigned short subrect_pixel_value;
				unsigned short * start;
			
				tmp_pshort = (unsigned short *)(header+4);
				bg_pixel_value = *tmp_pshort;

				//first, draw background color
				if ( (((unsigned long)dest & 0xF)==0) && (rfbur->width>=8) ) // address must be 16bytes aligned and pixel width >= 8
				{
					// use altivec
					vector unsigned short v_bg_pixel_value = (vector unsigned short){bg_pixel_value,
																																					 bg_pixel_value,
																																					 bg_pixel_value,
																																					 bg_pixel_value,
																																					 bg_pixel_value,
																																					 bg_pixel_value,
																																					 bg_pixel_value,
																																					 bg_pixel_value};
					vector unsigned short * v_dest = (vector unsigned short*)dest;
					int v_width = rfbur->width/8;
					int vrest_width = rfbur->width%8;
					int vres_width = rfb_info.server_init_msg.framebuffer_width/8;

					RPRINT("use altivec to draw background color [%x]\n", bg_pixel_value);
					for (h=0;h<rfbur->height;h++)
					{
						for(w=0;w<v_width;w++)
						{
							v_dest[w]=v_bg_pixel_value;
						}
						v_dest+=vres_width;
					}

					if (vrest_width)
					{
						start = (unsigned short*)(dest+v_width*16);
						for(h=0;h<rfbur->height;h++)
						{
							for(w = 0; w < vrest_width; w++)
							{
								start[w]=bg_pixel_value;
							}
							start+=rfb_info.server_init_msg.framebuffer_width;
						}
					}
					
				}
				else // use scalar
				{
					start = (unsigned short *)dest;
					for (h=0;h<rfbur->height;h++)
					{
						for(w=0;w<rfbur->width;w++)
						{
							start[w]=bg_pixel_value;
						}	
						start+=rfb_info.server_init_msg.framebuffer_width;
					}
				}

				RPRINT("draw sub rectangles\n");

				// then, draw sub rectangles
				for (sr=0;sr<nb_sub_rectangles;sr++)
				{
					ret = rfbGetBytes(subrect_info, 8 + bpp);
					if (ret<0)
					{
						RPRINT("failed to get sub rect info\n");
						goto end;
					}

					tmp_pshort = (unsigned short *)subrect_info;
					subrect_pixel_value = *tmp_pshort;
					rrsi = (RFB_RRE_SUBRECT_INFO *)(subrect_info+bpp);

					start=(unsigned short*)(dest + rrsi->y_position*rfb_bpw + rrsi->x_position*bpp);

					for (h=0;h<rrsi->height;h++)	// FIXME: use Altivec here !
					{
						for(w=0;w<rrsi->width;w++)
						{
							start[w]=subrect_pixel_value;
						}	
						start+=rfb_info.server_init_msg.framebuffer_width;
					}
				}
			}
			break;
		case 4: // 32 bits per pixel
			{
				unsigned int bg_pixel_value;
				unsigned int subrect_pixel_value;
				unsigned int * start;
				
				tmp_pint = (unsigned int *)(header+4);
				bg_pixel_value = *tmp_pint;
				start = (unsigned int *)dest;

				//first, draw background color
				for (h=0;h<rfbur->height;h++)	// FIXME: use Altivec here !
				{
					for(w=0;w<rfbur->width;w++)
					{
						start[w]=bg_pixel_value;
					}	
					start+=rfb_info.server_init_msg.framebuffer_width;
				}

				for (sr=0;sr<nb_sub_rectangles;sr++)
				{
					ret = rfbGetBytes(subrect_info, 8 + bpp);
					if (ret<0)
					{
						RPRINT("failed to get sub rect info\n");
						goto end;
					}

					tmp_pint = (unsigned int *)subrect_info;
					subrect_pixel_value = *tmp_pint;
					rrsi = (RFB_RRE_SUBRECT_INFO *)(subrect_info+bpp);

					start=(unsigned int*)(dest + rrsi->y_position*rfb_bpw + rrsi->x_position*bpp);

					for (h=0;h<rrsi->height;h++)	// FIXME: use Altivec here !
					{
						for(w=0;w<rrsi->width;w++)
						{
							start[w]=subrect_pixel_value;
						}	
						start+=rfb_info.server_init_msg.framebuffer_width;
					}
				}
			}
			break;

		default:
			RPRINT("invalid bpp\n");
			goto end;
	}

end:
	RPRINT("END_F:HandleRRERectangles\n");
	return ret;
}

static void pstermRefresh(void * arg)
{
	while(!vnc_end && !psprint_end)
	{
		usleep(100000);

		if(sysMutexLock(display_mutex, 0)==0)
		{
			PSTermDraw (&psterm);
			waitFlip();
			draw32bppRectangleToScreen(
						(unsigned int*)psterm.pixel_buf,
						(unsigned int)psterm.pixel_width,
						(unsigned int)psterm.pixel_height,
						0, 0);
			updateDisplay();
			
			sysMutexUnlock(display_mutex);
		}
	}
	sysThreadExit(0);
}
