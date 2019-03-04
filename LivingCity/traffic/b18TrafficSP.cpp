#include "b18TrafficSP.h"

#include <boost/graph/exterior_property.hpp>
#include "src/linux_host_memory_logger.h"
#define ROUTE_DEBUG 0
//#define DEBUG_JOHNSON 0

namespace LC {


////////////////
/////////////////////////////
using namespace boost;

inline bool fileExists(const std::string& fileName) {
  std::ifstream f(fileName.c_str());
  return f.good();
}

typedef exterior_vertex_property<RoadGraph::roadBGLGraph_BI, float>
DistanceProperty;
typedef DistanceProperty::matrix_type DistanceMatrix;
typedef DistanceProperty::matrix_map_type DistanceMatrixMap;

// Convert OD pairs to SP graph format
std::vector<std::array<abm::graph::vertex_t, 2>> B18TrafficSP::make_od_pairs(std::vector<B18TrafficPerson> trafficPersonVec, 
									     int nagents) {
  bool status = true;
  std::vector<std::array<abm::graph::vertex_t, 2>> all_od_pairs_;
  try {
    abm::graph::vertex_t v1, v2;
    abm::graph::weight_t weight;
    for (int person = 0; person < trafficPersonVec.size(); person++) {
      v1 = trafficPersonVec[person].init_intersection;
      v2 = trafficPersonVec[person].end_intersection;
      std::array<abm::graph::vertex_t, 2> od = {v1, v2};
      all_od_pairs_.emplace_back(od);
    }
    if (nagents != std::numeric_limits<int>::max())
      all_od_pairs_.resize(nagents);
  } catch (std::exception& exception) {
    std::cout << "Looping through trafficPersonVec doesn't work " << exception.what() << "\n";
    status = false;
  }
  return all_od_pairs_;
}



std::vector<abm::graph::vertex_t> B18TrafficSP::compute_routes(int mpi_rank,
                                                 int mpi_size,
						 std::shared_ptr<abm::Graph> graph_,
						 std::vector<std::array<abm::graph::vertex_t, 2>> all_od_pairs_) {

  //! All paths
  std::vector<abm::graph::vertex_t> all_paths_;
  //! All paths indices
  std::vector<std::array<abm::graph::vertex_t, 3>> all_paths_idx_;

  std::vector<std::array<abm::graph::vertex_t, 2>> od_pairs;
#ifdef USE_MPI
  // Create MPI pair type
  MPI_Datatype pair_t;
  MPI_Type_vector(2, 1, 1, MPI_LONG_LONG_INT, &pair_t);
  MPI_Type_commit(&pair_t);

  // Calculate chunk size to split router
  int chunk_size = all_od_pairs_.size() / mpi_size;
  MPI_Bcast(&chunk_size, 1, MPI_LONG_LONG_INT, 0, MPI_COMM_WORLD);
  od_pairs.resize(chunk_size);
  // Send route chunks to different compute nodes
  MPI_Scatter(all_od_pairs_.data(), chunk_size, pair_t, od_pairs.data(),
              od_pairs.size(), pair_t, 0, MPI_COMM_WORLD);

  // Calculate the remaining chunk of od_pairs and add to rank 0
  int chunk_remainder = all_od_pairs_.size() % mpi_size;
  if (mpi_rank == 0) {
    od_pairs.insert(od_pairs.begin(), all_od_pairs_.end() - chunk_remainder,
                    all_od_pairs_.end());
  }
#else
  od_pairs = all_od_pairs_;
#endif

  // Paths (vector of edges)
  std::vector<abm::graph::vertex_t> paths;
  paths.reserve(graph_->nedges());

  // Indices of start of path and length for each agent
  std::vector<std::array<abm::graph::vertex_t, 3>> paths_idx;
  paths_idx.reserve(od_pairs.size());

#pragma omp parallel for schedule(dynamic)
  for (abm::graph::vertex_t i = 0; i < od_pairs.size(); ++i) {
    const auto sp = graph_->dijkstra_edges(od_pairs[i][0], od_pairs[i][1]);
#pragma omp critical
    {
      paths_idx.emplace_back(std::array<abm::graph::vertex_t, 3>(
          {i, static_cast<abm::graph::vertex_t>(paths.size()),
           static_cast<abm::graph::vertex_t>(sp.size())}));
      paths.insert(std::end(paths), std::begin(sp), std::end(sp));
    }
  }

  // Get all paths and indices
  all_paths_ = abm::gather_vectors_ids(paths);
  all_paths_idx_ = abm::gather_vector_arrays(paths_idx);

#ifdef USE_MPI
  MPI_Type_free(&pair_t);
#endif

  return all_paths_;
}




void B18TrafficSP::generateRoutesSP(
    LC::RoadGraph::roadBGLGraph_BI &roadGraph,
    std::vector<B18TrafficPerson> &trafficPersonVec,
    std::vector<uint>& indexPathVec,
    std::map<RoadGraph::roadGraphEdgeDesc_BI, uint> &edgeDescToLaneMapNum,
    int weigthMode, float sample) {
  if (trafficPersonVec.empty()) {
    printf("ERROR generateRoutesSP: people empty");
    return;
  }

  printf(">> generatePathRoutes\n");
  QTime timer;
  timer.start();
  
  uint currIndexPath = 0;
  std::vector<uint> oldIndexPathVec = indexPathVec; // std::move(indexPathVec); // avoid copying
  indexPathVec.clear(); // = std::vector<uint>();

  // 1. Update weight edges
  printf(">> generateRoutes Update weight edges\n");
  int numEdges = 0;
  property_map<RoadGraph::roadBGLGraph_BI, float RoadGraphEdge::*>::type
    weight_pmap = boost::get(&RoadGraphEdge::edge_weight, roadGraph);

  RoadGraph::roadGraphEdgeIter_BI ei, eiEnd;
  float minTravelTime = FLT_MAX;
  float maxTravelTime = -FLT_MAX;
  float minLength = FLT_MAX;
  float maxLength = -FLT_MAX;
  float minSpeed = FLT_MAX;
  float maxSpeed = -FLT_MAX;

  //make graph object
  const bool directed = true;
  auto graph = std::make_unique<abm::Graph>(directed);

  for (boost::tie(ei, eiEnd) = boost::edges(roadGraph); ei != eiEnd; ++ei) {
    numEdges++;
    if ((edgeDescToLaneMapNum.size() > 20) && (numEdges % (edgeDescToLaneMapNum.size() / 20)) == 0) {
      printf("Edge %d of %d (%2.0f%%)\n", numEdges, edgeDescToLaneMapNum.size(), (100.0f * numEdges) / edgeDescToLaneMapNum.size());
    }
    if (roadGraph[*ei].numberOfLanes > 0) {
      float speed;
      if (weigthMode == 0 || roadGraph[*ei].averageSpeed.size() <= 0) {
        speed = roadGraph[*ei].maxSpeedMperSec;//(p0-p1).length();

      } else {
        // Use the avarage speed sampled in a former step
        float avOfAv = 0;
        for (int avOfAvN = 0; avOfAvN < roadGraph[*ei].averageSpeed.size(); avOfAvN++) {
          avOfAv += roadGraph[*ei].averageSpeed[avOfAvN];
        }
        speed = avOfAv / roadGraph[*ei].averageSpeed.size();
      }
      float travelTime = roadGraph[*ei].edgeLength / speed;
      roadGraph[*ei].edge_weight = travelTime;

      //printf("Vertex IDs of Edge: %d %d, Edge weight: %f\n", boost::source(*ei, roadGraph), boost::target(*ei, roadGraph), roadGraph[*ei].edge_weight);

      abm::graph::vertex_t source = boost::source(*ei, roadGraph);
      abm::graph::vertex_t target = boost::target(*ei, roadGraph);
      abm::graph::weight_t weight = roadGraph[*ei].edge_weight;
      abm::graph::vertex_t edge_id = numEdges;

      graph->add_edge(source, target, weight, edge_id);

      /*
      //generate ABM graph
      const bool directed = true;
      auto graph = std::make_unique<abm::Graph>(directed);
      graph->generate_simple_graph();
      abm::graph::vertex_t source = 1;
      abm::graph::vertex_t destination = 3;
      const auto path = graph->dijkstra_vertices(source, destination);
      // Check distances
      printf("path size = %d\n", path.size());
      */
    } else {
      roadGraph[*ei].edge_weight =
        100000000.0; //FLT_MAX;// if it has not lines, then the weight is inf
    }
  }


  printf("# of edges: %d\n", graph->nedges());


  /*
  //2. Generate route for each person
  int mpi_rank = 0;
  int mpi_size = 1;
  std::vector<std::array<abm::graph::vertex_t, 2>> all_od_pairs_;
  //all_od_pairs_ = make_od_pairs(trafficPersonVec, std::numeric_limits<int>::max()));
  all_od_pairs_ = LC::B18TrafficSP::make_od_pairs(trafficPersonVec, 10);
  const auto all_paths = LC::B18TrafficSP::compute_routes(mpi_rank, mpi_size, graph, all_od_pairs_);
  //end computing shortest paths with ABM
  */






  printf(">> Printing vertex IDs, edge IDs, and weights\n");
  int numVertex = boost::num_vertices(roadGraph);
  //vertex
  typedef LC::RoadGraph::roadGraphVertexDesc_BI VertexDescriptor;
  typedef std::map<VertexDescriptor, size_t> VertexIndexMap;
  VertexIndexMap mapVertexIndex;
  boost::associative_property_map<VertexIndexMap> pmVertexIndex(mapVertexIndex);

  DistanceMatrix distances(numVertex);
  DistanceMatrixMap dm(distances, roadGraph);



  #ifdef DEBUG_JOHNSON
  std::cerr << "indexPathVec: " << std::endl;
  int i = 0;
  for (const auto x : indexPathVec) {
    std::cerr << i++ << " " << x << " " << std::endl;
  }
  std::cerr << "trafficPersonVec: " << std::endl;
  for (const auto p : trafficPersonVec) {
    std::cerr << p.indexPathInit << " " << p.indexPathCurr << std::endl;
  }
  #endif

}


}  // Closing namespace LC
