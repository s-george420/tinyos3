#ifndef __KERNEL_SOCKET_H
#define __KERNEL_SOCKET_H

#include "util.h"
#include "kernel_pipe.h"
#include "kernel_dev.h"

typedef enum{
  SOCKET_LISTENER,
  SOCKET_UNBOUND,
  SOCKET_PEER
}socket_type;

Fid_t sys_Socket(port_t port);
int sys_Listen(Fid_t sock);
Fid_t sys_Accept(Fid_t lsock);
int sys_Connect(Fid_t sock, port_t port, timeout_t timeout);
int sys_ShutDown(Fid_t sock, shutdown_mode how);

typedef struct socket_control_block SCB;

typedef struct listener_s{
  rlnode queue;
  CondVar req_available;
}listener_socket;

typedef struct unbound_s{
  rlnode unbound_socket;
}unbound_socket;

typedef struct peer_s{
  SCB* peer;
  pipe_cb* write_pipe;
  pipe_cb* read_pipe;
}peer_socket;

typedef struct connection_request{
  int admitted;  
  SCB* peer;  
  CondVar connected_cv;
  rlnode queue_node; 
} c_req;

typedef struct socket_control_block {
  
  unsigned int refcount;  /*so that we know when to free a SCB*/

  FCB* fcb;

  socket_type type; /*listener,unbound or peer*/

  port_t port;  

  c_req* conReq;

  union { /*socket types*/
    listener_socket listener_s; 
    unbound_socket unbound_s;
    peer_socket peer_s; /*when connection has been achieved - contibutes to a connection*/
  };
  
} SCB;


#endif