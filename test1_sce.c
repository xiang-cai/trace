#define _GNU_SOURCE
#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include "usrtable.h"
#include "trace.h"

int test_readdir(){
        DIR* dir = NULL;
        struct dirent* ent = NULL;        
        if (-1 == trace_mkdir("testFolder", S_IRWXU | S_IRWXG))
        {
                printf("error %d happens in mkdir\n", errno);
                return -1;
        }

        if (trace_open("testFolder/test1", O_CREAT) == -1){
                printf("error %d happens in create test1\n", errno);
                return -1;
        }

        if (trace_open("testFolder/test2", O_CREAT) == -1){
                printf("error %d happens in create test2\n", errno);
                return -1;
        }
        if (-1 == trace_mkdir("testFolder/test3", S_IRWXU | S_IRWXG)){
                printf("error %d happens in mkdir\n", errno);
                return -1;
        }

        dir = trace_opendir("testFolder");
        if (!dir){
                printf("error %d happens in open testFolder directory\n", errno);
                return -1;
        }

        ent = trace_readdir(dir);
        if (!ent){
                printf("error %d happens in list directory testFolder\n", errno);
                return -1;
        }
        if (trace_symlink("testFolder/test4", "test4") == -1)
        {
                printf("error %d happens in creating symlink\n", errno);
                return -1;
        }

        print_table();

        //remove("testFolder/test3");
        //symlink("testFolder/test1", "testFolder/test3");

        //if (trace_listdir(dir) < 0){
        //        printf("pass readdir test!\n");
        //}
        //sleep(60);

        if (trace_open("testFolder/test4", O_RDONLY) < 0)
        {
                printf("pass symlink test!\n");
                return 0;
        }

        print_table();
        printf("don't pass the test!\n");
        return -1;
}

int main(int argc, char** argv){
        init_table();
        test_readdir();
        destroy_table();
        return 0;
}
