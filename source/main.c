#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <net/net.h>
#include <io/pad.h>
#include <SDL/SDL.h>
#include <SDL/SDL_thread.h>

#include "rfb.h"
#include "vncauth.h"
#include "screen.h"
#include "keymap.h"
#include "tick.h"
#include "remoteprint.h"
#include "localprint.h"
#include "osk_input.h"
#include "dialog.h"

char pad_text[80];

#define MAX_CHARS 128
struct vnc_client {
	RFB_INFO rfb_info;
	unsigned char input_msg[32];
	unsigned char output_msg[32];
	int vnc_end;
	char server_ip[MAX_CHARS];
	char password[MAX_CHARS];
	SDL_Thread *msg_thread;
	SDL_Thread *req_thread;
	SDL_mutex *lock;
	int frame_update_requested;
	SDL_Surface *framebuffer;
	SDL_Rect updated_rect;
	PIXEL_FORMAT pixel_format;
	unsigned int bits_pp;
	unsigned int bytes_pp;
	unsigned int rmask;
	unsigned int gmask;
	unsigned int bmask;
	unsigned int amask;
};

// functions prototypes
static int handshake(struct vnc_client *vncclient);
static int authenticate(char *password);
static int init(struct vnc_client *vncclient);
static int requestUpdate(void * data);
static int handleMsgs(void * data);
static int handleRectangle(struct vnc_client *vncclient);
static int handleRRERectangles(struct vnc_client * vncclient, SDL_Rect *rect);
static int handleHextileRectangles(struct vnc_client * vncclient, SDL_Rect *rect);
static int key_event(struct vnc_client *vncclient,
	unsigned char downflag, unsigned int key);
static int pointer_event(struct vnc_client *vncclient,
	unsigned char buttonmask, unsigned short x, unsigned short y);
static void reset_updated_region(struct vnc_client *);

#define CONFIG_FILE "/dev_hdd0/tmp/vncconfig.txt"
#define REMOTE_PRINT_SRV "192.168.1.2"
/*
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
*/
enum MouseButtons
{
	M_LEFT = 1 << 0,
	M_MIDDLE = 1 << 1,
	M_RIGHT = 1 << 2,
	M_WHEEL_UP = 1 << 3,
	M_WHEEL_DOWN = 1 << 4
};

static int request_framebuffer_update(struct vnc_client *vncclient)
{
	int ret = 0;
	if (vncclient->frame_update_requested)
		return ret;
	SDL_LockMutex(vncclient->lock);
	RFB_FRAMEBUFFER_UPDATE_REQUEST * rfbur =
		(RFB_FRAMEBUFFER_UPDATE_REQUEST *)vncclient->output_msg;
	rfbur->incremental = 1;
	rfbur->x_position = 0;
	rfbur->y_position = 0;
	rfbur->width = vncclient->rfb_info.server_init_msg.framebuffer_width;
	rfbur->height = vncclient->rfb_info.server_init_msg.framebuffer_height;
	ret = rfbSendMsg(RFB_FramebufferUpdateRequest, rfbur);
	vncclient->frame_update_requested = 1;
	SDL_UnlockMutex(vncclient->lock);
	if (ret<0)
		remotePrint("failed to request framebuffer update\n");
	else
		remotePrint("requested framebuffer update\n");
	return 0;
}

static int get_button_mask(SDL_Event *event)
{
	switch(event->button.button) {
		case SDL_BUTTON_LEFT:
			return M_LEFT;
		case SDL_BUTTON_MIDDLE:
			return M_MIDDLE;
		case SDL_BUTTON_RIGHT:
			return M_RIGHT;
		case SDL_BUTTON_WHEELUP:
			return M_WHEEL_UP;
		case SDL_BUTTON_WHEELDOWN:
			return M_WHEEL_DOWN;
	}
	return 0;
}
static int key_event(struct vnc_client *vncclient,
	unsigned char downflag, unsigned int key)
{
	int ret;
	SDL_LockMutex(vncclient->lock);
	RFB_KEY_EVENT * rke = (RFB_KEY_EVENT *)vncclient->output_msg;
	rke->down_flag = downflag;
	rke->key = map_key(key, downflag);
	remotePrint("key event: downflag=%u, code=%x, code sent=%x\n",
		downflag, key, rke->key);
	if (rke->key >= 0)
		ret = rfbSendMsg(RFB_KeyEvent, rke);
	SDL_UnlockMutex(vncclient->lock);
	return ret;
}

static int pointer_event(struct vnc_client *vncclient,
	unsigned char buttonmask, unsigned short x, unsigned short y)
{
	int ret;
	SDL_LockMutex(vncclient->lock);
	RFB_POINTER_EVENT * rpe = (RFB_POINTER_EVENT *)vncclient->output_msg;
	rpe->button_mask = buttonmask;
	rpe->x_position = x;
	rpe->y_position = y;
	ret = rfbSendMsg(RFB_PointerEvent, rpe);
	SDL_UnlockMutex(vncclient->lock);
	return ret;
}
static int get_input_event(struct vnc_client *vncclient) {
	SDL_Event event;
	padInfo padinfo;
	padData paddata;
	int ret = 0;
	int i;
	int mouse_event = 0;
	int pad_event = 0;
	static unsigned short x = 0;
	static unsigned short y = 0;
	static unsigned char mouse_buttonmask = 0;
	static unsigned char pad_buttonmask = 0;

	while(SDL_PollEvent(&event)) {
		switch(event.type) {
		case SDL_QUIT:
			return 1;
		case SDL_KEYDOWN:
			key_event(vncclient, 1, event.key.keysym.sym);
			mouse_event = 1;
			break;
		case SDL_KEYUP:
			key_event(vncclient, 0, event.key.keysym.sym);
			mouse_event = 1;
			break;
		case SDL_MOUSEMOTION:
			x = event.motion.x;
			y = event.motion.y;
			pointer_event(vncclient, mouse_buttonmask, x, y);
			mouse_event = 1;
			break;
		case SDL_MOUSEBUTTONDOWN:
			mouse_buttonmask |= get_button_mask(&event);
			pointer_event(vncclient, mouse_buttonmask, x, y);
			mouse_event = 1;
			break;
		case SDL_MOUSEBUTTONUP:
			mouse_buttonmask &= ~get_button_mask(&event);
			pointer_event(vncclient, mouse_buttonmask, x, y);
			mouse_event = 1;
			break;
		}
	}
	ioPadGetInfo(&padinfo);
	for(i=0; i<MAX_PADS; i++)
	{
		if(padinfo.status[i])
		{
			ioPadGetData(i, &paddata);
			if (paddata.BTN_SQUARE)
				return 1;

			if (paddata.BTN_CROSS)
			{
				pad_event = 1;
				pad_buttonmask |= M_LEFT;
			}
			else if (pad_buttonmask & M_LEFT)
			{
				pad_event = 1;
				pad_buttonmask &= ~M_LEFT;
			}
			if (paddata.BTN_CIRCLE)
			{
				pad_event = 1;
				pad_buttonmask |= M_RIGHT;
			}
			else if (pad_buttonmask & M_RIGHT)
			{
				pad_event = 1;
				pad_buttonmask &= ~M_RIGHT;
			}
			if (paddata.BTN_R1)
			{
				pad_event = 1;
				pad_buttonmask |= M_WHEEL_DOWN;
			}
			else if (pad_buttonmask & M_WHEEL_DOWN)
			{
				pad_event = 1;
				pad_buttonmask &= ~M_WHEEL_DOWN;
			}

			if (paddata.BTN_L1)
			{
				pad_event = 1;
				pad_buttonmask |= M_WHEEL_UP;
			}
			else if (pad_buttonmask & M_WHEEL_UP)
			{
				pad_event = 1;
				pad_buttonmask &= ~M_WHEEL_UP;
			}

			if (paddata.BTN_LEFT && (x - 5 > 0))
			{
				pad_event = 1;
				x-=5;
			}
		
			if (paddata.BTN_RIGHT && (x + 5 < res.width))
			{
				pad_event = 1;
				x+=5;
			}
		
			if (paddata.BTN_UP && (y - 5 > 0))
			{
				pad_event = 1;
				y-=5;
			}
		
			if (paddata.BTN_DOWN && (y + 5 < res.height))
			{
				pad_event = 1;
				y+=5;
			}

			if (paddata.ANA_L_H < 119 && (x - (127 - paddata.ANA_L_H) > 0))
			{
				pad_event = 1;
				x -= 127 - paddata.ANA_L_H;
			}

			if ( paddata.ANA_L_H > 135 && (x + (paddata.ANA_L_H - 127) < res.width))
			{
				pad_event = 1;
				x += paddata.ANA_L_H - 127;

			}

			if (paddata.ANA_L_V < 119 && (y - (127 - paddata.ANA_L_V) > 0))
			{
				pad_event = 1;
				y -= 127 - paddata.ANA_L_V;
			}

			if (paddata.ANA_L_V > 135 && (y + (paddata.ANA_L_V - 127) < res.height))
			{
				pad_event = 1;
				y += paddata.ANA_L_V - 127;
			}

			if (pad_event) {
				pointer_event(vncclient, pad_buttonmask, x, y);
			}
		}
		break;
	}
	if (mouse_event || pad_event)
		ret = request_framebuffer_update(vncclient);
	return ret;
}
static void displayUsage(void)
{
	SDL_Rect tmp_rect;
	tmp_rect.x = 0;
	tmp_rect.y = 0;
	tmp_rect.w = res.width;
	tmp_rect.h = res.height;
	clearDisplay(&tmp_rect);
	ok_dialog("PS3 Vnc viewer v1.0 by nicogrx@gmail.com.\nTHIS SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND.\n\nWhen connected to server, use SQUARE pad button to quit.\nPlug a USB mouse & keyboard for a better experience.", 1);
	tmp_rect.x = 0;
	tmp_rect.y = 0;
	tmp_rect.w = res.width;
	tmp_rect.h = res.height;
	clearDisplay(&tmp_rect);
}

static int getConnectionInfos(const char * config_file, struct vnc_client *vncclient)
{
	int ret = 0;
	char text[MAX_CHARS*2];
	SDL_Rect tmp_rect;

	if (!vncclient)
		return -1;

	memset(vncclient->server_ip, 0, MAX_CHARS);
	memset(vncclient->password, 0, MAX_CHARS);

	FILE * pf;
	pf=NULL;
	pf = fopen(config_file, "r");

	if (pf==NULL)
		remotePrint("failed to read %s\n", config_file);
	else {
		fscanf(pf, "%s\n", vncclient->server_ip);
		fscanf(pf, "%s\n", vncclient->password);
		fclose(pf);
		memset(text, 0, MAX_CHARS*2);
		sprintf(text, "server ip address: %s\npassword: %s",
			vncclient->server_ip, vncclient->password);
		if(yes_dialog(text)) {
			displayUsage();
			return 0;
		}
	}

	tmp_rect.x = 100;
	tmp_rect.y = 100;
	tmp_rect.w = 400;
	tmp_rect.h = 60;

	ret =  blitText("please, enter server ip address:", &tmp_rect);
	if (ret < 0)
		return ret;

	ret = Get_OSK_String(vncclient->server_ip, MAX_CHARS-1);
	if (ret < 0)
		return ret;

	remotePrint("getConnectionInfos: server ip address: %s\n", vncclient->server_ip);
	
	ret =  blitText("please, enter vnc password:", &tmp_rect);
	if (ret < 0)
		return ret;
	
	ret = Get_OSK_String(vncclient->password, MAX_CHARS-1);
	if (ret < 0)
		return ret;

	remotePrint("getConnectionInfos: password: %s\n", vncclient->password);

	pf = fopen(config_file, "w");
	if (pf==NULL)
		remotePrint("failed to write %s\n", config_file);
	else {
		fprintf(pf, "%s\n", vncclient->server_ip);
		fprintf(pf, "%s\n", vncclient->password);
		fclose(pf);
	}
	displayUsage();
	return ret;
}

int main(int argc, const char* argv[])
{
	int port, ret=0;
	struct vnc_client vncclient;
	vncclient.vnc_end=0;

	ioPadInit(7);
	startTicks();

	ret = netInitialize();
	if (ret < 0)
		return ret;

	
	localPrintInit();

	ret = remotePrintConnect(REMOTE_PRINT_SRV);
	if (ret<0)
		goto lprint_close;

	ret = initDisplay(1920, 1080);
	if (ret)
		goto display_close;
	
	ret = getConnectionInfos(CONFIG_FILE, &vncclient);
	if (ret < 0)
		goto rprint_close;

	reset_updated_region(&vncclient);
	
	vncclient.lock=SDL_CreateMutex();
	if(!vncclient.lock)
		goto display_close;
	 
	for(port=5900;port<5930;port++)
	{
		ret = rfbConnect(vncclient.server_ip, port);
		if (ret>=0)
		{
			remotePrint("connected to serveur %s:%i\n", vncclient.server_ip, port);
			break;
		}
	}
	if (ret<0)
	{
		remotePrint("failed to connect to server %s\n", vncclient.server_ip);
		vncclient.vnc_end=1;
		sleep(2);	
		goto mutex_destroy;
	}

	remotePrint("handshake\n");
	
	ret = handshake(&vncclient);
	if (ret<0)
	{
		vncclient.vnc_end=1;
		sleep(2);
		goto rfb_close;
	}

	remotePrint("init\n");

	ret = init(&vncclient);
	if (ret<0)
	{
		vncclient.vnc_end=1;
		sleep(2);
		goto rfb_close;
	}

	vncclient.frame_update_requested = 0;
	vncclient.msg_thread = SDL_CreateThread(handleMsgs, (void*)&vncclient); 
	vncclient.req_thread = SDL_CreateThread(requestUpdate, (void*)&vncclient); 
	while(!vncclient.vnc_end) {
			vncclient.vnc_end = get_input_event(&vncclient);
			usleep(50000);
    }

	if (vncclient.req_thread) {
		SDL_KillThread(vncclient.req_thread);
	}
	if (vncclient.msg_thread) {
		SDL_KillThread(vncclient.msg_thread);
	}

	if(vncclient.framebuffer!=NULL)
		SDL_FreeSurface(vncclient.framebuffer);
	if (vncclient.rfb_info.server_name_string!=NULL)
		free(vncclient.rfb_info.server_name_string);
rfb_close:
	rfbClose();
mutex_destroy:
	SDL_DestroyMutex(vncclient.lock);
display_close:
	closeDisplay();
rprint_close:
	remotePrintClose();
lprint_close:
	localPrintClose();
	netDeinitialize();
	ioPadEnd();
	return ret;
}

static int handshake(struct vnc_client *vncclient)
{
	int ret;
	char *reason=NULL;

	ret = rfbGetProtocolVersion();
	if (ret<0)
	{
		remotePrint("failed to get protocol version\n");
		goto end;
	}
	vncclient->rfb_info.version = ret;
	ret = rfbSendProtocolVersion(RFB_003_003);
	if (ret<0)
	{
		remotePrint("failed to send protocol version\n");
		goto end;
	}
	ret = rfbGetSecurityType();
	if (ret<0)
	{
		remotePrint("failed to get security type\n");
		goto end;
	}
	vncclient->rfb_info.security_type=ret;
	remotePrint("security type:%i\n", vncclient->rfb_info.security_type);
	switch(vncclient->rfb_info.security_type)
	{
		case RFB_SEC_TYPE_VNC_AUTH:
			// authentication is needed
			ret = authenticate(vncclient->password);
			if (ret!=RFB_SEC_RESULT_OK)
			{
				remotePrint("failed to authenticate, security result:%i\n", ret);
				ret = -1;
			}
			break;
		case RFB_SEC_TYPE_NONE:
			//switch to init phase
			ret=0;
			remotePrint("no authentication needed\n");
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
					remotePrint("failed to allocate %d bytes\n", l);
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
				remotePrint("%s\n",reason);
				free(reason);
			}
			ret = -1;
			break;
		default:
			remotePrint("security type is not supported\n");
	}

end:
	return ret;
}

static int authenticate(char *password)
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

static int init(struct vnc_client * vncclient)
{
	int ret;
	ret = rfbSendClientInit(RFB_NOT_SHARED);
	if (ret<0)
	{
		remotePrint("failed to send client init msg\n");
		goto end;
	}
	ret =rfbGetServerInitMsg(&(vncclient->rfb_info.server_init_msg));
	if (ret<0)
	{
		remotePrint("failed to get server init msg\n");
		goto end;
	}

	if ( vncclient->rfb_info.server_init_msg.name_length!=0)
	{
		int l = vncclient->rfb_info.server_init_msg.name_length;
		vncclient->rfb_info.server_name_string = NULL;
		vncclient->rfb_info.server_name_string = (char*)malloc(l+1);
		if (vncclient->rfb_info.server_name_string == NULL)
		{
			remotePrint("failed to allocate %d bytes\n", l+1);
			ret = -1;
			goto end;
		}
		ret = rfbGetBytes((unsigned char*)(vncclient->rfb_info.server_name_string), l);
		if (ret<0)
		{
			free(vncclient->rfb_info.server_name_string);
			goto end;
		}
		vncclient->rfb_info.server_name_string[l]='\0';
		remotePrint("server name:%s\n",vncclient->rfb_info.server_name_string);
	}

	// check width & height
	if (vncclient->rfb_info.server_init_msg.framebuffer_width>res.width ||
			vncclient->rfb_info.server_init_msg.framebuffer_height>res.height)
	{
		remotePrint("cannot handle server frame size: with=%i, height=%i\n",
			vncclient->rfb_info.server_init_msg.framebuffer_width,
			vncclient->rfb_info.server_init_msg.framebuffer_height);
		ret=-1;
		goto end;
	}

	remotePrint("server: framebuffer_width:%i framebuffer_height:%i\n",
		vncclient->rfb_info.server_init_msg.framebuffer_width,
		vncclient->rfb_info.server_init_msg.framebuffer_height);
	remotePrint("server: bits_per_pixel:%i depth:%i big_endian_flag:%i true_colour_flag:%i\n",
		vncclient->rfb_info.server_init_msg.pixel_format.bits_per_pixel,
		vncclient->rfb_info.server_init_msg.pixel_format.depth,
		vncclient->rfb_info.server_init_msg.pixel_format.big_endian_flag,
		vncclient->rfb_info.server_init_msg.pixel_format.true_colour_flag);
	remotePrint("server: red_max:%i green_max:%i blue_max:%i\n",
		vncclient->rfb_info.server_init_msg.pixel_format.red_max,
		vncclient->rfb_info.server_init_msg.pixel_format.green_max,
		vncclient->rfb_info.server_init_msg.pixel_format.blue_max);
	remotePrint("server: red_shift:%i green_shift:%i blue_shift:%i\n",
		vncclient->rfb_info.server_init_msg.pixel_format.red_shift,
		vncclient->rfb_info.server_init_msg.pixel_format.green_shift,
		vncclient->rfb_info.server_init_msg.pixel_format.blue_shift);

#if 1
	vncclient->pixel_format.bits_per_pixel = 32;
	vncclient->pixel_format.depth = 24;
	vncclient->pixel_format.big_endian_flag = 0;
	vncclient->pixel_format.true_colour_flag = 1;
	vncclient->pixel_format.red_max = 0xff;
	vncclient->pixel_format.green_max = 0xff;
	vncclient->pixel_format.blue_max = 0xff;
	vncclient->pixel_format.red_shift = 0;
	vncclient->pixel_format.green_shift = 8;
	vncclient->pixel_format.blue_shift = 16;

  vncclient->rmask =
		vncclient->pixel_format.red_max << 24;
	vncclient->gmask =
		vncclient->pixel_format.green_max << 16;
	vncclient->bmask =
		vncclient->pixel_format.blue_max << 8;
	vncclient->amask = 0x000000ff;

#else
	vncclient->pixel_format.bits_per_pixel = 16;
	vncclient->pixel_format.depth = 16;
	vncclient->pixel_format.big_endian_flag = 1;
	vncclient->pixel_format.true_colour_flag = 1;
	vncclient->pixel_format.red_max = 0x1f;
	vncclient->pixel_format.green_max = 0x3f;
	vncclient->pixel_format.blue_max = 0x1f;
	vncclient->pixel_format.red_shift = 11;
	vncclient->pixel_format.green_shift = 5;
	vncclient->pixel_format.blue_shift = 0;

	vncclient->rmask =
		vncclient->pixel_format.red_max << 11;
	vncclient->gmask =
		vncclient->pixel_format.green_max << 5;
	vncclient->bmask =
		vncclient->pixel_format.blue_max << 0;
	vncclient->amask = 0x0;

#endif

	vncclient->bits_pp = vncclient->pixel_format.bits_per_pixel;
	vncclient->bytes_pp = vncclient->pixel_format.bits_per_pixel / 8;

	{
		RFB_SET_PIXEL_FORMAT *rspf = (RFB_SET_PIXEL_FORMAT *)vncclient->output_msg;
		rspf->pixel_format.bits_per_pixel = vncclient->pixel_format.bits_per_pixel;
		rspf->pixel_format.depth = vncclient->pixel_format.depth;
		rspf->pixel_format.big_endian_flag = vncclient->pixel_format.big_endian_flag;
		rspf->pixel_format.true_colour_flag = vncclient->pixel_format.true_colour_flag;
		rspf->pixel_format.red_max = vncclient->pixel_format.red_max;
		rspf->pixel_format.green_max = vncclient->pixel_format.green_max;
		rspf->pixel_format.blue_max = vncclient->pixel_format.blue_max;
		rspf->pixel_format.red_shift = vncclient->pixel_format.red_shift;
		rspf->pixel_format.green_shift = vncclient->pixel_format.green_shift;
		rspf->pixel_format.blue_shift = vncclient->pixel_format.blue_shift;
		ret = rfbSendMsg(RFB_SetPIxelFormat, rspf);
		if (ret < 0) {
			ret = -1;
			goto end;
		}		
	}

	vncclient->framebuffer = SDL_CreateRGBSurface(SDL_SWSURFACE,
		vncclient->rfb_info.server_init_msg.framebuffer_width,
		vncclient->rfb_info.server_init_msg.framebuffer_height,
		vncclient->pixel_format.bits_per_pixel,
		vncclient->rmask, vncclient->gmask, vncclient->bmask, vncclient->amask);
	if (vncclient->framebuffer==NULL) {
		remotePrint("could not create framebuffer:%s.\n", SDL_GetError());
		ret = -1;
		goto end;
	} else {
		remotePrint("Framebuffer created\n");
	}
	SDL_SetAlpha(vncclient->framebuffer,0,0);
	
	// now send supported encodings formats
	{
		RFB_SET_ENCODINGS * rse = (RFB_SET_ENCODINGS *)vncclient->output_msg;
#if 1
		rse->number_of_encodings = 4;
		int encoding_type[4] = { RFB_RRE, RFB_CopyRect, RFB_Hextile, RFB_Raw };
#else
		rse->number_of_encodings = 4;
		int encoding_type[4] = { RFB_Hextile, RFB_RRE, RFB_CopyRect, RFB_Raw };
#endif
		rse->encoding_type = encoding_type;
		ret = rfbSendMsg(RFB_SetEncodings, rse);
	}

end:
	return ret;
}

static int requestUpdate(void * data)
{
	struct vnc_client *vncclient=(struct vnc_client *)data;
	while(!vncclient->vnc_end) // main loop
	{
		request_framebuffer_update(vncclient);
		usleep(20000);
	}
	return 0;
}

// handle incoming msgs and render screen 
static int handleMsgs(void * data)
{
	int i, ret;
	struct vnc_client *vncclient=(struct vnc_client *)data;

	while(!vncclient->vnc_end) // main loop
	{
		//handle server msgs
		ret = rfbGetMsg(vncclient->input_msg);
		if (ret<=0)
		{
				//remotePrint("rfbGetMsg failed, retrying in 10 ms...\n");
				continue;
		}
		switch (vncclient->input_msg[0])
		{
			case RFB_FramebufferUpdate:
				{
					SDL_LockMutex(vncclient->lock);
					RFB_FRAMEBUFFER_UPDATE *rfbu = (RFB_FRAMEBUFFER_UPDATE *)vncclient->input_msg;
					remotePrint("%i rectangles to update\n", rfbu->number_of_rectangles);

					for (i=0;i<rfbu->number_of_rectangles;i++)
					{
						ret = handleRectangle(vncclient);
						if (ret<0)
							goto end;
					}
				 
					remotePrint("draw updated rectangle to screen\n");
					fillDisplay(vncclient->framebuffer, &vncclient->updated_rect);
					updateDisplay();

					reset_updated_region(vncclient);
					vncclient->frame_update_requested = 0;
					SDL_UnlockMutex(vncclient->lock);
				}		
				break;
			case RFB_Bell:
				//vibratePad();
				break;
			case RFB_ServerCutText:
				{
					RFB_SERVER_CUT_TEXT * rsct = (RFB_SERVER_CUT_TEXT *)vncclient->input_msg;
					char * text = NULL;
					text = (char*)malloc(rsct->length+1);
					if (text==NULL)
					{
						remotePrint("cannot allocate %u bytes to get cut text buffer\n", rsct->length);
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
				remotePrint("cannot handle msg type:%d\n", vncclient->input_msg[0]);
				ret=-1;
				goto end;
		}
	} // end main loop

end:
	vncclient->vnc_end=1;
	return 0;
}

void reset_updated_region(struct vnc_client *vncclient)
{
	vncclient->updated_rect.w=0;
	vncclient->updated_rect.h=0;
	vncclient->updated_rect.x=0;
	vncclient->updated_rect.y=0;
}
void grow_updated_region(struct vnc_client *vncclient, SDL_Rect *rect)
{
	unsigned short ax1, ay1, ax2, ay2;
	unsigned short bx1, by1, bx2, by2;

  /* Original update rectangle */
  ax1 = vncclient->updated_rect.x;
  ay1 = vncclient->updated_rect.y;
  ax2 = vncclient->updated_rect.x + vncclient->updated_rect.w;
  ay2 = vncclient->updated_rect.y + vncclient->updated_rect.h;
  /* New update rectangle */
  bx1 = rect->x;
  by1 = rect->y;
  bx2 = rect->x + rect->w;
  by2 = rect->y + rect->h;
  /* Adjust */
  if (bx1 < ax1) ax1 = bx1;
  if (by1 < ay1) ay1 = by1;
  if (bx2 > ax2) ax2 = bx2;
  if (by2 > ay2) ay2 = by2;
  /* Update */
  vncclient->updated_rect.x = ax1;
  vncclient->updated_rect.y = ay1;
  vncclient->updated_rect.w = ax2 - ax1;
  vncclient->updated_rect.h = ay2 - ay1;
}

static int handleRectangle(struct vnc_client *vncclient)
{
	int ret=0;
	SDL_Surface *scratchbuffer;
	SDL_Rect rect;
	RFB_FRAMEBUFFER_UPDATE_RECTANGLE rfbur;
	
	remotePrint("handleRectangle\n");

	ret = rfbGetRectangleInfo(&rfbur);
	if (ret<0)
		goto end;
	
	remotePrint("update rectangle: encoding_type:%d, width:%d, height:%d, x_position:%d, y_position:%d\n",
		rfbur.encoding_type,
		rfbur.width,
		rfbur.height,
		rfbur.x_position,
		rfbur.y_position);

	rect.x = rfbur.x_position;
	rect.y = rfbur.y_position;
	rect.w = rfbur.width;
	rect.h = rfbur.height;
	grow_updated_region(vncclient, &rect); 

	switch (rfbur.encoding_type)
	{
		case RFB_Raw:
			{
				remotePrint("RFB_Raw\n");
				
				scratchbuffer = SDL_CreateRGBSurface(SDL_SWSURFACE, rect.w, rect.h,
					vncclient->bits_pp, vncclient->rmask, vncclient->gmask, vncclient->bmask, vncclient->amask);
        if (scratchbuffer) {                                  
         SDL_SetAlpha(scratchbuffer,0,0);
         remotePrint("created scratchbuffer.\n");
        } else {
					remotePrint("failed to create scratchbuffer.\n");
					ret = -1;
					goto end;
        }
				ret = rfbGetBytes(scratchbuffer->pixels, rect.w * rect.h * vncclient->bytes_pp);
				if (ret<0)
				{
					remotePrint("failed to get %d pixels\n", rect.w * rect.h * vncclient->bytes_pp);
					goto end;
				}
				SDL_BlitSurface(scratchbuffer, NULL, vncclient->framebuffer, &rect);
				SDL_FreeSurface(scratchbuffer);
				remotePrint("blitted scratchbuffer to framebuffer\n");
			}
			break;

		case RFB_CopyRect:
			{
				remotePrint("RFB_CopyRect\n");
				RFB_COPYRECT_INFO rci;
				SDL_Rect src_rect;
				ret = rfbGetBytes((unsigned char *)&rci, sizeof(RFB_COPYRECT_INFO));
				if (ret<0)
				{
					remotePrint("failed to get RFB_COPYRECT_INFO\n");
					goto end;
				}
				src_rect.x = rci.src_x_position;
				src_rect.y = rci.src_y_position;
				src_rect.w = rfbur.width;
				src_rect.h = rfbur.height;
				blitFromDisplay(vncclient->framebuffer, &src_rect, &rect);
			}
			break;
		case RFB_RRE:
			{
				ret=handleRRERectangles(vncclient, &rect);
				if (ret<0)
					goto end;
			}
			break;
		case RFB_Hextile:
			{
				ret=handleHextileRectangles(vncclient, &rect);
				if (ret<0)
					goto end;
			}
			break;
		default:
			remotePrint("unsupported encoding type:%d\n", rfbur.encoding_type);
			ret = -1;
			goto end;
	}

end:
	return ret;
}

static int handleRRERectangles(struct vnc_client * vncclient, SDL_Rect *rect)
{
	int ret=0;
	int i;
	unsigned char buf[12];
	unsigned int nb_sub_rectangles;
	unsigned int bg_pixel_value;
	unsigned int subrect_pixel_value;
	unsigned int *tmp;
	RFB_RRE_SUBRECT_INFO * rrsi;
	SDL_Rect sub_rect;

	ret = rfbGetBytes(buf, 4 + vncclient->bytes_pp);
	if (ret<0)
	{
		remotePrint("failed to get header\n");
		goto end;
	}
	tmp = (unsigned int *)buf;
	nb_sub_rectangles = *tmp;
	remotePrint("%u sub-rectangles\n", nb_sub_rectangles);
	
	tmp = (unsigned int *)(buf+4);
	if (vncclient->bits_pp <= 16)
		bg_pixel_value = *tmp >> vncclient->bits_pp;
	else
		bg_pixel_value = *tmp;

	ret = SDL_FillRect(vncclient->framebuffer, rect, bg_pixel_value);
	if (ret<0) {
		remotePrint("failed to fill background rectangle\n");
		goto end;
	}

	for (i=0;i<nb_sub_rectangles;i++) {
		ret = rfbGetBytes(buf, 8 + vncclient->bytes_pp);
		if (ret<0) {
			remotePrint("failed to get sub rect info\n");
			goto end;
		}
		tmp = (unsigned int *)buf;
		if (vncclient->bits_pp <= 16)
			subrect_pixel_value = *tmp >> vncclient->bits_pp;
		else
			subrect_pixel_value = *tmp;
		rrsi = (RFB_RRE_SUBRECT_INFO *)(buf+vncclient->bytes_pp);
		sub_rect.x = rect->x + rrsi->x_position;
		sub_rect.y = rect->y + rrsi->y_position;
		sub_rect.w = rrsi->width;
		sub_rect.h = rrsi->height;
		ret = SDL_FillRect(vncclient->framebuffer, &sub_rect, subrect_pixel_value);
		if (ret<0) {
			remotePrint("failed to fill sub-rectangle %i\n", i);
			goto end;
		}
	}
end:
	return ret;
}
static int handleHextileRectangles(struct vnc_client * vncclient, SDL_Rect *rect)
{
	int ret=0;
	int nb_16x16_tiles_in_row;
	int nb_16x16_tiles_in_column;
	int last_tile_width;
	int last_tile_height;
	int r, c;
	unsigned int *tmp;
	unsigned int bg_pixel_value;
	unsigned int fg_pixel_value;
	unsigned int subrect_pixel_value;
	unsigned char nb_sub_rectangles, s;
	unsigned char subencoding;
	unsigned char buf[6];
	unsigned char sub_rect_xy;
	unsigned char sub_rect_wh;
	SDL_Rect tile_rect;
	SDL_Rect sub_rect;
	SDL_Surface *scratchbuffer = NULL;

	bg_pixel_value = 0;
	fg_pixel_value = 0;

	last_tile_width = rect->w % 16;
	last_tile_height = rect->h % 16;
	nb_16x16_tiles_in_row = rect->w / 16;
	nb_16x16_tiles_in_column = rect->h / 16;

	remotePrint("Hextile encoding: %i of 16x16 tiles in one row, last tile width = %i\n",
		nb_16x16_tiles_in_row, last_tile_width);
	remotePrint("Hextile encoding: %i of 16x16 tiles in one column, last tile column = %i\n",
		nb_16x16_tiles_in_column, last_tile_height);

	for (c = 0; c < (nb_16x16_tiles_in_column + (last_tile_height ? 1:0)); c++) {
		for (r = 0; r < (nb_16x16_tiles_in_row + (last_tile_width ? 1:0)); r++) {
				
			tile_rect.x = rect->x + r * 16;
			tile_rect.y = rect->y + c * 16;
			if (r == nb_16x16_tiles_in_row)
				tile_rect.w = last_tile_width;
			else
				tile_rect.w = 16;
			if (c == nb_16x16_tiles_in_column)
				tile_rect.h = last_tile_height;
			else
				tile_rect.h = 16;

			ret = rfbGetBytes(&subencoding, 1);
			if (ret<0)
			{
				remotePrint("failed to get tile subencoding\n");
				goto end;
			}

			if (subencoding & Hextile_Raw) {
				scratchbuffer = SDL_CreateRGBSurface(SDL_SWSURFACE, tile_rect.w, tile_rect.h,
					vncclient->bits_pp, vncclient->rmask, vncclient->gmask, vncclient->bmask, vncclient->amask);
        if (scratchbuffer)
					SDL_SetAlpha(scratchbuffer,0,0);
        else {
					remotePrint("raw tile [%i,%i]: failed to create scratchbuffer.\n", r, c);
					ret = -1;
					goto end;
        }
				ret = rfbGetBytes(scratchbuffer->pixels, tile_rect.w * tile_rect.h * vncclient->bytes_pp);
				if (ret<0)
				{
					remotePrint("raw tile [%i,%i]: failed to get %i pixels\n", r, c,
						tile_rect.w * tile_rect.h * vncclient->bytes_pp);
					goto end;
				}
				SDL_BlitSurface(scratchbuffer, NULL, vncclient->framebuffer, &tile_rect);
				SDL_FreeSurface(scratchbuffer);
				continue;
			}

			if (subencoding & Hextile_BackgroundSpecified) {
				ret = rfbGetBytes(buf, vncclient->bytes_pp);
				if (ret<0)
				{
					remotePrint("tile [%i,%i]: failed to get hextile background color\n", r, c);
					goto end;
				}
				tmp = (unsigned int *)buf;
				if (vncclient->bits_pp <= 16)
					bg_pixel_value = *tmp >> vncclient->bits_pp;
				else
					bg_pixel_value = *tmp;
			}

			/* fill tile with background value */
			ret = SDL_FillRect(vncclient->framebuffer, &tile_rect, bg_pixel_value);
			if (ret<0) {
				remotePrint("tile [%i,%i]: failed to fill background\n", r, c);
				goto end;
			}

			if (!(subencoding & Hextile_AnySubrects))
				continue;

			if (subencoding & Hextile_ForegroundSpecified) {
				ret = rfbGetBytes(buf, vncclient->bytes_pp);
				if (ret<0)
				{
					remotePrint("tile [%i,%i]: failed to get foreground color\n", r, c);
					goto end;
				}
				tmp = (unsigned int *)buf;
				if (vncclient->bits_pp <= 16)
					fg_pixel_value = *tmp >> vncclient->bits_pp;
				else
					fg_pixel_value = *tmp;

				if (subencoding & Hextile_SubrectsColoured)
					remotePrint("tile [%i,%i]: warning, foreground color specified and SubrectsColoured bit is set\n", r, c);
			}

			/*get number of sub-rectangles */
			ret = rfbGetBytes(buf, 1);
			if (ret<0)
			{
				remotePrint("tile [%i,%i]: failed to get number of sub-rectangles\n", r, c);
				goto end;
			}
			nb_sub_rectangles = *buf;
			
			if (subencoding & Hextile_SubrectsColoured) {
				for (s = 0; s < nb_sub_rectangles; s++) {
					ret = rfbGetBytes(buf, vncclient->bytes_pp + 2);
					if (ret<0)
					{
						remotePrint("tile [%i,%i]: sub-rect %u: failed to get datas\n", r, c, s);
						goto end;
					}
					tmp = (unsigned int *)buf;
					if (vncclient->bits_pp <= 16)
						subrect_pixel_value = *tmp >> vncclient->bits_pp;
					else
						subrect_pixel_value = *tmp;

					sub_rect_xy = *(buf + vncclient->bytes_pp);
					sub_rect_wh = *(buf + vncclient->bytes_pp + 1);
					sub_rect.x = tile_rect.x + (sub_rect_xy >> 4);
					sub_rect.y = tile_rect.y + (sub_rect_xy & 0xF);
					sub_rect.w = (sub_rect_wh >> 4) + 1;
					sub_rect.h = (sub_rect_wh & 0xF) + 1;
					ret = SDL_FillRect(vncclient->framebuffer, &sub_rect, subrect_pixel_value);
					if (ret<0) {
						remotePrint("tile [%i,%i]: failed to fill sub rectangle %u\n", r, c, s);
						goto end;
					}
				}
			} else {
				for (s = 0; s < nb_sub_rectangles; s++) {
					ret = rfbGetBytes(buf, 2);
					if (ret<0)
					{
						remotePrint("tile [%i,%i]: failed to get sub-rect %u infos\n", r, c, s);
						goto end;
					}
					sub_rect_xy = *(buf);
					sub_rect_wh = *(buf + 1);
					sub_rect.x = tile_rect.x + (sub_rect_xy >> 4);
					sub_rect.y = tile_rect.y + (sub_rect_xy & 0xF);
					sub_rect.w = (sub_rect_wh >> 4) + 1;
					sub_rect.h = (sub_rect_wh & 0xF) + 1;
					
					ret = SDL_FillRect(vncclient->framebuffer, &sub_rect, fg_pixel_value);
					if (ret<0) {
						remotePrint("tile [%i,%i]: failed to fill sub rectangle %u\n", r, c, s);
						goto end;
					}
				}
			}
		}
	}

end:
	return ret;
}
