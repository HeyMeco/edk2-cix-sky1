/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The heap between .bss and the stack (pm.ld): first fit over a list of
 * free blocks kept in address order, neighbours merged on free.  SCP-
 * firmware allocates its contexts once at start-up; afterwards only short
 * buffers (fwk_io_vprintf) come and go.
 */
#include <stdlib.h>
#include <string.h>

#include "pm.h"

struct block {
	size_t size;			/* bytes, this header included */
	struct block *next;		/* free blocks only */
};

#define ALIGN			8u
#define HDR			((sizeof(struct block) + ALIGN - 1) & ~(ALIGN - 1))

extern char _heap_start[], _heap_end[];
static struct block *free_list;
static bool ready;

static void heap_init(void)
{
	uintptr_t lo = ((uintptr_t)_heap_start + ALIGN - 1) & ~(uintptr_t)(ALIGN - 1);

	free_list = (struct block *)lo;
	free_list->size = (uintptr_t)_heap_end - lo;
	free_list->next = NULL;
	ready = true;
}

void *malloc(size_t n)
{
	struct block **p, *b;
	size_t need;

	if (!ready)
		heap_init();
	if (n == 0 || n > (size_t)(_heap_end - _heap_start))
		return NULL;
	need = (n + HDR + ALIGN - 1) & ~(size_t)(ALIGN - 1);
	for (p = &free_list; (b = *p) != NULL; p = &b->next) {
		if (b->size < need)
			continue;
		if (b->size - need >= HDR + ALIGN) {
			struct block *rest = (struct block *)((char *)b + need);

			rest->size = b->size - need;
			rest->next = b->next;
			*p = rest;
			b->size = need;
		} else {
			*p = b->next;
		}
		return (char *)b + HDR;
	}
	return NULL;
}

void free(void *ptr)
{
	struct block *b, **p, *prev = NULL;

	if (ptr == NULL)
		return;
	b = (struct block *)((char *)ptr - HDR);
	for (p = &free_list; *p && *p < b; p = &(*p)->next)
		prev = *p;
	b->next = *p;
	*p = b;
	if (b->next && (char *)b + b->size == (char *)b->next) {
		b->size += b->next->size;
		b->next = b->next->next;
	}
	if (prev && (char *)prev + prev->size == (char *)b) {
		prev->size += b->size;
		prev->next = b->next;
	}
}

void *calloc(size_t num, size_t size)
{
	void *p;

	if (size && num > (size_t)-1 / size)
		return NULL;
	p = malloc(num * size);
	if (p)
		memset(p, 0, num * size);
	return p;
}

void *realloc(void *ptr, size_t n)
{
	size_t old;
	void *p;

	if (ptr == NULL)
		return malloc(n);
	if (n == 0) {
		free(ptr);
		return NULL;
	}
	old = ((struct block *)((char *)ptr - HDR))->size - HDR;
	if (old >= n)
		return ptr;
	p = malloc(n);
	if (p) {
		memcpy(p, ptr, old);
		free(ptr);
	}
	return p;
}

void *aligned_alloc(size_t alignment, size_t size)
{
	/* SCP asks for at most 8 */
	return alignment <= ALIGN ? malloc(size) : NULL;
}
