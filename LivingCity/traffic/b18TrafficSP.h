/************************************************************************************************
*		@desc Class that finds the path for each person using Johnsons
*		@author igaciad
************************************************************************************************/
#ifndef LC_B18_TRAFFIC_SP_H
#define LC_B18_TRAFFIC_SP_H

#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <queue>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "src/benchmarker.h"
#include "src/linux_host_memory_logger.h"
#include "b18TrafficPerson.h"
#include "../RoadGraph/roadGraph.h"
#include "sp/graph.h"
//#include "sp/external/csv.h"
#include "omp.h"

#include "sp/config.h"
#include "sp/mpi_wrapper.h"

namespace LC {
class B18TrafficSP {

 public:
	 
  static std::vector<abm::graph::vertex_t> compute_routes(int mpi_rank,
                                                          int mpi_size,
                                                          const std::shared_ptr<abm::Graph>& graph_,
                                                          const std::vector<std::array<abm::graph::vertex_t, 2>>& od_pairs);

  static std::vector<std::array<abm::graph::vertex_t, 2>> make_od_pairs(std::vector<B18TrafficPerson> trafficPersonVec,
                                                                        int nagents);

  static std::vector<std::array<abm::graph::vertex_t, 2>> read_od_pairs(const std::string& filename, int nagents);

  static std::vector<float> read_dep_times(const std::string& filename);


  static void convertVector(std::vector<abm::graph::vertex_t> paths_SP, std::vector<uint>& indexPathVec, tsl::robin_map<std::shared_ptr<abm::Graph::Edge>, uint> &edgeDescToLaneMapNumSP, const std::shared_ptr<abm::Graph>& graph_);

  static void filterODByHour(std::vector<std::array<abm::graph::vertex_t, 2>> od_pairs, std::vector<float> dep_times, float start_time, float end_time, std::vector<std::array<abm::graph::vertex_t, 2>> &filtered_od_pairs_, std::vector<float> &filtered_dep_times_);

  static void filterODByHourAndSubgraph(std::vector<std::array<abm::graph::vertex_t, 2>> od_pairs,
                                             std::vector<float> dep_times,
                                             float start_time,
                                             float end_time,
                                             std::vector<std::array<abm::graph::vertex_t, 2>> &filtered_od_pairs_,
                                             std::vector<float> &filtered_dep_times_,
                                             std::map<std::tuple<abm::graph::vertex_t, abm::graph::vertex_t>, std::tuple<abm::graph::vertex_t, abm::graph::vertex_t>> &localToSubMap,
                                             std::map<std::tuple<abm::graph::vertex_t, abm::graph::vertex_t>, std::vector<std::vector<abm::graph::vertex_t>>> &subgraphPathsMap,
                                             std::map<std::tuple<abm::graph::vertex_t, abm::graph::vertex_t>, std::vector<float>> &subgraphRouteShareMap,
                                             std::vector<abm::graph::vertex_t> &reconstructed_all_paths,
                                             std::vector<abm::graph::vertex_t> &LtoS,
                                             std::vector<abm::graph::vertex_t> &StoL);

  static void createTimeMatrix(std::vector<std::vector<abm::graph::vertex_t>> pathsMatrix, std::vector<std::vector<float>>& timeMatrix, const std::shared_ptr<abm::Graph>& graph_);

  static void updateRouteShareMatrix(std::vector<std::vector<float>>& timeMatrix, std::vector<std::vector<float>>& routeShareMatrix);

  static void createTimeMap(std::map<std::tuple<abm::graph::vertex_t, abm::graph::vertex_t>, std::vector<std::vector<abm::graph::vertex_t>>> &subgraphPathsMap, std::map<std::tuple<abm::graph::vertex_t, abm::graph::vertex_t>, std::vector<float>> &subgraphTimeMap, const std::shared_ptr<abm::Graph>& graph_);

  static void updateRouteShareMap(std::map<std::tuple<abm::graph::vertex_t, abm::graph::vertex_t>, std::vector<float>> &subgraphTimeMap, std::map<std::tuple<abm::graph::vertex_t, abm::graph::vertex_t>, std::vector<float>> &subgraphRouteShareMap);

  static void reconstructAllPaths(std::vector<abm::graph::vertex_t> &reconstructed_all_paths, std::tuple<abm::graph::vertex_t, abm::graph::vertex_t> od, std::vector<abm::graph::vertex_t> &LtoS, std::vector<abm::graph::vertex_t> &StoL, std::vector<abm::graph::vertex_t> od_chosen_path);

  explicit B18TrafficSP(const std::shared_ptr<abm::Graph>& graph) : graph_{graph} {};
 private:
  //all od pairs
  std::shared_ptr<std::vector<std::array<abm::graph::vertex_t, 3>>> all_od_pairs_;

  //graph (street network)
  std::shared_ptr<abm::Graph> graph_;

  //all paths
  std::vector<abm::graph::vertex_t> all_paths_;
};
} //namespace LC

#endif  // LC_B18_TRAFFIC_SP_H
