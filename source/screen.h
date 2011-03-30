extern VideoResolution res;

extern void initScreen(void);
extern void updateScreen(void);
int drawRectangleToScreen(unsigned int *buffer,
		unsigned int width,
		unsigned int height,
		unsigned int x,
		unsigned int y,
		int swap);
extern unsigned int * getCurrentFrameBuffer(void);
extern unsigned int * getOldFrameBuffer(void);
extern void waitFlip(void);
