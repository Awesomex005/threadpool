#include <stdio.h>

#include "queue.h"
#include "thread_pool.h"

void job_sleep_n_print(void* arg){
	long i;
	i = (long)arg;
	printf("\t\tJOBBBBBB ***%s*** %#010x\n", __func__, i);
	sleep(i);
	printf("\t\tJOBBBBBB ***%s*** %#010x DONE.\n", __func__, i);
}

int main(int argc, char **argv){
	threadpool * tp;
	int err;
	char *queue_path = "tp_queue";
	void* job_desciptor[2];

	tp = creat_thread_pool(10, 20, queue_path);
	tp->init(tp);

	/* Add 32 jobs to threadpool.*/
	job_desciptor[0] = (void*)job_sleep_n_print;
	job_desciptor[1] = (void*)0x20;
	for(; job_desciptor[1]; ){
		printf("add job to queue.\n");
		OS_ADD_TO_Q(job_desciptor, sizeof(job_desciptor),tp->job_queuefd, &err);
		job_desciptor[1] = (void*)((long)job_desciptor[1] - 1);
	}
	sleep(60);

	/* Add another 16 jobs to threadpool.*/
	job_desciptor[0] = (void*)job_sleep_n_print;
	job_desciptor[1] = (void*)0x10;
	for(; job_desciptor[1]; ){
		printf("add job to queue.\n");
		OS_ADD_TO_Q(job_desciptor, sizeof(job_desciptor),tp->job_queuefd, &err);
		job_desciptor[1] = (void*)((long)job_desciptor[1] - 1);
	}
	while(1)
		sleep(1);
	return 0;
}

