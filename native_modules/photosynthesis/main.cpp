// system includes
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <map>
#include <chrono>

// synavis includes
#include "Synavis.hpp"
#include "MediaReceiver.hpp"

// CPlantBox includes
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include "visualisation/PlantVisualiser.h"
#include "structural/Plant.h"
#include "structural/MappedOrganism.h"

class TaggedPlantVisualiser : public CPlantBox::PlantVisualiser
{
public:
  int tag = 0;
};

// MPI includes
#include <mpi.h>

// include cstlib for system call on unix
#if defined(__unix__)
#include <cstdlib>
#else
// on windows the system call is called _system
#include <stdlib.h>
#endif

// a trait-based detection on whether something is an iterator
template <typename T>
struct is_iterator
{
  template <typename U, typename = typename std::iterator_traits<U>::iterator_category>
  static std::true_type test(U&&);
  static std::false_type test(...);
  using type = decltype(test(std::declval<T>()));
  static constexpr bool value = type::value;
};



namespace V
{
  // addition of arrays of N size
  template <typename T, std::size_t N>
  static inline std::array<T, N> operator+(const std::array<T, N>& lhs, const std::array<T, N>& rhs)
  {
    std::array<T, N> result;
    std::transform(lhs.begin(), lhs.end(), rhs.begin(), result.begin(), std::plus<T>());
    return result;
  }
  template <typename T, std::size_t N>
  static inline std::array<T, N> operator-(const std::array<T, N>& lhs, const std::array<T, N>& rhs)
  {
    std::array<T, N> result;
    std::transform(lhs.begin(), lhs.end(), rhs.begin(), result.begin(), std::minus<T>());
    return result;
  }

  // element-wise scalar multiplication
  template <typename T, std::size_t N, typename U>
  static inline std::array<T, N> operator*(const std::array<T, N>& lhs, const U& rhs)
  {
    std::array<T, N> result;
    // ensure castable to T
    static_assert(std::is_convertible<U, T>::value, "U must be convertible to T");
    std::transform(lhs.begin(), lhs.end(), result.begin(), [rhs](const T& lhs) { return lhs * static_cast<const T&>(rhs); });
    return result;
  }

  // element-wise scalar division
  template <typename T, std::size_t N, typename U>
  static inline std::array<T, N> operator/(const std::array<T, N>& lhs, const U& rhs)
  {
    std::array<T, N> result;
    // ensure castable to T
    static_assert(std::is_convertible<U, T>::value, "U must be convertible to T");
    std::transform(lhs.begin(), lhs.end(), result.begin(), [rhs](const T& lhs) { return lhs / static_cast<const T&>(rhs); });
    return result;
  }

  using V2 = std::array<double, 2>;
  using V3 = std::array<double, 3>;

}

static std::deque<std::shared_ptr<TaggedPlantVisualiser>> submissionQueue;
// thread lock for submission queue
static std::mutex submissionQueueLock;
// thread semaphore for notification
static std::condition_variable submissionQueueCondition;

// global static start seed based on day/moth/year + hour
auto start_seed = std::chrono::duration_cast<std::chrono::hours>(std::chrono::system_clock::now().time_since_epoch()).count();


class FieldManager
{
public:
  FieldManager(V::V2 FieldSize, int comm_rank, int comm_size,
    std::string parameter_file,
    float seeding_distance = 0.05 /*[m]*/,
    float inter_row_distance = 0.1 /*[m]*/)
  {
    using namespace V;
    // our field size in each dimension
    V2 field_size = FieldSize / comm_size;
    // our field origin in each dimension
    V2 field_origin = { field_size[0] * comm_rank, field_size[1] * (comm_size % comm_rank) };
    // divide the field into rows
    int rows = static_cast<int>(field_size[1] / inter_row_distance);
    // divide the field into columns
    int columns = static_cast<int>(field_size[0] / seeding_distance);
    // our buffer areas are 10% of our field size
    V2 buffer_area = field_size * 0.1;
    field_seed = std::vector<std::vector<int>>(rows, std::vector<int>(columns, 0));

  }

  int get_seed_by_id(int id)
  {
    // fetch num columns
    int columns = field_seed[0].size();
    // make 2D id from input
    int row = id / columns;
    int column = id % columns;
    // return the seed at the given location
    return field_seed[row][column];
  }

  void populate_n(std::size_t n)
  {
    std::vector<std::shared_ptr<CPlantBox::MappedPlant>> plants;
    for (auto i : std::views::iota(0uL, n))
    {
      // create a new plant
      auto plant = std::make_shared<CPlantBox::MappedPlant>();
      plant->readParameters(parameter_file, "plant", true, false);
      // set the plant seed
      plant->setSeed(get_seed_by_id(i));
      // simulate the plant
      plant->simulate(plant_age, false);
      // add the plant to the list
      plants.push_back(plant);
      // make a new visualiser
      auto visualiser = std::make_shared<TaggedPlantVisualiser>();
      visualiser->setPlant(plant);
      visualiser->ComputeGeometryForOrganType(CPlantBox::Organism::OrganTypes::ot_leaf, false);
      visualiser->ComputeGeometryForOrganType(CPlantBox::Organism::OrganTypes::ot_stem, false);
      {
        auto lock = std::unique_lock<std::mutex>(submissionQueueLock);
        submissionQueue.push_back(std::move(visualiser));
        submissionQueueCondition.notify_one();
      }
    }
  }

  void populate(auto lower_x, auto upper_x, auto lower_y, auto upper_y)
  {
    std::vector<std::shared_ptr<CPlantBox::MappedPlant>> plants;
    for(auto x = lower_x; x < upper_x; ++x)
    {
      for(auto y = lower_y; y < upper_y; ++y)
      {
        // create a new plant
        auto plant = std::make_shared<CPlantBox::MappedPlant>();
        plant->readParameters(parameter_file, "plant", true, false);
        // set the plant seed
        plant->setSeed(field_seed[x][y]);
        // simulate the plant
        plant->simulate(plant_age, false);
        // add the plant to the list
        plants.push_back(plant);
        // make a new visualiser
        auto visualiser = std::make_shared<TaggedPlantVisualiser>();
        visualiser->tag = field_seed[x][y];
        visualiser->setPlant(plant);
        visualiser->ComputeGeometryForOrganType(CPlantBox::Organism::OrganTypes::ot_leaf, false);
        visualiser->ComputeGeometryForOrganType(CPlantBox::Organism::OrganTypes::ot_stem, false);
        {
          auto lock = std::unique_lock<std::mutex>(submissionQueueLock);
          submissionQueue.push_back(std::move(visualiser));
          submissionQueueCondition.notify_one();
        }
      }
    }
  }

private:
  std::vector<std::vector<int>> field_seed;
  std::string parameter_file;
  int comm_rank;
  int comm_size;
  int plant_age = 30;
};

int main(int argc, char** argv)
{
  // MPI init
  int rank, size;
  int sig_port = -1;

  Synavis::CommandLineParser parser(argc, argv);
  std::string ip_address = parser.GetArgument("i");
  if (ip_address.empty())
  {
    std::cerr << "No IP address provided" << std::endl;
    std::cout << "Usage: srun [slurm job info] " << argv[0] << " <ip address>" << std::endl;
    return 1;
  }
  else
  {
    // substring to colon
    auto colon = ip_address.find(':');
    if (colon != std::string::npos)
    {
      // get the port
      sig_port = std::stoi(ip_address.substr(colon + 1));
      // get the ip address
      ip_address = ip_address.substr(0, colon);
    }
  }

  // threads per rank
  int threads_per_rank = 1;

  if (parser.HasArgument("cores-per-task"))
  {
    // get the number of cores per task
    threads_per_rank = std::stoi(parser.GetArgument("cores-per-task"));
    // set the number of threads per rank
  }
  else
  {
    // retrieve the number of threads per rank from the environment variable
    char* env_threads_per_rank = std::getenv("OMP_NUM_THREADS");
    if (env_threads_per_rank != NULL)
    {
      threads_per_rank = std::stoi(env_threads_per_rank);
    }
  }

  if(!parser.HasArgument("no-mpi"))
  {
    try
    {
      MPI_Init(NULL, NULL);
      MPI_Comm_rank(MPI_COMM_WORLD, &rank);
      MPI_Comm_size(MPI_COMM_WORLD, &size);
    }
    catch (const std::exception& e)
    {
      std::cerr << "MPI_Init failed: " << e.what() << std::endl;
      std::cout << "Usage: srun [slurm job info] " << argv[0] << " <ip address>" << std::endl;
      return 1;
    }
  }
  else
  {
    rank = 0;
    size = 1;
  }

  std::string parameter_file = parser.GetArgument("-p");


  std::vector<std::shared_ptr<Synavis::WorkerThread>> worker_threads;
  for (auto i : std::views::iota(0, threads_per_rank))
  {
    worker_threads.push_back(std::make_shared<Synavis::WorkerThread>());
  }

  // create a new media receiver
  auto m = std::make_shared<Synavis::DataConnector>();
  m->SetConfig({ {"SignallingIP", ip_address}, {"SignallingPort", sig_port} });
  m->SetTakeFirstStep(true);
  m->Initialize();
  m->StartSignalling();
  m->LockUntilConnected(2000);
  
  m->SendJSON({ {"type","command"},{"name","cam"}, {"camera", "scene"} });
  m->SendJSON({{"type","console"},{"command",""}});

  bool stop = false;

  while(!stop)
  {
    std::unique_lock<std::mutex> lock(submissionQueueLock);
    submissionQueueCondition.wait(lock, [&] { return !submissionQueue.empty(); });
    // get the visualiser
    std::shared_ptr<TaggedPlantVisualiser> visualiser = submissionQueue.front();
    // remove the visualiser from the queue
    submissionQueue.pop_front();
    // if the visualiser is still valid
    if (visualiser)
    {
      // submit the visualiser to the media receiver
      m->SendGeometry(
        visualiser->GetGeometry(),
        visualiser->GetGeometryIndices(),
        "plant" + std::to_string(visualiser->tag),
        visualiser->GetGeometryNormals()
      );
    }
  }

  // MPI finalize
  MPI_Finalize();
  return 0;

}

