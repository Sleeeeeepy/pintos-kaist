/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include <bitmap.h>

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static struct bitmap *swap_table;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get (1, 1);
	swap_table = bitmap_create (disk_size (swap_disk) / (PGSIZE / DISK_SECTOR_SIZE));
	if (swap_table == NULL) {
		PANIC ("Failed to initialize swap table.");
	}
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;
	struct anon_page *anon_page = &page->anon;
	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
	size_t pos = anon_page->swap_idx;

	if (!bitmap_test (swap_table, pos)) {
		return false;
	}

	for (size_t i = 0; i < (PGSIZE / DISK_SECTOR_SIZE); i++) {
		disk_read (swap_disk, pos * (PGSIZE / DISK_SECTOR_SIZE) + i, page->frame->kva + DISK_SECTOR_SIZE * i);
	}

	bitmap_set (swap_table, pos, false);

	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	struct thread *thread = thread_find (page->tid);
	size_t pos = bitmap_scan (swap_table, 0, 1, false);
	if (thread == NULL) {
		return false;
	}
	
	if (pos == BITMAP_ERROR) {
		return false;
	}

	for (size_t i = 0; i < (PGSIZE / DISK_SECTOR_SIZE); i++) {
		disk_write (swap_disk, pos * (PGSIZE / DISK_SECTOR_SIZE) + i, page->va + DISK_SECTOR_SIZE * i);
	}

	bitmap_set (swap_table, pos, true);
	anon_page->swap = true;
	anon_page->swap_idx = pos;
	pml4_clear_page (thread->pml4, page->va);
	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	if (page->frame != NULL) {
		frame_return (page->frame);
		page->frame = NULL;
	}
}
