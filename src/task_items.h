#pragma once

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

		_result = _callable(resultGetter->GetResult());
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
	OutputType _result;
public:

	explicit InitialTaskNode(TaskCallable callable) :_callable(callable)
	{
	}

	OutputType GetResult()
	{
		return _result;
	}

	void ExecuteInt() override
	{
		_result = _callable();
	}
};

template<typename OutputType>
class ParallelTaskNode : public TaskBase, public TaskResult<OutputType>
{
	using TaskCallable = std::function<OutputType(unsigned int)>;
	unsigned int _chunk;
	TaskCallable _callable;
	OutputType _result;

public:
	explicit ParallelTaskNode(unsigned int chunk, TaskCallable callable) :
		_chunk(chunk), _callable(callable)
	{
	}

	OutputType GetResult() override
	{
		return _result;
	}
	void ExecuteInt() override
	{
		_result = _callable(_chunk);
	}
};

template<typename OutputType>
class MultiJoinTaskNode :public TaskBase, public TaskResult<OutputType>
{
	using TaskCallable = std::function<OutputType()>;
	TaskCallable _callable;
	OutputType _result;
	mutable std::set<TaskId> _prevTaskIds;
public:
	explicit MultiJoinTaskNode(TaskCallable callable, const std::vector<TaskRef>& prevTasks) : _callable(callable)
	{
		for (const auto& task : prevTasks)
		{
			_prevTaskIds.emplace(task->GetTaskId());
		}
	}

	OutputType GetResult() override
	{
		return _result;
	}

	bool CanRun(TaskId prevtaskId) const override
	{
		//make sure all previous tasks are executed before fetching this one
		_prevTaskIds.erase(prevtaskId);
		return _prevTaskIds.size() == 0;
	}

	void ExecuteInt() override
	{
		_result = _callable();
	}
};
