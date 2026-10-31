/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Heap: first fit over an address-ordered free list, neighbours merged on
 * free (the scheme of FreeRTOS heap_4, which CIX uses).
 */
#include "se.h"

#define HEAP_SIZE	0x8000
#define ALIGN		8

typedef struct block {
	struct block *next;	/* free list only */
	size_t size;		/* including this header; top bit: allocated */
} block_t;

#define USED		((size_t)1 << 31)
#define HDR		((sizeof(block_t) + ALIGN - 1) & ~(size_t)(ALIGN - 1))

static uint8_t g_heap[HEAP_SIZE] __attribute__((aligned(ALIGN), section(".heap")));
static block_t g_start, *g_end;

void system_heap_init(void)
{
	block_t *first = (block_t *)g_heap;

	g_end = (block_t *)(g_heap + HEAP_SIZE - HDR);
	g_end->size = 0;
	g_end->next = NULL;
	first->size = (uint8_t *)g_end - g_heap;
	first->next = g_end;
	g_start.next = first;
	g_start.size = 0;
}

static void insert_free(block_t *b)
{
	block_t *p;

	for (p = &g_start; p->next < b; p = p->next)
		;
	if ((uint8_t *)p + p->size == (uint8_t *)b) {
		p->size += b->size;
		b = p;
	}
	if ((uint8_t *)b + b->size == (uint8_t *)p->next && p->next != g_end) {
		b->size += p->next->size;
		b->next = p->next->next;
	} else {
		b->next = p->next;
	}
	if (p != b)
		p->next = b;
}

void *malloc(size_t size)
{
	block_t *p, *b, *rest;
	uint32_t irq;

	if (size == 0 || size >= USED - HDR - ALIGN)
		return NULL;
	if (g_end == NULL)
		system_heap_init();
	size = (size + HDR + ALIGN - 1) & ~(size_t)(ALIGN - 1);
	__asm__ volatile("mrs %0, primask\ncpsid i" : "=r"(irq) :: "memory");
	for (p = &g_start, b = p->next; b != g_end && b->size < size; p = b, b = b->next)
		;
	if (b == g_end) {
		b = NULL;
	} else {
		p->next = b->next;
		if (b->size - size > 2 * HDR) {
			rest = (block_t *)((uint8_t *)b + size);
			rest->size = b->size - size;
			b->size = size;
			insert_free(rest);
		}
		b->size |= USED;
		b->next = NULL;
	}
	if (!irq)
		__asm__ volatile("cpsie i" ::: "memory");
	return b ? (uint8_t *)b + HDR : NULL;
}

void free(void *ptr)
{
	block_t *b;
	uint32_t irq;

	if (ptr == NULL)
		return;
	b = (block_t *)((uint8_t *)ptr - HDR);
	if (!(b->size & USED))
		return;
	b->size &= ~USED;
	__asm__ volatile("mrs %0, primask\ncpsid i" : "=r"(irq) :: "memory");
	insert_free(b);
	if (!irq)
		__asm__ volatile("cpsie i" ::: "memory");
}

void *calloc(size_t n, size_t size)
{
	void *p;

	if (size && n > (size_t)-1 / size)
		return NULL;
	p = malloc(n * size);
	if (p)
		memset(p, 0, n * size);
	return p;
}
