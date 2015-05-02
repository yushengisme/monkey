/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Monkey HTTP Server
 *  ==================
 *  Copyright 2001-2015 Monkey Software LLC <eduardo@monkey.io>
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <monkey/mk_list.h>
#include <monkey/mk_rbtree.h>
#include <monkey/mk_event.h>

#ifndef MK_SCHEDULER_H
#define MK_SCHEDULER_H

#define MK_SCHEDULER_CONN_AVAILABLE  -1
#define MK_SCHEDULER_CONN_PENDING     0
#define MK_SCHEDULER_CONN_PROCESS     1
#define MK_SCHEDULER_SIGNAL_DEADBEEF  0xDEADBEEF
#define MK_SCHEDULER_SIGNAL_FREE_ALL  0xFFEE0000

/*
 * Scheduler balancing mode:
 *
 * - Fair Balancing: use a single socket and upon accept
 *   new connections, lookup the less loaded thread and
 *   assign the socket to that specific epoll queue.
 *
 * - ReusePort: Use new Linux Kernel 3.9 feature that
 *   allows thread to share binded address on a lister
 *   socket. We let the Kernel to decide how to balance.
 */
#define MK_SCHEDULER_FAIR_BALANCING   0
#define MK_SCHEDULER_REUSEPORT        1

extern __thread struct rb_root *cs_list;
extern __thread struct mk_list *cs_incomplete;

struct mk_sched_conn
{
    struct mk_event event;

    int status;                  /* connection status          */
    time_t arrive_time;          /* arrived time               */
    struct mk_list _head;        /* list head: av/busy         */
    struct mk_list status_queue; /* link to the incoming queue */
    struct rb_node _rb_head; /* red-black tree head */
};

/* Global struct per worker: this is a structure that lives at
 * */
struct sched_list_node
{
    /* The event loop on this scheduler thread */
    struct mk_event_loop *loop;

    unsigned long long accepted_connections;
    unsigned long long closed_connections;
    unsigned long long over_capacity;

    /*
     * Red-Black tree queue to perform fast lookup over
     * the scheduler busy queue
     */
    struct rb_root rb_queue;

    /*
     * Available and busy queue: provides a fast lookup
     * for available and used slot connections
     */
    //struct mk_list busy_queue;
    //struct mk_list av_queue;

    /*
     * The incoming queue represents client connections that
     * have not initiated it requests or the request status
     * is incomplete. This linear lists allows the scheduler
     * to perform a fast check upon every timeout.
     */
    struct mk_list incoming_queue;

    short int idx;
    unsigned char initialized;

    pthread_t tid;
    pid_t pid;

    struct mk_http_session *request_handler;

    /*
     * This variable is used to signal the active workers,
     * just available because of ULONG_MAX bug described
     * on mk_scheduler.c .
     */
    int signal_channel_r;
    int signal_channel_w;
};

struct mk_sched_notif {
    struct mk_event event;
};

extern __thread struct mk_sched_notif  *worker_sched_notif;
extern __thread struct sched_list_node *worker_sched_node;

/* global scheduler list */
struct sched_list_node *sched_list;

/* Struct under thread context */
typedef struct
{
} sched_thread_conf;

extern pthread_mutex_t mutex_worker_init;
extern pthread_mutex_t mutex_worker_exit;
pthread_mutex_t mutex_port_init;

struct sched_list_node *mk_sched_next_target();
void mk_sched_init();
int mk_sched_launch_thread(int max_events, pthread_t *tout);
void *mk_sched_launch_epoll_loop(void *thread_conf);
struct sched_list_node *mk_sched_get_handler_owner(void);

static inline struct rb_root *mk_sched_get_request_list()
{
    return cs_list;
}

static inline struct sched_list_node *mk_sched_get_thread_conf()
{
    return worker_sched_node;
}

void mk_sched_update_thread_status(struct sched_list_node *sched,
                                   int active, int closed);

int mk_sched_drop_connection(int socket);
int mk_sched_check_timeouts(struct sched_list_node *sched);
struct mk_sched_conn *mk_sched_add_connection(int remote_fd,
                                              struct sched_list_node *sched);
int mk_sched_remove_client(struct sched_list_node *sched, int remote_fd);
struct mk_sched_conn *mk_sched_get_connection(struct sched_list_node
                                                     *sched, int remote_fd);
int mk_sched_update_conn_status(struct sched_list_node *sched, int remote_fd,
                                int status);
int mk_sched_sync_counters();
void mk_sched_worker_free();

/* Event handlers */
int mk_sched_event_read(struct mk_sched_conn *conn,
                        struct sched_list_node *sched);
int mk_sched_event_write(struct mk_sched_conn *conn,
                         struct sched_list_node *sched);
int mk_sched_event_close(int socket, int event);

#endif
