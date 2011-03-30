/* Now double buffered with animation.
 */ 
#include <psl1ght/lv2.h>
#include <malloc.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sysutil/video.h>
#include <rsx/gcm.h>
#include <rsx/reality.h>
#include "screen.h"
#include "remoteprint.h"

gcmContextData *context; // Context to keep track of the RSX buffer.
VideoResolution res; // Screen Resolution
int current_frame_buffer = 0;
u32 *frame_buffers[2]; // The buffer we will be drawing into.

void waitFlip(void)
{ // Block the PPU thread untill the previous flip operation has finished.
	while(gcmGetFlipStatus() != 0) 
		usleep(200);
	gcmResetFlipStatus();
}

static void flip(s32 frame_buffer)
{
	assert(gcmSetFlip(context, frame_buffer) == 0);
	realityFlushBuffer(context);
	gcmSetWaitFlip(context); // Prevent the RSX from continuing until the flip has finished.
}

// Initilize everything. You can probally skip over this function.
void initScreen()
{
	// Allocate a 1Mb buffer, alligned to a 1Mb boundary to be our shared IO memory with the RSX.
	void *host_addr = memalign(1024*1024, 1024*1024);
	assert(host_addr != NULL);

	// Initilise Reality, which sets up the command buffer and shared IO memory
	context = realityInit(0x10000, 1024*1024, host_addr); 
	assert(context != NULL);

	VideoState state;
	assert(videoGetState(0, 0, &state) == 0); // Get the state of the display
	assert(state.state == 0); // Make sure display is enabled

	// Get the current resolution
	assert(videoGetResolution(state.displayMode.resolution, &res) == 0);
	
	// Configure the buffer format to xRGB
	VideoConfiguration vconfig;
	memset(&vconfig, 0, sizeof(VideoConfiguration));
	vconfig.resolution = state.displayMode.resolution;
	vconfig.format = VIDEO_BUFFER_FORMAT_XRGB;
	vconfig.pitch = res.width * 4;
	vconfig.aspect=state.displayMode.aspect;

	assert(videoConfigure(0, &vconfig, NULL, 0) == 0);
	assert(videoGetState(0, 0, &state) == 0); 

	s32 frame_buffers_size = 4 * res.width * res.height; // each pixel is 4 bytes
	gcmSetFlipMode(GCM_FLIP_VSYNC); // Wait for VSYNC to flip

	// Allocate two buffers for the RSX to draw to the screen (double buffering)
	frame_buffers[0] = rsxMemAlign(16, frame_buffers_size);
	frame_buffers[1] = rsxMemAlign(16, frame_buffers_size);
	assert(frame_buffers[0] != NULL && frame_buffers[1] != NULL);

	u32 offset[2];
	assert(realityAddressToOffset(frame_buffers[0], &offset[0]) == 0);
	assert(realityAddressToOffset(frame_buffers[1], &offset[1]) == 0);
	// Setup the display buffers
	assert(gcmSetDisplayBuffer(0, offset[0], res.width * 4, res.width, res.height) == 0);
	assert(gcmSetDisplayBuffer(1, offset[1], res.width * 4, res.width, res.height) == 0);

	gcmResetFlipStatus();
	flip(1);
}

//assuming input buffer type is XRGD - 32bpp
int drawRectangleToScreen(unsigned int *buffer,
		unsigned int width,
		unsigned int height,
		unsigned int x,
		unsigned int y,
		int swap)
{
	unsigned int h, w;
	unsigned int * src;
	unsigned int * dest;
	unsigned int pixel;
	unsigned int s_pixel;

	RPRINT("draw rectangle\n(%d,%d) @ (%d,%d)\n", width, height, x, y);
	RPRINT("screen size = (%d,%d)\n", res.width, res.height);

	if (x+width>res.width || y+height>res.height)
	{
		return -1;
	}
	src = buffer;
	dest=frame_buffers[current_frame_buffer]+y*res.width+x;
	
	if (swap)
	{
		for(h = 0; h < height; h++)
		{
			for(w = 0; w < width; w++)
			{
				pixel = src[w];
				s_pixel =
						((pixel >> 24) & 0x000000FF) |
						((pixel >> 8) & 0x0000FF00) |
						((pixel << 8) & 0x00FF0000) |
						(pixel << 24);
				dest[w]=s_pixel;
			}
			src+=width;
			dest+=res.width;
		}
	}
	else
	{
		for(h = 0; h < height; h++)
		{
			for(w = 0; w < width; w++)
			{
				pixel = src[w];
				dest[w] = pixel;
			}
			src+=width;
			dest+=res.width;
		}
	}
	return 0;
}

void updateScreen(void)
{
	flip(current_frame_buffer); // Flip buffer onto screen
	current_frame_buffer = !current_frame_buffer;
}

unsigned int * getCurrentFrameBuffer(void)
{
	return frame_buffers[current_frame_buffer];
}
unsigned int * getOldFrameBuffer(void)
{
	return frame_buffers[!current_frame_buffer];
}
