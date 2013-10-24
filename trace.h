#ifndef TRACE_H
#define TRACE_H
#include <dirent.h>
#include <utime.h>
#include <sys/statfs.h>
#include "usrtable.h"

void trace_txn_begin();
void trace_txn_end();
int do_pathres(pathret* retval, const char* pathname, int flag, int openflag, int mode, trace_credentials *tc);
int update_stat(pathret retval, int flags);
int do_open(int dirfd, const char* pathname, int flags, mode_t mode);
int do_stat(int ver, int dirfd, const char* pathname, struct stat* buf, int flags);
int do_link(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags);
int do_unlink(int dirfd, const char *pathname, int flags);
int do_rename(int olddirfd, const char *oldpath, int newdirfd, const char *newpath);
int do_mkdir(int dirfd, const char* pathname, mode_t mode);
int do_mknod(int dirfd, const char* pathname, mode_t mode, dev_t dev);
int do_symlink(const char *oldpath, int newdirfd, const char *newpath);
int do_chown(int dirfd, const char *pathname, uid_t owner, gid_t group, int flags);
int do_chmod(int dirfd, const char *pathname, mode_t mode, int flags);
int do_readlink(int dirfd, const char *pathname, char *buf, size_t bufsize);
int do_utimes(int dirfd, const char *pathname, const struct timeval times[2], int flags);

int trace_creat(const char *pathname, mode_t mode);
int trace_open(const char *pathname, int flags, ...);
int trace_openat(int dirfd, const char *pathname, int flags, ...);
int trace_stat(const char *pathname, struct stat* buf);
int trace__xstat(int ver, const char *pathname, struct stat* buf);
int trace_lstat(const char *pathname, struct stat* buf);
int trace__lxstat(int ver, const char *pathname, struct stat* buf);
int trace_fstatat(int dirfd, const char *pathname, struct stat* buf, int flags);
int trace__fxstatat(int ver, int dirfd, const char *pathname, struct stat* buf, int flags);
int trace_statfs(const char *pathname, struct statfs* buf);
int trace_link(const char *oldpath, const char *newpath);
int trace_linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags);
int trace_unlink(const char *pathname);
int trace_unlinkat(int dirfd, const char *pathname, int flags);
int trace_rename(const char *oldpath, const char *newpath);
int trace_renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath);
int trace_rmdir(const char *pathname);
int trace_mkdir(const char *pathname, mode_t mode);
int trace_mkdirat(int dirfd, const char* pathname, mode_t mode);
int trace_mknod(const char *pathname, mode_t mode, dev_t dev);
int trace_mknodat(int dirfd, const char* pathname, mode_t mode, dev_t dev);
int trace_symlink(const char *oldpath, const char *newpath);
int trace_symlinkat(const char *oldpath, int newdirfd, const char *newpath);
DIR* trace_opendir(const char* name);
struct dirent* trace_readdir(DIR* dir);
void trace_rewinddir(DIR* dir);
int trace_closedir(DIR* dir);
//int trace_listdir(DIR* dir);
int trace_truncate(const char *pathname, off_t length);
int trace_access(const char* pathname, int mode);
int faccessat(int dirfd, const char *pathname, int mode, int flags);
int trace_chdir(const char *pathname);
int trace_chown(const char *pathname, uid_t owner, gid_t group);
int trace_lchown(const char *pathname, uid_t owner, gid_t group);
int trace_fchownat(int dirfd, const char *pathname, uid_t owner, gid_t group, int flags);
int trace_chmod(const char *pathname, mode_t mode);
int trace_fchmodat(int dirfd, const char *pathname, mode_t mode, int flags);
int trace_chroot(const char *pathname);
ssize_t trace_readlink(const char *pathname, char *buf, size_t bufsize);
int trace_readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsize);
int trace_utimes(const char *pathname, const struct timeval times[2]);
int trace_lutimes(const char *pathname, const struct timeval times[2]);
int trace_futimesat(int dirfd, const char *pathname, const struct timeval times[2]);
int trace_utime(const char *pathname, const struct utimbuf *time);
int trace_utimensat(int dirfd, const char *pathname,const struct timespec times[2], int flags);
int trace_execve(const char *filename, char *const argv[], char *const envp[]);

int add_hash_entry(struct stat* parent_stat, const char* atom_name, struct stat* child_stat, char* target, int force_add);
int delete_hash_entry(struct stat* parent_st, ino_t atom_ino, struct stat* atom_st, char* target, const char* atom_name);

#endif
