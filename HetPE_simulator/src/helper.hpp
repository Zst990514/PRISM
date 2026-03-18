#ifndef HUB_CLAP_HELPER_HPP
#define HUB_CLAP_HELPER_HPP

#include "mem.hpp"
#include <list>
#include <string>
#include <tuple>
#include <vector>

// Struct to hold the separate workload assignments for Hubs and Non-Hubs
struct WorkloadAssignments
{
    std::vector<std::vector<std::pair<int, int>>> hub_work_pos_list;
    std::vector<std::vector<std::pair<int, int>>> non_hub_work_pos_list;
    long long max_pe_work_time;
    int max_work_time_pe_idx;
    std::vector<long long> pe_work_times;
};

// Forward declarations for helper functions
void write_trace_files(const std::string &prefix, int pe_count, mem<int> *main_mem, unsigned long int max_trace_limit);
void load_csr_from_binary(const std::string &filename, int &num_nodes, std::vector<int> &pos, std::vector<int> &neigh);
void print_memory_statistics(
    long long h2h_bytes,
    const std::vector<int> &HE_CSR_pos, const std::vector<int> &HE_CSR_neigh,
    const std::vector<int> &NHE_CSR_pos, const std::vector<int> &NHE_CSR_neigh);

WorkloadAssignments partition_and_distribute_workload(
    int hubs_count, int num_node,
    const std::vector<int> &HE_CSR_pos, const std::vector<int> &HE_CSR_neigh,
    const std::vector<int> &NHE_CSR_pos, const std::vector<int> &NHE_CSR_neigh,
    int pe_num, int cam_size);

std::vector<std::vector<std::pair<int, int>>> distribute_work(int start_v_idx, int end_v_idx, int PE_COUNT);

// Inline helper functions for tracing
inline void trace_csr_pos_read(mem<int> &sim_mem, const std::vector<int> &pos_array, int node_idx, long long offset, unsigned long max_trace_limit)
{
    if (node_idx + 1 < pos_array.size())
    {
        sim_mem.increment_total_access(2);
        sim_mem.autoswitch_track_detail(max_trace_limit);
        sim_mem.add_trace(&pos_array[0] + node_idx, &pos_array[0] + node_idx + 2, offset, 'l');
    }
}

inline void trace_csr_neigh_read(mem<int> &sim_mem, const int *begin, const int *end, long long offset, unsigned long max_trace_limit)
{
    if (begin < end)
    {
        sim_mem.increment_total_access(end - begin);
        sim_mem.autoswitch_track_detail(max_trace_limit);
        sim_mem.add_trace(begin, end, offset, 'l');
    }
}

inline void trace_h2h_read(mem<int> &sim_mem, long long h2h_start_addr, long long h2h_idx, unsigned long max_trace_limit)
{
    sim_mem.increment_total_access(1);
    if (sim_mem.autoswitch_track_detail(max_trace_limit))
    {
        long long sim_addr = h2h_start_addr + h2h_idx / 8;
        sim_mem.add_single_trace(sim_addr, 'l');
    }
}

#endif // HUB_CLAP_HELPER_HPP
