#pragma once
#include <algorithm>
#include <iterator> 
#include <bitset>
#include <future>
#include <thread>
#include <vector>
#include <queue>
#include <chrono>       
#include <ctime>
#include <set>

using TaskId = unsigned int;
class TaskBase;
using TaskRef = std::shared_ptr<TaskBase>;
class TaskController;
using TaskControllerRef = std::shared_ptr<TaskController>;
using TasksCollection = std::map<TaskId,const TaskRef>;

template <typename OutputType>
struct TaskResult
{
	virtual OutputType GetResult() const = 0;
};


class TaskAffinity
{
	std::bitset<32> _affinityBits;
	
public:

	template<typename ... T>
	constexpr TaskAffinity(T ...t)
	{
		_affinityBits = GetRawAffinity(t ...);
	}

	constexpr TaskAffinity()
	{}
	
	unsigned int GetFirstAffinity() const
	{
		unsigned int bitNumber = 0;

		while (bitNumber < _affinityBits.size() && !_affinityBits.test(bitNumber))
		{
			++bitNumber;
		}

		return bitNumber >= _affinityBits.size() ? 0: bitNumber;
	}

	unsigned int GetNextAffinity(unsigned int prevAffinityNumber) const
	{
		unsigned int bitNumber = prevAffinityNumber;

		do
		{
			++bitNumber;
		} while (bitNumber < _affinityBits.size() && !_affinityBits.test(bitNumber));
			
		return bitNumber < _affinityBits.size() ? bitNumber : GetFirstAffinity();
	}

	bool HasAffinity() const
	{
		return _affinityBits.any();
	}

	void SetAffinity(std::initializer_list<unsigned int> affinities)
	{		
		_affinityBits.reset();
		for (unsigned int affinity : affinities)
		{
			if (_affinityBits.size() > affinity)
			{
				_affinityBits.set(affinity, true);
			}
		}
	}	
private:
	//looks nice but not safe
	//TODO check value in range
	template <typename T, typename ...Tn>
	static constexpr unsigned int GetRawAffinity(T first, Tn ... rest)
	{
		static_assert(std::is_integral<T>::value, "Integral type required");
		return  (1 << (first)) | GetRawAffinity(rest ...);
	}

	template <typename T>
	static constexpr unsigned intGetRawAffinity(T first, T second)
	{
		static_assert(std::is_integral<T>::value, "Integral type required");
		return  1 << (first) | 1 << (second);
	}

	template <typename T>
	static constexpr unsigned int GetRawAffinity(T first)
	{
		static_assert(std::is_integral<T>::value, "Integral type required");
		return  1 << (first);
	}
};

class TaskBase
{
protected:
	TaskAffinity _affinity;
	TaskId GetNextTaskId() const
	{
		static std::atomic<unsigned int> id = 1;
		return id++;
	}
	TaskId _taskId = GetNextTaskId();

	virtual void ExecuteInt() = 0;

public:

	TaskBase() = default;

	TaskBase(TaskBase& other):
		_affinity(other._affinity),
		_taskId(GetNextTaskId())
	{
	}

	TaskBase& operator=(const TaskBase& other)
	{
		if (this != &other)
		{			
			_affinity = other._affinity;
			//taskid has to be unique
			_taskId = GetNextTaskId();
		}
		return *this;
	}

	TaskBase(TaskBase&& other) = default;
	
	TaskBase& operator=(TaskBase&& other) = default;

	virtual ~TaskBase() = default;
	
	TaskId GetTaskId() const
	{
		return _taskId;
	}

	virtual bool CanRun(TaskId prevTaskId) const
	{
		return true;
	}

	TaskId Run()
	{
		ExecuteInt();
		return GetTaskId();
	}
	
	void SetAffinity(const std::initializer_list<unsigned int>& affinities)
	{		
		_affinity.SetAffinity(affinities);
	}

	const TaskAffinity& GetAffinity() const
	{
		return _affinity;
	}
};

class TaskController
{
	unsigned int _numThreads{ 1 };
	unsigned int _threadNumberToAddTask{ 0 };

	std::condition_variable _cvReadyTasks;
	std::mutex _mutexReadyTasks;
	
	std::condition_variable _cvJobs;
	std::mutex _mutexJobs;

	std::vector<TaskId> _readyTasks;
	std::atomic<bool> _readyToExit{ false };
		
	std::vector<std::deque<TaskId>> _taskJobs;	
	std::queue<unsigned int> _threadsLookingForJob;

public:
	explicit TaskController(unsigned int NumThreads):
		_numThreads(NumThreads)
	{
		_taskJobs.resize(NumThreads);
	}

	std::vector<TaskId> WaitTillReadyTask()
	{
		std::unique_lock<std::mutex> guard(_mutexReadyTasks);
		
		_cvReadyTasks.wait(guard, [&]() {return _readyTasks.size() > 0;});
				
		//clear ready tasks
		std::vector<TaskId> resultTasks;
		resultTasks.swap(_readyTasks);

		return resultTasks;
	}

	bool WaitForTaskOrDone(unsigned int threadNumber)
	{
		std::unique_lock<std::mutex> guard(_mutexJobs);
		
		_cvJobs.wait(guard, [&]() { return _taskJobs[threadNumber].size() > 0 || _readyToExit;});

		return _readyToExit;
	}

	std::queue<TaskId> GetOneTaskFromPending(unsigned int threadNumber)
	{
		std::queue<TaskId> tasks;
		
		{
			std::unique_lock<std::mutex> lock(_mutexJobs);
			if (_taskJobs[threadNumber].size() > 0)
			{
				tasks.emplace(_taskJobs[threadNumber].front());
				_taskJobs[threadNumber].pop_front();
			}
		}
		

		return move(tasks);
	}

	std::queue<TaskId> GetAllPendingTasks(unsigned int threadNumber)
	{
		std::unique_lock<std::mutex> lock(_mutexJobs);

		std::queue<TaskId> tasks;
		auto& taskJobs = _taskJobs[threadNumber];
		for (auto it = taskJobs.begin(); it < taskJobs.end(); ++it)
		{
			tasks.emplace(std::move(*it));
		}
		taskJobs.clear();

		return tasks;
	}

	void AddTaskJobs(std::vector<TaskId>&& taskIds, const TasksCollection& tasks)
	{
		{
			std::unique_lock<std::mutex> lock(_mutexJobs);

			for (const auto taskId:taskIds)
			{
				const auto task = tasks.find(taskId);

				//no affinity add to next thread
				if (!task->second->GetAffinity().HasAffinity())
				{
					_taskJobs[_threadNumberToAddTask++].emplace_back(taskId);
				}
				//get affinity of task
				else
				{
					unsigned int Affinity = task->second->GetAffinity().GetFirstAffinity();
					Affinity = Affinity < _numThreads ? Affinity : _threadNumberToAddTask++;
					
					_taskJobs[Affinity].emplace_back(taskId);
				}
				
				if (_threadNumberToAddTask >= _numThreads)
				{
					_threadNumberToAddTask = 0;
				}
			}
		}
		_cvJobs.notify_all();
	}

	void SignalReadyToExit()
	{
		{
			_readyToExit = true;
		}
		_cvReadyTasks.notify_all();
	}

	void SignalTaskReady(TaskId taskId)
	{		
		{
			std::unique_lock<std::mutex> lock(_mutexReadyTasks);
			_readyTasks.push_back(taskId);
		}
		_cvReadyTasks.notify_one();
	}

	void Clear()
	{
		//all threads are done no need of locks
		while (!_threadsLookingForJob.empty())
		{
			_threadsLookingForJob.pop();
		}

		_readyTasks.clear();
		_readyToExit = false;		
	}

	void LookForOtherJob(unsigned int threadNumber)
	{
		{
			std::unique_lock<std::mutex> lock(_mutexReadyTasks);
			_threadsLookingForJob.emplace(threadNumber);
		}
		_cvReadyTasks.notify_one();

	}

	void RescheduleTaskJobs()
	{
		if(!_threadsLookingForJob.empty())
		{
			{
				std::lock(_mutexJobs, _mutexReadyTasks);

				const std::lock_guard<std::mutex> l1(_mutexJobs, std::adopt_lock);
				const std::lock_guard<std::mutex> l2(_mutexReadyTasks, std::adopt_lock);

				while (!_threadsLookingForJob.empty())
				{
					auto lookingThreadId = _threadsLookingForJob.front();
					_threadsLookingForJob.pop();

					for (unsigned int threadId = 0; threadId < _taskJobs.size(); ++threadId)
					{
						if (threadId != lookingThreadId && _taskJobs[threadId].size() > 1)
						{
							std::cout << "\n\nThread:" << lookingThreadId << " steals from Thread:" << threadId << "\n\n";

							//give thread some tasks ;) ( not matter the affinity)

							auto&  tasksCollection = _taskJobs[threadId];

							auto& recieverTasks = _taskJobs[lookingThreadId];
							
							//steal half of the tasks
							auto itMiddle = std::next(tasksCollection.begin(), tasksCollection.size() / 2);

							std::move(itMiddle, tasksCollection.end(), std::back_inserter(recieverTasks));
							
							tasksCollection.erase(itMiddle, tasksCollection.end());														

							break;
						}
					}
				}
			}
			_cvJobs.notify_all();
		}
	}


};
