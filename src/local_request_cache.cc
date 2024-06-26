// Copyright (c) 2018 The GAM Authors

int Worker::ProcessLocalRead(WorkRequest *wr) {
  epicAssert(wr->addr);
  epicAssert(!(wr->flag & ASYNC));
  //long init_time = get_time();
  if (!(wr->flag & FENCE)) {
    Fence *fence = fences_.at(wr->fd);
    fence->lock();
    if (unlikely(IsMFenced(fence, wr))) {
      AddToFence(fence, wr);
      epicLog(LOG_DEBUG, "fenced (mfenced = %d, sfenced = %d): %d",
        fence->mfenced, fence->sfenced, wr->op);
      fence->unlock();
      return FENCE_PENDING;
    }
    fence->unlock();
  }
  //long time_stamp_1 = get_time();

  if (likely(IsLocal(wr->addr))) {
    // epicLog(LOG_WARNING, "Why are you here?\n");
    GAddr start = wr->addr;
    GAddr start_blk = TOBLOCK(start);
    GAddr end = GADD(start, wr->size);
    wr->lock();
    /*
     * we increase it by 1 before we push to the to_serve_local_request queue
     * so we have to decrease by 1 again
     */
    if (wr->flag & TO_SERVE) {
      wr->counter--;
    }
    for (GAddr i = start_blk; i < end;) {
      GAddr nextb = BADD(i, 1);
      void *laddr = ToLocal(i);

      directory.lock(laddr);
      DirEntry *entry = directory.GetEntry(laddr);
      DirState s = directory.GetState(entry);
      if (unlikely(directory.InTransitionState(s))) {
        epicLog(LOG_INFO, "directory in transition state when local read %d",
          s);
        //we increase the counter in case
        //we false call Notify()
        wr->counter++;
        AddToServeLocalRequest(i, wr);
        directory.unlock(laddr);
        wr->unlock();
        wr->is_cache_hit_ = false;
        return IN_TRANSITION;
      }

      if (unlikely(s == DIR_DIRTY)) {
        WorkRequest *lwr = new WorkRequest(*wr);
        lwr->counter = 0;
        GAddr rc = directory.GetSList(entry).front();  //only one worker is updating this line
        Client *cli = GetClient(rc);
        lwr->op = FETCH_AND_SHARED;
        lwr->addr = i;
        lwr->size = BLOCK_SIZE;
        lwr->ptr = laddr;
        lwr->parent = wr;
        wr->counter++;
        wr->is_cache_hit_ = false;
        //intermediate state
        epicAssert(s != DIR_TO_SHARED);
        epicAssert(!directory.IsBlockLocked(entry));
        directory.ToToShared(entry, rc);
        SubmitRequest(cli, lwr, ADD_TO_PENDING | REQUEST_SEND);
      } else {
        GAddr gs = i > start ? i : start;
        void *ls = (void *)((ptr_t)wr->ptr + GMINUS(gs, start));
        int len = nextb > end ? GMINUS(end, gs) : GMINUS(nextb, gs);
        memcpy(ls, ToLocal(gs), len);
      }
      directory.unlock(laddr);
      i = nextb;
    }
    if (unlikely(wr->counter)) {
      wr->unlock();
      return REMOTE_REQUEST;
    } else {
      wr->unlock();
    }
  } else {
    //long time_stamp_2 = get_time();
#ifdef PROFILE_LATENCY
    Cache_return_t new_ret = cache.ReadCollect(wr);
    switch (new_ret.op) {
    case CACHE_READ_HIT:
      switch (new_ret.mode) {
      case 1:
        ++cache_read_hit_case1_;
        cache_read_hit_case1_time_ += new_ret.time;
        break;
      case 2:
        ++cache_read_hit_case2_;
        cache_read_hit_case2_time_ += new_ret.time;
        break;
      case 3:
        ++cache_read_hit_case3_;
        cache_read_hit_case3_time_ += new_ret.time;
        break;
      case 4:
        ++cache_read_hit_case4_;
        cache_read_hit_case4_time_ += new_ret.time;
        break;
      }
      break;
    case CACHE_READ_MISS:
      ++cache_read_miss_;
      cache_read_miss_time_ += new_ret.time;
      break;
    case CACHE_WRITE_MISS:
      ++cache_write_miss_;
      cache_write_miss_time_ += new_ret.time;
      break;
    case CACHE_WRITE_HIT:
      switch (new_ret.mode) {
      case 1:
        ++cache_write_hit_case1_;
        cache_write_hit_case1_time_ += new_ret.time;
        break;
      case 2:
        ++cache_write_hit_case2_;
        cache_write_hit_case2_time_ += new_ret.time;
        break;
      case 3:
        ++cache_write_hit_case3_;
        cache_write_hit_case3_time_ += new_ret.time;
        break;
      }
      break;
    }
    int ret = new_ret.original_ret;

#else
    int ret = cache.Read(wr);
#endif
    num_evict_ = cache.get_evict();
    num_request_send_ = cache.get_request_send();
    //long time_stamp_3 = get_time();
    //epicLog(LOG_WARNING, "This level read takes time: %ld 1:%ld 2:%ld\n", time_stamp_3 - time_stamp_2, time_stamp_1 - init_time, time_stamp_2 - time_stamp_1);
    if (ret)
      return REMOTE_REQUEST;
  }


#ifdef MULTITHREAD
  if (wr->flag & TO_SERVE || wr->flag & FENCE) {
#endif
    /*
     * notify the app thread directly
     * this can only happen when the request can be fulfilled locally
     * or we don't need to wait for reply from remote node
     */
    if (Notify(wr)) {
      epicLog(LOG_WARNING, "cannot wake up the app thread");
    }
#ifdef MULTITHREAD
  } else {
    /**
     * In this case, the read request is running in the app thread and
     * is fulfilled in the first trial (i.e., * chache hit)
     */
    epicAssert(wr->is_cache_hit_);
    if (IsLocal(wr->addr)) {
      ++no_local_reads_;
      ++no_local_reads_hit_;
    } else {
      ++no_remote_reads_;
      ++no_remote_reads_hit_;
    }
  }
#endif
  return SUCCESS;
}

int Worker::ProcessLocalReadP2P(WorkRequest *wr) {
  epicAssert(wr->addr);
  epicAssert(!(wr->flag & ASYNC));
  epicAssert(wr->wid > 0 && wr->wid <= 8);
  //just a hard code !!!
  Client *cli = GetClientByIP("10.0.0." + to_string(wr->wid));
  if (cli == nullptr) {
    epicLog(LOG_WARNING, "Cannot find the client by IP");
    epicAssert(false);
  }

  wr->lock();
  WorkRequest *lwr = new WorkRequest(*wr);
  void *new_addr = this->sb.sb_aligned_calloc(1, BLOCK_SIZE + CACHE_LINE_PREFIX);
  lwr->counter = 0;
  lwr->op = READ_P2P;
  lwr->ptr = new_addr;
  lwr->size = BLOCK_SIZE;

  wr->counter++;
  lwr->parent = wr;
  this->SubmitRequest(cli, lwr, ADD_TO_PENDING | REQUEST_SEND);

  int ret = wr->counter;
  wr->unlock();
  return ret;
}

int Worker::ProcessLocalWrite(WorkRequest *wr) {
  epicAssert(wr->addr);
  Fence *fence = fences_.at(wr->fd);
  if (!(wr->flag & FENCE)) {
    fence->lock();
    if (unlikely(IsFenced(fence, wr))) {
      epicLog(LOG_DEBUG, "fenced(mfenced = %d, sfenced = %d): %d",
        fence->mfenced, fence->sfenced, wr->op);
      AddToFence(fence, wr);
      fence->unlock();
      return FENCE_PENDING;
    }
    fence->unlock();
  }
  if ((wr->flag & ASYNC) && !(wr->flag & TO_SERVE)) {
    fence->pending_writes++;
    epicLog(LOG_DEBUG, "Local: one more pending write");
  }
  if (likely(IsLocal(wr->addr))) {
    GAddr start = wr->addr;
    GAddr start_blk = TOBLOCK(start);
    GAddr end = GADD(start, wr->size);
    if (TOBLOCK(end - 1) != start_blk) {
      epicLog(LOG_INFO, "read/write split to multiple blocks");
    }
    wr->lock();
    /*
     * we increase it by 1 before we push to the to_serve_local_request queue
     * so we have to decrease by 1 again
     */
    if (wr->flag & TO_SERVE) {
      wr->counter--;
    }
    for (GAddr i = start_blk; i < end;) {
      epicAssert(
        !(wr->flag & COPY) || ((wr->flag & COPY) && (wr->flag & ASYNC)));

      GAddr nextb = BADD(i, 1);
      void *laddr = ToLocal(i);

      directory.lock(laddr);
      DirEntry *entry = directory.GetEntry(laddr);
      DirState state = directory.GetState(entry);
      if (unlikely(directory.InTransitionState(state))) {
        epicLog(LOG_INFO, "directory in transition state when local write %d",
          state);
        //we increase the counter in case
        //we false call Notify()
        wr->counter++;
        wr->is_cache_hit_ = false;
        if (wr->flag & ASYNC) {
          if (!wr->IsACopy()) {
            wr->unlock();
            wr = wr->Copy();
            wr->lock();
          }
        }
        AddToServeLocalRequest(i, wr);
        directory.unlock(laddr);
        wr->unlock();
        return IN_TRANSITION;
      }

      /*
       * since we cannot guarantee that generating a completion indicates
       * the buf in the remote node has been updated (only means remote HCA received and acked)
       * (ref: http://lists.openfabrics.org/pipermail/general/2007-May/036615.html)
       * so we use Request/Reply mode even for DIR_SHARED invalidations
       * instead of direct WRITE or CAS to invalidate the corresponding cache line in remote node
       */
      if (state == DIR_DIRTY || state == DIR_SHARED) {
        list<GAddr> &shared = directory.GetSList(entry);
        WorkRequest *lwr = new WorkRequest(*wr);
        lwr->counter = 0;
        lwr->op = state == DIR_DIRTY ? FETCH_AND_INVALIDATE : INVALIDATE;
        lwr->addr = i;
        lwr->size = BLOCK_SIZE;
        lwr->ptr = laddr;
        wr->is_cache_hit_ = false;
        if (wr->flag & ASYNC) {
          if (!wr->IsACopy()) {
            wr->unlock();
            wr = wr->Copy();
            wr->lock();
          }
        }
        lwr->parent = wr;
        lwr->id = GetWorkPsn();
        lwr->counter = shared.size();
        wr->counter++;
        epicAssert(state != DIR_TO_UNSHARED);
        epicAssert(
          (state == DIR_DIRTY && !directory.IsBlockLocked(entry))
          || (state == DIR_SHARED && !directory.IsBlockWLocked(entry)));
        directory.ToToUnShared(entry);
        //we move AddToPending before submit request
        //since it is possible that the reply comes back before we add to the pending list
        //if we AddToPending at last
        AddToPending(lwr->id, lwr);
        for (auto it = shared.begin(); it != shared.end(); it++) {
          Client *cli = GetClient(*it);
          epicLog(LOG_DEBUG, "invalidate (%d) cache from worker %d (lwr = %lx)",
            lwr->op, cli->GetWorkerId(), lwr);
          SubmitRequest(cli, lwr);
          //lwr->counter++;
        }
      } else {
#ifdef GFUNC_SUPPORT
        if (wr->flag & GFUNC) {
          epicAssert(wr->gfunc);
          epicAssert(TOBLOCK(wr->addr) == TOBLOCK(GADD(wr->addr, wr->size - 1)));
          epicAssert(i == start_blk);
          void *laddr = ToLocal(wr->addr);
          wr->gfunc(laddr, wr->arg);
        } else {
#endif
          GAddr gs = i > start ? i : start;
          void *ls = (void *)((ptr_t)wr->ptr + GMINUS(gs, start));
          int len = nextb > end ? GMINUS(end, gs) : GMINUS(nextb, gs);
          memcpy(ToLocal(gs), ls, len);
          epicLog(LOG_DEBUG, "copy dirty data in advance");
#ifdef GFUNC_SUPPORT
        }
#endif
      }
      directory.unlock(laddr);
      i = nextb;
    }
    if (wr->counter) {
      wr->unlock();
      return REMOTE_REQUEST;
    } else {
      wr->unlock();
    }
  } else {
#ifdef PROFILE_LATENCY
    Cache_return_t new_ret = cache.WriteCollect(wr);
    switch (new_ret.op) {
    case CACHE_READ_HIT:
      switch (new_ret.mode) {
      case 1:
        ++cache_read_hit_case1_;
        cache_read_hit_case1_time_ += new_ret.time;
        break;
      case 2:
        ++cache_read_hit_case2_;
        cache_read_hit_case2_time_ += new_ret.time;
        break;
      case 3:
        ++cache_read_hit_case3_;
        cache_read_hit_case3_time_ += new_ret.time;
        break;
      case 4:
        ++cache_read_hit_case4_;
        cache_read_hit_case4_time_ += new_ret.time;
        break;
      }
      break;
    case CACHE_READ_MISS:
      ++cache_read_miss_;
      cache_read_miss_time_ += new_ret.time;
      break;
    case CACHE_WRITE_MISS:
      ++cache_write_miss_;
      cache_write_miss_time_ += new_ret.time;
      break;
    case CACHE_WRITE_HIT:
      switch (new_ret.mode) {
      case 1:
        ++cache_write_hit_case1_;
        cache_write_hit_case1_time_ += new_ret.time;
        break;
      case 2:
        ++cache_write_hit_case2_;
        cache_write_hit_case2_time_ += new_ret.time;
        break;
      case 3:
        ++cache_write_hit_case3_;
        cache_write_hit_case3_time_ += new_ret.time;
        break;
      }
      break;
    }
    int ret = new_ret.original_ret;
#else
    int ret = cache.Write(wr);
#endif
    if (ret) {
      return REMOTE_REQUEST;
    }

    ++no_remote_writes_direct_hit_;
  }
#ifdef MULTITHREAD
  if (wr->flag & ASYNC || wr->flag & TO_SERVE || wr->flag & FENCE) {
#endif
    /*
     * notify the app thread directly
     * this can only happen when the request can be fulfilled locally
     * or we don't need to wait for reply from remote node
     */
    if (Notify(wr)) {
      epicLog(LOG_WARNING, "cannot wake up the app thread");
    }
#ifdef MULTITHREAD
  }
#endif
  return SUCCESS;
}

int Worker::ProcessLocalRLock(WorkRequest *wr) {
  epicAssert(wr->addr);
  epicAssert(!(wr->flag & ASYNC));
  //epicAssert(!(wr->flag & FENCE));
  if (!(wr->flag & FENCE)) {
    Fence *fence = fences_.at(wr->fd);
    fence->lock();
    if (IsFenced(fence, wr)) {
      AddToFence(fence, wr);
      fence->unlock();
      epicLog(LOG_DEBUG, "fenced (mfenced = %d, sfenced = %d): %d",
        fence->mfenced, fence->sfenced, wr->op);
      return FENCE_PENDING;
    } else if (fence->pending_writes) {  //we only mark fenced when there are pending writes
      fence->mfenced = true;
      epicLog(LOG_DEBUG, "mfenced from RLOCK!");
      AddToFence(fence, wr);
      fence->unlock();
      return FENCE_PENDING;
    }
    fence->unlock();
  }
  if (IsLocal(wr->addr)) {
    GAddr start = wr->addr;
    GAddr start_blk = TOBLOCK(start);
    void *laddr = ToLocal(start_blk);

    wr->lock();
    directory.lock(laddr);
    DirEntry *entry = directory.GetEntry(laddr);
    DirState state = directory.GetState(entry);
    if (directory.InTransitionState(state)) {
      epicLog(LOG_INFO, "directory in transition state when local read %d",
        state);
      AddToServeLocalRequest(start_blk, wr);
      directory.unlock(laddr);
      wr->unlock();
      return IN_TRANSITION;
    }
    if (state == DIR_DIRTY) {
      WorkRequest *lwr = new WorkRequest(*wr);
      lwr->counter = 0;
      GAddr rc = directory.GetSList(entry).front();  //only one worker is updating this line
      Client *cli = GetClient(rc);
      lwr->op = FETCH_AND_SHARED;
      lwr->addr = start_blk;
      lwr->size = BLOCK_SIZE;
      lwr->ptr = laddr;
      lwr->parent = wr;
      lwr->flag |= LOCKED;
      wr->counter++;
      //intermediate state
      epicAssert(state != DIR_TO_SHARED);
      epicAssert(!directory.IsBlockLocked(entry));
      directory.ToToShared(entry, rc);
      SubmitRequest(cli, lwr, ADD_TO_PENDING | REQUEST_SEND);
    } else {
      int ret;
      if (entry) {
        ret = directory.RLock(entry, ToLocal(wr->addr));
      } else {
        ret = directory.RLock(ToLocal(wr->addr));
      }
      if (ret) {  //fail to lock
        epicLog(LOG_INFO, "cannot lock addr %lx, will try later", wr->addr);
        if (wr->flag & TRY_LOCK) {
          wr->status = LOCK_FAILED;
        } else {
          AddToServeLocalRequest(start_blk, wr);
          directory.unlock(laddr);
          wr->unlock();
          return IN_TRANSITION;
        }
      }
    }
    if (wr->counter) {
      directory.unlock(laddr);
      wr->unlock();
      return REMOTE_REQUEST;
    } else {
      directory.unlock(laddr);
      wr->unlock();
    }
  } else {
    //if there are remote requests
    int ret = cache.RLock(wr);
    if (ret) {
      return REMOTE_REQUEST;
    }
  }
#ifdef MULTITHREAD
  if (wr->flag & TO_SERVE || wr->flag & FENCE) {
#endif
    /*
     * notify the app thread directly
     * this can only happen when the request can be fulfilled locally
     * or we don't need to wait for reply from remote node
     */
    if (Notify(wr)) {
      epicLog(LOG_WARNING, "cannot wake up the app thread");
    }
#ifdef MULTITHREAD
  } else {
    epicAssert(wr->is_cache_hit_);
    if (IsLocal(wr->addr)) {
      ++no_local_reads_;
      ++no_local_reads_hit_;
    } else {
      ++no_remote_reads_;
      ++no_remote_reads_hit_;
    }
  }
#endif
  return SUCCESS;
  }

int Worker::ProcessLocalWLock(WorkRequest *wr) {
  epicAssert(wr->addr);
  epicAssert(!(wr->flag & ASYNC));
  if (!(wr->flag & FENCE)) {
    Fence *fence = fences_.at(wr->fd);
    fence->lock();
    if (IsFenced(fence, wr)) {
      AddToFence(fence, wr);
      fence->unlock();
      epicLog(LOG_DEBUG, "fenced (mfenced = %d, sfenced = %d): %d",
        fence->mfenced, fence->sfenced, wr->op);
      return FENCE_PENDING;
    } else if (fence->pending_writes) {  //we only mark fenced when there are pending writes
      fence->mfenced = true;
      epicLog(LOG_DEBUG, "mfenced from WLOCK!");
      AddToFence(fence, wr);
      fence->unlock();
      return FENCE_PENDING;
    }
    fence->unlock();
  }
  if (IsLocal(wr->addr)) {
    GAddr start = wr->addr;
    GAddr start_blk = TOBLOCK(start);
    void *laddr = ToLocal(start_blk);

    wr->lock();
    directory.lock(laddr);
    DirEntry *entry = directory.GetEntry(laddr);
    DirState state = directory.GetState(entry);
    if (directory.InTransitionState(state)) {
      epicLog(LOG_INFO, "directory in transition state when local write %d",
        state);
      AddToServeLocalRequest(start_blk, wr);
      directory.unlock(laddr);
      wr->unlock();
      return IN_TRANSITION;
    }
    if (DIR_DIRTY == state || DIR_SHARED == state) {
      list<GAddr> &shared = directory.GetSList(entry);
      WorkRequest *lwr = new WorkRequest(*wr);
      lwr->counter = 0;
      lwr->op = state == DIR_DIRTY ? FETCH_AND_INVALIDATE : INVALIDATE;
      lwr->addr = start_blk;
      lwr->size = BLOCK_SIZE;
      lwr->ptr = laddr;
      lwr->parent = wr;
      lwr->flag |= LOCKED;
      lwr->id = GetWorkPsn();
      lwr->counter = shared.size();
      wr->counter++;
      epicAssert(state != DIR_TO_UNSHARED);
      epicAssert(
        (state == DIR_DIRTY && !directory.IsBlockLocked(entry))
        || (state == DIR_SHARED && !directory.IsBlockWLocked(entry)));
      directory.ToToUnShared(entry);
      AddToPending(lwr->id, lwr);
      for (auto it = shared.begin(); it != shared.end(); it++) {
        Client *cli = GetClient(*it);
        epicLog(
          LOG_DEBUG,
          "invalidate (%d) cache from worker %d, state = %d, lwr->counter = %d",
          lwr->op, cli->GetWorkerId(), state, lwr->counter.load());
        SubmitRequest(cli, lwr);
        //lwr->counter++;
      }
    } else if (DIR_UNSHARED == state) {
      int ret;
      if (entry) {
        ret = directory.WLock(entry, ToLocal(wr->addr));
      } else {
        ret = directory.WLock(ToLocal(wr->addr));
      }
      if (ret) {  //failed to lock
        epicLog(LOG_INFO, "cannot lock addr %lx, will try later", wr->addr);
        if (wr->flag & TRY_LOCK) {
          wr->status = LOCK_FAILED;
        } else {
          AddToServeLocalRequest(start_blk, wr);
          directory.unlock(laddr);
          wr->unlock();
          return IN_TRANSITION;
        }
      }
    }
    if (wr->counter) {
      directory.unlock(laddr);
      wr->unlock();
      return REMOTE_REQUEST;
    } else {
      directory.unlock(laddr);
      wr->unlock();
    }
  } else {
    int ret = cache.WLock(wr);
    if (ret) {
      return REMOTE_REQUEST;
    }
  }
#ifdef MULTITHREAD
  if (wr->flag & TO_SERVE || wr->flag & FENCE) {
#endif
    /*
     * notify the app thread directly
     * this can only happen when the request can be fulfilled locally
     * or we don't need to wait for reply from remote node
     */
    if (Notify(wr)) {
      epicLog(LOG_WARNING, "cannot wake up the app thread");
    }
#ifdef MULTITHREAD
  } else {
    epicAssert(wr->is_cache_hit_);
    if (IsLocal(wr->addr)) {
      ++no_local_writes_;
      ++no_local_writes_hit_;
    } else {
      ++no_remote_writes_;
      ++no_remote_writes_hit_;
    }
  }
#endif
  return SUCCESS;
  }

int Worker::ProcessLocalUnLock(WorkRequest *wr) {
  if (!(wr->flag & FENCE)) {
    Fence *fence = fences_.at(wr->fd);
    fence->lock();
    if (IsFenced(fence, wr)) {
      AddToFence(fence, wr);
      fence->unlock();
      epicLog(LOG_DEBUG, "fenced (mfenced = %d, sfenced = %d): %d",
        fence->mfenced, fence->sfenced, wr->op);
      return FENCE_PENDING;
    } else if (fence->pending_writes) {  //we only mark fenced when there are pending writes
      fence->mfenced = true;
      epicLog(LOG_DEBUG, "mfenced from UNLOCK!");
      AddToFence(fence, wr);
      fence->unlock();
      return FENCE_PENDING;
    }
    fence->unlock();
  }

  if (IsLocal(wr->addr)) {
    GAddr start_blk = TOBLOCK(wr->addr);
    void *laddr = ToLocal(start_blk);
    directory.lock(laddr);
    directory.UnLock(ToLocal(wr->addr));
    directory.unlock(laddr);
  } else {
    cache.UnLock(wr->addr);
  }
  ProcessToServeRequest(wr);
#ifdef MULTITHREAD
  if (wr->flag & TO_SERVE || wr->flag & FENCE) {
#endif
    /*
     * notify the app thread directly
     * this can only happen when the request can be fulfilled locally
     * or we don't need to wait for reply from remote node
     */
    if (Notify(wr)) {
      epicLog(LOG_WARNING, "cannot wake up the app thread");
    }
#ifdef MULTITHREAD
  }
#endif
  return SUCCESS;
}

