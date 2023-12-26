/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
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
bool file_backed_initializer(struct page *page, enum vm_type type, void *kva)
{
    /* Set up the handler */
    /* First, set up a handler for file-backed pages in page->operations.*/
    page->operations = &file_ops;

    struct file_page *file_page = &page->file;

    /* You may also update some information in the page struct (such as information related to the file backing the memory).*/
    struct lazy_load_arg *lazy_load_arg =page->uninit.aux;
    file_page->file = lazy_load_arg->file;
    file_page->offset = lazy_load_arg->offset;
    file_page->read_bytes = lazy_load_arg->read_bytes;
    file_page->zero_bytes = lazy_load_arg->zero_bytes;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */

static void
file_backed_destroy(struct page *page)
{
    // page struct를 해제할 필요는 없습니다. (file_backed_destroy의 호출자가 해야 함)
    struct file_page *file_page UNUSED = &page->file;
    if (pml4_is_dirty(thread_current()->pml4, page->va))
    {
        file_write_at(file_page->file, page->va, file_page->read_bytes, file_page->offset);
        pml4_set_dirty(thread_current()->pml4, page->va, 0);
    }
    pml4_clear_page(thread_current()->pml4, page->va);
}
static bool
lazy_load (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	struct lazy_load_arg *args = aux;
	struct file *file = task_find_by_tid (thread_tid ())->executable;
	uint32_t page_read_bytes = args->read_bytes;
	uint32_t page_zero_bytes = args->zero_bytes;
	uint8_t *upage = args->upage;
	bool writable = args->writable;
	bool error = false;
	file_seek (file, args->offset);
	
	if (file_read (file, page->frame->kva, args->read_bytes) != (int) page_read_bytes) {
		error = true;
		goto cleanup;
	}

	memset (page->frame->kva + page_read_bytes, 0, page_zero_bytes);

cleanup:
	// where to remove aux? 
	free (aux);
	if (error) {
		palloc_free_page (page->frame->kva);
	}
	return !error;
}
/* Do the mmap */

/* Modified here 12/25 */
void *
do_mmap(void *addr, size_t length, int writable,
        struct file *file, off_t offset)
{
    struct file *f = file_reopen(file);
    /* Used to return the virtual address where the file is mapped when mapping is successful */
    void *start_addr = addr; 
    /*Total number of pages used for this mapping*/
    int total_page_count = length <= PGSIZE ? 1 : length % PGSIZE ? length / PGSIZE + 1
                                                                  : length / PGSIZE; 

    size_t read_bytes = file_length(f) < length ? file_length(f) : length;
    size_t zero_bytes = PGSIZE - read_bytes % PGSIZE;

    ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
    ASSERT(pg_ofs(addr) == 0);    
    ASSERT(offset % PGSIZE == 0); 

    while (read_bytes > 0 || zero_bytes > 0)
    {
/* Calculate how to fill this page.
   Read PAGE_READ_BYTES bytes from the file
   and fill the remaining PAGE_ZERO_BYTES bytes with 0. */

        size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
        size_t page_zero_bytes = PGSIZE - page_read_bytes;

        struct lazy_load_arg *lazy_load_arg = (struct lazy_load_arg *)malloc(sizeof(struct lazy_load_arg));
        lazy_load_arg->file = f;
        lazy_load_arg->offset = offset;
        lazy_load_arg->read_bytes = page_read_bytes;
        lazy_load_arg->zero_bytes = page_zero_bytes;
        lazy_load_arg->writable = writable;
        lazy_load_arg->upage = addr;

        /* Call vm_alloc_page_with_initializer to create a pending object.*/
        if (!vm_alloc_page_with_initializer(VM_FILE, addr,
                                            writable, lazy_load, lazy_load_arg))
            return NULL;

        struct page *p = spt_find_page(&thread_current()->spt, start_addr);
        p->mapped_page_count = total_page_count;

        /* Advance. */
        /* Track the read bytes and zero-filled bytes and increase the virtual address. */
        read_bytes -= page_read_bytes;
        zero_bytes -= page_zero_bytes;
        addr += PGSIZE;
        offset += page_read_bytes;
    }

    return start_addr;
}

/* Do the munmap */
void do_munmap(void *addr)
{
    struct supplemental_page_table *spt = &thread_current()->spt;
    struct page *p = spt_find_page(spt, addr);
    int count = p->mapped_page_count;
    for (int i = 0; i < count; i++)
    {
        if (p)
            destroy(p);
        addr += PGSIZE;
        p = spt_find_page(spt, addr);
    }
}