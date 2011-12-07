extern int remotePrintConnect(const char *);
extern void remotePrintClose(void);
extern void remotePrint(const char * fmt, ...);
extern int remoteSendBytes(unsigned char * bytes, int size); 
