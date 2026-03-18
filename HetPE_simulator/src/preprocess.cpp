//
// Created by user on 24-5-8.
//
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <set>
#include <map>

void save_csr_to_binary(const std::string &filename, int num_nodes, const std::vector<int> &pos, const std::vector<int> &neigh)
{
    std::ofstream outfile(filename, std::ios::binary);
    if (!outfile)
    {
        std::cerr << "Error: Cannot open file for writing: " << filename << std::endl;
        return;
    }

    // Write metadata: num_nodes, pos_size, neigh_size
    long long pos_size = pos.size();
    long long neigh_size = neigh.size();
    outfile.write(reinterpret_cast<const char *>(&num_nodes), sizeof(num_nodes));
    outfile.write(reinterpret_cast<const char *>(&pos_size), sizeof(pos_size));
    outfile.write(reinterpret_cast<const char *>(&neigh_size), sizeof(neigh_size));

    // Write data
    outfile.write(reinterpret_cast<const char *>(pos.data()), pos.size() * sizeof(int));
    outfile.write(reinterpret_cast<const char *>(neigh.data()), neigh.size() * sizeof(int));

    outfile.close();
    std::cout << "Successfully saved preprocessed graph to " << filename << std::endl;
}

int main(int argc, const char *argv[])
{
    if (argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " <graph_name>" << std::endl;
        return 1;
    }
    std::string graph_name = argv[1];
    std::string input_filename = "data/edge_list/random/" + graph_name + ".txt";
    std::string output_filename = "data/CSR/" + graph_name + ".bin";

    std::cout << "--- Starting Preprocessing for graph: " << graph_name << " ---" << std::endl;

    auto preprocess_start_time = std::chrono::steady_clock::now();

    // 1. Read Edge List
    std::vector<std::pair<int, int>> edge_list;
    int max_node_id = 0;
    std::ifstream infile(input_filename);
    if (!infile)
    {
        std::cerr << "Error: Cannot open file: " << input_filename << std::endl;
        return 1;
    }
    std::string line;
    while (std::getline(infile, line))
    {
        if (line.empty() || line[0] == '#' || line[0] == '%')
            continue;
        std::istringstream iss(line);
        int u, v;
        if (!(iss >> u >> v))
            continue;
        if (u == v)
            continue; // Skip self-loops
        edge_list.push_back({u, v});
        max_node_id = std::max({max_node_id, u, v});
    }
    infile.close();
    int num_node = max_node_id + 1;
    std::cout << "Read " << edge_list.size() << " raw edges. Num nodes = " << num_node << "." << std::endl;

    // 2. Degree-based Relabeling
    std::vector<int> degrees(num_node, 0);
    for (const auto &edge : edge_list)
    {
        degrees[edge.first]++;
        degrees[edge.second]++;
    }

    std::vector<std::pair<int, int>> sorted_degrees(num_node);
    for (int i = 0; i < num_node; ++i)
    {
        sorted_degrees[i] = {degrees[i], i};
    }
    std::sort(sorted_degrees.begin(), sorted_degrees.end(), [](const auto &a, const auto &b)
              {
                  if (a.first != b.first)
                      return a.first > b.first; // Degree descending
                  return a.second < b.second;   // Original ID ascending (tie-breaker)
              });

    std::vector<int> relabel_map(num_node);
    for (int i = 0; i < num_node; ++i)
    {
        relabel_map[sorted_degrees[i].second] = i;
    }

    // 3. Build Directed Graph (DAG) Adjacency List
    std::vector<std::vector<int>> dag_adj(num_node);
    for (const auto &edge : edge_list)
    {
        int u_new = relabel_map[edge.first];
        int v_new = relabel_map[edge.second];
        if (u_new > v_new)
        {
            dag_adj[u_new].push_back(v_new);
        }
        else if (v_new > u_new)
        {
            dag_adj[v_new].push_back(u_new);
        }
    }
    edge_list.clear(); // Free memory

    // 4. Convert to CSR format
    std::vector<int> csr_pos(num_node + 1, 0);
    std::vector<int> csr_neigh;
    long long edge_count = 0;
    for (int i = 0; i < num_node; ++i)
    {
        // Sort and remove duplicates
        std::sort(dag_adj[i].begin(), dag_adj[i].end());
        dag_adj[i].erase(std::unique(dag_adj[i].begin(), dag_adj[i].end()), dag_adj[i].end());

        csr_pos[i] = edge_count;
        csr_neigh.insert(csr_neigh.end(), dag_adj[i].begin(), dag_adj[i].end());
        edge_count += dag_adj[i].size();
    }
    csr_pos[num_node] = edge_count;

    // 5. Save CSR to binary file
    save_csr_to_binary(output_filename, num_node, csr_pos, csr_neigh);

    auto preprocess_end_time = std::chrono::steady_clock::now();
    float preprocess_duration = (preprocess_end_time - preprocess_start_time).count() / 1e9;
    std::cout << "Preprocessing finished in " << std::fixed << std::setprecision(4) << preprocess_duration << "s." << std::endl;
    std::cout << "--- Preprocessing for " << graph_name << " complete. ---" << std::endl;

    return 0;
}
