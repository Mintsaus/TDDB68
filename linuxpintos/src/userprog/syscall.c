#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include <lib/kernel/bitmap.h>
#include <lib/kernel/list.h>

static void syscall_handler (struct intr_frame *);
void power_off(void);
void putbuf(const char *buffer, size_t n);
uint8_t input_getc(void);
int fd_ok(int fd, struct bitmap *map);
tid_t process_execute(const char *);
void free(void*);
void* malloc(size_t);

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
	tid_t tid; //Added lab 3
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
	struct list *cs_list;
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
		
		
			
		case (SYS_EXEC):
		/*	Vi har just initat en cs_list i init_thread. Nästa problem är att skapa child_status nånstans.
		 * Vi har fått tips att skicka med den som aux argument från thread_create till start_process(). 
		 * Behövs tilldelas en pekare från child till dess parents cs. 	 */
		 
		 /* Vi har gjort det som står där uppe ^. Vi har även fixat ett lås runt ref_cnt i start_process().
		  * Erik sa nånting med att inita en global lista. Kolla att det blir rätt. Vi måste också sätta upp stacken(wtf),
		  * fast det kanske var del B. Vi skulle kunna skriva ett eget testprogram där userprog A startar userprog B.
		  * Om load misslyckas i start_process ska child_status-structen tas bort så fort som möjligt. 
		  * Kanske parent kan ta bort den om pid=-1?? */
		  
		  
		  /*Får pagefault som behöer debuggas. User programs ska kunna köras innan PartB. Se kommentar på github för error-meddelande.
		   * Behöver nog lägga till prints för att se ar det går fel. Alternativt börja från committen innan det fuckade ur och lägga
		   * till en sak i taget för att se vad som orsakar felet.
		   * PHYS_BASE ger plats för tre argument längst upp på användarens minne.
		   * 
		   * */
			name = (char *)(*(p + 1));
			printf("Calling process_execute in thread: %d \n", curr_thread->tid);
			tid = process_execute(name);
			printf("Done with process_execute in thread: %d \n", curr_thread->tid);
			f->eax = tid;
			break;
			
		case (SYS_EXIT):
			status = *(p + 1);
			printf("SYS_EXIT in thread: %d \n", curr_thread -> tid);
			//kolla om parent existerar?? Fråga Erik
			struct child_status *cs_parent;
			cs_parent = curr_thread->cs_parent;
			cs_parent->exit_status = status;
			
			printf("Before lock acquire thread: %d \n", curr_thread -> tid);
			lock_acquire(&cs_parent->cs_lock);
			cs_parent->ref_cnt--;
			if(cs_parent->ref_cnt == 0){		//Parent is dead
				printf("Parent of thread %d is dead \n", curr_thread -> tid);
				lock_release(&cs_parent->cs_lock);
				free(cs_parent);
			}else {								//Parent waits or doesn't care
				printf("Parent wait or doesn't care \n");
				lock_release(&cs_parent->cs_lock);
				printf("EXIT: sema up \n");
				sema_up(&cs_parent->sema_exec);
			}
			
			//remember the children!!
			struct elem_copy{
				struct list_elem *pointer;
				struct list_elem elem;
			};
			printf("Before first for loops \n");
			cs_list = &curr_thread->cs_list;
			struct list_elem *e;
			struct child_status *cs;
			struct list children_to_delete;
			list_init(&children_to_delete);
			for (e = list_begin (cs_list); e != list_end (cs_list); e = list_next(e))
			 {
				cs = list_entry (e, struct child_status, cs_elem);
				lock_acquire(&cs->cs_lock);		
				cs->ref_cnt--;
				if(cs->ref_cnt == 0){
					struct elem_copy *ec = (struct elem_copy *)malloc(sizeof(struct elem_copy));
					ec->pointer = e;
					list_push_front(&children_to_delete, &ec->elem);
				}
				lock_release(&cs->cs_lock);			
			 }
			 printf("Children to delete is filled. Size: %d \n", list_size(&children_to_delete));
			while (!list_empty (&children_to_delete))
			{
				e = list_pop_front (&children_to_delete);
				struct elem_copy *ec = list_entry(e, struct elem_copy, elem);
				struct child_status *cs = list_entry(ec->pointer, struct child_status, cs_elem);
				list_remove(ec->pointer);
				free(ec);
				free(cs);				
			}
			printf("Thread %d has exited, with %d children remaining \n", curr_thread ->tid, list_size(&curr_thread->cs_list));
      printf("%s: exit(%d)\n", curr_thread -> name, status);
			thread_exit();
			break;
			
		/*Fråga Erik om "Reconsider all the situations under the condition that the child does not 
		 * exit normally" */	
		case (SYS_WAIT):
			tid = *(p + 1);
			cs_list = &curr_thread->cs_list;
			struct list_elem *el;
			struct child_status *cs2 = NULL;
			int exit_code;
			for (el = list_begin (cs_list); el != list_end (cs_list); el = list_next(el))	//FInds 
			 {
				cs2 = list_entry (el, struct child_status, cs_elem);
				
			   if(cs2->pid == tid){
					break;
				}					
			 }
			 if(cs2 == NULL){
				f->eax = -1;
				break; 
			}
			lock_acquire(&cs2->cs_lock);
			if(cs2->ref_cnt == 2){
				lock_release(&cs2->cs_lock);
				sema_down(&cs2->sema_exec);
			}else{ lock_release(&cs2->cs_lock); }
						
			exit_code = cs2->exit_status;
			list_remove(el);
			free(cs2);
			f->eax = exit_code;
			break;
			
			
		default:
			printf ("system call!\n");
			thread_exit ();
	}
	
	
  
}
