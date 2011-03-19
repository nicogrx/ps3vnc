enum ProtocolVersion
{
	RFB_003_003=0,
	RFB_003_007,
	RFB_003_008,
};

enum SecurityTypes
{
	RFB_SEC_TYPE_INVALID=0,
	RFB_SEC_TYPE_NONE,
	RFB_SEC_TYPE_VNC_AUTH,
	RFB_SEC_TYPE_RA2=5,
	RFB_SEC_TYPE_RA2ne,
	RFB_SEC_TYPE_Tight=16,
	RFB_SEC_TYPE_Ultra,
	RFB_SEC_TYPE_TLS,
	RFB_SEC_TYPE_VeNCrypt,
	RFB_SEC_TYPE_GTK_VNC_SASL,
	RFB_SEC_TYPE_MD5_HASH_AUTH,
	RFB_SEC_TYPE_Colin_Dean_xvp
};

enum SecurityResult
{
	RFB_SEC_RESULT_OK=0,
	RFB_SEC_RESULT_FAILED=1
};

enum ClientInitSharedFlag
{
	RFB_NOT_SHARED=0,
	RFB_SHARED,
};

enum ClientToServerMsgs
{
	RFB_SetPIxelFormat=0,
	RFB_SetEncodings=2,
	RFB_FramebufferUpdateRequest,
	RFB_KeyEvent,
	RFB_PointerEvent,
	RFB_ClientCutText
};

enum ServerToClientMsgs
{
	RFB_FramebufferUpdate,
	RFB_SetColourMapEntries,
	RFB_Bell,
	RFB_ServerCutText
};

enum EncodingTypes
{
	RFB_Raw=0,
	RFB_CopyRect,
	RFB_RRE,
	RFB_Hextile=5,
	RFB_ZRLE=16,
	RFB_Cursor_pseudo_encoding=-239,
	RFB_DesktopSize_pseudo_encoding=-223,
};

typedef struct
{
	unsigned char bits_per_pixel;
	unsigned char depth;
	unsigned char big_endian_flag;
	unsigned char true_colour_flag;
	unsigned short red_max;
	unsigned short green_max;
	unsigned short blue_max;
	unsigned char red_shift;
	unsigned char green_shift;
	unsigned char blue_shift;
	unsigned char padding1;
	unsigned char padding2;
	unsigned char padding3;
} PIXEL_FORMAT;

typedef struct
{
	unsigned short framebuffer_width;
	unsigned short framebuffer_height;
	PIXEL_FORMAT pixel_format;
} RFB_SERVER_INIT_MSG;

typedef struct 
{
	int version;
	int security_type;
	RFB_SERVER_INIT_MSG server_init_msg;
	char * server_name_string;
	PIXEL_FORMAT pixel_format;
} RFB_INFO;

// Client messages
typedef struct
{
	unsigned char msg_type;
	unsigned char padding1;
	unsigned char padding2;
	unsigned char padding3;
	PIXEL_FORMAT pixel_format;
} RFB_SET_PIXEL_FORMAT;

typedef struct
{
	unsigned char msg_type;
	unsigned char padding;
	unsigned short number_of_encodings;
	unsigned int * encoding_type;
} RFB_SET_ENCODINGS;

typedef struct
{
	unsigned char msg_type;
	unsigned char incremental;
	unsigned short x_position;
	unsigned short y_position;
	unsigned short width;
	unsigned short height;
} RFB_FRAMEBUFFER_UPDATE_REQUEST;

typedef struct
{
	unsigned char msg_type;
	unsigned char down_flag;
	unsigned short padding;
	unsigned int key;
} RFB_KEY_EVENT;

typedef struct
{
	unsigned char msg_type;
	unsigned char button_mask;
	unsigned short x_position;
	unsigned short y_position;
} RFB_POINTER_EVENT;

typedef struct
{
	unsigned char msg_type;
	unsigned char padding1;
	unsigned char padding2;
	unsigned char padding3;
	unsigned int length;
	unsigned char * text;
} RFB_CLIENT_CUT_TEXT;

// Server messages
typedef struct
{
	unsigned char msg_type;
	unsigned char padding;
	unsigned short number_of_rectangles;
} RFB_FRAMEBUFFER_UPDATE;
typedef struct
{	
	unsigned short x_position;
	unsigned short y_position;
	unsigned short width;
	unsigned short height;
	int encoding_type;
} RFB_FRAMEBUFFER_UPDATE_RECTANGLE;

typedef struct
{	
	unsigned char msg_type;
	unsigned char padding;
	unsigned short first_colour;
	unsigned short number_of_colours;
} RFB_SET_COLOUR_MAP_ENTRIES;
typedef struct
{	
	unsigned short red;
	unsigned short green;
	unsigned short blue;
} RFB_SET_COLOUR_MAP_ENTRIES_COLOUR;

typedef struct
{
	unsigned char msg_type;
	unsigned char padding1;
	unsigned char padding2;
	unsigned char padding3;
	unsigned int length;
	unsigned char * text;
} RFB_SERVER_CUT_TEXT;

// prototypes
extern int rfbConnect(const char * server_addr, int port);
extern void rfbClose(void);
extern int rfbGetString(char * string);
extern int rfbGetProtocolVersion(void);
extern int rfbGetSecurityTypes(unsigned char * types); // version 3.7 onwards
extern int rfbGetSecurityType(void); // version 3.3
extern int rfbGetSecurityResult(void);
extern int rfbGetSecurityChallenge(unsigned char * challenge);
extern int rfbGetServerInitMsg(RFB_SERVER_INIT_MSG * server_init_msg);
extern int rfbGetMsg(void * data);

extern int rfbSendProtocolVersion(int version);
extern int rfbSendSecurityType(unsigned char type); // version 3.7 onwards
extern int rfbSendSecurityChallenge(unsigned char * challenge);
extern int rfbSendClientInit(unsigned char flag);
extern int rfbSendMsg(unsigned int msg_type, void * data);
