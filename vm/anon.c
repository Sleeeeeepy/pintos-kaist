/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
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

/* Initialize the data for anonymous pages
 * 1. Define and call a new auxiliary function to set up the swap_disk.
 * 2. Initialize the data structures for managing the swap area.
 * 3. Set up the page allocation algorithm.
 * 4. Set policies related to anonymous pages.
 */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = NULL;
}

/* Initialize the file mapping
 * 1. Check if the passed arguments (page, type, kva) are valid.
 * 2. Initialize the anonymous page structure.
 * 2-1. Initialize the anon_page within the page structure.
 * 3. Allocate the data for the anonymous page in actual memory or set its initial state.
 */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
}

/* Destroy the anonymous page. PAGE will be freed by the caller.
 * 1. Check if the passed page argument is valid.
 * 2. Extract the anon_page structure from the page structure. (already implemented)
 * 3. If there are resources allocated to anon_page, release them.
 * 3-1. Such as memory allocated to the anonymous page, file handles, etc.
 * 4. Perform any additional cleanup tasks and then exit the function with return;
 */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
}
