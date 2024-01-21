

#include <random>
#include <Synavis.hpp>


int main()
{
  auto WorkerThread = std::make_shared<Synavis::WorkerThread>();
  // spam tasks that take a few seconds to complete
  std::cout << " ----- Spamming Tasks -----" << std::endl;
  std::vector<int> tasks = {};
  for (int i = 0; i < 10; i++)
  {
    tasks.push_back(i);
    WorkerThread->AddTask([n=i, &tasks]()
    {
      std::cout << "Parallel Task started (" << n << ")" << std::endl;
      // random integer
      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_int_distribution<> dis(1, 2);
      int seconds = dis(gen);
      std::this_thread::sleep_for(std::chrono::seconds(seconds));
      tasks[n] = 0;
      std::cout << "Task completed" << std::endl;
    });
  }
  std::cout << " ----- Done Spamming Tasks (have " << tasks.size() << " now) -----" << std::endl;
  // wait for all tasks to complete
  std::cout << " ----- Waiting for Tasks -----" << std::endl;
  while (std::any_of(tasks.begin(), tasks.end(), [](int i) { return i != 0; }))
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    // amount of nonzero tasks
    std::cout << "Tasks remaining: " << std::count_if(tasks.begin(), tasks.end(), [](int i) { return i != 0; }) << " while " << WorkerThread->GetTaskCount() << " are in queue." << std::endl;
  }
  std::cout << " ----- Done Waiting for Tasks -----" << std::endl;
  std::cout << " ----- Testing of sequential tasks with wait time -----" << std::endl;
  for (int i = 0; i < 10; i++)
  {
    int taskvalue = 1;
    WorkerThread->AddTask([n=i, &taskvalue]()
    {
      std::cout << "Sqeuential Task started (" << n << ")" << std::endl;
      // random integer
      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_int_distribution<> dis(1, 2);
      int seconds = dis(gen);
      std::this_thread::sleep_for(std::chrono::seconds(seconds));
      taskvalue = 0;
      std::cout << "Sqeuential Task completed" << std::endl;
    });
    while(taskvalue != 0)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    std::cout << "Task " << i << " completed" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }
}
