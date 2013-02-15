#include <SDL/SDL.h>
#include "screen.h"
#include "localprint.h"

static SDL_mutex *local_print_mutex;
static SDL_Rect text_rect;

void localPrintInit(void)
{
	local_print_mutex=SDL_CreateMutex();
	text_rect.x = 0;
	text_rect.y = 800;
	text_rect.w = 300;
	text_rect.h = 60;
}

void localPrintClose(void)
{
	SDL_DestroyMutex(local_print_mutex);
}

void localPrint(const char * fmt, ...)
{
	va_list ap;
	char buffer[128];
	
	SDL_LockMutex(local_print_mutex);

	memset(buffer, 0, sizeof(buffer));
	va_start(ap, fmt);
	vsnprintf(buffer, sizeof(buffer)-1, fmt, ap);
	va_end(ap);

	blitText(buffer, &text_rect);

	SDL_UnlockMutex(local_print_mutex);
}

