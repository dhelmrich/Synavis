// system includes
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <map>
#include <chrono>
#include <sstream>

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

static const Synavis::Logger::LoggerInstance lmain = Synavis::Logger::Get()->LogStarter("main");


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

  template < typename T, typename Q, std::size_t N >
  std::array<T, N> As(const std::array<Q, N>& arr)
  {
    std::array<T, N> result;
    std::transform(arr.begin(), arr.end(), result.begin(), [](const Q& val) { return static_cast<T>(val); });
    return result;
  }

  template < typename T, std::size_t N, typename Q >
  std::array<T, N> As(const std::initializer_list<Q>& arr)
  {
    std::array<T, N> result;
    std::transform(arr.begin(), arr.end(), result.begin(), [](const Q& val) { return static_cast<T>(val); });
    return result;
  }

  // element wise multiplication (function only)
  template < typename T, std::size_t N >
  std::array<T, N> Dot(const std::array<T, N>& lhs, const std::array<T, N>& rhs)
  {
    std::array<T, N> result;
    std::transform(lhs.begin(), lhs.end(), rhs.begin(), result.begin(), std::multiplies<T>());
    return result;
  }

  // function to pad an array to a given size
  template < typename T, std::size_t N, std::size_t M >
  std::array<T, N> Pad(const std::array<T, M>& arr, const T& pad_value)
  {
    static_assert(N >= M, "N must be greater than or equal to M");
    std::array<T, N> result;
    std::fill(result.begin(), result.end(), pad_value);
    std::copy(arr.begin(), arr.end(), result.begin());
    return result;
  }
}

void wait_for_key(std::string message = "Press any key to continue...")
{
  std::cout << message;
  std::cin.get();
}

void write_visualiser_to_file(std::shared_ptr<TaggedPlantVisualiser> visualiser, std::string filename)
{
  std::ofstream file(filename, std::ios::binary);
  file.flush();
  // write number of vertices
  uint64_t num_vertices = visualiser->GetGeometry().size() / 3uL;
  uint64_t byte_size_total = Synavis::ByteSize(visualiser->GetGeometry()) + sizeof(uint64_t);
  //wait_for_key("About to write the number of vertices");
  file.write(reinterpret_cast<const char*>(&num_vertices), sizeof(uint64_t));
  file.flush();
  //wait_for_key("About to write the vertices");
  // write vertices
  file.write(reinterpret_cast<const char*>(visualiser->GetGeometry().data()), Synavis::ByteSize(visualiser->GetGeometry()));
  file.flush();
  //wait_for_key("About to write the number of indices");
  lmain(Synavis::ELogVerbosity::Debug) << "Byte size of vertices: " << Synavis::ByteSize(visualiser->GetGeometry()) << std::endl;
  // write number of indices
  uint64_t num_indices = visualiser->GetGeometryIndices().size();
  byte_size_total += num_indices * sizeof(unsigned int) + sizeof(uint64_t);
  file.write(reinterpret_cast<const char*>(&num_indices), sizeof(uint64_t));
  file.flush();
  //wait_for_key("About to write the indices");
  // write indices
  file.write(reinterpret_cast<const char*>(visualiser->GetGeometryIndices().data()), Synavis::ByteSize(visualiser->GetGeometryIndices()));
  file.flush();
  lmain(Synavis::ELogVerbosity::Debug) << "Byte size of indices: " << Synavis::ByteSize(visualiser->GetGeometryIndices()) << std::endl;
  
  // write number of normals
  uint64_t num_normals = visualiser->GetGeometryNormals().size() / 3uL;
  byte_size_total += num_normals * sizeof(double) * 3uL + sizeof(uint64_t);
  //wait_for_key("About to write the number of normals");
  file.write(reinterpret_cast<const char*>(&num_normals), sizeof(uint64_t));
  file.flush();
  //wait_for_key("About to write the normals");
  // write normals
  file.write(reinterpret_cast<const char*>(visualiser->GetGeometryNormals().data()), Synavis::ByteSize(visualiser->GetGeometryNormals()));
  file.flush();
  lmain(Synavis::ELogVerbosity::Debug) << "Byte size of normals: " << Synavis::ByteSize(visualiser->GetGeometryNormals()) << std::endl;
  // write number of ucs
  uint64_t num_ucs = visualiser->GetGeometryColors().size() / 2uL;
  byte_size_total += num_ucs * sizeof(double) * 2uL + sizeof(uint64_t);
  //wait_for_key("About to write the number of ucs");
  file.write(reinterpret_cast<const char*>(&num_ucs), sizeof(uint64_t));
  file.flush();
  //wait_for_key("About to write the ucs");
  // write ucs
  file.write(reinterpret_cast<const char*>(visualiser->GetGeometryColors().data()), Synavis::ByteSize(visualiser->GetGeometryColors()));
  file.flush();
  //wait_for_key();
  lmain(Synavis::ELogVerbosity::Debug) << "Byte size of ucs: " << Synavis::ByteSize(visualiser->GetGeometryColors()) << std::endl;
  // close the file
  //std::vector<char> end;
  //end.resize(1000000);
  //for(auto& c : end)
  //{
  //  c = 'a';
  //}
  //file.write("Hellotestitest1", 15);
  //lmain(Synavis::ELogVerbosity::Debug) << "Byte size of end: " << end.size() << std::endl;
  //file.write(end.data(), end.size());
  //file.write("Hellotestitest1", 15);
  if(file.bad())
  {
    lmain(Synavis::ELogVerbosity::Error) << "Failed to write to file: " << filename << std::endl;
    return;
  }
  file.close();
  lmain(Synavis::ELogVerbosity::Info) << "Wrote visualiser to file: " << filename << std::endl;
  lmain(Synavis::ELogVerbosity::Info) << "Vertices: " << num_vertices << std::endl;
  lmain(Synavis::ELogVerbosity::Info) << "Indices: " << num_indices << std::endl;
  lmain(Synavis::ELogVerbosity::Info) << "Normals: " << num_normals << std::endl;
  lmain(Synavis::ELogVerbosity::Info) << "UCS: " << num_ucs << std::endl;
  lmain(Synavis::ELogVerbosity::Info) << "Reference size is: " << sizeof(double) * 3uL << " resulting in " << byte_size_total / 1024uL << "kb" << std::endl;
  lmain(Synavis::ELogVerbosity::Info) << "10 Vectors, evenly spaced through GetGeometry(): " << std::endl;
  auto num_points = visualiser->GetGeometryNormals().size() / 3uL;
  auto step = num_points / 10uL;
  for (auto i = 0; i < num_points; i += step)
  {
    lmain(Synavis::ELogVerbosity::Info) << "Point " << i << ": " << visualiser->GetGeometryNormals()[i * 3uL] << ", " << visualiser->GetGeometryNormals()[i * 3uL + 1uL] << ", " << visualiser->GetGeometryNormals()[i * 3uL + 2uL] << std::endl;
  }
  
}


static std::deque<std::shared_ptr<TaggedPlantVisualiser>> submissionQueue;
// thread lock for submission queue
static std::mutex submissionQueueLock;
// thread semaphore for notification
static std::condition_variable submissionQueueCondition;

// global static start seed based on day/moth/year + hour
auto start_seed = std::chrono::duration_cast<std::chrono::hours>(
  std::chrono::system_clock::now().time_since_epoch()).count();


class FieldManager
{

public:
  FieldManager(V::V2 FieldSize, int comm_rank, int comm_size,
    std::string parameter_file,
    float seeding_distance = 0.05 /*[m]*/,
    float inter_row_distance = 0.1 /*[m]*/)
      : comm_rank(comm_rank), comm_size(comm_size), parameter_file(parameter_file)
  {
    using namespace V;
    // our field size in each dimension
    this->field_size = FieldSize / comm_size;
    plant_offset = { inter_row_distance, seeding_distance };
    // our field origin in each dimension
    V2 field_origin = { field_size[0] * comm_rank, field_size[1] * (comm_size % (comm_rank + 1)) };
    // divide the field into rows
    int rows = static_cast<int>(field_size[1] / inter_row_distance);
    // divide the field into columns
    int columns = static_cast<int>(field_size[0] / seeding_distance);
    // our buffer areas are 10% of our field size
    V2 buffer_area = field_size * 0.1;
    // compute local field bounds
    // we assume comm_size to be a square number
    local_field_bounds[0] = field_origin[0] + buffer_area[0];
    local_field_bounds[1] = field_origin[0] + field_size[0] - buffer_area[0];
    local_field_bounds[2] = field_origin[1] + buffer_area[1];
    local_field_bounds[3] = field_origin[1] + field_size[1] - buffer_area[1];
    
  }

  bool ScaleResolutionByRank{ false };

  int get_seed_by_id(int id)
  {
    // fetch num columns
    int columns = field_size[0] / plant_offset[0];
    // make 2D id from input
    int row = id / columns;
    int column = id % columns;
    // return the seed at the given location
    return start_seed + row * columns + column;
  }

  int get_seed_by_position(double x, double y)
  {
    // fetch num columns
    int columns = field_size[0] / plant_offset[0];
    // make 2D id from input
    int row = static_cast<int>(x / plant_offset[0]);
    int column = static_cast<int>(y / plant_offset[1]);
    // return the seed at the given location
    return start_seed + row * columns + column;
  }

  std::shared_ptr<TaggedPlantVisualiser> generate_(std::size_t position)
  {
    bool verbose_plant = Synavis::Logger::Get()->GetVerbosity() >= Synavis::ELogVerbosity::Debug;
    auto plant = std::make_shared<CPlantBox::MappedPlant>();
    plant->readParameters(parameter_file, "plant", true, verbose_plant);
    plant->setSeed(get_seed_by_id(position));
    auto rd = plant->getOrganRandomParameter(CPlantBox::Organism::OrganTypes::ot_seed);
    for (auto& r : rd)
    {
      auto seed_parameter = std::dynamic_pointer_cast<CPlantBox::SeedRandomParameter>(r);
      const auto id_pos = get_position_from_num(position);
      seed_parameter->seedPos = { id_pos[0], id_pos[1], 0.0 };
    }
    for(auto p : plant->getOrganRandomParameter(CPlantBox::Organism::OrganTypes::ot_leaf))
    {
      auto leaf = std::dynamic_pointer_cast<CPlantBox::LeafRandomParameter>(p);
      leaf->leafGeometryPhi = {-90.0, -80.0, -45.0, 0.0, 45.0, 90.0};
      // transform leafGeometryPhi to radians
      std::transform(leaf->leafGeometryPhi.begin(), leaf->leafGeometryPhi.end(), leaf->leafGeometryPhi.begin(), [](auto val) { return val * M_PI / 180.0; });
      leaf->leafGeometryX = {38.41053981,1.0 ,1.0, 0.3, 1.0, 38.41053981};
      leaf->createLeafRadialGeometry(leaf->leafGeometryPhi, leaf->leafGeometryX, 20);
    }
    plant->initialize(verbose_plant, true); // initialize with stochastic = true
    plant->simulate(plant_age, verbose_plant);
    auto visualiser = std::make_shared<TaggedPlantVisualiser>();
    visualiser->tag = get_seed_by_id(position);
    visualiser->setPlant(plant);
    if (ScaleResolutionByRank)
    {
      visualiser->SetLeafResolution(20 * (comm_rank + 1));
      visualiser->SetGeometryResolution(8 * (comm_rank + 1));
    }
    visualiser->ComputeGeometryForOrganType(CPlantBox::Organism::OrganTypes::ot_leaf, false);
    visualiser->ComputeGeometryForOrganType(CPlantBox::Organism::OrganTypes::ot_stem, false);
    return visualiser;
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
      auto rd = plant->getOrganRandomParameter(CPlantBox::Organism::OrganTypes::ot_seed);
      for (auto& r : rd)
      {
        auto seed_parameter = std::dynamic_pointer_cast<CPlantBox::SeedRandomParameter>(r);
        const auto id_pos = get_position_from_num(i);
        seed_parameter->seedPos = { id_pos[0], id_pos[1], 0.0 };
      }
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

  void set_parameter_file(std::string parameter_file)
  {
    this->parameter_file = parameter_file;
  }

  void populate(auto lower_x, auto upper_x, auto lower_y, auto upper_y)
  {
    std::vector<std::shared_ptr<CPlantBox::MappedPlant>> plants;
    for (auto x = lower_x; x < upper_x; ++x)
    {
      for (auto y = lower_y; y < upper_y; ++y)
      {
        // create a new plant
        auto plant = std::make_shared<CPlantBox::MappedPlant>();
        plant->readParameters(parameter_file, "plant", true, false);
        // set the plant seed
        
        // simulate the plant
        plant->simulate(plant_age, false);
        // add the plant to the list
        plants.push_back(plant);
        // make a new visualiser
        auto visualiser = std::make_shared<TaggedPlantVisualiser>();
        visualiser->tag = get_seed_by_position(x, y);
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

  std::array<double, 2> get_position_from_num(int num)
  {
    using namespace V;
    // fetch num columns
    int columns = field_size[0] / plant_offset[0];
    // make 2D id from input
    int row = num / columns;
    int column = num % columns;
    // return the seed at the given location
    return Dot(As<double, 2>({ row , column }), plant_offset);
  }

private:
  std::string parameter_file;
  int comm_rank;
  int comm_size;
  int plant_age = 12;
  std::array<double, 2> field_size;
  std::array<double, 2> plant_offset;
  std::array<double, 4> local_field_bounds;
};

#ifdef __unix__
int GPU_Usage()
{
  auto command = "nvidia-smi --format=csv,noheader --query-gpu=utilization.gpu";
  char buffer[128];
  std::string result;
  FILE* pipe = popen(command, "r");
  if (!pipe)
  {
       throw std::runtime_error("popen() failed!");
  }
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
  {
    result += buffer;
  }
  // result are 4 entries, we want the largest
  std::vector<int> values;
  std::stringstream ss(result);
  std::string token;
  while (std::getline(ss, token))
  {
    values.push_back(std::stoi(token));
  }
  pclose(pipe);
  return *std::max_element(values.begin(), values.end());
}
#endif

void scalability_test(std::shared_ptr<Synavis::DataConnector> m, std::shared_ptr<FieldManager> field_manager, auto rank = -1, auto size = -1, std::string tempf = "./", bool useFile = false, int delay = 100)
{
  using namespace std::literals::chrono_literals;
  field_manager->ScaleResolutionByRank = true;
  // ensure tempf ends with a slash
  if (tempf.back() != '/') tempf += "/";
  std::chrono::system_clock::time_point start_t = std::chrono::system_clock::now();
  // sample fps
  std::size_t w = 0;
  auto file = Synavis::OpenUniqueFile("scalability_test.csv");
#ifdef WIN32
  file << "time;frametime;num" << std::endl;
#else defined __unix__
  file << "time;frametime;num;gpu" << std::endl;
#endif
  auto log_fsp = [&file, &w, start_t](auto message)
    {
      auto jsonmessage = nlohmann::json::parse(message);
      if (!jsonmessage.contains("frametime")) return;
      lmain(Synavis::ELogVerbosity::Verbose) << "frametime: " << jsonmessage["frametime"] << std::endl;
      double fps = jsonmessage["frametime"];
      auto time = Synavis::TimeSince(start_t);
#ifdef WIN32
      file << time << ";" << fps << ";" << w << std::endl;
#else defined __unix__
    file << time << ";" << fps << ";" << w << ";" << GPU_Usage() << std::endl;
#endif
    };
  m->SetMessageCallback(log_fsp);
  lmain(Synavis::ELogVerbosity::Info) << "Scalability test started" << std::endl;
  while (true)
  {
    // another plant
    std::shared_ptr<TaggedPlantVisualiser> vis = field_manager->generate_(w++);
    vis->tag = static_cast<int>(w);
    if (useFile)
    {
      auto filename = tempf + "plant" + std::to_string(vis->tag) + ".bin";
      write_visualiser_to_file(vis, filename);
      m->SendJSON({ {"type","filegeometry"}, {"filename",filename} });
    }
    else
    {
      auto vertices = vis->GetGeometry();
      auto position = V::Pad<double,3>(field_manager->get_position_from_num(w), 0.0);
      std::size_t i = 0;
      std::transform(vertices.begin(), vertices.end(), vertices.begin(), [position,&i](double val) { return val + position[i++ % 3]; });
      m->SendGeometry(
        vertices,
        vis->GetGeometryIndices(),
        "plant" + std::to_string(vis->tag),
        vis->GetGeometryNormals()
      );
    }
    // let the thread sleep for 10 seconds
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
  }
}

void field_population(auto m, auto field_manager, auto& worker_threads, auto threads_per_rank, auto plants_per_thread)
{
  // make tasks for each thread to populate the field
  for (auto i : std::views::iota(0, threads_per_rank))
  {
    worker_threads[i]->AddTask([field_manager, plants_per_thread]() {
      // populate the field
      field_manager->populate_n(plants_per_thread);
      });
  }

  bool stop = false;

  while (!stop)
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
}


void response_concurrency(std::shared_ptr<Synavis::DataConnector> m, auto field_manager, auto rank = -1, auto size = -1, std::string tempf = "./", bool useFile = false, int delay = 100)
{
  field_manager->ScaleResolutionByRank = true;
  // ensure tempf ends with a slash
  if (tempf.back() != '/') tempf += "/";
  std::size_t w = 0;
  auto file = Synavis::OpenUniqueFile("concurrency_test.csv");
  // time stamp of reception, processing time (round trip), number of plants
  file << "time;processing_time;num" << std::endl;
  auto log_response = [&file, &w, start_t](auto message)
  {
    auto jsonmessage = nlohmann::json::parse(message);
    if(!jsonmessage.contains("processed_time")) return;
    auto processed_time = jsonmessage["processed_time"].get<double>();
    double current_time = Synavis::HighRes();
    file << current_time - processed_time << ";" << processed_time << ";" << w << std::endl;
  };
  m->SetMessageCallback(log_response);
  lmain(Synavis::ELogVerbosity::Info) << "Concurrency test started" << std::endl;
  std::shared_ptr<TaggedPlantVisualiser> vis = field_manager->generate_(w++);
  vis->tag = static_cast<int>(w);
  auto filename = tempf + "plant" + std::to_string(vis->tag) + ".bin";
  write_visualiser_to_file(vis, filename);
  while (true)
  {
    // another plant
    if(useFile)
    {
      m->SendJSON({{"type","file"}, {"filename",filename}});
    }
    else
    {
      m->SendGeometry(
        vis->GetGeometry(),
        vis->GetGeometryIndices(),
        "plant" + std::to_string(vis->tag),
        vis->GetGeometryNormals()
      );
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
  }
}

int main(int argc, char** argv)
{
  // MPI init
  int rank, size;
  int sig_port = -1;
  int delay = 100;

  Synavis::CommandLineParser parser(argc, argv);
  std::string ip_address = parser.GetArgument("i");
  if (parser.HasArgument("l"))
  {
    std::string log_level = parser.GetArgument("l");
    Synavis::Logger::Get()->SetVerbosity(log_level);
    lmain(Synavis::ELogVerbosity::Info) << "Log level set to " << log_level << std::endl;
  }
  lmain(Synavis::ELogVerbosity::Debug) << "Starting photosynthesis main" << std::endl;
  if (ip_address.empty())
  {
    lmain(Synavis::ELogVerbosity::Error) << "No IP address provided" << std::endl;
    lmain(Synavis::ELogVerbosity::Warning) << "Usage: srun [slurm job info] " << argv[0] << " <ip address>" << std::endl;
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
  
  if(parser.HasArgument("delay"))
  {
    delay = std::stoi(parser.GetArgument("delay"));
  }

  if (!parser.HasArgument("p"))
  {
    lmain(Synavis::ELogVerbosity::Error) << "No parameter file provided" << std::endl;
    lmain(Synavis::ELogVerbosity::Error) << "Usage: srun [slurm job info] " << argv[0] << " <ip address> -p <parameter file>" << std::endl;
    return 1;
  }
  V::V2 field_size;
  double seeding_distance = 0.05;
  double inter_row_distance = 0.1;
  if (!parser.HasArgument("f"))
  {
    lmain(Synavis::ELogVerbosity::Error) << "No field information [wxhxsxi] provided" << std::endl;
    lmain(Synavis::ELogVerbosity::Error) << "Usage: srun [slurm job info] " << argv[0] << " <ip address> -p <parameter file> -f <field information>" << std::endl;
    return 1;
  }
  else
  {
    // syntax: width x height x seeding distance x inter row distance
    auto field_info = parser.GetArgument("f");
    // split the string by x
    auto x1 = field_info.find('x');
    auto x2 = field_info.find('x', x1 + 1);
    auto x3 = field_info.find('x', x2 + 1);
    // get the field size
    field_size[0] = std::stod(field_info.substr(0, x1));
    field_size[1] = std::stod(field_info.substr(x1 + 1, x2));
    // get the seeding distance
    seeding_distance = std::stod(field_info.substr(x2 + 1, x3));
    // get the inter row distance
    inter_row_distance = std::stod(field_info.substr(x3 + 1));
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

  if (!parser.HasArgument("no-mpi"))
  {
    try
    {
      MPI_Init(NULL, NULL);
      MPI_Comm_rank(MPI_COMM_WORLD, &rank);
      MPI_Comm_size(MPI_COMM_WORLD, &size);
    }
    catch (const std::exception& e)
    {
      lmain(Synavis::ELogVerbosity::Error) << "MPI_Init failed: " << e.what() << std::endl;
      lmain(Synavis::ELogVerbosity::Error) << "Usage: srun [slurm job info] " << argv[0] << " <ip address>" << std::endl;
      return 1;
    }
  }
  else
  {
    rank = 0;
    size = 1;
  }

  std::string parameter_file;
  for(auto c : parser.GetArgument("p"))
  {
    parameter_file += c;
  }
  
  // test parameter file by reading it into CPB
  if(!parser.HasArgument("no-test"))
  {
    try
    {
      lmain(Synavis::ELogVerbosity::Info) << "Testing parameter file: " << parameter_file << std::endl;
      auto plant = std::shared_ptr<CPlantBox::MappedPlant>(new CPlantBox::MappedPlant());
      try
      {
        plant->readParametersChar(parameter_file.c_str());
      }
      catch (const std::exception& e)
      {
        lmain(Synavis::ELogVerbosity::Error) << "Parameter file failed to load: " << e.what() << std::endl;
      }
      for(auto p : plant->getOrganRandomParameter(CPlantBox::Organism::OrganTypes::ot_leaf))
      {
        auto leaf = std::dynamic_pointer_cast<CPlantBox::LeafRandomParameter>(p);
        leaf->leafGeometryPhi = {-90.0, -80.0, -45.0, 0.0, 45.0, 90.0};
        // transform leafGeometryPhi to radians
        std::transform(leaf->leafGeometryPhi.begin(), leaf->leafGeometryPhi.end(), leaf->leafGeometryPhi.begin(), [](auto val) { return val * M_PI / 180.0; });
        leaf->leafGeometryX = {38.41053981,1.0 ,1.0, 0.3, 1.0, 38.41053981};
        leaf->createLeafRadialGeometry(leaf->leafGeometryPhi, leaf->leafGeometryX, 20);
      }
      plant->initialize(true, true);
      plant->simulate(8, false);

      auto vis = std::make_shared<TaggedPlantVisualiser>();
      vis->SetLeafResolution(20);
      vis->SetGeometryResolution(8);
      vis->setPlant(plant);
      vis->SetVerbose(false);
      vis->ComputeGeometryForOrganType(CPlantBox::Organism::OrganTypes::ot_stem, true);
      vis->ComputeGeometryForOrganType(CPlantBox::Organism::OrganTypes::ot_leaf, false);
      auto test_points = vis->GetGeometry();
      auto num_nan = std::count_if(test_points.begin(), test_points.end(), [](auto val) { return std::isnan(val); });
      lmain(Synavis::ELogVerbosity::Info) << "Number of NaNs in test points: " << num_nan << std::endl;
      if (num_nan > 0)
      {
        lmain(Synavis::ELogVerbosity::Error) << "Parameter file failed to load: " << "NaNs in test points" << std::endl;
        return 1;
      }
      else
      {
        lmain(Synavis::ELogVerbosity::Info) << "Parameter file loaded successfully" << std::endl;
        lmain(Synavis::ELogVerbosity::Info) << "Number of test points: " << test_points.size() / 3 << std::endl;
      }
      //write_visualiser_to_file(vis, "./test.bin");
    }
    catch (const std::exception& e)
    {
      lmain(Synavis::ELogVerbosity::Error) << "Parameter file failed to load: " << e.what() << std::endl;
      return 1;
    }
  }

  // amount of plants per thread per rank
  int num_rows = static_cast<int>(field_size[1] / inter_row_distance);
  int num_columns = static_cast<int>(field_size[0] / seeding_distance);
  int plants_per_thread = static_cast<int>(std::ceil((num_rows * num_columns) / (size * threads_per_rank)));



  std::shared_ptr<FieldManager> field_manager = std::make_shared<FieldManager>(field_size,
    rank, size,
    parameter_file,
    seeding_distance, inter_row_distance);

  std::vector<std::shared_ptr<Synavis::WorkerThread>> worker_threads;
  for (auto i : std::views::iota(0, threads_per_rank))
  {
    worker_threads.push_back(std::make_shared<Synavis::WorkerThread>());
  }

  // create a new media receiver
  auto m = std::make_shared<Synavis::DataConnector>();
  m->SetConfig({ {"SignallingIP", ip_address}, {"SignallingPort", sig_port} });
  m->SetTakeFirstStep(false);
  m->Initialize();
  m->StartSignalling();
  m->LockUntilConnected(800);

  if(m->GetState() != Synavis::EConnectionState::CONNECTED)
  {
     lmain(Synavis::ELogVerbosity::Error) << "Failed to connect to the signalling server" << std::endl;
     exit(1);
   }

  m->SetMessageCallback([](auto message)
    {
      using json = nlohmann::json;
      json interp = json::parse(message);
      auto id = interp["id"].get<uint32_t>();
    });

  m->SendJSON({ {"type","command"},{"name","cam"}, {"camera", "scene"} });
  m->SendJSON({{"type","console"},{"command","r.Streaming.PoolSize 30000"}});
  m->SendJSON({ {"type","settings"},{"fMaxVelocity",50.0}, {"bRespondWithTiming", true} });
  m->SendJSON({ {"type", "schedule"}, {"time",1.0}, {"repeat",0.1}, {"command",{{"type","info"},{"frametime","yeye"}}} });

  std::string tempf = "/dev/shm/";
  if (parser.HasArgument("tempf"))
  {
    tempf = parser.GetArgument("tempf");
  }

  if (parser.HasArgument("test"))
  {
    std::string test = parser.GetArgument("test");
    if (test == "scalability")
    {
      bool use_file = parser.HasArgument("use-file");
      lmain(Synavis::ELogVerbosity::Info) << "We are " << (use_file ? "" : "not ") << "using file-based transmission." << std::endl;
      scalability_test(m, field_manager, rank, size, tempf, use_file, delay);
    }
    else if (test == "field-population")
    {
      field_population(m, field_manager, worker_threads, threads_per_rank, plants_per_thread);
    }
  }

  // MPI finalize
  if (!parser.HasArgument("no-mpi"))
  {
    MPI_Finalize();
  }
  return 0;

}

