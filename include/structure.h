// Copyright (c) 2018 The GAM Authors 


#ifndef INCLUDE_STRUCTURE_H_
#define INCLUDE_STRUCTURE_H_

#include <cstdlib>
#include <cstddef>
#include <string>
#include "settings.h"
#include "log.h"
#include "locked_unordered_map.h"

typedef long long Size;
typedef unsigned char byte;

#define DEFAULT_SPLIT_CHAR ':'

#define ALLOCATOR_ALREADY_EXIST_EXCEPTION 1
#define ALLOCATOR_NOT_EXIST_EXECEPTION 2

typedef uint64_t ptr_t;

typedef uint64_t Key;
typedef uint64_t GAddr;
#define OFF_MASK 0xFFFFFFFFFFFFL
#define WID(gaddr) ((gaddr) >> 48)
#define OFF(gaddr) ((gaddr) & OFF_MASK)
#define TO_GLOB(addr, base, wid) ((ptr_t)(addr) - (ptr_t)(base) + ((ptr_t)(wid) << 48))
#define EMPTY_GLOB(wid) ((ptr_t)(wid) << 48)

#define GADD(addr, off) ((addr)+(off)) //for now, we don't have any check for address overflow
#define GMINUS(a, b) ((a)-(b)) //need to guarantee WID(a) == WID(b)
#define TOBLOCK(x) (((ptr_t)x) & BLOCK_MASK)
#define BLOCK_ALIGNED(x) (!((x) & ~BLOCK_MASK))
#define BADD(addr, i) TOBLOCK((addr) + (i)*BLOCK_SIZE) //return an addr
#define BMINUS(i, j) (((i)-(j))>>BLOCK_POWER)
#define TO_LOCAL(gaddr, base)  (void*)(OFF(gaddr) + (ptr_t)(base))
#define Gnullptr 0

struct Conf {
  bool is_master = true;  //mark whether current process is the master (obtained from conf and the current ip)
  int master_port = 12345;
  std::string master_ip = "localhost";
  std::string master_bindaddr;
  int worker_port = 12346;
  std::string worker_bindaddr;
  std::string worker_ip = "localhost";

  /***********************************/
  /******** MY CODE STARTS ********/
  Size size = 1024 * 1024L * 1024 * 10;  //per-server size of memory pre-allocated
  /******** MY CODE ENDS ********/
  /***********************************/

  Size ghost_th = 1024 * 1024;
  double cache_th = 0.15;  //if free mem is below this threshold, we start to allocate memory from remote nodes
  int unsynced_th = 1;
  double factor = 1.25;
  int maxclients = 1024;
  int maxthreads = 10;
  int backlog = TCP_BACKLOG;
  int loglevel = LOG_WARNING;
  std::string* logfile = nullptr;
  int timeout = 10;  //ms
  int eviction_period = 100;  //ms
};

/***********************************/
/******** MY CODE STARTS ********/
#include <chrono>
#include <vector>
#include <set>
class agent_stats{
  private:
    vector<std::chrono::time_point<std::chrono::system_clock>> starting_point_4_app_thread;
    vector<std::chrono::time_point<std::chrono::system_clock>> ending_point_4_app_thread;
    vector<string> time_tag_4_app_thread;
    vector<std::chrono::time_point<std::chrono::system_clock>> starting_point_4_sys_thread;
    vector<std::chrono::time_point<std::chrono::system_clock>> ending_point_4_sys_thread;
    vector<string> time_tag_4_sys_thread;
    vector<std::chrono::time_point<std::chrono::system_clock>> starting_point_4_debug;
    vector<std::chrono::time_point<std::chrono::system_clock>> ending_point_4_debug;
    vector<string> time_tag_4_debug;
    int start_flag;
    std::mutex myMutex;
    double valid_ratio = 0.5;

  public:
    int byte_len = 0;
    void dont_start(){
      start_flag = 0;
    }
    void start(){
      start_flag = 1;
    }

    void add_starting_point_4at(){
      if(start_flag){
        starting_point_4_app_thread.push_back(std::chrono::system_clock::now());
      }
    }
    void del_starting_point_4at(){
      if(start_flag){
        starting_point_4_app_thread.pop_back();
      }
    }
    void add_ending_point_4at(const string& tag){
      if(start_flag){
        ending_point_4_app_thread.push_back(std::chrono::system_clock::now());
        time_tag_4_app_thread.emplace_back(tag);
      }
    }
    void add_time_tag_4at(string tag){
      if(start_flag){
        time_tag_4_app_thread.push_back(tag);
      }
    }
    void add_ending_point_4at(){
      if(start_flag){
        ending_point_4_app_thread.push_back(std::chrono::system_clock::now());
      } 
    }


    void add_starting_point_4st(){
      if(start_flag){
        starting_point_4_sys_thread.push_back(std::chrono::system_clock::now());
      }
    }
    void del_starting_point_4st(){
      if(start_flag){
        starting_point_4_sys_thread.pop_back();
      }
    }
    void add_ending_point_4st(const string& tag){
      if(start_flag){
        ending_point_4_sys_thread.push_back(std::chrono::system_clock::now());
        time_tag_4_sys_thread.emplace_back(tag);
      }
    }
    void add_time_tag_4st(string tag){
      if(start_flag){
        time_tag_4_sys_thread.push_back(tag);
      }
    }
    void add_ending_point_4st(){
      if(start_flag){
        ending_point_4_sys_thread.push_back(std::chrono::system_clock::now());
      } 
    }

    
    void add_starting_point_4debug(){
      if(start_flag) starting_point_4_debug.push_back(std::chrono::system_clock::now());
    }
    void add_ending_point_4debug(const string& tag){
      if(start_flag){
        ending_point_4_debug.push_back(std::chrono::system_clock::now());
        time_tag_4_debug.emplace_back(tag);
      }
    }
    void add_time_tag_4debug(string tag){
      if(start_flag) time_tag_4_debug.push_back(tag);
    }
    void add_ending_point_4debug(){
      if(start_flag) ending_point_4_debug.push_back(std::chrono::system_clock::now());
    }



    void print_stats(vector<string>& time_tag, 
      vector<std::chrono::time_point<std::chrono::system_clock>> starting_point,
      vector<std::chrono::time_point<std::chrono::system_clock>> ending_point){
      if(time_tag.size() > 0){
        std::set<std::string> unique_tag(time_tag.begin(), time_tag.end());
        vector<double> sum_time_duration(unique_tag.size());
        vector<int> count_time_duration(unique_tag.size());

        // printf("checkpoint 0: size of time_tag = %d\n", time_tag.size());
        // printf("checkpoint 0: size of starting_point = %d\n", starting_point.size());
        // printf("checkpoint 0: size of ending_point = %d\n", ending_point.size());

        for(int i = 0; i < time_tag.size(); i++){
          auto duration = std::chrono::duration_cast<std::chrono::microseconds>(ending_point[i] - starting_point[i]);
          auto duration_us = double(duration.count());

          // printf("checkpoint 1: i = %d, duration_us = %lf, time_tag = %s\n", i, duration_us, time_tag[i].c_str());

          auto it = std::find(unique_tag.begin(), unique_tag.end(), time_tag[i]);
          auto tag_index = std::distance(unique_tag.begin(), it);
          sum_time_duration[tag_index] += duration_us;
          count_time_duration[tag_index] += 1;
        }

        int i = 0;
        for (const auto& element : unique_tag) {
            printf("%lf; %s: num = %d, AVG latency = %lf us\n", sum_time_duration[i] / count_time_duration[i], element.c_str(), count_time_duration[i], sum_time_duration[i] / count_time_duration[i]);
            i++;
        }
        printf("\n");
      }
      else{
        printf("No duration\n");
        printf("\n");
      }
    }

    void print_valid_stats(vector<string>& time_tag, 
      vector<std::chrono::time_point<std::chrono::system_clock>> starting_point,
      vector<std::chrono::time_point<std::chrono::system_clock>> ending_point){

      int bias = time_tag.size() * (1 - valid_ratio);
      if(time_tag.size() > 0 && bias < time_tag.size()){
        std::set<std::string> unique_tag(time_tag.begin(), time_tag.end());
        vector<double> sum_time_duration(unique_tag.size());
        vector<int> count_time_duration(unique_tag.size());

        for(int i = bias; i < time_tag.size(); i++){
          auto duration = std::chrono::duration_cast<std::chrono::microseconds>(ending_point[i] - starting_point[i]);
          auto duration_us = double(duration.count());

          // printf("checkpoint 1: i = %d, duration_us = %lf, time_tag = %s\n", i, duration_us, time_tag[i].c_str());

          auto it = std::find(unique_tag.begin(), unique_tag.end(), time_tag[i]);
          auto tag_index = std::distance(unique_tag.begin(), it);
          sum_time_duration[tag_index] += duration_us;
          count_time_duration[tag_index] += 1;
        }

        int i = 0;
        for (const auto& element : unique_tag) {
            printf("%lf; %s: num = %d, AVG latency = %lf us\n", sum_time_duration[i] / count_time_duration[i], element.c_str(), count_time_duration[i], sum_time_duration[i] / count_time_duration[i]);
            i++;
        }
        printf("\n");
      }
      else{
        printf("No duration\n");
        printf("\n");
      }
    }

    void print_agent_stats(){
      // printf("app_thread_duration:\n");
      // print_stats(time_tag_4_app_thread, starting_point_4_app_thread, ending_point_4_app_thread);
      // printf("sys_thread_duration:\n");
      // print_stats(time_tag_4_sys_thread, starting_point_4_sys_thread, ending_point_4_sys_thread);
      // printf("debug_agent_duration:\n");
      // print_stats(time_tag_4_debug, starting_point_4_debug, ending_point_4_debug);
      printf("app_thread_duration (valid_ratio=%lf):\n", valid_ratio);
      print_valid_stats(time_tag_4_app_thread, starting_point_4_app_thread, ending_point_4_app_thread);
      printf("sys_thread_duration (valid_ratio=%lf):\n", valid_ratio);
      print_valid_stats(time_tag_4_sys_thread, starting_point_4_sys_thread, ending_point_4_sys_thread);
      printf("debug_agent_duration (valid_ratio=%lf):\n", valid_ratio);
      print_valid_stats(time_tag_4_debug, starting_point_4_debug, ending_point_4_debug);
    }
}
extern agent_stats_inst;

struct userop_stats{
  double userop_latency_us;
  string userop_type;
  string cache_coherence_type;
};
class my_stats{
  private:
    std::mutex myMutex;
    vector<vector<userop_stats>> allthread_stats;
  public:
    int get_num_of_existing_threads(){
      return allthread_stats.size();
    }
    void add_onethread_stats(vector<userop_stats> onethread_stats){
      lock_guard<mutex> lock(myMutex);
      allthread_stats.push_back(onethread_stats);
    }

    double calculate_average_latency(vector<double>& latency){
      double latency_sum = 0;
      for(int i = 0; i < latency.size(); i++){
        latency_sum += latency[i];
      }
      return latency_sum / latency.size();
    }

    double calculate_P50_latency(vector<double>& latency){
      int P50_index = latency.size() * 0.5;
      return latency[P50_index];
    }

    double calculate_P99_latency(vector<double>& latency){
      int P99_index = latency.size() * 0.99;
      return latency[P99_index];
    }

    void print_userop_stats(){
      lock_guard<mutex> lock(myMutex);
      
      // print average latency, p50 latency, p99 latency for
      // (WL+W,NO-CC), (WL+W,CC), (RL+R,NO-CC), (RL+R,CC)
      vector<double> RLR_NOCC_latency;
      vector<double> RLR_CC_latency;
      vector<double> WLW_NOCC_latency;
      vector<double> WLW_CC_latency;

      vector<double> R_NOCC_latency;
      vector<double> R_CC_latency;

      int thread_num = allthread_stats.size();
      for(int i = 0; i < thread_num; i++){
        int latency_num = allthread_stats[i].size();
        for(int j = 0; j < latency_num; j++){
          userop_stats userop_stats_inst = allthread_stats[i][j];
          if(userop_stats_inst.userop_type == "RL+R"){
            if(userop_stats_inst.cache_coherence_type == "NO-CC"){
              RLR_NOCC_latency.push_back(userop_stats_inst.userop_latency_us);
            }
            else{
              RLR_CC_latency.push_back(userop_stats_inst.userop_latency_us);
            }
          }
          else if(userop_stats_inst.userop_type == "WL+W"){
            if(userop_stats_inst.cache_coherence_type == "NO-CC"){
              WLW_NOCC_latency.push_back(userop_stats_inst.userop_latency_us);
            }
            else{
              WLW_CC_latency.push_back(userop_stats_inst.userop_latency_us);
            }
          }

          else if(userop_stats_inst.userop_type == "R"){
            if(userop_stats_inst.cache_coherence_type == "NO-CC"){
              R_NOCC_latency.push_back(userop_stats_inst.userop_latency_us);
            }
            else{
              R_CC_latency.push_back(userop_stats_inst.userop_latency_us);
            }
          }
        }
      }
      

      sort(RLR_NOCC_latency.begin(), RLR_NOCC_latency.end());
      sort(RLR_CC_latency.begin(), RLR_CC_latency.end());
      sort(WLW_NOCC_latency.begin(), WLW_NOCC_latency.end());
      sort(WLW_CC_latency.begin(), WLW_CC_latency.end());

      sort(R_NOCC_latency.begin(), R_NOCC_latency.end());
      sort(R_CC_latency.begin(), R_CC_latency.end());

      if(RLR_NOCC_latency.size() > 0){
        printf("RLR_NOCC_latency:num=%d\tAVG=%lf\tP50=%lf\tP99=%lf\n", 
        static_cast<int>(RLR_NOCC_latency.size()),
        calculate_average_latency(RLR_NOCC_latency),
        calculate_P50_latency(RLR_NOCC_latency),
        calculate_P99_latency(RLR_NOCC_latency));
      }
      else{
        printf("NO RLR_NOCC_latency\n");
      }

      if(WLW_NOCC_latency.size() > 0){
        printf("WLW_NOCC_latency:num=%d\tAVG=%lf\tP50=%lf\tP99=%lf\n", 
        static_cast<int>(WLW_NOCC_latency.size()),
        calculate_average_latency(WLW_NOCC_latency),
        calculate_P50_latency(WLW_NOCC_latency),
        calculate_P99_latency(WLW_NOCC_latency));
      }
      else{
        printf("NO WLW_NOCC_latency\n");
      }

      if(R_NOCC_latency.size() > 0){
        printf("R_NOCC_latency:num=%d\tAVG=%lf\tP50=%lf\tP99=%lf\n", 
        static_cast<int>(R_NOCC_latency.size()),
        calculate_average_latency(R_NOCC_latency),
        calculate_P50_latency(R_NOCC_latency),
        calculate_P99_latency(R_NOCC_latency));
      }
      else{
        printf("NO R_NOCC_latency\n");
      }

      if(RLR_CC_latency.size() > 0){
        printf("RLR_CC_latency:num=%d\tAVG=%lf\tP50=%lf\tP99=%lf\n", 
        static_cast<int>(RLR_CC_latency.size()),
        calculate_average_latency(RLR_CC_latency),
        calculate_P50_latency(RLR_CC_latency),
        calculate_P99_latency(RLR_CC_latency));
        printf("last RLR_CC_latency:%lf\n", RLR_CC_latency[RLR_CC_latency.size() - 1]);
      }
      else{
        printf("NO RLR_CC_latency\n");
      }

      if(WLW_CC_latency.size() > 0){
        printf("WLW_CC_latency:num=%d\tAVG=%lf\tP50=%lf\tP99=%lf\n",
        static_cast<int>(WLW_CC_latency.size()), 
        calculate_average_latency(WLW_CC_latency),
        calculate_P50_latency(WLW_CC_latency),
        calculate_P99_latency(WLW_CC_latency));
        printf("last WLW_CC_latency:%lf\n", WLW_CC_latency[WLW_CC_latency.size() - 1]);
      }
      else{
        printf("NO WLW_CC_latency\n");
      }

      if(R_CC_latency.size() > 0){
        printf("R_CC_latency:num=%d\tAVG=%lf\tP50=%lf\tP99=%lf\n", 
        static_cast<int>(R_CC_latency.size()),
        calculate_average_latency(R_CC_latency),
        calculate_P50_latency(R_CC_latency),
        calculate_P99_latency(R_CC_latency));
      }
      else{
        printf("NO R_CC_latency\n");
      }
      printf("\n");
    }
    void print_agent_stats(){
      agent_stats_inst.print_agent_stats();
    }

};
/******** MY CODE ENDS ********/
/***********************************/

typedef int PostProcessFunc(int, void*);

#define LOCK_MICRO(table, key) do {((table).lock(key));} while(0)
#define UNLOCK_MICRO(table, key) ((table).unlock(key))

#endif /* INCLUDE_STRUCTURE_H_ */
