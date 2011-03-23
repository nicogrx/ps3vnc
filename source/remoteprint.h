#ifdef VERBOSE
#define RPRINT remotePrint
//#define RPRINT (void) 
extern int remotePrintConnect(void);
extern void remotePrintClose(void);
extern void remotePrint(const char * fmt, ...);
extern int remoteSendBytes(unsigned char * bytes, int size); 
#else
#define RPRINT (void) 
#endif
