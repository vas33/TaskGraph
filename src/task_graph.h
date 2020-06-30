#pragma once
#include "task_base.h"
#include <set>
#include <queue>
#include <unordered_set>

template<typename InputType, typename OutputType>
class TaskNode : public TaskBase, public TaskResult<OutputType>
{
	using TaskCallable = std::function<OutputType(InputType)>;

	std::shared_ptr<TaskBase> _prev;
	TaskCallable _callable;
	OutputType _result;

public:
	explicit TaskNode(std::shared_ptr<TaskBase> prev, TaskCallable callable) :
		_prev(prev),
		_callable(callable)
	{
	}

	TaskNode(const TaskNode&) = delete;
	TaskNode& operator = (const TaskNode&) = delete;

	void ExecuteInt() override
	{
		auto resultGetter = std::dynamic_pointer_cast<TaskResult<InputType>>(_prev);
		auto caller = [&]
		{
			_result = _callable(resultGetter->GetResult());
			_taskController->SignalTaskReady(GetTaskId());
		};

		std::thread th(caller);
		th.detach();
	}

	OutputType GetResult() override
	{
		return _result;
	}
};

template<typename OutputType>
class InitialTaskNode :public TaskBase, public TaskResult<OutputType>
{
	using TaskCallable = std::function<OutputType()>;
	TaskCallable _callable;
	std::promise<OutputType> _resultGetter;
public:

	explicit InitialTaskNode(TaskCallable callable) :_callable(callable)
	{
	}

	OutputType GetResult()
	{
		return _resultGetter.get_future().get();
	}

	void ExecuteInt() override
	{
		auto runner = [&]()
		{
			_resultGetter.set_value_at_thread_exit(_callable());
			_taskController->SignalTaskReady(GetTaskId());
		};

		std::thread th(runner);
		th.detach();
	}
};

template<typename OutputType>
class ParallelTaskNode : public TaskBase, public TaskResult<OutputType>
{
	using TaskCallable = std::function<OutputType(unsigned int)>;
	unsigned int _chunk;
	TaskCallable _callable;
	std::promise<OutputType> _resultGetter;
public:
	explicit ParallelTaskNode(unsigned int chunk, TaskCallable callable):
		_chunk(chunk),_callable(callable)
	{
	}

	OutputType GetResult() override
	{
		return _resultGetter.get_future().get();
	}
	void ExecuteInt() override
	{
		auto runner = [&]()
		{
			_resultGetter.set_value_at_thread_exit(_callable(_chunk));
			_taskController->SignalTaskReady(GetTaskId());
		};

		std::thread th(runner);
		th.detach();
	}
};

template<typename OutputType>
class MultiJoinTaskNode:public TaskBase, public TaskResult<OutputType>
{
	using TaskCallable = std::function<OutputType()>;
	TaskCallable _callable;
	std::promise<OutputType> _resultGetter;
	std::set<TaskId> _prevTaskIds;
public:
	explicit MultiJoinTaskNode(TaskCallable callable, const std::vector<TaskRef>& prevTasks) : _callable(callable)
	{
		for(const auto& task: prevTasks)
		{					
			_prevTaskIds.emplace(task->GetTaskId());
		}
	}

	OutputType GetResult() override
	{
		return _resultGetter.get_future().get();
	}

	bool CanRun(TaskId prevtaskId) override
	{
		//make sure all previous tasks are executed before fetch this one
		_prevTaskIds.erase(prevtaskId);
		return _prevTaskIds.size() == 0;
	}

	void ExecuteInt() override
	{
		auto runner = [&]()
		{
			_resultGetter.set_value_at_thread_exit(_callable());
			_taskController->SignalTaskReady(GetTaskId());
		};

		std::thread th(runner);
		th.detach();
	}
};

class TaskGraph
{
	std::shared_ptr<TaskController> _taskController;
	std::map<TaskId, TaskRef> _tasks;
	std::unordered_set<TaskId> _runningTasks;
	std::queue<TaskId> _pendingTasks;
	std::vector<TaskId> _completedTasks;
	std::map<TaskId, std::vector<TaskId>> _taskChildren;
	unsigned int _maxRunningTasks{ 1 };
public:
	TaskGraph(unsigned int runningTasks):
		_taskController(std::make_shared<TaskController>()),
		_maxRunningTasks(runningTasks)
	{
	}

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

	void WaitAll()
	{
		while (!AllTasksDone())
		{
			if (HasFreeRunningSlots() && HasPendingTasks())
			{
				RunOneTask();
			}
			else
			{
				//wait for ready task
				WaitForReadyTasks();
			}
		}
	}
private:
	//add to global tasks collection
	void AddToTasks(TaskRef& task)
	{
		AttachController(task);
		_tasks.emplace(task->GetTaskId(), task);
	}
	
	void AttachController(TaskRef& task)
	{
		task->SetTaskController(_taskController);
	}

	void AddToPendingTasks(TaskId id)
	{
		_pendingTasks.emplace(id);
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

	bool HasFreeRunningSlots() const
	{		
		return _maxRunningTasks > _runningTasks.size();
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

	void AddTaskRunning(TaskId taskId)
	{
		_runningTasks.emplace(taskId);
	}

	void RemoveRunningTask(TaskId taskId)
	{
		_runningTasks.erase(taskId);
	}


	void WaitForReadyTasks()
	{
		auto readyTasks = _taskController->WaitTillReadyTask();

		//fetch next tasks
		for(auto readyTaskId:readyTasks)
		{
			RemoveRunningTask(readyTaskId);

			FetchTaskChildren(readyTaskId);

			CompleteTask(readyTaskId);
		}		
	}

	void RunOneTask()
	{
		//pop next running task
		auto runningTaskId = _pendingTasks.front();
		_pendingTasks.pop();

		//put to running tasks
		AddTaskRunning(runningTaskId);		

		const auto& task = _tasks[runningTaskId];
		task->Run();
	}
};
