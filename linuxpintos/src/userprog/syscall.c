#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include <lib/kernel/bitmap.h>

static void syscall_handler (struct intr_frame *);
void power_off(void);
void putbuf(const char *buffer, size_t n);
uint8_t input_getc(void);
int fd_ok(int fd, struct bitmap *map);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

int fd_ok(int fd, struct bitmap *map)
	{
		if ((fd < (int)bitmap_size(map)) && bitmap_test(map, fd))
		{
			return 1;
		}else
		{
			return 0;
		} 
	}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{	
	int fd;
	int status;
	size_t size;
	uint8_t key;
	char *buffer;
	char *name;
	struct file *file_handle;
	struct thread *curr_thread = thread_current();				/* Gets current thread*/
	struct bitmap *fd_map = curr_thread->fd_bitmap;				/* Gets bitmap from current thread*/
	struct file **file_names = curr_thread->file_names;			/* Gets pointer to array of file names from current thread */
	int *p = f->esp;
	switch(*p)
	{
		case (SYS_HALT)://Done			
		power_off();
			break;
			
		case (SYS_READ): //Done	
			fd = *(p + 1);
			buffer = (char *)(*(p + 2));
			size = *(p + 3);
			if(fd == 0) /* Reads from console */
			{
				int i;
				for(i = 0; i < (int)size; i++) 
				{
					key = input_getc();
					buffer[i] = (char)key;
				}
				f->eax = i;
			} else if(fd != 1 && fd_ok(fd, fd_map)) /* Reads from file, checks if ok fd */
			{
				f->eax = file_read(file_names[fd], buffer, size);
			}else /* Something went wrong */
			{
				f->eax = -1;
			}
			break;

		case (SYS_WRITE)://Done
			fd = *(p + 1);
			buffer = (char *)(*(p + 2));
			size = *(p + 3);
			if(fd == 1){									/* Writes to console */
				putbuf(buffer, size);
			}else if(fd == 0 || !fd_ok(fd, fd_map)){		/*invalid fd */
				f->eax = -1;
				break;
			}else{											/* Writes to file */
				file_handle = file_names[fd];
				status = file_write(file_handle, buffer, size);
				
				if(status == 0){
					f->eax = -1;
				}else{
					f->eax = status;
				}
				
			}
			break;
		
		case (SYS_CREATE)://Done
			name = (char *)(*(p + 1));
			size = *(p + 2);
			f -> eax = filesys_create(name, size);
			break;
		
		case (SYS_OPEN)://Done
			name = (char *)(*(p + 1));								/* Get name of file */
			fd = (int)bitmap_scan_and_flip(fd_map, 0, 1, 0);		/* Get next free position in bitmap (fd) */
			if(fd == (int)BITMAP_ERROR || (file_handle = filesys_open(name)) == NULL){
				f -> eax = -1;
			} else 
			{
				file_names[fd] = file_handle;
				f -> eax = fd;
			}
			break;
			
		case (SYS_CLOSE)://Done
			fd = *(p + 1);
			if(!fd_ok(fd, fd_map)){ break; }
			file_handle = file_names[fd];
			file_names[fd] = NULL;
			bitmap_set(fd_map, fd, 0);
			file_close(file_handle);
			break;
		
		case (SYS_EXIT):
			thread_exit();
			
		default:
			printf ("system call!\n");
			thread_exit ();
	}
	
	
  
}
