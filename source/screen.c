/* Now double buffered with animation.
 */

#include <ppu-lv2.h>
 
#include <malloc.h>
#include <string.h>
#include <unistd.h>

#include <sysutil/video.h>
#include <rsx/gcm_sys.h>
#include <rsx/rsx.h>
#include "rsxutil.h"

#include <altivec.h>

#include "rsxutil.h"
#include "screen.h"
#include "psprint.h"

#define MAX_BUFFERS 2

gcmContextData *context;
void *host_addr = NULL;
rsxBuffer frame_buffers[MAX_BUFFERS];
int current_frame_buffer = 0;
DisplayResolution res;

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

	//PSPRINT("draw 16bpp rectangle\n(%d,%d) @ (%d,%d)\n", width, height, x, y);
	//PSPRINT("screen size = (%d,%d)\n", res.width, res.height);

	if (x+width>res.width || y+height>res.height)
	{
		return -1;
	}

	v_src_width = width/8;	
	if (v_src_width!=0)
	{
		// use vectorized instructions to copy rectangle
		vc_src = (vector unsigned char *)buffer;
		vi_dest = (vector unsigned int *)(frame_buffers[current_frame_buffer].ptr+y*res.width+x);
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
				vi_dest[wd]   = (vector unsigned int)vec_mergeh(
								(vector unsigned short)vc_tmp1, 
								(vector unsigned short)vc_tmp2); // got 4 32bpp pixels !
				vi_dest[wd+1] = (vector unsigned int)vec_mergel(
								(vector unsigned short)vc_tmp1,
								(vector unsigned short)vc_tmp2); // got 4 32bpp pixels !

				vc_tmp1 = vec_mergel(vc_0, vc_red);
				vc_tmp2 = vec_mergel(vc_green, vc_blue);
				vi_dest[wd+2] = (vector unsigned int)vec_mergeh(
								(vector unsigned short)vc_tmp1,
								(vector unsigned short)vc_tmp2); // got 4 32bpp pixels !
				vi_dest[wd+3] = (vector unsigned int)vec_mergel(
								(vector unsigned short)vc_tmp1,
								(vector unsigned short)vc_tmp2); // got 4 32bpp pixels !
			}
			vc_src  += v_src_width;
			vi_dest += v_dest_width;
		}
		//PSPRINT("end of vectorized copy\n");
	}
	
	vrest_src_width = width%8;
	if (vrest_src_width!=0)
	{
		src = buffer + v_src_width*8;
		dest = frame_buffers[current_frame_buffer].ptr + y*res.width+x + v_src_width*8;
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
		//PSPRINT("end of scalar copy\n");
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

	//PSPRINT("draw 32bpp rectangle\n(%d,%d) @ (%d,%d)\n", width, height, x, y);
	//PSPRINT("screen size = (%d,%d)\n", res.width, res.height);

	if (x+width>res.width || y+height>res.height)
	{
		return -1;
	}

	v_src_width = width/4;	
	if (v_src_width!=0)
	{
		// use vectorized instructions to copy rectangle
		vi_src = (vector unsigned int *)buffer;
		vi_dest = (vector unsigned int *)(frame_buffers[current_frame_buffer].ptr+y*res.width+x);
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
		//PSPRINT("end of vectorized copy\n");
	}
	vrest_src_width = width%4;
	if (vrest_src_width!=0)
	{
		src = buffer + v_src_width*4;
		dest = frame_buffers[current_frame_buffer].ptr + y*res.width+x + v_src_width*4;
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
		//PSPRINT("end of scalar copy\n");
	}

	return 0;
}

void initDisplay()
{
	int i=0;

 /* Allocate a 1Mb buffer, alligned to a 1Mb boundary
  * to be our shared IO memory with the RSX. */
	host_addr = memalign (1024*1024, HOST_SIZE);
	context = initScreen (host_addr, HOST_SIZE);

  getResolution(&(res.width), &(res.height));
  for (i = 0; i < MAX_BUFFERS; i++)
    makeBuffer( &frame_buffers[i], res.width, res.height, i);
  flip(context, MAX_BUFFERS - 1);


}
void closeDisplay()
{
	int i;
  gcmSetWaitFlip(context);
  for (i = 0; i < MAX_BUFFERS; i++)
    rsxFree(frame_buffers[i].ptr);
  rsxFinish(context, 1);
  free(host_addr);
}

void updateDisplay(void)
{
	flip(context, frame_buffers[current_frame_buffer].id); // Flip buffer onto screen
	current_frame_buffer++;
	if (current_frame_buffer >= MAX_BUFFERS)
      current_frame_buffer = 0;

}
