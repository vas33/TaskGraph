#pragma once
#include "task_base.h"
#include <set>
#include <queue>
#include <unordered_set>

class WorkerThread
{
	unsigned int _threadNumber{ 0 };
	const TasksCollection& _tasksData;
	std::queue<TaskId>  _tasks{};
	std::shared_ptr<TaskController> _controller;
public:
	explicit WorkerThread(TasksCollection& tasksData, unsigned int threadNumber) :
		_threadNumber(threadNumber),
		_tasksData(tasksData)
	{
	}

	WorkerThread(const WorkerThread& other)= delete;

	WorkerThread&  operator=(WorkerThread& other) = delete;

	WorkerThread&  operator=(WorkerThread&& other) = delete;

	WorkerThread(WorkerThread&& other):
		_threadNumber(other._threadNumber),
		_tasksData(other._tasksData)
	{		
		_tasks.swap(other._tasks);
		
		_controller.swap(other._controller);		
	}

	void SetController(std::shared_ptr<TaskController>& controller)
	{
		_controller = controller;
	}

	void Start()
	{
		std::thread th(&WorkerThread::DoJobs, this);
		//Some OS specific code here that sets thread to concrete CPU
		th.detach();
	}

private:
	void DoJobs()
	{
		while (true)
		{
			//wait for more tasks or if done
			bool readyToExit = _controller->WaitForTaskOrDone(_threadNumber);
			if (readyToExit)
			{
				//we are done exit
				break;
			}

			while (true)
			{
				_tasks.swap(_controller->GetSomeTaskJobs(_threadNumber));
				if (_tasks.empty())
				{
					//no tasks available wait for more
					break;
				}

				//process tasks
				std::vector<TaskId> readyTasks;
				while (!_tasks.empty())
				{
					//get next task
					TaskId taskId = _tasks.front();
					_tasks.pop();

					auto it = _tasksData.find(taskId);
					if (it != _tasksData.end())
					{
						it->second->Run();
					}
								
					readyTasks.push_back(taskId);
				}
				_controller->SignalTasksReady(std::move(readyTasks));
			}
	
		}
		
	}
};

unsigned int GetNumberOfCPUs()
{
	auto hardwareConcurrency = std::thread::hardware_concurrency();
	//hardware_concurrency returns zero sometimes handle it 
	return hardwareConcurrency ? hardwareConcurrency : 1;	
}

class TaskGraph
{
	std::shared_ptr<TaskController> _taskController;
	unsigned int _maxRunningTasks{ 1 };

	TasksCollection _tasks;
	std::vector<TaskId> _pendingTasks;
	std::vector<TaskId> _completedTasks;
	std::map<TaskId, std::vector<TaskId>> _taskChildren;
	std::vector<WorkerThread> _workerThreads;
	
public:
	TaskGraph(unsigned int runningTasks = GetNumberOfCPUs()):
		_taskController(std::make_shared<TaskController>(runningTasks)),
		_maxRunningTasks(runningTasks)
	{
	}

	TaskGraph(const TaskGraph&) = delete;

	TaskGraph(TaskGraph&&) = delete;

	TaskGraph& operator=(const TaskGraph&) = delete;
	
	TaskGraph& operator=(const TaskGraph&&) = delete;

	void AddTask(TaskRef task)
	{
		AddToTasks(task);

		AddToPendingTasks(task->GetTaskId());		
	}

	void AddTaskEdge(const TaskRef parent, TaskRef child)
	{
		AddToTasks(child);
		AddTaskChild(parent->GetTaskId(), child->GetTaskId());
	}

	void AddTaskEdges(const std::vector<TaskRef>& parents, TaskRef child)
	{
		AddToTasks(child);
		for (const auto& parentTask : parents)
		{
			AddTaskChild(parentTask->GetTaskId(), child->GetTaskId());
		}
	}

	void PrintTasksExecution()
	{
		std::queue<TaskId> tasksOrder;
		std::unordered_set<TaskId> visited;

		//BFS on taks
		for (auto taskId: _pendingTasks)
		{
			tasksOrder.emplace(taskId);
			visited.emplace(taskId);
		}

		std::cout << "\n\nTasks order \n\n";

		while (!tasksOrder.empty())
		{
			auto taskId = tasksOrder.front();
			tasksOrder.pop();
			
			std::cout << " " << taskId << ", ";

			for (auto childTaskId : _taskChildren[taskId])
			{
				if (visited.find(childTaskId) == visited.end())
				{
					tasksOrder.emplace(childTaskId);
					visited.emplace(childTaskId);
				}
			}
		}
	}

	void WaitAll()
	{		
		StartWorkerThreads();

		while (!AllTasksDone())
		{
			if (HasPendingTasks())
			{
				SchedulePendingTasks();
			}
			else
			{
				WaitForReadyTasks();
			}
		}

		//shutdown threads
		DoneAndExit();
	}

private:
	void CleanUp()
	{
		_taskController->Clear();

		_tasks.clear();
		_pendingTasks.clear();
		_completedTasks.clear();
		_taskChildren.clear();
		_workerThreads.clear();
	}

	void DoneAndExit()
	{
		_taskController->SignalReadyToExit();
		//wait some time for worker threads to exit
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

		CleanUp();		
	}

	void StartWorkerThreads()
	{		
		_workerThreads.clear();

		for (unsigned threadIndex = 0; threadIndex < _maxRunningTasks; ++threadIndex)
		{				
			WorkerThread wth(_tasks, threadIndex);
			wth.SetController(_taskController);

			_workerThreads.push_back(std::move(wth));
		}

		for (auto& wt : _workerThreads)
		{	
			wt.Start();
		}
	}

private:
	//add to global tasks collection
	void AddToTasks(TaskRef& task)
	{
		if (_tasks.find(task->GetTaskId()) != _tasks.end())
		{
			throw std::invalid_argument("Error attempting to add duplicate task");
		}

		_tasks.emplace(task->GetTaskId(), task);
	}
	
	void AddToPendingTasks(TaskId id)
	{
		_pendingTasks.emplace_back(id);
	}

	void AddTaskChild(TaskId parentId, TaskId childId)
	{
		_taskChildren[parentId].push_back(childId);
	}
private:
	bool AllTasksDone() const
	{
		return _completedTasks.size() == _tasks.size();
	}

	bool HasPendingTasks() const
	{
		return _pendingTasks.size() > 0;
	}

	void CompleteTask(TaskId taskId)
	{
		_completedTasks.push_back(taskId);
	}

	void FetchTaskChildren(TaskId taskId)
	{
		//fetch ready task children
		//actualy do BFS kind traversal of graph
		for (auto taskChildId : _taskChildren[taskId])
		{
			if (_tasks[taskChildId]->CanRun(taskId))
			{
				AddToPendingTasks(taskChildId);
			}
		}
	}

	void WaitForReadyTasks()
	{
		auto readyTasks = move(_taskController->WaitTillReadyTask());

		//fetch next tasks
		for(auto readyTaskId:readyTasks)
		{
			FetchTaskChildren(readyTaskId);

			CompleteTask(readyTaskId);
		}		
	}

	void SchedulePendingTasks()
	{		
		_taskController->AddTaskJobs(move(_pendingTasks), _tasks);

		_pendingTasks.clear();
	}
};
