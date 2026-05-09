#ifndef BCAUSE_LIST_H
#define BCAUSE_LIST_H

#include <stddef.h>

/*
 * Minimal growable array of untyped pointers.
 *
 * Ownership is intentionally not encoded here: callers decide whether the
 * pointed-to items must be freed before list_clear() or list_free().
 */
struct list {
	size_t alloc; /* number of pointer slots allocated in data */
	size_t size;  /* number of pointer slots currently in use */
	void **data;  /* pointer storage; elements are owned by the caller */
};

/* Append one pointer, growing the backing array when necessary. */
void list_push(struct list *list, void *item);

/* Forget all elements but keep the allocated backing storage. */
void list_clear(struct list *list);

/* Release the backing storage; does not free pointed-to elements. */
void list_free(struct list *list);

#endif /* BCAUSE_LIST_H */
