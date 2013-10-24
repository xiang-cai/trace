#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/statfs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/time.h>
#include <errno.h>
#include "usrtable.h"
#include "trace.h"

#define CLOSEFD(retval) do {if(retval.last_dirfd) close(retval.last_dirfd); if(retval.last_atomfd) close(retval.last_atomfd);} while(0)

int do_pathres(pathret* retval, const char* pathname, int flag, int openflag, int openmode, trace_credentials * tc){
	// return 0 on success, -1 otherwise
	int symlink = 0;
	return pathres(retval, pathname, &symlink, flag, openflag, openmode, tc);
}


int update_stat(pathret retval, int flags){
	// update stat information for the table entry
	int x, bucket, conf_flag;
	struct stat buf;
	entry* item = NULL;
	entry* ptr = NULL;
	char target[MAXPATHLEN+1];
    memset(target, '\0', MAXPATHLEN+1);

    if(!(flags & AT_SYMLINK_NOFOLLOW) && retval.last_atomfd >= 0){	
		if(-1 == fstat(retval.last_atomfd, &buf))
			return -1;
	}
	else if(retval.last_dirfd >= 0){
		if(-1 == fstatat(retval.last_dirfd, retval.last_atom_name, &buf, flags))
			return -1;
	}
	else
		return -1;

	if(S_ISLNK(retval.last_atomst.st_mode)){ // last atom is a symlink
			x = readlinkat(retval.last_dirfd, retval.last_atom_name, target, MAXPATHLEN+1);
			target[x] = '\0';
	}

	item = (entry*)malloc(sizeof(entry) + strlen(retval.last_atom_name) + 1);
	assign_entry(item, retval.last_dirst.st_ino, retval.last_atomst.st_ino, &(retval.last_atomst), target, retval.last_atom_name);
	
	bucket = entry_hash(item->p_ino, item->atom_name);
	if(bucket == -1){
		printf("hash error!\n");
		goto update_fail;
	}
	ptr = lookup(bucket, item, &conf_flag);
	if(!ptr || conf_flag == 1)
		goto update_fail;

	memcpy(&(ptr->atom_st), &buf, sizeof(struct stat));
	
	free(item);
	return 0;

update_fail:
	free(item);
	return -1;
}

int add_hash_entry(struct stat* parent_stat, const char* atom_name, struct stat* child_stat, char* target, int force_add){
	return add_entry(parent_stat, atom_name, child_stat, target, force_add);
}


int delete_hash_entry(struct stat* parent_st, ino_t atom_ino, struct stat* atom_st, char* target, const char* atom_name){
	int ret;
	entry* tmp = (entry*)malloc(sizeof(entry) + strlen(atom_name) + 1);
	assign_entry(tmp, parent_st->st_ino, atom_ino, atom_st, target, atom_name);
	ret = delete_entry(parent_st, tmp, target);
	free(tmp);
	return ret;
}


void trace_txn_begin(){
	init_table();
}

void trace_txn_end(){
	destroy_table();
}

int do_open(int dirfd, const char* pathname, int flags, mode_t mode){
	// mode is ignored if O_CREAT is NOT set
	pathret retval;
	int ret = -1;
	retval.last_dirfd = retval.last_atomfd = dirfd;
  
	if(flags & O_NOFOLLOW){
		ret = do_pathres(&retval, pathname, P_SYM_NOFOLLOW, flags, mode, NULL);
		if(S_ISLNK(retval.last_atomst.st_mode)){
			CLOSEFD(retval);
			return -1;
		}
	}
	else
		ret = do_pathres(&retval, pathname, 0, flags, mode, NULL);
	
	if(ret == 0){
		if(retval.last_dirfd)
			close(retval.last_dirfd);
		return retval.last_atomfd;
	}

	CLOSEFD(retval);
	return -1;
}

int trace_creat(const char *pathname, mode_t mode){
	return do_open(-1, pathname, O_CREAT|O_WRONLY|O_TRUNC, mode);
}

int trace_open(const char *pathname, int flags, ...){
	va_list vl;
	mode_t mode = 0;

	va_start(vl, flags);
	if(flags & O_CREAT)
		mode = va_arg(vl, mode_t);
	va_end(vl);
	return do_open(-1, pathname, flags, mode);
}

int trace_openat(int dirfd, const char *pathname, int flags, ...){
	va_list vl;
	mode_t mode = 0;

	va_start(vl, flags);
	if(flags & O_CREAT)
		mode = va_arg(vl, mode_t);
	va_end(vl);
	return do_open(dirfd, pathname, flags, mode);
}

int trace_statfs(const char *pathname, struct statfs *buf){
	int x;
	pathret retval;
	retval.last_dirfd = retval.last_atomfd = -1;
	
	x = do_pathres(&retval, pathname, 0, O_SEARCH, 0, NULL);
	if(x == -1 && retval.last_atomfd == -1){
		CLOSEFD(retval);
		return -1;
	}

	x = fstatfs(retval.last_atomfd, buf);
    CLOSEFD(retval);
	return x;
}


int do_stat(int ver, int dirfd, const char* pathname, struct stat* buf, int flags){
	int x;
	pathret retval;
	retval.last_dirfd = retval.last_atomfd = dirfd;

	x = do_pathres(&retval, pathname, (flags & AT_SYMLINK_NOFOLLOW) ? P_SYM_NOFOLLOW : 0, O_SEARCH, 0, NULL);
	if(x == -1 && retval.last_atomst.st_ino == 0){
		CLOSEFD(retval);
		return -1;
	}
	
	memcpy(buf, &(retval.last_atomst), sizeof(struct stat));
    CLOSEFD(retval);
	return 0;
}

int trace_stat(const char *pathname, struct stat* buf){
	return do_stat(-1, -1, pathname, buf, 0);
}

int trace__xstat(int ver, const char *pathname, struct stat* buf){
	return do_stat(ver, -1, pathname, buf, 0);
}

int trace_lstat(const char *pathname, struct stat* buf){
	return do_stat(-1, -1, pathname, buf, 0);
}

int trace__lxstat(int ver, const char *pathname, struct stat* buf){
	return do_stat(ver, -1, pathname, buf, 0);
}

int trace_fstatat(int dirfd, const char *pathname, struct stat* buf, int flags){
	return do_stat(-1, dirfd, pathname, buf, flags);
}

int trace__fxstatat(int ver, int dirfd, const char *pathname, struct stat* buf, int flags){
	return do_stat(ver, dirfd, pathname, buf, flags);
}


int do_link(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags){
	int x;
	char target[MAXPATHLEN+1];
	pathret newret, oldret;
	newret.last_dirfd = newdirfd;
	oldret.last_dirfd = olddirfd;
	newret.last_atomfd = oldret.last_atomfd = -1;
    memset(target, '\0', MAXPATHLEN+1);
	if (0 == do_pathres(&newret, newpath, P_SYM_NOFOLLOW, O_SEARCH, 0, NULL)){
		CLOSEFD(newret);
		return -1;	// newpath should not exist
	}
	
	if(-1 == do_pathres(&oldret, oldpath, (flags & AT_SYMLINK_FOLLOW) ? 0 : P_SYM_NOFOLLOW, O_SEARCH, 0, NULL)){
		CLOSEFD(oldret);
		CLOSEFD(newret);
		return -1;
	}
	if(S_ISLNK(oldret.last_atomst.st_mode)){ // last atom is a symlink
			x = readlinkat(oldret.last_dirfd, oldret.last_atom_name, target, MAXPATHLEN+1);
			target[x] = '\0';
	}
	
	if(-1 == linkat(oldret.last_dirfd, oldret.last_atom_name, newret.last_dirfd, newret.last_atom_name, flags)){
		CLOSEFD(oldret);
		CLOSEFD(newret);
		return -1;
	}
	
	
	CLOSEFD(oldret);
	//delete a dangling entry and then insert a valid one
	if(-1 == delete_hash_entry(&(newret.last_dirst), 0, NULL, target, newret.last_atom_name)){
		printf("RACE DETECTED!\n");
		return -1;
	}
	if(-1 == add_hash_entry(&(newret.last_dirst),newret.last_atom_name, &(oldret.last_atomst), target, 1)){
		printf("RACE DETECTED!\n");
		return -1;
	}
	
	x = do_pathres(&newret, newret.last_atom_name, 0, O_SEARCH, 0, NULL);
	CLOSEFD(newret);
	if(x < 0)
		printf("RACE DETECTED!\n");
	return x;
}


int do_unlink(int dirfd, const char *pathname, int flags){
	int x, dircounter;
	pathret retval;
	char target[MAXPATHLEN+1];
	retval.last_dirfd = dirfd;
	retval.last_atomfd = -1;
    memset(target, '\0', MAXPATHLEN+1);
	
	if(-1 == do_pathres(&retval, pathname, P_SYM_NOFOLLOW, O_SEARCH, 0, NULL)){
		CLOSEFD(retval);
		return -1;
	}
	if(S_ISLNK(retval.last_atomst.st_mode)){ // last atom is a symlink
		x = readlinkat(retval.last_dirfd, retval.last_atom_name, target, MAXPATHLEN+1);
		target[x] = '\0';
	}
	if(flags & AT_REMOVEDIR)
		dircounter = update_dir_counter(retval.last_atomfd);
    
	errno = 0;
	if(-1 == unlinkat(retval.last_dirfd, retval.last_atom_name, flags)){
		if(errno == ENOTEMPTY && dircounter <= 2)
			printf("DIR COUNTER INCONSISTENT! RACE DETECTED!\n");
		CLOSEFD(retval);
		return -1;
	}

	CLOSEFD(retval);
	// delete the entry in hashtable
	if(-1 == delete_hash_entry(&(retval.last_dirst), retval.last_atomst.st_ino, &(retval.last_atomst), target, retval.last_atom_name))
		goto unlink_fail;
	if(flags & AT_REMOVEDIR){
		if(-1 == delete_hash_entry(&(retval.last_atomst), retval.last_dirst.st_ino, &(retval.last_dirst), target, ".."))
			goto unlink_fail;
		if(-1 == delete_hash_entry(&(retval.last_atomst), retval.last_atomst.st_ino, &(retval.last_atomst), target, "."))
			goto unlink_fail;
	}
	return 0;

unlink_fail:		
	printf("RACE DETECTED!\n");
	return -1;
}

int trace_link(const char *oldpath, const char *newpath){
	return do_link(-1, oldpath, -1, newpath, 0);
}

int trace_linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags){
	return do_link(olddirfd, oldpath, newdirfd, newpath, flags);
}

int trace_unlink(const char *pathname){
	return do_unlink(-1, pathname, 0);
}	

int trace_unlinkat(int dirfd, const char *pathname, int flags){
	return do_unlink(dirfd, pathname, flags);
}

int do_rename(int olddirfd, const char *oldpath, int newdirfd, const char *newpath){
	int x;
	int newpath_exist = 0;
	char oldtarget[MAXPATHLEN+1];
	char newtarget[MAXPATHLEN+1];
	pathret newret, oldret;
	newret.last_dirfd = newdirfd;
	oldret.last_dirfd = olddirfd;
	newret.last_atomfd = oldret.last_atomfd = -1;
    memset(oldtarget, '\0', MAXPATHLEN+1);
    memset(newtarget, '\0', MAXPATHLEN+1);

	if (0 == do_pathres(&newret, newpath, P_SYM_NOFOLLOW, O_SEARCH, 0, NULL))
		newpath_exist = 1;	// newpath exist, if it is a DIR, should be an empty one
	if(-1 == do_pathres(&oldret, oldpath, P_SYM_NOFOLLOW, O_SEARCH, 0, NULL)){
		CLOSEFD(newret);
		CLOSEFD(oldret);
		return -1;
	}
	if(newpath_exist && S_ISLNK(newret.last_atomst.st_mode)){ // last atom is a symlink
			x = readlinkat(newret.last_dirfd, newret.last_atom_name, newtarget, MAXPATHLEN+1);
			newtarget[x] = '\0';
	}
	if(S_ISLNK(oldret.last_atomst.st_mode)){ // last atom is a symlink
			x = readlinkat(oldret.last_dirfd, oldret.last_atom_name, oldtarget, MAXPATHLEN+1);
			oldtarget[x] = '\0';
	}

	if(-1 == renameat(oldret.last_dirfd, oldret.last_atom_name, newret.last_dirfd, newret.last_atom_name)){
		CLOSEFD(newret);
		CLOSEFD(oldret);
		return -1;
	}

	if(newpath_exist){	// the ino of the original last component of newpath changed, need to update hashtable
		//delete the entry of the newpath
		if(-1 == delete_hash_entry(&(newret.last_dirst), newret.last_atomst.st_ino, &(newret.last_atomst), newtarget, newret.last_atom_name))
			goto rename_fail;
		if(S_ISDIR(newret.last_atomst.st_mode)){
			//delete 2 more entries if newpath is a DIR, which should be an empty one
			if(-1 == delete_hash_entry(&(newret.last_atomst), newret.last_dirst.st_ino, &(newret.last_dirst), newtarget, ".."))
				goto rename_fail;
			if(-1 == delete_hash_entry(&(newret.last_atomst), newret.last_atomst.st_ino, &(newret.last_atomst), newtarget, "."))
				goto rename_fail;
		}
	}
	else{	// delete the dangling one
		if(-1 == delete_hash_entry(&(newret.last_dirst), 0, NULL, newtarget, newret.last_atom_name))
			goto rename_fail;
	}

	// delete the entry of the old path
	if(-1 == delete_hash_entry(&(oldret.last_dirst), oldret.last_atomst.st_ino, &(oldret.last_atomst), oldtarget, oldret.last_atom_name))
		goto rename_fail;
	if(-1 == add_hash_entry(&(newret.last_dirst),newret.last_atom_name, &(oldret.last_atomst), oldtarget, 1))
		goto rename_fail;

	if(S_ISDIR(oldret.last_atomst.st_mode)){
		//rename is done between 2 DIRs. Need to delete 2 more entries of old dir, and add 2 more entries for it
		if(-1 == delete_hash_entry(&(oldret.last_atomst), oldret.last_dirst.st_ino, &(oldret.last_dirst), oldtarget, ".."))
			goto rename_fail;
		if(-1 == add_hash_entry(&(oldret.last_atomst),"..", &(newret.last_dirst), oldtarget, 0))
			goto rename_fail;
	}

	newret.last_atomfd = -1;
	x = do_pathres(&newret, newret.last_atom_name, 0, O_SEARCH, 0, NULL);
	CLOSEFD(newret);
	if(x >= 0)
		return x;

rename_fail:
	printf("RACE DETECTED!\n");
	return -1;
}

int trace_rename(const char *oldpath, const char *newpath){
	return do_rename(-1, oldpath, -1, newpath);
}


int trace_renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath){
	return do_rename(olddirfd, oldpath, newdirfd, newpath);

}


int trace_rmdir(const char *pathname){
	return do_unlink(-1, pathname, AT_REMOVEDIR);
}

int do_mkdir(int dirfd, const char* pathname, mode_t mode){
	int i = 0;
	DIR* dir = NULL;
	struct dirent* dentry = NULL;
	pathret retval;
	retval.last_dirfd = dirfd;
	retval.last_atomfd = -1;
	
	if(0 == do_pathres(&retval, pathname, P_SYM_NOFOLLOW, O_SEARCH, 0, NULL)){
		CLOSEFD(retval);
		return -1;	// pathname should not exist
	}
	if(retval.last_atomst.st_ino && S_ISLNK(retval.last_atomst.st_mode)){
		CLOSEFD(retval);
		return -1; // last component is a symlink
	}
    if(-1 == mkdirat(retval.last_dirfd, retval.last_atom_name, mode)){
		CLOSEFD(retval);
		return -1;
	}
    
	if(-1 == (retval.last_atomfd = openat(retval.last_dirfd, retval.last_atom_name, O_SEARCH|O_DIRECTORY|O_NOFOLLOW))){
		CLOSEFD(retval);
		return -1;
	}
	if(-1 == fstat(retval.last_atomfd, &(retval.last_atomst))){
		CLOSEFD(retval);
		return -1;
	}

    // check whether the new dir is an emtpy one
	dir = fdopendir(retval.last_atomfd);
	if(!dir)
		return -1;
	while((dentry = readdir(dir)) != NULL)
		i++;
	closedir(dir);
	if(i != 2){
		printf("mkdir creates a non-empty dir! RACE DETECTED\n");
		CLOSEFD(retval);
		return -1;
	}
	
	CLOSEFD(retval);
	//delete dangling entry for dir
	if(-1 == delete_hash_entry(&(retval.last_dirst), 0, NULL, NULL, retval.last_atom_name))
		goto mkdir_fail;
	if(-1 == add_hash_entry(&(retval.last_dirst),retval.last_atom_name, &(retval.last_atomst), NULL, 1))
		goto mkdir_fail;
	if(-1 == add_hash_entry(&(retval.last_atomst),".", &(retval.last_atomst), NULL, 0))
		goto mkdir_fail;
	if(-1 == add_hash_entry(&(retval.last_atomst),"..", &(retval.last_dirst), NULL, 0))
		goto mkdir_fail;

	return 0;
mkdir_fail:
	printf("RACE DETECTED!\n");
	return -1;
}


int trace_mkdir(const char *pathname, mode_t mode){
	return do_mkdir(-1, pathname, mode);
}

int trace_mkdirat(int dirfd, const char* pathname, mode_t mode){
	return do_mkdir(dirfd, pathname, mode);
}


int do_mknod(int dirfd, const char* pathname, mode_t mode, dev_t dev){
	int i = 0;
	DIR* dir = NULL;
	struct dirent* dentry = NULL;
	pathret retval;
	retval.last_dirfd = dirfd;
	retval.last_atomfd = -1;
	
	if(0 == do_pathres(&retval, pathname, P_SYM_NOFOLLOW, O_SEARCH, 0, NULL)){
		CLOSEFD(retval);
		return -1;	// pathname should not exist
	}
    if(-1 == mknodat(retval.last_dirfd, retval.last_atom_name, mode, dev)){
		CLOSEFD(retval);
		return -1;
	}
    
	if(-1 == fstatat(retval.last_dirfd, retval.last_atom_name, &(retval.last_atomst), AT_SYMLINK_NOFOLLOW)){
		CLOSEFD(retval);
		return -1;
	}
	//delete dangling entry for new nod
	if(-1 == delete_hash_entry(&(retval.last_dirst), 0, NULL, NULL, retval.last_atom_name))
		goto mknod_fail;
	if(-1 == add_hash_entry(&(retval.last_dirst),retval.last_atom_name, &(retval.last_atomst), NULL, 1))
		goto mknod_fail;
	
	if(mode & S_IFDIR){
		if(-1 == (retval.last_atomfd = openat(retval.last_dirfd, retval.last_atom_name, O_SEARCH|O_DIRECTORY|O_NOFOLLOW))){
			CLOSEFD(retval);
			return -1;
		}
		// check whether the new dir is an emtpy one
		dir = fdopendir(retval.last_atomfd);
		if(!dir)
			return -1;
		while((dentry = readdir(dir)) != NULL)
			i++;
		closedir(dir);
		if(i != 2){
			printf("mkdir creates a non-empty dir! RACE DETECTED!\n");
			CLOSEFD(retval);
			return -1;
		}
		if(-1 == add_hash_entry(&(retval.last_atomst),".", &(retval.last_atomst), NULL, 0))
			goto mknod_fail;
		if(-1 == add_hash_entry(&(retval.last_atomst),"..", &(retval.last_dirst), NULL, 0))
			goto mknod_fail;
	}

	CLOSEFD(retval);
	return 0;
mknod_fail:
	printf("RACE DETECTED!\n");
	CLOSEFD(retval);
	return -1;
}


int trace_mknod(const char *pathname, mode_t mode, dev_t dev){
	return do_mknod(-1, pathname, mode, dev);
}

int trace_mknodat(int dirfd, const char* pathname, mode_t mode, dev_t dev){
	return do_mknod(dirfd, pathname, mode, dev);
}


int do_symlink(const char *oldpath, int newdirfd, const char *newpath){
	char target[MAXPATHLEN+1];
	pathret newret;
	newret.last_dirfd = newdirfd;
	newret.last_atomfd = -1;
    memset(target, '\0', MAXPATHLEN+1);
	if (0 == do_pathres(&newret, newpath, P_SYM_NOFOLLOW, O_SEARCH, 0, NULL)){
		CLOSEFD(newret);
		return -1;	// newpath should not exist
	}
	
	strcpy(target, oldpath);

	if(-1 == symlinkat(oldpath, newret.last_dirfd, newret.last_atom_name)){
		CLOSEFD(newret);
		return -1;
	}
	
	//delete a dangling entry and then insert a valid one
	if(-1 == delete_hash_entry(&(newret.last_dirst), 0, NULL, NULL, newret.last_atom_name))
		goto sym_fail;
	if(-1 == fstatat(newret.last_dirfd, newret.last_atom_name, &(newret.last_atomst), AT_SYMLINK_NOFOLLOW)){
		CLOSEFD(newret);
		return -1;
	}

	//the above operation should add an symlink entry to the hashtable. We now manually add one, which should not cause a conflict
	if(-1 == add_hash_entry(&(newret.last_dirst),newret.last_atom_name, &(newret.last_atomst), target, 1))
		goto sym_fail;	
	CLOSEFD(newret);
	return 0;
sym_fail:
	printf("RACE DETECTED!\n");
	CLOSEFD(newret);
	return -1;
}


int trace_symlink(const char *oldpath, const char *newpath){
	return do_symlink(oldpath, -1, newpath);
}

int trace_symlinkat(const char *oldpath, int newdirfd, const char *newpath){
	return do_symlink(oldpath, newdirfd, newpath);
}


void trace_rewinddir(DIR* dir){
	int parent_fd, conf_flag;
	int dir_no = (int)dir;
	struct stat buf;
	entry* ptr = NULL;
	dent* folder;

	rewinddir(dir);
	if(-1 == (parent_fd = dirfd(dir)))
		return;
	if(-1 == fstat(parent_fd, &buf))
		return;
	ptr = find_dot_entry(&buf, &conf_flag);
	if(ptr == NULL || conf_flag == 1)
		return;
	
	folder = ptr->opened_folders;
	while(folder){
		if(folder->dir_no == dir_no){
			break;
		}
		else
			folder = folder->next;
	}
	if(!folder)
		return;
	free_record_chain(folder->folder_contents);
	folder->folder_contents = NULL;
	// copy all file information inside ptr to folder
	if(-1 == copy_fcontents(ptr, folder))
		return;
}


DIR* trace_opendir(const char* name){
	DIR* dir;
	int conf_flag;
	struct stat buf;
	entry* ptr = NULL;
	dent* folder = NULL;
	pathret retval;
	retval.last_dirfd = retval.last_atomfd = -1;
	
	if(-1 == do_pathres(&retval, name, 0, O_SEARCH|O_DIRECTORY, 0, NULL)){
		CLOSEFD(retval);
		return NULL;	// name should exist
	}
	dir = fdopendir(retval.last_atomfd);
	if(!dir){
		CLOSEFD(retval);
		return NULL;
	}
	if(-1 == fstat(retval.last_atomfd, &buf)){
		CLOSEFD(retval);
		return NULL;
	}
	CLOSEFD(retval);
	
	ptr = find_dot_entry(&buf, &conf_flag);
	if(ptr == NULL || conf_flag == 1)
		return NULL;
	// alloc a new dent struct and attach it to dot_entry
	folder = alloc_dent(ptr, dir);
	if(!folder)
		return NULL;
	// copy all file information inside ptr to folder
	if(-1 == copy_fcontents(ptr, folder))
		return NULL;
	ptr->readdir_bit++;
	return dir;
}


struct dirent* trace_readdir(DIR* dir){
	//return dirent; add an entry to hash table; increase dir's nentries by 1
	struct dirent* dentry = NULL;
	int parent_fd, conf_flag, x;
	char target[MAXPATHLEN+1];
	struct stat parent_stat;
	struct stat child_stat;
	entry* ptr = NULL;
	
	memset(target, '\0', MAXPATHLEN+1);
	if(-1 == (parent_fd = dirfd(dir)))
		return NULL;
	if(-1 == fstat(parent_fd, &parent_stat))
		return NULL;
	if(!S_ISDIR(parent_stat.st_mode))	//it is not a directory
		return NULL;
	//first, add an entry for current dir: "."
	if (-1 == add_entry(&parent_stat, "." , &parent_stat, NULL, 0))
		return NULL;

	//find the entry "." in the hash table
	ptr = find_dot_entry(&parent_stat, &conf_flag);
	if(ptr == NULL || conf_flag == 1)
		return NULL;

	// add an entry for a name inside dir
	errno = 0;
	dentry = readdir(dir); 
	if(dentry == NULL){
		if(errno == 0){
			if(-1 == check_record(ptr, (int)dir)){
				printf("readdir gives inconsistent result!\n");
				return NULL;
			}
		}
		return dentry;
	}
	if(-1 == fstatat(parent_fd, dentry->d_name, &child_stat, AT_SYMLINK_NOFOLLOW))
		return NULL;
	if(S_ISLNK(child_stat.st_mode)){ // this entry is a symlink
			x = readlinkat(parent_fd, dentry->d_name, target, MAXPATHLEN+1);
			target[x] = '\0';
	}
	
	if(-1 == mark_record(ptr, (int)dir, parent_stat.st_ino, child_stat.st_ino, dentry->d_name)){
		if(-1 == add_entry(&parent_stat, dentry->d_name, &child_stat, target, 0))
			return NULL;
		return trace_readdir(dir);
	}
	return dentry;
}


int trace_closedir(DIR* dir){
	int parent_fd, conf_flag;
	struct stat parent_stat;
	entry* ptr = NULL;

	if(-1 == (parent_fd = dirfd(dir)))
		return -1;
	if(-1 == fstat(parent_fd, &parent_stat))
		return -1;
	if(!S_ISDIR(parent_stat.st_mode))	//it is not a directory
		return -1;
	
	ptr = find_dot_entry(&parent_stat, &conf_flag);
	if(ptr == NULL || conf_flag == 1)
		return -1;

	if(-1 == closedir(dir))
		return -1;
	if(ptr->readdir_bit <= 0)
		return -1;
	
	ptr->readdir_bit--;
	destroy_opened_folder(ptr, (int)dir);
	return 0;
}


int trace_truncate(const char *pathname, off_t length){
	int x;
	pathret retval;
	retval.last_dirfd = retval.last_atomfd = -1;

	if(-1 == do_pathres(&retval, pathname, 0, O_WRONLY, 0, NULL)){
		CLOSEFD(retval);
		return -1;	// pathname should exist
	}
    if(-1 == ftruncate(retval.last_atomfd, length)){
		CLOSEFD(retval);
		return -1;
	}

	x = update_stat(retval, 0);
	CLOSEFD(retval);
	return x;
}

int do_access(int dirfd, const char* pathname, int mode, int flags){
	int ret;
	int p_flag = 0;
	pathret retval;
	retval.last_dirfd = retval.last_atomfd = dirfd;
	trace_credentials tc;
	
	if (flags & AT_EACCESS)
		trace_credentials_init_effective(&tc);
	else
		trace_credentials_init_real(&tc);

	if (flags & AT_SYMLINK_NOFOLLOW)
		p_flag |= P_SYM_NOFOLLOW;

	ret = do_pathres(&retval, pathname, p_flag, O_SEARCH, 0, &tc);
	CLOSEFD(retval);
	
	if(ret == 0){
		if(access_mode_check(&(retval.last_atomst), mode, &tc) < 0){
			errno = EACCES;
			ret = -1;
		}
	}
	else if(retval.last_atomfd == -3)
		errno = EACCES;
	
	return ret;
}

int trace_access(const char* pathname, int mode){
	return do_access(-1, pathname, mode, 0);
}


int faccessat(int dirfd, const char *pathname, int mode, int flags){
	return do_access(dirfd, pathname, mode, flags);
}


int trace_chdir(const char *pathname){
	int x;
	pathret retval;
	retval.last_dirfd = retval.last_atomfd = -1;
	
	if(-1 == do_pathres(&retval, pathname, 0, O_SEARCH, 0, NULL)){
		CLOSEFD(retval);
		return -1;	// pathname should exist
	}

	x = fchdir(retval.last_atomfd);
	CLOSEFD(retval);
	return x;
}

int do_chown(int dirfd, const char *pathname, uid_t owner, gid_t group, int flags){
	int x;
	pathret retval;
	retval.last_dirfd = dirfd;
	retval.last_atomfd = -1;
		
	if(-1 == do_pathres(&retval, pathname, (flags & AT_SYMLINK_NOFOLLOW) ? P_SYM_NOFOLLOW : 0, O_SEARCH, 0, NULL)){
		CLOSEFD(retval);
		return -1;	// pathname should exist
	}
	
	if(-1 == fchown(retval.last_atomfd, owner, group)){
		CLOSEFD(retval);
		return -1;
	}

	x = update_stat(retval, flags);
	CLOSEFD(retval);
	return x;
}

int trace_chown(const char *pathname, uid_t owner, gid_t group){
	return do_chown(-1, pathname, owner, group, 0);
}

int trace_lchown(const char *pathname, uid_t owner, gid_t group){
	return do_chown(-1, pathname, owner, group, AT_SYMLINK_NOFOLLOW);
}

int trace_fchownat(int dirfd, const char *pathname, uid_t owner, gid_t group, int flags){
	return do_chown(dirfd, pathname, owner, group, flags);
}

int do_chmod(int dirfd, const char *pathname, mode_t mode, int flags){
	int x;
	pathret retval;
	retval.last_dirfd = dirfd;
	retval.last_atomfd = -1;
	int p_flag = (flags & AT_SYMLINK_NOFOLLOW) ? P_SYM_NOFOLLOW : 0;

	if(-1 == do_pathres(&retval, pathname, p_flag, O_SEARCH, 0, NULL)){
		CLOSEFD(retval);
		return -1;
	}

	if(retval.last_dirfd == -1)
		return -1;
	if(retval.last_atomfd > 0){
		if(-1 == fchmod(retval.last_atomfd, mode)){
			CLOSEFD(retval);
			return -1;
		}
	}
	else{
		if(-1 == fchmodat(retval.last_dirfd, retval.last_atom_name, 0400, flags)){
			CLOSEFD(retval);
			return -1;
		}
		
		if(-1 == do_pathres(&retval, retval.last_atom_name, p_flag, O_SEARCH, 0, NULL)){
			CLOSEFD(retval);
			return -1;
		}
		if(retval.last_atomfd == -1){
			CLOSEFD(retval);
			return -1;
		}
		if(-1 == fchmod(retval.last_atomfd, mode)){
			CLOSEFD(retval);
			return -1;
		}
	}
	x = update_stat(retval, flags);
	CLOSEFD(retval);
	return x;
}

int trace_chmod(const char *pathname, mode_t mode){
	return do_chmod(-1, pathname, mode, 0);
}

int trace_fchmodat(int dirfd, const char *pathname, mode_t mode, int flags){
	return do_chmod(dirfd, pathname, mode, flags);
}


int trace_chroot(const char *pathname){
	struct stat c_stat;
	int cfd, bucket, conf_flag;
	ino_t p_ino, c_ino;
	pathret retval;
	entry *item, *new_root;
	retval.last_dirfd = retval.last_atomfd = -1;
	item = new_root = NULL;

	cfd = open(".", O_RDONLY);
	if(cfd < 0)
		return -1;
	
	if(-1 == do_pathres(&retval, pathname, 0, O_SEARCH, 0, NULL)){
		CLOSEFD(retval);
		return -1;	// pathname should exist
	}
	p_ino = retval.last_dirst.st_ino;
	c_ino = retval.last_atomst.st_ino;
	memcpy(&c_stat, &(retval.last_atomst), sizeof(struct stat));

	trace_stat("/", &(retval.last_atomst));
	if (fchdir(retval.last_atomfd) < 0){
		CLOSEFD(retval);
		return -1;
	}
	CLOSEFD(retval);
	if(-1 == chroot("."))
		return -1;

	/* make some changes to hashtable*/
	//delete old root
    if(delete_hash_entry(&(retval.last_atomst), retval.last_atomst.st_ino, &(retval.last_atomst), NULL, "/") < 0)
		return -1;
	if(delete_hash_entry(&(retval.last_atomst), retval.last_atomst.st_ino, &(retval.last_atomst), NULL, "..") < 0)
		return -1;
	
	trace_stat("/", &(retval.last_atomst));
	if(add_hash_entry(&(retval.last_atomst), "/", &(retval.last_atomst), NULL, 0) < 0)
		return -1;

	item = (entry*)malloc(sizeof(entry) + strlen("..") + 1);
	retval.last_atomst.st_ino = c_ino;
	retval.last_dirst.st_ino = p_ino;
	assign_entry(item, retval.last_atomst.st_ino, retval.last_dirst.st_ino, &(retval.last_dirst), NULL, "..");
	bucket = entry_hash(item->p_ino, item->atom_name);
	if(bucket == -1){
		printf("hash error!\n");
		goto chroot_fail;
	}
	new_root = lookup(bucket, item, &conf_flag);
	if(!new_root || conf_flag == 1)
		goto chroot_fail;

	new_root->atom_ino = new_root->p_ino;
	memcpy(&(new_root->atom_st),&c_stat, sizeof(struct stat));

	return fchdir(cfd);

chroot_fail:
	free(item);
	return -1;
}

int do_readlink(int dirfd, const char *pathname, char *buf, size_t bufsize){
	int x;
	pathret retval;
	retval.last_dirfd = retval.last_atomfd = dirfd;
	
	if(-1 == do_pathres(&retval, pathname, P_SYM_NOFOLLOW, O_SEARCH, 0, NULL)){
		CLOSEFD(retval);
		return -1;
	}
	x = readlinkat(retval.last_dirfd, retval.last_atom_name, buf, bufsize);
	CLOSEFD(retval);
	return x;
}

ssize_t trace_readlink(const char *pathname, char *buf, size_t bufsize){
	return do_readlink(-1, pathname, buf, bufsize);
}

int trace_readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsize){
	return do_readlink(dirfd, pathname, buf, bufsize);
}

int do_utimes(int dirfd, const char *pathname, const struct timeval times[2], int flags){
	int x;
	pathret retval;
	retval.last_dirfd = retval.last_atomfd = dirfd;
	
	if(flags & AT_SYMLINK_NOFOLLOW){
		if(-1 == do_pathres(&retval, pathname, P_SYM_NOFOLLOW, O_SEARCH, 0, NULL)){
			CLOSEFD(retval);
			return -1;
		}
		if(-1 == futimesat(retval.last_dirfd, retval.last_atom_name, times)){
			CLOSEFD(retval);
			return -1;
		}
	}
	else{
		if(-1 == do_pathres(&retval, pathname, 0, O_SEARCH, 0, NULL)){
			CLOSEFD(retval);
			return -1;
		}
		if(-1 == futimes(retval.last_atomfd, times)){
			CLOSEFD(retval);
			return -1;
		}
	}
	
	x = update_stat(retval, flags);
	CLOSEFD(retval);
	return x;
}


int trace_utimes(const char *pathname, const struct timeval times[2]){
	return do_utimes(-1, pathname, times, 0);
}

int trace_lutimes(const char *pathname, const struct timeval times[2]){
	return do_utimes(-1, pathname, times, AT_SYMLINK_NOFOLLOW);
}

int trace_futimesat(int dirfd, const char *pathname, const struct timeval times[2]){
	return do_utimes(dirfd, pathname, times, 0);
}

int trace_utime(const char *pathname, const struct utimbuf *time){
	struct timeval times[2] = {{.tv_sec = time->actime, .tv_usec = 0}, {.tv_sec = time->modtime, .tv_usec = 0}};
	return do_utimes(-1, pathname, times, 0);
}

int trace_utimensat(int dirfd, const char *pathname,const struct timespec times[2], int flags){
	int x;
	pathret retval;
	retval.last_dirfd = retval.last_atomfd = dirfd;
	
	if(flags & AT_SYMLINK_NOFOLLOW){
		if(-1 == do_pathres(&retval, pathname, P_SYM_NOFOLLOW, O_SEARCH, 0, NULL)){
			CLOSEFD(retval);
			return -1;
		}
		if(-1 == utimensat(retval.last_dirfd, retval.last_atom_name, times, AT_SYMLINK_NOFOLLOW)){
			CLOSEFD(retval);
			return -1;
		}
	}
	else{
		if(-1 == do_pathres(&retval, pathname, 0, O_SEARCH, 0, NULL)){
			CLOSEFD(retval);
			return -1;
		}
		if(-1 == futimens(retval.last_atomfd, times)){
			CLOSEFD(retval);
			return -1;
		}
	}
	x = update_stat(retval, flags);
	CLOSEFD(retval);
	return x;
}

int trace_execve(const char *filename, char *const argv[], char *const envp[]){
	pathret retval;
	retval.last_dirfd = retval.last_atomfd = -1;

	if(-1 == do_pathres(&retval, filename, 0, O_SEARCH, 0, NULL)){
		CLOSEFD(retval);
		return -1;
	}
	trace_txn_end();
	fexecve(retval.last_atomfd, argv, envp);
	perror("trace_execve");
	return -1;
}






