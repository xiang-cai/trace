#define _ATFILE_SOURCE
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "usrtable.h"

/*****************************
 * global variables
 ****************************/

entry** htable = NULL;
/********************************
 * hash table related functions
 * *******************************/

void init_table(){
	int i;
	if(htable)
		return;
	htable = (entry**)malloc(BUCKET *sizeof(entry*));
	for(i = 0; i < BUCKET; i++)
		htable[i] = NULL;
}

void print_chain(int bucket){
	entry* ptr = htable[bucket];
	if(ptr == NULL)
		return;
	for(; ptr != NULL; ptr = ptr->next)
		printf("%-8ld   %-25s  %-8ld    %-5d           %-10s\n", ptr->p_ino, ptr->atom_name, ptr->atom_ino, ptr->readdir_bit, ptr->target == NULL ? "null" : ptr->target); 
}

void print_table(){
	int bucket;
	if(!htable)
		return;
	printf("              p_ino      atom_name                  atom_ino    readdir_bit    target\n");
	for(bucket = 0; bucket < BUCKET; bucket++){
		if(htable[bucket] != NULL){
			printf("bucket%5d   ", bucket);
			print_chain(bucket);
		}
	}
	printf("\n");
}

void print_folder(entry* dot_ptr){
	entry* ptr = dot_ptr;
	while(ptr){
		printf("atom_name %s\n", ptr->atom_name);
		ptr = ptr->next_dir_item;
	}
}

void print_folder_contents(dent* folder){
	record* r_ptr = NULL;
	if(!folder)
		return;
	printf("folder %d\n", folder->dir_no);
	r_ptr = folder->folder_contents;
	while(r_ptr){
		printf("%8ld   %8ld   %2d  %s\n", r_ptr->p_ino, r_ptr->atom_ino, r_ptr->visited, r_ptr->atom_name);
		r_ptr = r_ptr->next;
	}
}

void free_record_chain(record* optr){
	record* next = NULL;
	record* ptr = optr;

	if(ptr == NULL)
		return;
	for(next = ptr->next; ptr != NULL;){
		free(ptr);
		ptr = next;
		if(next != NULL)
			next = next->next;
	}
}

void free_dent_chain(dent* optr){
	dent* next = NULL;
	dent* ptr = optr;
	
	if(ptr == NULL)
		return;
	for(next = ptr->next; ptr != NULL;){
		free_record_chain(ptr->folder_contents);
		ptr->folder_contents = NULL;
		free(ptr);
		ptr = next;
		if(next != NULL)
			next = next->next;
	}
}

void free_entry_chain(entry* optr){
	entry* next = NULL;
	entry* ptr = optr;
	
	if(ptr == NULL)
		return;
	for(next = ptr->next; ptr != NULL;){
		if(ptr->target){
			free(ptr->target);
			ptr->target = NULL;
		}
		free_dent_chain(ptr->opened_folders);
		ptr->opened_folders = NULL;
		free(ptr);
		ptr = next;
		if(next != NULL)
			next = next->next;
	}
}

void destroy_opened_folder(entry* parent_dir, int dir_no){
	dent* d_pre = NULL;
	dent* d_ptr = NULL;

	if(!parent_dir)
		return;
	d_pre = d_ptr = parent_dir->opened_folders;
	while(d_ptr){
		if(d_ptr->dir_no == dir_no){
			break;
		}
		else{
			d_pre = d_ptr;
			d_ptr = d_ptr->next;
		}
	}
	if(d_ptr){
		free_record_chain(d_ptr->folder_contents);
		d_ptr->folder_contents = NULL;
	}
	if(d_pre == d_ptr)//this dent is the first one
		parent_dir->opened_folders = d_ptr->next;
	else
		d_pre->next = d_ptr->next;
	free(d_ptr);
	d_ptr = NULL;
}


void destroy_table(){
	int i;
	if(!htable)
		return;
	for(i = 0 ; i < BUCKET; i++){
		if(htable[i] != NULL){
			free_entry_chain(htable[i]);
			htable[i] = NULL;
		}
	}
	free(htable);
	htable = NULL;
}

int entry_hash(ino_t p_ino, char* atom){
// given an atomic path component and its parent dir ino, return the hash value
//printf("p_ino is %d, atom is %s\n", p_ino, atom);	
	int hash = INIT;
	char* ptr = atom;
	if(ptr == NULL){
		printf("error in hashing the atom\n");
		return -1;
	}

	for (; *ptr != '\0'; ptr++){
		hash = (hash * 17 + *ptr) % BUCKET;
	}
	hash = (hash * 17 + p_ino) % BUCKET;
	return hash; // return the bucket number on success
}


entry* lookup(int bucket, entry* item, int* conf_flag){
// lookup a given item in the hashtable, return NULL if
// a) not found,set conf_flag to 0
// b) found, but conflict, set conf_flag to 1
// otherwise return the pointer to that item in the hashtable
	
	entry* ptr = htable[bucket];
	while(ptr){
		if(ptr->p_ino == item->p_ino && 0 == strcmp(ptr->atom_name, item->atom_name)){
			if(ptr->atom_ino == item->atom_ino){ //item found
				if(item->atom_ino != 0 && (ptr->atom_st.st_dev != item->atom_st.st_dev || ptr->atom_st.st_uid != item->atom_st.st_uid || ptr->atom_st.st_gid != item->atom_st.st_gid || ptr->atom_st.st_mode != item->atom_st.st_mode || ptr->atom_st.st_nlink != item->atom_st.st_nlink || ptr->atom_st.st_rdev != item->atom_st.st_rdev)){
					printf("conflict type 1\n");
					printf("mode in table %d\nmode to insert %d\n", ptr->atom_st.st_mode, item->atom_st.st_mode);
					*conf_flag = 1;
				}
				else if(S_ISLNK(ptr->atom_st.st_mode)){
					if(ptr->target && item->target && 0 == strcmp(ptr->target, item->target))
						*conf_flag = 0;
					else{
						printf("conflict type 2\n");
						*conf_flag = 1;
					}
				}
				else{
					*conf_flag = 0;
				}
				if(*conf_flag == 1)
					errno = -ERACE;
				return ptr;
			}
			else if(ptr->atom_ino == 0){
				*conf_flag = 0;
				return ptr;
			}
			else{
				printf("conflict type 3\n");
				printf("ptr_pino %ld, name %s, ino %ld\nitem_pino %ld, name %s, ino %ld\n", ptr->p_ino, ptr->atom_name, ptr->atom_ino, item->p_ino, item->atom_name, item->atom_ino);
				*conf_flag = 1;
				errno = -ERACE;
				return ptr;
			}
		}
		else{	// nothing is found in current entry
			ptr = ptr->next;
		}
	}
	*conf_flag = 0;
	return NULL;
}

void assign_entry(entry* item, ino_t p_ino, ino_t atom_ino, struct stat* atom_st, char* target, const char* atom_name){
	//assign values to struct tmpentry
        item->p_ino = p_ino;
        item->atom_ino = atom_ino;
		item->readdir_bit = 0;
		item->opened_folders = NULL;
        item->next = NULL;
		item->next_dir_item = NULL;

		strcpy(item->atom_name, atom_name);
		if(atom_st){
			memcpy(&(item->atom_st), atom_st, sizeof(struct stat));
		}
		if(target != NULL){ // atom is a symlink, assign target-string to target
			item->target = (char*)malloc(strlen(target) + 1);
			strcpy(item->target, target);
		}
		else{
			item->target = NULL;
		}
}


entry* find_dot_entry(struct stat* parent_stat, int* conf_flag){
	//find the entry "." in the hash table
	entry* p_entry = NULL;
	entry* ptr = NULL;
	int bucket;

	p_entry = (entry*)malloc(sizeof(entry) + strlen(".") + 1);
	assign_entry(p_entry, parent_stat->st_ino, parent_stat->st_ino, parent_stat, NULL, ".");
	bucket = entry_hash(p_entry->p_ino, p_entry->atom_name);
	if(bucket == -1){
		printf("hash error!\n");
		goto find_fail;
	}
	ptr = lookup(bucket, p_entry, conf_flag);
	free(p_entry);
	return ptr;

find_fail:
	free(p_entry);
	return NULL;
}

int update_dir_counter(int dirfd){
	int conf_flag;
	struct stat parent_stat;
	entry* dot_entry = NULL;
	entry* ptr = NULL;

	if(-1 == fstat(dirfd, &parent_stat))
		return -1;
	dot_entry = find_dot_entry(&parent_stat, &conf_flag);
	if(!dot_entry)
		return -1;
	
	ptr = dot_entry;
	dot_entry->nentries = 0;

	while(ptr){
		if(0 != strcmp(ptr->atom_name, "?"))
			dot_entry->nentries++;
		ptr = ptr->next_dir_item;
	}
	return dot_entry->nentries;
}


int add_entry(struct stat* parent_stat, const char* atom_name, struct stat* child_stat, char* target, int force_add){
//insert an item to the hashtable
//return 1 if insertion succeeds
//return 0 if item already exists
//return -1 if conflict found or error
	int conf_flag = 0;
	int bucket;
	entry* ptr = NULL;
	entry* dot_ptr = NULL;
	entry* item = NULL;
	
	item = (entry*)malloc(sizeof(entry) + strlen(atom_name) + 1);
	assign_entry(item, parent_stat->st_ino, child_stat->st_ino, child_stat, target, atom_name);
	
	bucket = entry_hash(item->p_ino, item->atom_name);
	if(bucket == -1){
		free(item);
		printf("hash error!\n");
		return -1;
	}
	ptr = lookup(bucket, item, &conf_flag);

	if(ptr != NULL && conf_flag == 1){ //conflict found
		free(item);
		printf("conflict found, error during inserting!\n");
		return -1;
	}
	else if(ptr != NULL && conf_flag == 0){ //item already exists
		if(item->atom_ino !=0 && ptr->atom_ino == 0){
			if(!force_add){
				free(item);
				return -1;
			}
			item->next_dir_item = ptr->next_dir_item;
			memcpy(ptr, item, sizeof(entry));
		}
		free(item);
		return 0;
	}
	else if(ptr == NULL && conf_flag == 0){ // ok to insert
		//inserting a non-DNE entry
		dot_ptr = find_dot_entry(parent_stat, &conf_flag);
		ptr = htable[bucket];
		if(ptr == NULL)
			htable[bucket] = item;
		else{
			htable[bucket] = item;
			item->next = ptr;
		}

// insert item to parent_folder item chain
		if(dot_ptr){
			item->next_dir_item = dot_ptr->next_dir_item;
			dot_ptr->next_dir_item = item;
		}
		return 1;
	}
	else
		return -1;	// this should not happen
}

int delete_entry(struct stat* parent_st, entry* item, char* target){
//delete a given item in the hash table, return 0 on success
	int dot_conf_flag, conf_flag;
	entry* dot_ptr = NULL;
	entry* ptr = NULL;
	entry* pre = NULL;

	int hval = entry_hash(item->p_ino, item->atom_name);
	ptr = lookup(hval, item, &conf_flag);
	if(ptr){ //the item exists in the table,replace it with a dangling entry 
		// find parent_dot_dir first
		dot_ptr = find_dot_entry(parent_st, &dot_conf_flag);
		if(dot_ptr == NULL || dot_conf_flag == 1)
			return -1;
		// delete the given entry
		if(target && ptr->target)
			strcpy(target, ptr->target);
		
		if(0 == strcmp(ptr->atom_name, ".") || 0 == strcmp(ptr->atom_name, "..")){
			pre = htable[hval];
			if(pre == ptr){ // the first one is what we want to delete
				htable[hval] = ptr->next;
				free(ptr);
			}
			else{
				while(pre->next != ptr)
					pre = pre->next;
				pre->next = ptr->next;
				free(ptr);
			}
		}
		else{
			ptr->atom_ino = 0;
			ptr->readdir_bit = 0;
			if(ptr->target)
				free(ptr->target);
			ptr->target = NULL;
		}
	}
	return 0;
}

/*********************************
  permissions
  ********************************/

void trace_credentials_init_effective(trace_credentials *tc){
  tc->uid = geteuid();
  tc->gids[0] = getegid();
  tc->ngids = getgroups(NGROUPS_MAX + 1, &tc->gids[1]) + 1;
}

void trace_credentials_init_real(trace_credentials *tc){
  tc->uid = getuid();
  tc->gids[0] = getgid();
  tc->ngids = 1;
}

int get_permbits(struct stat *s, trace_credentials *tc){
  int i;

  if (tc->uid == s->st_uid)
    return s->st_mode & S_IRWXU;
  else
    for (i = 0; i < tc->ngids; i++)
      if (tc->gids[i] == s->st_uid)
	return s->st_mode & S_IRWXG;
  return s->st_mode & S_IRWXO;
}

int read_perm(struct stat *s, trace_credentials *tc){
  return tc->uid == 0 || get_permbits(s, tc) & (S_IRUSR | S_IRGRP | S_IROTH);
}

int write_perm(struct stat *s, trace_credentials *tc){
  return tc->uid == 0 || get_permbits(s, tc) & (S_IWUSR | S_IWGRP | S_IWOTH);
}

int exec_perm(struct stat *s, trace_credentials *tc){
  return tc->uid == 0 || get_permbits(s, tc) & (S_IXUSR | S_IXGRP | S_IXOTH);
}

int wstat_perm(struct stat *f, trace_credentials *tc){
  return tc->uid == 0 || f->st_uid == tc->uid;
}

int search_perm(struct stat *d, int dflags, trace_credentials *tc){
  return (dflags & O_SEARCH) || tc->uid == 0 || 
    (get_permbits(d, tc) & (S_IXUSR | S_IXGRP | S_IXOTH));
}

int link_perm(struct stat *d, struct stat *f, trace_credentials *tc){
  return tc->uid == 0 || get_permbits(d, tc) & (S_IWUSR | S_IWGRP | S_IWOTH);
}

int unlink_perm(struct stat *d, struct stat *f, trace_credentials *tc){
  /* Handle sticky bit restrictions */
  return tc->uid == 0 ||
    ((get_permbits(d, tc) & (S_IWUSR | S_IWGRP | S_IWOTH))
     && ((d->st_mode & S_ISVTX) == 0 || f->st_uid == tc->uid));
}


int access_mode_check(struct stat* atom_stat, int mode, trace_credentials *tc){
	if (((mode & R_OK) && !read_perm(atom_stat, tc)) ||
             ((mode & W_OK) && !write_perm(atom_stat, tc)) ||
             ((mode & X_OK) && !exec_perm(atom_stat,  tc)))
      return -1;
	return 0;
}


int trace_access_check(struct stat* atom_stat, trace_credentials *tc){
	return search_perm(atom_stat, 0, tc) ? 0 : -1;
}

int local_check(struct stat* parent_stat, int* atom_fd, int openerrno, int staterrno, char* atom_name, struct stat* child_stat, int openflag, int mode, char* target){
	// after openat or open inside pathres() or safe_open_child(), call this func.
	// it stats atom_name, check consistency if atom_fd < 0
	// return 1 if conflict is found
	trace_credentials tc;
	int conflict = 0;
	int accmode = openflag & O_ACCMODE;
	int needread = accmode == O_RDONLY || accmode == O_RDWR;
	int needwrite = accmode == O_WRONLY || accmode == O_RDWR || (openflag & O_TRUNC);
	
	trace_credentials_init_effective(&tc);

	if(*atom_fd < 0){
		if (staterrno == 0 && S_ISLNK(child_stat->st_mode) && strlen(target) != child_stat->st_size)
			return -1;

		switch(openerrno){
			case EACCES:
				*atom_fd = -3;
				if ((staterrno == ENOENT && (openflag & O_CREAT) && !link_perm(parent_stat, child_stat, &tc)) ||
	  (staterrno == 0 && 
	   ((needread   && !read_perm(child_stat, &tc)) ||
	    (needwrite  && !write_perm(child_stat, &tc)))))
					conflict = 0;
				else
					conflict = 1;
				break;
			case ELOOP:
			// O_NOFOLLOW is set but atom_name is a symlink
				if (openflag & O_NOFOLLOW && staterrno == 0 && S_ISLNK(child_stat->st_mode)){
					*atom_fd = -2;
					conflict = 0;
				}
				else{
					*atom_fd = -1;
					conflict = 1;
				}
				break;
			case ENOENT:
				*atom_fd = -1;
				if (!(openflag & O_CREAT) && staterrno == ENOENT){
					conflict = 0;
					child_stat->st_ino = 0;
				}
				else
					conflict = 1;
				break;
			case EEXIST:
				*atom_fd = -1;
				if ((openflag & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL) && staterrno == 0)
					conflict = 0;
				else
					conflict = 1;
				break;
			case EISDIR:
				if (needwrite && staterrno == 0 && S_ISDIR(child_stat->st_mode))
					conflict = 0;
				else
					conflict = 1;
				break;
			case ENOTDIR:
				if ((!S_ISDIR(parent_stat->st_mode) && staterrno == ENOTDIR) || 
	  ((openflag & O_DIRECTORY) && staterrno == 0 && !S_ISDIR(child_stat->st_mode)))
					return 0;
				break;

			default:
				conflict = 1;
				break;
		}
	}
	else if (*atom_fd >= 0 && (openflag & (O_CREAT|O_EXCL)) == (O_CREAT|O_EXCL)){
		mode_t um = umask(S_IRWXU | S_IRWXG | S_IRWXO) & (S_IRWXU | S_IRWXG | S_IRWXO);
		umask(um);

		/* File definitely created */
		if(link_perm(parent_stat, child_stat, &tc)                          &&
        child_stat->st_mode  == ((mode & ~um) | S_IFREG)                    &&
        child_stat->st_nlink == 1                                           && 
        child_stat->st_uid   == tc.uid                                      &&
        (child_stat->st_gid  == tc.gids[0] || child_stat->st_gid == parent_stat->st_gid)																  &&
        child_stat->st_size  == 0)
			conflict = 0;
		else
			conflict = 1;
	}
	else if(*atom_fd >= 0 && !(openflag & O_CREAT)){
		/* File definitely not created */
		if (!S_ISLNK(child_stat->st_mode)									&&
		(!(openflag & O_DIRECTORY) || S_ISDIR(child_stat->st_mode))			&&
		child_stat->st_nlink >= 1											&&
		(!needread || read_perm(child_stat, &tc))							&&
		(!needwrite || write_perm(child_stat, &tc))							&&
		(!(openflag & O_TRUNC) || child_stat->st_size == 0))
			conflict = 0;
		else
			conflict = 1;
	}
	else
		conflict = 0;

	return conflict;
}


int dne_in_hashtable(entry* parent_dir, record* cur_record){
	//return 1 if cur_record in hashtable is a dne entry; otherwise return 0
	entry* ptr = parent_dir;
	while(ptr){
		if(0 == strcmp(ptr->atom_name, cur_record->atom_name))
			break;
		ptr = ptr->next_dir_item;
	}
	if(ptr && ptr->atom_ino == 0)
		return 1;
	return 0;
}


dent* alloc_dent(entry* parent_dir, DIR* dir){
	//attach a new dent struct to entry's dent link
	dent* newdent = NULL;
	
	if(!parent_dir || !dir)
		return NULL;
	
	newdent = (dent*)malloc(sizeof(dent));
	if(!newdent)
		return NULL;
	newdent->dir_no = (int)dir;
	newdent->folder_contents = NULL;
	//insert it to the dent_link_list of parent_dir
	newdent->next = parent_dir->opened_folders;
	parent_dir->opened_folders = newdent;
	return newdent;
}

int add_record(entry* ptr, dent* folder){
	record* file = NULL;

	if(ptr->atom_ino == 0)
		return 0;

	file = (record*)malloc(sizeof(record) + strlen(ptr->atom_name) + 1);
	if(!file)
		return -1;
	file->p_ino = ptr->p_ino;
	file->atom_ino = ptr->atom_ino;
	file->visited = 0;
	file->next = NULL;
	strcpy(file->atom_name, ptr->atom_name);
	//attach the record to dent list
	file->next = folder->folder_contents;
	folder->folder_contents = file;

	return 0;
}

int copy_fcontents(entry* parent_dir, dent* folder){
	// requires folder->folder_contents == NULL
	entry* ptr = parent_dir;
	
	if(!folder || folder->folder_contents)
		return -1;
	while(ptr){
		if(-1 == add_record(ptr, folder))
			return -1;
		ptr = ptr->next_dir_item;
	}
	return 0;
}

record* find_record_head(entry* parent_dir, int dir_no){
	dent* d_ptr = NULL;
	record* f_contents = NULL;

	if(!parent_dir)
		return NULL;
	d_ptr = parent_dir->opened_folders;
	while(d_ptr){
		if(d_ptr->dir_no == dir_no){
			break;
		}
		else{
			d_ptr = d_ptr->next;
		}
	}
	if(d_ptr)
		f_contents = d_ptr->folder_contents;
	return f_contents;
}


int check_record(entry* parent_dir, int dir_no){
	// after readdir gives a NULL result, check the consistency
	record* f_contents = NULL;

	f_contents = find_record_head(parent_dir, dir_no);
	while(f_contents){
		if(f_contents->visited != 1){
			if(!dne_in_hashtable(parent_dir, f_contents))
				return -1;
		}
		f_contents = f_contents->next;
	}
	return 0;
}


int mark_record(entry* parent_dir, int dir_no, ino_t p_ino, ino_t atom_ino, char* atom_name){
	// if the given p_ino etc. match a non-DNE entry inside opendir_table, mark it as visited
	// return 0 if marked, -1 if no record is found
	record* f_contents = NULL;

	f_contents = find_record_head(parent_dir, dir_no);
	while(f_contents){
		if(f_contents->p_ino == p_ino && f_contents->atom_ino == atom_ino && ((0 == strcmp(f_contents->atom_name, "?")) || (0 == strcmp(f_contents->atom_name, atom_name)))){
			f_contents->visited = 1;
			return 1;
		}
		else{
			f_contents = f_contents->next;
		}
	}
	return -1;
}


/***********************************
 * path resolution
 * *********************************/

/* this function checks consistency and adds atom_name to hashtable, if atom is a DIR, "." and ".." are also added to the hashtable */

int safe_open_child(char* target, int parent_fd, struct stat* parent_stat, char* atom_name, struct stat* child_stat, int openflag, int mode, int* is_created){
	
	int x, bucket, child_fd, conf_flag, conflict, openerr, staterr;
	entry* item = NULL;
	entry* ptr = NULL;

	openerr = staterr = errno = 0;
	if(openflag & O_CREAT){
		// decide whether to add O_EXCL flag
		item = (entry*) malloc(sizeof(entry) + strlen(atom_name) + 1);
		assign_entry(item, parent_stat->st_ino, 0, parent_stat, NULL, atom_name);
	
		bucket = entry_hash(item->p_ino, item->atom_name);
		if(bucket == -1){
			free(item);
			printf("hash error!\n");
			return -1;
		}
		ptr = lookup(bucket, item, &conf_flag);
		if(ptr && conf_flag != 0){
			openflag = openflag & (~O_CREAT);
			*is_created = 0;
		}
		else{
			x = fstatat(parent_fd, atom_name, child_stat, 0);
			if(x < 0){ // cannot stat atom_name
				openflag = openflag|O_EXCL;
				*is_created = mode;
			}
			else{
				openflag = openflag & (~O_CREAT);
				*is_created = 0;
			}
		}
		free(item);
	}
	
	errno = 0;
	child_fd = openat(parent_fd, atom_name, openflag, mode);
	openerr = errno;

	child_stat->st_ino = 0;
	if(child_fd > 0){
		if(-1 == fstat(child_fd, child_stat))
			goto soc_fail; 
	}
	else{
		errno = 0;
		fstatat(parent_fd, atom_name, child_stat, AT_SYMLINK_NOFOLLOW);
		staterr = errno;
	}
	
	if(child_stat->st_ino != 0 && S_ISLNK(child_stat->st_mode)){
		x = readlinkat(parent_fd, atom_name, target, MAXPATHLEN+1);
		if(x < 0)
			goto soc_fail;
		target[x] = '\0';
	}
	else
		memset(target, '\0', MAXPATHLEN+1);

	conflict = local_check(parent_stat, &child_fd, openerr, staterr, atom_name, child_stat, openflag, mode, target);

	if(0 == strcmp(atom_name, "."))
		goto soc_success;
	
	if(0 == strcmp(atom_name, "..")){
		switch(add_entry(parent_stat, "..", child_stat, target, 0)){
			case -1:
				goto soc_fail;
				break;
			case 0:
				goto soc_success;
				break;
			case 1:
				if(-1 == add_entry(child_stat, "?", parent_stat, target, 0))
					goto soc_fail;
				goto soc_success;
				break;
			default:
				goto soc_fail;
				break;
		}
	}

	if(-1 == add_entry(parent_stat, atom_name, child_stat, target, (openflag & O_CREAT) ? 1 : 0))
			goto soc_fail;
	
	if((child_stat->st_ino != 0) && (S_ISDIR(child_stat->st_mode))){
		if(-1 == add_entry(child_stat, ".", child_stat, target, 0))
			goto soc_fail;
		if(-1 == add_entry(child_stat, "..", parent_stat, target, 0))
			goto soc_fail;
	}
soc_success:
	if(conflict == 0)
		return child_fd;
soc_fail:
	if(child_fd > 0)
		close(child_fd);
	//conflict found
	errno = -ERACE;
	return -1;
}


int pathres(pathret* retval, const char* pathname, int* symcount, int flag, int openflag, int openmode, trace_credentials* tc){
// flag == P_SYM_NOFOLLOW means do not dereference the last component if it is a symlink
//recursively reslove each component inside the pathname till the end
//insertion to the hashtable is done for each component, return 0 on success
	int old_fd = -1;
	char* ptr = NULL;
	char* start = NULL;
	char target[MAXPATHLEN+1];
	char pname[MAXPATHLEN+1];

	memset(pname, '\0', MAXPATHLEN+1);
	strcpy(pname, pathname);
	ptr = pname;
	memset(retval->last_atom_name, '\0', MAXLEN+1);
	if(ptr == NULL || *ptr == '\0')
		return 0;

	if(*ptr == '/'){ // pathname is an absolute pathname
		while(*ptr == '/')
			ptr++;
		if(retval->last_dirfd < 0){
			retval->last_dirfd = safe_open_child(target, AT_FDCWD, &(retval->last_dirst), "/", &(retval->last_dirst), O_NOFOLLOW|O_SEARCH|O_DIRECTORY, openmode, &(retval->is_created));
			if(retval->last_dirfd < 0)
				return -1;
		}
		retval->last_atomfd = safe_open_child(target, retval->last_dirfd, &(retval->last_dirst), "/", &(retval->last_atomst), (ptr == NULL || *ptr == '\0') ? (openflag|O_NOFOLLOW|O_DIRECTORY) : (O_NOFOLLOW|O_SEARCH|O_DIRECTORY), openmode, &(retval->is_created));
			
		strcpy(retval->last_atom_name, "/");
		if(S_ISLNK(retval->last_atomst.st_mode) || !S_ISDIR(retval->last_atomst.st_mode)) // "/" is not a dir or it is a symlink!!
			return -1;
	}

	// now it is a reletive pathname
	while(1){
		if(ptr == NULL || *ptr == '\0')
			return 0;
		start = ptr;
		ptr = strchr(ptr, '/');
		if(ptr != NULL){
			*ptr++ = '\0';
			while(*ptr == '/'){
				ptr++;
			}
		}

		if(retval->last_dirfd < 0){	// last_dirfd is not assigned
			retval->last_dirfd = safe_open_child(target, AT_FDCWD, &(retval->last_dirst), ".", &(retval->last_dirst), O_NOFOLLOW|O_SEARCH|O_DIRECTORY, openmode, &(retval->is_created));
			if(retval->last_dirfd < 0)
				return -1;
			if (-1 == add_entry(&(retval->last_dirst), ".", &(retval->last_dirst), NULL, 0))
				return -1;
		}
		
		if(0 == strcmp(start, ".")){
			if(ptr == NULL || *ptr == '\0'){
				retval->last_atomfd = retval->last_dirfd;
				memcpy(&(retval->last_atomst), &(retval->last_dirst), sizeof(struct stat));
			}
			continue;
		}
		// the substring [start,ptr-1] is the next atom
		if(retval->last_atomfd >= 0){	
			old_fd = retval->last_dirfd;
			retval->last_dirfd = retval->last_atomfd;
			memcpy(&(retval->last_dirst), &(retval->last_atomst), sizeof(struct stat));
			retval->last_atomfd = -1;
		}
		strcpy(retval->last_atom_name, start);
		
//		printf("dirfd %d, p_ino %ld, start %s\n", retval->last_dirfd, retval->last_dirst.st_ino, start);
		retval->last_atomfd = safe_open_child(target, retval->last_dirfd, &(retval->last_dirst), start, &(retval->last_atomst), (ptr == NULL || *ptr == '\0') ? (openflag|O_NOFOLLOW) : (O_SEARCH|O_NOFOLLOW), openmode, &(retval->is_created));
//		printf("last_atomfd is %d\n", retval->last_atomfd);

		if(old_fd > 0)
			close(old_fd);
	
		if(tc){
			if(trace_access_check(&(retval->last_dirst), tc) < 0)
				return -1;
		}

		if(retval->last_atomfd < 0 && retval->last_atomfd != -2)
			return -1;
		else if(-2 == retval->last_atomfd){
			//start is a symlink
			*symcount += 1;
			if(*symcount > MAXSYMNO){
				printf("too many symbolic links, abort resolving!\n");
				return -1;
			}
				
			if((ptr == NULL || *ptr == '\0') && (flag & P_SYM_NOFOLLOW)){
				//start is last atom, and flag is P_SYM_NOFOLLOW
				continue;
			}
			else if((ptr == NULL || *ptr == '\0') && !(flag & P_SYM_NOFOLLOW)){
				//start is last atom, and flag says FOLLOW
				if(pathres(retval, target, symcount, flag, openflag, openmode, tc) < 0){
					printf("recursively call path failed! 1\n");
					return -1;
				}
			}
			else if((ptr && *ptr != '\0') && (flag & P_SYM_NOFOLLOW)){ 
				//start is NOT last atom, and flag is P_SYM_NOFOLLOW
				if(pathres(retval, target, symcount, 0, O_SEARCH, 0, tc) < 0){
					printf("recursively call path failed! 2\n");
					return -1;
				}
			}
			else{
				//start is NOT last atom, and flag says FOLLOW
				if(pathres(retval, target, symcount, flag, O_SEARCH, 0, tc) < 0){
					printf("recursively call path failed! 3\n");
					return -1;
				}
			}
		}
		else
			;
	}
}


