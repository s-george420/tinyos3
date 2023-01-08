
#include <assert.h>
#include "kernel_cc.h"
#include "kernel_proc.h"
#include "kernel_streams.h"


/* 
 The process table and related system calls:
 - Exec
 - Exit
 - WaitPid
 - GetPid
 - GetPPid

 */

/* The process table */
PCB PT[MAX_PROC];
unsigned int process_count;

PCB* get_pcb(Pid_t pid)
{
  return PT[pid].pstate==FREE ? NULL : &PT[pid];
}

Pid_t get_pid(PCB* pcb)
{
  return pcb==NULL ? NOPROC : pcb-PT;
}

/* Initialize a PTCB */
void initialize_PTCB(PTCB* ptcb)
{

  ptcb->argl = 0;
  ptcb->args = NULL;
  ptcb->refcount = 0;
  ptcb->detached = 0;
  ptcb->exited = 0;
  ptcb->exitval = 0;
  ptcb->tcb = NULL;
  ptcb->task = NULL;
  rlnode_init(& ptcb->ptcb_list_node, ptcb); 
  ptcb->exit_cv = COND_INIT; 
}

/* Initialize a PCB */
static inline void initialize_PCB(PCB* pcb)
{
  pcb->pstate = FREE;
  pcb->argl = 0;
  pcb->args = NULL;

  for(int i=0;i<MAX_FILEID;i++)
    pcb->FIDT[i] = NULL;

  rlnode_init(& pcb->children_list, NULL);
  rlnode_init(& pcb->exited_list, NULL);
  rlnode_init(& pcb->children_node, pcb);
  rlnode_init(& pcb->exited_node, pcb);
  pcb->child_exit = COND_INIT;

  rlnode_init(& pcb->ptcb_list, pcb);   //PCB contains now a list of ptcbs
  pcb->thread_count = 0;                //each PCB has many threads now
}


static PCB* pcb_freelist;

void initialize_processes()
{
  /* initialize the PCBs */
  for(Pid_t p=0; p<MAX_PROC; p++) {
    initialize_PCB(&PT[p]);
  }

  /* use the parent field to build a free list */
  PCB* pcbiter;
  pcb_freelist = NULL;
  for(pcbiter = PT+MAX_PROC; pcbiter!=PT; ) {
    --pcbiter;
    pcbiter->parent = pcb_freelist;
    pcb_freelist = pcbiter;
  }

  process_count = 0;

  /* Execute a null "idle" process */
  if(Exec(NULL,0,NULL)!=0)
    FATAL("The scheduler process does not have pid==0");
}


/*
  Must be called with kernel_mutex held
*/
PCB* acquire_PCB()
{
  PCB* pcb = NULL;

  if(pcb_freelist != NULL) {
    pcb = pcb_freelist;
    pcb->pstate = ALIVE;
    pcb_freelist = pcb_freelist->parent;
    process_count++;
  }

  return pcb;
}

/*
  Must be called with kernel_mutex held
*/
void release_PCB(PCB* pcb)
{
  pcb->pstate = FREE;
  pcb->parent = pcb_freelist;
  pcb_freelist = pcb;
  process_count--;
}


/*
 *
 * Process creation
 *
 */

/*
	This function is provided as an argument to spawn,
	to execute the main thread of a process.
*/
void start_main_thread()
{
  int exitval;

  Task call =  CURPROC->main_task;
  int argl = CURPROC->argl;
  void* args = CURPROC->args;

  exitval = call(argl,args);
  Exit(exitval);
}


//creating process threads
void start_thread()
{
  int exitval;

  Task call = cur_thread()->ptcb->task;
  int argl = cur_thread()->ptcb->argl;
  void* args = cur_thread()->ptcb->args;

  exitval = call(argl,args);
  ThreadExit(exitval);
}


/*
	System call to create a new process.
 */
Pid_t sys_Exec(Task call, int argl, void* args)
{
  PCB *curproc, *newproc;
  
  /* The new process PCB */
  newproc = acquire_PCB();

  if(newproc == NULL) goto finish;  /* We have run out of PIDs! */

  if(get_pid(newproc)<=1) {
    /* Processes with pid<=1 (the scheduler and the init process) 
       are parentless and are treated specially. */
    newproc->parent = NULL;
  }
  else
  {
    /* Inherit parent */
    curproc = CURPROC;

    /* Add new process to the parent's child list */
    newproc->parent = curproc;
    rlist_push_front(& curproc->children_list, & newproc->children_node);

    /* Inherit file streams from parent */
    for(int i=0; i<MAX_FILEID; i++) {
       newproc->FIDT[i] = curproc->FIDT[i];
       if(newproc->FIDT[i])
          FCB_incref(newproc->FIDT[i]);
    }
  }


  /* Set the main thread's function */
  newproc->main_task = call;

  /* Copy the arguments to new storage, owned by the new process */
  newproc->argl = argl;
  if(args!=NULL) {
    newproc->args = malloc(argl);
    memcpy(newproc->args, args, argl);
  }
  else
    newproc->args=NULL;

  /* 
    Create and wake up the thread for the main function. This must be the last thing
    we do, because once we wakeup the new thread it may run! so we need to have finished
    the initialization of the PCB.
   */
  if(call != NULL) {

   // TCB* main_thread = spawn_thread(newproc, start_main_thread); 

    newproc-> main_thread = spawn_thread(newproc, start_main_thread); //Create the new processes' main thread

    PTCB* ptcb = xmalloc(sizeof(PTCB)); //Allocate space for a new PTCB that will be connected with the main thread
    //Initialize PTCB
    initialize_PTCB(ptcb);
    ptcb->argl = argl;
    ptcb->args = args;
    ptcb->task = call;
    //make needed connections with PCB and TCB
    ptcb->tcb = newproc->main_thread; //Connect the new PTCB with the TCB main thread
    newproc->main_thread->ptcb = ptcb; //Connect the new PCB with the new PTCB
    rlist_push_back(&newproc->ptcb_list, &ptcb->ptcb_list_node); //The PTCB is added to the new PCB's list of PTCBs
    newproc->thread_count++; //Thread count has to be increased
    wakeup(newproc->main_thread); //Finally wake up the thread
  }


finish:
  return get_pid(newproc);
}


/* System call */
Pid_t sys_GetPid()
{
  return get_pid(CURPROC);
}


Pid_t sys_GetPPid()
{
  return get_pid(CURPROC->parent);
}


static void cleanup_zombie(PCB* pcb, int* status)
{
  if(status != NULL)
    *status = pcb->exitval;

  rlist_remove(& pcb->children_node);
  rlist_remove(& pcb->exited_node);

  release_PCB(pcb);
}


static Pid_t wait_for_specific_child(Pid_t cpid, int* status)
{

  /* Legality checks */
  if((cpid<0) || (cpid>=MAX_PROC)) {
    cpid = NOPROC;
    goto finish;
  }

  PCB* parent = CURPROC;
  PCB* child = get_pcb(cpid);
  if( child == NULL || child->parent != parent)
  {
    cpid = NOPROC;
    goto finish;
  }

  /* Ok, child is a legal child of mine. Wait for it to exit. */
  while(child->pstate == ALIVE)
    kernel_wait(& parent->child_exit, SCHED_USER);
  
  cleanup_zombie(child, status);
  
finish:
  return cpid;
}


static Pid_t wait_for_any_child(int* status)
{
  Pid_t cpid;

  PCB* parent = CURPROC;

  /* Make sure I have children! */
  int no_children, has_exited;
  while(1) {
    no_children = is_rlist_empty(& parent->children_list);
    if( no_children ) break;

    has_exited = ! is_rlist_empty(& parent->exited_list);
    if( has_exited ) break;

    kernel_wait(& parent->child_exit, SCHED_USER);    
  }

  if(no_children)
    return NOPROC;

  PCB* child = parent->exited_list.next->pcb;
  assert(child->pstate == ZOMBIE);
  cpid = get_pid(child);
  cleanup_zombie(child, status);

  return cpid;
}


Pid_t sys_WaitChild(Pid_t cpid, int* status)
{
  /* Wait for specific child. */
  if(cpid != NOPROC) {
    return wait_for_specific_child(cpid, status);
  }
  /* Wait for any child */
  else {
    return wait_for_any_child(status);
  }

}


void sys_Exit(int exitval)
{

  PCB *curproc = CURPROC;  /* cache for efficiency */

  /* First, store the exit status */
  curproc->exitval = exitval;

  sys_ThreadExit(exitval);
}

static file_ops procinfo_file_ops = {
  .Open = NULL,
  .Read = procinfo_read,
  .Write = NULL,
  .Close = procinfo_close
};

Fid_t sys_OpenInfo()
{
  Fid_t fid;
  FCB* fcb;

  if(FCB_reserve(1, &fid, &fcb) == 0) {
    return -1;
  }

  procinfo_cb * proc_cb = (procinfo_cb*)xmalloc(sizeof(procinfo_cb));

  proc_cb->pcb_cursor = 0;
  fcb->streamobj = proc_cb;
  fcb->streamfunc = &procinfo_file_ops;
  
	return fid;
}

int procinfo_read(void* _procinfo_cb, char* buf, unsigned int size)
{
  //cast
  procinfo_cb* proc_cb = (procinfo_cb*) _procinfo_cb;

  if(proc_cb==NULL){
    return -1;
  }

  while(proc_cb->pcb_cursor < MAX_PROC && PT[proc_cb->pcb_cursor].pstate == FREE) {
    proc_cb->pcb_cursor++;
  }
  
  if(proc_cb->pcb_cursor == MAX_PROC) {
    return 0;
  }

  procinfo* proc_info = proc_cb -> info;
  proc_info = xmalloc(sizeof(procinfo));

  PCB tmp_pcb = PT[proc_cb->pcb_cursor];
  

  proc_info->pid = get_pid(&PT[proc_cb->pcb_cursor]);
  proc_info->ppid = get_pid(tmp_pcb.parent);

  if(tmp_pcb.pstate == ZOMBIE){
    proc_info->alive = 0;
  }
  else{
    proc_info->alive = 1;
  }

  proc_info->thread_count = tmp_pcb.thread_count;
  proc_info->main_task = tmp_pcb.main_task;
  proc_info->argl = tmp_pcb.argl;

  if(tmp_pcb.argl < PROCINFO_MAX_ARGS_SIZE) {
    memcpy(proc_info->args, tmp_pcb.args, tmp_pcb.argl);
  }
  else {
    memcpy(proc_info->args, tmp_pcb.args, PROCINFO_MAX_ARGS_SIZE);
  }
 
  memcpy(buf, proc_info, size);

  free(proc_info);

  proc_cb->pcb_cursor++;   
  
  return size;
}

int procinfo_close(void* proccb){
  //cast
  procinfo_cb* proc_cb = (procinfo_cb*) proccb;

  if(proc_cb == NULL){
    return -1;
  }

  free(proc_cb);

  return 0;
}