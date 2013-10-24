#ifndef HASH_LIB
#define HASH_LIB

struct list_head {
	struct list_head *next;
	struct list_head *prev;
};

struct hash_entry {
	int inode;
	struct list_head hlist;
	char fname[];
};

/* Stack structure for chdir */
struct chdir_stack {
	int fd;
	struct chdir_stack *next;
};
typedef struct chdir_stack c_stack;

extern c_stack *cur_dir_stk;
extern struct list_head *head_node;

#define HASH_MOD	32
#ifdef DEBUG
#define DEBUG_PRINT(fmt, ...)	\
		printf("\n%s: " fmt, __FUNCTION__,  __VA_ARGS__); 
#else
#define DEBUG_PRINT(fmt, ...) 
#endif
#define INIT_LIST_HEAD(ENTRY) \
	do { 	\
		(ENTRY)->next = ENTRY;	\
		(ENTRY)->prev = ENTRY;	\
	} while (0);


#define IS_STACK_EMPTY(top) (top == NULL)
/* 
 * Prototype of all the hashing functions
 */
void init_hash_table (void) ;
int insert_inode_fname_hash(const char *fname, int inode); 
void delete_inode_fname_hash(const char *fname);
int search_inode(const char *fname);

int inline push_fd_cstack (int fd, c_stack **top);
int inline pop_fd_cstack (c_stack **top);
void delete_fd_cstack (c_stack **top);
inline int get_top_c_stack(void);

int inline push_node_cstack (c_stack *node, c_stack **top);
int inline pop_node_cstack (c_stack **node, c_stack **top);
#endif
