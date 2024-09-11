#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_create_initd(const char *file_name);
tid_t process_fork(const char *name, struct intr_frame *if_);
int process_exec(void *f_name);
int process_wait(tid_t);
void process_exit(void);
void process_activate(struct thread *next);

// 추가
void argument_stack_for_user(char **argv, int argc, struct intr_frame *if_);
// void argument_stack(char **argv, int argc, struct intr_frame *if_);
// 휘건 추가
struct thread *get_child_process(int pid);
int process_add_file(struct file *f);
struct file *process_get_file(int fd);
int process_close_file(int fd);

int add_file_to_fdt(struct file *file);

#endif /* userprog/process.h */
