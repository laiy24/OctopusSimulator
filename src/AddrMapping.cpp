/*
 * File  :      AddrMapping.cpp
 * Author:      Octopus Simulator Team
 *
 * Banked LLC Address Mapping & Requestor-based Routing implementation
 */

#include "../header/AddrMapping.h"

namespace octopus
{
    AddrMapping *AddrMapping::_AddrMapping = nullptr;

    AddrMapping::AddrMapping(ParametersMap map, std::string config_path, std::string name, std::string pname)
        : Configurable(map, config_path, name, pname)
    {
        m_llc_nbnks = 1;
        m_cl_size = 64;
        m_bnk_size = 524288;
        m_nway = 8;
        m_ncores = 1;
        partition_setup = "NoPartition";
        m_num_dedicated_cores = 2;
        m_banks_per_dedicated_core = 0; // 0 indicates automatic calculation

        if (parameters.count("num_llc_banks"))
            m_llc_nbnks = std::get<int>(parameters.at("num_llc_banks").value);

        if (parameters.count("partition_setup"))
            partition_setup = std::get<std::string>(parameters.at("partition_setup").value);

        if (parameters.count("num_dedicated_cores"))
            m_num_dedicated_cores = std::get<int>(parameters.at("num_dedicated_cores").value);

        if (parameters.count("banks_per_dedicated_core"))
            m_banks_per_dedicated_core = std::get<int>(parameters.at("banks_per_dedicated_core").value);

        if (parameters.count("dedicated_core_ids"))
            m_dedicated_core_ids = std::get<std::vector<int>>(parameters.at("dedicated_core_ids").value);

        if (parameters.count("m_block_size"))
            m_cl_size = std::get<int>(parameters.at("m_block_size").value);

        if (parameters.count("cache_size"))
            m_bnk_size = std::get<int>(parameters.at("cache_size").value);

        if (parameters.count("m_ways_count"))
            m_nway = std::get<int>(parameters.at("m_ways_count").value);

        if (parameters.count("num_cores"))
            m_ncores = std::get<int>(parameters.at("num_cores").value);
    }

    AddrMapping *AddrMapping::getAddrMapping()
    {
        if (_AddrMapping == nullptr)
            _AddrMapping = new AddrMapping();
        return _AddrMapping;
    }

    AddrMapping *AddrMapping::initAddrMapping(ParametersMap map, std::string config_path, std::string name, std::string pname)
    {
        if (_AddrMapping != nullptr)
            delete _AddrMapping;
        _AddrMapping = new AddrMapping(map, config_path, name, pname);
        return _AddrMapping;
    }

    bool AddrMapping::isLLCId(int id) const
    {
        for (int llc_id : m_llc_ids)
        {
            if (llc_id == id)
                return true;
        }
        return false;
    }

    int AddrMapping::getBankIndexFromLLCId(int id) const
    {
        for (size_t i = 0; i < m_llc_ids.size(); i++)
        {
            if (m_llc_ids[i] == id)
                return (int)i;
        }
        return 0;
    }

    int AddrMapping::getLLCId(int bank_index) const
    {
        if (bank_index >= 0 && bank_index < (int)m_llc_ids.size())
            return m_llc_ids[bank_index];
        if (!m_llc_ids.empty())
            return m_llc_ids[0];
        return 10;
    }

    unsigned int AddrMapping::getBankBits(uint64_t addr, unsigned int owner_id)
    {
        if (m_llc_nbnks <= 1)
            return 0;

        unsigned int offset_bits = (unsigned int)std::log2(m_cl_size);
        unsigned int bnk_bits = (addr >> offset_bits) & (m_llc_nbnks - 1); // LSB is bank

        if (partition_setup == "BankPartition")
        {
            unsigned int num_bnk_per_core = m_banks_per_dedicated_core;
            if (num_bnk_per_core == 0)
            {
                num_bnk_per_core = (m_ncores > 0) ? (m_llc_nbnks / m_ncores) : 1;
                if (num_bnk_per_core == 0 && m_llc_nbnks > 2)
                    num_bnk_per_core = 1;
            }

            bool is_dedicated = false;
            int dedicated_idx = -1;

            // When explicit IDs are configured, the position in the list is the
            // dedicated-core slot. For example, with dedicated_core_ids = [6, 3],
            // core 6 is slot 0 and core 3 is slot 1; the list order, rather than
            // the numeric core ID, determines which high-end bank range is used.
            // If no list is provided, the final m_num_dedicated_cores are treated
            // as dedicated for backward compatibility.
            if (!m_dedicated_core_ids.empty())
            {
                for (size_t i = 0; i < m_dedicated_core_ids.size(); i++)
                {
                    if (m_dedicated_core_ids[i] == (int)owner_id)
                    {
                        is_dedicated = true;
                        dedicated_idx = (int)i;
                        break;
                    }
                }
            }
            // Without an explicit ID list, the last m_num_dedicated_cores
            // cores are dedicated; convert their core ID to a zero-based slot.
            else if ((int)owner_id >= m_ncores - m_num_dedicated_cores && (int)owner_id < m_ncores)
            {
                is_dedicated = true;
                dedicated_idx = owner_id - (m_ncores - m_num_dedicated_cores);
            }

            if (is_dedicated)
            {
                // The address-derived bank is first reduced to the number of banks
                // assigned to one dedicated core, then placed in that core's
                // contiguous range at the top of the LLC bank space. Thus, for
                // N dedicated cores, slot 0 receives the lowest range within
                // the dedicated region and later slots receive higher ranges.
                unsigned int num_ded = m_dedicated_core_ids.empty() ? m_num_dedicated_cores : (int)m_dedicated_core_ids.size();
                unsigned int tmp_offset = num_ded - dedicated_idx;
                bnk_bits = m_llc_nbnks - num_bnk_per_core * tmp_offset + (bnk_bits % (num_bnk_per_core > 0 ? num_bnk_per_core : 1));
            }
            else
            {
                // Non-dedicated cores share the banks below the dedicated ranges.
                // Reduce the original bank selection modulo the size of that
                // shared region so it cannot enter a dedicated core's banks.
                unsigned int num_ded = m_dedicated_core_ids.empty() ? m_num_dedicated_cores : (int)m_dedicated_core_ids.size();
                unsigned int num_shared = m_llc_nbnks - num_ded * num_bnk_per_core;
                if (num_shared > 0)
                    bnk_bits = bnk_bits % num_shared;
            }
        }
        return bnk_bits % m_llc_nbnks;
    }

    unsigned int AddrMapping::getSetBits(uint64_t addr, unsigned int owner_id)
    {
        unsigned int offset_bits = (unsigned int)std::log2(m_cl_size);
        unsigned int total_num_sets = (m_cl_size > 0 && m_nway > 0) ? (m_bnk_size / m_cl_size / m_nway) : 1;
        unsigned int bank_bits = (unsigned int)std::log2(m_llc_nbnks > 0 ? m_llc_nbnks : 1);
        unsigned int set_bits = (addr >> (offset_bits + bank_bits)) & (total_num_sets - 1);
        unsigned int num_set_per_core = (m_ncores > 0) ? (total_num_sets / m_ncores) : total_num_sets;

        if (partition_setup == "SetPartition")
        {
            bool is_dedicated = false;
            int dedicated_idx = -1;

            // Use the same owner-to-slot mapping as BankPartition: explicit
            // dedicated_core_ids are ordered slots; otherwise the final
            // m_num_dedicated_cores are the dedicated owners.
            if (!m_dedicated_core_ids.empty())
            {
                for (size_t i = 0; i < m_dedicated_core_ids.size(); i++)
                {
                    if (m_dedicated_core_ids[i] == (int)owner_id)
                    {
                        is_dedicated = true;
                        dedicated_idx = (int)i;
                        break;
                    }
                }
            }
            else if ((int)owner_id >= m_ncores - m_num_dedicated_cores && (int)owner_id < m_ncores)
            {
                is_dedicated = true;
                dedicated_idx = owner_id - (m_ncores - m_num_dedicated_cores);
            }

            if (is_dedicated)
            {
                unsigned int num_ded = m_dedicated_core_ids.empty() ? m_num_dedicated_cores : (int)m_dedicated_core_ids.size();
                unsigned int tmp_offset = num_ded - dedicated_idx;
                set_bits = total_num_sets - num_set_per_core * tmp_offset + (set_bits % (num_set_per_core > 0 ? num_set_per_core : 1));
            }
            else
            {
                unsigned int num_ded = m_dedicated_core_ids.empty() ? m_num_dedicated_cores : (int)m_dedicated_core_ids.size();
                unsigned int num_shared = total_num_sets - num_ded * num_set_per_core;
                if (num_shared > 0)
                    set_bits = set_bits % num_shared;
            }
        }
        return set_bits;
    }

    unsigned int AddrMapping::getLlcRestoreBit(uint64_t addr, unsigned int owner_id)
    {
        unsigned int offset_bits_shift = (unsigned int)std::log2(m_cl_size);
        unsigned int total_num_sets = (m_cl_size > 0 && m_nway > 0) ? (m_bnk_size / m_cl_size / m_nway) : 1;
        unsigned int llc_restore_bit = 0;

        if (partition_setup == "SetPartition")
            llc_restore_bit = (addr >> offset_bits_shift) & (total_num_sets - 1);
        else if (partition_setup == "BankPartition")
            llc_restore_bit = getBankIndexFromLLCId((int)owner_id);

        return llc_restore_bit;
    }

    uint64_t AddrMapping::addrMap(uint64_t addr, unsigned int owner_id, unsigned int llc_restore_bits)
    {
        // SetPartition can still remap set bits when the LLC has one bank;
        // only the bank-specific mapping is unnecessary in that case.
        if (m_llc_nbnks <= 1 && partition_setup != "SetPartition")
            return addr;

        unsigned int bank_bits_shift = (unsigned int)std::log2(m_llc_nbnks);
        unsigned int offset_bits_shift = (unsigned int)std::log2(m_cl_size);
        unsigned int total_num_sets = (m_cl_size > 0 && m_nway > 0) ? (m_bnk_size / m_cl_size / m_nway) : 1;
        unsigned int set_bits_shift = (unsigned int)std::log2(total_num_sets);
        uint64_t orig_info = 0;

        // Remove the bank field from the middle of the address so the remaining
        // tag/set bits become contiguous with the cache-line offset:
        //   before: | TAG | SET | BANK | OFFSET |
        //   after:  | TAG | SET |          OFFSET |
        uint64_t new_addr = (addr >> (bank_bits_shift + offset_bits_shift) << offset_bits_shift) | (addr & (m_cl_size - 1));

        if (partition_setup == "SetPartition")
        {
            // SetPartition keeps the original set in the high bits so it can
            // be restored later, then replaces the normal set field:
            //
            //   original: | TAG | SET(old) | BANK | OFFSET |
            //   step 1:   | TAG | SET(old) |          OFFSET |  (BANK removed)
            //   step 2:   | TAG | SET(new) |          OFFSET |  (SET replaced)
            //   final:    | SET(old) | TAG | SET(new) | OFFSET |
            //
            // Extract SET(old) for restoration. The set field is located above
            // the offset and bank fields in the original address.
            orig_info = (addr >> (offset_bits_shift + bank_bits_shift)) & (total_num_sets - 1);
            uint64_t new_set = 0;
            if (isLLCId(owner_id))
                new_set = llc_restore_bits;
            else
                new_set = getSetBits(addr, owner_id);

            // Clear the set field in the bank-removed address, insert SET(new),
            // and save SET(old) in the high bits for addrMapRestore().
            uint64_t set_mask = ((uint64_t)(total_num_sets - 1) << offset_bits_shift);
            new_addr = (new_addr & ~set_mask) | (new_set << offset_bits_shift);
            new_addr = new_addr | (orig_info << (64 - set_bits_shift));
        }
        else if (partition_setup == "BankPartition")
        {
            // Preserve the original bank by moving it to the high bits after
            // the middle bank field was removed:
            //   before: | TAG | SET | BANK(old) | OFFSET |
            //   result: | BANK(old) | TAG | SET | OFFSET |
            orig_info = (addr >> offset_bits_shift) & (m_llc_nbnks - 1);
            new_addr = new_addr | (orig_info << (64 - bank_bits_shift));
        }

        return new_addr;
    }

    uint64_t AddrMapping::addrMapRestore(uint64_t addr, unsigned int current_bnk)
    {
        if (m_llc_nbnks <= 1 && partition_setup != "SetPartition")
            return addr;

        unsigned int bank_bits_shift = (unsigned int)std::log2(m_llc_nbnks);
        unsigned int offset_bits_shift = (unsigned int)std::log2(m_cl_size);
        unsigned int total_set_per_bnk = (m_cl_size > 0 && m_nway > 0) ? (m_bnk_size / m_cl_size / m_nway) : 1;
        unsigned int set_bits_shift = (unsigned int)std::log2(total_set_per_bnk);
        uint64_t new_addr = addr;

        if (partition_setup == "SetPartition")
        {
            // Restore the original address layout produced by addrMap():
            //
            //   mapped:   | SET(old) | TAG | SET(new) | OFFSET |
            //   step 1:   |          | TAG | SET(new) | OFFSET |  (read SET(old))
            //   step 2:   |          | TAG | SET(new) |        |  (clear OFFSET)
            //   step 3:   |          | TAG |          |        |  (clear SET(new))
            //   step 4:   | TAG | SET(old) |          |        |  (make room for BANK)
            //   final:    | TAG | SET(old) | BANK | OFFSET |
            //
            // The current bank is supplied separately because the mapped
            // address no longer stores BANK in its original middle position.
            uint64_t orig_set = addr >> (64 - set_bits_shift);
            // Remove the saved original set, offset, and remapped set fields.
            new_addr = new_addr & ~(((uint64_t)total_set_per_bnk - 1) << (64 - set_bits_shift));
            new_addr = new_addr & ~((uint64_t)m_cl_size - 1);
            new_addr = (new_addr & ~(((uint64_t)total_set_per_bnk - 1) << offset_bits_shift)) << bank_bits_shift;
            // Reinsert the original set, current bank, and cache-line offset.
            new_addr = new_addr |
                       (orig_set << (offset_bits_shift + bank_bits_shift)) |
                       ((uint64_t)current_bnk << offset_bits_shift) |
                       (addr & (m_cl_size - 1));
        }
        else
        {
            if (partition_setup == "BankPartition")
            {
                // BankPartition restores the bank from the high bits:
                //
                //   mapped: | BANK(old) | TAG | SET | OFFSET |
                //   step 1: |           | TAG | SET | OFFSET |  (read BANK(old))
                //   step 2: |           | TAG | SET | OFFSET |  (clear BANK(old))
                //   step 3: |           | TAG | SET |          |  (clear OFFSET)
                //   step 4: | TAG | SET |          |          |  (make room for BANK)
                //   final:  | TAG | SET | BANK(old) | OFFSET |
                current_bnk = (unsigned int)(addr >> (64 - bank_bits_shift));
                // Remove the saved bank before shifting the remaining fields.
                new_addr = new_addr & ~(((uint64_t)m_llc_nbnks - 1) << (64 - bank_bits_shift));
            }

            // Shift the compacted address left to recreate the original
            // position of the bank field, then restore BANK and OFFSET.
            new_addr = ((new_addr & ~((uint64_t)m_cl_size - 1)) << bank_bits_shift);
            new_addr = new_addr |
                       ((uint64_t)current_bnk << offset_bits_shift) |
                       (addr & (m_cl_size - 1));
        }

        return new_addr;
    }

    void AddrMapping::addrMap(Message *msg)
    {
        msg->original_set = (msg->addr >> ((unsigned int)std::log2(m_cl_size) +
                                           (unsigned int)std::log2(m_llc_nbnks > 0 ? m_llc_nbnks : 1))) &
                            ((m_cl_size > 0 && m_nway > 0 ? m_bnk_size / m_cl_size / m_nway : 1) - 1);
        msg->original_bank = getBankBits(msg->addr, msg->owner);

        if (m_llc_nbnks <= 1 && partition_setup != "SetPartition")
        {
            msg->mapped_set = msg->original_set;
            msg->mapped_bank = msg->original_bank;
            msg->address_mapping_recorded = true;
            return;
        }

        if (isLLCId(msg->owner) && partition_setup == "BankPartition")
            msg->bank_id = (uint8_t)msg->llc_addr_restore;
        else
            msg->bank_id = (uint8_t)getBankBits(msg->addr, msg->owner);

        msg->addr = addrMap(msg->addr, msg->owner, msg->llc_addr_restore);
        unsigned int offset_bits = (unsigned int)std::log2(m_cl_size);
        unsigned int total_num_sets = (m_cl_size > 0 && m_nway > 0) ? (m_bnk_size / m_cl_size / m_nway) : 1;
        msg->mapped_set = (msg->addr >> offset_bits) & (total_num_sets - 1);
        msg->mapped_bank = msg->bank_id;
        msg->address_mapping_recorded = true;
    }

    void AddrMapping::addrMapRestore(Message *msg, unsigned int current_bnk)
    {
        if (m_llc_nbnks <= 1 && partition_setup != "SetPartition")
            return;

        if (isLLCId(msg->owner))
            msg->llc_addr_restore = (uint16_t)getLlcRestoreBit(msg->addr, msg->owner);

        msg->addr = addrMapRestore(msg->addr, current_bnk);
    }
}
