/* halt.c

   Simple program to test whether running a user program works.
 	
   Just invokes a system call that shuts down the OS. */

#include <syscall.h>

int 
printf(const char * __format, ...);

int
main (void)
{
	printf("Testing printf");
	halt ();
  /* not reached */
}
