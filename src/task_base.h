#pragma once
#include <future>
#include <thread>
#include <vector>
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
	std::condition_variable _cv;
	std::mutex _mutex;
	std::vector<TaskId> _readyTasks;

public:
	std::vector<TaskId> WaitTillReadyTask()
	{
		std::unique_lock<std::mutex> guard(_mutex);
		
		_cv.wait(guard, [&]() {return _readyTasks.size() > 0;});
				
		auto tasksCopy = _readyTasks;
		_readyTasks.clear();

		return tasksCopy;				
	}

	void SignalTaskReady(TaskId taskId)
	{		
		{
			std::unique_lock<std::mutex> lock(_mutex);
			_readyTasks.push_back(taskId);
		}
		_cv.notify_one();
	}
};

class TaskBase
{
protected:
	TaskControllerRef _taskController;
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

	void SetTaskController(TaskControllerRef& taskController)
	{
		_taskController = taskController;
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

