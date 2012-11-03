#include <types.h>
#include <stddef.h>
#include <string.h>
#include "matrix/matrix.h"
#include "matrix/debug.h"
#include "mm/kmem.h"
#include "mm/malloc.h"
#include "mm/slab.h"
#include "proc/thread.h"
#include "proc/process.h"
#include "proc/sched.h"

/* Temporarily used thread id */
static tid_t _next_tid = 1;

/* Thread structure cache */
static struct slab_cache *_thread_cache;

extern uint32_t read_eip();

static tid_t id_alloc()
{
	return _next_tid++;
}

void arch_thread_init(struct thread *t, void *kstack)
{
	t->arch.esp = kstack;
	t->arch.ebp = 0;
	t->arch.eip = t->entry;
}

void arch_thread_switch(struct thread *curr, struct thread *prev)
{
	void *esp, *ebp, *eip;

	/* Get the stack pointer and base pointer first */
	asm volatile("mov %%esp, %0" : "=r"(esp));
	asm volatile("mov %%ebp, %0" : "=r"(ebp));

	/* Read the instruction pointer. We do some cunning logic here:
	 * One of the two things could have happened when this function exits
	 *   (a) We called the function and it returned the EIP as requested.
	 *   (b) We have just switched processes, and because the saved EIP is
	 *       essentially the instruction after read_eip(), it will seem as
	 *       if read_eip has just returned.
	 * In the second case we need to return immediately. To detect it we put
	 * a magic number in EAX further down at the end of this function. As C
	 * returns value in EAX, it will look like the return value is this
	 * magic number. (0x87654321).
	 */
	eip = (void *)read_eip();
	
	/* If we have just switched processes, do nothing */
	if (0x87654321 == (uint32_t)eip) {
		return;
	}

	/* Save the process context for previous process */
	if (prev) {
		prev->arch.eip = eip;
		prev->arch.esp = esp;
		prev->arch.ebp = ebp;
	}

	/* Switch to current process context */
	eip = curr->arch.eip;
	esp = curr->arch.esp;
	ebp = curr->arch.ebp;

	/* Switch the kernel stack in TSS to the process's kernel stack */
	set_kernel_stack(curr->kstack);

	/* Here we:
	 * [1] Disable interrupts so we don't get bothered.
	 * [2] Temporarily puts the new EIP location in EBX.
	 * [3] Loads the stack and base pointers from the new process struct.
	 * [4] Puts a magic number (0x87654321) in EAX so that above we can recognize
	 *     that we've just switched process.
	 * [5] Jumps to the location in EBX (remember we put the new EIP in there)
	 * Note that you can't change the sequence we set each register here. You
	 * can check the actual asm code generated by your compiler for the reason.
	 */
	asm volatile ("mov %0, %%ebx\n"
		      "mov %1, %%esp\n"
		      "mov %2, %%ebp\n"
		      "mov $0x87654321, %%eax\n"	/* read_eip() will return 0x87654321 */
		      "jmp *%%ebx\n"
		      :: "r"(eip), "r"(esp), "r"(ebp)
		      : "%ebx", "%esp", "%eax");
}

static void thread_ctor(void *obj)
{
	struct thread *t = (struct thread *)obj;

	t->ref_count = 0;
	
	LIST_INIT(&t->runq_link);
	LIST_INIT(&t->wait_link);

	/* Initialize the death notifier */
	init_notifier(&t->death_notifier);
}

static void thread_dtor(void *obj)
{
	;
}

int thread_create(struct process *owner, int flags, thread_func_t func,
		  void *args, struct thread **tp)
{
	int rc = -1;
	struct thread *t;

	if (!owner) {
		owner = _kernel_proc;
	}
	
	t = kmalloc(sizeof(struct thread), 0);
	if (!t) {
		goto out;
	}

	/* Allocate an ID for the thread */
	t->id = id_alloc();
	
	/* Allocate kernel stack for the process */
	t->kstack = kmalloc(KSTACK_SIZE, MM_ALIGN) + KSTACK_SIZE;
	memset((void *)((uint32_t)t->kstack - KSTACK_SIZE), 0, KSTACK_SIZE);

	thread_ctor(t);

	/* Initialize the architecture-specific data */
	arch_thread_init(t, t->kstack);

	/* Initially set the CPU to NULL - the thread will be assigned to a
	 * CPU when thread_run() is called on it.
	 */
	t->cpu = NULL;

	t->state = THREAD_CREATED;
	t->flags = flags;
	t->priority = 16;
	t->ustack = 0;
	t->ustack_size = 0;
	t->entry = func;
	t->args = args;

	/* Add the thread to the owner */
	process_attach(owner, t);

	if (tp) {
		*tp = t;
	} else {
		/* Caller doesn't want a pointer, just start running it */
		thread_run(t);
	}

out:
	return rc;
}

void thread_run(struct thread *t)
{
	ASSERT(t->state == THREAD_CREATED);

	t->state = THREAD_READY;
	sched_insert_thread(t);
}

void thread_kill(struct thread *t)
{
	if (t->owner != _kernel_proc) {
		;
	}
}

void thread_release(struct thread *t)
{
	void *kstack;

	ASSERT((t->state == THREAD_CREATED) || (t->state == THREAD_DEAD));
	ASSERT(LIST_EMPTY(&t->runq_link));

	/* Detach from its owner */
	process_detach(t);

	/* Cleanup the thread */
	kstack = (void *)((uint32_t)t->kstack - KSTACK_SIZE);
	DEBUG(DL_DBG, ("thread_release: kstack(%p).\n", kstack));
	kfree(kstack);

	notifier_clear(&t->death_notifier);
}

void thread_exit()
{
	boolean_t state;

	if (CURR_THREAD->ustack_size) {
		/* TODO: Unmap the user mode stack */
		;
	}

	notifier_run(&CURR_THREAD->death_notifier);

	state = irq_disable();
	
	CURR_THREAD->state = THREAD_DEAD;

	sched_reschedule(state);

	PANIC("Should not get here");
}

void init_thread()
{
	/* Create the thread slab cache */
	_thread_cache = slab_cache_create("thread_cache", sizeof(struct thread), 0,
					  thread_ctor, thread_dtor, NULL, 0, 0);
}


