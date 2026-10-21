#include "../../header/Arbiters/RROFArbiter.h"

#include <limits>

namespace octopus
{
    RROFArbiter::RROFArbiter(vector<int> *candidates_ids, int arbiter_period)
        : Arbiter(candidates_ids, arbiter_period)
    {
    }

    RROFArbiter::~RROFArbiter()
    {
    }

    bool RROFArbiter::elect(uint64_t, vector<vector<Message> *> &buffers, Message *out_msg)
    {
        size_t message_count = 0;
        for (vector<Message> *buffer : buffers)
            message_count += buffer->size();
        if (message_count == 0)
            return false;

        // Clear existing entries so the index contains strictly the messages currently
        // in buffers. Reusing the persistent member preserves the allocated bucket array,
        // eliminating bucket reallocations while guaranteeing exact original election behavior.
        buffer_index.clear();
        if (buffer_index.bucket_count() < message_count)
            buffer_index.reserve(message_count);
        for (int buffer_index_value = 0; buffer_index_value < (int)buffers.size(); ++buffer_index_value)
        {
            for (int message_index = 0; message_index < (int)buffers[buffer_index_value]->size(); ++message_index)
            {
                buffer_index[(*buffers[buffer_index_value])[message_index].msg_id] =
                    make_pair(buffer_index_value, message_index);
            }
        }

        MCsim::RequestorsQueues *requestors_queues = RequestorsQueues::getRequestorsQueues();
        if (requestors_queues != nullptr)
        {
            unsigned int requestor_count = requestors_queues->getRRQueueSize();
            for (unsigned int slot = 0; ; ++slot)
            {
                bool has_live_requestor = false;
                for (unsigned int requestor_index = 0; requestor_index < requestor_count; ++requestor_index)
                {
                    unsigned int requestor_id = requestors_queues->getRRCore(requestor_index);
                    if (slot >= requestors_queues->getRequestorSize(requestor_id))
                        continue;

                    has_live_requestor = true;
                    unsigned int original_owner = 0;
                    uint64_t request_id = requestors_queues->getRequest(requestor_id, slot, &original_owner);
                    auto it = buffer_index.find(request_id);
                    if (it == buffer_index.end())
                        continue;

                    vector<Message> *buffer = buffers[it->second.first];
                    out_msg->copy(buffer->at(it->second.second));
                    buffer->erase(buffer->begin() + it->second.second);
                    return true;
                }
                if (!has_live_requestor)
                    break;
            }
        }

        // A cache hit or a request that has not reached MCsim yet has no
        // RequestorsQueues entry. Do not deadlock that traffic; use oldest-first
        // until the shared memory queue can identify it.
        int best_buffer = -1;
        int best_message = -1;
        uint64_t best_cycle = numeric_limits<uint64_t>::max();
        for (int i = 0; i < (int)buffers.size(); ++i)
        {
            for (int j = 0; j < (int)buffers[i]->size(); ++j)
            {
                if ((*buffers[i])[j].cycle < best_cycle)
                {
                    best_buffer = i;
                    best_message = j;
                    best_cycle = (*buffers[i])[j].cycle;
                }
            }
        }
        if (best_buffer == -1)
            return false;

        out_msg->copy(buffers[best_buffer]->at(best_message));
        buffers[best_buffer]->erase(buffers[best_buffer]->begin() + best_message);
        return true;
    }
}
