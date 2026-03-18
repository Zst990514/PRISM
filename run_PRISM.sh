#!/bin/bash
set -e
echo "--- Ensuring project is built ---"
./setup.sh
HUB_PERCENTAGE=${HUB_PERCENTAGE:-"0.01"}
OUTPUT_FOLDER="output/HUB_Node_Percentage_${HUB_PERCENTAGE}"

# List of graphs to process
GRAPHS=("astro")
# GRAPHS=("astro" "Enron" "mico" "YT")
# GRAPHS=("twitter")
# GRAPHS=("LJ")
# GRAPHS=("YT")
# GRAPHS=("orkut")
# GRAPHS=("wiki")

for graph_name in "${GRAPHS[@]}"; do
    echo "Processing graph: ${graph_name}"
    
    LOG_FOLDER="${OUTPUT_FOLDER}/Triangle_counting_details"
    TRACE_FOLDER="${OUTPUT_FOLDER}/trace/${graph_name}"
    STDOUT_FILE="${LOG_FOLDER}/${graph_name}.stdout"

    mkdir -p "$LOG_FOLDER"
    mkdir -p "$TRACE_FOLDER"

    # Ensure a clean log file for each run by removing the old one
    rm -f "$STDOUT_FILE"

    TRACE_PREFIX="${TRACE_FOLDER}/"

    echo "--- Running Triangle Counting for ${graph_name} ---" | tee -a "$STDOUT_FILE"
    
    ./HetPE_simulator/build/triangle_counting "${graph_name}" "${HUB_PERCENTAGE}" "${TRACE_PREFIX}" 2>&1 | tee -a "$STDOUT_FILE"
    
    echo "--- Triangle Counting for ${graph_name} finished. ---" | tee -a "$STDOUT_FILE"
    
    echo "Finished processing $graph_name." | tee -a "$STDOUT_FILE"
    echo "-----------------------------------------------------" | tee -a "$STDOUT_FILE"
done

echo "Finished."
echo "====================================================="
