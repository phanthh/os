#include "threadpool.h"
#include <stdio.h>
#include <stdlib.h>

void thread_pool_init(ThreadPool *pool, int num_threads, int job_size) {
  // Initialize variables
  pool->jobs = (Job *)malloc(job_size * sizeof(Job));
  pool->job_count = 0;
  pool->job_size = job_size;
  pool->front = 0;
  pool->threads = (Thread *)malloc(num_threads * sizeof(Thread));
  pool->num_threads = num_threads;
  pool->stop_requested = 0;

  // Initialize mutexes and semaphore
  pthread_mutex_init(&pool->lock, NULL);
  pthread_mutex_init(&pool->job_lock, NULL);
  sem_init(&pool->jobs_available, 0, 0);

  // Create threads
  for (int i = 0; i < num_threads; ++i) {
    pool->threads[i].id = i;
    WorkerInput *input = (WorkerInput *)malloc(sizeof(WorkerInput));
    input->pool = pool;
    input->thread = &pool->threads[i];
    pthread_create(&pool->threads[i].thread, NULL, worker_thread,
                   (void *)input);
  }
}

void thread_pool_submit(ThreadPool *pool, Job job) {
  pthread_mutex_lock(&pool->lock);

  // Check if the job queue is full
  if (pool->job_count == pool->job_size) {
    printf("Job queue is full!\n");
    // Free the args of the job if should_free is true
    if (job.should_free && !job.is_freed) {
      free(job.args);
      job.is_freed = 1;
    }
  } else {
    // Add the job to the correct position
    int index = (pool->front + pool->job_count) % pool->job_size;
    pool->jobs[index] = job;
    pool->job_count++;

    // Signal that a job is available
    sem_post(&pool->jobs_available);
  }

  pthread_mutex_unlock(&pool->lock);
}

void *worker_thread(void *args) {
  // Cast the void* argument to a WorkerInput* structure
  WorkerInput *input = (WorkerInput *)args;

  // Access necessary information from the WorkerInput structure
  ThreadPool *pool = input->pool;
  Thread *thread = input->thread;

  // Infinite loop to keep the thread running
  while (1) {
    // Wait for signal on the jobs_available semaphore
    sem_wait(&pool->jobs_available);

    // Check if the thread pool is requested to stop and exit the loop if
    // requested
    if (pool->stop_requested) {
      break;
    }

    // Lock the job queue mutex for access
    pthread_mutex_lock(&pool->lock);

    // Dequeue a job from the front of the queue
    Job job = pool->jobs[pool->front];
    pool->front = (pool->front + 1) % pool->job_size;
    pool->job_count--;

    // Unlock the job queue mutex
    pthread_mutex_unlock(&pool->lock);

    // Execute the job
    if (job.run_safely) {
      // Lock the job lock for running the job safely
      pthread_mutex_lock(&pool->job_lock);
      job.function(job.args);
      // Unlock the job lock
      pthread_mutex_unlock(&pool->job_lock);
    } else {
      job.function(job.args);
    }

    // Free the args of the job if should_free is true
    if (job.should_free && !job.is_freed) {
      free(job.args);
      job.is_freed = 1;
    }
  }

  // Print thread finished message
  printf("Thread with id %d is finished.\n", thread->id);

  // Free the WorkerInput
  free(input);

  return NULL;
}

void thread_pool_stop(ThreadPool *pool) {
  // Set the stop_requested flag
  pool->stop_requested = 1;

  // Signal all worker threads to exit using sem_post and jobs_available
  // semaphore
  for (int i = 0; i < pool->num_threads; ++i) {
    sem_post(&pool->jobs_available);
  }
}

void thread_pool_wait(ThreadPool *pool) {
  // Wait for all worker threads to finish their work using pthread_join
  for (int i = 0; i < pool->num_threads; ++i) {
    pthread_join(pool->threads[i].thread, NULL);
  }
}

void thread_pool_clean(ThreadPool *pool) {
  // Clean the *args of any job which should_free is 1 and is_freed is 0
  for (int i = 0; i < pool->job_size; ++i) {
    if (pool->jobs[i].should_free && !pool->jobs[i].is_freed) {
      free(pool->jobs[i].args);
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
