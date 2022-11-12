
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"


/** 
  @brief Create a new thread in the current process.
  */
Tid_t sys_CreateThread(Task task, int argl, void* args)
{
  PCB* pcb = CURPROC;
  
  
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
  tcb->owner_pcb = pcb;   
  rlist_push_back(&pcb->ptcb_list, &ptcb->ptcb_list_node);
  /*if(task!=NULL){
    tcb = spawn_thread(pcb, start_thread);
  }*/
  
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
  if(tid == NOTHREAD){
    return -1;
  }

  if (tid == ThreadSelf()) {
    return -1;
  }



	return 0;
}

/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid)
{
	return -1;
}

/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{

}

