#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
// 휘건 추가
#include <stdbool.h>
void check_address(void *addr);
void halt(void);
void exit(int status);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);

void syscall_init(void);

// 휘건 추가
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned length);
int write(int fd, const void *buffer, unsigned length);
void seek(int fd, unsigned position);
int tell(int fd);
void close(int fd);

#endif /* userprog/syscall.h */
