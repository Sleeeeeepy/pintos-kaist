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
 * Function to increase the stack
 * Process to increase the stack from the position specified by the address (addr)
 * Current stack size is fixed at 4KB -> Implement to expand up to a maximum of 1MB
 * 1. If the address is within the stack area, implement logic to increase the stack and allocate necessary pages
 * 1-1. Implement a function to check if the address belongs to the stack area -> already implemented in vm_try_handle_fault
 * 1-2. Use a function to allocate pages
 * 2. Allocate and initialize vm-entry
 * 3. Call install_page() to set up the page table
 * 4. Return True on success, False on failure
 */
static void
vm_stack_growth (void *addr UNUSED) {
	struct thread *curr = thread_current();
	void *round_down_addr = pt_round_down(addr);

    /* Check if it does not exceed the maximum size of the stack */
    if ((uint64_t)(USER_STACK + MAX_STACK_SIZE) <= (uint64_t)round_down_addr) {
        return false;
    }

    /* Check if the page already exists */
    if (spt_find_page (&curr->spt, round_down_addr) != NULL) {
        return false;
    }

    /* Allocate a new page */
    struct page *new_page = malloc (sizeof (struct page));
    if (new_page == NULL) {
        return false;
    }

	/* Map to the page table */
    if (!install_page (round_down_addr, new_page, true)) {
        free(new_page);
        return false;
    }

	/* Add the page to the supplementary page table */
    if (!spt_insert_page (&curr->spt, new_page)) {
        free(new_page);
        return false;
    }
	return true;
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success 
 * Function to handle page faults
 * Implement the logic to handle the fault for a given address (addr) when a page fault occurs
 * Add logic to detect and handle stack access
 * 1. Detect stack access to determine whether it's for stack expansion or an invalid memory access
 * 1-1. Consider access to an address lower than the stack pointer but within a certain range (32 bytes) as stack expansion
 * 1-2. If it's an invalid memory access, terminate the process and display an error message
 */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
    /* Check stack pointer */
    void *stack_pointer = f->rsp;

    /* Find page */
    page = spt_find_page (spt, addr);

    /* Stack expansion logic */
    if (addr >= stack_pointer - 32 && addr < stack_pointer) {
        if (page == NULL) {
            vm_stack_growth (addr);
			return true;
        }
    }

    /* Handle invalid memory access */
    if (page == NULL) {
        exit (-1);
        return false;
    }

    /* Normal page fault handling */
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
 * 1-1. Call the destroy function.
 */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
}
