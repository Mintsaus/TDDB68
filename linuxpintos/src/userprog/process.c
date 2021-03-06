#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

/*-------------Lab3--------------*/
#include <stdlib.h>
void* malloc(size_t);
void free(void *);


static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  //~ printf("in process exec\n");
  char *fn_copy, *fn_copy2;
  char *file_name_no_args;
  char *save_ptr;
  tid_t tid;
  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  fn_copy2 = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);
  strlcpy (fn_copy2, file_name, PGSIZE); //Added to be used to create file_name_no_args
  file_name_no_args = strtok_r(fn_copy2, " ", &save_ptr);
  /* Lab 3 */
  struct child_status *cs = (struct child_status *)malloc(sizeof(struct child_status));
  cs->ref_cnt = 2;
  cs->fn_copy = fn_copy; //Är nu en hård kopia //palloc:as inte tänk efter om mem. leak

  /* Create a new thread to execute FILE_NAME. */
  //~ printf("Before thread create, tid: %d\n", thread_current()->tid);
  tid = thread_create (file_name_no_args, PRI_DEFAULT, start_process, cs);
  //~ printf("After thread create\n");
  /*-------------Lab3-----------------*/
	  sema_init(&cs->sema_exec, 0);
	  lock_init(&cs->cs_lock);
	  list_push_front(&thread_current()->cs_list, &cs->cs_elem);
	  cs->pid = tid;
  
	
	sema_down(&cs->sema_exec);
	
	//Get tid from child_status
	tid = cs->pid;
	//~ printf("Process exec tid: %d\n", tid);
	//~ printf("Process exec after sema_down, cs ref_cnt: %d\n", cs->ref_cnt);
	if(tid == TID_ERROR){		//om -1 misslyckades load, kan ta bort strukten
		list_remove(&cs->cs_elem);
		free(cs);
    palloc_free_page (fn_copy);
    palloc_free_page (fn_copy2);
	}
  
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
	struct thread *curr_thread = thread_current();
  //~ printf("Beginning of start_process for thread: %d \n", curr_thread->tid);
  struct child_status *cs = (struct child_status *)file_name_;
  char *file_name;
  if(curr_thread->tid >= 2){
		file_name = &cs->filename; //Lab3 file_name_ is our struct cs
	}else{
		//printf("Using old method for file_name \n");
		file_name = (char *) file_name_;
	}
  struct intr_frame if_;
  bool success;
  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  //printf(" About to load %s \n", cs->fn_copy);
  success = load (cs->fn_copy, &if_.eip, &if_.esp); //cs->fn_copy

  /* If load failed, quit. */
 
  //~ palloc_free_page (cs->fn_copy); //cs->fn_copy   free:as rätt?
  
  if (!success){ 
		//~ printf("Not success \n");
		cs->pid = -1;
		sema_up(&cs->sema_exec);
		//~ lock_acquire(&cs->cs_lock);  //This should be done in process exit, not here
		//~ cs->ref_cnt--;
		//~ lock_release(&cs->cs_lock);
		thread_exit ();
	}else{
		sema_up(&cs->sema_exec); //Causes problems. Is not initialized? Try to comment out and run lab3test, interesting stuff.
	}

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid UNUSED) 
{
  struct list *cs_list;
  struct thread *cur = thread_current();
  cs_list = &cur->cs_list;
  struct list_elem *el;
  struct child_status *cs = NULL;
  int exit_code;
  for (el = list_begin (cs_list); el != list_end (cs_list); el = list_next(el))	//Finds the child to wait for 
   {
    cs = list_entry (el, struct child_status, cs_elem);
    
     if(cs->pid == child_tid){
      break; 
    }					
   }
   if(cs == NULL){
    return -1; //Did not find the child, something went wrong
  }
  lock_acquire(&cs->cs_lock);
  if(cs->ref_cnt == 2){
    lock_release(&cs->cs_lock);
    sema_down(&cs->sema_exec);
  }else{ lock_release(&cs->cs_lock); }
        
  exit_code = cs->exit_status;
  list_remove(el);
  free(cs);
  return exit_code;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;
  //~ printf("Process exit, thread_tid: %d\n", thread_current()->tid);
   //If parent is alive: That needs to be checked
  struct child_status *cs_parent;
  cs_parent = cur->cs_parent;
  //~ printf("cs_parent ref_cont: %d\n", cs_parent->ref_cnt);
  
  // If parent dead we need to destroy the cs. If parent waiting, wake it up. //
  //~ printf("Proc_exit: Before cs_parent \n");
  if(cs_parent){
	  lock_acquire(&cs_parent->cs_lock); //----------THIS IS WHERE CRASH OCCURS-------------
	  //~ printf("Proc_exit: After cs_parent \n");
	  cs_parent->ref_cnt--;                 
	  
	  if(cs_parent->ref_cnt == 0){		//Parent is dead
		//~ printf("Parent of thread %d is dead \n", cur -> tid);
		lock_release(&cs_parent->cs_lock);
		free(cs_parent);
	  }else {								//Parent waits or doesn't care
		//~ printf("Parent wait or doesn't care \n");
		lock_release(&cs_parent->cs_lock);
		//~ printf("EXIT: sema up \n");
		sema_up(&cs_parent->sema_exec);
	  }
  }
  
  
  //Struct to be able to save pointers to the children from list
  struct elem_copy{
    struct list_elem *pointer;
    struct list_elem elem;
  };
  //Creating necessary variables for the loops
  struct list *cs_list;
  cs_list = &cur->cs_list;
  struct list_elem *e;
  struct child_status *cs;
  struct list children_to_delete;
  list_init(&children_to_delete);
  //Loops through list of children and adds the ones to be deleted to a separate list
  for (e = list_begin (cs_list); e != list_end (cs_list); e = list_next(e))
   {
	//~ printf("inside for loop\n");
    cs = list_entry (e, struct child_status, cs_elem);
    //~ printf("before lock_acquire\n");
    lock_acquire(&cs->cs_lock);
    //~ printf("after lock_acquire\n");	
    cs->ref_cnt--;
    if(cs->ref_cnt == 0){
		//~ printf("ref_cnt == 0\n");
      struct elem_copy *ec = (struct elem_copy *)malloc(sizeof(struct elem_copy));
      ec->pointer = e;
      list_push_front(&children_to_delete, &ec->elem);
    }
    lock_release(&cs->cs_lock);
    //~ printf("end of loop\n");			
   }
   //~ printf("Children to delete is filled. Size: %d \n", list_size(&children_to_delete));
   
   //~ printf("Before while in proc_exit\n");
   //Loops through the children to delete, and deletes them
   while (!list_empty (&children_to_delete))
    {
      e = list_pop_front (&children_to_delete);
      struct elem_copy *ec = list_entry(e, struct elem_copy, elem);
      struct child_status *cs = list_entry(ec->pointer, struct child_status, cs_elem);
      list_remove(ec->pointer);
      free(ec);
      free(cs);		
    } 
   //~ printf("PROC_EXIT after while\n");
   


  
	
  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
    //printf("Last in proc_exit\n");
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp, const char **arguments);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name_, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  
  const char delim[] = " ";
  char *token, *save_ptr;
  char *file_name = strtok_r(file_name_, delim, &save_ptr);
  //printf("LOAD: file_name: %s\n", file_name);
  
  const char *arguments[32];
  int arg_cnt = 0;   
  
  /* Divide file_name into arguments */
  for(token = file_name; token != NULL; token = strtok_r (NULL, delim, &save_ptr)){
    arguments[arg_cnt] = token;
    arg_cnt++;
  };
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Set up stack. */
  if (!setup_stack (esp, arguments)){
    goto done;
  }

   /* Uncomment the following line to print some debug
     information. This will be useful when you debug the program
     stack.*/
//#define STACK_DEBUG

#ifdef STACK_DEBUG
  printf("*esp is %p\nstack contents:\n", *esp);
  hex_dump((int)*esp , *esp, PHYS_BASE-*esp+16, true);
  /* The same information, only more verbose: */
  /* It prints every byte as if it was a char and every 32-bit aligned
     data as if it was a pointer. */
  void * ptr_save = PHYS_BASE;
  i=-15;
  while(ptr_save - i >= *esp) {
    char *whats_there = (char *)(ptr_save - i);
    // show the address ...
    printf("%x\t", (uint32_t)whats_there);
    // ... printable byte content ...
    if(*whats_there >= 32 && *whats_there < 127)
      printf("%c\t", *whats_there);
    else
      printf(" \t");
    // ... and 32-bit aligned content 
    if(i % 4 == 0) {
      uint32_t *wt_uint32 = (uint32_t *)(ptr_save - i);
      printf("%x\t", *wt_uint32);
      printf("\n-------");
      if(i != 0)
        printf("------------------------------------------------");
      else
        printf(" the border between KERNEL SPACE and USER SPACE ");
      printf("-------");
    }
    printf("\n");
    i++;
  }
#endif

  /* Open executable file. */
  file = filesys_open (file_name);
  
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  file_close (file);
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp, const char **arguments) 
{
  uint8_t *kpage;
  bool success = false;
	
  //printf("arguments[%d] = %s", 1, arguments[1]);
  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success){
		//printf("Great success!");
        *esp = PHYS_BASE;
      }else{
        palloc_free_page (kpage);
        //printf("Free page in setup_stack");
        return success;
	}
    }
    
    
  /*--------Pusha argument på stacken---------------------*/  
  char *argv[32]; 
  int i, argc;
  
  //Changed order in which arguments are pushed to the stack. Starting with the last argument
  for(i = 0; arguments[i] != NULL; i++){ //Counts the number of arguments, the result will be actual #args-1 since we start from zero.
	  argc = i;
  }
  
  //Pushes arguments to stack from the last to the first arg
  for(i = argc; i >= 0; i--){ 
	*esp -= strlen(arguments[i]) + 1; //+1 because of \0 at end of each string
	memcpy(*esp, arguments[i], strlen(arguments[i])+1); //the actual pushing to the stack
	argv[i] = *esp;   //saving a pointer to where on the stack each argument is placed
  }
  argc++; //This is needed to reflect the true number of args
  
  /* -------Old solution-------------------------
    for(i = 0; arguments[i] != NULL; i++){
    *esp -= strlen(arguments[i]) + 1; //+1 because of \0 at end of each string
    memcpy(*esp, arguments[i], strlen(arguments[i])); //the actual pushing to the stack
    //printf("arguments[%d] = %s \n", i, arguments[i]);
    argv[i] = *esp;   //saving a pointer to where on the stack each argument is placed
  }
  argc = i; */
  
  
  /*--------Avrunda *esp till närmsta 4-tal---------------*/
  char *np = 0;
  int round =  ((size_t) * esp)%4;
  //printf("Round %d\n", round);
  if(round>0){
	//printf("Round var > 0 \n");
	*esp -= round;
	memcpy(*esp, &np, round);
	
  }
  //printf("Avrundning gjord\n");
  
  /*--------Pusha argv, adresser till argumenten ovan-----*/
  
  //printf("Pushing sentinel \n");
  
  /* ------This was removed since starting the next step from argc accomplishes the same thing 
   *esp -= 4;
  memcpy(*esp, &np, 4);   //NULL-pointer sentinel
	*/
	
  int j;
  //printf("Pushing argv[] \n");
  for(j = argc; j >= 0; j--){ //Since this now starts from argc the first iteration will push the null-pointer sentinel. This is because &argv[argc] doesn't point to anything.
    *esp -= 4;
    memcpy(*esp, &argv[j], 4);
    argv[j] = *esp; // What does this do? Nothing?
  }
  char *argv_onstack = *esp;    //save the adress of argv on the stack to be able to point to it in next step.
  *esp -= sizeof(char **);
  memcpy(*esp, &argv_onstack, sizeof(char **));   //push pointer to argv
  *esp -= sizeof(int);
  memcpy(*esp, &argc, sizeof(int));    //push argc
  *esp -= sizeof(void *);
  memcpy(*esp, argv[argc], sizeof(void *));    //push fake return address
  return success;
  
}



/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
