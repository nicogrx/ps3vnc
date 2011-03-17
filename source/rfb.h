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



extern int rfbConnect(const char * server_addr, int port);
extern void rfbClose(void);
extern int rfbGetString(char * string);
extern int rfbGetProtocolVersion(void);
extern int rfbSendProtocolVersion(int version);
extern int rfbGetSecurityTypes(unsigned char * types); // version 3.7 onwards
extern int rfbSendSecurityType(unsigned char type); // version 3.7 onwards
extern int rfbGetSecurityType(void); // version 3.3
extern int rfbGetSecurityResult(void);
extern int rfbGetSecurityChallenge(unsigned char * challenge);
extern int rfbSendSecurityChallenge(unsigned char * challenge);
extern int rfbSendClientInit(unsigned char flag);
extern int rfbGetServerInitMsg(RFB_SERVER_INIT_MSG * server_init_msg);
