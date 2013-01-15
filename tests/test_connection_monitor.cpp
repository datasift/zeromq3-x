/*
    Copyright (c) 2007-2012 iMatix Corporation
    Copyright (c) 2011 250bpm s.r.o.
    Copyright (c) 2007-2011 Other contributors as noted in the AUTHORS file

    This file is part of 0MQ.

    0MQ is free software; you can redistribute it and/or modify it under
    the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    0MQ is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <string>

#include <pthread.h>
#include <unistd.h>

#include "../include/zmq.h"
#include "../include/zmq_utils.h"
#include "testutil.hpp"

int const message_count = 50000;

static bool alive = true;
static int pull_events = 0;
static int push_events = 0;

// REQ socket monitor thread
static void *push_connection_monitor (void *c)
{
    while(alive) {
        int rc = zmq_collator_process (c);
        assert (rc >= 0);

        if (rc == 0) {
            // pause briefly
            usleep (1000);
            continue;
        }

        push_events += rc;

        int connections;
        rc = zmq_collator_connections (c, &connections);
        assert (rc == 0);

        if (connections == 0)
            continue;

        assert (connections == 1);
        zmq_connection_status_t status[1];
        rc = zmq_collator_status (c, &status[0], &connections);
        assert (rc == 0);
        assert (connections == 1);
    }
    zmq_collator_close (c);
    return NULL;
}

// 2nd REQ socket monitor thread
static void *pull_connection_monitor (void *c)
{
    while(alive) {
        int rc = zmq_collator_process (c);
        assert (rc >= 0);

        if (rc == 0) {
            // pause briefly
            usleep (1000);
            continue;
        }

        pull_events += rc;

        int connections;
        rc = zmq_collator_connections (c, &connections);
        assert (rc >= 0);

        if (connections == 0)
            continue;

        assert (connections == 1);
        zmq_connection_status_t status[1];
        rc = zmq_collator_status (c, &status[0], &connections);
        assert (rc == 0);
        assert (connections == 1);
    }
    zmq_collator_close (c);
    return NULL;
}

static void *source (void *socket)
{
    for (int i = 0; i < message_count; ++i)
    {
        int rc = zmq_send (socket, "Hello World!", strlen("Hello World!"), 0);
        assert (rc >= 0);
    }

    int rc = zmq_close (socket);
    assert (rc == 0);

    return NULL;
}

static void *sink (void *socket)
{
    for (int i = 0; i < message_count; ++i)
    {
        zmq_msg_t msg;
        zmq_msg_init (&msg);
        int rc = zmq_msg_recv (&msg, socket, 0);
        assert (rc >= 0);
    }

    int rc = zmq_close (socket);
    assert (rc == 0);

    return NULL;
}

int main (void)
{
    fprintf (stderr, "test_connection_monitor running...\n");
    const char *addr = "tcp://127.0.0.1:5560";

    //  Create the infrastructure
    void *ctx = zmq_init (1);
    assert (ctx);

    // PUSH socket
    void *push = zmq_socket (ctx, ZMQ_PUSH);
    assert (push);

    void *collator1 = zmq_collator (ctx, push);
    assert (collator1);
    pthread_t push_monitor;
    int rc = pthread_create (&push_monitor, NULL, push_connection_monitor, collator1);
    assert (rc == 0);

    rc = zmq_bind (push, addr);
    assert (rc == 0);

    // PULL socket
    void *pull = zmq_socket (ctx, ZMQ_PULL);
    assert (pull);

    void *collator2 = zmq_collator (ctx, pull);
    assert (collator2);
    pthread_t pull_monitor;
    rc = pthread_create (&pull_monitor, NULL, pull_connection_monitor, collator2);
    assert (rc == 0);

    rc = zmq_connect (pull, addr);
    assert (rc == 0);

    pthread_t push_sender;
    rc = pthread_create (&push_sender, NULL, source, push);
    assert (rc == 0);

    // Start the sink late
    usleep(100000);

    pthread_t pull_reciever;
    rc = pthread_create (&pull_reciever, NULL, sink, pull);
    assert (rc == 0);

    void *result = NULL;
    rc = pthread_join (push_sender, &result);
    assert (rc == 0);
    assert (result == NULL);

    rc = pthread_join (pull_reciever, &result);
    assert (rc == 0);
    assert (result == NULL);

    // An unclean way of terminating the children
    usleep(100000);
    alive = false;

    rc = pthread_join (push_monitor, &result);
    assert (rc == 0);
    assert (result == NULL);

    rc = pthread_join (pull_monitor, &result);
    assert (rc == 0);
    assert (result == NULL);


    zmq_term (ctx);

    // Excepted connection stats
    assert (pull_events > 0);
    assert (push_events > 0);

    fprintf(stderr, "Generated %d push and %d pull events for %d messages\n", push_events, pull_events, message_count);

    pthread_exit (NULL);

    return 0 ;
}

