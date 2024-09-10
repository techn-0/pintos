#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

#include "filesys/off_t.h"
#include "filesys/file.h"
#include "threads/palloc.h"
#include <string.h>
typedef int pid_t;

void syscall_entry(void);
void syscall_handler(struct intr_frame *);
// 휘건 추가
struct lock filesys_lock; // 파일 읽기/쓰기 용 lock
// typedef int pid_t;
// typedef int32_t off_t;
// #define PAL_ZERO 002

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081			/* Segment selector msr */
#define MSR_LSTAR 0xc0000082		/* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void syscall_init(void)
{
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
							((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			  FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

	// 휘건 추가
	lock_init(&filesys_lock); // read & write 용 lock 초기화
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED)
{
	// TODO: Your implementation goes here.
	// %rdi %rsi %rdx %r10 %r8 %r9
	int sys_number = f->R.rax;

	switch (sys_number)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		exit(f->R.rdi);
		break;
	case SYS_FORK:
		f->R.rax = fork(f->R.rdi);
		break;
	case SYS_EXEC:
		f->R.rax = exec(f->R.rdi);
		break;
	case SYS_WAIT:
		f->R.rax = process_wait(f->R.rdi);
		break;
	case SYS_CREATE:
		f->R.rax = create(f->R.rdi, f->R.rsi);
		break;
	case SYS_REMOVE:
		f->R.rax = remove(f->R.rdi);
		break;
	case SYS_OPEN:
		f->R.rax = open(f->R.rdi);
		break;
	case SYS_FILESIZE:
		f->R.rax = filesize(f->R.rdi);
		break;
	case SYS_READ:
		f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_WRITE:
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_SEEK:
		seek(f->R.rdi, f->R.rsi);
		break;
	case SYS_TELL:
		f->R.rax = tell(f->R.rdi);
		break;
	case SYS_CLOSE:
		close(f->R.rdi);
		break;
	default:
		exit(-1);
	}
	// printf ("system call!\n");
	// thread_exit ();
}

// 휘건 추가
void check_address(void *addr) // 주소값이 user영역에서 사용하는 주소값인지 확인
{
	if (is_kernel_vaddr(addr) || addr == NULL || pml4_get_page(thread_current()->pml4, addr) == NULL)
		exit(-1); // 유저 영역 아니면 강종
}

void halt(void) // 핀토스 종료 sys-call
{
	power_off();
}

void exit(int status) // 현재 프로세스 종료 sys-call
{
	struct thread *t = thread_current();
	t->exit_status = status;
	printf("%s: exit(%d)\n", t->name, t->exit_status); // Process Termination Message
	thread_exit();
}

bool create(const char *file, unsigned initial_size) // 파일 생성 sys-call
{
	// file: 생성 파일이름, 경로
	// initial_size: 파일 크기
	check_address(file);

	return filesys_create(file, initial_size); // 성공 시 T / 실패 시 F
}

bool remove(const char *file) // 파일 삭제 sys-call
{
	check_address(file); // file: 생성 파일이름, 경로

	return filesys_remove(file); // 성공 시 T / 실패 시 F
}

int open(const char *file) // 파일 열기 sys-call
{
	// check_address(file);
	// struct file *newfile = filesys_open(file);

	// if (newfile == NULL)
	// 	return -1;

	// int fd = process_add_file(newfile);

	// if (fd == -1)
	// 	file_close(newfile);

	// return fd;
	//------
	// check_address(file);
	// // Opens the file with the given NAME. returns the new file if successful or a null pointer otherwise.
	// struct file *newfile = filesys_open(file);

	// // fails if no file named NAME exists, or if an internal memory allocation fails.
	// if (newfile == NULL)
	// 	return -1;

	// // allocate file to current process fdt
	// int fd = add_file_to_fdt(newfile);

	// // FD table full
	// if (fd == -1)
	// 	file_close(newfile);

	// return fd;
	//----
	check_address(file);
	struct file *newfile = filesys_open(file);

	// fails if no file named NAME exists, or if an internal memory allocation fails.
	if (newfile == NULL)
		return -1;
	else
	{
		struct thread *t = thread_current();
		int fd;
		for (fd = 3; fd < 32; fd++)
		{
			if (t->fdt[fd] == NULL)
			{
				t->fdt[fd] = newfile;
				return fd;
			}
		}
		file_close(newfile);
		return -1;
	}
}

int filesize(int fd) // 파일 크기 sys-call
{
	struct file *file = process_get_file(fd);

	if (file == NULL)
		return -1;

	return file_length(file);
}

int read(int fd, void *buffer, unsigned length) // 열린 파일의 데이터를 읽기 sys-call
{
	check_address(buffer);

	if (fd == 0)
	{			   // 0(stdin) -> keyboard로 직접 입력
		int i = 0; // 쓰레기 값 return 방지
		char c;
		unsigned char *buf = buffer;

		for (; i < length; i++)
		{
			c = input_getc();
			*buf++ = c;
			if (c == '\0')
				break;
		}

		return i;
	}
	// 그 외의 경우
	if (fd < 3) // stdout, stderr를 읽으려고 할 경우 & fd가 음수일 경우
		return -1;

	struct file *file = process_get_file(fd);
	off_t bytes = -1;

	if (file == NULL) // 파일이 비어있을 경우
		return -1;

	lock_acquire(&filesys_lock);
	bytes = file_read(file, buffer, length);
	lock_release(&filesys_lock);

	return bytes;
}

int write(int fd, const void *buffer, unsigned length) // 열린 파일 데이터 기록 sys-call
{
	check_address(buffer);

	off_t bytes = -1;

	if (fd <= 0) // stdin에 쓰려고 할 경우 & fd 음수일 경우
		return -1;

	if (fd < 3)
	{ // 1(stdout) * 2(stderr) -> console로 출력
		putbuf(buffer, length);
		return length;
	}

	struct file *file = process_get_file(fd);

	if (file == NULL)
		return -1;

	lock_acquire(&filesys_lock);
	bytes = file_write(file, buffer, length);
	lock_release(&filesys_lock);

	return bytes;
}

void seek(int fd, unsigned position)
{
	struct file *file = process_get_file(fd);

	if (fd < 3 || file == NULL)
		return;

	file_seek(file, position);
}

int tell(int fd)
{
	struct file *file = process_get_file(fd);

	if (fd < 3 || file == NULL)
		return -1;

	return file_tell(file);
}

void close(int fd) // 파일 닫기
{
	struct file *file = process_get_file(fd);

	if (fd < 3 || file == NULL)
		return;

	process_close_file(fd);

	file_close(file);
}

pid_t fork(const char *thread_name)
{
	check_address(thread_name);

	return process_fork(thread_name, NULL);
}

int exec(const char *cmd_line)
{
	check_address(cmd_line);

	off_t size = strlen(cmd_line) + 1;
	char *cmd_copy = palloc_get_page(PAL_ZERO);

	if (cmd_copy == NULL)
		return -1;

	memcpy(cmd_copy, cmd_line, size);

	if (process_exec(cmd_copy) == -1)
		return -1;

	return 0; // process_exec 성공시 리턴 값 없음 (do_iret)
}

int wait(pid_t tid)
{
	return process_wait(tid);
}