// Copyright (c) 2018 The GAM Authors 


#include <thread>
#include <atomic>
#include <stdlib.h>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <cstring>
#include <mutex>

#include "../include/lockwrapper.h"
#include "../include/structure.h"
#include "zmalloc.h"
#include "util.h"
#include "gallocator.h"


/***********************************/
/******** MY CODE STARTS ********/
#include <cstdint>
#include <chrono>
#include <vector>
using namespace std;
using namespace chrono;
// #define STEPS 204800 //100M much larger than 10M L3 cache
// long ITERATION = 2000000;
#define STEPS 204800
long ITERATION = 204800;
long ITERATION_warmup = 204800;
agent_stats agent_stats_inst;
int is_cache = 0;
int is_request = 0;
int cache_rw = 0;
int request_rw = 0;
// unsigned int seedp = 0;
// int is_home = 0; // = is_master
/******** MY CODE ENDS ********/
/***********************************/

//#define PERF_GET
//#define PERF_MALLOC

//#define BENCHMARK_DEBUG
//#define STATS_COLLECTION
//#define LOCAL_MEMORY


#define DEBUG_LEVEL LOG_WARNING

#define SYNC_KEY STEPS

int node_id;

int is_master = 1;
string ip_master = get_local_ip("eth0");
string ip_worker = get_local_ip("eth0");
int port_master = 12345;
int port_worker = 12346;

const char* result_file = "result.csv";

//exp parameters
//long FENCE_PERIOD = 1000;
int no_thread = 2;
int no_node = 1;
int remote_ratio = 0;  //0..100
int shared_ratio = 10;  //0..100
int space_locality = 10;  //0..100
int time_locality = 10;  //0..100 (how probable it is to re-visit the current position)
int read_ratio = 10;  //0..100
int op_type = 0;  //0: read/write; 1: rlock/wlock; 2: rlock+read/wlock+write

float cache_th = 0.15;  //0.15

//runtime statistics
atomic<long> remote_access(0);
atomic<long> shared_access(0);
atomic<long> space_local_access(0);
atomic<long> time_local_access(0);
atomic<long> read_access(0);

atomic<long> total_throughput(0);
atomic<long> avg_latency(0);

bool reset = false;

set<GAddr> gen_accesses;
set<GAddr> real_accesses;
LockWrapper stat_lock;

int addr_size = sizeof(GAddr);
int item_size = addr_size;
int items_per_block = BLOCK_SIZE / item_size;

bool TrueOrFalse(double probability, unsigned int* seedp) {
  return (rand_r(seedp) % 100) < probability;
}


int CyclingIncr(int a, int cycle_size) {
  return ++a == cycle_size ? 0 : a;
}

double Revise(double orig, int remaining, bool positive) {
  if (positive) {  //false positive
    return (remaining * orig - 1) / remaining;
  } else {  //false negative
    return (remaining * orig + 1) / remaining;
  }
}

/*
 * not used any more
 */
void PopulateOneBlock(GAlloc* alloc, GAddr data[], GAddr* ldata[], int i,
                      double remote_ratio, double space_locality,
                      unsigned int* seedp) {
  for (int j = 0; j < items_per_block; j++) {
    GAddr next;
    if (j + 1 == items_per_block) {
      //next = GADD(data[GetRandom(0, STEPS, seedp)], GetRandom(0, items_per_block, seedp)*item_size); //data[CyclingIncr(i, STEPS)];
      GAddr n = GADD(data[GetRandom(0, STEPS, seedp)],
                     GetRandom(0, items_per_block, seedp) * item_size);
      while (TOBLOCK(n) == TOBLOCK(data[i])) {
        n = GADD(data[GetRandom(0, STEPS, seedp)],
                 GetRandom(0, items_per_block, seedp) * item_size);
      }
      next = n;
    } else {
      if (TrueOrFalse(space_locality, seedp)) {
        next = GADD(data[i], (j + 1) * item_size);
      } else {
        //next = GADD(data[GetRandom(0, STEPS, seedp)], GetRandom(0, items_per_block, seedp)*item_size); //data[CyclingIncr(i, STEPS)];
        GAddr n = GADD(data[GetRandom(0, STEPS, seedp)],
                       GetRandom(0, items_per_block, seedp) * item_size);
        while (TOBLOCK(n) == TOBLOCK(data[i])) {
          n = GADD(data[GetRandom(0, STEPS, seedp)],
                   GetRandom(0, items_per_block, seedp) * item_size);
        }
        next = n;
      }
    }
#ifdef LOCAL_MEMORY
    *((GAddr*)data[i] + j) = next;
#else
    ldata[i][j] = next;
#endif

#ifdef STATS_COLLECTION
    stat_lock.lock();
    gen_accesses.insert(TOBLOCK(next));
    stat_lock.unlock();
#endif
  }
}

void Init(GAlloc* alloc, GAddr data[], GAddr access[], bool shared[], int id,
          unsigned int* seedp) {
  epicLog(LOG_WARNING, "start init");

  int l_remote_ratio = remote_ratio;
  int l_space_locality = space_locality;
  int l_shared_ratio = shared_ratio;

  //the main thread (id == 0) in the master node (is_master == true)
  // is responsible for reference data access pattern
  if (is_master && id == 0) {
    epicLog(LOG_WARNING, "checkpoint 0");
    for (int i = 0; i < STEPS; i++) {
      if (TrueOrFalse(l_shared_ratio, seedp)) {
        shared[i] = true;
      } else {
        shared[i] = false;
      }
#ifdef LOCAL_MEMORY
      int ret = posix_memalign((void **)&data[i], BLOCK_SIZE, BLOCK_SIZE);
      epicAssert(!ret);
      epicAssert(data[i] % BLOCK_SIZE == 0);
#else
      if (TrueOrFalse(remote_ratio, seedp)) {
        data[i] = alloc->AlignedMalloc(BLOCK_SIZE, REMOTE);
      } else {
        data[i] = alloc->AlignedMalloc(BLOCK_SIZE);
      }
#endif
      if (shared_ratio != 0)
        alloc->Put(i, &data[i], addr_size);
#ifdef BENCHMARK_DEBUG
      if (shared_ratio != 0) {
        GAddr readback;
        int back_ret = alloc->Get(i, &readback);
        epicAssert(back_ret == addr_size);
        epicAssert(readback == data[i]);
      }
#endif
    }
  } else {
    epicLog(LOG_WARNING, "checkpoint 1");
    for (int i = 0; i < STEPS; i++) {
      //we prioritize the shared ratio over other parameters
      if (TrueOrFalse(l_shared_ratio, seedp)) {
        GAddr addr;
        int ret = alloc->Get(i, &addr);
        epicAssert(ret == addr_size);
        data[i] = addr;
        //revise the l_remote_ratio accordingly if we get the shared addr violate the remote probability
        if (TrueOrFalse(l_remote_ratio, seedp)) {  //should be remote
          if (alloc->GetID() == WID(addr)) {  //false negative
            l_remote_ratio = Revise(l_remote_ratio, STEPS - i - 1, false);
          }
        } else {  //shouldn't be remote
          if (alloc->GetID() != WID(addr)) {  //false positive
            l_remote_ratio = Revise(l_remote_ratio, STEPS - i - 1, true);
          }
        }
        shared[i] = true;
      } else {
#ifdef LOCAL_MEMORY
        int ret = posix_memalign((void **)&data[i], BLOCK_SIZE, BLOCK_SIZE);
        epicAssert(data[i] % BLOCK_SIZE == 0);
#else
        if (TrueOrFalse(remote_ratio, seedp)) {
          data[i] = alloc->AlignedMalloc(BLOCK_SIZE, REMOTE);
        } else {
          data[i] = alloc->AlignedMalloc(BLOCK_SIZE);
        }
#endif
        shared[i] = false;
      }
    }
  }
  //access[0] = data[0];
  epicLog(LOG_WARNING, "checkpoint 2");


//   access[0] = data[GetRandom(0, STEPS, seedp)];
// #ifdef STATS_COLLECTION
//   stat_lock.lock();
//   gen_accesses.insert(TOBLOCK(access[0]));
//   stat_lock.unlock();
// #endif
//   for (int i = 1; i < ITERATION_warmup; i++) {
//     //PopulateOneBlock(alloc, data, ldata, i, l_remote_ratio, l_space_locality, seedp);
//     GAddr next;
//     if (TrueOrFalse(space_locality, seedp)) {
//       next = GADD(access[i - 1], item_size);
//       if (TOBLOCK(next) != TOBLOCK(access[i - 1])) {
//         next = TOBLOCK(access[i - 1]);
//       }
//     } else {
//       GAddr n = data[GetRandom(0, STEPS, seedp)];
//       while (TOBLOCK(n) == TOBLOCK(access[i - 1])) {
//         n = data[GetRandom(0, STEPS, seedp)];
//       }
//       next = GADD(n, GetRandom(0, items_per_block, seedp) * item_size);
//     }
//     access[i] = next;
// #ifdef STATS_COLLECTION
//     stat_lock.lock();
//     gen_accesses.insert(TOBLOCK(next));
//     stat_lock.unlock();
// #endif
//   }



  /***********************************/
  /******** MY CODE STARTS ********/
  // needs sequential access!!!!!!!!!
  access[0] = data[0];
#ifdef STATS_COLLECTION
  stat_lock.lock();
  gen_accesses.insert(TOBLOCK(access[0]));
  stat_lock.unlock();
#endif
  for (int i = 1; i < ITERATION_warmup; i++) {
    GAddr next;
    GAddr n = data[i];
    next = GADD(n, GetRandom(0, items_per_block, seedp) * item_size); // one item in the block
    access[i] = next;
#ifdef STATS_COLLECTION
    stat_lock.lock();
    gen_accesses.insert(TOBLOCK(next));
    stat_lock.unlock();
#endif
  }
  /******** MY CODE ENDS ********/
  /***********************************/



  epicLog(LOG_WARNING, "end init");
}

bool Equal(char buf1[], char buf2[], int size) {
  int i;
  for (i = 0; i < size; i++) {
    if (buf1[i] != buf2[i]) {
      break;
    }
  }
  return i == size ? true : false;
}




void Run_cache(GAlloc* alloc, GAddr data[], GAddr access[],
         unordered_map<GAddr, int>& addr_to_pos, bool shared[], int id,
         unsigned int* seedp, bool warmup, my_stats& my_stats_inst) {



    /***********************************/
    /******** MY CODE STARTS ********/
    vector<userop_stats> userop_stats_vector_inst;
    userop_stats userop_stats_inst;
    agent_stats_inst.start_flag = 0;
    /******** MY CODE ENDS ********/
    /***********************************/
  
  

    GAddr to_access = access[0];  //access starting point
    char buf[item_size];
    int ret;
    int j = 0;
    long start = get_time();
    for (int i = 0; i < ITERATION_warmup; i++) {



    /***********************************/
    /******** MY CODE STARTS ********/
    auto userop_start = system_clock::now();
    /******** MY CODE ENDS ********/
    /***********************************/


#ifdef STATS_COLLECTION
    int pos;
    try {
      pos = addr_to_pos.at(TOBLOCK(to_access));
    } catch (const exception& e) {
      epicLog(LOG_WARNING, "cannot find pos for addr %lx\n", TOBLOCK(to_access), e.what());
      epicAssert(false);
    }
    if(WID(to_access) != alloc->GetID()) {
      remote_access++;
    }
    if(shared[pos]) {
      shared_access++;
    }
    stat_lock.lock();
    real_accesses.insert(TOBLOCK(to_access));
    //printf("%lx,%lx\n", TOBLOCK(to_access), to_access);
    stat_lock.unlock();
#endif



    /***********************************/
    /******** MY CODE STARTS ********/
      switch (cache_rw) {
        case 0:{
          userop_stats_inst.userop_type = "RL+R";
          alloc->RLock(userop_stats_inst, to_access, item_size);
          memset(buf, 0, item_size);
          ret = alloc->Read(to_access, buf, item_size);
          alloc->UnLock(to_access, item_size);
#ifdef STATS_COLLECTION
          read_access++;
#endif
          break;}

        case 1:{
          userop_stats_inst.userop_type = "WL+W";
          alloc->WLock(userop_stats_inst, to_access, item_size);
          memset(buf, i, item_size);
          ret = alloc->Write(to_access, buf, item_size);
#ifdef BENCHMARK_DEBUG
          char readback[item_size];
          int back_ret = alloc->Read(to_access, &readback, item_size);
          epicAssert(back_ret == item_size);
          epicAssert(Equal(readback, buf, item_size));
#endif
          alloc->UnLock(to_access, item_size);
          epicAssert(item_size == ret);
          break;}

        default:{
          break;}
      }
    /******** MY CODE ENDS ********/
    /***********************************/



    //time locality
    if (TrueOrFalse(time_locality, seedp)) {
      //we keep to access the same addr
      //epicLog(LOG_DEBUG, "keep to access the current location");
#ifdef STATS_COLLECTION
      time_local_access++;
#endif
    } else {
      j++;
      if (j == ITERATION_warmup) {
        j = 0;
        epicAssert(i == ITERATION_warmup - 1);
      }
#ifdef STATS_COLLECTION
      if (TOBLOCK(to_access) == TOBLOCK(access[j])) {
        space_local_access++;
      }
#endif
      to_access = access[j];
      //epicAssert(buf == to_access || addr_to_pos.count(buf) == 0);
    }



    /***********************************/
    /******** MY CODE STARTS ********/
    auto userop_end = system_clock::now();
    auto userop_duration = duration_cast<microseconds>(userop_end - userop_start);
    userop_stats_inst.userop_latency_us = double(userop_duration.count());
    userop_stats_vector_inst.push_back(userop_stats_inst);
    /******** MY CODE ENDS ********/
    /***********************************/



  }

  long end = get_time();
  long throughput = ITERATION_warmup / ((double) (end - start) / 1000 / 1000 / 1000);
  long latency = (end - start) / ITERATION_warmup;
  epicLog(
      LOG_WARNING,
      "node_id %d, thread %d, average throughput = %ld per-second, latency = %ld ns %s",
      node_id, id, throughput, latency, warmup ? "(warmup)" : "");
  if (!warmup) {
    total_throughput.fetch_add(throughput);
    avg_latency.fetch_add(latency);
  }



  /***********************************/
  /******** MY CODE STARTS ********/
  if (!warmup) {
    epicLog(LOG_WARNING, "amax 8, checkpoint 0, id=%d", id);
    my_stats_inst.add_onethread_stats(userop_stats_vector_inst);
    epicLog(LOG_WARNING, "amax 8, checkpoint 1, id=%d", id);
  }
  /******** MY CODE ENDS ********/
  /***********************************/



}



void Run_request(GAlloc* alloc, GAddr data[], GAddr access[],
         unordered_map<GAddr, int>& addr_to_pos, bool shared[], int id,
         unsigned int* seedp, bool warmup, my_stats& my_stats_inst) {



    /***********************************/
    /******** MY CODE STARTS ********/
    vector<userop_stats> userop_stats_vector_inst;
    userop_stats userop_stats_inst;
    agent_stats_inst.start_flag = 1;
    /******** MY CODE ENDS ********/
    /***********************************/
  
  

    GAddr to_access = access[0];  //access starting point
    char buf[item_size];
    int ret;
    int j = 0;
    long start = get_time();
    for (int i = 0; i < ITERATION; i++) {



    /***********************************/
    /******** MY CODE STARTS ********/
    auto userop_start = system_clock::now();
    /******** MY CODE ENDS ********/
    /***********************************/


#ifdef STATS_COLLECTION
    int pos;
    try {
      pos = addr_to_pos.at(TOBLOCK(to_access));
    } catch (const exception& e) {
      epicLog(LOG_WARNING, "cannot find pos for addr %lx\n", TOBLOCK(to_access), e.what());
      epicAssert(false);
    }
    if(WID(to_access) != alloc->GetID()) {
      remote_access++;
    }
    if(shared[pos]) {
      shared_access++;
    }
    stat_lock.lock();
    real_accesses.insert(TOBLOCK(to_access));
    //printf("%lx,%lx\n", TOBLOCK(to_access), to_access);
    stat_lock.unlock();
#endif



    /***********************************/
    /******** MY CODE STARTS ********/
      switch (request_rw) {
        case 0:{
          userop_stats_inst.userop_type = "RL+R";
          alloc->RLock(userop_stats_inst, to_access, item_size);
          memset(buf, 0, item_size);
          ret = alloc->Read(to_access, buf, item_size);
          alloc->UnLock(to_access, item_size);
#ifdef STATS_COLLECTION
          read_access++;
#endif
          break;}

        case 1:{
          userop_stats_inst.userop_type = "WL+W";
          alloc->WLock(userop_stats_inst, to_access, item_size);
          memset(buf, i, item_size);
          ret = alloc->Write(to_access, buf, item_size);
#ifdef BENCHMARK_DEBUG
          char readback[item_size];
          int back_ret = alloc->Read(to_access, &readback, item_size);
          epicAssert(back_ret == item_size);
          epicAssert(Equal(readback, buf, item_size));
#endif
          alloc->UnLock(to_access, item_size);
          epicAssert(item_size == ret);
          break;}

        default:{
          break;}
      }
    /******** MY CODE ENDS ********/
    /***********************************/



    //time locality
    if (TrueOrFalse(time_locality, seedp)) {
      //we keep to access the same addr
      //epicLog(LOG_DEBUG, "keep to access the current location");
#ifdef STATS_COLLECTION
      time_local_access++;
#endif
    } else {
      j++;
      if (j == ITERATION) {
        j = 0;
        epicAssert(i == ITERATION - 1);
      }
#ifdef STATS_COLLECTION
      if (TOBLOCK(to_access) == TOBLOCK(access[j])) {
        space_local_access++;
      }
#endif
      to_access = access[j];
      //epicAssert(buf == to_access || addr_to_pos.count(buf) == 0);
    }



    /***********************************/
    /******** MY CODE STARTS ********/
    auto userop_end = system_clock::now();
    auto userop_duration = duration_cast<microseconds>(userop_end - userop_start);
    userop_stats_inst.userop_latency_us = double(userop_duration.count());
    userop_stats_vector_inst.push_back(userop_stats_inst);
    // if(userop_stats_inst.cache_coherence_type == "CC"){
    //   epicLog(LOG_WARNING, "i = %d", i);
    // }
    /******** MY CODE ENDS ********/
    /***********************************/



  }

  long end = get_time();
  long throughput = ITERATION / ((double) (end - start) / 1000 / 1000 / 1000);
  long latency = (end - start) / ITERATION;
  epicLog(
      LOG_WARNING,
      "node_id %d, thread %d, average throughput = %ld per-second, latency = %ld ns %s",
      node_id, id, throughput, latency, warmup ? "(warmup)" : "");
  if (!warmup) {
    total_throughput.fetch_add(throughput);
    avg_latency.fetch_add(latency);
  }



  /***********************************/
  /******** MY CODE STARTS ********/
  if (!warmup) {
    epicLog(LOG_WARNING, "amax 8, checkpoint 0, id=%d", id);
    my_stats_inst.add_onethread_stats(userop_stats_vector_inst);
    epicLog(LOG_WARNING, "amax 8, checkpoint 1, id=%d", id);
  }
  /******** MY CODE ENDS ********/
  /***********************************/



}

void Benchmark(int id, my_stats& my_stats_inst) {
  GAlloc* alloc = GAllocFactory::CreateAllocator();


  /***********************************/
  /******** MY CODE STARTS ********/
  unsigned int seedp = 0;//no_node * alloc->GetID() + id;
  epicLog(LOG_INFO, "seedp = %d", seedp);
  /******** MY CODE ENDS ********/
  /***********************************/


#ifdef PERF_MALLOC
  long it = 1000000;
  long start = get_time();
  int len = sizeof(int);
  GAddr addrs[it];
  for (int i = 0; i < it; i++) {
    len = GetRandom(1, 10485, &seedp);
    addrs[i] = alloc->Malloc(len);
    alloc->Free(addrs[i]);
    epicAssert(addrs[i]);
  }
  long end = get_time();
  long duration = end - start;
  epicLog(LOG_WARNING, "Malloc (local): throughput = %lf op/s, latency = %ld ns",
      (double )it / ((double )duration / 1000 / 1000 / 1000),
      duration / it);

  start = get_time();
  for (int i = 0; i < it; i++) {
    len = GetRandom(1, 1048576, &seedp);
    addrs[i] = alloc->Malloc(len, REMOTE);
    alloc->Free(addrs[i]);
  }
  end = get_time();
  duration = end - start;
  epicLog(LOG_WARNING, "Malloc (remote): throughput = %lf op/s, latency = %ld ns",
      (double )it / ((double )duration / 1000 / 1000 / 1000),
      duration / it);
#endif

  GAddr *data = (GAddr*) malloc(sizeof(GAddr) * STEPS);
  unordered_map<GAddr, int> addr_to_pos;

  GAddr* access = (GAddr*) malloc(sizeof(GAddr) * ITERATION_warmup);

  //bool shared[STEPS];
  bool* shared = (bool*) malloc(sizeof(bool) * STEPS);

  Init(alloc, data, access, shared, id, &seedp);

  //init addr_to_pos map
  for (int i = 0; i < STEPS; i++) {
    addr_to_pos[data[i]] = i;
  }

  epicLog(LOG_WARNING, "start warmup the benchmark on thread %d", id);

  bool warmup = true;
  if(is_cache) Run_cache(alloc, data, access, addr_to_pos, shared, id, &seedp, warmup, my_stats_inst);
// #ifndef LOCAL_MEMORY
//   //make sure all the requests are complete
//   alloc->MFence();
//   alloc->WLock(data[0], BLOCK_SIZE);
//   alloc->UnLock(data[0], BLOCK_SIZE);
// #endif
  uint64_t SYNC_RUN_BASE = SYNC_KEY + no_node * 2;
  int sync_id = SYNC_RUN_BASE + no_node * node_id + id;
  alloc->Put(sync_id, &sync_id, sizeof(int));
  for (int i = 1; i <= no_node; i++) {
    for (int j = 0; j < no_thread; j++) {
      epicLog(LOG_WARNING, "waiting for node %d, thread %d", i, j);
      alloc->Get(SYNC_RUN_BASE + no_node * i + j, &sync_id);
      epicAssert(sync_id == SYNC_RUN_BASE + no_node * i + j);
      epicLog(LOG_WARNING, "get sync_id %d from node %d, thread %d", sync_id, i,
              j);
    }
  }

  warmup = false;
  // reset cache statistics
  stat_lock.lock();
  if (!reset) {
    alloc->ResetCacheStatistics();
    reset = true;
  }
  stat_lock.unlock();


  epicLog(LOG_WARNING, "start run the benchmark on thread %d", id);
  if(is_request) Run_request(alloc, data, access, addr_to_pos, shared, id, &seedp, warmup, my_stats_inst);


// #ifndef LOCAL_MEMORY
//   //make sure all the requests are complete
//   alloc->MFence();
//   alloc->WLock(data[0], BLOCK_SIZE);
//   alloc->UnLock(data[0], BLOCK_SIZE);
// #endif
}

int main(int argc, char* argv[]) {
  //the first argument should be the program name
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--ip_master") == 0) {
      ip_master = string(argv[++i]);
    } else if (strcmp(argv[i], "--ip_worker") == 0) {
      ip_worker = string(argv[++i]);
    } else if (strcmp(argv[i], "--port_master") == 0) {
      port_master = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--iface_master") == 0) {
      ip_master = get_local_ip(argv[++i]);
    } else if (strcmp(argv[i], "--port_worker") == 0) {
      port_worker = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--iface_worker") == 0) {
      ip_worker = get_local_ip(argv[++i]);
    } else if (strcmp(argv[i], "--iface") == 0) {
      ip_worker = get_local_ip(argv[++i]);
      ip_master = get_local_ip(argv[i]);
    } else if (strcmp(argv[i], "--is_master") == 0) {
      is_master = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--no_node") == 0) {
      no_node = atoi(argv[++i]);  //0..100
    } else if (strcmp(argv[i], "--result_file") == 0) {
      result_file = argv[++i];  //0..100
    } else if (strcmp(argv[i], "--item_size") == 0) {
      item_size = atoi(argv[++i]);
      items_per_block = BLOCK_SIZE / item_size;
    } else if (strcmp(argv[i], "--cache_th") == 0) {
      cache_th = atof(argv[++i]);
    } 

    /***********************************/
    /******** MY CODE STARTS ********/
    else if(strcmp(argv[i], "--is_cache") == 0){
      is_cache = atoi(argv[++i]);
    }
    else if(strcmp(argv[i], "--cache_rw") == 0){
      cache_rw = atoi(argv[++i]);
    }
    else if(strcmp(argv[i], "--is_request") == 0){
      is_request = atoi(argv[++i]);
    }
    else if(strcmp(argv[i], "--request_rw") == 0){
      request_rw = atoi(argv[++i]);
    }
    /******** MY CODE ENDS ********/
    /***********************************/
    
    else {
      fprintf(stderr, "Unrecognized option %s for benchmark\n", argv[i]);
    }
  }



  /***********************************/
  /******** MY CODE STARTS ********/
  printf("My CC configuration is: ");
  printf("is_home = %d, is_cache = %d, cache_rw = %d, is_request = %d, request_rw = %d\n",
      is_master, is_cache, cache_rw, is_request, request_rw);
  no_thread = 1;
  shared_ratio = 100;
  space_locality = 0;
  time_locality = 0;
  if(is_master == 1){
    remote_ratio = 0;
  }else{
    remote_ratio = 100;
  }
  /******** MY CODE ENDS ********/
  /***********************************/



#ifdef LOCAL_MEMORY
  int memory_type = 0;  //"local memory";
#else
  int memory_type = 1;  //"global memory";
#endif
  printf("Currently configuration is: ");
  printf(
      "master: %s:%d, worker: %s:%d, is_master: %s, no_thread: %d, no_node: %d\n",
      ip_master.c_str(), port_master, ip_worker.c_str(), port_worker,
      is_master == 1 ? "true" : "false", no_thread, no_node);
  printf(
      "no_node = %d, no_thread = %d, remote_ratio: %d, shared_ratio: %d, "
      "space_locality: %d, time_locality: %d, memory_type = %s, item_size = %d, cache_th = %f, result_file = %s\n",
      no_node,
      no_thread,
      remote_ratio,
      shared_ratio,
      space_locality,
      time_locality,
      memory_type == 0 ? "local memory" : "global memory", item_size, cache_th,
      result_file);

  //srand(1);

  Conf conf;
  conf.is_master = is_master;
  conf.master_ip = ip_master;
  conf.master_port = port_master;
  conf.worker_ip = ip_worker;
  conf.worker_port = port_worker;



  /***********************************/
  /******** MY CODE STARTS ********/
  conf.loglevel = LOG_WARNING;//DEBUG_LEVEL; //LOG_FATAL
  // long size = ((long) BLOCK_SIZE) * STEPS * no_thread * 4;
  // conf.size = size < conf.size ? conf.size : size;
  cout << "conf.size = " << conf.size << endl;
  conf.cache_th = cache_th;
  cout << "conf.cache_th = " << conf.cache_th << endl;
  conf.cache_th = ((long) BLOCK_SIZE) * STEPS * conf.cache_th * 1.0 / conf.size;
  cout << "revised conf.cache_th = " << conf.cache_th << endl;
  cout << "gmem size = " << conf.size / (1024 * 1024 * 1024) << "GB" << endl;
  cout << "----------" << endl;
  cout << "user access space = " << ((long) BLOCK_SIZE) * STEPS * 1.0 / (1024 * 1024 * 1024) << "GB" << endl;
  cout << "cache size = " << conf.size * conf.cache_th / (1024 * 1024 * 1024) << "GB" << endl;
  cout << "cache ratio = " << conf.size * conf.cache_th / (((long) BLOCK_SIZE) * STEPS) << endl;
  /******** MY CODE ENDS ********/
  /***********************************/



  GAlloc* alloc = GAllocFactory::CreateAllocator(&conf);

  sleep(1);

  //sync with all the other workers
  //check all the workers are started
  int id;
  node_id = alloc->GetID();
  //sleep(20);
  alloc->Put(SYNC_KEY + node_id, &node_id, sizeof(int));
  //sleep(20);
  for (int i = 1; i <= no_node; i++) {
    alloc->Get(SYNC_KEY + i, &id);
    epicAssert(id == i);
  }

#ifdef PERF_GET
  int it = 1000000;
  alloc->Put(UINT_MAX, &it, sizeof(int));
  long start = get_time();
  int ib;
  for (int i = 0; i < it; i++) {
    alloc->Get(UINT_MAX, &ib);
    epicAssert(ib == it);
  }
  long end = get_time();
  long duration = end - start;
  epicLog(LOG_WARNING, "GET: throughput = %lf op/s, latency = %ld ns",
      (double )it / ((double )duration / 1000 / 1000 / 1000),
      duration / it);
#endif



  /***********************************/
  /******** MY CODE STARTS ********/
  my_stats my_stats_inst;
  thread ths[no_thread];
  for (int i = 0; i < no_thread; i++) {
    // ths[i] = new thread(Benchmark, i, my_stats_inst);
    ths[i] = thread(Benchmark, i, std::ref(my_stats_inst));
  }
  for (int i = 0; i < no_thread; i++) {
    ths[i].join();
  }
  epicLog(LOG_WARNING, "amax 8, checkpoint 2, id=%d", id);
  my_stats_inst.print_userop_stats();
  my_stats_inst.print_agent_stats();
  /******** MY CODE ENDS ********/
  /***********************************/



  // print cache statistics
  alloc->ReportCacheStatistics();

  long t_thr = total_throughput;
  long a_thr = total_throughput;
  a_thr /= no_thread;
  long a_lat = avg_latency;
  a_lat /= no_thread;
  epicLog(
      LOG_WARNING,
      "results for node_id %d: total_throughput: %ld, avg_throuhgput:%ld, avg_latency:%ld",
      node_id, t_thr, a_thr, a_lat);

  //sync with all the other workers
  //check all the benchmark are completed
  long res[3];
  res[0] = t_thr;  //total throughput for the current node
  res[1] = a_thr;  //avg throuhgput for the current node
  res[2] = a_lat;  //avg latency for the current node
  alloc->Put(SYNC_KEY + no_node + node_id, res, sizeof(long) * 3);
  t_thr = a_thr = a_lat = 0;
  for (int i = 1; i <= no_node; i++) {
    memset(res, 0, sizeof(long) * 3);
    alloc->Get(SYNC_KEY + no_node + i, &res);
    t_thr += res[0];
    a_thr += res[1];
    a_lat += res[2];
  }
  a_thr /= no_node;
  a_lat /= no_node;

  if (is_master) {
    ofstream result;
    result.open(result_file, ios::app);
    result << no_node << "," << no_thread << "," << remote_ratio << ","
           << shared_ratio << "," << space_locality << ","
           << time_locality << "," << memory_type << ","
           << item_size << "," << t_thr << "," << a_thr << "," << a_lat << ","
           << cache_th << "\n";
    epicLog(
        LOG_WARNING,
        "results for all the nodes: "
        "no_node: %d, no_thread: %d, remote_ratio: %d, shared_ratio: %d, space_locality: %d, "
        "time_locality: %d, memory_type = %d, item_size = %d, "
        "total_throughput: %ld, avg_throuhgput:%ld, avg_latency:%ld, cache_th = %f\n\n",
        no_node, no_thread, remote_ratio, shared_ratio, 
        space_locality, time_locality, memory_type, item_size, t_thr,
        a_thr, a_lat, cache_th);
    result.close();
  }

#ifdef STATS_COLLECTION
  epicLog(LOG_WARNING, "shared_ratio = %lf, remote_ratio = %lf, space_locality = %lf, time_locality = %lf, "
      "total blocks touched %d, expected blocks touched %d\n",
      ((double)shared_access)/(ITERATION*no_thread)*100/2, ((double)remote_access)/(ITERATION*no_thread)*100/2,
      ((double)space_local_access)/(ITERATION*no_thread)*100/2, ((double)time_local_access)/(ITERATION*no_thread)*100/2,
      real_accesses.size(), gen_accesses.size());
#endif

  long time = 1;
  epicLog(LOG_WARNING, "sleep for %ld s\n\n", time);
  sleep(time);

  return 0;
}



// amax7     /home/zxy/DSM_prj/gam_20240103/scripts/../test_cc/benchmark --is_cache 1 --cache_rw 0 --is_request 1 --request_rw 0 --is_master 1 --ip_master 10.0.0.7 --ip_worker 10.0.0.7 --no_node 2 --port_worker 1234 --port_master 1231 --no_thread 1 --cache_th 2 --result_file /home/zxy/DSM_prj/gam_20240103/scripts/results/read_ratio 
// amax8     /home/zxy/DSM_prj/gam_20240103/scripts/../test_cc/benchmark --is_cache 1 --cache_rw 0 --is_request 1 --request_rw 0 --is_master 0 --ip_master 10.0.0.7 --ip_worker 10.0.0.8 --no_node 2 --port_worker 1234 --port_master 1231 --no_thread 1 --cache_th 2 --result_file /home/zxy/DSM_prj/gam_20240103/scripts/results/read_ratio 