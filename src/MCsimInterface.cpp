/*
 * File  :      MCsimInterface.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On August 4, 2022
 */

#include <cstdlib>
#include "../header/MCsimInterface.h"
#include "../header/Logger.h"
#include "../header/RequestorsQueues.h"
#include "../header/AddrMapping.h"

using namespace std;

namespace octopus
{
    MCsimInterface::MCsimInterface(CommunicationInterface *lower_interface, int dram_id, int llc_id,
                                   int num_cores, int block_size, uint64_t clk_period,
                                   const std::string &mem_system)
        : MCsimInterface(lower_interface, dram_id, std::vector<int>{ llc_id }, num_cores, block_size, clk_period, mem_system)
    {
    }

    MCsimInterface::MCsimInterface(CommunicationInterface *lower_interface, int dram_id, const std::vector<int> &llc_ids,
                                   int num_cores, int block_size, uint64_t clk_period,
                                   const std::string &mem_system)
        : ClockedObj(clk_period) // == LLC period: one tick per core cycle (see header). Must be
                                 // non-zero: ClockManager reschedules at current_time+period, so
                                 // period 0 self-reschedules at the same timestamp forever.
    {
        if (clk_period == 0)
        {
            cout << "MCsimInterface: clock period must be non-zero" << endl;
            exit(0);
        }
        m_id = dram_id;
        m_llc_ids = llc_ids;
        m_llc_id = llc_ids.empty() ? 10 : llc_ids[0];
        m_clk_cycle = 1;
        m_llc_line_size = block_size;
        m_read_count = 0;
        m_write_count = 0;

        m_lower_interface = lower_interface;
        m_processing_queue = new FRFCFS_Buffer<Message, MCsimInterface>(&MCsimInterface::getRequestState, this);

        /******************** Initialization of MCsim (DDR4-2400U, 8Gb x8, 1 channel/1 rank) ********************/
        std::string sys_init_file = std::string(MCSIM_PATH) + "system/" + mem_system + "/" + mem_system + ".ini";
        m_mcsim = MCsim::getMemorySystemInstance(
            num_cores,
            sys_init_file,
            "DDR4",
            "2400U",
            "8Gb_x8",
            1,
            1);
        m_mcsim->setCPUClockSpeed(2.4 * 1e9);
        RequestorsQueues::setRequestorsQueues(m_mcsim->getRequestorsQueues());

        MCsim::TransactionCompleteCB *read_cb = new MCsim::MCsimCallback<MCsimInterface, void, uint64_t, uint64_t, uint64_t>(this, &MCsimInterface::read_callback);
        MCsim::TransactionCompleteCB *write_cb = new MCsim::MCsimCallback<MCsimInterface, void, uint64_t, uint64_t, uint64_t>(this, &MCsimInterface::write_callback);
        m_mcsim->RegisterCallbacks(read_cb, write_cb);
    }

    MCsimInterface::~MCsimInterface()
    {
        if (RequestorsQueues::getRequestorsQueues() == m_mcsim->getRequestorsQueues())
            RequestorsQueues::setRequestorsQueues(nullptr);
        delete m_mcsim;
    }

    void MCsimInterface::cycleProcess()
    {
        processLogic();
        m_mcsim->update();

        m_clk_cycle++;
    }

    void MCsimInterface::init()
    {
    }

    static uint64_t mcsimTraceAddr()
    {
        static uint64_t v = [](){ const char *t = getenv("OCTOPUS_TRACE_ADDR"); return t ? strtoull(t, NULL, 0) : 0ULL; }();
        return v;
    }
    static bool mcsimTraced(uint64_t addr)
    {
        uint64_t t = mcsimTraceAddr();
        return t == 1 || (t != 0 && (addr & ~63ULL) == (t & ~63ULL));
    }

    void MCsimInterface::processLogic()
    {
        addRequests2ProcessingQueue(*m_processing_queue);

        Message ready_msg;
        if (m_processing_queue->getFirstReady(&ready_msg))
        {
            if (mcsimTraced(ready_msg.addr))
                cout << "[mcsim] cyc " << m_clk_cycle << " request addr 0x" << std::hex << ready_msg.addr << std::dec << " msg " << ready_msg.msg_id
                     << " owner " << ready_msg.owner << (ready_msg.data == NULL ? " READ" : " WRITE") << " pending " << m_pending_requests.size() << endl;
            if (m_mcsim->addRequest(ready_msg.owner, ready_msg.addr, ready_msg.data == NULL,
                                    m_llc_line_size, ready_msg.msg_id)) // 1 -> Read, 0 -> Write
            {
                // Design B: request has entered DRAM (mirrors MainMemoryController's stamp).
                Logger::getLogger()->event(ready_msg.msg_id, Logger::Role::DRAM, (uint32_t)m_id, Logger::Phase::ENTER);
                Logger::getLogger()->trace(ready_msg, Logger::Role::DRAM, (uint32_t)m_id, Logger::Phase::ENTER);
                MCsim::RequestorsQueues *requestors_queues = RequestorsQueues::getRequestorsQueues();
                if (requestors_queues != nullptr)
                    requestors_queues->addRequest(ready_msg.owner, (unsigned int)ready_msg.msg_id,
                                                  1, (unsigned int)ready_msg.addr);
                if (ready_msg.data == NULL) // Add read requests only (writes are fire-and-forget)
                    m_pending_requests.push_back(ready_msg);
                else
                    m_pending_writes.push_back(ready_msg);
            }
            else
            {
                cout << "MCsimInterface: Error failed to add request to MCsim" << endl;
                exit(0);
            }
        }

        if (!m_output_buffer.empty())
        {
            // Design B: DRAM finished servicing this read -> data leaves DRAM.
            Logger::getLogger()->event(m_output_buffer[0].msg_id, Logger::Role::DRAM, (uint32_t)m_id, Logger::Phase::EXIT);
            Logger::getLogger()->trace(m_output_buffer[0], Logger::Role::DRAM, (uint32_t)m_id, Logger::Phase::EXIT);
            if (!m_lower_interface->pushMessage(m_output_buffer[0], m_clk_cycle, MessageType::DATA_RESPONSE))
            {
                cout << "MCsimInterface(id = " << this->m_id << "): Cannot insert the Msg into the lower interface FIFO, FIFO is Full" << endl;
                exit(0);
            }
            m_output_buffer.erase(m_output_buffer.begin());
        }
    }

    void MCsimInterface::addRequests2ProcessingQueue(FRFCFS_Buffer<Message, MCsimInterface> &buf)
    {
        Message msg;

        if (m_lower_interface->peekMessage(&msg))
        {
            msg.source = Message::Source::LOWER_INTERCONNECT;
            msg.cycle = m_clk_cycle;
            if (buf.pushBack(msg, FRFCFS_State::Ready))
                m_lower_interface->popFrontMessage();
        }
    }

    FRFCFS_State MCsimInterface::getRequestState(const Message &msg, FRFCFS_State current_state)
    {
        return FRFCFS_State::Ready;
    }

    void MCsimInterface::read_callback(uint64_t id, uint64_t address, uint64_t clock_cycle)
    {
        bool found = false;
        if (mcsimTraced(address))
            cout << "[mcsim] cyc " << m_clk_cycle << " read_callback addr 0x" << std::hex << address << std::dec << " id " << id << " pending " << m_pending_requests.size() << endl;
        for (int i = 0; i < (int)m_pending_requests.size(); i++)
        {
            // Match by unique msg_id. Matching by address alone aliases when multiple
            // cores read the same line, causing premature removal from RequestorsQueues
            // and deadlocking the RROF scheduler.
            if (m_pending_requests[i].msg_id == id || (id == 0 && m_pending_requests[i].addr == address))
            {
                uint64_t data = m_read_count;
                m_read_count++;
                uint8_t data_buffer[64] = {};
                memcpy(data_buffer, &data, sizeof(data));

                Message msg = Message(m_pending_requests[i].msg_id, // Id
                                      m_pending_requests[i].addr,   // Addr
                                      m_clk_cycle,                  // Cycle
                                      0,                            // Complementary_value
                                      m_pending_requests[i].owner); // Owner
                msg.llc_addr_restore = m_pending_requests[i].llc_addr_restore;
                int target_llc = m_llc_id;
                if (AddrMapping::getAddrMapping()->isBanked())
                {
                    if (AddrMapping::getAddrMapping()->isLLCId(m_pending_requests[i].owner))
                    {
                        target_llc = m_pending_requests[i].owner;
                    }
                    else
                    {
                        int bank = AddrMapping::getAddrMapping()->get_bnk_bits(m_pending_requests[i].addr, m_pending_requests[i].owner);
                        if (!m_llc_ids.empty() && bank < (int)m_llc_ids.size())
                            target_llc = m_llc_ids[bank];
                        else
                            target_llc = AddrMapping::getAddrMapping()->getLLCId(bank);
                    }
                }
                msg.to.push_back((uint16_t)target_llc);               // To (back to the LLC)
                msg.kind = Message::K_FILL;
                msg.copy(data_buffer);
                m_output_buffer.push_back(msg);

                MCsim::RequestorsQueues *requestors_queues = RequestorsQueues::getRequestorsQueues();
                if (requestors_queues != nullptr &&
                    requestors_queues->isRequestExist(m_pending_requests[i].owner,
                                                      (unsigned int)m_pending_requests[i].msg_id,
                                                      nullptr, nullptr) >= 0)
                {
                    requestors_queues->removeRequest(m_pending_requests[i].owner,
                                                     (unsigned int)m_pending_requests[i].msg_id);
                }

                m_pending_requests.erase(m_pending_requests.begin() + i);
                found = true;
                break;
            }
        }

        if (!found)
        {
            cout << "MCsimInterface: Error read_callback couldn't find the pending request" << endl;
            exit(0);
        }
    }

    void MCsimInterface::write_callback(uint64_t id, uint64_t address, uint64_t clock_cycle)
    {
        m_write_count++;
        for (int i = 0; i < (int)m_pending_writes.size(); ++i)
        {
            // Match by unique msg_id (falling back to address if id == 0).
            if (m_pending_writes[i].msg_id != id && (id != 0 || m_pending_writes[i].addr != address))
                continue;

            MCsim::RequestorsQueues *requestors_queues = RequestorsQueues::getRequestorsQueues();
            if (requestors_queues != nullptr &&
                requestors_queues->isRequestExist(m_pending_writes[i].owner,
                                                  (unsigned int)m_pending_writes[i].msg_id,
                                                  nullptr, nullptr) >= 0)
            {
                requestors_queues->removeRequest(m_pending_writes[i].owner,
                                                 (unsigned int)m_pending_writes[i].msg_id);
            }
            m_pending_writes.erase(m_pending_writes.begin() + i);
            return;
        }
    }
}
