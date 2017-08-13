#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <pthread.h>
#include <signal.h>
#include <semaphore.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "queue.h"
#include "thread_pool.h"

#ifdef DEBUG
#define DEBUG_TEST 1
#else
#define DEBUG_TEST 0
#endif

#define debug_print(fmt, ...) \
        do { if (DEBUG_TEST) fprintf(stderr, "%s:%d:%s(): " fmt, __FILE__, \
                                __LINE__, __func__, ##__VA_ARGS__); } while (0)

void *threadpool_worker(void *pthread);
void *threadpool_scheduler(void *pthread);
void *threadpool_manager(void *pthread);

TPBOOL threadpool_init(threadpool *this);
void threadpool_close(threadpool *this);
int	 threadpool_get_workerid(threadpool *this, pthread_t thread_id);
TPBOOL threadpool_del_worker(threadpool *this);
int	 threadpool_check_status(threadpool *this, long *worker_num, long *busy_num);

/**
  * To creat a thread pool.
  */
threadpool* creat_thread_pool(int min_num, int max_num, char* job_queue_path){
	threadpool *this;
	this = (threadpool*)malloc(sizeof(threadpool));

	memset(this, 0, sizeof(threadpool));

	this->init = threadpool_init;
	this->close = threadpool_close;
	this->get_workerid = threadpool_get_workerid;
	this->del_worker = threadpool_del_worker;
	this->check_status = threadpool_check_status;

	this->worker = threadpool_worker;
	this->scheduler = threadpool_scheduler;
	this->manager = threadpool_manager;

	this->min_worker_num = min_num;
	this->max_worker_num = max_num;
	pthread_mutex_init(&this->threadpool_lock, NULL);
	sem_init(&this->sem_available_worker, 0, max_num);

	this->wcb_list = (WCB*)malloc(sizeof(WCB)*this->max_worker_num);
	OS_CREATE_Q(job_queue_path);
	OS_OPEN_Q(job_queue_path, &this->job_queuefd);

	return this;
}

TPBOOL threadpool_init(threadpool *this){
	int err;
	int worker_id=0;

	for(; worker_id<this->max_worker_num; worker_id++){
		pthread_cond_init(&this->wcb_list[worker_id].worker_cond, NULL);
		pthread_mutex_init(&this->wcb_list[worker_id].worker_lock, NULL);
		pthread_mutex_init(&this->wcb_list[worker_id].worker_mgt_lock, NULL);
		this->wcb_list[worker_id].armed = FALSE;
		this->wcb_list[worker_id].is_busy = FALSE;
		this->wcb_list[worker_id].job = NULL;
		this->wcb_list[worker_id].job_args = NULL;
	}

	for(worker_id=0;worker_id<this->min_worker_num;worker_id++){
		err = pthread_create(&this->wcb_list[worker_id].thread_id, NULL, this->worker, (void *)this);
		if(0 != err){
			printf("threadpool_init: creat worker thread failed\n");
			return FALSE;
		}
		this->wcb_list[worker_id].armed = TRUE;
	}

	err = pthread_create(&this->scheduler_id, NULL, this->scheduler, (void *)this);
	if(0 != err){
		printf("threadpool_init: creat scheduler thread failed\n");
		return FALSE;
	}
	debug_print("threadpool_init: creat scheduler thread %lu\n", this->scheduler_id);


	err = pthread_create(&this->manager_id, NULL, this->manager, (void *)this);
	if(0 != err){
		printf("threadpool_init: creat manager thread failed\n");
		return FALSE;
	}
	debug_print("threadpool_init: creat manager thread %lu\n", this->manager_id);

	return TRUE;
}

void threadpool_close(threadpool *this){
	// consider never close it, or implement it in the future.
	printf("Warnning: no close method has been provided yet.\n");
}

/**
 * Get worker control brock by worker armed thread id.
 */
int threadpool_get_workerid(threadpool *this, pthread_t threadid){
	int worker_id;

	for(worker_id = 0; worker_id<this->max_worker_num; worker_id++){
		if(pthread_equal(threadid, this->wcb_list[worker_id].thread_id))
			return worker_id;
	}
	return -1;
}

/**
 * Delete idle worker from the pool. Delete idle worker in descending order.
 */
TPBOOL threadpool_del_worker(threadpool *this){
	int worker_id = this->max_worker_num - 1;
	int err;
	void *res;

	for(; worker_id != this->min_worker_num-1; worker_id--){
		if(this->wcb_list[worker_id].armed){
			pthread_mutex_lock(&this->wcb_list[worker_id].worker_mgt_lock);
			pthread_mutex_lock(&this->wcb_list[worker_id].worker_lock);
			if(this->wcb_list[worker_id].is_busy){
				pthread_mutex_unlock(&this->wcb_list[worker_id].worker_lock);
				pthread_mutex_unlock(&this->wcb_list[worker_id].worker_mgt_lock);
				return FALSE;
			}
			else{
				/** Cancelling the running worker may case the pthread_mutex it runs with works unpredictable.
				 *	So, need to destroy it and resume it. */
				debug_print("kill~~ worker %d's thread %lu\n", worker_id, this->wcb_list[worker_id].thread_id);
				err = pthread_cancel(this->wcb_list[worker_id].thread_id);
				if(err){
					printf("kill err: %d\n", err);
					perror("kill worker: ");
				}
				this->wcb_list[worker_id].armed = FALSE;
				pthread_mutex_unlock(&this->wcb_list[worker_id].worker_lock);
				err = pthread_join(this->wcb_list[worker_id].thread_id, &res);
				if(err){
					perror("pthread_cancel worker: ");
				}
				if (res == PTHREAD_CANCELED)
               		debug_print("thread was canceled\n");
				printf("Unarm worker %d's thread %lu Successful.\n", worker_id, this->wcb_list[worker_id].thread_id);
				pthread_mutex_destroy(&this->wcb_list[worker_id].worker_lock);
				pthread_mutex_init(&this->wcb_list[worker_id].worker_lock, NULL);
				pthread_mutex_unlock(&this->wcb_list[worker_id].worker_mgt_lock);
				return TRUE;
			}
		}
	}
	return FALSE;
}

/**
 * Check current thread pool status:idle, normal/busy.
 * return:
 *	   0: idle; 1: normal or busy
 */
int	 threadpool_check_status(threadpool *this, long *worker_num, long *busy_num){
	float busy_number = 0.0;
	float worker_number = 0.0;
	int worker_id = 0;

	for(; worker_id<this->max_worker_num; worker_id++){
		if(this->wcb_list[worker_id].armed)
			worker_number++;
		else
			continue;
		if(this->wcb_list[worker_id].is_busy)
			busy_number++;
	}
	*worker_num = worker_number;
	*busy_num = busy_number;
	debug_print("threadpool_manager: busy_num: %f worker_num: %f busy_num/worker_num: %f\n", busy_number, worker_number, busy_number/worker_number);
	if(busy_number/worker_number < BUSY_THRESHOLD)
		return 0; //idle
	else
		return 1; //busy
}

/**
 * the worker
 */
void *threadpool_worker(void *pthread){
	pthread_t threadid;
	int worker_id;
	threadpool *this = (threadpool*)pthread;

	threadid = pthread_self();
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
  	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	worker_id = this->get_workerid(this, threadid);
	if(worker_id < 0){
		printf("critical error, can not find worker control block for thread %lu\n", threadid);
		return NULL;
	}
	debug_print("worker %d, thread id is %lu\n", worker_id, threadid);

	while( TRUE ){
		pthread_mutex_lock(&this->wcb_list[worker_id].worker_lock);
		if(!this->wcb_list[worker_id].is_busy)
			pthread_cond_wait(&this->wcb_list[worker_id].worker_cond, &this->wcb_list[worker_id].worker_lock);
		pthread_mutex_unlock(&this->wcb_list[worker_id].worker_lock);

		printf("------->>> worker %d (id: %lu) works on job! <<<-------\n", worker_id, pthread_self());

		//process the job
		void *job_args = this->wcb_list[worker_id].job_args;
		this->wcb_list[worker_id].job(job_args);

		//resume worker state to available
		pthread_mutex_lock(&this->wcb_list[worker_id].worker_lock);
		this->wcb_list[worker_id].job = NULL;
		this->wcb_list[worker_id].job_args = NULL;
		this->wcb_list[worker_id].is_busy = FALSE;
		sem_post(&this->sem_available_worker);
		pthread_mutex_unlock(&this->wcb_list[worker_id].worker_lock);
		printf("\t\t==>>> worker %d finish job <<<==\n", worker_id);
	}
}

/**
 * Read jobs from job queue and schedule jobs.
 */
void* threadpool_scheduler(void *pthread){
	int worker_id;
	int err = 0, size;
	threadpool *this = (threadpool*)pthread;
	void* job_desciptor[2];
	JOB job;
	void* job_args;
	INT8U committed;

	while(1){
		debug_print("threadpool_scheduler on duty.\n");
		OS_GET_FROM_Q(job_desciptor, sizeof(job_desciptor), this->job_queuefd, &err);
		job = (JOB)job_desciptor[0];
		job_args = job_desciptor[1];
		committed = FALSE;
		debug_print("get a job to schedule. job addr: %#010x job_args addr: %#010x\n", job, job_args);
		sem_wait(&this->sem_available_worker);
		for(worker_id = 0; worker_id<this->max_worker_num; worker_id++){
			pthread_mutex_lock(&this->wcb_list[worker_id].worker_mgt_lock);
			pthread_mutex_lock(&this->wcb_list[worker_id].worker_lock);
			if(this->wcb_list[worker_id].armed && !this->wcb_list[worker_id].is_busy){
				// worker is available
				this->wcb_list[worker_id].is_busy = TRUE;
				this->wcb_list[worker_id].job = job;
				this->wcb_list[worker_id].job_args = job_args;
				pthread_mutex_unlock(&this->wcb_list[worker_id].worker_lock);
				debug_print("threadpool_scheduler: woker %d accpet a job.\n", worker_id);
				pthread_cond_signal(&this->wcb_list[worker_id].worker_cond);
				pthread_mutex_unlock(&this->wcb_list[worker_id].worker_mgt_lock);
				committed = TRUE;
				break;
			}
			else if(!this->wcb_list[worker_id].armed){
				// arm a new worker to accept this job.
				err = pthread_create(&this->wcb_list[worker_id].thread_id, NULL, threadpool_worker, this);
				while(err){
					err = pthread_create(&this->wcb_list[worker_id].thread_id, NULL, threadpool_worker, this);
					printf("threadpool_scheduler failed to arm worker %d, threadpool is panic.\n", worker_id);
				}
				printf("=======>> %s: arm worker %d with thread %lu\n", __func__, worker_id, this->wcb_list[worker_id].thread_id);
				this->wcb_list[worker_id].armed = TRUE;
				this->wcb_list[worker_id].is_busy = TRUE;
				this->wcb_list[worker_id].job = job =job;
				this->wcb_list[worker_id].job_args = job_args;
				pthread_mutex_unlock(&this->wcb_list[worker_id].worker_lock);
				debug_print("threadpool_scheduler: woker %d accpet a job.\n", worker_id);
				pthread_cond_signal(&this->wcb_list[worker_id].worker_cond);
				pthread_mutex_unlock(&this->wcb_list[worker_id].worker_mgt_lock);
				committed = TRUE;
				break;
			}
			else{
				// worker is busy.
				pthread_mutex_unlock(&this->wcb_list[worker_id].worker_lock);
				pthread_mutex_unlock(&this->wcb_list[worker_id].worker_mgt_lock);
				continue;
			}
		}
		if(!committed){
			printf("shall never run into here, critical error.!!!!\n");
		}
	}
}


/**
 * manage thread pool and delete idle thread.
 */
void *threadpool_manager(void *pthread){
	threadpool *this = (threadpool*)pthread;
	long worker_num;
	long busy_num;

	debug_print("threadpool_manager on duty.\n");
	sleep(MANAGE_INTERVAL);
	while(1){
		if(!this->check_status(this, &worker_num, &busy_num) && worker_num > this->min_worker_num){
			debug_print("going to delete worker.\n");
			this->del_worker(this);
		}
		if(0 == busy_num) printf("idel.\n");
		sleep(MANAGE_INTERVAL);
	}
}

