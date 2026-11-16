#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <limits>
#include <algorithm>
#include "omp.h"
#define MAX_N_THREADS 8

// Read metadata from the `.bin` file
void get_bin_metadata(const std::string &bin_file, size_t &nrows,
                      size_t &ncols) {
  std::ifstream reader(bin_file, std::ios::binary);
  if (!reader) {
    throw std::runtime_error("Cannot open file: " + bin_file);
  }
  int nrows_32, ncols_32;
  reader.read(reinterpret_cast<char *>(&nrows_32), sizeof(int));
  reader.read(reinterpret_cast<char *>(&ncols_32), sizeof(int));
  nrows = static_cast<size_t>(nrows_32);
  ncols = static_cast<size_t>(ncols_32);
}

// Read data from the `.bin` file
template<typename T>
std::vector<std::vector<T>> load_bin_data(const std::string &bin_file) {
  size_t nrows, ncols;
  get_bin_metadata(bin_file, nrows, ncols);
  std::ifstream reader(bin_file, std::ios::binary);
  reader.seekg(2 * sizeof(int), std::ios::beg);  // Skip metadata
  std::vector<std::vector<T>> data(nrows, std::vector<T>(ncols));

  for (size_t i = 0; i < nrows; i++) {
    reader.read(reinterpret_cast<char *>(data[i].data()), ncols * sizeof(T));
  }
  return data;
}

// Compute Euclidean distance
double euclidean_distance(const std::vector<float> &a,
                          const std::vector<float> &b) {
  double sum = 0.0;
  for (size_t i = 0; i < a.size(); i++) {
    double diff = static_cast<float>(a[i]) - static_cast<float>(b[i]);
    sum += diff * diff;
  }
  return std::sqrt(sum);
}

// Generate `ground_truth` and save it as `.bin`
void generate_ground_truth_bin(const std::string &base_file,
                               const std::string &query_file,
                               const std::string &gt_file, int startid,
                               int endid, size_t K) {
  // Read the base and query data
  std::vector<std::vector<float>> base_data = load_bin_data<float>(base_file);
  std::vector<std::vector<float>> query_data = load_bin_data<float>(query_file);

  std::cout << "Read Finish! " << std::endl;
  std::cout << "base:" << base_file << std::endl;
  std::cout << "query:" << query_file << std::endl;

  size_t base_size = base_data.size();
  size_t dim = base_data[0].size();
  size_t query_size = query_data.size();

  // Select the last `fraction` of the base dataset
  // size_t start_idx = static_cast<size_t>(base_size * (1.0 - fraction));
  std::vector<std::vector<float>> search_base(base_data.begin() + startid,
                                              base_data.begin() + endid + 1);

  std::cout << "Select Finish! " << std::endl;
  std::cout << "start_id: " << startid << " end_id: " << endid << std::endl;
  // std::cout << "fraction:" << fraction << std::endl;

  size_t new_base_size = search_base.size();

  // Compute ground_truth
  std::vector<std::vector<int>> ground_truth(query_size, std::vector<int>(K));

  for (size_t i = 0; i < query_size; i++) {
    std::vector<std::pair<double, int>> distances(new_base_size);

#pragma omp parallel for
    for (size_t j = 0; j < new_base_size; j++) {
      double dist = euclidean_distance(query_data[i], search_base[j]);
      distances[j] = std::make_pair(dist, j + startid);  // Record the original index
    }

    // Sort and select the first K nearest neighbors
    std::partial_sort(distances.begin(), distances.begin() + K,
                      distances.end());

    for (size_t k = 0; k < K; k++) {
      ground_truth[i][k] = distances[k].second;
    }

    if (i % 100 == 0) {
      std::cout << "Calculate: " << i << std::endl;
    }
  }

  std::cout << "Calculate ground_truth Finish! " << std::endl;

  // Write ground_truth to a `.bin` file
  std::ofstream writer(gt_file, std::ios::binary);
  if (!writer) {
    throw std::runtime_error("Cannot open output file: " + gt_file);
  }

  int query_size_32 = static_cast<int>(query_size);
  int K_32 = static_cast<int>(K);
  writer.write(reinterpret_cast<char *>(&query_size_32), sizeof(int));
  writer.write(reinterpret_cast<char *>(&K_32), sizeof(int));

  for (const auto &row : ground_truth) {
    writer.write(reinterpret_cast<const char *>(row.data()), K * sizeof(int));
  }

  std::cout << "Write Finish! " << std::endl;
  std::cout << "gt_file:" << gt_file << std::endl;
}

int main(int argc, char **argv) {
  int top_K = 10;  // Also adjust M_LOW and EF in hnsw.h

  std::string base_file = argv[1];
  std::string query_file = argv[2];
  std::string gt_file = argv[3];
  int         start = atoi(argv[4]);
  int         end = atoi(argv[5]);
  generate_ground_truth_bin(base_file, query_file, gt_file, start, end, top_K);
  return 0;
}