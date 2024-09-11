#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#include "threads/fixed_point.h" // 추가 AS
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

// 추가---------------------------------------------------------
static struct list sleep_list;
bool compare_thread_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);
// 추가 AS
static struct list all_list;

/* Statistics. */
static long long idle_ticks;   /* # of timer ticks spent idle. */
static long long kernel_ticks; /* # of timer ticks in kernel threads. */
static long long user_ticks;   /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4		  /* # of timer ticks to give each thread. */
static unsigned thread_ticks; /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;
int load_avg; // 추가 AS

static void kernel_thread(thread_func *, void *aux);

static void idle(void *aux UNUSED);
static struct thread *next_thread_to_run(void);
static void init_thread(struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule(void);
static tid_t allocate_tid(void);

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *)(pg_round_down(rrsp())))

// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = {0, 0x00af9a000000ffff, 0x00cf92000000ffff};

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void thread_init(void)
{
	ASSERT(intr_get_level() == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof(gdt) - 1,
		.address = (uint64_t)gdt};
	lgdt(&gdt_ds);

	/* Init the globla thread context */
	lock_init(&tid_lock);
	list_init(&ready_list);
	list_init(&destruction_req);
	// 추가-------------------------------------------------
	list_init(&sleep_list);
	// 추가 AS
	list_init(&all_list);

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread();
	init_thread(initial_thread, "main", PRI_DEFAULT);
	// 추가 AS
	if (thread_mlfqs)
		list_push_back(&all_list, &(initial_thread->all_elem));

	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void thread_start(void)
{
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init(&idle_started, 0);
	thread_create("idle", PRI_MIN, idle, &idle_started);

	load_avg = LOAD_AVG_DEFAULT; // 추가 AS

	/* Start preemptive thread scheduling. */
	intr_enable();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down(&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void thread_tick(void)
{
	struct thread *t = thread_current();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return();
}

/* Prints thread statistics. */
void thread_print_stats(void)
{
	printf("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
		   idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t thread_create(const char *name, int priority, thread_func *function, void *aux)
{
	struct thread *t;
	tid_t tid;

	ASSERT(function != NULL);

	/* Allocate thread. */
	t = palloc_get_page(PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread(t, name, priority);
	tid = t->tid = allocate_tid();

// 휘건 추가
#ifdef USERPROG
	t->fdt = palloc_get_multiple(PAL_ZERO, FDT_PAGES);
	if (t->fdt == NULL)
		return TID_ERROR;

	t->exit_status = 0; // exit_status 초기화

	t->fd_idx = 3;
	t->fdt[0] = 0; // stdin 예약된 자리 (dummy)
	t->fdt[1] = 1; // stdout 예약된 자리 (dummy)
	t->fdt[2] = 2; // stderr 예약된 자리 (dummy)

	list_push_back(&thread_current()->child_list, &t->child_elem);

#endif
	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t)kernel_thread;
	t->tf.R.rdi = (uint64_t)function;
	t->tf.R.rsi = (uint64_t)aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* Add to run queue. */
	thread_unblock(t);
	check_preemption(); // 추가

	if(t->priority > thread_current()->priority)
		thread_yield();

	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void thread_block(void)
{
	ASSERT(!intr_context());
	ASSERT(intr_get_level() == INTR_OFF);
	thread_current()->status = THREAD_BLOCKED;
	schedule();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void thread_unblock(struct thread *t)
{
	enum intr_level old_level;

	ASSERT(is_thread(t));

	old_level = intr_disable();
	ASSERT(t->status == THREAD_BLOCKED);
	// list_push_back(&ready_list, &t->elem);// 이거 대신 프라이오리티 기준으로 정렬
	list_insert_ordered(&ready_list, &t->elem, compare_thread_priority, NULL);

	t->status = THREAD_READY;
	intr_set_level(old_level);
}

bool compare_thread_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
	struct thread *st_a = list_entry(a, struct thread, elem);
	struct thread *st_b = list_entry(b, struct thread, elem);
	return st_a->priority > st_b->priority;
	// 	if (thread_a->priority > thread_b->priority) // 쓰레드 간의 우선순위 비교
	// 		return true;
	// 	else
	// 		return false;
	// }
}

/* Returns the name of the running thread. */
const char *
thread_name(void)
{
	return thread_current()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current(void)
{
	struct thread *t = running_thread();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT(is_thread(t));
	ASSERT(t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
tid_t thread_tid(void)
{
	return thread_current()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void thread_exit(void)
{
	ASSERT(!intr_context());

#ifdef USERPROG
	process_exit();
#endif
	if (thread_mlfqs)
		list_remove(&thread_current()->all_elem);

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable();
	do_schedule(THREAD_DYING);
	NOT_REACHED();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void thread_yield(void)
{
	struct thread *curr = thread_current(); // 현재 실행 중인 스레드 가져옴
	enum intr_level old_level;				// 인터럽트 레벨 저장
	ASSERT(!intr_context());				// 현재 인터럽트 컨텍스인지 확인
	old_level = intr_disable();				// 인터럽트 비활성화,이전 레벨 저장
	if (curr != idle_thread)
		// list_push_back(&ready_list, &curr->elem); // FIFO방식으로 하네? 우선순위 방식으로 바꿔주자!
		list_insert_ordered(&ready_list, &curr->elem, compare_thread_priority, NULL);
	do_schedule(THREAD_READY);
	intr_set_level(old_level);
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void thread_set_priority(int new_priority)
{
	if (thread_mlfqs) // 추가 AS
		return;
	thread_current()->init_priority = new_priority;
	update_priority_before_donations();
	check_preemption();
}

/* Returns the current thread's priority. */
int thread_get_priority(void)
{
	return thread_current()->priority;
}

/* Sets the current thread's nice value to NICE. */
void thread_set_nice(int nice UNUSED)
{
	struct thread *t = thread_current();
	enum intr_level old_level = intr_disable();
	t->niceness = nice;
	mlfqs_priority(t);
	check_preemption();
	intr_set_level(old_level);
}

/* Returns the current thread's nice value. */
int thread_get_nice(void)
{
	struct thread *t = thread_current();

	enum intr_level old_level = intr_disable();
	int nice = t->niceness;
	intr_set_level(old_level);

	return nice;
}

/* Returns 100 times the system load average. */
int thread_get_load_avg(void)
{
	enum intr_level old_level = intr_disable();
	int load_avg_val = fp_to_int_round(mult_mixed(load_avg, 100));
	intr_set_level(old_level);

	return load_avg_val;
}

/* Returns 100 times the current thread's recent_cpu value. */
int thread_get_recent_cpu(void)
{
	struct thread *t = thread_current();

	enum intr_level old_level = intr_disable();
	int recent_cpu = fp_to_int_round(mult_mixed(t->recent_cpu, 100));
	intr_set_level(old_level);

	return recent_cpu;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle(void *idle_started_ UNUSED)
{
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current();
	sema_up(idle_started);

	for (;;)
	{
		/* Let someone else run. */
		intr_disable();
		thread_block();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread(thread_func *function, void *aux)
{
	ASSERT(function != NULL);

	intr_enable(); /* The scheduler runs with interrupts off. */
	function(aux); /* Execute the thread function. */
	thread_exit(); /* If function() returns, kill the thread. */
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void init_thread(struct thread *t, const char *name, int priority)
{
	ASSERT(t != NULL);
	ASSERT(PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT(name != NULL);

	memset(t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy(t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t)t + PGSIZE - sizeof(void *);
	// 추가 AS
	if (thread_mlfqs)
	{
		mlfqs_priority(t);
		list_push_back(&all_list, &t->all_elem);
	}
	else
	{
		t->priority = priority; // 원래 방식
	}

	// 추가
	t->init_priority = priority;

	t->wait_on_lock = NULL;
	list_init(&(t->donations));

	t->magic = THREAD_MAGIC;

	// 추가 AS
	t->init_priority = t->priority;
	t->niceness = NICE_DEFAULT;
	t->recent_cpu = RECENT_CPU_DEFAULT;

	// 휘건 추가
	t->runn_file = NULL;

	list_init(&t->child_list);
	sema_init(&t->fork_sema, 0);
	sema_init(&t->exit_sema, 0);
	sema_init(&t->wait_sema, 0);
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run(void)
{
	if (list_empty(&ready_list))
		return idle_thread;
	else
		return list_entry(list_pop_front(&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void do_iret(struct intr_frame *tf)
{
	__asm __volatile(
		"movq %0, %%rsp\n"
		"movq 0(%%rsp),%%r15\n"
		"movq 8(%%rsp),%%r14\n"
		"movq 16(%%rsp),%%r13\n"
		"movq 24(%%rsp),%%r12\n"
		"movq 32(%%rsp),%%r11\n"
		"movq 40(%%rsp),%%r10\n"
		"movq 48(%%rsp),%%r9\n"
		"movq 56(%%rsp),%%r8\n"
		"movq 64(%%rsp),%%rsi\n"
		"movq 72(%%rsp),%%rdi\n"
		"movq 80(%%rsp),%%rbp\n"
		"movq 88(%%rsp),%%rdx\n"
		"movq 96(%%rsp),%%rcx\n"
		"movq 104(%%rsp),%%rbx\n"
		"movq 112(%%rsp),%%rax\n"
		"addq $120,%%rsp\n"
		"movw 8(%%rsp),%%ds\n"
		"movw (%%rsp),%%es\n"
		"addq $32, %%rsp\n"
		"iretq"
		: : "g"((uint64_t)tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch(struct thread *th)
{
	uint64_t tf_cur = (uint64_t)&running_thread()->tf;
	uint64_t tf = (uint64_t)&th->tf;
	ASSERT(intr_get_level() == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile(
		/* Store registers that will be used. */
		"push %%rax\n"
		"push %%rbx\n"
		"push %%rcx\n"
		/* Fetch input once */
		"movq %0, %%rax\n"
		"movq %1, %%rcx\n"
		"movq %%r15, 0(%%rax)\n"
		"movq %%r14, 8(%%rax)\n"
		"movq %%r13, 16(%%rax)\n"
		"movq %%r12, 24(%%rax)\n"
		"movq %%r11, 32(%%rax)\n"
		"movq %%r10, 40(%%rax)\n"
		"movq %%r9, 48(%%rax)\n"
		"movq %%r8, 56(%%rax)\n"
		"movq %%rsi, 64(%%rax)\n"
		"movq %%rdi, 72(%%rax)\n"
		"movq %%rbp, 80(%%rax)\n"
		"movq %%rdx, 88(%%rax)\n"
		"pop %%rbx\n" // Saved rcx
		"movq %%rbx, 96(%%rax)\n"
		"pop %%rbx\n" // Saved rbx
		"movq %%rbx, 104(%%rax)\n"
		"pop %%rbx\n" // Saved rax
		"movq %%rbx, 112(%%rax)\n"
		"addq $120, %%rax\n"
		"movw %%es, (%%rax)\n"
		"movw %%ds, 8(%%rax)\n"
		"addq $32, %%rax\n"
		"call __next\n" // read the current rip.
		"__next:\n"
		"pop %%rbx\n"
		"addq $(out_iret -  __next), %%rbx\n"
		"movq %%rbx, 0(%%rax)\n" // rip
		"movw %%cs, 8(%%rax)\n"	 // cs
		"pushfq\n"
		"popq %%rbx\n"
		"mov %%rbx, 16(%%rax)\n" // eflags
		"mov %%rsp, 24(%%rax)\n" // rsp
		"movw %%ss, 32(%%rax)\n"
		"mov %%rcx, %%rdi\n"
		"call do_iret\n"
		"out_iret:\n"
		: : "g"(tf_cur), "g"(tf) : "memory");
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status)
{
	ASSERT(intr_get_level() == INTR_OFF);
	ASSERT(thread_current()->status == THREAD_RUNNING);
	while (!list_empty(&destruction_req))
	{
		struct thread *victim =
			list_entry(list_pop_front(&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current()->status = status;
	schedule();
}

static void
schedule(void)
{
	struct thread *curr = running_thread();
	struct thread *next = next_thread_to_run();

	ASSERT(intr_get_level() == INTR_OFF);
	ASSERT(curr->status != THREAD_RUNNING);
	ASSERT(is_thread(next));
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate(next);
#endif

	if (curr != next)
	{
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread)
		{
			ASSERT(curr != next);
			list_push_back(&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch(next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid(void)
{
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire(&tid_lock);
	tid = next_tid++;
	lock_release(&tid_lock);

	return tid;
}

// 추가--------------------
void thread_sleep(int64_t ticks)
{
	struct thread *curr;
	enum intr_level old_level;
	old_level = intr_disable(); // 인터럽트 비활성

	curr = thread_current();	 // 현재 스레드
	ASSERT(curr != idle_thread); // 현재 스레드가 idle이 아닐 때만

	curr->wakeup_ticks = ticks;												   // 일어날 시각 저장
	list_insert_ordered(&sleep_list, &curr->elem, compare_thread_ticks, NULL); // sleep_list에 추가
	thread_block();															   // 현재 스레드 재우기

	intr_set_level(old_level); // 인터럽트 상태를 원래 상태로 변경
}

// 두 스레드의 wakeup_ticks를 비교해서 작으면 true를 반환하는 함수
bool compare_thread_ticks(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
	struct thread *st_a = list_entry(a, struct thread, elem);
	struct thread *st_b = list_entry(b, struct thread, elem);
	return st_a->wakeup_ticks < st_b->wakeup_ticks;
}

void thread_wakeup(int64_t current_ticks)
{
	enum intr_level old_level;
	old_level = intr_disable(); // 인터럽트 비활성

	struct list_elem *curr_elem = list_begin(&sleep_list);
	while (curr_elem != list_end(&sleep_list))
	{
		struct thread *curr_thread = list_entry(curr_elem, struct thread, elem); // 현재 검사중인 elem의 스레드

		if (current_ticks >= curr_thread->wakeup_ticks) // 깰 시간이 됐으면
		{
			curr_elem = list_remove(curr_elem); // sleep_list에서 제거, curr_elem에는 다음 elem이 담김
			thread_unblock(curr_thread);		// ready_list로 이동
			check_preemption();					// 추가
		}
		else
			break;
	}
	intr_set_level(old_level); // 인터럽트 상태를 원래 상태로 변경
}

void check_preemption(void)
{
	if (thread_current() == idle_thread)
		return;
	if (list_empty(&ready_list)) // 대기 리스트가 비었다면 함수종료
		return;
	struct thread *curr = thread_current();
	struct thread *ready = list_entry(list_front(&ready_list), struct thread, elem);
	if (curr->priority < ready->priority)
	{
		if (intr_context())
		{
			intr_yield_on_return();
		}
		else
		{
			thread_yield();
		}
		// 	thread_yield();
	}
}
// void check_preemption(void)
// {
// 	if (thread_mlfqs)
// 	{
// 		// MLFQS 모드일 때 우선순위 비교
// 		struct thread *current_thread = thread_current();
// 		if (!list_empty(&ready_list))
// 		{
// 			struct thread *highest_priority_thread = list_entry(list_begin(&ready_list), struct thread, elem);
// 			if (highest_priority_thread->priority > current_thread->priority)
// 			{
// 				// 우선순위가 더 높은 스레드가 있으면 현재 스레드를 선점
// 				intr_yield_on_return();
// 			}
// 		}
// 	}
// 	else
// 	{
// 		if (thread_current() == idle_thread)
// 			return;
// 		if (list_empty(&ready_list)) // 대기 리스트가 비었다면 함수종료
// 			return;
// 		struct thread *curr = thread_current();
// 		struct thread *ready = list_entry(list_front(&ready_list), struct thread, elem);
// 		if (curr->priority < ready->priority)
// 			thread_yield();
// 	}
// }

// 추가 AS
// void mlfqs_priority(struct thread *t)
// {
// 	if (t == idle_thread)
// 		return;

// 	t->priority = fp_to_int(add_mixed(div_mixed(t->recent_cpu, -4), PRI_MAX - t->niceness * 2));
// }

void mlfqs_priority(struct thread *t)
{
	if (t != idle_thread)
	{
		int rec_by_4 = div_mixed(t->recent_cpu, 4);
		int nice2 = 2 * t->niceness;
		int to_sub = add_mixed(rec_by_4, nice2);
		int tmp = sub_mixed(to_sub, (int)PRI_MAX);
		int pri_result = fp_to_int(sub_fp(0, tmp));

		if (pri_result < PRI_MIN)
			pri_result = PRI_MIN;
		if (pri_result > PRI_MAX)
			pri_result = PRI_MAX;

		t->priority = pri_result;
	}
}

void mlfqs_recent_cpu(struct thread *t)
{
	if (t == idle_thread)
		return;

	t->recent_cpu = add_mixed(mult_fp(div_fp(mult_mixed(load_avg, 2), add_mixed(mult_mixed(load_avg, 2), 1)), t->recent_cpu), t->niceness);
}

void mlfqs_load_avg(void)
{
	int ready_threads;

	ready_threads = list_size(&ready_list);

	if (thread_current() != idle_thread)
		ready_threads++;

	load_avg = add_fp(mult_fp(div_fp(int_to_fp(59), int_to_fp(60)), load_avg), mult_mixed(div_fp(int_to_fp(1), int_to_fp(60)), ready_threads));

	// printf("Debug: load_avg = %d.%02d\n", load_avg / 100, load_avg % 100);
}

void mlfqs_increment(void)
{
	if (thread_current() == idle_thread)
		return;
	//  현재 쓰레드의 recent_cpu 값을 1 증가
	thread_current()->recent_cpu = add_mixed(thread_current()->recent_cpu, 1);
}

void mlfqs_recalc_recent_cpu(void) // 시스템의 모든 스레드 recent_cpu 재계산
{
	struct list_elem *e = list_begin(&all_list);
	struct thread *t = NULL;

	while (e != list_end(&all_list))
	{
		t = list_entry(e, struct thread, all_elem);
		mlfqs_recent_cpu(t);

		// printf("Debug: thread %p recent_cpu = %d.%02d\n", (void *)t, t->recent_cpu / 100, t->recent_cpu % 100);

		e = list_next(e);
	}
}

/** project1-Advanced Scheduler */
void mlfqs_recalc_priority(void) // 시스템의 모든 스레드 우선순위 재계산
{
	struct list_elem *e = list_begin(&all_list);
	struct thread *t = NULL;

	while (e != list_end(&all_list))
	{
		t = list_entry(e, struct thread, all_elem);
		mlfqs_priority(t); // 해당 쓰레드 우선순위 재계산

		e = list_next(e); // 다음 요소로
	}
}