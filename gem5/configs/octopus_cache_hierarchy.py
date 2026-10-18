import os
from m5.objects import *
from m5.objects import (
    Port,
    SystemXBar,
    AddrRange,
    Octopus,
)

try:
    from m5.objects import ProfileGen
except (ImportError, AttributeError):
    ProfileGen = None

from gem5.components.boards.abstract_board import AbstractBoard
from gem5.components.cachehierarchies.classic.abstract_classic_cache_hierarchy import (
    AbstractClassicCacheHierarchy,
)
from gem5.components.cachehierarchies.classic.caches.mmu_cache import MMUCache
from gem5.isas import ISA


class OctopusCacheHierarchy(AbstractClassicCacheHierarchy):
    """
    One Octopus bridge per L1. The Octopus system itself is described by the
    CSV preset `config_name` (configuration/SystemConfigurations/<name>.csv);
    it must declare cpu_type=ExternalCPU and one core per bridge, with the
    instruction caches at ids 0..n-1 and the data caches at n..2n-1.
    """

    def __init__(self, config_name, output_path, atp_files=None, extra_params=None) -> None:
        AbstractClassicCacheHierarchy.__init__(self=self)
        self._useATP = False
        self._configName = config_name
        self._outputPath = output_path
        # Octopus 'name(type)=value' overrides applied on top of the preset,
        # e.g. "bus[0].interconnect_controller.m_response_latency(i)=1".
        # Every bridge must carry the same list: only the first one built
        # constructs the Octopus system.
        self._extraParams = list(extra_params or [])
        # Request logging (cpu[*].log_requests(i)=1) writes under
        # <outdir>/newLogger, which the Logger does not create itself.
        if any("log_requests" in p for p in self._extraParams):
            os.makedirs(os.path.join(self._outputPath, "newLogger"), exist_ok=True)
        self.membus = SystemXBar(width=64)
        if atp_files is not None:
            if ProfileGen is None:
                raise RuntimeError(
                    "ProfileGen is not available in m5.objects. Please ensure gem5 is compiled with ProfileGen support to use ATP."
                )
            self._useATP = True
            self.atp_adaptor = ProfileGen(
                config_files=atp_files,
                exit_when_done=False,
                trace_atp=False,
                init_only=False
            )

    def _bridge(self, cache_id, core):
        return Octopus(
            cache_id=cache_id,
            log_core=core,
            system_name="MultiCoreSystem",
            config_name=self._configName,
            # The Logger writes its per-core reports under <workload_path>/newLogger.
            cl_params=[f"workload_path(s)={self._outputPath}"] + self._extraParams,
        )

    def get_mem_side_port(self) -> Port:
        return self.membus.mem_side_ports

    def get_cpu_side_port(self) -> Port:
        return self.membus.cpu_side_ports

    def incorporate_cache(self, board: AbstractBoard) -> None:
        board.connect_system_port(self.membus.cpu_side_ports)

        for cntr in board.get_memory().get_memory_controllers():
            cntr.port = self.membus.mem_side_ports

        n = board.get_processor().get_num_cores()
        # Core i's instruction L1 is Octopus id i, its data L1 id n + i; both
        # report as core i when request logging is on.
        self.l1i_caches = [self._bridge(i, i) for i in range(n)]
        self.l1d_caches = [self._bridge(n + i, i) for i in range(n)]

        if board.has_coherent_io():
            self._setup_io_cache(board)

        for i, cpu in enumerate(board.get_processor().get_cores()):
            cpu.connect_icache(self.l1i_caches[i].cpu_side)
            cpu.connect_dcache(self.l1d_caches[i].cpu_side)

            self.l1i_caches[i].mem_side = self.membus.cpu_side_ports
            self.l1d_caches[i].mem_side = self.membus.cpu_side_ports

            cpu.connect_walker_ports(
                self.membus.cpu_side_ports, self.membus.cpu_side_ports
            )

            if board.get_processor().get_isa() == ISA.X86:
                int_req_port = self.membus.mem_side_ports
                int_resp_port = self.membus.cpu_side_ports
                cpu.connect_interrupt(int_req_port, int_resp_port)
            else:
                cpu.connect_interrupt()

        if self._useATP:
            base = 2 * n
            # Ensure num_ports matches the number of masters in your ATP profiles
            # If each profile file has 1 master, len(config_files) is fine;
            # otherwise, set this to the total number of masters (e.g., 2 if you have default + GPU).
            num_ports = max(len(self.atp_adaptor.config_files), 2)

            self.atp_caches = [
                self._bridge(base + i, i)
                for i in range(num_ports)
            ]

            # 1. Connect all Octopus bridges downstream to the membus
            for bridge in self.atp_caches:
                bridge.mem_side = self.membus.cpu_side_ports

            # 2. Assign the upstream ports as a Python list to populate the VectorPort
            self.atp_adaptor.port = [bridge.cpu_side for bridge in self.atp_caches]

    def _setup_io_cache(self, board: AbstractBoard) -> None:
        """Create a cache for coherent I/O connections"""
        self.iocache = Cache(
            assoc=8,
            tag_latency=50,
            data_latency=50,
            response_latency=50,
            mshrs=20,
            size="1KiB",
            tgts_per_mshr=12,
            addr_ranges=board.mem_ranges,
        )
        self.iocache.mem_side = self.membus.cpu_side_ports
        self.iocache.cpu_side = board.get_mem_side_coherent_io_port()

    def setLogEn(self, flag):
        self.l1d_caches[0].setOctLoggerEn(flag)
