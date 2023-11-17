// Author: Son Nguyen

#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <stdio.h>
#include "threadpool.h"

void thread_pool_init(ThreadPool *pool, int num_threads, int job_size)
{
        // init variables and jobs and threads using malloc
        pool->jobs = (Job *) malloc(sizeof(Job) * job_size);
        pool->job_count = 0;
        pool->job_size = job_size;
        pool->front = 0;
        pool->threads = (Thread *) malloc(sizeof(Thread) * num_threads);
        pool->num_threads = num_threads;
        pool->stop_requested = 0;

        // init mutexes and semaphore using functions
        pthread_mutex_init(&pool->lock, NULL);
        pthread_mutex_init(&pool->job_lock, NULL);
        sem_init(&pool->jobs_available, 0, 0);

        // create threads
        for (int i = 0; i < num_threads; ++i) {
                pool->threads[i].id = i;
                WorkerInput *worker_input = (WorkerInput * ) malloc(sizeof(WorkerInput));
                worker_input->pool = pool;
                worker_input->thread = &pool->threads[i];

                if (pthread_create(&pool->threads[i].thread, NULL, worker_thread, (void *) worker_input) != 0) {
                        exit(EXIT_FAILURE);
                }
        }
}

void thread_pool_submit(ThreadPool *pool, Job job)
{
        // lock job queue mutex
        pthread_mutex_lock(&pool->lock);

        // check if job queue is full
        if (pool->job_count >= pool->job_size) {
                printf("job queue is full!\n");
                if (job.should_free && !job.is_freed) {
                        free(job.args);
                        job.is_freed = 1;
                }
        } else {
        // add the job to correct position
                int index = (pool->front + pool->job_count) % pool->job_size;
                pool->jobs[index]= job;
                pool->job_count++;

        // signal job availability
                sem_post(&pool->jobs_available);
        }

        // unlock mutex
        pthread_mutex_unlock(&pool->lock);
}

void thread_pool_wait(ThreadPool *pool)
{
        for (int i = 0; i < pool->num_threads; ++i) {
                if (pthread_join(pool->threads[i].thread, NULL) != 0) {
                        exit(EXIT_FAILURE);
                }
        }
}

void thread_pool_stop(ThreadPool *pool)
{
        pool->stop_requested = 1;
        for (int i = 0; i < pool->num_threads; ++i) {
                sem_post(&pool->jobs_available);
        }
}

void thread_pool_clean(ThreadPool *pool)
{
        // clean *args of jobs
        pthread_mutex_lock(&pool->lock);
        for (int i = 0; i < pool->job_count; ++i) {
                if (pool->jobs[i].should_free && !pool->jobs[i].is_freed) {
                        free(pool->jobs[i].args);
                        pool->jobs[i].is_freed = 1;
                }
        }
        pthread_mutex_unlock(&pool->lock);

        // free memory of pool and components
        pthread_mutex_destroy(&pool->lock);
        pthread_mutex_destroy(&pool->job_lock);
        sem_destroy(&pool->jobs_available);

        // free threads and jobs
        free(pool->threads);
        free(pool->jobs);
}

void* worker_thread(void* args)
{
        // casting
        WorkerInput *input = (WorkerInput *) args;
        ThreadPool *pool = input->pool;
        Thread *thread = input->thread;

        while (1) {
                // wait for signal
                sem_wait(&pool->jobs_available);

                if (pool->stop_requested) {
                        break;
                }
                
                // lock job queue mutex for access
                pthread_mutex_lock(&pool->lock);

                // dequeue job
                Job job = pool->jobs[pool->front];
                pool->front = (pool->front + 1) % pool->job_size;
                pool->job_count--;

                // unlock job queue mutex
                pthread_mutex_unlock(&pool->lock);

                // execute the job
                if (job.run_safely) {
                        pthread_mutex_lock(&pool->job_lock);
                        job.function(job.args);
                        pthread_mutex_unlock(&pool->job_lock);
                } else {
                        job.function(job.args);
                }

                // free args of job if needed
                if (job.should_free && !job.is_freed) {
                        free(job.args);
                        job.is_freed = 1;
                }
        }

        free(input);
        printf("thread with id %d is finished.\n", thread->id);
        pthread_exit(NULL);

        return NULL;
}
