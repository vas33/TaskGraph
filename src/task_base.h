#pragma once
#include <bitset>
#include <future>
#include <thread>
#include <vector>
#include <queue>
#include <chrono>       
#include <ctime>

using TaskId = unsigned int;
class TaskBase;
using TaskRef = std::shared_ptr<TaskBase>;
class TaskController;
using TaskControllerRef = std::shared_ptr<TaskController>;

template <typename OutputType>
struct TaskResult
{
	virtual OutputType GetResult() = 0;
};

//TODO make itearator for TaskAffinity
class TaskAffinity
{
	std::bitset<24> _affinityBits;

public:
	TaskAffinity(){}
	TaskAffinity(std::initializer_list<unsigned int> affinities)
	{
		SetAffinity(affinities);
	}

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
		for (unsigned int affinity : affinities)
		{
			if (_affinityBits.size() > affinity)
			{
				_affinityBits.set(affinity, true);
			}
		}
	}
};

class TaskBase
{
protected:
	TaskAffinity _affinity;
	TaskId GetNextTaskId() const
	{
		static unsigned int id = 1;
		return id++;
	}
	TaskId _taskId = GetNextTaskId();

	virtual void ExecuteInt() = 0;

public:
	TaskId GetTaskId() const
	{
		return _taskId;
	}

	virtual bool CanRun(TaskId prevTaskId)
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
	
	std::condition_variable _cv;
	std::mutex _mutex;

	std::vector<TaskId> _readyTasks;
	bool _readyToExit{ false };
		
	std::map<unsigned int, std::queue<TaskRef>> _taskJobs;

public:
	explicit TaskController(unsigned int NumThreads):_numThreads(NumThreads)
	{
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

	void WaitForTaskOrDone(unsigned int threadNumber)
	{
		std::unique_lock<std::mutex> guard(_mutex);

		_cv.wait(guard, [&]() { return _taskJobs[threadNumber].size() > 0 || _readyToExit;});
	}

	std::queue<TaskRef>&& GetPendingTasks(unsigned int threadNumber)
	{
		std::unique_lock<std::mutex> lock(_mutex);

		return move(_taskJobs[threadNumber]);
	}

	void AddTaskJobs(std::vector<TaskId>&& taskIds, const std::map<TaskId, TaskRef>& tasks)
	{
		{
			std::unique_lock<std::mutex> lock(_mutex);

			for (const auto taskId:taskIds)
			{
				const auto task = tasks.find(taskId);

				//no affinity add to next thread
				if (!task->second->GetAffinity().HasAffinity())
				{
					_taskJobs[_threadNumberToAddTask++].emplace(task->second);
				}
				//get affinity of task
				else
				{
					unsigned int Affinity = task->second->GetAffinity().GetFirstAffinity();
					Affinity = Affinity < _numThreads ? Affinity : _threadNumberToAddTask++;
					
					_taskJobs[Affinity].emplace(task->second);
				}
								
				
				if (_threadNumberToAddTask >= _numThreads)
				{
					_threadNumberToAddTask = 0;
				}
			}
		}
		_cv.notify_all();
	}

	void SignalReadyToExit()
	{
		{
			std::unique_lock<std::mutex> lock(_mutex);
			_readyToExit = true;
		}
		_cv.notify_all();		
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
		std::lock(_mutex, _mutexReadyTasks);

		const std::lock_guard<std::mutex> l1(_mutex, std::adopt_lock);
		const std::lock_guard<std::mutex> l2(_mutexReadyTasks, std::adopt_lock);

		_readyTasks.clear();
		_readyToExit = false;		
	}
};
