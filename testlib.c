#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "usrtable.h"
#include "trace.h"

int main(){
	int x;
	struct stat buf;
	trace_txn_begin();
	x = trace_stat("/tmp/x", &buf);
	if(x >= 0)
		printf("success!\n");
	else
		printf("fail!\n");
	trace_txn_end();
	return 0;
}
