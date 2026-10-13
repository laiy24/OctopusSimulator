#ifndef _RROF_ARBITER_H
#define _RROF_ARBITER_H

#include "Arbiter.h"
#include "../RequestorsQueues.h"

#include <unordered_map>

namespace octopus
{
    class RROFArbiter : public Arbiter
    {
    private:
        std::unordered_map<uint64_t, std::pair<int, int>> buffer_index;

    public:
        RROFArbiter(vector<int> *candidates_ids, int arbiter_period);
        ~RROFArbiter() override;

        bool elect(uint64_t cycle_number, vector<vector<Message> *> &buffers, Message *out_msg) override;
    };
}

#endif
