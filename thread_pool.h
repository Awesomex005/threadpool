#ifndef __THREAD_POOL_H__
#define __THREAD_POOL_H__

#include <semaphore.h>
#include <pthread.h>


#ifndef TPBOOL
typedef char TPBOOL;
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define BUSY_THRESHOLD 0.5
#define MANAGE_INTERVAL 5

typedef struct worker_controller_block_s WCB;
typedef struct threadpool_s threadpool;
typedef void (*JOB)(void *);

struct worker_controller_block_s{
	pthread_t thread_id;
	TPBOOL armed;	   //woker's thread is armed or not
	TPBOOL is_busy;
	pthread_cond_t worker_cond;
	pthread_mutex_t worker_lock;
	pthread_mutex_t worker_mgt_lock;
	void (*job)(void *args);
	void *job_args;
};

struct threadpool_s{
	TPBOOL (*init)(threadpool *this);
	void (*close)(threadpool *this);
	int	 (*get_workerid)(threadpool *this, pthread_t thread_id);
	TPBOOL (*del_worker)(threadpool *this);
	int (*check_status)(threadpool *this, long *work_num, long *busy_num);

	void* (*worker)(void*);
	void* (*scheduler)(void*);
	void* (*manager)(void*);

	int min_worker_num;
	int max_worker_num;
	pthread_mutex_t threadpool_lock;
	pthread_t scheduler_id;
	pthread_t manager_id;
	sem_t sem_available_worker;
	WCB *wcb_list;
	int job_queuefd;
};

threadpool *creat_thread_pool(int min_num, int max_num, char* queue_path);

#endif
