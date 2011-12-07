#include "remoteprint.h"
#include "term.h"

#ifdef VERBOSE
//#define PSPRINT remotePrint
#define PSPRINT PSPrint
#else
#define PSPRINT (void)
#endif

extern PSTerm psterm;
extern PSFont psfont;

extern int PSPrint(const char * fmt, ...);
