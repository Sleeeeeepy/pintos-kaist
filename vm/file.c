/* file.c: Implementation of memory backed file object (mmaped object). */
#include <string.h>
#include "vm/vm.h"
#include "userprog/process.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
	return false;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
	return false;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
	if (pml4_is_dirty (thread_current ()->pml4, page->va)) {
		file_write_at (page->file.file, page->va, page->file.read_bytes, page->file.offset);
		pml4_set_dirty (thread_current ()->pml4, page->va, 0);
	}

	if (file_page->file_deallocatable) {
		file_close (file_page->file);
		file_page->file = NULL;
	}
	free (page->frame);
	page->frame = NULL;
}

bool
lazy_load_mmap (struct page *page, void *aux) {
	struct lazy_load_args *args = aux;
	struct file *file = args->file;
	uint32_t page_read_bytes = args->read_bytes;
	uint32_t page_zero_bytes = args->zero_bytes;
	uint8_t *addr = args->addr;
	bool writable = args->writable;
	bool error = false;
	bool file_deallocatable = args->file_deallocatable;

	page->file.file = file;
	page->file.file_deallocatable = file_deallocatable;
	page->file.offset = args->offset;
	page->file.read_bytes = args->read_bytes;
	file_seek (file, args->offset);
	
	if (file_read (file, page->frame->kva, args->read_bytes) != (int) page_read_bytes) {
		error = true;
		goto cleanup;
	}

	memset (page->frame->kva + page_read_bytes, 0, page_zero_bytes);

cleanup:
	free (aux);
	if (error) {
		palloc_free_page (page->frame->kva);
		// do we need to deallocate frame?
		free (page->frame);
		page->frame = NULL;
	}
	return !error;
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	void *ret = addr;
	size_t read_bytes = (file_length (file)) > length ? length : file_length (file);
	size_t zero_bytes = PGSIZE - read_bytes % PGSIZE;
	bool file_deallocatable = true;
	file_seek (file, offset);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		struct lazy_load_args *aux = malloc (sizeof (struct lazy_load_args));
		*aux = (struct lazy_load_args) {
			.file = file,
		 	.offset = offset,
			.read_bytes = page_read_bytes,
			.zero_bytes = page_zero_bytes,
			.writable = writable,
			.addr = addr,
			.file_deallocatable = file_deallocatable
		};
		if (!vm_alloc_page_with_initializer (VM_FILE | VM_MARKER_2, addr,
					writable, lazy_load_mmap, (void *) aux))
			return NULL;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr += PGSIZE;
		
		/* Update offset. */
		offset += page_read_bytes;
		file_deallocatable = false;
	}
	return ret;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	while (true) {
		struct page* page = spt_find_page(&thread_current()->spt, addr);
		if (page == NULL) {
			break;
		}

		if (pml4_is_dirty (thread_current ()->pml4, page->va)) {
			file_write_at (page->file.file, addr, page->file.read_bytes, page->file.offset);
			pml4_set_dirty (thread_current ()->pml4, page->va, 0);
		}

		pml4_clear_page(thread_current()->pml4, page->va);
		addr += PGSIZE;
	}
}
