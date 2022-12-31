
#include "tinyos.h"
#include "kernel_socket.h"
#include "kernel_streams.h"
#include "util.h"
#include "kernel_sched.h"
#include "kernel_cc.h"

SCB* PORT_MAP[MAX_PORT+1] = {NULL};

static file_ops socket_file_ops = {
	.Open = NULL,
	.Read = NULL,
	.Write = NULL,
	.Close = NULL
};

Fid_t sys_Socket(port_t port)
{
	if(port < NOPORT || port > MAX_PORT){
		return NOFILE;
	}

	FCB* fcb;
	Fid_t fid;

	if(FCB_reserve(1,&fid,&fcb) == 0){
		return NOFILE;
	}

	SCB* new_socket_cb = xmalloc(sizeof(SCB));
	new_socket_cb->refcount = 0;
	new_socket_cb->fcb = fcb;
	new_socket_cb->type = SOCKET_UNBOUND;
	new_socket_cb->port = port;
	rlnode_init(&new_socket_cb->unbound_s.unbound_socket, NULL);
  	fcb->streamobj = new_socket_cb;
  	//fcb->streamfunc = &socket_file_ops;
	return fid;
}



int sys_Listen(Fid_t sock)
{
	FCB* fcb = get_fcb(sock);
	if(fcb == NULL) {
		return -1;
	}
	/*
		- the file id is not legal
		- the socket is not bound to a port
		- the port bound to the socket is occupied by another listener
		- the socket has already been initialized
	*/
	SCB* socket = fcb->streamobj;
	if(socket == NULL || socket->port != NOPORT || PORT_MAP[(int)(socket->port)] != NULL || socket->type != SOCKET_UNBOUND) {
		return -1;
	}

	PORT_MAP[(int)(socket->port)] = socket;
	socket->type = SOCKET_LISTENER;
	rlnode_init(&socket->listener_s.queue, NULL); 
	socket->listener_s.req_available = COND_INIT;
	
	return 0;
}


Fid_t sys_Accept(Fid_t lsock)
{
/*     - the file id is not legal
	   - the file id is not initialized by @c Listen()
	   - the available file ids for the process are exhausted
	   - while waiting, the listening socket @c lsock was closed
*/
	FCB* fcb = get_fcb(lsock);
	if(fcb == NULL) {
		return -1;
	}
	
	SCB* socket = fcb->streamobj;

	if(socket->type != SOCKET_LISTENER) {
		return -1;
	}

	FCB* p_fcb;
	Fid_t p_fid;

	//check exhausted
	if(FCB_reserve(1,&p_fid,&p_fcb) == 0){
		return NOFILE;
	}

	socket->refcount ++;
	
	while (is_rlist_empty(&socket->listener_s.queue))
	{
		kernel_wait(&socket->listener_s.req_available, SCHED_PIPE);
	}
	
	if(socket->type == SOCKET_UNBOUND) {
		return NOFILE;
	}

	rlnode* con = rlist_pop_front(&socket->listener_s.queue);
   	SCB * scb_client = con->scb;

	socket->conReq->admitted = 1;



	
	
	

	return NOFILE;
}


int sys_Connect(Fid_t sock, port_t port, timeout_t timeout)
{
	return -1;
}


int sys_ShutDown(Fid_t sock, shutdown_mode how)
{
	FCB* fcb = get_fcb(sock);
	SCB* socket = fcb->streamobj;
	PORT_MAP[(int)(socket->port)] = NULL;
		return -1;
}
