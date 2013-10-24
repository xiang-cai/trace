#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <malloc.h>
#include "hash_lib.h"


struct list_head *head_node = NULL;

int __search_inode(const char *fname, struct hash_entry **found_entry);

/* List manipulation functions */
void list_add_head(struct list_head *new, struct list_head *prev, 
		struct list_head *next)
{
	new->next = prev->next;
	new->prev = prev;
	next->prev = new;
	prev->next = new;
}

void list_del_entry (struct list_head *entry)
{
	struct list_head *next = entry->next;
	struct list_head *prev = entry->prev;

	next->prev = prev;
	prev->next = next;
}

/* Hash table manipulation functions */
void init_hash_table (void) 
{
	int i;	
	struct list_head *head;

	if (head_node != NULL)	
		return;
	head_node = malloc (HASH_MOD * sizeof(struct list_head)); 

	/* Error handling should take place at the caller's end */
	if (!head_node)
	{
		printf("\n Allocation failed");
		return;
	}
	for (i = 0; i < HASH_MOD ; i++) {	
		head = &head_node[i];
		INIT_LIST_HEAD(head); 	
	}	
}

/*Function to store the inode number and filepath in the hash table*/
int insert_inode_fname_hash(const char *fname, int inode)
{
	int ret = 0;
	int hash_index = 0;
	int i = 0;
	struct hash_entry *entry = NULL;
	struct list_head *head;
	ret = __search_inode(fname, &entry);

	/* Do not replace existing entries. 
	 * Only link call can do that.
	 */	
	if (entry != NULL)
	{
		DEBUG_PRINT("Found inode -- %d, %s",entry->inode, entry->fname);
		return 0;
	}

	int entry_len = strlen(fname) + sizeof(struct hash_entry) + 1;

	entry = malloc (entry_len);
	if (!entry)
		return -1;

	entry->inode = inode;
	
	strcpy (entry->fname, fname);
	INIT_LIST_HEAD (&(entry->hlist));		

	/* Calculate the hash on the basis of fname MOD 32 */
	while (entry->fname[i] != '\0') {
		hash_index += (int) entry->fname[i];
		i++;
	}
	
	hash_index = hash_index%HASH_MOD;

	/* Insert the node at the index of _head_node */	
	head = &head_node[hash_index];
	list_add_head(&(entry->hlist), head, (head->next));

	DEBUG_PRINT("fname : %s entry_len : %d, hash_index : %d, "
			"inode : %d\n", fname, entry_len, hash_index, inode);
	return ret;
}


/* Function to search for inode number corresponding to input file path 
	Return value of 0 means the entry is not present */
int __search_inode(const char *fname, struct hash_entry **found_entry)
{
	int hash_index = 0;
	int i = 0;
	struct hash_entry *entry;
	struct list_head *head, *node;
	int inode = -1;
	*found_entry = NULL;

	if (head_node == NULL)	
		return -1;

	/* Calculate the hash on the basis of fname MOD 32 */
	while (fname[i] != '\0') {
		hash_index += (int) fname[i];
		i++;
	}
	
	hash_index = hash_index%HASH_MOD;
	
	/* Find the file name in the buckets of this hash */
	head = &head_node[hash_index];
	for (node = head->next; node != head; node = node->next )
	{
		/* Extract the element and compare the filename */
		//list_entry (node, struct hash_entry, hlist);

		const typeof(((struct hash_entry *)0)->hlist) *mptr = (node); 
		entry = (struct hash_entry *) 
			((char *) mptr - offsetof(struct hash_entry, hlist));
 
		if (strcmp (entry->fname, fname) == 0)
		{
			inode = entry->inode;
			*found_entry = entry;
			break;		
		}
	}

	DEBUG_PRINT("Index : %d, fname : %s, inode: %d",hash_index, fname, inode);
	return inode;
}

/* Function to search for inode number corresponding to input file path 
	Return value of 0 means the entry is not present */
int search_inode(const char *fname)
{
	int inode;
	struct hash_entry *found_entry;
	inode = __search_inode(fname, &found_entry);
	return inode;
}

void delete_inode_fname_hash(const char *fname)
{
	int inode;
	struct hash_entry *found_entry;
	inode = __search_inode(fname, &found_entry);
	
	if (found_entry != NULL)
	{
		DEBUG_PRINT("Found inode -- %d, %s",found_entry->inode, found_entry->fname);
		list_del_entry(&(found_entry->hlist));	
		INIT_LIST_HEAD(&(found_entry->hlist));
		free (found_entry);
	}
	return;
}

/* Stack manipulation functions - For chdir */

int inline push_fd_cstack (int fd, c_stack **top)
{
	c_stack *cstack_elem;
	cstack_elem = (c_stack *) malloc (sizeof(c_stack));
	if (!cstack_elem)
		return -1;

	cstack_elem->fd = fd;
	cstack_elem->next = *top;
	*top = cstack_elem;

	return 0;
}

int inline pop_fd_cstack (c_stack **top)
{
	int fd;

	c_stack *cstack_elem;
	if (! (cstack_elem = *top)) {
		return 0;
	}

	fd = cstack_elem->fd;
	close(fd);
	*top = cstack_elem->next;
	free(cstack_elem);

	return fd;
}

void delete_fd_cstack (c_stack **top)
{
	c_stack *next_elem;
	while (*top) {
		next_elem = (*top)->next;
		free (*top);
		*top = next_elem;
	}
}


/* Push / pop variants with actual nodes -- useful to move 
 * the node from one stack to another 
 */
int inline push_node_cstack (c_stack *node, c_stack **top)
{
	node->next = *top;
	*top = node;

	return 0;
}

int inline pop_node_cstack (c_stack **node, c_stack **top)
{
	
	c_stack *cstack_elem;
	int fd;

	if (! (cstack_elem = *top)) {
		*node = NULL;
		return 0;
	}

	fd = cstack_elem->fd;
	*node = cstack_elem;
	*top = cstack_elem->next;

	return fd;

}
