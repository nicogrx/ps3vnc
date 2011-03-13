extern int remotePrintConnect(int * sockfd);
extern void remotePrintClose(int sockfd);
extern void remotePrint(int sockfd, const char * fmt, ...);

