typedef struct {
	int char_width;
	int char_height;
} PSFont;

typedef struct{
	int width;
	int height;
	int pixel_width;
	int pixel_height;
	int fg_color;
	int bg_color;
	char * char_buf;
	char * char_buf_cpy;
	unsigned int * pixel_buf;
	PSFont * font;
} PSTerm;

extern int PSTermInit(PSTerm * psterm, PSFont * psfont);
extern void PSTermDestroy(PSTerm * psterm);
extern int PSTermUpdate(PSTerm * psterm, char * text);
extern void PSTermDraw (PSTerm * psterm);
