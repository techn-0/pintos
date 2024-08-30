/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void sema_init(struct semaphore *sema, unsigned value)
{
	ASSERT(sema != NULL);

	sema->value = value;
	list_init(&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void sema_down(struct semaphore *sema)
{
	enum intr_level old_level;

	ASSERT(sema != NULL);
	ASSERT(!intr_context());

	old_level = intr_disable();
	while (sema->value == 0)
	{
		// list_push_back (&sema->waiters, &thread_current ()->elem); // 원래 방식은 또 FIFO네
		// priority기준으로 정렬되게 하자!
		list_insert_ordered(&sema->waiters, &thread_current()->elem, compare_thread_priority, NULL);
		thread_block(); // 스레드 대기상태로
	}
	sema->value--;
	intr_set_level(old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool sema_try_down(struct semaphore *sema)
{
	enum intr_level old_level;
	bool success;

	ASSERT(sema != NULL);

	old_level = intr_disable();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level(old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void sema_up(struct semaphore *sema)
{
	enum intr_level old_level;

	ASSERT(sema != NULL);

	old_level = intr_disable();
	if (!list_empty(&sema->waiters))
	{
		list_sort(&sema->waiters, compare_thread_priority, NULL);
		thread_unblock(list_entry(list_pop_front(&sema->waiters), struct thread, elem));
	}
	sema->value++;
	check_preemption(); // 추가: 현재 쓰레드와 대기 리스트 스레드의 우선순위 비교
	intr_set_level(old_level);
}

static void sema_test_helper(void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void sema_self_test(void)
{
	struct semaphore sema[2];
	int i;

	printf("Testing semaphores...");
	sema_init(&sema[0], 0);
	sema_init(&sema[1], 0);
	thread_create("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up(&sema[0]);
		sema_down(&sema[1]);
	}
	printf("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper(void *sema_)
{
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down(&sema[0]);
		sema_up(&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void lock_init(struct lock *lock)
{
	ASSERT(lock != NULL);

	lock->holder = NULL;
	sema_init(&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void lock_acquire(struct lock *lock)
{
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(!lock_held_by_current_thread(lock));
	// 추가
	struct thread *curr = thread_current(); // 실행중인 쓰레드 가져옴
	if (lock->holder != NULL)				// 이미 점유일때
	{
		curr->wait_on_lock = lock; // 획득을 시도하지만 아직 획득하지 못한 락 저장(나중에 도네이션이 발생할 때, 이 스레드가 어떤 락을 기다리고 있었는지 추적하기 위해)
		list_insert_ordered(&lock->holder->donations, &curr->donation_elem, compare_donation_priority, NULL);
		// 현재 스레드를 락의 소유자의 도네이션 리스트에 추가
		donate_priority(); // 현재 스레드의 우선순위를 락을 소유한 스레드에게 도네이트
	}

	sema_down(&lock->semaphore);

	curr->wait_on_lock = NULL; // 추가: 락을 얻었을때 필요한 락이 없는 상태로

	lock->holder = thread_current();
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool lock_try_acquire(struct lock *lock)
{
	bool success;

	ASSERT(lock != NULL);
	ASSERT(!lock_held_by_current_thread(lock));

	success = sema_try_down(&lock->semaphore);
	if (success)
		lock->holder = thread_current();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void lock_release(struct lock *lock)
{
	ASSERT(lock != NULL);
	ASSERT(lock_held_by_current_thread(lock));
	// 추가
	remove_donor(lock);				 // 락 해제하면서 락 대기 리스트 에서 현재 쓰레드 제거
	update_priority_before_donations(); // 도네이션으로 변경된 우선순위 업데이트
	//------------------------------
	lock->holder = NULL;
	sema_up(&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool lock_held_by_current_thread(const struct lock *lock)
{
	ASSERT(lock != NULL);

	return lock->holder == thread_current();
}

/* One semaphore in a list. */
struct semaphore_elem
{
	struct list_elem elem;		/* List element. */
	struct semaphore semaphore; /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void cond_init(struct condition *cond)
{
	ASSERT(cond != NULL);

	list_init(&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void cond_wait(struct condition *cond, struct lock *lock)
{
	struct semaphore_elem waiter;

	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	sema_init(&waiter.semaphore, 0);
	// list_push_back (&cond->waiters, &waiter.elem); // 이것도 FIFO
	list_insert_ordered(&cond->waiters, &waiter.elem, compare_sema_priority, NULL);
	lock_release(lock);
	sema_down(&waiter.semaphore);
	lock_acquire(lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_signal(struct condition *cond, struct lock *lock UNUSED)
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	if (!list_empty(&cond->waiters))
	{
		list_sort(&cond->waiters, compare_sema_priority, NULL); //  리스트 우선순위로 정렬
		sema_up(&list_entry(list_pop_front(&cond->waiters), struct semaphore_elem, elem)->semaphore);
	}
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_broadcast(struct condition *cond, struct lock *lock)
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);

	while (!list_empty(&cond->waiters))
		cond_signal(cond, lock);
}

// 추가
bool compare_sema_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
	// 세마포어 구조체로 변경
	struct semaphore_elem *sema_a = list_entry(a, struct semaphore_elem, elem);
	struct semaphore_elem *sema_b = list_entry(b, struct semaphore_elem, elem);
	// 대기 리스트 가져옴
	struct list *waiters_a = &(sema_a->semaphore.waiters);
	struct list *waiters_b = &(sema_b->semaphore.waiters);
	// 리스트 첫 원소
	struct thread *root_a = list_entry(list_begin(waiters_a), struct thread, elem);
	struct thread *root_b = list_entry(list_begin(waiters_b), struct thread, elem);
	return root_a->priority > root_b->priority; // 비교
}

// donation_elem을 우선순위 기준으로 정렬하는 함수를 만들자
bool compare_donation_priority(const struct list_elem *a, const struct list_elem *b, void *aux)
{
	struct thread *st_a = list_entry(a, struct thread, donation_elem);
	struct thread *st_b = list_entry(b, struct thread, donation_elem);
	return st_a->priority > st_b->priority;
}

void donate_priority(void)
{
	struct thread *curr = thread_current(); // 검사중인 스레드
	struct thread *holder;					// 현재 쓰레드가 원하는 락을 점유한 스레드

	int priority = curr->priority; // 현재 스레드 우선순위 저장

	for (int i = 0; i < 8; i++)
	{
		if (curr->wait_on_lock == NULL) // 현재 스레드가 기다리는 락이 없음 종료
			return;
		holder = curr->wait_on_lock->holder; // 원래 락 소유자
		holder->priority = priority;		 // 락 소유자의 우선순위를 내꺼로
		curr = holder;						 // 현재 쓰레드를 락 소유자로
	}
}

void remove_donor(struct lock *lock)
{
	struct list *donations = &(thread_current()->donations); // 현재 스레드 donations 리스트
	struct list_elem *donor_elem;							 // 도네이션 리스트 요소 담을거
	struct thread *donor_thread;							 // donations 리스트에서 꺼낼 쓰레드 담을변수

	if (list_empty(donations)) // 비어있음 종료
		return;

	donor_elem = list_front(donations); // donations 리스트 첫 번째 요소

	while (1)
	{
		donor_thread = list_entry(donor_elem, struct thread, donation_elem); // 현재 요소의 스레드 구조체 가져옴
		if (donor_thread->wait_on_lock == lock)								 // 현재 스레드가 기다리는 락괴 주어진 락이 같으면
			list_remove(&donor_thread->donation_elem);						 // donations리스트에서 제거
		donor_elem = list_next(donor_elem);									 // 다음 요소로
		if (donor_elem == list_end(donations))								 // 다 돌았음 종료
			return;
	}
}

void update_priority_before_donations(void)
{
    struct thread *curr = thread_current(); // 현재 쓰레드
    struct list *donations = &(thread_current()->donations); // 쓰레드의 도네이션 리스트
    struct thread *donations_root;// 도네이션리스트의 첫 원소 저장할 거

    if (list_empty(donations)) // 도네이션 리스트가 비었으면
    {
        curr->priority = curr->init_priority; // 처음 priority로 돌아가
        return;
    }

    donations_root = list_entry(list_front(donations), struct thread, donation_elem);
    curr->priority = donations_root->priority; // 쓰레드의 우선순위를 donations_root의 우선순위로
}