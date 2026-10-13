#include "../header/RequestorsQueues.h"

namespace octopus
{
    MCsim::RequestorsQueues *RequestorsQueues::s_requestors_queues = nullptr;

    MCsim::RequestorsQueues *RequestorsQueues::getRequestorsQueues()
    {
        return s_requestors_queues;
    }

    void RequestorsQueues::setRequestorsQueues(MCsim::RequestorsQueues *queues)
    {
        s_requestors_queues = queues;
    }
}
