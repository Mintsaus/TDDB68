#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include <lib/kernel/bitmap.h>
#include <lib/kernel/list.h>
#include "threads/vaddr.h"
#include "userprog/pagedir.h"

static void syscall_handler (struct intr_frame *);
void power_off(void);
void putbuf(const char *buffer, size_t n);
void exit(int status);
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
  //Check that stack pointer is ok
  //printf("syscall before *p check \n");
  if(!is_user_vaddr((const void *)p) || pagedir_get_page(curr_thread->pagedir, (const void *)p) == NULL)
  {
    //printf("syscall *p check failed \n");
    exit(-1);
    }
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
    //printf("SYS_CREATE\n");
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
		
		
		/* KOLLA HÄR LUDWIG - Vad jag gjort 10/3 
     * 
     * Jag upptäckte när jag kollade närmare på stacken att när argv pushades så pekade de på fel argument. 
     * argv[3] pekade på adressen till argument 0, argv[2] till argument 1 osv. 
     * 
     * För att fixa detta ändrade jag så att vi nu pushar argumenten i motsatt ordning alltså 3,2,1,0 istället för 0,1,2,3. 
     * Detta reflekterar stanfordexemplet bättre. Gamla koden är bortkommenterad. Jag har försökt kommentera det nya jag har gjort så bra som möjligt.
     * Jämför gärna stackutskriften (är bortkommenterad nu) med stanfordexemplet för att konfirmera att det blir rätt. Ser bra ut enligt mig.
     * Snackade nu med Erik och han tycker det verkar se rätt ut också.
     * 
     * Tyvärr löste inte detta några problem... :(
     * 
     * Erik misstänkte att det var något knas med att strtok_r (i proc_exec) sabbade något fn_copy. Vi skapade därför en fn_copy2 som
     * används enbart för att skapa file_name_no_args. Detta löste problemet.
     * 
     * Tyvärr får vi fortfarande en crash när programmet körs klart. Antagligen något fel med exit säger Erik.
     * Jag har lokaliserat problemet till när cs_parent ska accessas vad jag tror är sista gången. Den verkar helt enkelt inte finnas eller så får vi inte accessa den.
     * Här låg det locks som var bortkommenterade som jag la till igen.
     * 
     * Snackade mer med Erik och han tror att det antagligen är urtråden som ställer till det eftersom den inte har en parent. Jag la till en check om cs_parent existerar. 
     * Detta tar bort crashen, men skapar istället en deadlock. 
     * Erik snackade om att det möjligtvis kan vara något med när process_wait i init.c kallas och att parent då aldrig släpps. (Hängde inte helt med)
     * 
     * Checken jag la till gör att det blir en sema_up som aldrig körs. Detta kan också vara det som orsakar problemet. Behöver tänka på var motsvarade sema_down sätts.
     * 
     * En sista tanke innan jag drar. I process_execute verkar vi har snurrat till det lite. Vet inte vad tanken är med tid:en som ska returnas och är för trött för att tänka, 
     * men tid returnas för thread_create, sedan sätt cs->pid = tid, lite senare sätts tid = cs->pid = tid. Crazy shit helt enkelt. Vet inte om det har något med det här att göra
     * men värt att tänka på vad som ska ske där egentligen.
     * */	
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
			//printf("Calling process_execute in thread: %d \n", curr_thread->tid);
			tid = process_execute(name);
			//printf("Done with process_execute in thread: %d \n", curr_thread->tid);
			f->eax = tid;
			break;
			
		case (SYS_EXIT):
			status = *(p + 1);
      //a change
			//printf("SYS_EXIT in thread: %d \n", curr_thread -> tid);
			
      //kolla om parent existerar?? Fråga Erik
			exit(status);
      
			break;
			
		/*Fråga Erik om "Reconsider all the situations under the condition that the child does not 
		 * exit normally" */	
		case (SYS_WAIT):
			tid = *(p + 1);
			f->eax = process_wait(tid);
			break;			
			
		default:
			printf ("system call!\n");
			thread_exit ();
	}
	
}

void exit(int status){
    struct child_status *cs_parent;
    struct thread *curr_thread = thread_current();
    cs_parent = curr_thread->cs_parent;
    cs_parent->exit_status = status;
    
    printf("%s: exit(%d)\n", curr_thread -> name, status);
    thread_exit();
	}
