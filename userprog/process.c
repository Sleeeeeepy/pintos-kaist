#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/task.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/mmu.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#include "devices/timer.h"
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *task);
static void __do_fork (void *);
static void build_stack (const char *file_name, struct intr_frame *if_);

/* Initialize process system. */
void
process_init (void) {
	lock_init (&process_filesys_lock);
	task_init ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
pid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	struct thread *t;
	struct task *task;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return PID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);

	/* Create a new thread to execute FILE_NAME. */
	task = task_create (file_name, NULL);
	if (task == NULL) {
		return PID_ERROR;
	}

	task->args = fn_copy;
	t = create_thread (file_name, PRI_DEFAULT, initd, task);
	if (t == NULL) {
		palloc_free_page(fn_copy);
		return PID_ERROR;
	}
	
	return task->pid;
}

/* A thread function that launches first user process. */
static void
initd (void *task) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif
	struct task *t = (struct task *) task;
	task_set_thread (t, thread_current ());
	if (process_exec (t->args) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
pid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	/* Clone current thread to new thread.*/
	struct thread *thread;
	struct task *child;
	struct task *parent;
	pid_t child_pid;

	parent = task_find_by_tid (thread_tid ());
	if (parent == NULL) {
		return PID_ERROR;
	}

	child = task_create (name, NULL);
	child->parent_pid = parent->pid;
	child->if_ = if_;
	child_pid = child->pid;

	// lock_acquire (&process_filesys_lock);
	child->executable = file_reopen (parent->executable);
	// lock_release (&process_filesys_lock);
	thread = create_thread (name, PRI_DEFAULT, __do_fork, child);
	if (thread != NULL) {
		sema_down (&child->fork_lock);
	} else {
		task_free (child);
		return -1;
	}

	if (task_find_by_pid (child_pid) == NULL) {
		return -1;
	}
	return child->pid;
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. If the parent_page is kernel page, then return immediately. */
	if (is_kern_pte (pte)) {
		/* We will not copy the kernel page. but there are allocated things
		 * like struct task. to successfully duplicate all of the user pte,
		 * duplicate_pte returns true. 
		 * So far, all we need to fork the process is user pages. */
		return true;
	}

	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page (parent->pml4, va);

	/* 3. Allocate new PAL_USER page for the child and set result to NEWPAGE. */
	newpage = palloc_get_page (PAL_ZERO | PAL_USER);
	if (newpage == NULL) {
		return false;
	}

	/* 4. Duplicate parent's page to the new page and 
	 *	  check whether parent's page is writable or not (set WRITABLE 
	 *	  according to the result). */
	memcpy (newpage, parent_page, PGSIZE);
	writable = is_writable (pte);

	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. if fail to insert page, do error handling. */
		palloc_free_page (newpage);
		return false;
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork (void *aux) {
	struct intr_frame if_;
	struct task *task = (struct task *) aux;
	struct thread *current = thread_current ();
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame *parent_if = task->if_;
	bool succ = true;
	task_set_thread (task, current);
	struct task *parent = task_find_by_pid (task->parent_pid);
	if (parent == NULL) {
		goto error;
	}

	/* 1. Read the cpu context to local stack. */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));
	if_.R.rax = 0;

	/* 2. Duplicate PT */
	current->pml4 = pml4_create ();
	if (current->pml4 == NULL)
		goto error;

	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->thread->spt)) {
		goto error;
	}
#else
	if (!pml4_for_each (parent->thread->pml4, duplicate_pte, parent->thread))
		goto error;
#endif

	task_fork_fd (parent, task);

	/* Finally, switch to the newly created process. */
	if (succ) {
		file_deny_write (task->executable);
		list_push_back (&parent->children, &task->celem);
		sema_up (&task->fork_lock);
		do_iret (&if_);
	}
		
error:
	task_set_status (task, PROCESS_FAIL);
	sema_up (&task->fork_lock);
	task_exit (-1);
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int
process_exec (void *f_name) {
	char *file_name = f_name;	
	bool success;

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */
	process_cleanup ();

	/* And then load the binary */
	success = load (file_name, &_if);
	
	/* If load failed, quit. */
	palloc_free_page (file_name);
	if (!success)
		return -1;

	/* Start switched process. */
	do_iret (&_if);
	NOT_REACHED ();
}


/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int
process_wait (pid_t child_pid UNUSED) {
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */
	struct task *current = task_find_by_tid (thread_tid ());
	struct task *child = task_find_by_pid (child_pid);
	int result;
	if (child == NULL) {
		return -1;
	}

	/* The child process terminated before wait. */
	if (child->status == PROCESS_EXITED) {
		result = child->exit_code;
		list_remove (&child->celem);
		task_free (child);
		goto done;
	}

	task_set_status (current, PROCESS_WAIT);
	sema_down (&child->wait_lock);
	task_set_status (child, PROCESS_DYING);
	/* clean up process. */
	result = child->exit_code;
	if (current == NULL) {
		goto done;
	}

	list_remove (&child->celem);
	task_free (child);
	task_set_status (current, PROCESS_READY);
done:
	return result;
}

/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	struct task *task = task_find_by_tid (thread_tid ());
	if (task == NULL) {
		goto cleanup;
	}
	
	/* Fork fails */
	if (task->status == PROCESS_FAIL) {
		task_free (task);
		goto cleanup;	
	}

	printf ("%s: exit(%d)\n", task->name, task->exit_code);
	task_set_status (task, PROCESS_EXITED);
	sema_up (&task->wait_lock);
	task_file_cleanup (task);

	/* If the current process has children, remove children. 
	 * If there is child processes that aren't complete its task,
	 * make its parent process to initd. 
	 * 
	 * Note that this assumption will fail: in the current situation, 
	 * this project's initd may not wait for another program to exit. 
	 * It's named daemon, but it's not actually a daemon, 
	 * which could potentially cause an error.
	 */
	if (task_child_len (task) != 0) {
		task_inherit_initd (task);
	}

	/* If there is no the parent process, then remove immediately. */
	if (task->parent_pid < 0) {
		task_free (task);
	}
	
cleanup:
	process_cleanup();
}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *curr = thread_current ();
	struct task* task = task_find_by_tid (curr->tid);
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	char cmd_line[255], *save_ptr, *program;
	
	/* Parse program name */
	strlcpy (cmd_line, file_name, sizeof (cmd_line));
	program = strtok_r (cmd_line, " ", &save_ptr);

	/* Allocate and activate page directory. */
	curr->pml4 = pml4_create ();
	if (curr->pml4 == NULL)
		goto fail;
	process_activate (curr);

	/* Open executable file. */
	lock_acquire (&process_filesys_lock);
	file = filesys_open (program);
	lock_release (&process_filesys_lock);
	if (file == NULL) {
		printf ("load: %s: open failed\n", program);
		goto fail;
	}

	/* Read and verify executable header. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", program);
		goto fail;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (int i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto fail;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto fail;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto fail;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto fail;
				}
				else
					goto fail;
				break;
		}
	}

	/* Set up stack. */
	if (!setup_stack (if_))
		goto fail;

	/* Start address. */
	if_->rip = ehdr.e_entry;

	/* Build stack. */
	build_stack (file_name, if_);

	/* Deny write on executable. */
	task->executable = file;
	file_deny_write (file);

	return true;
fail:
	// lock_acquire (&process_filesys_lock);
	file_close (file);
	// lock_release (&process_filesys_lock);
	return false;
}

static void
build_stack (const char *file_name, struct intr_frame *if_) {
	uintptr_t stack = if_->rsp;
	int argc = 0;
	uintptr_t arg_map[255] = {0};
	size_t len = 0;
	char args[255] = {0}, *save_ptr = NULL;
	strlcpy (args, file_name, sizeof (args));
	
	/* Push arguments. */
	char *token = strtok_r (args, " ", &save_ptr);
	for (; token != NULL; token = strtok_r (NULL, " ", &save_ptr)) {
		len = strlen (token) + 1;
		stack -= len;
		arg_map[argc] = stack;
		strlcpy ((void *) stack, token, len);
		argc++;
	}

	/* Align stack */
	stack -= stack % 8;

	/* Push argument pointers. */
	for (int j = argc; j >= 0; j--) {
		stack -= sizeof (uintptr_t);
		*(uintptr_t *) stack = arg_map[j];
	}
	
	if_->R.rsi = stack;
	if_->R.rdi = argc;

	/* Push return address. */
	stack -= sizeof (uintptr_t);
	*(uintptr_t *)stack = 0;
	
	if_->rsp = stack;
}

/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	struct lazy_load_args *args = aux;
	struct file *file = task_find_by_tid (thread_tid ())->executable;
	uint32_t page_read_bytes = args->read_bytes;
	uint32_t page_zero_bytes = args->zero_bytes;
	uint8_t *upage = args->addr;
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

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		struct lazy_load_args *aux = malloc (sizeof (struct lazy_load_args));
		*aux = (struct lazy_load_args) {
		 	.offset = ofs,
			.read_bytes = page_read_bytes,
			.zero_bytes = page_zero_bytes,
			.writable = writable,
			.addr = upage
		};
		if (!vm_alloc_page_with_initializer (VM_ANON | VM_MARKER_1, upage,
					writable, lazy_load_segment, (void *) aux))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
		
		/* Update offset. */
		ofs += page_read_bytes;	
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */
	if (!vm_alloc_page (VM_ANON | VM_MARKER_0, stack_bottom, true)) {
		return false;
	}

	success = vm_claim_page (stack_bottom);
	if (success) {
		if_->rsp = USER_STACK;
	}
	return success;
}
#endif /* VM */
