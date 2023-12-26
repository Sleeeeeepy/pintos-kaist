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
				// free (page);
				// goto err;
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
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = malloc (sizeof (frame));
	if (frame == NULL) {
		PANIC ("todo");
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
		PANIC ("todo");
		/* TODO: evict here. */
	}

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Modification - December 22, 2022 /
/* Growing the stack. */
/* To-Do: Allocate one or more anon pages to increase the stack size in such a way that the given address (addr) no longer qualifies as a faulted address.
When allocating, round down the addr to PGSIZE for handling. */

static void
vm_stack_growth (void *addr UNUSED)
{
 
    vm_alloc_page (VM_ANON | VM_MARKER_0, pg_round_down(addr), 1);
}
/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}


/* Modification - December 22, 2022 /
/* Return true on success */
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED,
                         bool user UNUSED, bool write UNUSED, bool not_present UNUSED)
{
    struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
    struct page *page = NULL;
    if (addr == NULL)
        return false;

    if (is_kernel_vaddr(addr))
        return false;
	/*When the physical page of accessed memory does not exist...*/
    if (not_present) 
    {
        /* TODO: Validate the fault */
		/*It checks whether the page fault is a valid case for stack expansion.*/
        void *rsp = f->rsp; /*In the case of user access, rsp points to the user stack.*/ 

		/*In the case of kernel access, the rsp should be obtained from the thread*/
        if (!user)           
            rsp = thread_current()->rsp;

        /*If it is a fault that can be handled by stack expansion, call vm_stack_growth.*/
        if (USER_STACK - (1 << 20) <= rsp - 8 && rsp - 8 == addr && addr <= USER_STACK)
            vm_stack_growth(addr);
        else if (USER_STACK - (1 << 20) <= rsp && rsp <= addr && addr <= USER_STACK)
            vm_stack_growth(addr);

        page = spt_find_page(spt, addr);
        if (page == NULL)
            return false;
			/*When a write request is made to a page that is not writable...*/
        if (write == 1 && page->writable == 0) 
            return false;
        return vm_do_claim_page(page);
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
	struct frame *frame = vm_get_frame ();
	if (frame == NULL) {
		return false;
	}

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	if (pml4_get_page (thread_current ()->pml4, page->va) != NULL) {
		return false;
	}

	pml4_set_page (thread_current ()->pml4, page->va, frame->kva, page->writable);
	return swap_in (page, frame->kva);
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
			// TODO: clean up dst table
			hash_clear(&dst->page_map,page_map_destruct);
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
page_map_destruct (struct hash_elem *e, void *aux) {
	struct page *page = hash_entry (e, struct page, elem);
	if (page == NULL) {
		return;
	}

	vm_dealloc_page (page);
}

static bool
page_copy_anon (struct supplemental_page_table *dst, 
				struct supplemental_page_table *src, struct page* page) {
	void *va = page->va;
	struct intr_frame *if_ = &thread_current ()->tf;
	if (!vm_alloc_page (page->operations->type, va, page->writable)) {
		return false;
	}

	if (!vm_claim_page (va)) {
		return false;
	}

	return true;
}

static bool
page_copy_file (struct supplemental_page_table *dst, 
				struct supplemental_page_table *src, struct page* page) {
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
page_copy_uninit (struct supplemental_page_table *dst, 
				struct supplemental_page_table *src, struct page* page) {
	void *va = page->va;

	if ((page->uninit.type & VM_MARKER_1)) {
		void *aux = malloc (sizeof (struct load_segment_args));
		if (aux == NULL) {
			return false;
		}
		memcpy (aux, page->uninit.aux, sizeof (struct load_segment_args));
		return vm_alloc_page_with_initializer (page->uninit.type, va, page->writable, page->uninit.init, aux);
	}
	return false;
}

