// Copyright (c) 2018 The GAM Authors 


#include <thread>
#include <atomic>
#include <stdlib.h>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <cstring>
#include <mutex>

#include "lockwrapper.h"
#include "structure.h"
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

#define STEPS 2097152 //保证整个系统有8GB共享空间 
long ITERATION = STEPS * 2;

int is_home = 0;
int is_cache = 0;
int is_request = 0;
int cache_rw = 0;
int request_rw = 0;
int breakdown_times = 1024;//1024;//204800;
int local_addr_num = STEPS * 0.1;
/******** MY CODE ENDS ********/
/***********************************/

#define DEBUG_LEVEL LOG_WARNING

#define SYNC_KEY (STEPS*2)

int node_id;

int is_master = 1;
string ip_master = get_local_ip("eth0");
string ip_worker = get_local_ip("eth0");
int port_master = 12345;
int port_worker = 12346;

const char *result_directory = "gam_result";

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
int item_size = 4096;//4096;//addr_size;
int items_per_block = BLOCK_SIZE / item_size;

GAddr *unshared_data;

bool TrueOrFalse(double probability, unsigned int *seedp) {
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

void Init(GAlloc *alloc, GAddr data[], GAddr access[], bool shared[], int id,
  unsigned int *seedp) {

  int l_remote_ratio = remote_ratio;
  int l_space_locality = space_locality;
  int l_shared_ratio = shared_ratio;

  //the main thread (id == 0) in the master node (is_master == true)
  // is responsible for reference data access pattern
  if(id == 0){
    if(is_master){
      epicLog(LOG_WARNING, "master (id = 0) init starts! node_id = %d, thread=%d", node_id, id);
      for (int i = 0; i < STEPS; ++i) {
        // init unshared_data
        if (TrueOrFalse(l_remote_ratio, seedp) && i < local_addr_num) {
          unshared_data[i] = alloc->AlignedMalloc(BLOCK_SIZE, REMOTE);
        } else {
          unshared_data[i] = alloc->AlignedMalloc(BLOCK_SIZE);
        }

        //init shared_data
        shared[i] = TrueOrFalse(l_shared_ratio, seedp);
        if(shared[i]){
          if (TrueOrFalse(l_remote_ratio, seedp)) {
            data[i] = alloc->AlignedMalloc(BLOCK_SIZE, REMOTE);
          } else {
            data[i] = alloc->AlignedMalloc(BLOCK_SIZE);
          }
          alloc->Put(i, &data[i], addr_size);
        }
        else{
          if(i < local_addr_num) data[i] = unshared_data[i];
          else data[i] = unshared_data[GetRandom(0, local_addr_num, seedp)];

          GAddr tmp_data;
          if (TrueOrFalse(l_remote_ratio, seedp)) {
            tmp_data = alloc->AlignedMalloc(BLOCK_SIZE, REMOTE);
          } else {
            tmp_data = alloc->AlignedMalloc(BLOCK_SIZE);
          }
          alloc->Put(i, &tmp_data, addr_size);
        }
      }
      epicLog(LOG_WARNING, "master (id = 0) init ends! node_id = %d, thread=%d", node_id, id);
    }
    else{
      sleep(15);
      epicLog(LOG_WARNING, "non-master (id = 0) init starts! node_id = %d, thread=%d", node_id, id);
      for (int i = 0; i < STEPS; i++) {
        // init unshared_data
        if (TrueOrFalse(l_remote_ratio, seedp) && i < local_addr_num) {
          unshared_data[i] = alloc->AlignedMalloc(BLOCK_SIZE, REMOTE);
        } else {
          unshared_data[i] = alloc->AlignedMalloc(BLOCK_SIZE);
        }

        //init shared_data
        //we prioritize the shared ratio over other parameters
        shared[i] = TrueOrFalse(l_shared_ratio, seedp);
        if (shared[i]) {
          GAddr addr;
          int ret = alloc->Get(i, &addr);
          epicAssert(ret == addr_size);
          data[i] = addr;
          //revise the l_remote_ratio accordingly if we get the shared addr violate the remote probability
          if (TrueOrFalse(l_remote_ratio, seedp)) {  //should be remote
            if (alloc->GetID() == WID(addr)) {  //false negative
              if(STEPS - i - 1 != 0) l_remote_ratio = Revise(l_remote_ratio, STEPS - i - 1, false);
            }
          } else {  //shouldn't be remote
            if (alloc->GetID() != WID(addr)) {  //false positive
              if(STEPS - i - 1 != 0) l_remote_ratio = Revise(l_remote_ratio, STEPS - i - 1, true);
            }
          }
        } else {
          if(i < local_addr_num) data[i] = unshared_data[i];
          else data[i] = unshared_data[GetRandom(0, local_addr_num, seedp)];
        }
      }
      epicLog(LOG_WARNING, "non-master (id = 0) init ends! node_id = %d, thread=%d", node_id, id);
    }
  }
  else{
    sleep(60);
    epicLog(LOG_WARNING, "master/non-master (id != 0) init starts! node_id = %d, thread=%d", node_id, id);
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
            if(STEPS - i - 1 != 0) l_remote_ratio = Revise(l_remote_ratio, STEPS - i - 1, false);
          }
        } else {  //shouldn't be remote
          if (alloc->GetID() != WID(addr)) {  //false positive
            if(STEPS - i - 1 != 0) l_remote_ratio = Revise(l_remote_ratio, STEPS - i - 1, true);
          }
        }
        shared[i] = true;
      } else {
        data[i] = unshared_data[i];
        shared[i] = false;
      }
    }
    epicLog(LOG_WARNING, "master/non-master (id != 0) init ends! node_id = %d, thread=%d", node_id, id);
  }

  epicLog(LOG_WARNING, "breakdown init starts! node_id = %d, thread=%d", node_id, id);
  if(is_home && id == 0){
    for (int i = STEPS; i < STEPS + breakdown_times; i++) {
        data[i] = alloc->AlignedMalloc(BLOCK_SIZE);
        alloc->Put(i, &data[i], addr_size);
    }
  }
  else{
    sleep(10);
    for (int i = STEPS; i < STEPS + breakdown_times; i++) {
      GAddr addr;
      int ret = alloc->Get(i, &addr);
      epicAssert(ret == addr_size);
      data[i] = addr;
    }
  }
  (LOG_WARNING, "breakdown init ends! node_id = %d, thread=%d", node_id, id);

  //access[0] = data[0];
  // epicLog(LOG_WARNING, "checkpoint 2");

  epicLog(LOG_WARNING, "trace init starts! node_id = %d, thread=%d", node_id, id);
  access[0] = data[GetRandom(0, STEPS, seedp)];
  for (int i = 1; i < ITERATION; i++) {
    GAddr next;
    if (TrueOrFalse(space_locality, seedp)) {
      next = GADD(access[i - 1], item_size);
      if (TOBLOCK(next) != TOBLOCK(access[i - 1])) {
        next = TOBLOCK(access[i - 1]);
      }
    } else {
      GAddr n = data[GetRandom(0, STEPS, seedp)];
      while (TOBLOCK(n) == TOBLOCK(access[i - 1])) {
        n = data[GetRandom(0, STEPS, seedp)];
      }
      next = GADD(n, GetRandom(0, items_per_block, seedp) * item_size);
    }
    access[i] = next;
  }
  // epicLog(LOG_WARNING, "checkpoint 3");
  for (int i = 0; i < breakdown_times; i++) {
    GAddr next;
    GAddr n = data[STEPS + i];
    next = n;
    access[ITERATION + i] = next;

    if (id == 0) {
      agent_stats_inst.push_valid_gaddr(next);
    }
  }
  epicLog(LOG_WARNING, "trace init ends! node_id = %d, thread=%d", node_id, id);
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

void Run_cache(GAlloc *alloc, GAddr data[], GAddr access[],
  unordered_map<GAddr, int> &addr_to_pos, bool shared[], int id,
  unsigned int *seedp, bool warmup) {

  char buf[item_size];
  int ret;
  int j = 0;
  long start = get_time();

  for (int i = 0; i < breakdown_times; i++) {
    GAddr to_access = access[ITERATION + i];
    switch (cache_rw) {
      case 0: {
        alloc->RLock(to_access, item_size);
        alloc->UnLock(to_access, item_size);
        break;
      }
      case 1: {
        alloc->WLock(to_access, item_size);
        alloc->UnLock(to_access, item_size);
        break;
      }
      default: {
        break;
      }
    }
  }

  long end = get_time();
  long throughput = breakdown_times / ((double)(end - start) / 1000 / 1000 / 1000);
  long latency = (end - start) / breakdown_times;
  epicLog(
    LOG_WARNING,
    "node_id %d, thread %d, average throughput = %ld per-second, latency = %ld ns %s",
    node_id, id, throughput, latency, warmup ? "(warmup)" : "");
  if (!warmup) {
    total_throughput.fetch_add(throughput);
    avg_latency.fetch_add(latency);
  }
}

void Run_request(GAlloc *alloc, GAddr data[], GAddr access[],
  unordered_map<GAddr, int> &addr_to_pos, bool shared[], int id,
  unsigned int *seedp, bool warmup) {

  int count_4_nobreakdown = 0;
  int count_4_breakdown = 0;
  // edited by cxz, multi 0.75 is used for let app thread 0 stop early than other app thread, so that we can get the "congestion" result
  // int thres_4_nobreakdown = 0.75 * (ITERATION / (breakdown_times + 1)) + 1;
  int thres_4_nobreakdown = (ITERATION / (breakdown_times + 1)) + 1;

  GAddr to_access = access[0];  //access starting point
  char buf[item_size];
  char tmp_buf[item_size];
  int ret;
  int j = 0;

  long start = get_time();
  for (int i = 0; i < ITERATION; i++) {
    // if(id == 0 && i % 10000 == 0) epicLog(LOG_WARNING, "i = %d, id = %d", i, id);
    /***********************************/
    /******** MY CODE STARTS ********/
    if (is_request == 1 && !warmup && id == 0) {
      count_4_nobreakdown++;
      if (count_4_nobreakdown == thres_4_nobreakdown) {
        count_4_nobreakdown = 0;
        GAddr to_access_breakdown = access[ITERATION + count_4_breakdown];
        count_4_breakdown++;

        if(count_4_breakdown <= breakdown_times){

        switch (request_rw) {
          case 0: {
            agent_stats_inst.start_record_app_thread(to_access_breakdown);
            ret = alloc->Read_with_thread_id(id, to_access_breakdown, buf, item_size);
            agent_stats_inst.stop_record_app_thread_with_op(to_access_breakdown, APP_THREAD_OP::WAKEUP_2_READ_RETURN);
            read_access++;
            break;
          }
          case 1:{
            agent_stats_inst.start_record_app_thread(to_access_breakdown);
            alloc->RLock_with_thread_id(id, to_access_breakdown, item_size);
            memcpy(tmp_buf, buf, item_size); //INFO：模拟数据拷贝
            agent_stats_inst.stop_record_app_thread_with_op(to_access_breakdown, APP_THREAD_OP::WAKEUP_2_WRITE_RETURN);
            break;
          }
          default: {
            break;
          }
        }
        }
      }
    }

    if(id == 0) agent_stats_inst.start_record_with_memaccess_type();

    switch (op_type) {
      case 0:{  //read/write
        if (TrueOrFalse(read_ratio, seedp)) {       
          ret = alloc->Read_with_thread_id(id, to_access, buf, item_size);
        } else {
          //INFO: 直接用mfence似乎会卡住
          alloc->WLock_with_thread_id(id, to_access, item_size);
          memcpy(tmp_buf, buf, item_size);
          alloc->UnLock_with_thread_id(id, to_access, item_size);
          // ret = alloc->Write_with_thread_id(id, to_access, buf, item_size);
          // alloc->MFence();
        }
        break;
      }
      default:{
        epicLog(LOG_WARNING, "unknown op type");
        break;
      }
    }

    if(id == 0) agent_stats_inst.stop_record_with_memaccess_type();

    //time locality
    if (TrueOrFalse(time_locality, seedp)) {
      //we keep to access the same addr
      //epicLog(LOG_DEBUG, "keep to access the current location");
    } else {
      j++;
      if (j == ITERATION) {
        j = 0;
        epicAssert(i == ITERATION - 1);
      }
      to_access = access[j];
    }
  }

  //INFO: 直接用mfence似乎会卡住
  // if (op_type == 0) {
  //   // issue a fence and a read request to the last address to ensure all previous
  //   // op have been done
  //   alloc->MFence();
  //   ret = alloc->Read(to_access, buf, item_size);
  // }

  long end = get_time();
  long throughput = (ITERATION + breakdown_times) / ((double)(end - start) / 1000 / 1000 / 1000);
  long latency = (end - start) / (ITERATION + breakdown_times);
  epicLog(
    LOG_WARNING,
    "node_id %d, thread %d, average throughput = %ld per-second, latency = %ld ns %s",
    node_id, id, throughput, latency, warmup ? "(warmup)" : "");
  if (!warmup) {
    total_throughput.fetch_add(throughput);
    avg_latency.fetch_add(latency);
  }
}

void Benchmark(int id) {
  now_thread_id = std::this_thread::get_id();
  GAlloc *alloc = GAllocFactory::CreateAllocator();

  /***********************************/
  /******** MY CODE STARTS ********/
  unsigned int seedp = no_thread * (alloc->GetID() + 1) + id;//0
  epicLog(LOG_INFO, "seedp = %d", seedp);
  /******** MY CODE ENDS ********/
  /***********************************/

  /***********************************/
  /******** MY CODE STARTS ********/
  GAddr *data = (GAddr *)malloc(sizeof(GAddr) * (STEPS + breakdown_times));
  unordered_map<GAddr, int> addr_to_pos;
  GAddr *access = (GAddr *)malloc(sizeof(GAddr) * (ITERATION + breakdown_times));
  //bool shared[STEPS];
  bool *shared = (bool *)malloc(sizeof(bool) * (STEPS + breakdown_times));
  /******** MY CODE ENDS ********/
  /***********************************/

  uint64_t SYNC_RUN_BASE;
  int sync_id;

  Init(alloc, data, access, shared, id, &seedp);

  // SYNC_RUN_BASE = SYNC_KEY * 4 + no_node * 2;
  // sync_id = SYNC_RUN_BASE + no_node * node_id + id;
  // alloc->Put(sync_id, &sync_id, sizeof(int));
  // for (int i = 1; i <= no_node; i++) {
  //   for (int j = 0; j < no_thread; j++) {
  //     epicLog(LOG_INFO, "waiting for node %d, thread %d", i, j);
  //     alloc->Get(SYNC_RUN_BASE + no_node * i + j, &sync_id);
  //     epicAssert(sync_id == SYNC_RUN_BASE + no_node * i + j);
  //     epicLog(LOG_INFO, "get sync_id %d from node %d, thread %d", sync_id, i, j);
  //   }
  // }

  // sleep(2 * id);

  // while(agent_stats_inst.read_thread_init_flag(id) == 0);
  // epicLog(LOG_WARNING, "init starts, id = %d", id);
  // Init(alloc, data, access, shared, id, &seedp);
  // epicLog(LOG_WARNING, "init ends");
  // agent_stats_inst.set1_thread_init_flag(id + 1);

  //init addr_to_pos map
  for (int i = 0; i < STEPS; i++) {
    addr_to_pos[data[i]] = i;
  }

  bool warmup = true;

  epicLog(LOG_WARNING, "start warmup the cache for no-breakdown on node_id %d, thread %d", node_id, id);
  Run_request(alloc, data, access, addr_to_pos, shared, id, &seedp, warmup);
  SYNC_RUN_BASE = SYNC_KEY + no_node * 2;
  sync_id = SYNC_RUN_BASE + no_node * node_id + id;
  alloc->Put(sync_id, &sync_id, sizeof(int));
  for (int i = 1; i <= no_node; i++) {
    for (int j = 0; j < no_thread; j++) {
      epicLog(LOG_INFO, "waiting for node %d, thread %d", i, j);
      alloc->Get(SYNC_RUN_BASE + no_node * i + j, &sync_id);
      epicAssert(sync_id == SYNC_RUN_BASE + no_node * i + j);
      epicLog(LOG_INFO, "get sync_id %d from node %d, thread %d", sync_id, i, j);
    }
  }


  if (is_cache && id == 0) {
    epicLog(LOG_WARNING, "start warmup the cache for breakdown on node_id %d, thread %d", node_id, id);
    Run_cache(alloc, data, access, addr_to_pos, shared, id, &seedp, warmup);
  }
  SYNC_RUN_BASE = SYNC_KEY * 2 + no_node * 2;
  sync_id = SYNC_RUN_BASE + no_node * node_id + id;
  alloc->Put(sync_id, &sync_id, sizeof(int));
  for (int i = 1; i <= no_node; i++) {
    for (int j = 0; j < no_thread; j++) {
      epicLog(LOG_INFO, "waiting for node %d, thread %d", i, j);
      alloc->Get(SYNC_RUN_BASE + no_node * i + j, &sync_id);
      epicAssert(sync_id == SYNC_RUN_BASE + no_node * i + j);
      epicLog(LOG_INFO, "get sync_id %d from node %d, thread %d", sync_id, i, j);
    }
  }

  sleep(5);

  epicLog(LOG_WARNING, "start agent_stats");
  if (id == 0) agent_stats_inst.start_collection();
  SYNC_RUN_BASE = SYNC_KEY * 3 + no_node * 2;
  sync_id = SYNC_RUN_BASE + no_node * node_id + id;
  alloc->Put(sync_id, &sync_id, sizeof(int));
  for (int i = 1; i <= no_node; i++) {
    for (int j = 0; j < no_thread; j++) {
      epicLog(LOG_INFO, "waiting for node %d, thread %d", i, j);
      alloc->Get(SYNC_RUN_BASE + no_node * i + j, &sync_id);
      epicAssert(sync_id == SYNC_RUN_BASE + no_node * i + j);
      epicLog(LOG_INFO, "get sync_id %d from node %d, thread %d", sync_id, i, j);
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


  epicLog(LOG_WARNING, "start run the benchmark on node_id %d, thread %d", node_id, id);
  Run_request(alloc, data, access, addr_to_pos, shared, id, &seedp, warmup);
  epicLog(LOG_WARNING, "benchmark ends on node_id %d, thread %d", node_id, id);

  if (id == 0) {
    // agent_stats_inst.print_app_thread_stat();
    // agent_stats_inst.print_poll_thread_stat();
    // agent_stats_inst.print_multi_sys_thread_stat();
    agent_stats_inst.save_stat_to_file(std::string(result_directory), agent_stats_inst.sys_thread_num, no_thread);
  }
}

int main(int argc, char *argv[]) {
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
    } else if (strcmp(argv[i], "--result_dir") == 0) {
      result_directory = argv[++i];  //0..100
    } else if (strcmp(argv[i], "--item_size") == 0) {
      item_size = atoi(argv[++i]);
      items_per_block = BLOCK_SIZE / item_size;
    } else if (strcmp(argv[i], "--cache_th") == 0) {
      cache_th = atof(argv[++i]);
    } else if (strcmp(argv[i], "--no_sys_thread") == 0) {
      agent_stats_inst.sys_thread_num = atoi(argv[++i]);
    }

    else if (strcmp(argv[i], "--no_thread") == 0) {
      no_thread = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--remote_ratio") == 0) {
      remote_ratio = atoi(argv[++i]);  //0..100
    } else if (strcmp(argv[i], "--shared_ratio") == 0) {
      shared_ratio = atoi(argv[++i]);  //0..100
    } else if (strcmp(argv[i], "--read_ratio") == 0) {
      read_ratio = atoi(argv[++i]);  //0..100
    } else if (strcmp(argv[i], "--space_locality") == 0) {
      space_locality = atoi(argv[++i]);  //0..100
    } else if (strcmp(argv[i], "--time_locality") == 0) {
      time_locality = atoi(argv[++i]);  //0..100
    } else if (strcmp(argv[i], "--op_type") == 0) {
      op_type = atoi(argv[++i]);  //0..100
    }


    /***********************************/
    /******** MY CODE STARTS ********/
    else if (strcmp(argv[i], "--is_cache") == 0) {
      is_cache = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--cache_rw") == 0) {
      cache_rw = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--is_request") == 0) {
      is_request = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--request_rw") == 0) {
      request_rw = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--is_home") == 0) {
      is_home = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--breakdown_times") == 0) {
      breakdown_times = atoi(argv[++i]);
    }
    /******** MY CODE ENDS ********/
    /***********************************/

    else {
      fprintf(stderr, "Unrecognized option %s for benchmark\n", argv[i]);
    }
  }
  if(items_per_block == 0){
    printf("ERROR: items_per_block = 0! BLOCK_SIZE (CACHE_LINE_SIZE) < item_size (read/write bytes)");
    exit(0);
  }

  /***********************************/
  /******** MY CODE STARTS ********/
  printf("My CC configuration is: ");
  printf("is_home = %d, is_cache = %d, cache_rw = %d, is_request = %d, request_rw = %d, breakdown_times = %d\n",
    is_home, is_cache, cache_rw, is_request, request_rw, breakdown_times);
  agent_stats_inst.is_cache = is_cache;
  agent_stats_inst.is_request = is_request;
  agent_stats_inst.is_home = is_home;
  /******** MY CODE ENDS ********/
  /***********************************/

  int memory_type = 1;  //"global memory";

  printf("Currently configuration is: ");
  printf(
    "master: %s:%d, worker: %s:%d, is_master: %s, no_thread: %d, no_node: %d\n",
    ip_master.c_str(), port_master, ip_worker.c_str(), port_worker,
    is_master == 1 ? "true" : "false", no_thread, no_node);
  printf(
    "no_node = %d, no_thread = %d, remote_ratio: %d, shared_ratio: %d, read_ratio: %d, "
    "space_locality: %d, time_locality: %d, op_type = %s, memory_type = %s, item_size = %d, cache_th = %f, result_directory = %s\n",
    no_node,
    no_thread,
    remote_ratio,
    shared_ratio,
    read_ratio,
    space_locality,
    time_locality,
    op_type == 0 ?
    "read/write" :
    (op_type == 1 ?
      "rlock/wlock" :
      (op_type == 2 ? "rlock+read/wlock+write" : "try_rlock/try_wlock")),
    memory_type == 0 ? "local memory" : "global memory", item_size, cache_th,
    result_directory);

  //srand(1);

  Conf conf;
  agent_stats_inst.local_ip = ip_worker;
  conf.is_master = is_master;
  conf.master_ip = ip_master;
  conf.master_port = port_master;
  conf.worker_ip = ip_worker;
  conf.worker_port = port_worker;

  /***********************************/
  /******** MY CODE STARTS ********/
  conf.loglevel = LOG_WARNING;//LOG_DEBUG;//LOG_WARNING;
  long size = (1UL << 34) + (1UL << 34); // 16+16=32GB
  conf.size = size < conf.size ? conf.size : size;
  cout << "conf.sb_allc_size = " << conf.size << endl;
  conf.cache_th = cache_th;
  cout << "conf.app_cache_ratio = " << conf.cache_th << endl;
  conf.cache_th = (((long)BLOCK_SIZE) * STEPS * 2) * conf.cache_th / conf.size;
  cout << "conf.reserved_cache_th = " << conf.cache_th << endl;
  cout << "gmem size = " << conf.size / (1024 * 1024 * 1024) << "GB" << endl;
  agent_stats_inst.end_collection();

  unshared_data = (GAddr *)malloc(sizeof(GAddr) * (STEPS + breakdown_times));
  /******** MY CODE ENDS ********/
  /***********************************/

  printf("CreateAllocator starts\n");
  GAlloc *alloc = GAllocFactory::CreateAllocator(&conf);
  printf("CreateAllocator ends\n");

  sleep(1);

  //sync with all the other workers
  //check all the workers are started
  printf("sync starts\n");
  int id;
  node_id = alloc->GetID();
  //sleep(20);
  alloc->Put(SYNC_KEY + node_id, &node_id, sizeof(int));
  //sleep(20);
  for (int i = 1; i <= no_node; i++) {
    alloc->Get(SYNC_KEY + i, &id);
    epicAssert(id == i);
  }
  printf("sync ends\n");

  /***********************************/
  /******** MY CODE STARTS ********/
  epicLog(LOG_WARNING, "benchmark starts (numa0 & numa1)");
  agent_stats_inst.set1_thread_init_flag(0);
  thread ths[no_thread];
  for (int i = 0; i < no_thread; i++) {
    ths[i] = thread(Benchmark, i);
    if(i < 24) bind_to_core(ths[i], 1, i);
    // if(i < 23) bind_to_core(ths[i], 0, i+1);
    // else bind_to_core(ths[i], 0, 0);
  }
  for (int i = 0; i < no_thread; i++) {
    ths[i].join();
  }
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
  alloc->Put(SYNC_KEY * 4 + no_node + node_id, res, sizeof(long) * 3);
  t_thr = a_thr = a_lat = 0;
  for (int i = 1; i <= no_node; i++) {
    memset(res, 0, sizeof(long) * 3);
    alloc->Get(SYNC_KEY * 4 + no_node + i, &res);
    t_thr += res[0];
    a_thr += res[1];
    a_lat += res[2];
  }
  a_thr /= no_node;
  a_lat /= no_node;

  if (is_master) {
    epicLog(
      LOG_WARNING,
      "results for all the nodes: "
      "no_node: %d, no_sys_thread: %d, no_app_thread: %d, remote_ratio: %d, shared_ratio: %d, read_ratio: %d, space_locality: %d, "
      "time_locality: %d, op_type = %d, memory_type = %d, item_size = %d, "
      "total_throughput: %ld, avg_throuhgput:%ld, avg_latency:%ld, cache_th = %f\n\n",
      no_node, agent_stats_inst.sys_thread_num, no_thread, remote_ratio, shared_ratio, read_ratio,
      space_locality, time_locality, op_type, memory_type, item_size, t_thr,
      a_thr, a_lat, cache_th);

    std::string common_suffix = ".txt";
    if (!std::experimental::filesystem::exists(result_directory)) {
        if (!std::experimental::filesystem::create_directory(result_directory)) {
            std::cerr << "Error creating folder " << result_directory << std::endl;
            exit(1);
        }
    }
    FILE *file;
    std::experimental::filesystem::path dir(result_directory);
    std::experimental::filesystem::path filePath = dir / std::experimental::filesystem::path("end_to_end" + common_suffix);
    file = fopen(filePath.c_str(), "a");
    assert(file != nullptr);
    fprintf(
      file,
      "%d\t%ld\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%ld\t%ld\t%ld\t%f\n",
      no_node, agent_stats_inst.sys_thread_num, no_thread, remote_ratio, shared_ratio, read_ratio,
      space_locality, time_locality, op_type, memory_type, item_size, 
      t_thr, a_thr, a_lat, cache_th);
    fclose(file);
  }
  long time = 5;
  epicLog(LOG_WARNING, "sleep for %ld s\n\n", time);
  sleep(time);
  return 0;
}