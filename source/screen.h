typedef struct
{
	unsigned short width;
	unsigned short height;
} DisplayResolution;
extern DisplayResolution res;

int initDisplay(int, int);
void updateDisplay(void);
void closeDisplay(void);
void fillDisplay(SDL_Surface *in_surface, SDL_Rect *updated_rect);
void blitFromDisplay(SDL_Surface *out_surface, SDL_Rect *src_rect, SDL_Rect *dest_rect);
void updateDisplay(void);

