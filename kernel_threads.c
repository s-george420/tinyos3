
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"
#include "kernel_cc.h"
#include "kernel_streams.h"


/** 
  @brief Create a new thread in the current process.
  */
Tid_t sys_CreateThread(Task task, int argl, void* args)
{
  PCB* pcb = CURPROC;
  
  if(task==NULL){
    return NOTHREAD;
  }

  //Initialize and return a new TCB
  //TCB* tcb; //task?
  TCB* tcb = spawn_thread(pcb, start_thread);

  //Acquire a PTCB
  //allocate space
  PTCB* ptcb = xmalloc(sizeof(PTCB));
  //Initialize PTCB
  initialize_PTCB(ptcb);
  ptcb->argl = argl;
  ptcb->args = args;
  ptcb->task = task;
  //make needed connections with PCB and TCB
  ptcb->tcb = tcb;
  tcb->ptcb = ptcb;
  rlist_push_back(&pcb->ptcb_list, &ptcb->ptcb_list_node);
  /*if(task!=NULL){
    tcb = spawn_thread(pcb, start_thread);
  }*/

  pcb->thread_count++;
  
  //Wake up TCB
  wakeup(tcb);
	return (Tid_t) ptcb;
}

/**
  @brief Return the Tid of the current thread.
 */
Tid_t sys_ThreadSelf()
{
	return (Tid_t) cur_thread()->ptcb;
}

/**
  @brief Join the given thread.
  */
int sys_ThreadJoin(Tid_t tid, int* exitval)
{
  if(tid == NOTHREAD) {
    return -1;
  }

  PTCB* ptcb = (PTCB*)tid;
  
  //tid not in curproc
  if (rlist_find(& CURPROC->ptcb_list, ptcb, NULL)==NULL) {
    return -1;
  }
  //can't join self
  if (tid == sys_ThreadSelf()) {
    return -1;
  }
  
///////

  ptcb->refcount++;

  while(ptcb->exited == 0 && ptcb->detached == 0){
    kernel_wait(& ptcb->exit_cv, SCHED_USER);
  }

  ptcb->refcount--;
  if(exitval!=NULL){
    *exitval = ptcb->exitval;
}
  //can't join if detached
  if(ptcb->detached == 1) {
    return -1;
  }

  if(ptcb->refcount==0) {
    free(ptcb);
  }
	return 0;
}

/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid)
{

  
   if(tid == NOTHREAD){
    return -1;
  }

  PTCB* ptcb = (PTCB*)tid;

 

  //tid not in curproc
  if (rlist_find(& CURPROC->ptcb_list, ptcb, NULL)==NULL) {
    return -1;
  }

  //tid already exited
  if(ptcb->exited == 1){
    return -1;
  }

  ptcb->detached = 1;

  kernel_broadcast(&ptcb->exit_cv);

	return 0;
}

/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{
  PCB* curproc = CURPROC;
  PTCB* ptcb = (PTCB*) sys_ThreadSelf();

  curproc->thread_count--;
    /* 
    Here, we must check that we are not the init task. 
    If we are, we must wait until all child processes exit. 
   */
  if(curproc->thread_count==0){
    if(get_pid(curproc)==1) {

      while(sys_WaitChild(NOPROC,NULL)!=NOPROC);

    } else {

      /* Reparent any children of the exiting process to the 
        initial task */
      PCB* initpcb = get_pcb(1);
      while(!is_rlist_empty(& curproc->children_list)) {
        rlnode* child = rlist_pop_front(& curproc->children_list);
        child->pcb->parent = initpcb;
        rlist_push_front(& initpcb->children_list, child);
      }

      /* Add exited children to the initial task's exited list 
        and signal the initial task */
      if(!is_rlist_empty(& curproc->exited_list)) {
        rlist_append(& initpcb->exited_list, &curproc->exited_list);
        kernel_broadcast(& initpcb->child_exit);
      }

      /* Put me into my parent's exited list */
      rlist_push_front(& curproc->parent->exited_list, &curproc->exited_node);
      kernel_broadcast(& curproc->parent->child_exit);

    }

    assert(is_rlist_empty(& curproc->children_list));
    assert(is_rlist_empty(& curproc->exited_list));


    /* 
      Do all the other cleanup we want here, close files etc. 
    */

    /* Release the args data */
    if(curproc->args) {
      free(curproc->args);
      curproc->args = NULL;
    }

    /* Clean up FIDT */
    for(int i=0;i<MAX_FILEID;i++) {
      if(curproc->FIDT[i] != NULL) {
        FCB_decref(curproc->FIDT[i]);
        curproc->FIDT[i] = NULL;
      }
    }

    /* Disconnect my main_thread */
    curproc->main_thread = NULL; 

    /* Now, mark the process as exited. */
    curproc->pstate = ZOMBIE;
  }
    ptcb->exited=1; 
    ptcb->exitval = exitval;
    
    kernel_broadcast(&ptcb->exit_cv);
  
    /* Bye-bye cruel world */
    kernel_sleep(EXITED, SCHED_USER);
  
}

