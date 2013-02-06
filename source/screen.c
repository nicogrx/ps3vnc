#include <SDL/SDL.h>
#include "screen.h"
#include "remoteprint.h"

DisplayResolution res;
static SDL_Surface *screen_surface = NULL;

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
	return 0;
}

void closeDisplay()
{
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
