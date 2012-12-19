#include "mbed.h"
#include "config.h"

static char *free_ptr = (char *)AHBMEM;
static int free_sz = AHBMEMSIZE;
void reset_ahb_mem(void)
 {
   free_ptr = (char *)AHBMEM;
   free_sz = AHBMEMSIZE;
 }
void *mad_malloc(unsigned int sz)
{
  unsigned int nsz = ((sz >> 3) + 1) << 3; // align to 8 byte
  if(nsz < free_sz)
  {
    char *p = free_ptr;
	free_ptr += nsz;
	free_sz -=nsz;
	return(p);
  }
  else
  {
    return(malloc(sz));
  }
}
