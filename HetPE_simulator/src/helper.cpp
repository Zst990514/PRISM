#include "helper.hpp"
#include "mem.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <list>
#include <string>
#include <tuple>
#include <vector>

#define RECORD_TRACE true

void write_trace_files(const std::string &prefix, int pe_count, mem<int> *main_mem, unsigned long int max_trace_limit)
{
#if RECORD_TRACE
    std::cout << "\n--- Writing Trace Files ---" << std::endl;
    long long total_traces = 0;

    std::string dir_path = prefix.substr(0, prefix.find_last_of("/\\"));
    system(("mkdir -p " + dir_path).c_str());

    for (int i = 0; i < pe_count; ++i)
    {
        long long count = main_mem[i].count_trace('a');
        total_traces += count;
        if (count > 0)
        {
            std::string out_name = prefix + std::to_string(i) + ".trace";
            std::ofstream outfile(out_name, std::ios::out);
            if (outfile)
            {
                main_mem[i].write_file(outfile, max_trace_limit);
            }
            else
            {
                std::cerr << "Error: Could not open file " << out_name << " for writing." << std::endl;
            }
        }
    }
    long long max_trace_num = 0;
    int max_pe_idx = -1;
    for (int i = 0; i < pe_count; ++i)
    {
        if (main_mem[i].count_trace('a') > max_trace_num)
        {
            max_trace_num = main_mem[i].count_trace('a');
            max_pe_idx = i;
        }
    }
    std::cout << "Max PE traces (PE " << max_pe_idx << "): " << max_trace_num << std::endl;
    std::cout << "Total traces: " << total_traces << std::endl;
    std::cout << "---------------------------\n"
              << std::endl;
#endif
}

void load_csr_from_binary(const std::string &filename, int &num_nodes, std::vector<int> &pos, std::vector<int> &neigh)
{
    std::ifstream infile(filename, std::ios::binary);
    if (!infile)
    {
        std::cerr << "Error: Cannot open file for reading: " << filename << std::endl;
        exit(1);
    }

    long long pos_size, neigh_size;
    infile.read(reinterpret_cast<char *>(&num_nodes), sizeof(num_nodes));
    infile.read(reinterpret_cast<char *>(&pos_size), sizeof(pos_size));
    infile.read(reinterpret_cast<char *>(&neigh_size), sizeof(neigh_size));

    pos.resize(pos_size);
    neigh.resize(neigh_size);

    infile.read(reinterpret_cast<char *>(pos.data()), pos_size * sizeof(int));
    infile.read(reinterpret_cast<char *>(neigh.data()), neigh_size * sizeof(int));

    infile.close();
}

void print_memory_statistics(
    long long h2h_bytes,
    const std::vector<int> &HE_CSR_pos, const std::vector<int> &HE_CSR_neigh,
    const std::vector<int> &NHE_CSR_pos, const std::vector<int> &NHE_CSR_neigh)
{
    auto print_mem_usage = [](const std::string &name, size_t bytes)
    {
        double size = bytes;
        std::string unit = "B";
        if (size > 1024)
        {
            size /= 1024;
            unit = "KB";
        }
        if (size > 1024)
        {
            size /= 1024;
            unit = "MB";
        }
        if (size > 1024)
        {
            size /= 1024;
            unit = "GB";
        }
        std::cout << name << " size: " << std::fixed << std::setprecision(2) << size << " " << unit << std::endl;
    };

    long long he_pos_bytes = HE_CSR_pos.size() * sizeof(int);
    long long he_neigh_bytes = HE_CSR_neigh.size() * sizeof(int);
    long long nhe_pos_bytes = NHE_CSR_pos.size() * sizeof(int);
    long long nhe_neigh_bytes = NHE_CSR_neigh.size() * sizeof(int);

    print_mem_usage("H2H_BitArray", h2h_bytes);
    print_mem_usage("HE_pos", he_pos_bytes);
    print_mem_usage("HE_neigh", he_neigh_bytes);
    print_mem_usage("NHE_pos", nhe_pos_bytes);
    print_mem_usage("NHE_neigh", nhe_neigh_bytes);

    long long total_bytes = h2h_bytes + he_pos_bytes + he_neigh_bytes + nhe_pos_bytes + nhe_neigh_bytes;
    double total_size = total_bytes;
    std::string total_unit = "B";
    if (total_size > 1024)
    {
        total_size /= 1024;
        total_unit = "KB";
    }
    if (total_size > 1024)
    {
        total_size /= 1024;
        total_unit = "MB";
    }
    if (total_size > 1024)
    {
        total_size /= 1024;
        total_unit = "GB";
    }
    std::cout << "Total (H2H + HE + NHE) size: " << std::fixed << std::setprecision(2) << total_size << " " << total_unit << std::endl;
}

WorkloadAssignments partition_and_distribute_workload(
    int hubs_count, int num_node,
    const std::vector<int> &HE_CSR_pos, const std::vector<int> &HE_CSR_neigh,
    const std::vector<int> &NHE_CSR_pos, const std::vector<int> &NHE_CSR_neigh,
    int pe_num, int cam_size)
{
    WorkloadAssignments assignments;
    assignments.hub_work_pos_list.resize(pe_num);
    assignments.non_hub_work_pos_list.resize(pe_num);

    std::cout << "\n--- Separated Workload Partitioning ---" << std::endl;

    // --- Phase 1: Distribute Hub nodes via simple round-robin ---
    std::cout << "Distributing " << hubs_count << " hub nodes via round-robin." << std::endl;
    for (int v = 0; v < hubs_count; ++v)
    {
        int target_pe = v % pe_num;
        assignments.hub_work_pos_list[target_pe].push_back({v, v + 1});
    }

    // --- Phase 2: Distribute Non-Hub nodes via PE state machine for load balancing ---
    std::list<std::tuple<long long, long long, long long, long long>> non_hub_workload;
    std::list<std::pair<int, int>> non_hub_workload_pos;

    long long max_directed_edges = std::max(HE_CSR_neigh.size(), NHE_CSR_neigh.size());
    long long real_cam_size = max_directed_edges / pe_num;
    if (real_cam_size > cam_size)
    {
        real_cam_size = cam_size;
    }
    std::cout << "Target chunk size (real_cam_size) for non-hubs: " << real_cam_size << std::endl;

    int cur_node = hubs_count;
    while (cur_node < num_node)
    {
        long long cur_chunk_he_edge_count = 0;
        long long cur_chunk_nhe_edge_count = 0;
        int begin_pos = cur_node;
        int end_pos = cur_node;

        for (int v = cur_node; v < num_node; ++v, ++end_pos)
        {
            long long he_degree = HE_CSR_pos[v + 1] - HE_CSR_pos[v];
            long long nhe_degree = NHE_CSR_pos[v + 1] - NHE_CSR_pos[v];
            if ((std::max(cur_chunk_he_edge_count + he_degree, cur_chunk_nhe_edge_count + nhe_degree) > real_cam_size) && (v > begin_pos))
            {
                break;
            }
            cur_chunk_he_edge_count += he_degree;
            cur_chunk_nhe_edge_count += nhe_degree;
        }

        long long total_chunk_edge_count = cur_chunk_he_edge_count + cur_chunk_nhe_edge_count;
        long long cam_op = 0, bitmap_op = 0;
        for (int v = begin_pos; v < end_pos; ++v)
        {
            long long he_degree = HE_CSR_pos[v + 1] - HE_CSR_pos[v];
            if (he_degree >= 2)
            {
                bitmap_op += (he_degree * (he_degree - 1)) / 2;
            }
            for (int i = NHE_CSR_pos[v]; i < NHE_CSR_pos[v + 1]; ++i)
            {
                int u = NHE_CSR_neigh[i];
                cam_op += (HE_CSR_pos[u + 1] - HE_CSR_pos[u]) + (NHE_CSR_pos[u + 1] - NHE_CSR_pos[u]);
            }
        }
        non_hub_workload.push_back({total_chunk_edge_count, cam_op, bitmap_op, cur_chunk_nhe_edge_count});
        non_hub_workload_pos.push_back({begin_pos, end_pos});
        cur_node = end_pos;
    }
    std::cout << "Partitioned " << num_node - hubs_count << " non-hub nodes into " << non_hub_workload.size() << " chunks." << std::endl;

    // Simple Round-Robin Workload Distribution
    std::vector<long long> work_time(pe_num, 0);
    std::vector<long long> cam_operation(pe_num, 0);
    std::vector<long long> bitmap_operation(pe_num, 0);
    int current_pe_idx = 0;

    while (!non_hub_workload.empty())
    {
        auto &chunk = non_hub_workload.front();

        // Assign chunk to the current PE in a round-robin fashion
        assignments.non_hub_work_pos_list[current_pe_idx].push_back(non_hub_workload_pos.front());

        // Still calculate the precise work_time for post-simulation analysis,
        // even though we are not using it for distribution.
        long long total_edges = std::get<0>(chunk);
        long long cam_searches = std::get<1>(chunk);
        long long bitmap_ops = std::get<2>(chunk);
        long long nhe_edges = std::get<3>(chunk);
        // const double BITMAP_OP_WEIGHT = 7.0;
        const double BITMAP_OP_WEIGHT = 2.0;
        long long analysis_work_time = total_edges * 2 + cam_searches + static_cast<long long>(bitmap_ops * BITMAP_OP_WEIGHT) + nhe_edges * 2;
        work_time[current_pe_idx] += analysis_work_time;

        // Accumulate stats for the assigned PE
        cam_operation[current_pe_idx] += cam_searches;
        bitmap_operation[current_pe_idx] += bitmap_ops;

        // Move to the next PE for the next chunk
        current_pe_idx = (current_pe_idx + 1) % pe_num;

        // Pop the processed chunk
        non_hub_workload.pop_front();
        non_hub_workload_pos.pop_front();
    }

    long long max_work_time = 0, min_work_time = -1, total_cam_ops = 0, total_bitmap_ops = 0;
    int max_pe_idx = 0, min_pe_idx = 0;

    for (int i = 0; i < pe_num; ++i)
    {
        total_cam_ops += cam_operation[i];
        total_bitmap_ops += bitmap_operation[i];
        if (work_time[i] > max_work_time)
        {
            max_work_time = work_time[i];
            max_pe_idx = i;
        }
        if (min_work_time == -1 || work_time[i] < min_work_time)
        {
            min_work_time = work_time[i];
            min_pe_idx = i;
        }
    }
    std::cout << "--- PE Workload Stats (for Non-Hubs) ---" << std::endl;
    std::cout << "Total CAM operations: " << total_cam_ops << std::endl;
    std::cout << "Total Bitmap operations: " << total_bitmap_ops << std::endl;
    std::cout << "Max Work Time PE[" << max_pe_idx << "]: " << max_work_time << " cycles" << std::endl;
    std::cout << "Min Work Time PE[" << min_pe_idx << "]: " << min_work_time << " cycles" << std::endl;
    std::cout << "----------------------------------------" << std::endl;

    assignments.max_pe_work_time = max_work_time;
    assignments.max_work_time_pe_idx = max_pe_idx;
    assignments.pe_work_times = std::move(work_time);

    return assignments;
}
