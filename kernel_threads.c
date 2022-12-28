
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

  
  TCB* tcb = spawn_thread(pcb, start_thread); //Initialize and return a new TCB

  //Acquire a PTCB
  PTCB* ptcb = xmalloc(sizeof(PTCB)); //allocate space
  
  
  initialize_PTCB(ptcb); //Initialize PTCB
  
  ptcb->argl = argl;
  ptcb->args = args;
  ptcb->task = task;
  //make needed connections with PCB and TCB
  ptcb->tcb = tcb;
  tcb->ptcb = ptcb;
  rlist_push_back(&pcb->ptcb_list, &ptcb->ptcb_list_node); //Add the new PTCB node to the end of the PCB's list of PTCBs


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

  PTCB* ptcb = (PTCB*)tid;  //get the ptcb of the given thread

  // Searches for this PTCB in the current process's list of PTCBs. If the PTCB isn't found, the function returns null
  if (rlist_find(& CURPROC->ptcb_list, ptcb, NULL)==NULL) {
    return -1;
  }
 
  //Can't join self
  if (tid == sys_ThreadSelf()) {
    return -1;
  }
  
  ptcb->refcount++; //refcount is increased since the given thread seems joinable

  //we need to wait until the given thread is exited or detached
  while(ptcb->exited == 0 && ptcb->detached == 0){
    kernel_wait(& ptcb->exit_cv, SCHED_USER); 
  }

  ptcb->refcount--;

  //can't join if detached
  if(ptcb->detached == 1) {
    return -1;
  }

  //The given PTCB's exitval is assigned to the pointed argument
  if(exitval!=NULL){
    *exitval = ptcb->exitval;
  }
  
  //If the given PTCB is exited and has refcount = 0 then it is no longer needed 
  if(ptcb->refcount==0) {
    rlist_remove(&ptcb->ptcb_list_node);
    free(ptcb);
  }
	return 0;
}

/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid)
{

  //get the PTCB of the given thread
  PTCB* ptcb = (PTCB*)tid;

  //if given PTCB does not exist in the current process the function fails
  if (rlist_find(& CURPROC->ptcb_list, ptcb, NULL)==NULL) {
    return -1;
  }

  //PTCB might have exited, function fails here too
  if(ptcb->exited == 1){
    return -1;
  }

  //now detached can be done
  ptcb->detached = 1;

  //broadcast the exit_cv of the PTCB
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

  curproc->thread_count--;  //thread count gets decreased since a thread exits
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
    //these PTCB variables need to be adapted at the exit
    ptcb->exited=1; 
    ptcb->exitval = exitval;

    kernel_broadcast(&ptcb->exit_cv);
  
    /* Bye-bye cruel world */
    kernel_sleep(EXITED, SCHED_USER);
  
}

