/*
 * File  :      PMSIAsteriskProtocol.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Nov 28, 2021
 */

#include "../../header/Protocols/PMSIAsteriskProtocol.h"
#include "../../header/AddrMapping.h"
using namespace std;

namespace octopus
{
    PMSIAsteriskProtocol::PMSIAsteriskProtocol(CacheDataHandler *cache, const string &fsm_path, int id, int sharedMemId) : MSIProtocol(cache, fsm_path, id, sharedMemId)
    {
    }

    PMSIAsteriskProtocol::~PMSIAsteriskProtocol()
    {
    }

    void PMSIAsteriskProtocol::readEvent(Message &msg, MSIProtocol::EventId *out_id)
    {
        MSIProtocol::readEvent(msg, out_id);

        if (*out_id == MSIProtocol::EventId::OwnData)
        {
            bool from_llc = (!msg.to.empty()) &&
                            ((msg.to[0] == (uint16_t)this->m_shared_memory_id) ||
                             AddrMapping::getAddrMapping()->isLLCId(msg.to[0]));
            *out_id = (MSIProtocol::EventId)(from_llc ? EventId::RDM : EventId::RDC);
        }
    }
}