typedef struct
{
	unsigned short width;
	unsigned short height;
} DisplayResolution;
extern DisplayResolution res;

extern void initDisplay(void);
extern void updateDisplay(void);
extern void closeDisplay(void);
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

enum draw_screen_mode
{
	DS_MODE_16BPP,
	DS_MODE_32BPP
};

