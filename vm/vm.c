/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"

#include "thread.h"
#include "interrupt.h"
#include "vaddr.h"
#include "threads/palloc.h"

const MAX_STACK_SIZE = 1 << 20;

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
 * `vm_alloc_page`.
 * 1. Check if the passed type is valid, i.e., not VM_UNINIT, then obtain the current thread's spt, and check the page at the given virtual address. This part is already implemented.
 * 2. Create a new page and call the initializer function based on vm_type.
 * 2-1. Use the uninit_new function to create an uninit page structure.
 * 2-2. Modify the necessary fields.
 * 3. Insert the created page into the supplemental page table.
 * 4. Return true if all processes are successful, otherwise go to err and return false.
 */
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

		/* TODO: Insert the page into the spt. */
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL.
 * 1. Verify the validity of the provided spt and va.
 * 1-1. If invalid, return NULL.
 * 2. Convert the virtual address (va) into a page number.
 * 3. Use hash table lookup to find the struct page entry.
 * 3-1. Utilize find_bucket or find_elem functions.
 * 4. Return the found page entry, or return NULL if not found.
 */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function. */

	return page;
}

/* Insert PAGE into spt with validation.
 * 1. Check if the received spt and page are valid.
 * 1-1. If invalid, perform error handling.
 * 2. Check if there is already a page entry in the spt with the same virtual address (va) as the page.
 * 3. Insert the page into the spt.
 * 3-1. Use the insert_elem function.
 * 4. Return true if the insertion is successfully completed, otherwise return fault.
 */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
	/* TODO: Fill this function. */

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
 * and return it. This always returns a valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.
 * 1. Check if the frame pool is initialized.
 * 2. Find an available and unallocated frame.
 * 3. If an available frame is found, initialize that frame.
 * 3-1. If no available frame is found, use the page replacement algorithm.
 * 3-2. For now, there's no need to handle swap out, so mark it as PANIC("todo").
 * 4. Return that frame.
 */
static struct frame *
vm_get_frame (void) {
	struct frame *frame = NULL;
	/* TODO: Fill this function. */

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack.
 * 1. Check if the passed addr argument is valid.
 * 2. Round down the addr to the nearest multiple of the page size (PGSIZE) to align it with a page boundary.
 * 2-1. Use the pg_round_down function.
 * 3. Check if the stack does not exceed the maximum size (1MB).
 * 3-1. Calculate the distance between the current stack pointer position and aligned_addr.
 * 4. Allocate and initialize a new page.
 * 5. Initialize the stack and exit the function with return.
 */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success
 * 1. Validate that the page fault is within a valid address range.
 * 1-1. Check the state at the time the fault occurred.
 * 2. If the address is near the current stack and the fault is for a write operation, grow the stack.
 * 2-1. Call the vm_stack_growth function.
 * 3. Handle other valid page fault cases that are not stack growth.
 * 4. Call vm_do_claim_page for the found page.
 */
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

/* Claim the page that is allocated on VA.
 * 1. Validate the virtual address. Return false if it does not exist.
 * 2. Find the page corresponding to the given virtual address.
 * 2-1. Use hash_find, find_bucket, and find_elem functions.
 */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function */

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu.
 * 1. Check if the passed page pointer is valid.
 * 2. Call the frame allocation function vm_get_frame() to allocate a frame.
 * 2-1. Implement logic to swap pages if no available frame is found.
 * 3. Link the page and frame. The materials for set links are already implemented.
 * 4. Update the page table.
 * 4-1. Insert the page-frame pair into the page table entry.
 */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */

	return swap_in (page, frame->kva);
}

/* Initialize new supplemental page table
 * Choose a data structure, using the already implemented Hash Table.
 * This function is called when a new process starts and when a process forks.
 * 1. Allocate memory for the structure representing the supplemental page table and initialize the necessary fields.
 * 2. Write error handling code in case of memory allocation failure.
 * 3. Initialize the hash table.
 * 3-1. Create the hash table using hash init.
 * 3-2. Initialize the keys and values of the hash table using hash clear.
 * 4. Since the function type is void, it returns nothing.
 */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
}

/* Copy supplemental page table from src to dst 
 * This function is used in the fork function when a child needs to inherit the execution context of the parent.
 * 1. Iterate through each page of the supplemental page table of src.
 * 1-1. Copy the same entries into the supplemental page table of dst.
 * 2. Allocate uninit page and claim them.
 */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
}

/* Free the resource held by the supplemental page table
 * This function is called when a process terminates.
 * There is no need to worry about the actual page table and physical memory.
 * 1. Iterate through page entries and kill the pages in the table.
 * 1-1. Call the uninit_destroy of anon_destroy function.
 */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
}
