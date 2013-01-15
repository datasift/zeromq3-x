/*
    Copyright (c) 2007-2012 iMatix Corporation
    Copyright (c) 2009-2011 250bpm s.r.o.
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

#ifndef __ZMQ_COLLATOR_HPP_INCLUDED__
#define __ZMQ_COLLATOR_HPP_INCLUDED__

#include <map>
#include <string>

#include "err.hpp"
#include "stdint.hpp"

namespace zmq
{
    class ctx_t;
    class socket_base_t;

    // TODO: Rework so this item can be added to the normal zmq_poll command
    //       as though it was a socket/fd. It pretty much is a socket wrapper
    //       anyway so it shouldn't be overly hard.
    class collator_t
    {

    public:
        virtual ~collator_t ();

        //  Returns false if object is not a socket.
        bool check_tag ();

        //  Create a socket of a specified type.
        static collator_t *create (zmq::ctx_t *parent_,
            zmq::socket_base_t *socket_);

        //  Interface for communication with the API layer.
        int process ();
        void connections (int *connections_);
        void status (zmq_connection_status_t* status, int *connections_);
        void close ();

    private:
        collator_t (zmq::ctx_t *parent_, void *socket);

        void connect(void *monitor_);
        void event_connected(int connection_id_, char const* addr_);
        void event_disconnected(int connection_id_);
        void event_update_msgs(int connection_id_, size_t msgs_);

        void *socket;
        void *monitor;

        //  Map of open connections.
        typedef std::map <int, zmq_connection_status_t> status_t;
        status_t connection_status;

        //  Used to check whether the object is a collection.
        uint32_t tag;

        //  No copy
        collator_t (const collator_t&);
        const collator_t &operator = (const collator_t&);
    };

}

#endif

