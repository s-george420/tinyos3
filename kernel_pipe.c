
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

pipe_cb * initialize_pipe() 
{
    pipe_cb* pipe = (pipe_cb*)xmalloc(sizeof(pipe_cb));
    pipe -> has_space = COND_INIT;
	pipe -> has_data = COND_INIT;
	pipe -> w_position = 0;
	pipe -> r_position = 0;
	pipe -> buffer_size = 0;
	return pipe;
}



int sys_Pipe(pipe_t* pipe)
{
	Fid_t fids[2];
	FCB* fcbs[2];

	if(FCB_reserve(2, fids, fcbs) == 0) {
    	return -1;
	}
	
	pipe_cb* new_pipe_cb = initialize_pipe();

	if(new_pipe_cb == NULL){
		return -1;
	}

	//first fcb is for read
	new_pipe_cb->reader = fcbs[0];
	pipe->read = fids[0];
	fcbs[0]->streamobj = new_pipe_cb;
	fcbs[0]->streamfunc = &reader_file_ops;

	//second fcb is for write
	new_pipe_cb->writer = fcbs[1];
	pipe->write = fids[1];
	fcbs[1]->streamobj = new_pipe_cb;
	fcbs[1]->streamfunc = &writer_file_ops;

	return 0;
}

int pipe_write(void* pipecb_t, const char *buf, unsigned int n)
{
	pipe_cb* pipe = (pipe_cb*) pipecb_t;

	if(pipe == NULL || pipe->reader == NULL || pipe->writer == NULL) {
		return -1;
	}
	int i=0;
	for(i=0; i<n; i++){
		while(pipe->reader != NULL && pipe->buffer_size == PIPE_BUFFER_SIZE) {
			kernel_broadcast(&pipe->has_data);
			kernel_wait(&pipe->has_space, SCHED_PIPE);
		}

		if(pipe->reader == NULL || pipe->writer == NULL){
			return i;
		}

		pipe -> BUFFER[pipe->w_position] = buf[i];  // write to buffer

		pipe -> w_position ++;

		pipe -> buffer_size ++;
		if(pipe->w_position == PIPE_BUFFER_SIZE){
			pipe->w_position = 0;
		}
	}
	kernel_broadcast(&pipe->has_data); // to read last byte

	return i;
}

int pipe_read(void* pipecb_t, char *buf, unsigned int n)
{
	pipe_cb* pipe = (pipe_cb*) pipecb_t;

	if(pipe == NULL || pipe->reader == NULL || pipe->writer == NULL) {
		return -1;
	}
	for(int i=0; i<n; i++){
		while(pipe->writer != NULL && pipe->buffer_size == 0) {
			kernel_broadcast(&pipe->has_space);
			kernel_wait(&pipe->has_data, SCHED_PIPE);
		}

		if(pipe->reader == NULL || pipe->writer == NULL){
			return i;
		}

		buf[i] = pipe -> BUFFER[pipe->r_position];  // write to buffer

		pipe -> r_position ++;

		pipe -> buffer_size --;
		if(pipe->r_position == PIPE_BUFFER_SIZE){
			pipe->r_position = 0;
		}
	}
	kernel_broadcast(&pipe->has_space); // to read last byte

	return n;
}

int pipe_writer_close(void* _pipecb)
{
	if(_pipecb == NULL){
		return -1;
	}

	pipe_cb* pipe = (pipe_cb*)_pipecb;

	if(pipe==NULL){
		return -1;
	}

	if(pipe->reader==NULL) {
    	free(pipe);
    }

	pipe->writer = NULL;
	kernel_broadcast(&pipe->has_data);
	

	return 0;
}

int pipe_reader_close(void* _pipecb)
{
	pipe_cb* pipe = (pipe_cb*)_pipecb;

	if(pipe==NULL){
		return -1;
	}

	if(pipe->writer==NULL){
        free(pipe);
    }

	pipe->reader = NULL;
	kernel_broadcast(&pipe->has_space);

	return 0;
}


