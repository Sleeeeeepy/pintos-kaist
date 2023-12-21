/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/mmu.h"
static struct list frame_list;
static struct lock frame_lock;

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
	list_init(&frame_list);
	lock_init(&frame_lock);
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
static struct frame _victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */

/* Added here on 12/18 - Function to find a page using the hash_elem member within the page and
insert a hash value into the virtual address of the page (hash_hash_func role) */
unsigned
page_hash (const struct hash_elem *p_, void *aux ) {
	const struct page *p = hash_entry (p_, struct page, hash_elem);
	return hash_bytes (p->va, sizeof(p->va));
}

/* Added here on 12/18 - Function to compare page address values for two page elements within the hash table (hash_less_func role) */
page_less(const struct hash_elem *a, const struct hash_elem *b,void *aux){
	const struct page *p_a = hash_entry (a,struct page,hash_elem);
	const struct page *p_b = hash_entry (b,struct page, hash_elem);
	return p_a->va < p_b->va;
}


/* Added here on 12/18
Function to insert the hash_elem structure associated with a particular page into the hash table.
This function takes the hash_elem structure from the specified page and inserts it into the hash table as an argument. */
bool
page_insert (struct hash *h, struct page *p){
	return hash_insert (h, &p->hash_elem) == NULL;
}
/* Added here on 12/18 - page_delete */
bool
page_delete(struct hash *h, struct page *p){
	return hash_delete(&h, &p->hash_elem) != NULL;
}

bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */

		/* TODO: Insert the page into the spt. */
	}
err:
	return false;
}

/* Modified here on 12/18
Define a function to find the struct page corresponding to va in the specified spt:

Create a page with the starting address to which va belongs.
Check if this page exists in the spt.
If it exists, return the page; otherwise, return NULL. */
/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt, void *va) {
	
	/* TODO: Fill this function. */
	struct page page;
	page.va=pg_round_down(va);
	struct hash_elem *e=hash_find(&spt->pages, &page.hash_elem);
	if(e==NULL)return NULL;
	struct page* result=hash_entry(e, struct page, hash_elem);
	ASSERT((va<result->va+PGSIZE)&& va>=result->va);
	return result;
}

/* Insert PAGE into spt with validation. */

/* Modified here on 12/18
Function to insert a page into the specified spt.
The hash_insert function finds the bucket where the page should go and checks if the page already exists.
If it doesn't exist, it inserts the page into the bucket. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,


		struct page *page UNUSED) {
	int succ = false;
	/* TODO: Fill this function. */
	if(!hash_insert(&spt->pages,&page->hash_elem))
		succ=true;
	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
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
	
/* Modified here on 12/18 - Declaration of the frame table to be used globally */
	struct frame *frame = (struct frame*)calloc(1,sizeof(struct frame));
	/* TODO: Fill this function. */

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	frame->kva=palloc_get_page(PAL_USER);
	if(frame->kva==NULL){
		PANIC("to do");
	}
	list_push_back(&frame_list,&frame->element);
	frame->page=NULL;
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
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

	return vm_do_claim_page (page);
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
	struct page *page = NULL;
	/* TODO: Fill this function */

/* Modified here - 12/18
Retrieve the page containing the argument virtual address (va) from the spt of the currently running process.
Then, call vm_do_claim_page() for that page. */
	page=spt_find_page(&thread_current()->spt, pg_round_down(va));
	if(page==NULL){
		return false;
	}
	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {

/* Modified here on 12/18 - Claiming refers to allocating a physical frame for a page.
It means allocating a physical frame for a page in the user's virtual memory space.

Call vm_get_frame to obtain a new frame.
Link it with the received page as an argument.
Map the virtual address to the physical address using pml4_set_page.
If successful, return true; otherwise, return false if swap_in fails. */
	struct frame *frame = vm_get_frame ();
	struct thread *t=thread_current();
	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	if(pml4_get_page(t->pml4,page->va)==NULL && pml4_set_page(t->pml4,page->va,frame->kva,page->writable)){
		return swap_in(page,frame->kva);
	}
	return swap_in (page, frame->kva);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	/* 여기 수정함 12/18*/
	hash_init(&spt->pages,page_hash,page_less,NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
}
