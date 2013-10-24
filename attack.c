#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <sys/statfs.h>
#include "usrtable.h"
#include "trace.h"

int main(int argc, char** argv){
  struct stat buf;
  char cwd[100];
  int fd = 0;
  
  init_table();
  
#if 0
  /* make tmpfile race condition
     an attacker can create the file before the victim does
  */

  fd = -1;
/* BEGIN statattack */
  if(trace_stat(argv[1], &buf) < 0){
    
    /* attacker's operation */
    open(argv[1], O_CREAT, S_IRWXU); 
    
    fd = trace_open(argv[1], O_CREAT, S_IRWXU);
  }
  printf("fd is %d\n\n", fd);
/* END statattack */

	printf("stat attack end\n");
#endif

#if 1
/*	tmp cleaning race condition
	an attack can "mv /tmp/a/b /tmp"
	after chdir(".."), the victim will be at /tmp instead of /tmp/a
 */

/* BEGIN chdirattack */
  if((fd = trace_chdir("/tmp/a/b")) < 0)
    return fd;
  if((fd = trace_unlink("./foo")) < 0)
    return fd;
  
  /* attacker's operation */
  system("mv /tmp/a/b /tmp");  
  
  if((fd = trace_chdir("..")) < 0){
	printf("chdir fail\n");
	return fd;
  }
  printf("cwd is %s\n", getcwd(cwd, 100));
/* END chdirattack */
	
  printf("chdir attack end\n");

#endif
  
  print_table();
  destroy_table();
  return fd;
}
