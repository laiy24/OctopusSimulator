/*
 * File  :      AddrMapping.h
 * Author:      Octopus Simulator Team
 *
 * Banked LLC Address Mapping & Requestor-based Routing
 */

#ifndef _ADDR_MAPPING_H
#define _ADDR_MAPPING_H

#include <cstdint>
#include <string>
#include <vector>
#include <cmath>
#include <iostream>

#include "Configurable.h"
#include "Interconnect/CommunicationInterface.h"

namespace octopus
{
    class AddrMapping : public Configurable
    {
    protected:
        int m_llc_nbnks;
        int m_cl_size;
        int m_bnk_size;
        int m_nway;
        int m_ncores;
        std::string partition_setup;
        std::vector<int> m_llc_ids;

        // Configurable bank partitioning settings
        int m_num_dedicated_cores;
        std::vector<int> m_dedicated_core_ids;
        int m_banks_per_dedicated_core;

        static AddrMapping *_AddrMapping;

    public:
        AddrMapping(ParametersMap map = ParametersMap(),
                    std::string config_path = std::string(CONFIGURATION_PATH),
                    std::string name = STRINGIFY(addr_mapping),
                    std::string pname = "");

        static AddrMapping *getAddrMapping();
        static AddrMapping *initAddrMapping(ParametersMap map = ParametersMap(),
                                            std::string config_path = std::string(CONFIGURATION_PATH),
                                            std::string name = STRINGIFY(addr_mapping),
                                            std::string pname = "");

        bool isBanked() const { return m_llc_nbnks > 1; }

        // Core address mapping
        uint64_t addrMap(uint64_t addr, unsigned int owner_id = 0, unsigned int llc_restore_bits = 0);
        uint64_t addrMapRestore(uint64_t addr, unsigned int current_bnk);
        void addrMap(Message *msg);
        void addrMapRestore(Message *msg, unsigned int current_bnk);

        // Aliases for compatibility
        uint64_t addr_map(uint64_t addr, unsigned int owner_id = 0, unsigned int llc_restore_bits = 0)
        {
            return addrMap(addr, owner_id, llc_restore_bits);
        }
        uint64_t addr_map_restore(uint64_t addr, unsigned int current_bnk)
        {
            return addrMapRestore(addr, current_bnk);
        }
        void addr_map(Message *msg) { addrMap(msg); }
        void addr_map_restore(Message *msg, unsigned int current_bnk) { addrMapRestore(msg, current_bnk); }

        unsigned int getBankBits(uint64_t addr, unsigned int owner_id = 0);
        unsigned int getSetBits(uint64_t addr, unsigned int owner_id = 0);
        unsigned int getLlcRestoreBit(uint64_t addr, unsigned int owner_id = 0);

        // Aliases for compatibility
        unsigned int get_bnk_bits(uint64_t addr, unsigned int owner_id = 0) { return getBankBits(addr, owner_id); }
        unsigned int get_set_bits(uint64_t addr, unsigned int owner_id = 0) { return getSetBits(addr, owner_id); }
        unsigned int get_llc_restore_bit(uint64_t addr, unsigned int owner_id = 0) { return getLlcRestoreBit(addr, owner_id); }

        void setPartitionSetup(const std::string &part) { partition_setup = part; }
        const std::string &getPartitionSetup() const { return partition_setup; }
        void set_partition_setup(const std::string &part) { setPartitionSetup(part); }
        const std::string &get_partition_setup() const { return getPartitionSetup(); }

        void setNumLlcBanks(int nbnks) { m_llc_nbnks = nbnks; }
        int getNumLlcBanks() const { return m_llc_nbnks; }
        void set_llc_nbnks(int nbnks) { setNumLlcBanks(nbnks); }
        int get_llc_nbnks() const { return getNumLlcBanks(); }

        void setCacheLineSize(int size) { m_cl_size = size; }
        int getCacheLineSize() const { return m_cl_size; }
        void set_cl_size(int size) { setCacheLineSize(size); }
        int get_cl_size() const { return getCacheLineSize(); }

        void setNumCores(int cores) { m_ncores = cores; }
        int getNumCores() const { return m_ncores; }
        void set_ncores(int cores) { setNumCores(cores); }
        int get_ncores() const { return getNumCores(); }

        void setLlcIds(const std::vector<int> &ids) { m_llc_ids = ids; }
        const std::vector<int> &getLlcIds() const { return m_llc_ids; }
        void set_llc_ids(const std::vector<int> &ids) { setLlcIds(ids); }
        const std::vector<int> &get_llc_ids() const { return getLlcIds(); }

        bool isLLCId(int id) const;
        int getBankIndexFromLLCId(int id) const;
        int getLLCId(int bank_index) const;

        // Configurable partition parameters
        void setNumDedicatedCores(int count) { m_num_dedicated_cores = count; }
        int getNumDedicatedCores() const { return m_num_dedicated_cores; }
        void set_num_dedicated_cores(int count) { setNumDedicatedCores(count); }
        int get_num_dedicated_cores() const { return getNumDedicatedCores(); }

        void setDedicatedCoreIds(const std::vector<int> &core_ids) { m_dedicated_core_ids = core_ids; }
        const std::vector<int> &getDedicatedCoreIds() const { return m_dedicated_core_ids; }
        void set_dedicated_core_ids(const std::vector<int> &core_ids) { setDedicatedCoreIds(core_ids); }
        const std::vector<int> &get_dedicated_core_ids() const { return getDedicatedCoreIds(); }

        void setBanksPerDedicatedCore(int b) { m_banks_per_dedicated_core = b; }
        int getBanksPerDedicatedCore() const { return m_banks_per_dedicated_core; }
        void set_banks_per_dedicated_core(int b) { setBanksPerDedicatedCore(b); }
        int get_banks_per_dedicated_core() const { return getBanksPerDedicatedCore(); }

    };
}

#endif /* _ADDR_MAPPING_H */
