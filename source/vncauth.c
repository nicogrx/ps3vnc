#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "d3des.h"

#define CHALLENGESIZE 16
void vncEncryptBytes(unsigned char *bytes, char *passwd)
{
	unsigned char key[8];
  int i;

  for (i = 0; i < 8; i++)
	{
		if (i < strlen(passwd))
		{
	    key[i] = passwd[i];
		} else
		{
	    key[i] = 0;
		}
  }

  deskey(key, EN0);

  for (i = 0; i < CHALLENGESIZE; i += 8)
	{
		des(bytes+i, bytes+i);
  }
}
