
#define _GNU_SOURCE 1      // Do this in order to enable the RTLD_NEXT symbol
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>
#include <dlfcn.h>
#include "hash_lib.h"

#define LIB_RDONLY	0
#define LIB_WRONLY	1
#define LIB_RDWR	2

#define CWD_MAX_LEN	1024
#define PAD_LEN		3

#define GET_PARENT	1
#define GET_CHILD	2

#define IS_FACCESS_MODE(MODE) \
	(MODE != -1)
#define IS_PARENT_REQ(REQ_TYPE) \
	(REQ_TYPE == GET_PARENT)

#define IS_VALID_INODE(INODE) \
	((INODE) >= 0)

c_stack *cur_dir_stk = NULL;
char cwd[CWD_MAX_LEN];
int init_sec_interpose = 0;

/* Function pointers for calling the next DLL's symbol */
static int (*func_access) (const char *, int mode);
static int (*func_lxstat) (int,const char *,struct stat *);
static int (*func_open_create) (const char *, int flags,__mode_t mode);
static int (*func_open) (const char *, __mode_t mode) ;
static int (*func_openat) (int, const char *, __mode_t mode) ;
static int (*func_chdir)(int);
static int (*func_unlink)(const char*);
static int (*func_chown)(const char *path, uid_t owner, gid_t group);
static int (*func_fchown)(int fd, uid_t owner, gid_t group);
static int (*func_chmod)(const char *path, mode_t mode);
static int (*func_fchmod)(int fildes, mode_t mode);


static void init_fn_ptrs (void) 
{
	if (init_sec_interpose)
		return;

	func_access = (int (*)(const char *, int)) dlsym(RTLD_NEXT, "access");
	func_lxstat = (int (*)(int, const char *, struct stat *)) 
				dlsym(RTLD_NEXT, "__lxstat");
	func_open_create =  
	 (int (*)(const char *, int,  __mode_t)) dlsym(RTLD_NEXT, "open");
	func_open = (int (*)(const char *, __mode_t)) dlsym(RTLD_NEXT, "open");
	func_openat = (int (*)(int, const char *, __mode_t)) 
			dlsym(RTLD_NEXT, "openat");
	func_chdir = (int (*)(int)) dlsym(RTLD_NEXT, "fchdir");
	func_unlink = (int (*)(const char*)) dlsym(RTLD_NEXT, "unlink");
	func_chown = (int (*)(const char *, uid_t, gid_t)) 
				dlsym(RTLD_NEXT, "chown");

	func_fchown = (int (*)(int, uid_t, gid_t )) dlsym(RTLD_NEXT, "fchown");
	func_chmod = (int (*)(const char *, mode_t)) dlsym(RTLD_NEXT, "chmod");
	func_fchmod = (int (*)(int, mode_t)) dlsym(RTLD_NEXT, "fchmod");
	init_sec_interpose = 1;
	
} 

/* Helper functions */
static inline int is_relative_path(const char *path)
{
	if (path[0] == '/')
		return 0;
	else
		return 1; 
}

/* Get the top of the stack */
inline int get_top_c_stack()
{
	int fd; 

	if (!cur_dir_stk) {
		/* We need to create the current directory's entry */
		if (func_open)
			fd = func_open (".", LIB_RDONLY);
		if (fd < 0)
		{
			DEBUG_PRINT("Open Failed : %d\n",errno);
			return 0;
		}
		push_fd_cstack(fd, &cur_dir_stk);
		/* Store pwd */
		getcwd(cwd, CWD_MAX_LEN);
	}
	return cur_dir_stk->fd;
}

/*
 * This accepts a relative path w.r.t present top of the stack fd
 * or absolute path. 
 * It chops each component of the pathname and opens it w.r.t to 
 * its parent fd. If the path is relative to curent top of stack,
 * or some directory below top of stack, we will always open the correct
 * file. 
 * @returns: fd of the last component 
 */
int chop_hash_pathname(int top_fd, const char *path, int prefix_len, 
					int mode)
{
	int ret = -1;
	char *temp_path = NULL, *comp_path = NULL;
	char *prev_ptr;
	char *temp_ptr = (char *)path;
	int cur_fd = -1, parent_fd = 0, old_chdir = 0;
	int i = 1, len;
    	struct stat stat_buf;
	
	DEBUG_PRINT("%s Prefix Len :%d path : %s, pathlen : %d", __FUNCTION__,
			prefix_len, path, strlen(path));
	if (prefix_len > strlen(path))
	{
		DEBUG_PRINT("%s BUG : Prefix Len :%d path : %s", __FUNCTION__,
			prefix_len, path);
		return -1;
	}

	temp_path = malloc (strlen(path) + 1);
	if (!temp_path) 
	{
		errno = ENOMEM;
		goto out;
	}

	comp_path = malloc (strlen(path) + 1);
	if (!comp_path)
	{
		errno = ENOMEM;
		goto out;
	}

	/* access : Store the old stack pointer as we chdir */
	old_chdir = get_top_c_stack();

	if (!top_fd) {
		/* An extra open_push for "/" */
		parent_fd = func_open ("/", LIB_RDONLY);
		if (parent_fd < 0) {
			goto out;
		}
		
		/* Call fstat on it and store the target's inode */
		ret = fstat(parent_fd, &stat_buf);
		if (ret < 0)
		{
			perror("fstat");
			goto out;
		}
		insert_inode_fname_hash ("/", stat_buf.st_ino);
	
		temp_ptr++;
		i++;
	} else {
		DEBUG_PRINT("Got parent fd : %d\n",top_fd);
		parent_fd = top_fd;
	}

	if (IS_FACCESS_MODE(mode)) {
		DEBUG_PRINT("ACESS : Opening the file in %d mode \n", mode);
		ret = func_chdir(parent_fd);
		if (ret < 0) {
			perror("ACCESS fchdir:");
			goto out;
		}
	}

	temp_ptr = temp_ptr + prefix_len;
	i += prefix_len;
	prev_ptr = temp_ptr;	
	
	while (*temp_ptr != '\0') {
		while(*temp_ptr != '\0' && *temp_ptr != '/') {
			i++;
			temp_ptr++;
		}
		strncpy (temp_path, path, i-1);
		temp_path[i-1] = '\0';
		len = temp_ptr - prev_ptr;
		strncpy (comp_path, prev_ptr, len);
		comp_path[len] = '\0';
		
		DEBUG_PRINT ("comp_path : %s, temp_path : %s, i: %d, " 
			"len : %d\n",comp_path, temp_path, i , len);
	
		/* 
		 * Openat and store the inode of the subcomponent
		 * in the hash 
		 */ 
		cur_fd = func_openat (parent_fd, comp_path, LIB_RDONLY);
		if (cur_fd < 0)
		{
			perror("openat : chop_hash");
			ret = -1;
			goto out;
		}

		ret = fstat(cur_fd, &stat_buf);
		if (ret < 0)
		{
			perror("fstat");
			goto out;
		}

		/* Called from access */
		if (IS_FACCESS_MODE(mode)) {
			/* No need to execute func_access, as :-
			 * open() check READ permissions and chdir 
			 * checked EXECUTE permissions on cur dir
			 * Last component to be handled separately.
			 */
			if (S_ISDIR(stat_buf.st_mode)) {
				ret = func_chdir(cur_fd);
				if (ret < 0) {
					perror("ACCESS fchdir:");
					goto out;
				}

				/* It is the last component, 
				 * check the exact access */
				if (*temp_ptr == '\0') {
					ret = func_access(".", mode);
					if (ret < 0) 
						goto out;
				}
			} else {
				//File Condition
				ret = func_access(comp_path, mode);
				if (ret < 0) 
					goto out;
			} //ELSE of S_ISDIR
		} //if (IS_FACCESS())

		/* Cur component path looks good -- cache it */
		insert_inode_fname_hash (temp_path, stat_buf.st_ino);
		if ((parent_fd) && (parent_fd != top_fd))
			close(parent_fd);
		parent_fd = cur_fd;

		if (*temp_ptr == '\0')
			break;
		
		temp_ptr++;
		prev_ptr = temp_ptr;
		i++;

		/* Remove all the extra slashes */
		while (*temp_ptr != '\0' && *temp_ptr == '/') {
			i++;
			temp_ptr++;
		}
	}

	ret = cur_fd;
out:
	if (temp_path)
		free(temp_path);
	if (comp_path)
		free(comp_path);

	func_chdir(old_chdir);
	return ret;

}

/* Resolve the relative path */
int resolve_relative_path(const char *pathname, char **resolved_path, int *prefix_len)
{
 	c_stack *temp_stk = NULL;
 	c_stack *temp_node = NULL;
	int parent_fd =0, ret =0, skip_level = 0;
	char *temp_path = (char *)pathname;
	char *new_path;
	int i, new_path_len ;

	*prefix_len = 0;
	/* Till the number of .. pop out */
	while (*temp_path != '\0') {
		if (strncmp(temp_path, "..", 2) == 0) {
			/* Pop the node from the orig stack 
			 * and push on temp stack for recovery 
			 */
			pop_node_cstack(&temp_node, &cur_dir_stk);
			push_node_cstack(temp_node, &temp_stk);
			temp_path += 2;
			skip_level++;
			while (*temp_path != '\0' && *temp_path == '/')
				temp_path++;
		} else if (strncmp(temp_path, "./", 2) == 0) {
			temp_path += 2;
		} else
			break; 
        } 

	/* Store the parent fd and restore the dir stack */
	parent_fd = get_top_c_stack();
	while (!IS_STACK_EMPTY(temp_stk)) {
		pop_node_cstack(&temp_node, &temp_stk);
		push_node_cstack(temp_node, &cur_dir_stk);
	}

	ret = parent_fd;
	/* Resolve the path */
	i = strlen (cwd) - 1;
	DEBUG_PRINT("CWD %s %d i=%d , top : %d\n", cwd, skip_level,i,parent_fd);
	while (skip_level) {
		/* Remove all the trailing // */
		while ((i > 0) && (cwd[i] == '/')) {
			i--;
		}
	
		/* skip the last file/dirname */
		while ((i > 0) && (cwd[i] != '/')) {
			i--;
		}
		skip_level--;
	}

	new_path_len = strlen(temp_path) + i + PAD_LEN;

	new_path = malloc (new_path_len);
	if(new_path == NULL) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}
	memset (new_path, 0, new_path_len);

	strncpy (new_path, &cwd[0], (i + 1));
	new_path[i+1] = '\0';
	*prefix_len = i+1;
	if (new_path[i] != '/') {
		strcat(new_path, "/");
		*prefix_len = *prefix_len + 1;
		DEBUG_PRINT("Prefix %d\n",*prefix_len);
	}

	strcat (new_path, temp_path);
	DEBUG_PRINT("New Path : %s\n", new_path);
	*resolved_path = new_path;
out:
	return ret;
}

/* 
 * In this function we check for consistency from / till the leaf
 * node, or till the values exist in the hash.
 * If the complete path is absent in hash, it means a stat/access
 * was not performed, so we *cannot* protect this operation.
 * Even if the earlier component in the path are not found, we
 * continue till we get an entry.
 * E.g. if /tmp/user1 , /tmp 's entry is absent we continue and
 * try to find /tmp/user1's entry.
 */
int check_hash_pathname(const char *path, int req_type)
{
	int ret = -1;
	int cur_fd = -1, parent_fd = -1, prev_parent_fd = 0;
	int i = 1, len;
	int inode;
	char *temp_path = NULL, *comp_path = NULL;
	char *prev_ptr;
	char *temp_ptr = (char *)path;
    	struct stat stat_buf;

	temp_path = malloc (strlen(path) + 1);
	if (!temp_path) 
	{
		errno = ENOMEM;
		goto out;
	}

	comp_path = malloc (strlen(path) + 1);
	if (!comp_path)
	{
		errno = ENOMEM;
		goto out;
	}


	if (is_relative_path(path)) {
		DEBUG_PRINT ("%s BUG : Cannot be relative pathname :%s \n",
			__FUNCTION__, path);
		return -1;
	} else {
		/* An extra open_push for "/" */
		parent_fd = func_open ("/", LIB_RDONLY);
		if (parent_fd < 0) {
			ret = -1;
			goto out;
		}
		
		ret = fstat(parent_fd, &stat_buf);
		if (ret < 0)
		{
			perror("fstat");
			goto out;
		}
		inode = search_inode("/");

		/* 
		 * If the entry is absent means - no one is concerned about 
		 * stat/access for this component, we try finding the other
		 * components in the path
		 */
		if(inode < 0) {
			DEBUG_PRINT("\n!!CAUTION !! : It seems stat/access " 
			"for not called still continuing %s... \n","/");	
				
		} else {
			DEBUG_PRINT("Inode  value from fstat for  %s is %d\n","/", stat_buf.st_ino);
			if(inode != stat_buf.st_ino) {
				DEBUG_PRINT("Inode for %s does not " 
				"match with the previous entry\n","/");
				errno = EACCES;
				ret = -1;
				goto out;
			}
		}
	
	}

	temp_ptr++;
	i++;
	prev_ptr = temp_ptr;	
	
	while (*temp_ptr != '\0') {
		while(*temp_ptr != '\0' && *temp_ptr != '/') {
			i++;
			temp_ptr++;
		}

		/* Openat and store the inode of the subcomponent
		 * in the hash 
		 */ 
		strncpy (temp_path, path, i-1);
		temp_path[i-1] = '\0';
		len = temp_ptr - prev_ptr;
		strncpy (comp_path, prev_ptr, len);
		comp_path[len] = '\0';
		DEBUG_PRINT ("comp_path : %s, temp_path : %s, i: %d, len : %d\n",comp_path, temp_path, i , len);
		cur_fd = func_openat (parent_fd, comp_path, LIB_RDONLY);
		if (cur_fd < 0)
		{
			perror("openat : check_hash");
			goto out;
		}

		ret = fstat(cur_fd, &stat_buf);
		if (ret < 0)
		{
			perror("fstat");
			goto out;
		}
		
		inode = search_inode(temp_path);
		/* If the entry is absent means - no one is concerned about 
		 * stat/access - so let the application open normally. 
		 */
		if(inode < 0) {
			DEBUG_PRINT("\n!!CAUTION !! : It seems stat/access " 
			"was not called on %s, still continuing ...\n ", 
			temp_path);	
		} else {
			if(inode != stat_buf.st_ino) {
				DEBUG_PRINT("Inode for %s does not match " 
				"with the previous entry\n","/");
				errno = EACCES;
				ret = -1;
				goto out;
			}
		}

		if (prev_parent_fd)
			close(prev_parent_fd);
		prev_parent_fd = parent_fd;
		parent_fd = cur_fd;

		DEBUG_PRINT("Inode : %s : %d, prev_parent: %d, " 
		"parent : %d , curfd :%d \n", temp_path, stat_buf.st_ino, 
		prev_parent_fd, parent_fd, cur_fd);

		if (*temp_ptr == '\0') {
			break;
		}
		temp_ptr++;
		prev_ptr = temp_ptr;
		i++;

		/* Remove all the extra slashes */
		while (*temp_ptr != '\0' && *temp_ptr == '/') {
			i++;
			temp_ptr++;
		}
	}

	if (ret != -1) {
		/* Setting the correct fd for return value */
		if (IS_PARENT_REQ(req_type)) {
			close(cur_fd);
			ret = prev_parent_fd;
		} else {
			close(prev_parent_fd);
			ret = cur_fd;
		}
	} else
		ret  = 0;
out:
	if (temp_path)
		free(temp_path);
	if (comp_path)
		free(comp_path);
	DEBUG_PRINT("RET : %d",ret);
	return ret;

}

/* Called from chdir to push each component's fd onto the stack */
int open_push_fd(char *path)
{
	int ret = 0;
	char *temp_path = NULL;
	char *temp_ptr = path;
	int fd;
	int i = 1;

	temp_path = malloc (strlen(path));
	if (!temp_path)
		return -ENOMEM;

	if (!is_relative_path(path)) {
		/* An extra open_push for "/" */
		fd = func_open ("/", LIB_RDONLY);
		if (fd < 0) {
			ret = fd;
			goto out;
		}
		push_fd_cstack(fd, &cur_dir_stk);
		temp_ptr++;
		i++;
	}

	while (*temp_ptr != '\0') {
		while(*temp_ptr != '\0' && *temp_ptr != '/') {
			i++;
			temp_ptr++;
		}
		strncpy (temp_path, path, i);
		temp_path[i] = '\0';
		DEBUG_PRINT("Temp %s i : %d\n", temp_path, i);	
		fd = func_open (temp_path, LIB_RDONLY);
		push_fd_cstack(fd, &cur_dir_stk);

		if (*temp_ptr == '\0')
			break;

		temp_ptr++;
		i++;

		/* Remove all the extra slashes */
		while (*temp_ptr != '\0' && *temp_ptr == '/') {
			i++;
			temp_ptr++;
		}
	}

	fd = get_top_c_stack();
	ret = func_chdir(fd);
	DEBUG_PRINT("Top stack fd: %d . Chdir into it. ret : %d \n",fd, ret);
	
out:	
	if (temp_path)
		free(temp_path);
	return ret;
}

/* Function gets the last component of an absolute path name */
int get_child_fname (char*pathname, char **filename)
{
	int path_len = strlen (pathname);
	int i = path_len - 1;
	char *fname = NULL;
	
	/*Remove all the trailing // e.g. /tmp/a/// */
	while ((i > 0) && (pathname[i] == '/')) {
		i--;
	}
	
	/* skip the last file/dirname */
	while ((i > 0) && (pathname[i] != '/')) {
		i--;
	}
	path_len = path_len - i;
	fname = malloc(sizeof(char) * (path_len + 1));
	if (!fname) {
		errno = ENOMEM;
		return -1;
	}

	/* Copy File name */
	strncpy(fname, &pathname[i+1], path_len);
	fname[path_len] = '\0';

	*filename = fname;
	DEBUG_PRINT ("Filename : %s, pathlen : %d\n", fname, path_len);

	return 0;
}

/* 
 * Library Calls Interposed -- access()
 */
int access(const char *pathname , int mode)
{
	char *resolved_path = NULL;
	char *final_path = (char *)pathname;
	int ret= 0;
	int parent_fd = 0;
	uid_t ruid, euid;
	gid_t rgid, egid;
	int prefix_len = 0;

	DEBUG_PRINT("Pathname: %s Mode : %d\n", pathname, mode);

	init_fn_ptrs ();
	init_hash_table();
	/* 1. Seteuid to real uid/gid IF euid and uid are different
	 * 2. Open each component file with real user and store the fd (this is
	 *    is because "open" call opens the file with euid/
	 * 3. chdir into it if it is directory 
	 * 4. This will be used later to fstat and compare with 
	 * 	later fd.
	 * 5. Set the eduid back to normal.
	 * 6. Call access function.
	 */
	euid = geteuid();
	ruid = getuid();
	egid = getegid();
	rgid = getgid();

	if ((euid == ruid) && (egid == rgid)) {
		DEBUG_PRINT("Both euid/ruid and egid/rgid are the same " 
			": %d, %d\n",euid, egid);
		goto out;
	}
	
	DEBUG_PRINT("Old euid: %d, readuid : %d, p_euid : %d", 
				euid, ruid, geteuid());

	/* Set the real uid/gid and then perform open and store its inodes */
	setegid(rgid);
	seteuid(ruid);

	if (is_relative_path(pathname))
	{
		parent_fd = resolve_relative_path(pathname, 
					&resolved_path, &prefix_len);
		if (parent_fd < 0) {
			ret = -1;
			goto out;
		}
	}
	DEBUG_PRINT("Resolved Path : %s\n", resolved_path);
	if (resolved_path != NULL)
		final_path = resolved_path;
	parent_fd = chop_hash_pathname(parent_fd, final_path,
						prefix_len, mode);
	/* Reset the old euid */
	seteuid(euid);
	setegid(egid);

	DEBUG_PRINT("p_euid : %d", geteuid());

out:
	if (resolved_path)
		free(resolved_path);
	return ret;
}

/* Libary call interposed -- chdir()
 * NOTE : If the chdir fails due to malloc, open error etc,
 * we need to return to the same directory and resume the stack.
 */
int chdir(const char *pathname) 
{
	int ret = 0;
	int fd;		
	int is_abs_path = 0;
	char *new_path = (char *)pathname;
	c_stack *temp_stk = NULL;
	c_stack *temp_node = NULL;
	c_stack *restore_point = NULL;
	
	init_fn_ptrs ();
	if (is_relative_path(new_path)) {
		/* Till the number of .. pop out */
		while (*new_path != '\0') {
			if (strncmp(new_path, "..", 2) == 0) {
				/* Pop the node from the orig stack 
				 * and push on temp stack for recovery 
				 */
				pop_node_cstack(&temp_node, &cur_dir_stk);
				push_node_cstack(temp_node, &temp_stk);

				/* Store the top of the stack so that we 
				 * know beyonf this point it was new locations
				 * To solve ../../dir1 kinda probs 
				 */
				restore_point = cur_dir_stk;
				//fd = pop_fd_cstack(&cur_dir_stk);
				new_path += 2;
				while (*new_path != '\0' && *new_path == '/')
					new_path++;
			} else if (strncmp(new_path, "./", 2) == 0) {
				new_path += 2;
			} else
				break; 
		}
	} else {
		/* Change the pointers of temp_stk to point to the
		 * to the orig stack and make cu_stk=NULL.
		 * Mark a flag to take care of this.
		 */
		is_abs_path = 1;
		temp_stk = cur_dir_stk;
		cur_dir_stk = NULL;
	}
	
	/* 
	 * In absolute or after we have popped all the ../ 
	 * we fchdir to top of the stack dir and 
	 * then do a open till the pathend and keep
	 * pushing the elements. 
	 */
	if (!IS_STACK_EMPTY(cur_dir_stk)) {
		fd = get_top_c_stack();
		DEBUG_PRINT("Top stack fd: %d",fd);
		ret = func_chdir(fd);
	}

	/* 
	 * Since we are now in the correct directory, and the
	 * the path still exists, we keep pushing it
	 */
	if (*new_path != '\0') {
		ret = open_push_fd(new_path);
	}
	
	if (ret < 0) {
		DEBUG_PRINT("Error in chdir : restore to the old stack\n");
		if (IS_STACK_EMPTY(temp_stk)) 
			goto out;
 
		
		/* Restore the orig from the temp. */
		if (is_abs_path) {
			cur_dir_stk = temp_stk;
		} else {
		
			/* Not absolute 
			 * go from cur_dir_stk till restore_point and pop out
			 * out. Start popping out nodes from temp_stk and push them to
			 * cur_dir_stk.
			 */
			while (cur_dir_stk != restore_point) {
				pop_fd_cstack(&cur_dir_stk);
			}

			while (!IS_STACK_EMPTY(temp_stk)) {
				pop_node_cstack(&temp_node, &temp_stk);
				push_node_cstack(temp_node, &cur_dir_stk);
			}
		}

		/* Chdir to orig directory */
		fd = get_top_c_stack();
		ret = func_chdir(fd);
		if (ret < 0)
			goto out;
		
	} else {
		/* Store pwd */
		ret = (int)getcwd(cwd, CWD_MAX_LEN);
		
		if (ret < 0)
			goto out;
		strcat(cwd,"/");
		/* Everything is fine, so delete the temp stack */
		DEBUG_PRINT("Chdir Performed w/o problems cwd : %s\n", cwd);
		delete_fd_cstack (&temp_stk);
	}
out:
	return ret;
}

/*
 * Library call interposed -- unlink()
 * 1. Traverse the whole path and check for inconsistency.
 * 2. If no inconsistency, fchdir to the second last directory 
 * and then unlink
 */
int unlink(const char *pathname)
{
	int parent_fd = 0, old_chdir_fd = 0;
	int ret = -1;
	int prefix_len = 0;
	char *resolved_path = NULL;
	char *final_path = NULL;
	char *filename = NULL;

	init_fn_ptrs ();
	init_hash_table();

	if (is_relative_path(pathname))
	{
		parent_fd = resolve_relative_path(pathname, 
				&resolved_path, &prefix_len);
		if (parent_fd < 0) {
			goto error;
		} else {
			if (resolved_path == NULL) {
				DEBUG_PRINT("BUG : Resolved path should " 
						"not be NULL\n");
				goto out;
			}
		}
	}

	if (resolved_path == NULL)
		final_path = (char *)pathname;
	else
		final_path = resolved_path;

	/* Retrieve the child filename */
	get_child_fname(final_path, &filename);

	/* Ask for the container directory */
	parent_fd = check_hash_pathname(final_path, GET_PARENT);
	if(parent_fd < 0) {
		DEBUG_PRINT("Unlink Vulnerability detected. %s\n",pathname);
		goto error;
	} else if (parent_fd == 0){
		DEBUG_PRINT("Performing unlink on %s, w/o checks. \n",pathname);
		final_path = (char *)pathname;
		goto out;
	
	} else {
		DEBUG_PRINT("File %s is consistent. " 
			"Chdir into parent dir \n",pathname);
		old_chdir_fd = get_top_c_stack();
		/* fchdir to parent dir */
		ret = func_chdir (parent_fd);
		final_path = filename;
		goto out;

	}

out:
	ret = func_unlink(final_path);

error:
	if (parent_fd > 0) {
		close(parent_fd);
		delete_inode_fname_hash(final_path);
		/* Restore to the orig directory */
		func_chdir (old_chdir_fd);
	}

	if (resolved_path)
		free (resolved_path);

	if (filename)
		free(filename);

	return ret;
}

/*
 *   Interposed __xstat() call that is called from actual stat() call and
 *   does the following:
 *   1. open() the input filename to get the fd
 *   2. Store the filename and fd in the hash
 */
int __lxstat(int version, const char *pathname , struct stat *buf)
{

	int result =0, fd =0;
	char *resolved_path = NULL, *final_path = NULL;
	int prefix_len = 0;
	int inode;

	init_fn_ptrs ();
	init_hash_table();
	if (is_relative_path(pathname))
	{
		fd = resolve_relative_path(pathname, 
				&resolved_path, &prefix_len);
		if (fd < 0) {
			result = -1;
			goto out;
		} else {
			if (resolved_path == NULL) {
				DEBUG_PRINT("BUG : Resolved path should " 
						"not be NULL\n");
				result = -1;
				goto out;
			}
		
		}
	}

	if (resolved_path == NULL)
		final_path = (char *)pathname;
	else
		final_path = resolved_path;

	fd = chop_hash_pathname(fd, final_path, prefix_len, -1);

	if(fd > 0) {
		result = func_lxstat(version, pathname, buf);

		/* Check the inode against the one which we have stored
		 * ONLY if the file is not a symbolic link:-
		 * We have stored the target's inode and
		 * if the lxstat is getting result that
		 * it is symlink, it means it is a legitimate
		 * call
		 */
		if (S_ISLNK(buf->st_mode))
			goto out;
		inode = search_inode(final_path);
		if (inode != buf->st_ino) 
		{
			/* Somebody has changed the last component 
			 * under our feet 
			 */
			DEBUG_PRINT("\n!!CAUTION!! : Program is under attack "
				" returning error\n");
			errno = EACCES;
			result = -1;
			goto out;
			
		}
	} else { 
		DEBUG_PRINT("ERROR in consistency check : %s\n",pathname);
		result = -1;
	}

out:
	if (resolved_path)
		free(resolved_path);
	return result;
}

/* 
 * Interposed  open() call that intercepts the actual open() call and
 * does the following:
 * 1. fstat() the given filename to get the new file descriptor
 * 2. Get the old file descriptor from the hash table 
 * 3. If found compare the old and new file descriptors and if they dont
 *    match return appropriate error message. 
 * 4. If old fd not found means the filename was not stat() before so
 *    proceed as usual.   
*/
int open(const char *pathname, int flags, mode_t mode)
{

	int check_fd = 0, ret_fd = -1, ret = -1;
	int prefix_len = 0;
	int inode;
	char *resolved_path = NULL;
	char *final_path = NULL;
    	struct stat stat_buf;

	init_fn_ptrs ();
	init_hash_table();
	if (is_relative_path(pathname))
	{
		check_fd = resolve_relative_path(pathname, 
				&resolved_path, &prefix_len);
		if (check_fd < 0) {
			goto out;
		} else {
			if (resolved_path == NULL) {
				DEBUG_PRINT("BUG : Resolved path should " 
						"not be NULL\n");
				goto out;
			}
		}
	}

	if (resolved_path == NULL)
		final_path = (char *)pathname;
	else
		final_path = resolved_path;

	check_fd = check_hash_pathname(final_path, GET_CHILD);
	if(check_fd < 0){
		DEBUG_PRINT("\n!!CAUTION!! : Program is under attack "
				" returning error : %s\n",pathname);
		goto error;
	} else {
		DEBUG_PRINT("File is consistent or pattern "
			"not found %s . Fd : %d\n",pathname, check_fd);

		/* Open the complete file again with the 
		 * permission passed by the user 
		 */
		ret_fd = func_open_create(pathname, flags, mode);	
		if (ret_fd && check_fd) {

			/* Check inode again to be doubly sure, 
			 * our API is not tricked
			 */
			inode = search_inode (final_path);
			ret = fstat(ret_fd, &stat_buf);
			if (ret >= 0) {
				if (IS_VALID_INODE(inode) && 
					(inode != stat_buf.st_ino)) {
				DEBUG_PRINT("\n!!CAUTION!! : Program is under " 
				"attack returning error : %s\n",pathname);
				goto error;
				}
			} else {
				DEBUG_PRINT("Failed to check the consistency of "
					"pathname : %s\n",pathname);
				perror("fstat");
				goto error;
			}
		}
		goto out;
	 }

error:
	if (ret_fd > 0)
		close(ret_fd);
	ret_fd = -1;

out:
	if (check_fd > 0)
		close(check_fd);
	if (resolved_path)
		free (resolved_path);
	return ret_fd;
}

/* 
 * Interposed  chmod() call that intercepts the actual chmod() call and
 * does the following:
 * 1. fstat() the given filename to get the new file descriptor
 * 2. Get the old file descriptor from the hash table 
 * 3. If found compare the old and new file descriptors and if they dont
 *    match return appropriate error message. 
 * 4. If old fd not found means the filename was not stat() before so
 *    proceed as usual.   
*/
int chmod(const char *pathname, mode_t mode)
{

	int check_fd = 0, ret = -1;
	int prefix_len = 0;
	char *resolved_path = NULL;
	char *final_path = NULL;

	init_fn_ptrs ();
	init_hash_table();
	if (is_relative_path(pathname))
	{
		check_fd = resolve_relative_path(pathname, 
				&resolved_path, &prefix_len);
		if (check_fd < 0) {
			goto out;
		} else {
			if (resolved_path == NULL) {
				DEBUG_PRINT("BUG : Resolved path should " 
						"not be NULL\n");
				goto out;
			}
		}
	}

	if (resolved_path == NULL)
		final_path = (char *)pathname;
	else
		final_path = resolved_path;

	check_fd = check_hash_pathname(final_path, GET_CHILD);
	if(check_fd < 0){
		DEBUG_PRINT("\n!!CAUTION!! : Program is under " 
				"attack returning error : %s\n",pathname);
		goto error;
	} else if (check_fd == 0) {
		DEBUG_PRINT("Pattern not found in the hash table for %s \n",pathname);
		ret = func_chmod(final_path, mode);
		goto out;
	
	} else {
		DEBUG_PRINT("File %s is consistent\n",pathname);
		ret = func_fchmod(check_fd, mode);
		goto out;

	}
error:
	errno = EACCES;
	ret = -1;

out:
	if (check_fd > 0)
		close(check_fd);
	if (resolved_path)
		free (resolved_path);
	return ret;
}
	
/* 
 * Interposed  chown() call that intercepts the actual chmod() call and
 * does the following:
 * 1. fstat() the given filename to get the new file descriptor
 * 2. Get the old file descriptor from the hash table 
 * 3. If found compare the old and new file descriptors and if they dont
 *    match return appropriate error message. 
 * 4. If old fd not found means the filename was not stat() before so
 *    proceed as usual.   
*/
int chown(const char *pathname, uid_t owner, gid_t group)
{
	int check_fd = 0, ret = -1;
	int prefix_len = 0;
	char *resolved_path = NULL;
	char *final_path = NULL;

	init_fn_ptrs ();
	init_hash_table();
	if (is_relative_path(pathname))
	{
		check_fd = resolve_relative_path(pathname, 
				&resolved_path, &prefix_len);
		if (check_fd < 0) {
			goto out;
		} else {
			if (resolved_path == NULL) {
				DEBUG_PRINT("BUG : Resolved path should " 
						"not be NULL\n");
				goto out;
			}
		}
	}

	if (resolved_path == NULL)
		final_path = (char *)pathname;
	else
		final_path = resolved_path;

	check_fd = check_hash_pathname(final_path, GET_CHILD);
	if(check_fd < 0) {
		DEBUG_PRINT("!CAUTION!! : Program is under " 
				"attack returning error : %s\n",pathname);
		goto error;
	} else {
		if (check_fd) {
			DEBUG_PRINT("File %s is consistentd\n",pathname);
			ret = func_fchown(check_fd,owner,group);
		} else {
			ret = func_chown(final_path, owner, group);
		}
		goto out;

	}
error:
	errno = EACCES;
	ret = -1;

out:
	if (check_fd > 0)
		close(check_fd);
	if (resolved_path)
		free (resolved_path);
	return ret;
}
