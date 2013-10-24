#ifndef USRTABLE_H
#define USRTABLE_H

#include <sys/stat.h>
#include <dirent.h>

#define INIT 5381
#define MAXLEN 255
#define MAXSYMNO 40
#define MAXPATHLEN 4096
#define BUCKET 3000
#define P_SYM_NOFOLLOW 0001
#define O_SEARCH (04000000)
#define ERACE 160

typedef struct trace_credentials {
  uid_t uid;
  int ngids;
  gid_t gids[NGROUPS_MAX+2];
} trace_credentials;

typedef struct _record{
	ino_t p_ino;
	ino_t atom_ino;
	int visited;
	struct _record* next;
	char atom_name[0];
}record;

typedef struct _dent{
	int dir_no;
	record* folder_contents;
	struct _dent* next;
}dent;

typedef struct _entry{
	ino_t p_ino;
	ino_t atom_ino;
//	int tmpnentries;
	int nentries;
	int readdir_bit;
//	mode_t atom_st;
	struct stat atom_st;
	char* target;
	dent* opened_folders;
	struct _entry* next;
	struct _entry* next_dir_item;
	char atom_name[0];
}entry;

typedef struct _pathret{
	int is_created;
	int last_dirfd;
	int last_atomfd;
	char last_atom_name[MAXLEN+1];
    struct stat last_dirst;
	struct stat last_atomst;
}pathret;

void init_table();
void print_chain(int bucket);
void print_table();
void print_folder(entry* dot_ptr);
void print_folder_contents(dent* folder);
void free_entry_chain(entry* ptr);
void free_dent_chain(dent* ptr);
void free_record_chain(record* ptr);
void destroy_opened_folder(entry* parent_dir, int dir_no);
void destroy_table();
int entry_hash(ino_t p_ino, char* atom);
int update_dir_counter(int dirfd);
entry* lookup(int bucket, entry* item, int* conf_flag);
int local_check(struct stat* parent_stat, int* atom_fd, int openerrno, int staterr, char* atom_name, struct stat* child_stat, int openflag, int mode, char* target);
int add_entry(struct stat* parent_stat, const char* atom_name, struct stat* child_stat, char* target, int force_add);
int delete_entry(struct stat* parent_st, entry* item, char* target);
void assign_entry(entry* item, ino_t p_ino, ino_t atom_ino, struct stat* atom_st, char* target, const char* atom_name);
dent* alloc_dent(entry* parent_dir, DIR* dir);
int add_record(entry* ptr, dent* folder);
int copy_fcontents(entry* parent_dir, dent* folder);
int dne_in_hashtable(entry* parent_dir, record* cur_record);
record* find_record_head(entry* parent_dir, int dir_no);
int check_record(entry* parent_dir, int dir_no);
int mark_record(entry* parent_dir, int dir_no, ino_t p_ino, ino_t atom_ino, char* atom_name);

void trace_credentials_init_effective(trace_credentials *tc);
void trace_credentials_init_real(trace_credentials *tc);
int access_mode_check(struct stat* atom_stat, int mode, trace_credentials *tc);	
int safe_open_child(char* target, int parent_fd, struct stat* parent_stat, char* atom_name, struct stat* child_stat, int openflag, int mode, int* is_created);
entry* find_dot_entry(struct stat* parent_stat, int* conf_flag);
int pathres(pathret* retval, const char* pathname, int* symcount, int flag, int openflag, int mode, trace_credentials *tc);

#endif
