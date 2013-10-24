#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <sys/time.h>
#include <sys/statfs.h>
#include "usrtable.h"
#include "trace.h"

int main(int argc, char** argv){
//	char* a[] = {NULL};
//	char* b[] = {NULL};
//	struct stat buf;
//	char target[100] = {'\0'};
//	struct statfs fsbuf;
//	DIR* dir = NULL;
//	struct dirent* dent = NULL;
	trace_txn_begin();

#if 0
	struct timeval times[2] = {{1, 0}, {1, 0}};
	if(-1 == trace_utimes("/tmp/x", times))
		printf("errno is %d\n", errno);
#endif

#if 0
	ssize_t x = trace_readlink("./sym_tmp", target, 100);
	printf("x is %d, target is %s\n", x, target);
#endif

#if 0
	if(-1 == trace_stat("./d1/x", &buf))
		printf("0. errno is %d\n", errno);
	else
		printf("./d1/x ino is %ld\n", buf.st_ino);
	
	if(-1 == trace_stat("/x", &buf))
		printf("1. errno is %d\n", errno);
	else
		printf("/x ino is %ld\n", buf.st_ino);

	if(-1 == trace_chroot("./d1"))
		printf("chroot failed\n");

	if(-1 == trace_stat("/x", &buf))
		printf("2. errno is %d\n", errno);
	else
		printf("/x ino is %ld\n", buf.st_ino);

#endif

#if 0
	if(-1 == trace_stat("./sub", &buf))
		printf("0. errno is %d\n", errno);
	if(-1 == trace_chdir("./dir"))
		printf("1. errno is %d\n", errno);
	if(-1 == trace_stat("./sub", &buf))
		printf("2. errno is %d\n", errno);
#endif

#if 0
	if(-1 == trace_statfs("./dir/x", &fsbuf))
		printf("errno is %d\n", errno);
	else
		printf("ftype is %X\n", fsbuf.f_type);
	if(-1 == statfs("./dir/x", &fsbuf))
		printf("errno is %d\n", errno);
	else
		printf("ftype is %X\n", fsbuf.f_type);
	print_table();
//	if(-1 == trace_stat("./dir/y", &buf))
//		printf("errno is %d\n", errno);
#endif

#if 0
	dir = trace_opendir(argv[1]);
	if(!dir){
		printf("errno is %d\n", errno);
		return -1;
	}
	
	
/*	
	if(-1 == trace_unlink("./dir/x"))
		printf("errno is %d\n", errno);
	if(-1 == trace_unlink("./dir/y"))
		printf("errno is %d\n", errno);
*/
	while(NULL != (dent = trace_readdir(dir))){
		printf("trace_readdir result: %s\n", dent->d_name);
	}
	
	trace_closedir(dir);

	if(-1 == trace_rmdir(argv[1]))
		printf("errno is %d\n", errno);

	print_table();
#endif

#if 0 
	if(-1 == trace_link(argv[1], argv[2]))
		printf("errno is %d\n", errno);
#endif

#if 1
	if(0 > trace_access(argv[1], R_OK))
			printf("errno is %d\n", errno);
	else
		printf("ok\n");
#endif

#if 0
	if(-1 == trace_open(argv[1], O_RDONLY, 0644))
			printf("errno is %d\n", errno);
/*
	if(-1 == trace_open(argv[1], O_CREAT, 0644))
			printf("2. errno is %d\n", errno);
	
	if(strcmp("O_CREAT", argv[2]) == 0){
		if(-1 == trace_open(argv[1], O_CREAT, 0644))
			printf("errno is %d\n", errno);
	}
	else{
		if(-1 == trace_open(argv[1], O_RDONLY, 0644))
			printf("errno is %d\n", errno);
	}
	*/
#endif

#if 0
	if(-1 == trace_symlink(argv[1], argv[2]))
		printf("errno is %d\n", errno);
#endif

#if 0
	if(-1 == trace_unlink(argv[2]))
		printf("errno is %d\n", errno);
#endif


#if 0
	if(-1 == trace_rename(argv[1], argv[2]))
		printf("errno is %d\n", errno);
#endif

#if 0	
	if(-1 == trace_rmdir(argv[1]))
		printf("errno is %d\n", errno);
#endif

#if	0
	if(-1 == trace_mkdir(argv[1], 0777))
		printf("errno is %d\n", errno);
#endif

#if	0

	if(-1 == trace_mknod(argv[1], S_IFDIR|S_IRUSR|S_IWUSR, 0))
		printf("errno is %d\n", errno);
#endif

#if 0
	print_table();
	if(0 > trace_execve("./hello", a, b))
			printf("errno is %d\n", errno);
	else
		printf("ok\n");
#endif

	print_table();
	trace_txn_end();
	print_table();
	return 0;
}
