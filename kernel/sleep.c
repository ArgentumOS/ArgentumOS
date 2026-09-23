/*
 * fnx/kernel/sleep.c
 *
 * Copyright 2018-2022, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/asm.h>
#include <fnx/kernel.h>
#include <fnx/limits.h>
#include <fnx/sleep.h>
#include <fnx/sched.h>
#include <fnx/signal.h>
#include <fnx/process.h>
#include <fnx/stdio.h>
#include <fnx/string.h>

#define NR_BUCKETS		(NR_PROCS >= 10 ? (NR_PROCS * 10) / 100 : 1)
#define SLEEP_HASH(addr)	((addr) % (NR_BUCKETS))

struct proc *sleep_hash_table[NR_BUCKETS];
struct proc *proc_run_head;
static unsigned int area = 0;

void runnable(struct proc *p)
{
	unsigned int flags;

	if(p->state == PROC_RUNNING) {
		printk("WARNING: %s(): process with pid '%d' is already running!\n", __FUNCTION__, p->pid);
		return;
	}

	SAVE_FLAGS(flags); CLI();
	if(proc_run_head) {
		p->next_run = proc_run_head;
		proc_run_head->prev_run = p;
	}
	proc_run_head = p;
	p->state = PROC_RUNNING;
	RESTORE_FLAGS(flags);
}

void not_runnable(struct proc *p, int state)
{
	unsigned int flags;

	SAVE_FLAGS(flags); CLI();
	if(p->next_run) {
		p->next_run->prev_run = p->prev_run;
	}
	if(p->prev_run) {
		p->prev_run->next_run = p->next_run;
	}
	if(p == proc_run_head) {
		proc_run_head = p->next_run;
	}
	p->prev_run = p->next_run = NULL;
	p->state = state;
	RESTORE_FLAGS(flags);
}

int sleep(void *address, int state)
{
	unsigned int flags;
	struct proc **h;
	int signum, i;

	SAVE_FLAGS(flags); CLI();

	/* return if it has signals */
	if(state == PROC_INTERRUPTIBLE) {
		if((signum = issig())) {
			RESTORE_FLAGS(flags);
			return signum;
		}
	}

	if(current->state == PROC_SLEEPING) {
		printk("WARNING: %s(): process with pid '%d' is already sleeping!\n", __FUNCTION__, current->pid);
		RESTORE_FLAGS(flags);
		return 0;
	}

	/* FNX: canonicalize the address so a symbol (&sys_wait4, &do_select,
	 * ...) hashes and matches the SAME value regardless of which image
	 * alias (identity vs high-half) the caller ran from; see SLEEP_ADDR
	 * in include/fnx/sleep.h. */
	i = SLEEP_HASH(SLEEP_ADDR(address));
	h = &sleep_hash_table[i];

	/* insert process in the head */
	if(!*h) {
		*h = current;
		(*h)->prev_sleep = (*h)->next_sleep = NULL;
	} else {
		current->prev_sleep = NULL;
		current->next_sleep = *h;
		(*h)->prev_sleep = current;
		*h = current;
	}
	current->sleep_address = (void *)SLEEP_ADDR(address);
	if(state == PROC_UNINTERRUPTIBLE) {
		current->flags |= PF_NOTINTERRUPT;
	}
	not_runnable(current, PROC_SLEEPING);

	do_sched();

	signum = 0;
	if(state == PROC_INTERRUPTIBLE) {
		signum = issig();
	}

	RESTORE_FLAGS(flags);
	return signum;
}

/*
 * ARM / DISARM / COMMIT - THE WAIT THAT CANNOT LOSE A WAKEUP.
 *
 * WHY THEY EXIST, AND IT IS A WINDOW RATHER THAN A RACE ANYONE CAN SCHEDULE AROUND. The shape every
 * multiplexing waiter in this kernel uses is
 *
 *	for(;;) { if(ready()) break; sleep(&channel, PROC_INTERRUPTIBLE); }
 *
 * and the two halves of that are NOT atomic together. `sleep()` takes interrupts off for its OWN registration,
 * so a wake that arrives BEFORE that point finds no entry in the sleep table, is dropped, and the waiter then
 * sleeps for ever. What makes such a wake reachable is that it is never the only event: the STATE CHANGE comes
 * FIRST and the wake second - `loopback_deliver` queues the packet and THEN wakes - so a waiter that registers
 * BEFORE it looks cannot miss one:
 *
 *	sleep_arm(&channel);				// registered: any later wake will find us
 *	if(ready()) { sleep_disarm(); break; }		// ... and any EARLIER one is visible right here
 *	sleep_commit(&channel, PROC_INTERRUPTIBLE);	// blocks, with the registration already in place
 *
 * AND BEING WOKEN WHILE NOT SLEEPING IS HARMLESS BY CONSTRUCTION: wakeup() matches on the channel and does not
 * consult the state, so a wake during the check marks this process runnable and clears `sleep_address` - which
 * is exactly the flag sleep_disarm() and sleep_commit() read, so neither can unlink one entry twice, and a
 * spurious wake simply sends the loop around.
 *
 * FOUND IN §58.1: an infinite poll that never returned with the far end's data already on the wire, while the
 * same poll with a FINITE timeout was correct - because that wait ended anyway and its re-check saw the data.
 * That asymmetry IS the window, and it is also why "poll's timeout is exact" has always been true here.
 * foundation-plan.md §58.1 records it.
 */
void sleep_arm(void *address)
{
	unsigned int flags;
	struct proc **h;
	int i;

	SAVE_FLAGS(flags); CLI();
	/* IDEMPOTENT, AND IT HAS TO BE: the loop calls this on every pass, and a spurious return from the commit
	 * leaves us registered. Registering one process twice would corrupt the table. */
	if(current->sleep_address != NULL) {
		RESTORE_FLAGS(flags);
		return;
	}
	if(current->state == PROC_SLEEPING) {
		printk("WARNING: %s(): process with pid '%d' is already sleeping!\n", __FUNCTION__, current->pid);
		RESTORE_FLAGS(flags);
		return;
	}
	i = SLEEP_HASH(SLEEP_ADDR(address));
	h = &sleep_hash_table[i];

	/* insert process in the head */
	if(!*h) {
		*h = current;
		(*h)->prev_sleep = (*h)->next_sleep = NULL;
	} else {
		current->prev_sleep = NULL;
		current->next_sleep = *h;
		(*h)->prev_sleep = current;
		*h = current;
	}
	current->sleep_address = (void *)SLEEP_ADDR(address);
	RESTORE_FLAGS(flags);
}

void sleep_disarm(void)
{
	unsigned int flags;
	struct proc **h;
	int i;

	SAVE_FLAGS(flags); CLI();
	/* A WAKE CLEARS sleep_address, AND THAT IS THE ONLY PROOF AN ENTRY IS ALREADY GONE - so this cannot unlink
	 * one that the waker has already unlinked. */
	if(current->sleep_address != NULL) {
		i = SLEEP_HASH((addr_t)current->sleep_address);
		h = &sleep_hash_table[i];
		while(*h) {
			if(*h == current) {
				if(current->next_sleep) {
					current->next_sleep->prev_sleep = current->prev_sleep;
				}
				if(current->prev_sleep) {
					current->prev_sleep->next_sleep = current->next_sleep;
				}
				*h = current->next_sleep;
				break;
			}
			h = &(*h)->next_sleep;
		}
		current->sleep_address = NULL;
		current->prev_sleep = current->next_sleep = NULL;
	}
	RESTORE_FLAGS(flags);
}

int sleep_commit(void *address, int state)
{
	unsigned int flags;
	int signum;

	/* IF A WAKE CLEARED OUR REGISTRATION, DO NOT SLEEP AT ALL - GO AROUND THE CALLER'S LOOP INSTEAD.
	 *
	 * AND THIS IS THE ONE PLACE THE WINDOW COULD STILL HAVE BEEN LEFT OPEN, which is why it is spelled out. A
	 * wake is cleared by the waker (`wakeup()` nulls sleep_address), so "already cleared" here means A WAKE HAS
	 * ARRIVED SINCE WE ARM'D - and that wake's STATE CHANGE may have landed BEHIND the caller's look: the look
	 * and the waker run concurrently, so a look that had already passed the descriptor which became ready misses
	 * it. Re-arming and sleeping there would park the caller with the work already waiting. Returning 0 without
	 * sleeping sends it around for another - cheap - look, and whether it slept or not is invisible to it. */
	SAVE_FLAGS(flags); CLI();
	if(current->sleep_address == NULL) {
		RESTORE_FLAGS(flags);
		return 0;
	}

	if(state == PROC_INTERRUPTIBLE && (signum = issig())) {
		RESTORE_FLAGS(flags);
		return signum;
	}
	if(state == PROC_UNINTERRUPTIBLE) {
		current->flags |= PF_NOTINTERRUPT;
	}
	not_runnable(current, PROC_SLEEPING);

	do_sched();

	signum = 0;
	if(state == PROC_INTERRUPTIBLE) {
		signum = issig();
	}

	RESTORE_FLAGS(flags);
	return signum;
}

void wakeup(void *address)
{
	unsigned int flags;
	struct proc **h;
	int i, found;

	SAVE_FLAGS(flags); CLI();
	/* FNX: canonicalize the address so a symbol (&sys_wait4, &do_select,
	 * ...) hashes and matches the SAME value regardless of which image
	 * alias (identity vs high-half) the caller ran from; see SLEEP_ADDR
	 * in include/fnx/sleep.h. */
	i = SLEEP_HASH(SLEEP_ADDR(address));
	h = &sleep_hash_table[i];
	found = 0;

	while(*h) {
		if((*h)->sleep_address == (void *)SLEEP_ADDR(address)) {
			/* ONLY A SLEEPING PROCESS IS MADE RUNNABLE. THE ARM/LOOK/COMMIT CURE MAKES THIS REACHABLE:
			 * sleep_arm() registers a process that is STILL RUNNING (it arms before it looks), so a wake can
			 * now match a process that never slept - and runnable() inserts into the run queue, so calling it
			 * there would list that process twice. Clearing the registration and sending it around its loop is
			 * all a running process needs; for every other caller of wakeup() the state is PROC_SLEEPING and
			 * nothing changes. */
			int asleep = ((*h)->state == PROC_SLEEPING);

			(*h)->sleep_address = NULL;
			(*h)->flags &= ~PF_NOTINTERRUPT;
			(*h)->cpu_count = (*h)->priority;
			if(asleep) {
				runnable(*h);
			}
			found = 1;
			if((*h)->next_sleep) {
				(*h)->next_sleep->prev_sleep = (*h)->prev_sleep;
			}
			if((*h)->prev_sleep) {
				(*h)->prev_sleep->next_sleep = (*h)->next_sleep;
			}
			if(h == &sleep_hash_table[i]) {	/* if it's the head */
				*h = (*h)->next_sleep;
			}
			continue;
		}
		h = &(*h)->next_sleep;
	}
	RESTORE_FLAGS(flags);
	if(found) {
		need_resched = 1;
	}
}

void wakeup_proc(struct proc *p)
{
	unsigned int flags;
	struct proc **h;
	int i;

	if(p->state != PROC_SLEEPING && p->state != PROC_STOPPED) {
		return;
	}

	/* return if the process is not interruptible */
	if(p->flags & PF_NOTINTERRUPT) {
		return;
	}

	SAVE_FLAGS(flags); CLI();

	/* stopped processes don't have sleep address */
	if(p->sleep_address) {
		if(p->next_sleep) {
			p->next_sleep->prev_sleep = p->prev_sleep;
		}
		if(p->prev_sleep) {
			p->prev_sleep->next_sleep = p->next_sleep;
		}

		i = SLEEP_HASH((addr_t)p->sleep_address);
		h = &sleep_hash_table[i];

		if(*h == p) {	/* if it's the head */
			*h = (*h)->next_sleep;
		}
	}
	p->sleep_address = NULL;
	p->cpu_count = p->priority;
	runnable(p);
	need_resched = 1;

	RESTORE_FLAGS(flags);
}

void lock_resource(struct resource *resource)
{
	unsigned int flags;

	for(;;) {
		SAVE_FLAGS(flags); CLI();
		if(resource->locked) {
			resource->wanted = 1;
			RESTORE_FLAGS(flags);
			sleep(resource, PROC_UNINTERRUPTIBLE);
		} else {
			break;
		}
	}
	resource->locked = 1;
	RESTORE_FLAGS(flags);
}

void unlock_resource(struct resource *resource)
{
	unsigned int flags;

	SAVE_FLAGS(flags); CLI();
	resource->locked = 0;
	if(resource->wanted) {
		resource->wanted = 0;
		wakeup(resource);
	}
	RESTORE_FLAGS(flags);
}

int can_lock_area(unsigned int type)
{
	unsigned int flags;
	int retval;

	SAVE_FLAGS(flags); CLI();
	retval = area & type;
	area |= type;
	RESTORE_FLAGS(flags);

	return !retval;
}

int unlock_area(unsigned int type)
{
	unsigned int flags;
	int retval;

	SAVE_FLAGS(flags); CLI();
	retval = area & type;
	area &= ~type;
	RESTORE_FLAGS(flags);

	return retval;
}

void sleep_init(void)
{
	proc_run_head = NULL;
	memset_b(sleep_hash_table, 0, sizeof(sleep_hash_table));
}
