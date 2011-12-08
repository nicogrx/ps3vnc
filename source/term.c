#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <malloc.h>
#include "font.h"
#include "psprint.h"

int PSTermInit(PSTerm * psterm, PSFont * psfont)
{
	int ret=0;

	if (psterm->fg_color==psterm->bg_color)
	{
		ret= -1;
		goto end;
	}

	psterm->char_buf=NULL;
	psterm->char_buf = (char *)malloc(psterm->width*psterm->height+1);
	if (psterm->char_buf==NULL)
	{
		ret= -1;
		goto end;
	}
	psterm->char_buf_cpy=NULL;
	psterm->char_buf_cpy = (char *)malloc(psterm->width*psterm->height+1);
	if (psterm->char_buf_cpy==NULL)
	{
		ret= -1;
		goto end;
	}
	
	psterm->pixel_width=psterm->width*psfont->char_width;
	psterm->pixel_height=psterm->height*psfont->char_height;

	psterm->pixel_buf=NULL;
	psterm->pixel_buf = (unsigned int *)malloc(psterm->pixel_width*psterm->pixel_height*4);
	if (psterm->pixel_buf==NULL)
	{
		ret= -1;
		free(psterm->char_buf);
		goto end;
	}

	memset(psterm->char_buf, 0, psterm->width*psterm->height+1);
	psterm->font = psfont;

end:
	return ret;
}
void PSTermDestroy(PSTerm * psterm)
{
	if (psterm->char_buf!=NULL)
		free(psterm->char_buf);
	if (psterm->char_buf_cpy!=NULL)
		free(psterm->char_buf_cpy);
	if (psterm->pixel_buf!=NULL)
		free(psterm->pixel_buf);
}
int PSTermUpdate(PSTerm * psterm, char * text)
{
	int ret = 0;
	int max_chars, used_chars;
	int text_length;

	char * tmp_buf;
	
	max_chars = psterm->width*psterm->height;
	used_chars = strlen(psterm->char_buf);
	text_length = strlen(text);

	if (text_length>max_chars) // text too big for PSTerm size
	{
		ret = -1;
		goto end;
	}

	if (text_length>(max_chars-used_chars))
	{
		strcpy(psterm->char_buf_cpy, psterm->char_buf+text_length);
		tmp_buf = psterm->char_buf;
		psterm->char_buf = psterm->char_buf_cpy;
		psterm->char_buf_cpy = tmp_buf;
	}

	strcat(psterm->char_buf, text);
	
end:
	return ret;
}

static char* findStartCharToDraw(PSTerm * psterm)
{
	int l = 0;
	int chars_in_a_line = 0;

	char * last_char = psterm->char_buf+strlen(psterm->char_buf)-1;

	while ( l<psterm->height && (last_char > psterm->char_buf) )
	{
		if (*last_char=='\n' || (chars_in_a_line==psterm->width))
		{
			chars_in_a_line=0;
			l++;
		}
		else
		{
			chars_in_a_line++;
		}
		last_char--;
	}
	return last_char;
}

void PSTermDraw (PSTerm * psterm)
{
	int i;
	int curX=0, curY=0, tempx=0, tempy=0;
	char c;
	char * text;

	text = findStartCharToDraw(psterm);
	for (i=0; i < psterm->pixel_width*psterm->pixel_height ;i++)
	{
		psterm->pixel_buf[i]=psterm->bg_color;
	}
	while(*text != '\0')
	{
		c = *text;
		if(c == '\n')
		{
			curX = 0;
			curY += psterm->font->char_height;
		}
		else
		{
			if (curX+psterm->font->char_width == psterm->pixel_width)
			{
				curX = 0;
				curY += psterm->font->char_height;
			}

			if(c < 32 || c >132)
				c = 180;

			for(i=0; i < psterm->font->char_width*psterm->font->char_height ; i++)
			{
				if(Font_8x16[-32+c][i] == 1)
				{
					psterm->pixel_buf[(curY + tempy)*psterm->pixel_width + curX + tempx] = psterm->fg_color;
				}
				tempx++;
				if (tempx == psterm->font->char_width)
				{
					tempx = 0;
					tempy++;
				}
			}
			tempy = 0;
			curX += psterm->font->char_width;
		}
		++text;
	}
	
}

