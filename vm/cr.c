#include "vm/cr.h"
#include <inttypes.h>
#include "intrinsic.h"

/* Set the write protection on ring 0. 
 * It MUST be unset after the operation is complete. */
void cr0_wp_set (void) {
	uint64_t val = rcr0 () | ~0xFFFEFFFF;
	lcr0 (val);
}

/* Unset the write protection on ring 0. */
void cr0_wp_unset (void) {
	uint64_t val = rcr0 () & 0xFFFEFFFF;
	lcr0 (val);
}