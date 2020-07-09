// SPDX-License-Identifier: GPL-2.0

#include <stdarg.h>

#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/stacktrace.h>
#include <linux/string.h>

#include "kfence.h"

#define NUM_STACK_ENTRIES 64

/* Helper function to either print to a seq_file or to console. */
static void seq_con_printf(struct seq_file *seq, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	if (seq)
		seq_vprintf(seq, fmt, args);
	else
		vprintk(fmt, args);
	va_end(args);
}

bool stack_entry_matches(unsigned long addr, const char *pattern)
{
	char buf[64];
	int buf_len, len;

	buf_len = scnprintf(buf, sizeof(buf), "%ps", (void *)addr);
	len = strlen(pattern);
	if (len > buf_len)
		return false;
	if (strnstr(buf, pattern, len))
		return true;
	return false;
}

static int scroll_stack_to(const unsigned long stack_entries[], int num_entries,
			   const char *pattern)
{
	int i;

	for (i = 0; i < num_entries; i++) {
		if (stack_entry_matches(stack_entries[i], pattern))
			return (i + 1 < num_entries) ? (i + 1) : 0;
	}
	return 0;
}

static int get_stack_skipnr(const unsigned long stack_entries[], int num_entries,
			    enum kfence_error_type type)
{
	switch (type) {
	case KFENCE_ERROR_UAF:
	case KFENCE_ERROR_OOB:
		return scroll_stack_to(stack_entries, num_entries, "asm_exc_page_fault");
	case KFENCE_ERROR_CORRUPTION:
		return scroll_stack_to(stack_entries, num_entries, "__slab_free");
	}
	return 0;
}

static void kfence_dump_stack(struct seq_file *seq, struct kfence_alloc_metadata *obj,
			      bool is_alloc)
{
	unsigned long *entries = is_alloc ? obj->stack_alloc : obj->stack_free;
	unsigned long nr_entries = is_alloc ? obj->nr_alloc : obj->nr_free;

	if (nr_entries) {
		/*
		 * Unfortunately stack_trace_seq_print() does not exist, and we
		 * require a temporary buffer for printing the stack trace. We
		 * expect that printing KFENCE object information is serialized
		 * under the KFENCE lock.
		 */
		static char buf[PAGE_SIZE];

		stack_trace_snprint(buf, sizeof(buf), entries, nr_entries, 0);
		seq_con_printf(seq, "%s\n", buf);
	} else {
		seq_con_printf(seq, "  no %s stack.\n", is_alloc ? "allocation" : "deallocation");
	}
}

void kfence_dump_object(struct seq_file *seq, int obj_index, struct kfence_alloc_metadata *obj)
{
	int size = abs(obj->size);
	unsigned long start = obj->addr;
	struct kmem_cache *cache;

	seq_con_printf(seq, "Object #%d: starts at %px, size=%d\n", obj_index, (void *)start, size);
	seq_con_printf(seq, "allocated at:\n");
	kfence_dump_stack(seq, obj, true);

	if (kfence_metadata[obj_index].state == KFENCE_OBJECT_FREED) {
		seq_con_printf(seq, "freed at:\n");
		kfence_dump_stack(seq, obj, false);
	}

	cache = kfence_metadata[obj_index].cache;
	if (cache && cache->name)
		seq_con_printf(seq, "Object #%d belongs to cache %s\n", obj_index, cache->name);
}

static void kfence_print_object(int obj_index, struct kfence_alloc_metadata *obj)
{
	kfence_dump_object(NULL, obj_index, obj);
}

static void dump_bytes_at(unsigned long addr)
{
	unsigned char *c = (unsigned char *)addr;
	unsigned char *max_addr = (unsigned char *)min(ALIGN(addr, PAGE_SIZE), addr + 16);
	pr_err("Bytes at %px:", (void *)addr);
	for (; c < max_addr; c++)
		pr_cont(" %02X", *c);
	pr_cont("\n");
}

void kfence_report_error(unsigned long address, int obj_index, struct kfence_alloc_metadata *object,
			 enum kfence_error_type type)
{
	unsigned long stack_entries[NUM_STACK_ENTRIES] = { 0 };
	int num_stack_entries = stack_trace_save(stack_entries, NUM_STACK_ENTRIES, 1);
	int skipnr = get_stack_skipnr(stack_entries, num_stack_entries, type);
	bool is_left;

	pr_err("==================================================================\n");
	switch (type) {
	case KFENCE_ERROR_OOB:
		is_left = address < object->addr;
		pr_err("BUG: KFENCE: slab-out-of-bounds in %pS\n", (void *)stack_entries[skipnr]);
		pr_err("Memory access at address %px to the %s of object #%d\n", (void *)address,
		       is_left ? "left" : "right", obj_index);
		break;
	case KFENCE_ERROR_UAF:
		pr_err("BUG: KFENCE: use-after-free in %pS\n", (void *)stack_entries[skipnr]);
		pr_err("Memory access at address %px\n", (void *)address);
		break;
	case KFENCE_ERROR_CORRUPTION:
		pr_err("BUG: KFENCE: memory corruption in %pS\n", (void *)stack_entries[skipnr]);
		pr_err("Invalid write detected at address %px\n", (void *)address);
		dump_bytes_at(address);
		break;
	}

	stack_trace_print(stack_entries + skipnr, num_stack_entries - skipnr, 0);
	pr_err("\n");
	kfence_print_object(obj_index, object);
	pr_err("==================================================================\n");
}
