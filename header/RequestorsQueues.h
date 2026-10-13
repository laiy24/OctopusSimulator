#ifndef OCTOPUS_REQUESTORS_QUEUES_H
#define OCTOPUS_REQUESTORS_QUEUES_H

#include "MCsim/MCsim.h"

namespace octopus
{
    // Bridge the cache/interconnect side to the RequestorsQueues instance owned by
    // the active MCsim memory system. The pointer is registered after MCsim is
    // constructed, before the clock starts.
    class RequestorsQueues
    {
    public:
        static MCsim::RequestorsQueues *getRequestorsQueues();
        static void setRequestorsQueues(MCsim::RequestorsQueues *queues);

    private:
        static MCsim::RequestorsQueues *s_requestors_queues;
    };
}

#endif
