/* vm.c: Generic interface for virtual memory objects. */
#include "vm/vm.h"
#include <debug.h>
#include <hash.h>
#include <string.h>
#include "threads/malloc.h"
#include "threads/mmu.h"
#include "userprog/process.h"
#include "vm/inspect.h"
#include "filesys/page_cache.h"

#define FRAME_POOL_SIZE 100
static struct page *page_lookup (struct supplemental_page_table *spt, void *addr);
static bool page_less (const struct hash_elem *a_,
					const struct hash_elem *b_, void *aux UNUSED);
static uint64_t page_hash (const struct hash_elem *p_, void *aux UNUSED);
static void page_map_destruct (struct hash_elem *e, void *aux);
static bool page_copy_anon (struct supplemental_page_table *dst, 
				struct supplemental_page_table *src, struct page* page);
static bool page_copy_file (struct supplemental_page_table *dst, 
				struct supplemental_page_table *src, struct page* page);
static bool page_copy_uninit (struct supplemental_page_table *dst, 
				struct supplemental_page_table *src, struct page* page);
static inline bool is_within_stack_boundary (uintptr_t addr, uintptr_t rsp);
static void frame_pool_init (size_t size);
static struct list frame_pool;
static struct lock frame_lock;
static size_t frame_pool_size;

/* List of frames in use */
static struct list frame_list;
static struct list_elem *next_victim = NULL;
/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	frame_pool_init (FRAME_POOL_SIZE);
	list_init (&frame_list);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		struct page *page = malloc (sizeof (struct page));
		void *initializer = NULL;

		if (page == NULL) {
			goto err;
		}

		switch (VM_TYPE (type)) {
			case VM_UNINIT:
			default:
				NOT_REACHED ();
			case VM_ANON:
				initializer = anon_initializer;
				break;
			case VM_FILE:
				initializer = file_backed_initializer;
				break;
			case VM_PAGE_CACHE:
				initializer = page_cache_initializer;
				break;
		}	
		uninit_new (page, upage, init, type, aux, initializer);
		page->writable = writable;
		page->tid = thread_tid ();
		/* TODO: Insert the page into the spt. */
		if (!spt_insert_page (spt, page)) {
			free (page);
			free (aux);
			goto err;
		}

		return true;
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page;
	/* TODO: Fill this function. */
	if ((page = page_lookup (spt, va)) == NULL) {
		return NULL;
	}
	return page;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	return hash_insert (&spt->page_map, &page->elem) == NULL;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	bool found = false;
	lock_acquire (&frame_lock);
	victim = list_entry (list_pop_front (&frame_list), struct frame, felem);
	lock_release (&frame_lock);
	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	ASSERT (victim != NULL);
	ASSERT (victim->page != NULL);
	swap_out (victim->page);
	return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = frame_get ();
	if (frame == NULL) {
		/* we need sizeof (struct frame) / 4096 * MEM_SIZE bytes of
		 * pre-allocated block in kernel pool. for example, if total memory of
		 * the system is 4 MB, and the size of metadata is 16 byte
		 * then we need 16384 bytes to map all frames.
		 */

		/* or return NULL. */
		return NULL;
	}

	void *kva = palloc_get_page (PAL_USER | PAL_ZERO);
	frame->kva = kva;
	frame->page = NULL;
	if (frame->kva == NULL) {
		frame_return (frame);
		struct frame *victim = vm_evict_frame ();
		victim->page = NULL;
		frame = victim;
	}

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

void
vm_free_frame (struct frame *frame, bool cleanup) {
	return;
}

/* Growing the stack. */
static bool
vm_stack_growth (void *addr UNUSED) {
	if (!vm_alloc_page (VM_ANON | VM_MARKER_0, pg_round_down (addr), true)) {
		return false;
	}

	if (!vm_claim_page (pg_round_down (addr))) {
		return false;
	}

	return true;
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	if (addr == NULL) {
		return false;
	}

	/* If fault address is kernel page, then it's a kernel bug. */
	if (is_kernel_vaddr (addr)) {
		return false;
	}

	/* It's present, but page fault occured. it's also a bug. */
	if (!not_present) {
		return false;
	}

	/* If the page is exist, claim it. */
	if ((page = spt_find_page (spt, addr)) != NULL) {
		return vm_do_claim_page (page);
	}

	/* Try stack growth */
	uintptr_t rsp = f->rsp;
	if (!user) {
		rsp = thread_current ()->intr_rsp;
	}

	if (is_within_stack_boundary ((uintptr_t) addr, rsp)) {
		return vm_stack_growth (addr);
	}

	return false;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = spt_find_page (&thread_current ()->spt, va);
	/* TODO: Fill this function */
	if (page == NULL) {
		return false;
	}

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	bool success = false;
	struct frame *frame = vm_get_frame ();
	if (frame == NULL) {
		goto end;
	}

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	if (pml4_get_page (thread_current ()->pml4, page->va) != NULL) {
		goto end;
	}

	if (!pml4_set_page (thread_current ()->pml4, page->va, frame->kva, page->writable)) {
		goto end;
	}

	if (!(success = swap_in (page, frame->kva))) {
		goto end;
	}

end:
	if (!success) {
		frame_return (frame);
		return success;
	}

	lock_acquire (&frame_lock);
	list_push_back (&frame_list, &frame->felem);
	lock_release (&frame_lock);
	return success;
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init (&spt->page_map, page_hash, page_less, NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
	struct hash_iterator iter;
	hash_clear (&dst->page_map, page_map_destruct);
	hash_first (&iter, &src->page_map);
	bool success = false;
	while (hash_next (&iter)) {
		struct page *page = hash_entry (hash_cur (&iter), struct page, elem);
		enum vm_type type = VM_TYPE (page->operations->type);
		switch (type) {
			case VM_ANON:
				success = page_copy_anon (dst, src, page);
				break;
			case VM_FILE:
				success = page_copy_file (dst, src, page);
				break;
			case VM_UNINIT:
				success = page_copy_uninit (dst, src, page);
				break;
			default:
				PANIC ("Unkown page type: %d", type);
		}

		if (!success) {
			goto fail;
		}

		if (type != VM_UNINIT) {
			struct page *cpage = spt_find_page (dst, page->va);
			if (cpage == NULL) {
				success = false;
				goto fail;
			}
			memcpy (cpage->frame->kva, page->frame->kva, PGSIZE);
		}
	}

fail:
	if (!success) {
		hash_clear (&dst->page_map, page_map_destruct);
	}
	return success;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread */
	hash_clear (&thread_current ()->spt.page_map, page_map_destruct);
	/* TODO: writeback all the modified contents to the storage. */
}

/* Returns the page containing the given virtual address, or a null pointer if no such page exists. */
static struct page *
page_lookup (struct supplemental_page_table *spt, void *addr) {
	struct page p;
	struct hash_elem *e;
	p.va = pg_round_down (addr);
	e = hash_find (&spt->page_map, &p.elem);
	return e != NULL ? hash_entry (e, struct page, elem) : NULL;
}

/* Returns true if page a precedes page b. */
static bool
page_less (const struct hash_elem *a_,
		   const struct hash_elem *b_, void *aux UNUSED) {
	const struct page *a = hash_entry (a_, struct page, elem);
	const struct page *b = hash_entry (b_, struct page, elem);

	return a->va < b->va;
}

/* Returns a hash value for page p. */
static uint64_t
page_hash (const struct hash_elem *p_, void *aux UNUSED) {
	const struct page *p = hash_entry (p_, struct page, elem);
	return hash_bytes (&p->va, sizeof p->va);
}

static void
page_map_destruct (struct hash_elem *e, void *aux UNUSED) {
	struct page *page = hash_entry (e, struct page, elem);
	if (page == NULL) {
		return;
	}
	
	vm_dealloc_page (page);
}

static bool
page_copy_anon (struct supplemental_page_table *dst UNUSED, 
				struct supplemental_page_table *src UNUSED, struct page* page) {
	void *va = page->va;
	if (!vm_alloc_page (page->operations->type, va, page->writable)) {
		return false;
	}

	if (!vm_claim_page (va)) {
		return false;
	}

	return true;
}

static bool
page_copy_file (struct supplemental_page_table *dst UNUSED, 
				struct supplemental_page_table *src UNUSED, struct page* page) {
	void *va = page->va;
	if (!vm_alloc_page (page->operations->type, va, page->writable)) {
		return false;
	}

	if (!vm_claim_page (va)) {
		return false;
	}

	return true;
}

static bool
page_copy_uninit (struct supplemental_page_table *dst UNUSED, 
				  struct supplemental_page_table *src UNUSED, struct page* page) {
	void *va = page->va;

	if ((page->uninit.type & VM_MARKER_1)) {
		void *aux = malloc (sizeof (struct lazy_load_args));
		if (aux == NULL) {
			return false;
		}
		memcpy (aux, page->uninit.aux, sizeof (struct lazy_load_args));
		return vm_alloc_page_with_initializer (page->uninit.type, va, page->writable, page->uninit.init, aux);
	}
	return false;
}

static inline bool
is_within_stack_boundary (uintptr_t addr, uintptr_t rsp) {
	static const uintptr_t boundary = USER_STACK - MAX_STACK_SIZE;
	return	boundary <= addr && USER_STACK >= addr && rsp - 8 <= addr;
}

static void
frame_pool_init (size_t size) {
	lock_init (&frame_lock);
	list_init (&frame_pool);
	for (size_t i = 0; i < size; i++) {
		struct frame *frame = calloc (1, sizeof (struct frame));
		if (frame == NULL) {
			PANIC ("Failed to initalize frame pool.");
		}
		list_push_back (&frame_pool, &frame->elem);
		frame_pool_size += 1;
	}
}

struct frame *
frame_get (void) {
	struct frame *frame = NULL;
	lock_acquire (&frame_lock);
	if (frame_pool_size > 0) {
		frame = list_entry (list_pop_front (&frame_pool), struct frame, elem);
		frame_pool_size -= 1;
		memset (frame, 0x00, sizeof (struct frame));
	} else {
		frame = calloc (1, sizeof (struct frame));
	}
	lock_release (&frame_lock);
	return frame;
}

void
frame_return (struct frame *frame) {
	lock_acquire (&frame_lock);
	frame_pool_size += 1;
	frame->page = NULL;
	list_push_back (&frame_pool, &frame->elem);
	lock_release (&frame_lock);
}