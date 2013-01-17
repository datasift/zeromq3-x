/*
    Copyright (c) 2009-2011 250bpm s.r.o.
    Copyright (c) 2007-2009 iMatix Corporation
    Copyright (c) 2011 VMware, Inc.
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

#include <new>
#include <string>
#include <sstream>
#include <algorithm>

#include "platform.hpp"

#if defined ZMQ_HAVE_WINDOWS
#include "windows.hpp"
#if defined _MSC_VER
#if defined WINCE
#include <cmnintrin.h>
#else
#include <intrin.h>
#endif
#endif
#else
#include <unistd.h>
#endif

#include "ctx.hpp"
#include "likely.hpp"
#include "msg.hpp"
#include "collator.hpp"
#include "socket_base.hpp"

#ifdef ZMQ_HAVE_OPENPGM
#include "pgm_socket.hpp"
#endif

bool zmq::collator_t::check_tag ()
{
    return tag == 0xdecafbad;
}

zmq::collator_t *zmq::collator_t::create (class ctx_t *parent_,
    socket_base_t *socket_)
{
    std::stringstream address;
    address << "inproc://monitor-" << socket_->get_tid();

    int rc = zmq_socket_monitor (socket_, address.str().c_str(), ZMQ_EVENT_CONNECTED |
            ZMQ_EVENT_ACCEPTED | ZMQ_EVENT_DISCONNECTED |
            ZMQ_EVENT_CONNECTION_MSGS | ZMQ_EVENT_CLOSED);
    if (rc == -1)
        return NULL;

    void *monitor = zmq_socket(parent_, ZMQ_PAIR);
    alloc_assert(monitor);

    rc = zmq_connect(monitor, address.str().c_str());
    if (rc == -1) {
        zmq_socket_monitor (socket_, NULL, 0);
        return NULL;
    }

    int linger = 0;
    rc = zmq_setsockopt (monitor, ZMQ_LINGER, &linger, sizeof (linger));
    if (rc == -1) {
        zmq_socket_monitor (socket_, NULL, 0);
        return NULL;
    }

    collator_t *c = new (std::nothrow) collator_t(parent_, socket_);
    alloc_assert (c);
    c->connect (monitor);
    return c;
}

zmq::collator_t::collator_t (ctx_t *parent_, void *socket_) :
    socket (socket_),
    monitor (NULL),
    connection_status (),
    tag (0xdecafbad)
{
}

zmq::collator_t::~collator_t ()
{
    //  Close should have been called prior to destruction.
    zmq_assert (!monitor);
}

int zmq::collator_t::process ()
{
    //  Should not be able to get to this point without the connect step.
    zmq_assert (monitor);

    zmq_pollitem_t poller = { monitor, 0, ZMQ_POLLIN, 0 };
    int rc = zmq_poll (&poller, 1, 0);
    if (rc <= 0)
        return rc;

    zmq_msg_t msg;
    rc = zmq_msg_init (&msg);
    if (rc == -1)
        return rc;

    zmq_event_t event;
    int events = 0;

    while ((rc = zmq_msg_recv (&msg, monitor, ZMQ_DONTWAIT)) >= 0)
    {
        ++events;
        memcpy (&event, zmq_msg_data (&msg), sizeof (zmq_event_t));
        switch(event.event)
        {
        case ZMQ_EVENT_CONNECTED:
            // Remote peer connection
            event_connected(event.data.connected.fd, event.data.connected.peer_addr);
            break;
        case ZMQ_EVENT_ACCEPTED:
            // Remote peer connection
            event_connected(event.data.accepted.fd, event.data.accepted.peer_addr);
            break;
        case ZMQ_EVENT_DISCONNECTED:
            // Remote peer disconnection
            event_disconnected(event.data.disconnected.fd);
            break;
        case ZMQ_EVENT_CONNECTION_MSGS:
            // Remote peer queue update
            event_update_msgs(event.data.connection_msgs.connection_id, event.data.connection_msgs.msgs);
            break;
        case ZMQ_EVENT_CLOSED:
            // All closed
            connection_status.clear();
            break;
        }
    }

    //  If it is just no more data tell the client how much we processed
    if (errno == EAGAIN)
        return events;

    return rc;
}

void zmq::collator_t::connections (int *connections_)
{
    *connections_ = connection_status.size();
}

void zmq::collator_t::status (zmq_connection_status_t* status, int *connections_)
{
    int copied = 0;
    for(status_t::const_iterator it = connection_status.begin(); it != connection_status.end(); ++it) {
        memcpy (&status[copied], &(it->second), sizeof(zmq_connection_status_t));
        if (++copied >= *connections_)
            break;
    }

    *connections_ = copied;
}

void zmq::collator_t::close ()
{
    zmq_close (monitor);
    zmq_socket_monitor (socket, NULL, 0);
    monitor = NULL;
}

void zmq::collator_t::connect(void *monitor_)
{
    //  This should only be done during create phase.
    zmq_assert(!monitor);
    zmq_assert(monitor_);
    monitor = monitor_;
}

void zmq::collator_t::event_connected(int connection_id_, char const* addr_)
{
    size_t size = strlen (addr_);
    if (size > ZMQ_CONNECTION_STATUS_ADDRESS_MAX_SIZE)
        size = ZMQ_CONNECTION_STATUS_ADDRESS_MAX_SIZE;

    connection_status[connection_id_].connected = true;
    connection_status[connection_id_].addr[ZMQ_CONNECTION_STATUS_ADDRESS_MAX_SIZE] = 0;
    memcpy (connection_status[connection_id_].addr, addr_, size);
    connection_status[connection_id_].pending = 0;
}

void zmq::collator_t::event_disconnected(int connection_id_)
{
    status_t::iterator it = connection_status.find(connection_id_);
    if (it == connection_status.end())
        return;

    it->second.connected = false;
}

void zmq::collator_t::event_update_msgs(int connection_id_, size_t msgs_)
{
    status_t::iterator it = connection_status.find(connection_id_);
    if (it == connection_status.end())
        return;

    it->second.pending = msgs_;
}
