/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_KFENCE_H
#define _LINUX_KFENCE_H

#include <linux/types.h>

struct kmem_cache;
struct page;

#ifdef CONFIG_KFENCE
/* TODO: API documentation */

void kfence_init(void);

void *kfence_alloc_and_fix_freelist(struct kmem_cache *s, gfp_t gfp);

bool kfence_free(struct kmem_cache *s, struct page *page, void *head,
		 void *tail, int cnt, unsigned long addr);

void kfence_cache_register(struct kmem_cache *s);

void kfence_cache_unregister(struct kmem_cache *s);

bool kfence_discard_slab(struct kmem_cache *s, struct page *page);

bool kfence_handle_page_fault(unsigned long address);

bool is_kfence_ptr(unsigned long address);

void kfence_observe_memcg_cache(struct kmem_cache *memcg_cache);

#else /* CONFIG_KFENCE */

// TODO: remove for v1
// clang-format off

static inline void kfence_init(void) { }
static inline void *kfence_alloc_and_fix_freelist(struct kmem_cache *s) { return NULL; }
static inline bool kfence_free(struct kmem_cache *s, struct page *page,
			       void *head, void *tail, int cnt,
			       unsigned long addr) { return false; }
static inline void kfence_cache_register(struct kmem_cache *s)   { }
static inline void kfence_cache_unregister(struct kmem_cache *s) { }
static inline bool kfence_discard_slab(struct kmem_cache *s, struct page *page) { return false; }
static inline bool kfence_handle_page_fault(unsigned long address) { return false; }
static inline bool is_kfence_ptr(unsigned long address) { return false; }
static inline void kfence_observe_memcg_cache(struct kmem_cache *memcg_cache) { }

// TODO: remove for v1
// clang-format on

#endif /* CONFIG_KFENCE */

#endif /* _LINUX_KFENCE_H */
