
#include "tinyos.h"
#include "kernel_pipe.h"
#include "kernel_sched.h"
#include "kernel_cc.h"

/*reader_file_ops performs only pipe_read and pipe_reader_close*/
static file_ops reader_file_ops = {
	.Open = NULL,
	.Read = pipe_read,
	.Write = NULL,
	.Close = pipe_reader_close
};

/*writer_file_ops performs only pipe_write and pipe_writer_close*/
static file_ops writer_file_ops = {
	.Open = NULL,
	.Read = NULL,
	.Write = pipe_write,
	.Close = pipe_writer_close
};


int sys_Pipe(pipe_t* pipe)
{
	/*create 2 fcbs and fids one for the reader and one for the writer*/
	FCB *fcbs[2];
	Fid_t fids[2];

	/*reserve space for 2 FCB's*/
	int retval = FCB_reserve(2,fids,fcbs);

    if(retval == 0) {	/*if allocation didn't succeed*/
		return -1;	//failure
	}

	/*initialization of the new pipe*/
	pipe_cb* new_pipe_cb = (pipe_cb*)xmalloc(sizeof(pipe_cb));
	new_pipe_cb -> buffer_size = 0;

	if(new_pipe_cb == NULL) {
		return -1;	//failure
	}

	/*the first fcb and fid is for the reading operation*/
	//new_pipe_cb->pipe = pipe;
	new_pipe_cb->reader = fcbs[0];
	pipe->read = fids[0];
	new_pipe_cb->has_data = COND_INIT;
	new_pipe_cb->r_position = 0;
	fcbs[0]->streamobj = new_pipe_cb;
	fcbs[0]->streamfunc = &reader_file_ops;

	/*the second fcb and fid is for the writing operation*/
	new_pipe_cb->writer = fcbs[1];
	pipe->write = fids[1];
	new_pipe_cb->has_space = COND_INIT;
	new_pipe_cb->w_position = 0;
	fcbs[1]->streamobj = new_pipe_cb;
	fcbs[1]->streamfunc = &writer_file_ops;

	return 0;
}
int pipe_write(void* pipecb_t, const char *buf, unsigned int n)
{
	//cast to get the pipe control block
	pipe_cb* pipe = (pipe_cb*) pipecb_t;
	
	//writer and reader must not be NULL
	if(pipe->reader == NULL || pipe->writer == NULL) {
		return -1;
	}
	int i=0;
	for(i=0; i<n; i++) {
		//if buffer is full we wait
		while(pipe->reader!=NULL && pipe->buffer_size==PIPE_BUFFER_SIZE) {
			//kernel broadcast since there is data to be read and writer can't write
			kernel_broadcast(&pipe->has_data);
			kernel_wait(&pipe->has_space, SCHED_PIPE);
		}
		//write byte at position i
		pipe->BUFFER[pipe->w_position] = buf[i];
		//increase w_position by 1
		pipe->w_position = (pipe->w_position+1)%PIPE_BUFFER_SIZE;
		//buffer size increases
		pipe->buffer_size++;
	}

	kernel_broadcast(&pipe->has_data);
	return i;
}

int pipe_read(void* pipecb_t, char *buf, unsigned int n)
{
	//cast to get the pipe control block
	pipe_cb* pipe = (pipe_cb*) pipecb_t;

	if(pipe->reader == NULL) {
		return -1;
	}
	//if read position is the same as the write position then there is nothing to read
	if(pipe->writer == NULL && pipe->r_position==pipe->w_position) {
		return 0;
	}
	
	int i=0;
	for(i=0; i<n; i++) {
		while(pipe->writer!=NULL && pipe->buffer_size==0) {
			kernel_broadcast(&pipe->has_space);
			kernel_wait(&pipe->has_data, SCHED_PIPE);
		}
		//writer closed and there is nothing to be read
		if(pipe->buffer_size == 0 && pipe->writer == NULL) {
			return i; //success
		}
		// read from read position of buffer the character and write it on buf at position i
		buf[i] = pipe->BUFFER[pipe->r_position];
		//increase read position
		pipe->r_position = (pipe->r_position+1)%PIPE_BUFFER_SIZE;
		//decrease pipe buffer size since one character is read
		pipe->buffer_size--;
	}
	kernel_broadcast(&pipe->has_space);
	return i;
}

int pipe_writer_close(void* _pipecb)
{
	//cast to get the pipe
	pipe_cb* pipe = (pipe_cb*)_pipecb;
	//check it is not closed already
	if(pipe==NULL || pipe->writer==NULL){
		return -1;
	}
	//close the writer
	pipe->writer = NULL;

	return 0;
}

int pipe_reader_close(void* _pipecb)
{	//cast to get the pipe
	pipe_cb* pipe = (pipe_cb*)_pipecb;
	//check that it is not closed already
	if(pipe==NULL || pipe->reader==NULL) {
		return -1;
	}

	//close the reader
	pipe->reader = NULL;

	return 0;
}


