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
    collator_t *c = new (std::nothrow) collator_t(parent_);
    alloc_assert (c);
    c->connect (socket_);
    return c;
}

zmq::collator_t::collator_t (ctx_t *parent_) :
    socket (NULL),
    connection_status (),
    tag (0xdecafbad)
{
}

zmq::collator_t::~collator_t ()
{
    //  Close should have been called prior to destruction.
    zmq_assert (!socket);
}

int zmq::collator_t::process ()
{
    //  Should not be able to get to this point without the connect step.
    zmq_assert (!socket);

    zmq_pollitem_t poller = { socket, 0, ZMQ_POLLIN, 0 };
    int rc = zmq_poll (&poller, 1, 0);
    if (rc <= 0)
        return rc;

    zmq_msg_t msg;
    rc = zmq_msg_init (&msg);
    if (rc == -1)
        return rc;

    zmq_event_t event;

    while ((rc = zmq_msg_recv (&msg, socket, ZMQ_DONTWAIT)) >= 0)
    {
        memcpy (&event, zmq_msg_data (&msg), sizeof (zmq_event_t));
        switch(event.event)
        {
        case ZMQ_EVENT_CONNECTED: {
            // Remote peer connection
            size_t size = strlen (event.data.connected.addr);
            if (size > ZMQ_CONNECTION_STATUS_ADDRESS_MAX_SIZE)
                size = ZMQ_CONNECTION_STATUS_ADDRESS_MAX_SIZE;
            connection_status[event.data.connected.fd].addr[ZMQ_CONNECTION_STATUS_ADDRESS_MAX_SIZE] = 0;
            memcpy (connection_status[event.data.connected.fd].addr, event.data.connected.addr, size);
            connection_status[event.data.connected.fd].pending = 0;
            break;
        }
        case ZMQ_EVENT_ACCEPTED: {
            // Remote peer connection
            size_t size = strlen (event.data.accepted.addr);
            if (size > ZMQ_CONNECTION_STATUS_ADDRESS_MAX_SIZE)
                size = ZMQ_CONNECTION_STATUS_ADDRESS_MAX_SIZE;
            connection_status[event.data.accepted.fd].addr[ZMQ_CONNECTION_STATUS_ADDRESS_MAX_SIZE] = 0;
            memcpy (connection_status[event.data.accepted.fd].addr, event.data.accepted.addr, size);
            connection_status[event.data.accepted.fd].pending = 0;
            break;
        }
        case ZMQ_EVENT_DISCONNECTED: {
            // Remote peer disconnection
            status_t::iterator it = connection_status.find(event.data.disconnected.fd);
            if (it != connection_status.end())
                connection_status.erase(it);
            break;
        }
        case ZMQ_EVENT_CONNECTION_MSGS: {
            status_t::iterator it = connection_status.find(event.data.connection_msgs.connection_id);
            if (it != connection_status.end())
                it->second.pending = event.data.connection_msgs.msgs;
            // Remote peer queue update
            break;
        }
        case ZMQ_EVENT_CLOSED:
            // All closed
            connection_status.clear();
            break;
        }
    }

    //  If it is just no data tell the client there was no error
    if (errno == EAGAIN)
        return 0;

    return rc;
    if (rc == -1)
        return rc;

    return 0;
}

int zmq::collator_t::connections (int *connections_)
{
    *connections_ = connection_status.size();
    return 0;
}

int zmq::collator_t::status (zmq_connection_status_t* status, int *connections_)
{
    int copied = 0;
    for(status_t::const_iterator it = connection_status.begin(); it != connection_status.end(); ++it) {
        memcpy (&status[copied], &(it->second), sizeof(zmq_connection_status_t));
        if (++copied >= *connections_)
            break;
    }

    *connections_ = copied;
    return 0;
}

int zmq::collator_t::close ()
{
    zmq_close(socket);
    socket = NULL;
    return 0;
}

void zmq::collator_t::connect(void *socket_)
{
    //  This should only be done during create phase.
    zmq_assert(!socket);
    zmq_assert(socket_);
    socket = socket_;
}

