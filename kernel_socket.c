
#include "tinyos.h"
#include "kernel_socket.h"
#include "kernel_streams.h"

SCB* PORT_MAP[MAX_PORT];

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
	rlnode_init(&new_socket_cb->unbound_s.unbound_socket, new_socket_cb);
  	fcb->streamobj = new_socket_cb;
  	//fcb->streamfunc = &socket_file_ops;
	PORT_MAP[port] = new_socket_cb;
	return fid;
}



int sys_Listen(Fid_t sock)
{
	FCB* fcb = get_fcb(sock);
	if(fcb == NULL) {
		return -1;
	}

	SCB* socket = fcb->streamobj;
	if(socket == NULL || socket->type == SOCKET_UNBOUND || PORT_MAP[(int)(socket->port)] == NULL) {
		return -1;
	}
	
	return -1;
}


Fid_t sys_Accept(Fid_t lsock)
{
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
