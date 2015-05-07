/* Tries to open a nonexistent file. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
	printf("$$$$$$$$$$$$$$$$$$$$$$$$\n");
	printf("$$$$$$$$$$$$$$$$$$$$$$$$\n");
  int handle = open ("no-such-file");
	printf("$$$$$$$$$$$$$$$$$$$$$$$$\n");
	printf("$$$$$$$$$$$$$$$$$$$$$$$$\n");
  if (handle != -1)
    fail ("open() returned %d", handle);
}
