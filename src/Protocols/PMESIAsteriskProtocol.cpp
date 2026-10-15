/*
 * File  :      PMESIAsteriskProtocol.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Nov 28, 2021
 */

#include "../../header/Protocols/PMESIAsteriskProtocol.h"
#include "../../header/AddrMapping.h"
using namespace std;

namespace octopus
{
    PMESIAsteriskProtocol::PMESIAsteriskProtocol(CacheDataHandler *cache, const string &fsm_path, int id, int sharedMemId) : MESIProtocol(cache, fsm_path, id, sharedMemId)
    {
    }

    PMESIAsteriskProtocol::~PMESIAsteriskProtocol()
    {
    }

    void PMESIAsteriskProtocol::readEvent(Message &msg, MSIProtocol::EventId *out_id)
    {
        MESIProtocol::readEvent(msg, out_id);

        bool from_llc = (!msg.to.empty()) &&
                        ((msg.to[0] == (uint16_t)this->m_shared_memory_id) ||
                         AddrMapping::getAddrMapping()->isLLCId(msg.to[0]));
        if ((MESIProtocol::EventId)*out_id == MESIProtocol::EventId::OwnData)
            *out_id = (MSIProtocol::EventId)(from_llc ? EventId::RDM : EventId::RDC);
        else if ((MESIProtocol::EventId)*out_id == MESIProtocol::EventId::OwnData_Execlusive)
            *out_id = (MSIProtocol::EventId) EventId::RDM_Execlusive;
    }
}