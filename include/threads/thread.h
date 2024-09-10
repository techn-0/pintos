#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#include "threads/synch.h" // 휘건 추가

#ifdef VM
#include "vm/vm.h"
#endif
// 휘건 추가
#define USERPROG


/* States in a thread's life cycle. */
enum thread_status
{
	THREAD_RUNNING, /* Running thread. */
	THREAD_READY,	/* Not running but ready to run. */
	THREAD_BLOCKED, /* Waiting for an event to trigger. */
	THREAD_DYING	/* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) - 1) /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0	   /* Lowest priority. */
#define PRI_DEFAULT 31 /* Default priority. */
#define PRI_MAX 63	   /* Highest priority. */
// 추가 AS
#define NICE_DEFAULT 0
#define RECENT_CPU_DEFAULT 0
#define LOAD_AVG_DEFAULT 0
// 휘건 추가
#define FDT_PAGES 3 // test `multi-oom` 테스트용
#define FDCOUNT_LIMIT FDT_PAGES * (1 << 9)


/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread
{
	/* Owned by thread.c. */
	tid_t tid;				   /* Thread identifier. */
	enum thread_status status; /* Thread state. */
	char name[16];			   /* Name (for debugging purposes). */
	int priority;			   /* Priority. */

	int64_t wakeup_ticks; // 일어날 시각 추가

	/* Shared between thread.c and synch.c. */
	struct list_elem elem; /* List element. */

	// 추가
	int init_priority;				// 원래 우선순위로 돌아와야 하니 원래 우선순위를 담아둘거임
	struct lock *wait_on_lock;		// 현재 획득하려고 기다리고 있는 락
	struct list donations;			// 나에게 우선순위를 도네이트한 다른 스레드들의 목록
	struct list_elem donation_elem; // 스레드가 도네이션 리스트에 추가될 때, 이 donation_elem을 통해 리스트에 연결
	// 추가 AS
	int niceness;
	int recent_cpu;
	struct list_elem all_elem;
	//
	// struct file *runn_file; // 실행중인 파일

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4; /* Page map level 4 */
	// 휘건 추가
	int exit_status;   // exit(), wait() 에 사용할 변수 선언
	
	int fd_idx;              // 파일 디스크립터 인덱스
    struct file **fdt;       // 파일 디스크립터 테이블
	struct file *runn_file;  // 실행중인 파일
	
	// struct file *runn_file; // 실행중인 파일
	struct intr_frame parent_if; // 부모 프로세스 if
	struct list child_list;
	struct list_elem child_elem;
	struct semaphore fork_sema; // fork가 완료될 때 signal
	struct semaphore exit_sema; // 자식 프로세스 종료 signal
	struct semaphore wait_sema; // exit_sema를 기다릴 때 사용
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf; /* Information for switching */
	unsigned magic;		  /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init(void);
void thread_start(void);

void thread_tick(void);
void thread_print_stats(void);

typedef void thread_func(void *aux);
tid_t thread_create(const char *name, int priority, thread_func *, void *);

void thread_block(void);
void thread_unblock(struct thread *);

struct thread *thread_current(void);
tid_t thread_tid(void);
const char *thread_name(void);

void thread_exit(void) NO_RETURN;
void thread_yield(void);

int thread_get_priority(void);
void thread_set_priority(int);

int thread_get_nice(void);
void thread_set_nice(int);
int thread_get_recent_cpu(void);
int thread_get_load_avg(void);

void do_iret(struct intr_frame *tf);

// 추가--------------------------------------
void thread_sleep(int64_t ticks);
bool compare_thread_ticks(const struct list_elem *a, const struct list_elem *b, void *aux);
void thread_wakeup(int64_t global_ticks);
bool compare_thread_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);
void check_preemption(void);
bool compare_sema_priority(const struct list_elem *a, const struct list_elem *b, void *aux);
bool compare_donation_priority(const struct list_elem *a, const struct list_elem *b, void *aux);
void donate_priority(void);
void remove_donor(struct lock *lock);
void update_priority_before_donations(void);

// 추가 AS
void mlfqs_priority(struct thread *t);
void mlfqs_recent_cpu(struct thread *t);
void mlfqs_load_avg(void);
void mlfqs_increment(void);
void mlfqs_recalc_recent_cpu(void);
void mlfqs_recalc_priority(void);

#endif /* threads/thread.h */
