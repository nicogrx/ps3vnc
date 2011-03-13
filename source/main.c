#include <stdio.h>
#include "remoteprint.h"

int main(int argc, const char* argv[])
{
	int s;
	int ret;
	ret = remotePrintConnect(&s);
	if (ret < 0)
		goto end;
	
	remotePrint(s, "\n\nHello World! i am %s\n\n", "nicogrx");
	remotePrintClose(s);

end:
	return ret;
}
