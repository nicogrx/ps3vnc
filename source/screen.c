#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>
#include "screen.h"
#include "remoteprint.h"

#define FONT_FILE "/dev_hdd0/tmp/FreeSans.ttf"

DisplayResolution res;
static SDL_Surface *screen_surface = NULL;
static TTF_Font *font;
static SDL_Color text_color = { 255, 255, 255 };

int initDisplay(int width, int height)
{
	if(SDL_Init(SDL_INIT_VIDEO) < 0) 
  {
		remotePrint( "Unable to init SDL: %s\n", SDL_GetError() );
    return -1;
  }
	atexit( SDL_Quit );
	screen_surface = SDL_SetVideoMode(width, height, 32,
		SDL_HWSURFACE | SDL_DOUBLEBUF);
  if(screen_surface == NULL)
  {
		remotePrint("Unable to set video mode: %s\n", SDL_GetError());
    return -1;
  }
	res.width = width;
	res.height = height;

	if (TTF_Init()) {
		remotePrint("Unable to init TTF\n", TTF_GetError());
		SDL_Quit();
    return -1;
	}

	font = TTF_OpenFont(FONT_FILE, 24);
	if (!font) {
		remotePrint("TTF_OpenFont failed\n", TTF_GetError());
		TTF_Quit();
		SDL_Quit();
		return -1;
	}

	return 0;
}

void closeDisplay()
{
	TTF_Quit();
	if (screen_surface)
	SDL_FreeSurface(screen_surface);
  SDL_Quit();
}

void fillDisplay(SDL_Surface *in_surface, SDL_Rect *updated_rect)
{
  SDL_BlitSurface(in_surface, updated_rect, screen_surface, updated_rect);
	SDL_UpdateRects(screen_surface, 1, updated_rect);
}
void blitFromDisplay(SDL_Surface *out_surface, SDL_Rect *src_rect, SDL_Rect *dest_rect)
{
	SDL_BlitSurface(screen_surface, src_rect, out_surface, dest_rect);
}
void updateDisplay(void)
{
	SDL_Flip(screen_surface);
}

int blitText(const char *text, SDL_Rect *dest_rect)
{
	SDL_Surface *text_surface;

	text_surface = TTF_RenderText_Solid(font, text, text_color);
	if (!text_surface) {
		remotePrint("TTF_RenderText_Solid failed\n", TTF_GetError());
		return -1;
	}
	SDL_FillRect(screen_surface, dest_rect, 0);
	SDL_BlitSurface(text_surface, NULL, screen_surface, dest_rect);
	SDL_Flip(screen_surface);
	return 0;
}
