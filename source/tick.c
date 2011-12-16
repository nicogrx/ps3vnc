#include <sys/time.h>
#include <sys/unistd.h>

static struct timeval start;

void startTicks(void)
{
  /* Set first ticks value */
  gettimeofday(&start, NULL);
}

unsigned int getTicks(void)
{
  static unsigned int saved_ticks;
  unsigned int ticks;
  unsigned int diff;
  struct timeval now;
  gettimeofday(&now, NULL);
  ticks = (now.tv_sec - start.tv_sec) * 1000 + (now.tv_usec - start.tv_usec) / 1000;
	diff = ticks-saved_ticks;
	saved_ticks=ticks;
  return (diff);
}
