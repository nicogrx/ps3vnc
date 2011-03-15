#include <stdio.h>
#include "remoteprint.h"

int main(int argc, const char* argv[])
{
	int ret;
	ret = remotePrintConnect();
	if (ret < 0)
		goto end;
	
	remotePrint("\n\nHello World! i am %s\n\n", "nicogrx");
	remotePrintClose();

end:
	return ret;
}
