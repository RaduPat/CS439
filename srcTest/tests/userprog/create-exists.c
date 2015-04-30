/* Verifies that trying to create a file under a name that
   already exists will fail. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  CHECK (create ("quux.dat", 0), "create quux.dat");
  CHECK (create ("warble.dat", 0), "create warble.dat");
  // if(!create ("quux.dat", 0)){
  // 	printf("^^^^^^^^^ file create failed in test file\n");
  // }
  // else{
  // 	printf("^^^^^^^^^ file create succeeded in test file\n");
  // }
  CHECK (!create ("quux.dat", 0), "try to re-create quux.dat");
  CHECK (create ("baffle.dat", 0), "create baffle.dat");
  CHECK (!create ("warble.dat", 0), "try to re-create quux.dat");
}
