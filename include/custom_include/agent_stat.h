#pragma once

#include <vector>
#include <set>
#include <unordered_set>
#include <unordered_map>
#include <atomic>
#include <infiniband/verbs.h>

#include "util.h"
#include "structure.h"
#include "histogram.h"
#include "atomic_queue/atomic_queue.h"


using GAddr = uint64_t;

class queue_entry {
public:
    ibv_wc wc;
    uint64_t starting_point; // created by rdtsc();
    uint64_t dummy;
};

using SPSC_QUEUE = atomic_queue::AtomicQueueB2<queue_entry, std::allocator<queue_entry>, true, false, true>;

enum class APP_THREAD_OP {
    NONE,
    AFTER_PROCESS_LOCAL_REQUEST,
    WAIT_ASYNC_FINISH,
    WAKEUP_2_READ_RETURN,
    WAKEUP_2_WRITE_RETURN,
    WAKEUP_2_RLOCK_RETURN,
    WAKEUP_2_WLOCK_RETURN,
    WAKEUP_2_UNLOCK_RETURN,
    MEMSET,
};

enum class SYS_THREAD_OP {
    NONE,
};

enum class MULTI_SYS_THREAD_OP {
    NONE,
    PROCESS_IN_HOME_NODE, // home node receive request node, and forward to cache node
    PROCESS_PENDING_IN_HOME_OR_REQ_NODE, // home node and request node receive rdma_write_with_imm response from cache node
    PROCESS_IN_CACHE_NODE, // process in cache node
};

enum class POLL_OP {
    NONE,
    WAITING_IN_SYSTHREAD_QUEUE,
};


extern __thread std::thread::id now_thread_id;

class agent_stats {
private:
    // only collect 1 special app thread stat
    Histogram *app_thread_stats;
    uint64_t app_thread_counter;
    std::unordered_map<APP_THREAD_OP, Histogram *> app_thread_op_stats;

    // maybe more than 1 sys thread, need use atomic op
    // I don't know why we need sys_thread_stats and multi_sys_thread_stats now(2024/3/5)
    Histogram *sys_thread_stats;
    uint64_t sys_thread_counter;
    std::unordered_map<SYS_THREAD_OP, Histogram *> sys_thread_op_stats;

    // used for multi sys thread situation
    Histogram *multi_sys_thread_stats[MAX_SYS_THREAD];
    uint64_t multi_sys_thread_counter[MAX_SYS_THREAD];
    std::unordered_map<MULTI_SYS_THREAD_OP, Histogram *> multi_sys_thread_op_stats[MAX_SYS_THREAD];


    // only 1 sys thread to poll rdma CQ
    Histogram *poll_thread_stats;
    std::unordered_map<POLL_OP, Histogram *> poll_thread_op_stats;


    std::unordered_set <GAddr> valid_gaddrs;

    std::atomic<int> is_start;

public:

    uint64_t sys_thread_num = 1;
    std::thread::id app_thread_target_thread_id;
    std::thread::id poll_thread_target_thread_id;
    explicit agent_stats() {
        // TODO
        app_thread_stats = new Histogram(1, 1000000, 3, 10);
        app_thread_op_stats[APP_THREAD_OP::AFTER_PROCESS_LOCAL_REQUEST] = new Histogram(1, 1000000, 3, 10);
        app_thread_op_stats[APP_THREAD_OP::WAIT_ASYNC_FINISH] = new Histogram(1, 1000000, 3, 10);
        app_thread_op_stats[APP_THREAD_OP::WAKEUP_2_READ_RETURN] = new Histogram(1, 1000000, 3, 10);
        app_thread_op_stats[APP_THREAD_OP::WAKEUP_2_WRITE_RETURN] = new Histogram(1, 1000000, 3, 10);
        app_thread_op_stats[APP_THREAD_OP::WAKEUP_2_RLOCK_RETURN] = new Histogram(1, 1000000, 3, 10);
        app_thread_op_stats[APP_THREAD_OP::WAKEUP_2_WLOCK_RETURN] = new Histogram(1, 1000000, 3, 10);
        app_thread_op_stats[APP_THREAD_OP::WAKEUP_2_UNLOCK_RETURN] = new Histogram(1, 1000000, 3, 10);
        app_thread_op_stats[APP_THREAD_OP::MEMSET] = new Histogram(1, 1000000, 3, 10);

        sys_thread_stats = new Histogram(1, 1000000, 3, 10);

        for (int i = 0; i < MAX_SYS_THREAD; i++) {
            multi_sys_thread_stats[i] = new Histogram(1, 1000000, 3, 10);
            multi_sys_thread_op_stats[i][MULTI_SYS_THREAD_OP::PROCESS_IN_HOME_NODE] = new Histogram(1, 1000000, 3, 10);
            multi_sys_thread_op_stats[i][MULTI_SYS_THREAD_OP::PROCESS_PENDING_IN_HOME_OR_REQ_NODE] = new Histogram(1, 1000000, 3, 10);
            multi_sys_thread_op_stats[i][MULTI_SYS_THREAD_OP::PROCESS_IN_CACHE_NODE] = new Histogram(1, 1000000, 3, 10);
        }

        poll_thread_stats = new Histogram(1, 1000000, 3, 10);
        poll_thread_op_stats[POLL_OP::WAITING_IN_SYSTHREAD_QUEUE] = new Histogram(1, 1000000, 3, 10);
    }

    ~agent_stats() {
        // TODO
    }

    void print_app_thread_stat() {
        std::cout << "\nAFTER_PROCESS_LOCAL_REQUEST: " << std::endl;
        app_thread_op_stats[APP_THREAD_OP::AFTER_PROCESS_LOCAL_REQUEST]->print(stdout, 5);
        std::cout << "\nWAIT_ASYNC_FINISH: " << std::endl;
        app_thread_op_stats[APP_THREAD_OP::WAIT_ASYNC_FINISH]->print(stdout, 5);
        std::cout << "\nWAKEUP_2_READ_RETURN: " << std::endl;
        app_thread_op_stats[APP_THREAD_OP::WAKEUP_2_READ_RETURN]->print(stdout, 5);
        std::cout << "\nWAKEUP_2_WRITE_RETURN: " << std::endl;
        app_thread_op_stats[APP_THREAD_OP::WAKEUP_2_WRITE_RETURN]->print(stdout, 5);
        std::cout << "\nWAKEUP_2_RLOCK_RETURN: " << std::endl;
        app_thread_op_stats[APP_THREAD_OP::WAKEUP_2_RLOCK_RETURN]->print(stdout, 5);
        std::cout << "\nWAKEUP_2_WLOCK_RETURN: " << std::endl;
        app_thread_op_stats[APP_THREAD_OP::WAKEUP_2_WLOCK_RETURN]->print(stdout, 5);
        std::cout << "\nWAKEUP_2_UNLOCK_RETURN: " << std::endl;
        app_thread_op_stats[APP_THREAD_OP::WAKEUP_2_UNLOCK_RETURN]->print(stdout, 5);
        std::cout << "\nMEMSET: " << std::endl;
        app_thread_op_stats[APP_THREAD_OP::MEMSET]->print(stdout, 5);
    }

    void print_multi_sys_thread_stat() {
        //TODO
    }

    void print_poll_thread_stat() {
        std::cout << "\nWAITING_IN_SYSTHREAD_QUEUE: " << std::endl;
        poll_thread_op_stats[POLL_OP::WAITING_IN_SYSTHREAD_QUEUE]->print(stdout, 5);
    }

    bool is_valid_gaddr(GAddr gaddr) {
        return valid_gaddrs.count(gaddr);
    }

    void push_valid_gaddr(GAddr gaddr) {
        valid_gaddrs.insert(gaddr);
    }

    void pop_valid_gaddr(GAddr gaddr) {
        valid_gaddrs.erase(gaddr);
    }

    void start_collection() {
        is_start = 1;
    }

    void end_collection() {
        is_start = 0;
    }

    inline void start_record_app_thread(GAddr gaddr) {
        if (is_valid_gaddr(gaddr) && is_start) {
            app_thread_counter = rdtsc();
        }
    }
    inline void stop_record_app_thread_with_op(GAddr gaddr, APP_THREAD_OP op = APP_THREAD_OP::NONE) {
        if (is_valid_gaddr(gaddr) && is_start) {
            uint64_t ns = rdtscp() - app_thread_counter;
            if (op == APP_THREAD_OP::NONE) {
                app_thread_stats->record(ns);
            } else {
                app_thread_op_stats[op]->record(ns);
            }
        }
    }

    inline void start_record_sys_thread(GAddr gaddr) {
        if (is_valid_gaddr(gaddr) && is_start) {
            sys_thread_counter = rdtsc();
        }
    }
    inline void stop_record_sys_thread_with_op(GAddr gaddr, SYS_THREAD_OP op = SYS_THREAD_OP::NONE) {
        if (is_valid_gaddr(gaddr) && is_start) {
            uint64_t ns = rdtscp() - sys_thread_counter;
            if (op == SYS_THREAD_OP::NONE) {
                sys_thread_stats->record_atomic(ns);
            } else {
                sys_thread_op_stats[op]->record_atomic(ns);
            }
        }
    }

    inline void start_record_multi_sys_thread(uint64_t thread_id) {
        multi_sys_thread_counter[thread_id] = rdtsc();
    }
    inline void stop_record_multi_sys_thread_with_op(uint64_t thread_id, MULTI_SYS_THREAD_OP op = MULTI_SYS_THREAD_OP::NONE) {
        uint64_t ns = rdtscp() - multi_sys_thread_counter[thread_id];
        if (op == MULTI_SYS_THREAD_OP::NONE) {
            multi_sys_thread_stats[thread_id]->record(ns);
        } else {
            multi_sys_thread_op_stats[thread_id][op]->record(ns);
        }
    }

    // TODO whether need add gaddr check?
    inline void record_poll_thread_with_op(uint64_t ns, POLL_OP op = POLL_OP::NONE) {
        if (op == POLL_OP::NONE) {
            poll_thread_stats->record(ns);
        } else {
            poll_thread_op_stats[op]->record(ns);
        }
    }

};

extern agent_stats agent_stats_inst;