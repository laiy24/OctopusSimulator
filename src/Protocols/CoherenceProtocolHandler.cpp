/*
 * File  :      CoherenceProtocolHandler.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 23, 2021
 */

#include "../../header/Protocols/CoherenceProtocolHandler.h"
#include "../../header/AddrMapping.h"
using namespace std;

namespace octopus
{
    CoherenceProtocolHandler::CoherenceProtocolHandler(CacheDataHandler *cache, const string &fsm_path, int id, int sharedMemId)
    {
        this->m_data_handler = cache;
        this->m_fsm = new FSMReader(fsm_path);

        this->m_id = id;
        this->m_shared_memory_id = sharedMemId;
    }

    CoherenceProtocolHandler::~CoherenceProtocolHandler()
    {
        delete this->m_fsm;
    }

    void CoherenceProtocolHandler::initializeCacheStates()
    {   //This function should be overridden if the name
        //of the invalid state is different than "I"
        int initState = this->m_fsm->getState(string("I"));
        m_data_handler->initializeCacheStates(initState);
    }

    int CoherenceProtocolHandler::getLLCDestination(uint64_t addr, uint16_t owner) const
    {
        if (AddrMapping::getAddrMapping()->isBanked() && !AddrMapping::getAddrMapping()->isLLCId(this->m_id))
        {
            int bank = AddrMapping::getAddrMapping()->get_bnk_bits(addr, owner);
            return AddrMapping::getAddrMapping()->getLLCId(bank);
        }
        return m_shared_memory_id;
    }
}