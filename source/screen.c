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

#include <altivec.h>

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
//assuming input buffer type is XRGD - 16bpp
int draw16bppRectangleToScreen(unsigned short *buffer,
		unsigned int width,
		unsigned int height,
		unsigned int x,
		unsigned int y)
{
	unsigned int h, w, wd;

	unsigned short * src;
	unsigned int * dest;
	unsigned int pixel;

	vector unsigned char * vc_src;
	vector unsigned char vc_rg, vc_gb;
	vector unsigned char vc_red, vc_green, vc_blue;
	vector unsigned char vc_gh, vc_gl, vc_tmp1, vc_tmp2;

	vector unsigned char vc_even = { 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30 };
	vector unsigned char vc_odd  = { 1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31 };

	vector unsigned char vc_0  = vec_splat_u8(0x0);
	vector unsigned char vc_2  = vec_splat_u8(0x2);
	vector unsigned char vc_3  = vec_splat_u8(0x3);
	vector unsigned char vc_5  = vec_splat_u8(0x5);

	vector unsigned int * vi_dest;

	unsigned int v_src_width;
	unsigned int vrest_src_width;
	unsigned int v_dest_width;

	RPRINT("draw 16bpp rectangle\n(%d,%d) @ (%d,%d)\n", width, height, x, y);
	RPRINT("screen size = (%d,%d)\n", res.width, res.height);

	if (x+width>res.width || y+height>res.height)
	{
		return -1;
	}

	v_src_width = width/8;	
	if (v_src_width!=0)
	{
		// use vectorized instructions to copy rectangle
		vc_src = (vector unsigned char *)buffer;
		vi_dest = (vector unsigned int *)(frame_buffers[current_frame_buffer]+y*res.width+x);
		v_dest_width = res.width/4; 
		
		for(h = 0; h < height; h++)
		{
			for(w=0, wd=0 ; w<v_src_width ; w+=2, wd+=4)
			{
				vc_rg = vec_perm(vc_src[w], vc_src[w+1], vc_odd);
				vc_gb = vec_perm(vc_src[w], vc_src[w+1], vc_even);
				
				vc_tmp1 = vec_sr(vc_rg, vc_3);
				vc_red  = vec_sl(vc_tmp1, vc_3); // got 16 red pixel values !
				
				vc_gh  = vec_sl(vc_rg, vc_5);
				vc_tmp1 = vec_sr(vc_gb, vc_5);
				vc_gl  = vec_sl(vc_tmp1, vc_2);
				vc_green = vec_or(vc_gh, vc_gl); // got 16 green pixel values !
				
				vc_blue  = vec_sl(vc_gb, vc_3);	 // got 16 blue pixel values !

				vc_tmp1 = vec_mergeh(vc_0, vc_red);
				vc_tmp2 = vec_mergeh(vc_green, vc_blue);
				vi_dest[wd]   = (vector unsigned int)vec_mergeh((vector unsigned short)vc_tmp1, (vector unsigned short)vc_tmp2); // got 4 32bpp pixels !
				vi_dest[wd+1] = (vector unsigned int)vec_mergel((vector unsigned short)vc_tmp1, (vector unsigned short)vc_tmp2); // got 4 32bpp pixels !

				vc_tmp1 = vec_mergel(vc_0, vc_red);
				vc_tmp2 = vec_mergel(vc_green, vc_blue);
				vi_dest[wd+2] = (vector unsigned int)vec_mergeh((vector unsigned short)vc_tmp1, (vector unsigned short)vc_tmp2); // got 4 32bpp pixels !
				vi_dest[wd+3] = (vector unsigned int)vec_mergel((vector unsigned short)vc_tmp1, (vector unsigned short)vc_tmp2); // got 4 32bpp pixels !
			}
			vc_src  += v_src_width;
			vi_dest += v_dest_width;
		}
		RPRINT("end of vectorized copy\n");
	}
	
	vrest_src_width = width%8;
	if (vrest_src_width!=0)
	{
		src = buffer + v_src_width*8;
		dest = frame_buffers[current_frame_buffer] + y*res.width+x + v_src_width*8;
		for(h = 0; h < height; h++)
		{
			for(w = 0; w < width; w++)
			{
				pixel = (unsigned int)src[w];
			
				// RGhGlB => GlBRGh
				dest[w] = ((unsigned int)(pixel&0xF8)<<16)|
									((unsigned int)(((pixel<<5)|(pixel>>11))&0xFC)<<8)|
									((unsigned int)((pixel>>5)&0xF8));

			}
			src+=width;
			dest+=res.width;
		}
		RPRINT("end of scalar copy\n");
	}
	return 0;
}
//assuming input buffer type is XRGD - 32bpp
int draw32bppRectangleToScreen(unsigned int *buffer,
		unsigned int width,
		unsigned int height,
		unsigned int x,
		unsigned int y)
{
	unsigned int h, w;

	unsigned int * src;
	unsigned int * dest;
	unsigned int pixel;

	vector unsigned int * vi_src;
	vector unsigned int * vi_dest;
	vector unsigned int vi_pixel;
	vector unsigned int vi_8;
	unsigned int v_src_width;
	unsigned int vrest_src_width;
	unsigned int v_dest_width;

	RPRINT("draw 32bpp rectangle\n(%d,%d) @ (%d,%d)\n", width, height, x, y);
	RPRINT("screen size = (%d,%d)\n", res.width, res.height);

	if (x+width>res.width || y+height>res.height)
	{
		return -1;
	}

	v_src_width = width/4;	
	if (v_src_width!=0)
	{
		// use vectorized instructions to copy rectangle
		vi_src = (vector unsigned int *)buffer;
		vi_dest = (vector unsigned int *)(frame_buffers[current_frame_buffer]+y*res.width+x);
		vi_8 = vec_splat_u32(8);
		v_dest_width = res.width/4; // handle 16 pixels per loop => 4 vectors of 4 pixels each
		
		for(h = 0; h < height; h++)
		{
			for(w = 0; w < v_src_width;w++)
			{
				vi_pixel   = vi_src[w];
				vi_dest[w] = vec_sr(vi_pixel, vi_8);
			}
			vi_src  += v_src_width;
			vi_dest += v_dest_width;
		}
		RPRINT("end of vectorized copy\n");
	}
	vrest_src_width = width%4;
	if (vrest_src_width!=0)
	{
		src = buffer + v_src_width*4;
		dest = frame_buffers[current_frame_buffer] + y*res.width+x + v_src_width*4;
		for(h = 0; h < height; h++)
		{
			for(w = 0; w < vrest_src_width; w++)
			{
				pixel = src[w];
				dest[w]=pixel>>8;
			}
			src+=width;
			dest+=res.width;
		}
		RPRINT("end of scalar copy\n");
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
unsigned int getScreenWidth(void)
{
	return res.width;
}
unsigned int getScreenHeight(void)
{
	return res.height;
}
