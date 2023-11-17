#include "threadpool.h"
#include <stdio.h>
#include <stdlib.h>

void thread_pool_init(ThreadPool *pool, int num_threads, int job_size) {
  // Initialize variables
  pool->jobs = (Job *)malloc(sizeof(Job) * job_size);
  pool->job_count = 0;
  pool->job_size = job_size;
  pool->front = 0;
  pool->threads = (Thread *)malloc(sizeof(Thread) * num_threads);
  pool->num_threads = num_threads;
  pool->stop_requested = 0;

  // Initialize mutexes and semaphore
  pthread_mutex_init(&pool->lock, NULL);
  pthread_mutex_init(&pool->job_lock, NULL);
  sem_init(&pool->jobs_available, 0, 0);

  // Create threads
  for (int i = 0; i < num_threads; i++) {
    pool->threads[i].id = i;
    pthread_create(&pool->threads[i].thread, NULL, worker_thread,
                   (void *)&(WorkerInput){pool, &(pool->threads[i])});
  }
}

void thread_pool_submit(ThreadPool *pool, Job job) {
  pthread_mutex_lock(&pool->lock);

  if (pool->job_count >= pool->job_size) {
    printf("Job queue is full!\n");
    if (job.should_free && !job.is_freed) {
      free(job.args);
      job.is_freed = 1;
    }
  } else {
    int index = (pool->front + pool->job_count) % pool->job_size;
    pool->jobs[index] = job;
    pool->job_count++;

    sem_post(&pool->jobs_available);
  }

  pthread_mutex_unlock(&pool->lock);
}

void *worker_thread(void *args) {
  WorkerInput *input = (WorkerInput *)args;
  ThreadPool *pool = input->pool;
  Thread *thread = input->thread;

  while (1) {
    sem_wait(&pool->jobs_available);

    if (pool->stop_requested) {
      break;
    }

    pthread_mutex_lock(&pool->lock);

    if (pool->job_count == 0) {
      pthread_mutex_unlock(&pool->lock);
      continue; // No job available, go back to waiting
    }

    Job job = pool->jobs[pool->front];
    pool->front = (pool->front + 1) % pool->job_size;
    pool->job_count--;

    pthread_mutex_unlock(&pool->lock);

    if (job.run_safely) {
      pthread_mutex_lock(&pool->job_lock);
      job.function(job.args);
      pthread_mutex_unlock(&pool->job_lock);
    } else {
      job.function(job.args);
    }

    if (job.should_free && !job.is_freed) {
      free(job.args);
      job.is_freed = 1;
    }
  }

  printf("Thread with id %d is finished.\n", thread->id);

  free(input); // Move the free statement here
  pthread_exit(NULL);
}

void thread_pool_stop(ThreadPool *pool) {
  pool->stop_requested = 1;
  for (int i = 0; i < pool->num_threads; ++i) {
    sem_post(&pool->jobs_available);
  }
}

void thread_pool_wait(ThreadPool *pool) {
  for (int i = 0; i < pool->num_threads; ++i) {
    pthread_join(pool->threads[i].thread, NULL);
  }
}

void thread_pool_clean(ThreadPool *pool) {
  // Set the stop_requested flag
  pool->stop_requested = 1;

  // Signal all worker threads to exit using sem_post and jobs_available
  // semaphore
  for (int i = 0; i < pool->num_threads; i++) {
    sem_post(&pool->jobs_available);
  }

  // Wait for all worker threads to finish their work
  for (int i = 0; i < pool->num_threads; i++) {
    pthread_join(pool->threads[i].thread, NULL);
  }

  // Clean the *args of any job which should_free is 1 and is_freed is 0.
  for (int i = 0; i < pool->job_size; i++) {
    if (pool->jobs[i].should_free && !pool->jobs[i].is_freed) {
      free(pool->jobs[i].args);
      pool->jobs[i].is_freed = 1;
    }
  }

  // Release the memory allocated for the thread pool and its components
  free(pool->jobs);
  free(pool->threads);

  // Destroy mutexes and semaphore
  pthread_mutex_destroy(&pool->lock);
  pthread_mutex_destroy(&pool->job_lock);
  sem_destroy(&pool->jobs_available);
}
