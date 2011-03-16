#ifdef VERBOSE
#define RPRINT remotePrint
extern int remotePrintConnect(void);
extern void remotePrintClose(void);
extern void remotePrint(const char * fmt, ...);
#else
#define RPRINT (void) 
#endif
