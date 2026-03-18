//
// Created by user on 24-5-2.
//

#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <atomic>
#include "mem.hpp"
#include "helper.hpp"

#define PE_NUM_1 120
#define CAM_SIZE_1 512
#define PE_NUM_2 4 // For large degree nodes, though not used in this logic yet
#define PEs_PER_PU 8
#define NUM_RANKS 16
#define INTER_RANK_BANDWIDTH_GBps 19.2 // From 2400 MT/s * 8 bytes/transfer

int main(int argc, const char *argv[])
{
    auto clock = std::chrono::steady_clock();
    auto total_start_time = clock.now();

    if (argc != 4)
    {
        std::cerr << "Usage: " << argv[0] << " <graph_name> <hub_percentage> <trace_prefix>" << std::endl;
        return 1;
    }
    std::string graph_name = argv[1];
    double hub_percentage = std::stod(argv[2]);
    std::string trace_prefix = argv[3];
    std::string filename = "data/CSR/" + graph_name + ".bin";

    std::cout << "--- HUB-CLAP Triangle Counting Simulator ---" << std::endl;
    std::cout << "Graph: " << graph_name << ", Hub Percentage: " << hub_percentage * 100 << "%" << std::endl;
    std::cout << "------------------------------------------\n"
              << std::endl;

    auto preprocess_start_time = clock.now();

    int num_node;
    std::vector<int> full_csr_pos, full_csr_neigh;
    load_csr_from_binary(filename, num_node, full_csr_pos, full_csr_neigh);

    int hubs_count = static_cast<int>(num_node * hub_percentage);
    std::cout << "Loaded preprocessed graph with " << num_node << " nodes and " << full_csr_neigh.size() << " directed edges." << std::endl;
    std::cout << "Hub count set to " << hubs_count << " nodes." << std::endl;

    std::vector<bool> H2H_BitArray;
    std::vector<int> HE_CSR_pos(num_node + 1, 0), HE_CSR_neigh;
    std::vector<int> NHE_CSR_pos(num_node + 1, 0), NHE_CSR_neigh;

    if (hubs_count > 0)
    {
        long long h2h_size = (long long)hubs_count * (hubs_count - 1) / 2;
        if (h2h_size > 0)
            H2H_BitArray.resize(h2h_size, false);
    }

    for (int v = 0; v < num_node; ++v)
    {
        HE_CSR_pos[v] = HE_CSR_neigh.size();
        NHE_CSR_pos[v] = NHE_CSR_neigh.size();

        for (int i = full_csr_pos[v]; i < full_csr_pos[v + 1]; ++i)
        {
            int u = full_csr_neigh[i];
            bool v_is_hub = v < hubs_count;
            bool u_is_hub = u < hubs_count;

            if (u_is_hub)
            {
                HE_CSR_neigh.push_back(u);
                if (v_is_hub) // v > u is guaranteed by preprocessing
                {
                    long long h2h_idx = (long long)v * (v - 1) / 2 + u;
                    if (h2h_idx < H2H_BitArray.size())
                        H2H_BitArray[h2h_idx] = true;
                }
            }
            else
            {
                NHE_CSR_neigh.push_back(u);
            }
        }
    }
    HE_CSR_pos[num_node] = HE_CSR_neigh.size();
    NHE_CSR_pos[num_node] = NHE_CSR_neigh.size();

    auto preprocess_end_time = clock.now();
    float preprocess_duration = (preprocess_end_time - preprocess_start_time).count() / 1e9;

    std::cout << "\n--- Preprocessing Summary ---" << std::endl;
    long long h2h_set_bits = 0;
    for (bool bit : H2H_BitArray)
        if (bit)
            h2h_set_bits++;

    std::cout << "HE Subgraph Edges: " << HE_CSR_neigh.size() << std::endl;
    std::cout << "NHE Subgraph Edges: " << NHE_CSR_neigh.size() << std::endl;
    std::cout << "H2H Bit Array Edges: " << h2h_set_bits << " / " << H2H_BitArray.size() << std::endl;

    long long h2h_bytes = (H2H_BitArray.empty()) ? 0 : (H2H_BitArray.size() - 1) / 8 + 1;
    print_memory_statistics(h2h_bytes, HE_CSR_pos, HE_CSR_neigh, NHE_CSR_pos, NHE_CSR_neigh);

    std::cout << "Preprocessing Time: " << std::fixed << std::setprecision(4) << preprocess_duration << "s" << std::endl;
    std::cout << "-----------------------------\n"
              << std::endl;

    // --- Simulation Setup ---
    const int PE_COUNT = PE_NUM_1;
    // const unsigned long int MAX_TRACE_LIMIT = 5000000;
    const unsigned long int MAX_TRACE_LIMIT = 10000000;

    // Heuristic for bottleneck calculation:
    // This value estimates how many PU cycles are equivalent to a single DRAM access.
    // Derivation for the current hardware configuration:
    //  - PU Frequency: 400 MHz  => PU Cycle Time = 1 / 400M = 2.5 ns
    //  - DRAM: DDR4-2400 => Base Clock = 1200 MHz => DRAM Cycle Time = 0.833 ns
    //  - A typical random DRAM access latency is ~50 ns (including controller, bus, tRCD, CL).
    //  - Weight = DRAM Latency / PU Cycle Time = 50 ns / 2.5 ns = 20.
    const int AVG_MEM_LATENCY_CYCLE_EQUIVALENT = 20;

    mem<int> max_mem;
    int max_pe_idx = -1;
    size_t max_trace_size = 0;

    long long current_addr = 0;

    auto place_in_memory = [&](size_t num_elements, size_t element_size)
    {
        long long start_addr = current_addr;
        if (start_addr % 64 != 0)
            start_addr += (64 - start_addr % 64);
        current_addr = start_addr + num_elements * element_size;
        return start_addr;
    };

    long long h2h_start_addr = place_in_memory(h2h_bytes, 1);
    long long he_pos_start_addr = place_in_memory(HE_CSR_pos.size(), sizeof(int));
    long long he_neigh_start_addr = place_in_memory(HE_CSR_neigh.size(), sizeof(int));
    long long nhe_pos_start_addr = place_in_memory(NHE_CSR_pos.size(), sizeof(int));
    long long nhe_neigh_start_addr = place_in_memory(NHE_CSR_neigh.size(), sizeof(int));

    long long he_pos_offset = (long long)HE_CSR_pos.data() - he_pos_start_addr;
    long long he_neigh_offset = (long long)HE_CSR_neigh.data() - he_neigh_start_addr;
    long long nhe_pos_offset = (long long)NHE_CSR_pos.data() - nhe_pos_start_addr;
    long long nhe_neigh_offset = (long long)NHE_CSR_neigh.data() - nhe_neigh_start_addr;

    auto assignments = partition_and_distribute_workload(
        hubs_count, num_node, HE_CSR_pos, HE_CSR_neigh, NHE_CSR_pos, NHE_CSR_neigh, PE_NUM_1, CAM_SIZE_1);

    // --- OPTIMIZATION: Predict bottleneck PE and only simulate that one ---
    int predicted_bottleneck_pe_idx = 0;
    if (PE_COUNT > 0)
    {
        for (int i = 1; i < PE_COUNT; ++i)
        {
            if (assignments.pe_work_times[i] > assignments.pe_work_times[predicted_bottleneck_pe_idx])
            {
                predicted_bottleneck_pe_idx = i;
            }
        }
    }
    std::cout << "\n--- OPTIMIZATION ENABLED ---" << std::endl;
    std::cout << "Running full simulation for predicted bottleneck PE #" << predicted_bottleneck_pe_idx << " ONLY." << std::endl;
    std::cout << "NOTE: Triangle counts and communication stats will be incomplete." << std::endl;
    std::cout << "----------------------------\n"
              << std::endl;

    std::cout << "--- Starting Triangle Counting ---" << std::endl;
    auto count_start_time = clock.now();

    std::atomic<long long> HHH_count(0), HHN_count(0), HNN_count(0), NNN_count(0);

    // Store all PE memory traces temporarily to find the true bottleneck later.
    std::vector<mem<int>> all_pe_mem(PE_COUNT);
    std::atomic<long long> total_inter_rank_bytes(0);

    auto get_node_rank = [&](int node_id)
    {
        return node_id % NUM_RANKS;
    };

    const int num_pus = (PE_COUNT + PEs_PER_PU - 1) / PEs_PER_PU;
    if (num_pus < NUM_RANKS)
    {
        std::cout << "Info: Number of PUs (" << num_pus << ") is less than NUM_RANKS (" << NUM_RANKS << "). Some ranks will be idle." << std::endl;
    }

    for (int i_PE = 0; i_PE < PE_COUNT; ++i_PE)
    {
        // --- OPTIMIZATION: Skip non-bottleneck PEs to speed up simulation ---
        if (i_PE != predicted_bottleneck_pe_idx)
        {
            continue;
        }

        int pu_id = i_PE / PEs_PER_PU;
        int pe_rank = pu_id % NUM_RANKS; // PUs are distributed to ranks round-robin
        mem<int> sim_mem;

        // Process HHH triangles from hub workloads
        if (!assignments.hub_work_pos_list[i_PE].empty())
        {
            for (const auto &chunk : assignments.hub_work_pos_list[i_PE])
            {
                for (int v_hub = chunk.first; v_hub < chunk.second; ++v_hub)
                {
                    trace_csr_pos_read(sim_mem, HE_CSR_pos, v_hub, he_pos_offset, MAX_TRACE_LIMIT);
                    const size_t he_v_degree = HE_CSR_pos[v_hub + 1] - HE_CSR_pos[v_hub];
                    if (he_v_degree < 2)
                        continue;

                    const int *he_v_neigh = &HE_CSR_neigh[HE_CSR_pos[v_hub]];
                    trace_csr_neigh_read(sim_mem, he_v_neigh, he_v_neigh + he_v_degree, he_neigh_offset, MAX_TRACE_LIMIT);

                    for (size_t j = 1; j < he_v_degree; ++j)
                    {
                        for (size_t i = 0; i < j; ++i)
                        {
                            int h1 = he_v_neigh[j];
                            int h2 = he_v_neigh[i];
                            if (h1 < h2)
                                std::swap(h1, h2);

                            long long h2h_idx = (long long)h1 * (h1 - 1) / 2 + h2;
                            trace_h2h_read(sim_mem, h2h_start_addr, h2h_idx, MAX_TRACE_LIMIT);
                            if (h2h_idx < H2H_BitArray.size() && H2H_BitArray[h2h_idx])
                            {
                                HHH_count++;
                            }
                        }
                    }
                }
            }
        }

        // Process non-hub workloads (HHN, NNN, HNN)
        if (!assignments.non_hub_work_pos_list[i_PE].empty())
        {
            for (const auto &chunk : assignments.non_hub_work_pos_list[i_PE])
            {
                // HHN (Type 1) triangles
                for (int v_non_hub = chunk.first; v_non_hub < chunk.second; ++v_non_hub)
                {
                    trace_csr_pos_read(sim_mem, HE_CSR_pos, v_non_hub, he_pos_offset, MAX_TRACE_LIMIT);
                    const size_t he_v_degree = HE_CSR_pos[v_non_hub + 1] - HE_CSR_pos[v_non_hub];
                    if (he_v_degree < 2)
                        continue;

                    const int *he_v_neigh = &HE_CSR_neigh[HE_CSR_pos[v_non_hub]];
                    trace_csr_neigh_read(sim_mem, he_v_neigh, he_v_neigh + he_v_degree, he_neigh_offset, MAX_TRACE_LIMIT);

                    for (size_t j = 1; j < he_v_degree; ++j)
                    {
                        for (size_t i = 0; i < j; ++i)
                        {
                            int h1 = he_v_neigh[j];
                            int h2 = he_v_neigh[i];
                            if (h1 < h2)
                                std::swap(h1, h2);

                            long long h2h_idx = (long long)h1 * (h1 - 1) / 2 + h2;
                            trace_h2h_read(sim_mem, h2h_start_addr, h2h_idx, MAX_TRACE_LIMIT);
                            if (h2h_idx < H2H_BitArray.size() && H2H_BitArray[h2h_idx])
                            {
                                HHN_count++;
                            }
                        }
                    }
                }

                // HNN (Type 2) and NNN Counting (CAM-style)
                std::vector<std::pair<int, int>> hnn_cam; // CAM_2 for HNN matching
                std::vector<std::pair<int, int>> nnn_cam; // CAM_1 to drive the search

                int chunk_begin_v = chunk.first;
                int chunk_end_v = chunk.second;

                // --- NNN Counting Phase ---
                const int *nhe_chunk_begin_ptr = &NHE_CSR_neigh[NHE_CSR_pos[chunk_begin_v]];
                const int *nhe_chunk_end_ptr = &NHE_CSR_neigh[NHE_CSR_pos[chunk_end_v]];
                sim_mem.add_trace(&NHE_CSR_pos[0] + chunk_begin_v, &NHE_CSR_pos[0] + chunk_begin_v + 1, nhe_pos_offset, 'l');
                sim_mem.add_trace(&NHE_CSR_pos[0] + chunk_end_v, &NHE_CSR_pos[0] + chunk_end_v + 1, nhe_pos_offset, 'l');
                trace_csr_neigh_read(sim_mem, nhe_chunk_begin_ptr, nhe_chunk_end_ptr, nhe_neigh_offset, MAX_TRACE_LIMIT);

                for (int v = chunk.first; v < chunk.second; ++v)
                {
                    const size_t nhe_v_degree = NHE_CSR_pos[v + 1] - NHE_CSR_pos[v];
                    if (nhe_v_degree > 0)
                    {
                        const int *nhe_v_neigh = &NHE_CSR_neigh[NHE_CSR_pos[v]];
                        for (size_t i = 0; i < nhe_v_degree; ++i)
                        {
                            nnn_cam.push_back({v, nhe_v_neigh[i]});
                        }
                    }
                }

                for (const auto &edge : nnn_cam)
                {
                    int v = edge.first;
                    int u = edge.second;

                    if (get_node_rank(u) != pe_rank)
                    {
                        total_inter_rank_bytes += 2 * sizeof(int); // for pos[u] and pos[u+1]
                    }
                    trace_csr_pos_read(sim_mem, NHE_CSR_pos, u, nhe_pos_offset, MAX_TRACE_LIMIT);
                    const size_t nhe_u_degree = NHE_CSR_pos[u + 1] - NHE_CSR_pos[u];
                    if (nhe_u_degree > 0)
                    {
                        if (get_node_rank(u) != pe_rank)
                        {
                            total_inter_rank_bytes += nhe_u_degree * sizeof(int);
                        }
                        const int *nhe_u_neigh = &NHE_CSR_neigh[NHE_CSR_pos[u]];
                        trace_csr_neigh_read(sim_mem, nhe_u_neigh, nhe_u_neigh + nhe_u_degree, nhe_neigh_offset, MAX_TRACE_LIMIT);
                        for (size_t i = 0; i < nhe_u_degree; ++i)
                        {
                            int n = nhe_u_neigh[i];
                            if (u > n)
                            {
                                if (std::find(nnn_cam.begin(), nnn_cam.end(), std::make_pair(v, n)) != nnn_cam.end())
                                {
                                    NNN_count++;
                                }
                            }
                        }
                    }
                }

                // --- HNN Counting Phase ---
                const int *he_chunk_begin_ptr = &HE_CSR_neigh[HE_CSR_pos[chunk_begin_v]];
                const int *he_chunk_end_ptr = &HE_CSR_neigh[HE_CSR_pos[chunk_end_v]];
                sim_mem.add_trace(&HE_CSR_pos[0] + chunk_begin_v, &HE_CSR_pos[0] + chunk_begin_v + 1, he_pos_offset, 'l');
                sim_mem.add_trace(&HE_CSR_pos[0] + chunk_end_v, &HE_CSR_pos[0] + chunk_end_v + 1, he_pos_offset, 'l');
                trace_csr_neigh_read(sim_mem, he_chunk_begin_ptr, he_chunk_end_ptr, he_neigh_offset, MAX_TRACE_LIMIT);

                for (int v = chunk.first; v < chunk.second; ++v)
                {
                    const size_t he_v_degree = HE_CSR_pos[v + 1] - HE_CSR_pos[v];
                    if (he_v_degree > 0)
                    {
                        const int *he_v_neigh = &HE_CSR_neigh[HE_CSR_pos[v]];
                        for (size_t i = 0; i < he_v_degree; ++i)
                        {
                            hnn_cam.push_back({v, he_v_neigh[i]});
                        }
                    }
                }

                for (const auto &edge : nnn_cam)
                {
                    int v = edge.first;
                    int u = edge.second;
                    if (get_node_rank(u) != pe_rank)
                    {
                        total_inter_rank_bytes += 2 * sizeof(int); // for pos[u] and pos[u+1]
                    }
                    trace_csr_pos_read(sim_mem, HE_CSR_pos, u, he_pos_offset, MAX_TRACE_LIMIT);
                    const size_t he_u_degree = HE_CSR_pos[u + 1] - HE_CSR_pos[u];
                    if (he_u_degree > 0)
                    {
                        if (get_node_rank(u) != pe_rank)
                        {
                            total_inter_rank_bytes += he_u_degree * sizeof(int);
                        }
                        const int *he_u_neigh = &HE_CSR_neigh[HE_CSR_pos[u]];
                        trace_csr_neigh_read(sim_mem, he_u_neigh, he_u_neigh + he_u_degree, he_neigh_offset, MAX_TRACE_LIMIT);
                        for (size_t i = 0; i < he_u_degree; ++i)
                        {
                            int h = he_u_neigh[i];
                            if (std::find(hnn_cam.begin(), hnn_cam.end(), std::make_pair(v, h)) != hnn_cam.end())
                            {
                                HNN_count++;
                            }
                        }
                    }
                }
            }
        }

        // Move the completed mem object into our vector for later analysis.
        all_pe_mem[i_PE] = std::move(sim_mem);
    }

    auto count_end_time = clock.now();
    float count_duration = (count_end_time - count_start_time).count() / 1e9;

    auto total_end_time = clock.now();
    float total_duration = (total_end_time - total_start_time).count() / 1e9;

    // --- Find the Overall Bottleneck PE ---
    int bottleneck_pe_idx = -1;
    long long max_bottleneck_score = -1;

    for (int i_PE = 0; i_PE < PE_COUNT; ++i_PE)
    {
        long long predicted_compute_time = assignments.pe_work_times[i_PE];
        unsigned long long actual_mem_accesses = all_pe_mem[i_PE].get_total_access_count();
        long long estimated_mem_stall_time = actual_mem_accesses * AVG_MEM_LATENCY_CYCLE_EQUIVALENT;
        long long total_bottleneck_score = predicted_compute_time + estimated_mem_stall_time;

        if (total_bottleneck_score > max_bottleneck_score)
        {
            max_bottleneck_score = total_bottleneck_score;
            bottleneck_pe_idx = i_PE;
        }
    }

    std::cout << "\n--- Counting Summary ---" << std::endl;
    std::cout << "Compute time (Wall clock): " << std::fixed << std::setprecision(2) << count_duration << " s" << std::endl;
    std::cout << "Simulated Compute time (Max PE based on workload): " << assignments.max_pe_work_time << " cycles" << std::endl;

    std::cout << "--- Triangle Counts ---" << std::endl;
    std::cout << "HHH: " << HHH_count << std::endl;
    std::cout << "HHN: " << HHN_count << std::endl;
    std::cout << "HNN: " << HNN_count << std::endl;
    std::cout << "NNN: " << NNN_count << std::endl;
    std::cout << "-----------------------" << std::endl;
    long long total_triangles = HHH_count + HHN_count + HNN_count + NNN_count;
    std::cout << "num_triangle: " << total_triangles << std::endl;

    // --- New Communication Overhead Calculation ---
    double communication_time_s = 0;
    if (INTER_RANK_BANDWIDTH_GBps > 0)
    {
        communication_time_s = (double)total_inter_rank_bytes / (INTER_RANK_BANDWIDTH_GBps * 1e9);
    }
    double max_pe_time_s = (double)max_bottleneck_score * (1.0 / 400e6); // PU Freq is 400 MHz
    double total_simulated_time_s = max_pe_time_s + communication_time_s;

    std::cout << "\n--- Inter-Rank Communication Summary ---" << std::endl;
    std::cout << "Total data transferred between ranks: " << std::fixed << std::setprecision(2) << total_inter_rank_bytes / (1024.0 * 1024.0) << " MB" << std::endl;
    std::cout << "Bottleneck PE time (compute + memory): " << std::fixed << std::setprecision(4) << max_pe_time_s << " s" << std::endl;
    std::cout << "Estimated communication time: " << std::fixed << std::setprecision(4) << communication_time_s << " s" << std::endl;
    std::cout << "Total simulated time (PE bottleneck + communication): " << std::fixed << std::setprecision(4) << total_simulated_time_s << " s" << std::endl;
    if (total_simulated_time_s > 0)
    {
        std::cout << "Communication overhead percentage: " << std::fixed << std::setprecision(2) << (communication_time_s / total_simulated_time_s) * 100 << "%" << std::endl;
    }
    std::cout << "--------------------------------------" << std::endl;

    // --- Write Trace File ---
    if (bottleneck_pe_idx != -1)
    {
        std::cout << "\n--- Writing Trace File for Overall Bottleneck PE ---" << std::endl;

        mem<int> &bottleneck_pe_mem = all_pe_mem[bottleneck_pe_idx];
        long long work_time = assignments.pe_work_times[bottleneck_pe_idx];
        unsigned long long total_accesses = bottleneck_pe_mem.get_total_access_count();
        size_t stored_trace_size = bottleneck_pe_mem.count_trace();

        std::cout << "Overall Bottleneck PE (highest compute + memory stall estimate): " << bottleneck_pe_idx << std::endl;
        std::cout << "  - Total Score: " << max_bottleneck_score << " (arbitrary units)" << std::endl;
        std::cout << "  - Component 1 (Predicted Compute Time): " << work_time << " cycles" << std::endl;
        std::cout << "  - Component 2 (Actual Memory Accesses): " << total_accesses << " accesses" << std::endl;
        std::cout << "Trace size to be stored: " << stored_trace_size << " entries." << std::endl;

        std::string output_path = "";
        size_t last_slash_idx = trace_prefix.rfind('/');
        if (std::string::npos != last_slash_idx)
        {
            output_path = trace_prefix.substr(0, last_slash_idx + 1);
        }
        std::string max_trace_filename = output_path + graph_name + "_Max.trace";

        std::ofstream max_trace_file(max_trace_filename);
        if (max_trace_file.is_open())
        {
            bottleneck_pe_mem.write_file(max_trace_file, MAX_TRACE_LIMIT);
            max_trace_file.close();
            std::cout << "Max trace file written to " << max_trace_filename << std::endl;
        }
        else
        {
            std::cerr << "Error: Could not open file " << max_trace_filename << " for writing." << std::endl;
        }
    }
    else
    {
        std::cout << "\nNo traces were generated as bottleneck PE could not be determined." << std::endl;
    }

    return 0;
}
