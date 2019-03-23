# c_threadpool
A simple threadpool implemented in C.
- It has a jobs queue to hold the jobs which are to be handled. 
- A scheduler to schedule and assign the jobs to the workers in the pool, and create new workers if it is necessary. 
- A manager to control the number of the workers in the pool.

## Example
tp_test.c is the example code showing how to use this threadpool.
- How to add jobs to the pool.
- How to pass parameters to the job.

## Further
About how to collect the outputs of jobs.
- Consider using queues.

## Notices
- Jobs are responsible for releasing the resources which are passed to jobs as parameters.
