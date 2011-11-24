extern VideoResolution res;

extern void initScreen(void);
extern void updateScreen(void);
int draw16bppRectangleToScreen(unsigned short *buffer,
		unsigned int width,
		unsigned int height,
		unsigned int x,
		unsigned int y);
int draw32bppRectangleToScreen(unsigned int *buffer,
		unsigned int width,
		unsigned int height,
		unsigned int x,
		unsigned int y);
extern unsigned int * getCurrentFrameBuffer(void);
extern unsigned int * getOldFrameBuffer(void);
extern unsigned int getScreenWidth(void);
extern unsigned int getScreenWidth(void);
extern void waitFlip(void);

enum draw_screen_mode
{
	DS_MODE_16BPP,
	DS_MODE_32BPP
};
