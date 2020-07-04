#pragma once
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
				auto task = tasks.find(taskId);

				_taskJobs[_threadNumberToAddTask++].emplace(task->second);
				
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
};

class TaskBase
{
protected:
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
};

