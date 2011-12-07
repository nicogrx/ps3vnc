#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "psprint.h"

PSTerm psterm;
PSFont psfont;

int PSPrint(const char * fmt, ...)
{
	va_list ap;
	char buffer[128];
	memset(buffer, 0, sizeof(buffer));
	va_start(ap, fmt);
	vsnprintf(buffer, sizeof(buffer)-1, fmt, ap);
	va_end(ap);
	return PSTermUpdate(&psterm, buffer);
}

