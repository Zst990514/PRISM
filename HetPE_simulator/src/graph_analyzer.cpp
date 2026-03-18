//
// Created by user on 24-5-5 for graph analysis.
//

#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include "helper.hpp"

// Function to convert bytes to a readable string (B, KB, MB, GB)
std::string format_bytes(long long bytes)
{
    if (bytes < 0)
        return "Invalid size";
    if (bytes == 0)
        return "0.00 B";

    double size = static_cast<double>(bytes);
    std::string unit = "B";

    if (size >= 1024.0)
    {
        size /= 1024.0;
        unit = "KB";
    }
    if (size >= 1024.0)
    {
        size /= 1024.0;
        unit = "MB";
    }
    if (size >= 1024.0)
    {
        size /= 1024.0;
        unit = "GB";
    }

    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << size << " " << unit;
    return ss.str();
}

// Function to calculate the maximum degree from a CSR pos array
int calculate_max_degree(const std::vector<int> &csr_pos, int num_nodes)
{
    if (num_nodes == 0)
        return 0;
    int max_degree = 0;
    for (int i = 0; i < num_nodes; ++i)
    {
        int degree = csr_pos[i + 1] - csr_pos[i];
        if (degree > max_degree)
        {
            max_degree = degree;
        }
    }
    return max_degree;
}

int main(int argc, const char *argv[])
{
    if (argc != 3)
    {
        std::cerr << "Usage: " << argv[0] << " <graph_name> <hub_percentage>" << std::endl;
        return 1;
    }
    std::string graph_name = argv[1];
    double hub_percentage = std::stod(argv[2]);
    std::string filename = "data/CSR/" + graph_name + ".bin";

    std::cout << "--- Graph Analyzer ---" << std::endl;
    std::cout << "Graph: " << graph_name << ", Hub Percentage: " << hub_percentage * 100 << "%" << std::endl;
    std::cout << "----------------------\n"
              << std::endl;

    // --- 1. Load Graph ---
    int num_node;
    std::vector<int> full_csr_pos, full_csr_neigh;
    load_csr_from_binary(filename, num_node, full_csr_pos, full_csr_neigh);

    int hubs_count = static_cast<int>(num_node * hub_percentage);
    std::cout << "Loaded graph with " << num_node << " nodes and " << full_csr_neigh.size() << " directed edges." << std::endl;
    std::cout << "Hub count set to " << hubs_count << " nodes." << std::endl;

    // --- 2. Calculate Original CSR Storage ---
    long long original_pos_bytes = full_csr_pos.size() * sizeof(int);
    long long original_neigh_bytes = full_csr_neigh.size() * sizeof(int);
    long long original_total_bytes = original_pos_bytes + original_neigh_bytes;
    int original_max_degree = calculate_max_degree(full_csr_pos, num_node);

    std::cout << "\n--- Original CSR_half Storage ---" << std::endl;
    std::cout << "CSR_pos size: " << format_bytes(original_pos_bytes) << std::endl;
    std::cout << "CSR_neigh size: " << format_bytes(original_neigh_bytes) << std::endl;
    std::cout << "Total CSR_half size: " << format_bytes(original_total_bytes) << std::endl;
    std::cout << "Max Degree: " << original_max_degree << std::endl;
    std::cout << "---------------------------------" << std::endl;

    // --- 3. Preprocess into Hub/Non-Hub structures (logic copied from HUB-CLAP) ---
    std::vector<bool> H2H_BitArray;
    std::vector<int> HE_CSR_pos(num_node + 1, 0), HE_CSR_neigh;
    std::vector<int> NHE_CSR_pos(num_node + 1, 0), NHE_CSR_neigh;

    // --- Performance Optimization: Pre-allocate vector capacity to avoid reallocations ---
    HE_CSR_neigh.reserve(full_csr_neigh.size());
    NHE_CSR_neigh.reserve(full_csr_neigh.size());

    if (hubs_count > 0)
    {
        long long h2h_size = (long long)hubs_count * (hubs_count - 1) / 2;
        if (h2h_size > 0)
            H2H_BitArray.resize(h2h_size, false);
    }

    std::cout << "\n--- Starting Preprocessing ---" << std::endl;
    auto preprocess_start_time = std::chrono::steady_clock::now();
    int progress_checkpoint = num_node > 100 ? num_node / 10 : 20;
    if (progress_checkpoint == 0)
        progress_checkpoint = 1;

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
                if (v_is_hub)
                { // v > u is guaranteed by preprocessing
                    int h1 = v;
                    int h2 = u;
                    if (h1 < h2)
                        std::swap(h1, h2); // Ensure h1 > h2 for index calculation
                    long long h2h_idx = (long long)h1 * (h1 - 1) / 2 + h2;
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

    auto preprocess_end_time = std::chrono::steady_clock::now();
    float preprocess_duration = (preprocess_end_time - preprocess_start_time).count() / 1e9;
    std::cout << "--- Preprocessing Finished in " << std::fixed << std::setprecision(4) << preprocess_duration << "s ---" << std::endl;

    // --- 4. Print Preprocessing Storage Summary ---
    long long h2h_set_bits = 0;
    for (bool bit : H2H_BitArray)
        if (bit)
            h2h_set_bits++;

    int he_max_degree = calculate_max_degree(HE_CSR_pos, num_node);
    int nhe_max_degree = calculate_max_degree(NHE_CSR_pos, num_node);

    std::cout << "\n--- Preprocessing Storage Summary ---" << std::endl;
    std::cout << "HE Subgraph Edges: " << HE_CSR_neigh.size() << std::endl;
    std::cout << "NHE Subgraph Edges: " << NHE_CSR_neigh.size() << std::endl;
    std::cout << "H2H Bit Array Edges: " << h2h_set_bits << " / " << H2H_BitArray.size() << std::endl;
    std::cout << "Max HE Degree: " << he_max_degree << std::endl;
    std::cout << "Max NHE Degree: " << nhe_max_degree << std::endl;

    long long h2h_bytes = (H2H_BitArray.empty()) ? 0 : (H2H_BitArray.size() - 1) / 8 + 1;
    long long he_pos_bytes = HE_CSR_pos.size() * sizeof(int);
    long long he_neigh_bytes = HE_CSR_neigh.size() * sizeof(int);
    long long nhe_pos_bytes = NHE_CSR_pos.size() * sizeof(int);
    long long nhe_neigh_bytes = NHE_CSR_neigh.size() * sizeof(int);
    long long total_new_bytes = h2h_bytes + he_pos_bytes + he_neigh_bytes + nhe_pos_bytes + nhe_neigh_bytes;

    std::cout << "H2H_BitArray size: " << format_bytes(h2h_bytes) << std::endl;
    std::cout << "HE_pos size: " << format_bytes(he_pos_bytes) << std::endl;
    std::cout << "HE_neigh size: " << format_bytes(he_neigh_bytes) << std::endl;
    std::cout << "NHE_pos size: " << format_bytes(nhe_pos_bytes) << std::endl;
    std::cout << "NHE_neigh size: " << format_bytes(nhe_neigh_bytes) << std::endl;
    std::cout << "Total (H2H + HE + NHE) size: " << format_bytes(total_new_bytes) << std::endl;
    if (original_total_bytes > 0)
    {
        double storage_ratio = static_cast<double>(total_new_bytes) / original_total_bytes;
        std::cout << "Storage Overhead Ratio (HUB-CLAP/CSR): " << std::fixed << std::setprecision(2) << storage_ratio << std::endl;
    }
    std::cout << "-----------------------------------" << std::endl;

    // --- 5. Calculate and Print Relative Density ---
    std::cout << "\n--- Relative Density Analysis (RD_H) ---" << std::endl;
    double full_graph_edges = full_csr_neigh.size();
    double full_graph_nodes = num_node;

    double hub_subgraph_edges = h2h_set_bits;
    double hub_subgraph_nodes = hubs_count;

    if (full_graph_nodes == 0 || hub_subgraph_nodes == 0)
    {
        std::cout << "Graph or hub subgraph is empty, cannot compute density." << std::endl;
    }
    else
    {
        double density_full_graph = full_graph_edges / (full_graph_nodes * full_graph_nodes);
        double density_hub_subgraph = hub_subgraph_edges / (hub_subgraph_nodes * hub_subgraph_nodes);
        double completeness_hub_subgraph = 0.0;
        if (hub_subgraph_nodes > 1)
        {
            long long max_possible_dag_edges = (long long)hub_subgraph_nodes * (hub_subgraph_nodes - 1) / 2;
            if (max_possible_dag_edges > 0)
            {
                completeness_hub_subgraph = static_cast<double>(hub_subgraph_edges) / max_possible_dag_edges;
            }
        }

        std::cout << "Hub Subgraph Standard Density (E/V^2): " << std::fixed << std::setprecision(3) << (density_hub_subgraph * 100.0) << "%" << std::endl;
        std::cout << "Hub Subgraph Completeness (E/Max_E_DAG): " << std::fixed << std::setprecision(3) << (completeness_hub_subgraph * 100.0) << "%" << std::endl;

        if (density_full_graph == 0)
        {
            std::cout << "Full graph has zero density, cannot compute relative density." << std::endl;
        }
        else
        {
            double rd_h = density_hub_subgraph / density_full_graph;
            std::cout << "RD_H (Hub/Full Graph): " << std::fixed << std::setprecision(2) << rd_h << std::endl;
        }
    }
    std::cout << "----------------------------------------" << std::endl;

    return 0;
}
