
/**
	Program to check correctness of open function under all scenarios of race conditions
	Author : Rucha Lale
*/

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>


int main(int argc, char *argv[])
{
	int fd ;
	struct stat stat_info;

	if(stat(argv[1], &stat_info) < 0){
		printf("Error in stating file \n");
		return 0;
	}
	
	if(!S_ISLNK(stat_info.st_mode)){
		printf("Sleeping for 30 sec\n");
		sleep(30);
		fd = open(argv[1], O_RDONLY);
//		fd = open(argv[1], O_TRUNC);
		printf(" returned fd : %d\n", fd);
		if(fd < 0){
			printf("Error in opening file\n");
		}else
			printf("Open executed successfully\n");	
	}
	return 0;
}

