// Copyright (c) 2018 The GAM Authors


#include <cstring>
#include "worker_handle.h"

#include "../include/lockwrapper.h"
#include "zmalloc.h"
#include "util.h"

LockWrapper WorkerHandle::lock;

WorkerHandle::WorkerHandle(Worker *w)
  : worker(w),
  wqueue(w->GetWorkQ()) {
  //#if !defined(USE_PIPE_W_TO_H) || (!defined(USE_BOOST_QUEUE) && !defined(USE_PIPE_H_TO_W))
#if !(defined(USE_PIPE_W_TO_H) && defined(USE_PIPE_H_TO_W))
  int notify_buf_size = sizeof(WorkRequest) + sizeof(int);
  int ret = posix_memalign((void **)&notify_buf, HARDWARE_CACHE_LINE,
    notify_buf_size);
  epicAssert((uint64_t)notify_buf % HARDWARE_CACHE_LINE == 0 && !ret);
  *notify_buf = 2;
#endif
#ifdef USE_PTHREAD_COND
  pthread_mutex_init(&cond_lock, NULL);
  pthread_cond_init(&cond, NULL);
  pthread_mutex_lock(&cond_lock);
#endif
  RegisterThread();
}

WorkerHandle::~WorkerHandle() {
  DeRegisterThread();
#if !defined(USE_PIPE_W_TO_H) || (!defined(USE_BOOST_QUEUE) && !defined(USE_PIPE_H_TO_W))
  free((void *)notify_buf);
#endif
#ifdef USE_PTHREAD_COND
  pthread_mutex_unlock(&cond_lock);
#endif
}

void WorkerHandle::RegisterThread() {
  if (pipe(send_pipe)) {
    epicLog(LOG_WARNING, "create send pipe failed");
  }
  if (pipe(recv_pipe)) {
    epicLog(LOG_WARNING, "create recv pipe failed");
  }
  worker->RegisterFence(recv_pipe[1]);
#if defined(USE_PIPE_W_TO_H) || defined(USE_PIPE_H_TO_W)
  worker->RegisterHandle(send_pipe[0]);
#endif
#ifndef USE_BOOST_QUEUE
  worker->RegisterNotifyBuf(notify_buf);
#endif
}

void WorkerHandle::DeRegisterThread() {
#if defined(USE_PIPE_W_TO_H) || defined(USE_PIPE_H_TO_W)
  worker->DeRegisterHandle(send_pipe[0]);
#endif
#ifndef USE_BOOST_QUEUE
  worker->DeRegisterNotifyBuf(notify_buf);
#endif
  if (close(send_pipe[0])) {
    epicLog(LOG_WARNING, "close send_pipe[0] (%d) failed: %s (%d)",
      send_pipe[0], strerror(errno), errno);
  }
  if (close(send_pipe[1])) {
    epicLog(LOG_WARNING, "close send_pipe[1] (%d) failed: %s (%d)",
      send_pipe[1], strerror(errno), errno);
  }
  if (close(recv_pipe[0])) {
    epicLog(LOG_WARNING, "close recv_pipe[0] (%d) failed: %s (%d)",
      recv_pipe[0], strerror(errno), errno);
  }
  if (close(recv_pipe[1])) {
    epicLog(LOG_WARNING, "close recv_pipe[1] (%d) failed: %s (%d)",
      recv_pipe[1], strerror(errno), errno);
  }
}

int WorkerHandle::SendRequest(WorkRequest *wr) {
  wr->flag |= LOCAL_REQUEST;
#ifdef MULTITHREAD
  *notify_buf = 1;  //not useful to set it to 1 if boost_queue is enabled
  epicAssert(*(int *)notify_buf == 1);
  wr->fd = recv_pipe[1];  //for legacy code
  wr->notify_buf = this->notify_buf;
  epicLog(LOG_DEBUG,
    "workid = %d, wr->notify_buf = %d, wr->op = %d, wr->flag = %d, wr->status = %d, wr->addr = %lx, wr->size = %d, wr->fd = %d",
    worker->GetWorkerId(), *wr->notify_buf, wr->op, wr->flag, wr->status,
    wr->addr, wr->size, wr->fd);
  long start_time = get_time();
  int ret = worker->ProcessLocalRequest(wr);  //not complete due to remote or previously-sent similar requests


  if (wr->op == RLOCK || wr->op == WLOCK) {
    agent_stats_inst.stop_record_app_thread_with_op(wr->addr, APP_THREAD_OP::AFTER_PROCESS_LOCAL_REQUEST_LOCK);
  } else if (wr->op == UNLOCK) {
    agent_stats_inst.stop_record_app_thread_with_op(wr->addr, APP_THREAD_OP::AFTER_PROCESS_LOCAL_REQUEST_UNLOCK);
  } else if (wr->op == READ) {
    agent_stats_inst.stop_record_app_thread_with_op(wr->addr, APP_THREAD_OP::AFTER_PROCESS_LOCAL_REQUEST_READ);
  } else if (wr->op == WRITE) {
    agent_stats_inst.stop_record_app_thread_with_op(wr->addr, APP_THREAD_OP::AFTER_PROCESS_LOCAL_REQUEST_WRITE);
  } else {
    agent_stats_inst.stop_record_app_thread_with_op(wr->addr, APP_THREAD_OP::AFTER_PROCESS_LOCAL_REQUEST_OTHER);
  }


  agent_stats_inst.start_record_app_thread(wr->addr);
  if (ret) {  //not complete due to remote or previously-sent similar requests
    if (wr->flag & ASYNC) {
      return SUCCESS;
    } else {
#ifdef USE_PIPE_W_TO_H
      char buf[1];
      if (1 != read(recv_pipe[0], buf, 1)) {  //blocking
        epicLog(LOG_WARNING, "read notification from worker failed");
      } else {
        epicLog(LOG_DEBUG, "request returned %c", buf[0]);
        if (wr->status) {
          epicLog(LOG_INFO, "request failed %d\n", wr->status);
        }
      }
#elif defined(USE_PTHREAD_COND)
      int ret = pthread_cond_wait(&cond, &cond_lock);
      epicAssert(!ret);
#else
      epicLog(LOG_DEBUG, "Waiting for remote reply");
      while (*notify_buf != 2);
      agent_stats_inst.stop_record_app_thread_with_op(wr->addr, APP_THREAD_OP::WAIT_ASYNC_FINISH);
      agent_stats_inst.start_record_app_thread(wr->addr);
      long end_time = get_time();
      worker->cdf_cnt_remote[latency_to_bkt((end_time - start_time) / 1000)]++;
      //if(wr->op == READ) {
      //  worker->cache_read_miss_time_ += end_time - start_time;
      //} else if (wr->op == WRITE) {
      //  worker->cache_write_miss_time_ += end_time - start_time;
      //}
      epicLog(LOG_DEBUG, "get notified via buf");
#endif
      return wr->status;
    }
  } else {
    long end_time = get_time();
    worker->cdf_cnt_local[latency_to_bkt((end_time - start_time) / 1000)]++;
    return wr->status;
  }
#else //if not MULTITHREAD

  WorkRequest *to = wr;
  char buf[1];
  buf[0] = 's';

  if (wr->flag & ASYNC) {  //if the work request is asynchronous, we return immediately
    //copy the workrequest
    to = wr->Copy();
  }

#ifdef USE_PIPE_W_TO_H
  to->fd = recv_pipe[1];
  wqueue->push(to);
#elif defined(USE_PTHREAD_COND)
  to->cond_lock = &cond_lock;
  to->cond = &cond;
  to->fd = recv_pipe[1];
  wqueue->push(to);
#else
#ifdef USE_BOOST_QUEUE
  *notify_buf = 1;  //not useful to set it to 1 if boost_queue is enabled
  epicAssert(*(int *)notify_buf == 1);
  to->fd = recv_pipe[1];//for legacy code
  to->notify_buf = this->notify_buf;
  wqueue->push(to);
#elif defined(USE_BUF_ONLY)
  *(WorkRequest **)(notify_buf + 1) = to;
  to->fd = recv_pipe[1];  //for legacy code
  to->notify_buf = this->notify_buf;
  *notify_buf = 1;//put last since it will trigger the process
#else
  *notify_buf = 1;  //put first since notify_buf = 1 may happen after notify_buf = 2 by worker
  epicAssert(*(int *)notify_buf == 1);
  to->fd = recv_pipe[1];//for legacy code
  to->notify_buf = this->notify_buf;
  wqueue->push(to);
#endif
#endif

#ifndef USE_BUF_ONLY //otherwise, we have to return after the worker copy the data from notify_buf
  //we return directly without writing to the pipe to notify the worker thread
  if (to->flag & ASYNC) {
    epicLog(LOG_DEBUG, "asynchronous request");
    return SUCCESS;
  }
#endif

#ifdef USE_PIPE_H_TO_W
#ifdef WH_USE_LOCK
  if (lock.try_lock()) {
#endif
    if (1 != write(send_pipe[1], buf, 1)) {
      epicLog(LOG_WARNING, "write to pipe failed (%d:%s)", errno,
        strerror(errno));
    }

    //we write twice in order to reduce the epoll latency
    if (1 != write(send_pipe[1], buf, 1)) {
      epicLog(LOG_WARNING, "write to pipe failed (%d:%s)", errno,
        strerror(errno));
    }

#ifdef WH_USE_LOCK
    lock.unlock();
  }
#endif
#endif

#ifdef USE_PIPE_W_TO_H
  if (1 != read(recv_pipe[0], buf, 1)) {  //blocking
    epicLog(LOG_WARNING, "read notification from worker failed");
  } else {
    epicLog(LOG_DEBUG, "request returned %c", buf[0]);
    if (wr->status) {
      epicLog(LOG_INFO, "request failed %d\n", wr->status);
    }
  }
#elif defined(USE_PTHREAD_COND)
  int ret = pthread_cond_wait(&cond, &cond_lock);
  epicAssert(!ret);
#else
  while (*notify_buf != 2);
  //while(atomic_read(notify_buf) != 2);
  epicLog(LOG_DEBUG, "get notified via buf");
#endif
  return wr->status;

#endif //MULTITHREAD end
}

void WorkerHandle::ReportCacheStatistics() {
  epicLog(LOG_WARNING, "Cache Statistics");
  fprintf(stdout, "lread lrhit lwrite lwhit rread rrhit rwrite rwhit\n");
  fprintf(stdout, "%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\n"
    , worker->no_local_reads_.load()
    , worker->no_local_reads_hit_.load()
    , worker->no_local_writes_.load()
    , worker->no_local_writes_hit_.load()
    , worker->no_remote_reads_.load()
    , worker->no_remote_reads_hit_.load()
    , worker->no_remote_writes_.load()
    , worker->no_remote_writes_hit_.load()
    , worker->no_remote_writes_direct_hit_.load());
}

void WorkerHandle::CollectCacheStatistics() {
  epicLog(LOG_WARNING, "Cache Statistics");
  fprintf(stdout, "rh1 rh2 rh3 rh4 wh1 wh2 wh3 rm wm\n");
  fprintf(stdout, "%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\n"
    , worker->cache_read_hit_case1_.load()
    , worker->cache_read_hit_case2_.load()
    , worker->cache_read_hit_case3_.load()
    , worker->cache_read_hit_case4_.load()
    , worker->cache_write_hit_case1_.load()
    , worker->cache_write_hit_case2_.load()
    , worker->cache_write_hit_case3_.load()
    , worker->cache_read_miss_.load()
    , worker->cache_write_miss_.load());
  fprintf(stdout, "Average latency for: rh1 rh2 rh3 rh4 wh1 wh2 wh3 rm wm\n");
  fprintf(stdout, "%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\n"
    , worker->cache_read_hit_case1_time_.load()
    , worker->cache_read_hit_case2_time_.load()
    , worker->cache_read_hit_case3_time_.load()
    , worker->cache_read_hit_case4_time_.load()
    , worker->cache_write_hit_case1_time_.load()
    , worker->cache_write_hit_case2_time_.load()
    , worker->cache_write_hit_case3_time_.load()
    , worker->cache_read_miss_time_.load()
    , worker->cache_write_miss_time_.load());
}

void WorkerHandle::CollectEvictStatistics(int thread_num, int pass_num) {
  fprintf(stdout, "Eviction: thread: %d pass: %d count: %lld\n", thread_num, pass_num, worker->num_evict_.load());
}

void WorkerHandle::CollectInvalidStatistics(int thread_num, int pass_num) {
  fprintf(stdout, "Invalidation: thread: %d pass: %d count: %lld\n", thread_num, pass_num, worker->num_invalid_.load());
}

void WorkerHandle::CollectRemoteStatistics(int thread_num, int pass_num) {
  fprintf(stdout, "Remote request send: thread: %d pass: %d count: %lld\n", thread_num, pass_num, worker->num_request_send_.load());
}

void WorkerHandle::CollectNetworkCdf(int thread_num, int pass_num) {
  int i = 0;
  for (i = 0; i < CDF_BUCKET_NUM; i++)
    printf("CDF Network: thread: %d pass: %d count: %lu\n", thread_num, pass_num, worker->cdf_cnt_network[i].load());
}

void WorkerHandle::CollectEvictCdf(int thread_num, int pass_num) {
  worker->CollectEvictCdf(thread_num, pass_num);
}

void WorkerHandle::CollectLocalRemoteCdf(int thread_num, int pass_num) {
  int i = 0;
  for (i = 0; i < CDF_BUCKET_NUM; i++)
    printf("CDF LOCAL: thread: %d pass: %d count: %lu\n", thread_num, pass_num, worker->cdf_cnt_local[i].load());
  for (i = 0; i < CDF_BUCKET_NUM; i++)
    printf("CDF REMOTE: thread: %d pass: %d count: %lu\n", thread_num, pass_num, worker->cdf_cnt_remote[i].load());
}

void WorkerHandle::ResetCacheStatistics() {

  worker->no_local_reads_ = 0;
  worker->no_local_reads_hit_ = 0;

  worker->no_local_writes_ = 0;
  worker->no_local_writes_hit_ = 0;

  worker->no_remote_reads_ = 0;
  worker->no_remote_reads_hit_ = 0;

  worker->no_remote_writes_ = 0;
  worker->no_remote_writes_hit_ = 0;

  worker->no_remote_writes_direct_hit_ = 0;
}

