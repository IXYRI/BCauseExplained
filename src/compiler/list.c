#include "list.h"

#include <stdlib.h>
#include <memory.h>

void list_push(struct list *list, void *item) {
	if (!list->alloc) list->data = malloc((list->alloc = 32) * sizeof(void *));
	else if (list->alloc - list->size < 2) list->data = realloc(list->data, (list->alloc *= 2) * sizeof(void *));

	/* Store pointer only; element ownership stays with the caller. */
	list->data [list->size++] = item;
}

void list_clear(struct list *list) {
	memset(list->data, 0, list->size * sizeof(void *));

	/* Keep allocation for reuse. */
	list->size = 0;
}

void list_free(struct list *list) {
	/* Free only the pointer array, not the elements. */
	if (list->alloc) free(list->data);

	list->size = list->alloc = 0;
}
