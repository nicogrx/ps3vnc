#include "remoteprint.h"
#include "term.h"

#ifdef VERBOSE

#ifdef REMOTE_PRINT
#define PSPRINT remotePrint
#else
#define PSPRINT PSPrint
#endif

#else
#define PSPRINT (void)
#endif

extern PSTerm psterm;
extern PSFont psfont;

extern int PSPrint(const char * fmt, ...);
