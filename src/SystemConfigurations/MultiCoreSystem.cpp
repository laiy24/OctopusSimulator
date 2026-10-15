/*
 * File  :      MultiCoreSystem.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 1, 2023
 */

#include "../../header/SystemConfigurations/MultiCoreSystem.h"

using namespace std;

namespace octopus
{   
    MultiCoreSystem::MultiCoreSystem(vector<string> cl_params, string config_name, string config_path) :
        Configurable(cl_params, config_path, config_name)
    {
        string name = STRINGIFY(MultiCoreSystem);
        //Parameters initialization
        string workload_path = std::get<string>(parameters.at(STRINGIFY(workload_path)).value);
        int num_cores = std::get<int>(parameters.at(STRINGIFY(num_cores)).value);
        vector<string> bus_type = std::get<vector<string>>(parameters.at(STRINGIFY(bus_type)).value);
        string cache_controller_type = std::get<string>(parameters.at(STRINGIFY(cache_controller_type)).value);
        // LLC may use a different controller than the L1s (e.g. CacheController_End2End,
        // which drives the TripleBus service channel for back-invalidation on eviction).
        // Falls back to cache_controller_type when not specified.
        string llc_controller_type = cache_controller_type;
        if (parameters.count(STRINGIFY(llc_controller_type)))
            llc_controller_type = std::get<string>(parameters.at(STRINGIFY(llc_controller_type)).value);
        // Core model: the trace-driven CPU by default, or ExternalCPU when an
        // embedder (the gem5 bridge) injects the requests itself.
        string cpu_type = STRINGIFY(CPU);
        if (parameters.count(STRINGIFY(cpu_type)))
            cpu_type = std::get<string>(parameters.at(STRINGIFY(cpu_type)).value);

        int num_llc_banks = 1;
        if (parameters.count(STRINGIFY(num_llc_banks)))
            num_llc_banks = std::get<int>(parameters.at(STRINGIFY(num_llc_banks)).value);

        string llc_partition = "NoPartition";
        if (parameters.count(STRINGIFY(llc_partition)))
            llc_partition = std::get<string>(parameters.at(STRINGIFY(llc_partition)).value);

        //Constructor
        Bus *bus[bus_type.size()];
        for(int i = 0; i < bus_type.size(); i++)
        {
            if(bus_type[i] == "TripleBus")
                bus[i] = new TripleBus(getSubMap(STRINGIFY(bus), i), name);
            else if(bus_type[i] == "Bus")
                bus[i] = new Bus(getSubMap(STRINGIFY(bus), i), NULL, name);
            else
            {
                cout << "Error unsupported bus type" << endl;
                exit(0);
            }
        }

        // Design B: bus[1] is the LLC<->DRAM bus; its crossings log as MEM_BUS.
        if (bus_type.size() > 1)
            bus[1]->setMemBus();

        // Initialize AddrMapping for banked LLC and routing
        ParametersMap addr_map_params = getSubMap(STRINGIFY(addr_mapping));
        addr_map_params.erase("num_llc_banks");
        addr_map_params.emplace("num_llc_banks", Parameter("num_llc_banks", Parameter::Type::Integer, num_llc_banks));
        addr_map_params.erase("partition_setup");
        addr_map_params.emplace("partition_setup", Parameter("partition_setup", Parameter::Type::String, llc_partition));
        addr_map_params.erase("num_cores");
        addr_map_params.emplace("num_cores", Parameter("num_cores", Parameter::Type::Integer, num_cores));
        if (parameters.count("num_dedicated_cores"))
        {
            addr_map_params.erase("num_dedicated_cores");
            addr_map_params.emplace("num_dedicated_cores", parameters.at("num_dedicated_cores"));
        }
        if (parameters.count("dedicated_core_ids"))
        {
            addr_map_params.erase("dedicated_core_ids");
            addr_map_params.emplace("dedicated_core_ids", parameters.at("dedicated_core_ids"));
        }
        if (parameters.count("banks_per_dedicated_core"))
        {
            addr_map_params.erase("banks_per_dedicated_core");
            addr_map_params.emplace("banks_per_dedicated_core", parameters.at("banks_per_dedicated_core"));
        }

        auto llc_sub0 = getSubMap(STRINGIFY(llc_controller), num_llc_banks > 1 ? 0 : -1);
        if (llc_sub0.count("m_data_handler.m_block_size"))
        {
            addr_map_params.erase("m_block_size");
            addr_map_params.emplace("m_block_size", llc_sub0.at("m_data_handler.m_block_size"));
        }
        if (llc_sub0.count("m_data_handler.cache_size"))
        {
            addr_map_params.erase("cache_size");
            addr_map_params.emplace("cache_size", llc_sub0.at("m_data_handler.cache_size"));
        }
        if (llc_sub0.count("m_data_handler.m_ways_count"))
        {
            addr_map_params.erase("m_ways_count");
            addr_map_params.emplace("m_ways_count", llc_sub0.at("m_data_handler.m_ways_count"));
        }

        AddrMapping *addr_map = AddrMapping::initAddrMapping(addr_map_params, string(CONFIGURATION_PATH), STRINGIFY(addr_mapping), name);

        vector<int> llc_ids;
        if (num_llc_banks > 1)
        {
            const vector<int> &bus0_upper = bus[0]->getUpperLevelIds();
            for (int b = 0; b < num_llc_banks; b++)
            {
                auto sub = getSubMap(STRINGIFY(llc_controller), b);
                if (sub.count("m_id"))
                    llc_ids.push_back(std::get<int>(sub.at("m_id").value));
                else if (b < (int)bus0_upper.size())
                    llc_ids.push_back(bus0_upper[b]);
                else
                    llc_ids.push_back(50 + b);
            }
        }
        else
        {
            auto sub = getSubMap(STRINGIFY(llc_controller));
            int single_id = 10;
            if (sub.count("m_id"))
                single_id = std::get<int>(sub.at("m_id").value);
            else if (!bus[0]->getUpperLevelIds().empty())
                single_id = bus[0]->getUpperLevelIds()[0];
            llc_ids.push_back(single_id);
        }
        addr_map->set_llc_ids(llc_ids);

        // iterate over each core
        for (int i = 0; i < num_cores; i++)
        {
            // Workload per core: a periodic task program (docs/Tasks.md) if present, else the
            // access trace, else nothing (an idle core).
            string file_path = workload_path + "/task_C" + std::to_string(i) + ".task.csv";
            if (!std::ifstream(file_path).good())
            {
                file_path = workload_path + "/trace_C" + std::to_string(i) + ".trc.shared";
                if (!std::ifstream(file_path).good())
                    file_path = "";
            }
            BaseController *cache_controller;
            int cache_id = std::get<int>(getSubMap(STRINGIFY(cache_controller), i).at("m_id").value);

            DirectInterconnect *cpu_interconnect = new DirectInterconnect(getSubMap(STRINGIFY(cpu_interconnect), i), cache_id, -1, name);

            ExternalCPU *external_cpu = NULL;
            if (cpu_type == STRINGIFY(ExternalCPU))
            {
                external_cpu = new ExternalCPU(getSubMap(STRINGIFY(cpu), i), cache_id,
                                               cpu_interconnect->getInterfaceFor(-1), name);
                ExternalCPU::getExtCPUs()->emplace(cache_id, external_cpu);
            }
            else
                new CPU(getSubMap(STRINGIFY(cpu), i), cache_id, cpu_interconnect->getInterfaceFor(-1), file_path, name);

            cache_controller = createController(cache_controller_type, 
                                                getSubMap(STRINGIFY(cache_controller), i),
                                                bus[0]->getInterfaceFor(cache_id),
                                                cpu_interconnect->getInterfaceFor(cache_id), name);
            if (external_cpu != NULL)
            {
                cache_controller->setCpuPort(external_cpu);
                external_cpu->attachCache(cache_controller);
            }
        }

        vector<BaseController *> llc_controllers;
        for (int b = 0; b < num_llc_banks; b++)
        {
            ParametersMap sub = (num_llc_banks > 1) ? getSubMap(STRINGIFY(llc_controller), b)
                                                    : getSubMap(STRINGIFY(llc_controller));
            int llc_id = llc_ids[b];
            BaseController *ctrl = createController(llc_controller_type, sub,
                                                    bus[1]->getInterfaceFor(llc_id),
                                                    bus[0]->getInterfaceFor(llc_id), name);
            ctrl->setLogRole(Logger::Role::LLC);   // Design B: tag LLC events
            ctrl->setBankIndex(b);
            llc_controllers.push_back(ctrl);
        }

        // Main memory: the basic MainMemoryController by default, or the MCsim DRAM
        // simulator when `main_memory_type=MCsim` (configured for DDR4 in MCsimInterface).
        int main_memory_id = std::get<int>(getSubMap(STRINGIFY(m_main_memory)).at("m_id").value);
        string main_memory_type = STRINGIFY(MainMemoryController);
        if (parameters.count(STRINGIFY(main_memory_type)))
            main_memory_type = std::get<string>(parameters.at(STRINGIFY(main_memory_type)).value);

        if (main_memory_type == "MCsim")
        {
            string mem_system = "FRFCFS";
            if (parameters.count(STRINGIFY(mcsim_scheduler)))
                mem_system = std::get<string>(parameters.at(STRINGIFY(mcsim_scheduler)).value);
            new MCsimInterface(bus[1]->getInterfaceFor(main_memory_id), main_memory_id, llc_ids,
                               num_cores, /*block_size=*/64, llc_controllers[0]->getClkPeriod(), mem_system);
        }
        else
        {
            auto mem_map = getSubMap(STRINGIFY(m_main_memory));
            if (mem_map.empty())
                mem_map = getSubMap(STRINGIFY(MainMemoryController));
            new MainMemoryController(mem_map, bus[1]->getInterfaceFor(main_memory_id), name);
        }

        Logger::getLogger()->registerReportPath(workload_path + string("/newLogger"));
    }

    BaseController* MultiCoreSystem::createController(string type, ParametersMap map,
                                                       CommunicationInterface *upper_interface, 
                                                       CommunicationInterface *lower_interface,
                                                       string pname)
    {

        if(type == STRINGIFY(BaseController))
            return new BaseController(map, upper_interface, lower_interface, pname);
        else if(type == STRINGIFY(CacheController))
            return new CacheController(map, upper_interface, lower_interface, pname);
        else if(type == STRINGIFY(CacheControllerExclusive))
            return new CacheControllerExclusive(map, upper_interface, lower_interface, pname);
        else if(type == STRINGIFY(CacheController_End2End))
            return new CacheController_End2End(map, upper_interface, lower_interface, pname);
        else if(type == STRINGIFY(CacheControllerDirectory))
            return new CacheControllerDirectory(map, upper_interface, lower_interface, pname);
        else
        {
            cout << "Error: wrong cache controller type." << endl;
            exit(0);
            return NULL;
        }
    }
}