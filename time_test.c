#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include "usrtable.h"
#include "trace.h"

FILE * logfile;

long long tv2usecs(struct timeval *tv)
{
  return tv->tv_sec * 1000000ULL + tv->tv_usec;
}

long long nowusecs(void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv2usecs (&tv);
}

int main(int argc, char** argv){
//  struct stat buf;
  unsigned long long x1, x2;
  int testrounds=1;
  
  init_table();
  if(-1 == access(argv[1], R_OK))
		printf("errno is %d\n", errno);
  if(-1 == open(argv[1], O_RDONLY))
		printf("errno is %d\n", errno);
  x1=nowusecs();
  for(testrounds=1;testrounds <= 500;testrounds++){
	if(-1 == access(argv[1], R_OK))
		printf("errno is %d\n", errno);
	if(-1 == open(argv[1], O_RDONLY))
		printf("errno is %d\n", errno);
  }
  x2=nowusecs();
//  destroy_table();
  logfile = fopen ("./logfile","a");
  fprintf(logfile,"/********************* Average time is %lf usecs ********************\n",(double)(x2-x1)/500);
  fclose(logfile); 
  
  return 0;
}
