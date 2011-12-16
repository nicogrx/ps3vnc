#include "remoteprint.h"
#include "term.h"

#if defined(REMOTE_PRINT)
#define RPRINT remotePrint
#else
#define RPRINT (void)
#endif

#define PSPRINT PSPrint

extern PSTerm psterm;
extern PSFont psfont;

extern int PSPrint(const char * fmt, ...);
