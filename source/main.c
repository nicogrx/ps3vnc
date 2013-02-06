#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <net/net.h>

#include <SDL/SDL.h>
#include <SDL/SDL_thread.h>

#include "rfb.h"
#include "vncauth.h"
#include "rsxutil.h"
#include "screen.h"
#include "tick.h"
#include "remoteprint.h"

#define MAX_CHARS 128
struct vnc_client {
	RFB_INFO rfb_info;
	unsigned char input_msg[32];
	unsigned char output_msg[32];
	int vnc_end;
	int frame_update_requested;
	char server_ip[MAX_CHARS];
	char password[MAX_CHARS];
	SDL_Thread *msg_thread;
	SDL_mutex *display_mutex;
	SDL_Surface *framebuffer;
	SDL_Rect updated_rect;
	unsigned int rmask;
	unsigned int gmask;
	unsigned int bmask;
	unsigned int amask;
};

// functions prototypes
static int handshake(struct vnc_client *vncclient);
static int authenticate(char *password);
static int init(struct vnc_client *vncclient);
//static int handleInputEvents(void * data);
static int handleMsgs(void * data);
static int handleRectangle(struct vnc_client *vncclient);
#if 0
static int handleRRERectangles(struct vnc_client *vncclient, 
	const RFB_FRAMEBUFFER_UPDATE_RECTANGLE *, int, int, int);
#endif
static int key_event(struct vnc_client *vncclient,
	unsigned char downflag, unsigned int key);
static int pointer_event(struct vnc_client *vncclient,
	unsigned char buttonmask, unsigned short x, unsigned short y);
static void reset_updated_region(struct vnc_client *);

#define CONFIG_FILE "/dev_hdd0/tmp/vncconfig.txt"
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
	RFB_KEY_EVENT * rke = (RFB_KEY_EVENT *)vncclient->output_msg;
	rke->down_flag = downflag;
	rke->key = key;
	return rfbSendMsg(RFB_KeyEvent, rke);
}

static int pointer_event(struct vnc_client *vncclient,
	unsigned char buttonmask, unsigned short x, unsigned short y)
{
	RFB_POINTER_EVENT * rpe = (RFB_POINTER_EVENT *)vncclient->output_msg;
	rpe->button_mask = buttonmask;
	rpe->x_position = x;
	rpe->y_position = y;
	return rfbSendMsg(RFB_PointerEvent, rpe);
}
static int get_sdl_event(struct vnc_client *vncclient) {
	SDL_Event event;
	static unsigned short x = 0;
	static unsigned short y = 0;
	static unsigned char buttonmask = 0;

  while(SDL_PollEvent(&event)) {
		switch(event.type) {
		case SDL_QUIT:
			return 1;
		case SDL_KEYDOWN:
			if(event.key.keysym.sym == SDLK_q) 
				return 1;
			key_event(vncclient, 1, event.key.keysym.sym);
			break;
    case SDL_KEYUP:
			key_event(vncclient, 0, event.key.keysym.sym);
			break;
		case SDL_MOUSEMOTION:
			x = event.motion.x;
			y = event.motion.y;
			pointer_event(vncclient, buttonmask, x, y);
			break;
		case SDL_MOUSEBUTTONDOWN:
			buttonmask |= get_button_mask(&event);
			pointer_event(vncclient, buttonmask, x, y);
			break;
		case SDL_MOUSEBUTTONUP:
			buttonmask &= ~get_button_mask(&event);
			pointer_event(vncclient, buttonmask, x, y);
			break;
		}
  }
	return 0;
}

int main(int argc, const char* argv[])
{
	int port, ret=0;
	FILE * pf;
	struct vnc_client vncclient;

	vncclient.vnc_end=0;
	vncclient.frame_update_requested=0;
	startTicks();

	ret = netInitialize();
	if (ret < 0)
		return ret;

#ifdef REMOTE_PRINT
	ret = remotePrintConnect("192.168.1.4");
	if (ret<0)
		goto net_close;
#endif

	ret = initDisplay(1920, 1080);
	if (ret)
		goto rprint_close;

	reset_updated_region(&vncclient);
	
	vncclient.display_mutex=SDL_CreateMutex();
	if(!vncclient.display_mutex)
		goto display_close;
	
	memset(vncclient.server_ip, 0, MAX_CHARS);
	memset(vncclient.password, 0, MAX_CHARS);

	remotePrint("Read configuration file: %s\n", CONFIG_FILE);
	pf=NULL;
	pf = fopen(CONFIG_FILE, "r");

	if (pf==NULL)
	{
		remotePrint("failed to open configuration file\n");
		vncclient.vnc_end=1;
		sleep(2);
		goto mutex_destroy;
	}

	fscanf(pf, "%s\n", vncclient.server_ip);
	fscanf(pf, "%s\n", vncclient.password);
	fclose(pf);
	remotePrint("server ip address = %s\n", vncclient.server_ip);
	remotePrint("password = %s\n", vncclient.password);
	remotePrint("connecting to %s\n", vncclient.server_ip);

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
	
	vncclient.msg_thread = SDL_CreateThread(handleMsgs, (void*)&vncclient); 
	while(!vncclient.vnc_end) {
			vncclient.vnc_end = get_sdl_event(&vncclient);
			usleep(10000);
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
	SDL_DestroyMutex(vncclient.display_mutex);
display_close:
	closeDisplay();
rprint_close:
#ifdef REMOTE_PRINT
	remotePrintClose();
#endif
net_close:
	netDeinitialize();
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

	remotePrint("framebuffer_width:%i framebuffer_height:%i\n",
		vncclient->rfb_info.server_init_msg.framebuffer_width,
		vncclient->rfb_info.server_init_msg.framebuffer_height);
	remotePrint("bits_per_pixel:%i depth:%i big_endian_flag:%i true_colour_flag:%i\n",
		vncclient->rfb_info.server_init_msg.pixel_format.bits_per_pixel,
		vncclient->rfb_info.server_init_msg.pixel_format.depth,
		vncclient->rfb_info.server_init_msg.pixel_format.big_endian_flag,
		vncclient->rfb_info.server_init_msg.pixel_format.true_colour_flag);
	remotePrint("red_max:%i green_max:%i blue_max:%i\n",
		vncclient->rfb_info.server_init_msg.pixel_format.red_max,
		vncclient->rfb_info.server_init_msg.pixel_format.green_max,
		vncclient->rfb_info.server_init_msg.pixel_format.blue_max);
	remotePrint("red_shift:%i green_shift:%i blue_shift:%i\n",
		vncclient->rfb_info.server_init_msg.pixel_format.red_shift,
		vncclient->rfb_info.server_init_msg.pixel_format.green_shift,
		vncclient->rfb_info.server_init_msg.pixel_format.blue_shift);


	// check width & height
	if (vncclient->rfb_info.server_init_msg.framebuffer_width>res.width ||
			vncclient->rfb_info.server_init_msg.framebuffer_height>res.height)
	{
		remotePrint("cannot handle frame size: with=%i, height=%i\n",
			vncclient->rfb_info.server_init_msg.framebuffer_width,
			vncclient->rfb_info.server_init_msg.framebuffer_height);
		ret=-1;
		goto end;
	}
	
	// check bpp & depth
	// FIXME: in theory, every pixel format should be supported ...
	// moreover, should be possible to send prefered PIXEL FORMAT to server
	if (vncclient->rfb_info.server_init_msg.pixel_format.bits_per_pixel==32 &&
			vncclient->rfb_info.server_init_msg.pixel_format.depth==24)
	{
    vncclient->rmask = 0xff000000;
    vncclient->gmask = 0x00ff0000;
    vncclient->bmask = 0x0000ff00;
    vncclient->amask = 0x000000ff;
		vncclient->framebuffer = SDL_CreateRGBSurface(SDL_SWSURFACE,
			vncclient->rfb_info.server_init_msg.framebuffer_width,
			vncclient->rfb_info.server_init_msg.framebuffer_height,
			32, vncclient->rmask, vncclient->gmask, vncclient->bmask, vncclient->amask);
		if (vncclient->framebuffer==NULL) {
			remotePrint("could not create framebuffer:%s.\n", SDL_GetError());
			ret = -1;
			goto end;
		} else {
			remotePrint("Framebuffer created\n");
		}
		SDL_SetAlpha(vncclient->framebuffer,0,0);
	}
	else if (vncclient->rfb_info.server_init_msg.pixel_format.bits_per_pixel==16 &&
			vncclient->rfb_info.server_init_msg.pixel_format.depth==16)
	{
    vncclient->rmask = 0xf800;
    vncclient->gmask = 0x7e0;
    vncclient->bmask = 0x1f;
		vncclient->framebuffer = SDL_CreateRGBSurface(SDL_SWSURFACE,
			vncclient->rfb_info.server_init_msg.framebuffer_width,
			vncclient->rfb_info.server_init_msg.framebuffer_height,
			16, vncclient->rmask, vncclient->gmask, vncclient->bmask, 0);
		if (vncclient->framebuffer==NULL) {
			remotePrint("could not create framebuffer:%s.\n", SDL_GetError());
			ret = -1;
			goto end;
		} else {
			remotePrint("Framebuffer created\n");
		}
		SDL_SetAlpha(vncclient->framebuffer,0,0);
	}
	else
	{
		remotePrint("cannot handle bpp=%d, depth=%d\n",
		vncclient->rfb_info.server_init_msg.pixel_format.bits_per_pixel,
		vncclient->rfb_info.server_init_msg.pixel_format.depth);
		ret=-1;
		goto end;

	}

	// check colour mode
	if (vncclient->rfb_info.server_init_msg.pixel_format.true_colour_flag!=1)
	{
		remotePrint("cannot handle colour map\n");
		ret=-1;
		goto end;
	}

	// now send supported encodings formats
	{
		RFB_SET_ENCODINGS * rse = (RFB_SET_ENCODINGS *)vncclient->output_msg;
#if 0
		rse->number_of_encodings = 3;
		int encoding_type[3] = {RFB_RRE, RFB_CopyRect, RFB_Raw};
#else
		rse->number_of_encodings = 2;
		int encoding_type[2] = {RFB_CopyRect, RFB_Raw};
#endif
		rse->encoding_type = encoding_type;
		ret = rfbSendMsg(RFB_SetEncodings, rse);
	}

end:
	return ret;
}

// handle incoming msgs and render screen 
static int handleMsgs(void * data)
{
	int i, ret;
	struct vnc_client *vncclient=(struct vnc_client *)data;
	RFB_FRAMEBUFFER_UPDATE_REQUEST * rfbur =
		(RFB_FRAMEBUFFER_UPDATE_REQUEST *)vncclient->output_msg;
	rfbur->incremental = 1;
	rfbur->x_position = 0;
	rfbur->y_position = 0;
	rfbur->width = vncclient->rfb_info.server_init_msg.framebuffer_width;
	rfbur->height = vncclient->rfb_info.server_init_msg.framebuffer_height;
	ret = rfbSendMsg(RFB_FramebufferUpdateRequest, rfbur);
	if (ret<0)
		goto end;
	vncclient->frame_update_requested=1;
	remotePrint("requested initial framebuffer update\n");

	while(!vncclient->vnc_end) // main loop
	{
		//handle server msgs
		ret = rfbGetMsg(vncclient->input_msg);
		if (ret<=0)
		{
				//remotePrint("rfbGetMsg failed, retrying in 10 ms...\n");
				usleep(10000);
				continue;
		}
		
		switch (vncclient->input_msg[0])
		{
			case RFB_FramebufferUpdate:
				{
					RFB_FRAMEBUFFER_UPDATE *rfbu = (RFB_FRAMEBUFFER_UPDATE *)vncclient->input_msg;
					remotePrint("%i rectangles to update\n", rfbu->number_of_rectangles);

					for (i=0;i<rfbu->number_of_rectangles;i++)
					{
						ret = handleRectangle(vncclient);
						if (ret<0)
							goto end;
					}
					vncclient->frame_update_requested=0;
				 
					remotePrint("draw updated rectangle to screen\n");
					fillDisplay(vncclient->framebuffer, &vncclient->updated_rect);
					updateDisplay();
					reset_updated_region(vncclient);
					
					if (!vncclient->frame_update_requested)
					{
						vncclient->frame_update_requested=1;
						// request framebuffer update
						RFB_FRAMEBUFFER_UPDATE_REQUEST * rfbur =
							(RFB_FRAMEBUFFER_UPDATE_REQUEST *)vncclient->output_msg;
						rfbur->incremental = 1;
						rfbur->x_position = 0;
						rfbur->y_position = 0;
						rfbur->width = vncclient->rfb_info.server_init_msg.framebuffer_width;
						rfbur->height = vncclient->rfb_info.server_init_msg.framebuffer_height;
						ret = rfbSendMsg(RFB_FramebufferUpdateRequest, rfbur);
						if (ret<0)
							goto end;
						remotePrint("requested framebuffer update after rendering screen\n");
					}
				}		
				break;
			case RFB_Bell:
#ifdef PAD_ENABLED			
				vibratePad();
#endif
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
	int bpp;
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

	bpp=vncclient->rfb_info.server_init_msg.pixel_format.bits_per_pixel/8;

	switch (rfbur.encoding_type)
	{
		case RFB_Raw:
			{
				remotePrint("RFB_Raw\n");
				unsigned char * dest;
				
				scratchbuffer = SDL_CreateRGBSurface(SDL_SWSURFACE, rect.w, rect.h,
					vncclient->rfb_info.server_init_msg.pixel_format.bits_per_pixel,
          vncclient->rmask, vncclient->gmask, vncclient->bmask, vncclient->amask);
        if (scratchbuffer) {                                  
         SDL_SetAlpha(scratchbuffer,0,0);
         remotePrint("created scratchbuffer.\n");
        } else {
					remotePrint("failed to create scratchbuffer.\n");
					ret = -1;
					goto end;
        }
				dest = scratchbuffer->pixels;
				ret = rfbGetBytes(dest, rect.w * rect.h * bpp);
				if (ret<0)
				{
					remotePrint("failed to %d pixels\n", rect.w * rect.h * bpp);
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
#if 0
		case RFB_RRE:
			{
				ret=handleRRERectangles(vncclient, &rfbur, bpp, bpw, rfb_bpw);
			}
			break;
#endif
		default:
			remotePrint("unsupported encoding type:%d\n", rfbur.encoding_type);
			ret = -1;
			goto end;
	}

end:
	return ret;
}
#if 0
static int handleRRERectangles(struct vnc_client * vncclient, const RFB_FRAMEBUFFER_UPDATE_RECTANGLE * rfbur,
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
	
	remotePrint("handleRRERectangles\n");

	ret = rfbGetBytes(header, 4 + bpp);
	if (ret<0)
	{
		remotePrint("failed to get header\n");
		goto end;
	}
	
	tmp_pint = (unsigned int *)header;
	nb_sub_rectangles = *tmp_pint;

	dest = vncclient->raw_pixel_data + rfbur->y_position*rfb_bpw + rfbur->x_position*bpp;

	remotePrint("%u sub-rectangles to draw\n", nb_sub_rectangles);

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
					int vres_width = vncclient->rfb_info.server_init_msg.framebuffer_width/8;

					remotePrint("use altivec to draw background color [%x]\n", bg_pixel_value);
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
							start+=vncclient->rfb_info.server_init_msg.framebuffer_width;
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
						start+=vncclient->rfb_info.server_init_msg.framebuffer_width;
					}
				}

				remotePrint("draw sub rectangles\n");

				// then, draw sub rectangles
				for (sr=0;sr<nb_sub_rectangles;sr++)
				{
					ret = rfbGetBytes(subrect_info, 8 + bpp);
					if (ret<0)
					{
						remotePrint("failed to get sub rect info\n");
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
						start+=vncclient->rfb_info.server_init_msg.framebuffer_width;
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
					start+=vncclient->rfb_info.server_init_msg.framebuffer_width;
				}

				for (sr=0;sr<nb_sub_rectangles;sr++)
				{
					ret = rfbGetBytes(subrect_info, 8 + bpp);
					if (ret<0)
					{
						remotePrint("failed to get sub rect info\n");
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
						start+=vncclient->rfb_info.server_init_msg.framebuffer_width;
					}
				}
			}
			break;

		default:
			remotePrint("invalid bpp\n");
			goto end;
	}

end:
	return ret;
}
#endif
