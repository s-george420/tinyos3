
#include "tinyos.h"
#include "kernel_socket.h"
#include "kernel_streams.h"
#include "util.h"
#include "kernel_sched.h"
#include "kernel_cc.h"
#include "kernel_pipe.h"

SCB* PORT_MAP[MAX_PORT+1] = {NULL};

static file_ops socket_file_ops = {
	.Open = NULL,
	.Read = socket_read,
	.Write = socket_write,
	.Close = socket_close
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
	//rlnode_init(&new_socket_cb->unbound_s.unbound_socket, NULL);
  	fcb->streamobj = new_socket_cb;
  	fcb->streamfunc = &socket_file_ops;
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
	if(socket == NULL || socket->port == NOPORT || PORT_MAP[(int)(socket->port)] != NULL || socket->type != SOCKET_UNBOUND) {
		return -1;
	}
		
	PORT_MAP[(int)(socket->port)] = socket;
	socket->type = SOCKET_LISTENER;
	// initialize queue
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
	
	SCB* listening_socket = fcb->streamobj;

	if(listening_socket == NULL || listening_socket->type != SOCKET_LISTENER || listening_socket->port == NOPORT) {
		return -1;
	}

	FCB* p_fcb;
	Fid_t p_fid;

	//check exhausted
	if(FCB_reserve(1,&p_fid,&p_fcb) == 0){
		return NOFILE;
	}

	listening_socket->refcount ++;
	
	while (is_rlist_empty(&listening_socket->listener_s.queue))
	{
		kernel_wait(&listening_socket->listener_s.req_available, SCHED_IO);
	}


	//check if port is still valid because socket might have been closed
	if(listening_socket->type == SOCKET_UNBOUND || PORT_MAP[listening_socket->port] == NULL) {
		listening_socket->refcount--;
		return NOFILE;
	}

	FCB* server_fcb;
	Fid_t server_fid;

	//check exhausted
	if(FCB_reserve(1,&p_fid,&p_fcb) == 0) {
		return NOFILE;
	}

	rlnode* con_node = rlist_pop_front(&listening_socket->listener_s.queue);
  
    c_req* cr = con_node->cr;
    SCB* client_peer = cr->peer;
  

    server_fid = sys_Socket(listening_socket->port);
	
	server_fcb = get_fcb(server_fid);

  	if(server_fcb == NULL || server_fcb->streamfunc != &socket_file_ops) {
		return NOFILE;	//failure
	}

	SCB* server_peer = server_fcb->streamobj;

	//construct pipes
	pipe_cb* pipe1;
	pipe1 = xmalloc(sizeof(pipe_cb));
	pipe1->writer = server_peer->fcb;
	pipe1->reader = client_peer->fcb;
	pipe1->has_data = COND_INIT;
	pipe1->has_space = COND_INIT;
	pipe1->w_position = 0;
	pipe1->r_position = 0;
	pipe1->buffer_size = 0;

	pipe_cb* pipe2;
	pipe2 = xmalloc(sizeof(pipe_cb));
	pipe2->writer = server_peer->fcb;
	pipe2->reader = client_peer->fcb;
	pipe2->has_data = COND_INIT;
	pipe2->has_space = COND_INIT;
	pipe2->w_position = 0;
	pipe2->r_position = 0;
	pipe2->buffer_size = 0;

	client_peer->type = SOCKET_PEER;
	server_peer->type = SOCKET_PEER;

	cr->admitted = 1;
	kernel_signal(&(cr->connected_cv));

	listening_socket->refcount--;

	if (listening_socket->refcount == 0) {
		free(listening_socket);
	}

	return NOFILE;
}


int sys_Connect(Fid_t sock, port_t port, timeout_t timeout)
{
	/*- the file id @c sock is not legal (i.e., an unconnected, non-listening socket)
	   - the given port is illegal.
	   - the port does not have a listening socket bound to it by @c Listen.
	   - the timeout has expired without a successful connection.
	*/
	//the given port is illegal
	if(port <= NOPORT || port > MAX_PORT) {
		return -1;
	}

	//find fcb and then the socket from fcb;
	FCB* fcb = get_fcb(sock);
	if(fcb == NULL) {
		return -1;
	}

	SCB* client_socket = fcb->streamobj;

	if(client_socket == NULL || client_socket->type != SOCKET_UNBOUND) {
		return -1;
	}

	SCB* listening_socket = PORT_MAP[port];
	if(listening_socket == NULL || listening_socket->type != SOCKET_LISTENER) {
		return -1;
	}

	client_socket->refcount ++;
	c_req* cr = xmalloc(sizeof(c_req));
	cr->admitted = 0;
	cr->connected_cv = COND_INIT;
	rlnode_init(&cr->queue_node, cr);
	cr->peer = client_socket;

	rlist_push_back(&listening_socket->listener_s.queue, &cr->queue_node);
	kernel_signal(&listening_socket->listener_s.req_available);

	while(cr->admitted==0) {
		kernel_timedwait(&cr->connected_cv, SCHED_IO, timeout);
	}

	client_socket->refcount--;
	
	if(cr->admitted==0) {
		return -1;
	}
		free(cr);
	
	return 0;
}


int sys_ShutDown(Fid_t sock, shutdown_mode how)
{
	FCB* fcb = get_fcb(sock);
	if(fcb == NULL) {
		return -1;
	}

	SCB* socket = fcb->streamobj;
	
	switch(how)
   	{
    	case SHUTDOWN_READ:
			pipe_reader_close(socket->peer_s.read_pipe);
			socket->peer_s.read_pipe = NULL;
      		break;

    	case SHUTDOWN_WRITE:
            pipe_writer_close(socket->peer_s.write_pipe);
			socket->peer_s.write_pipe = NULL;
    		break;
		
		case SHUTDOWN_BOTH:	
			pipe_reader_close(socket->peer_s.write_pipe);
			socket->peer_s.read_pipe = NULL;
			
			pipe_writer_close(socket->peer_s.write_pipe);
			socket->peer_s.write_pipe = NULL;
			break;
    
   // default:
      // default statements
    }
	return 0;
}

	int socket_read(void* sock, char* buf, unsigned int size) 
	{

		SCB* socket = (SCB*) sock;
		if(socket == NULL || socket->type != SOCKET_PEER || socket->peer_s.read_pipe == NULL)
			return -1;
        
		pipe_cb* pipe = socket->peer_s.read_pipe;
		return pipe_read(pipe, buf, size); 
	}

	int socket_write(void* sock, char* buf, unsigned int size) 
	{
		SCB* socket = (SCB*) sock;
		if(socket == NULL || socket->type != SOCKET_PEER || socket->peer_s.write_pipe == NULL)
			return -1;
        
		pipe_cb* pipe = socket->peer_s.write_pipe;
		
		return pipe_write(pipe, buf, size);
	}


int socket_close(void* sock){
	
    if(sock == NULL)
        return -1;

    SCB* socket = (SCB*) sock;

    switch (socket->type){
        case SOCKET_PEER:
            pipe_writer_close(socket->peer_s.write_pipe);
            pipe_reader_close(socket->peer_s.read_pipe);
            break;
			
        case SOCKET_LISTENER:
            while(!is_rlist_empty(&(socket->listener_s.queue))){
                rlnode_ptr node = rlist_pop_front(&(socket->listener_s.queue));
                free(node);
            }
            kernel_broadcast(&(socket->listener_s.req_available));
            PORT_MAP[socket->port] = NULL;
            break;
        
		case SOCKET_UNBOUND:
            break;
    }

	socket->refcount--;
    if (socket->refcount == 0) {
		free(socket);
	}
   
    return 0;
}

