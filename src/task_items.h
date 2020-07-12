#pragma once

template<typename T, typename  ...Args>
struct shared_enabler : public T
{
	shared_enabler(Args&&...  args) :T(std::forward<Args>(args)...) {};
};

template<typename T, typename  ...Args>
auto make_shared_enabler(Args... args)
{
	return std::make_shared<shared_enabler<T, Args...>>(std::forward<Args>(args)...);
}

template<typename TaskType>
struct TaskFactory
{
	template<typename ...Args>
	static const std::shared_ptr<TaskType> create(Args&& ... args)
	{
		return make_shared_enabler<TaskType>(std::forward<Args>(args)...);
	}
};

template<typename InputType, typename OutputType>
class TaskNode : 
	public TaskBase,
	public TaskResult<OutputType>,
	public TaskFactory<TaskNode<InputType, OutputType>>
{
	template<typename T, typename ...Args>
	friend struct shared_enabler;

	using TaskCallable = std::function<OutputType(InputType)>;

	std::shared_ptr<TaskBase> _prev;
	TaskCallable _callable;
	OutputType _result;

	explicit TaskNode(std::shared_ptr<TaskBase> prev, TaskCallable callable) :
		_prev(prev),
		_callable(callable)
	{
	}
public:		
	TaskNode(const TaskNode&) = delete;
	TaskNode& operator = (const TaskNode&) = delete;

	void ExecuteInt() override
	{
		auto resultGetter = std::dynamic_pointer_cast<TaskResult<InputType>>(_prev);
		if (resultGetter)
		{
			_result = _callable(resultGetter->GetResult());
		}	
	}

	OutputType GetResult() const override
	{
		return _result;
	}
};

template<typename OutputType>
class InitialTaskNode :
	public TaskBase, 
	public TaskResult<OutputType>,
	public TaskFactory<InitialTaskNode<OutputType>>
{
	using TaskCallable = std::function<OutputType()>;
	
	template<typename T, typename ...Args>
	friend struct shared_enabler;
	
	OutputType _result;
	std::packaged_task<OutputType()> _caller;
	mutable std::future<OutputType> _future;

	explicit InitialTaskNode(TaskCallable&& callable) :
		_caller(std::forward<TaskCallable>(callable)),
		_future(_caller.get_future())
	{
	}
public:
	
	OutputType GetResult() const
	{		
		return _future.get();
	}

	void ExecuteInt() override
	{
		_caller();
	}
private:

};

template<>
class InitialTaskNode<void> :
	public TaskBase,
	public TaskFactory<InitialTaskNode<void>>
{
	using TaskCallable = std::function<void()>;
	
	template<typename T, typename ...Args>
	friend struct shared_enabler;

	TaskCallable _callable;
	
	explicit InitialTaskNode(TaskCallable callable) :_callable(callable)
	{
	}
public:	
	void ExecuteInt() override
	{
		_callable();
	}
};

template<typename OutputType>
class ParallelTaskNode : 
	public TaskBase,
	public TaskResult<OutputType>,
	public TaskFactory<ParallelTaskNode<OutputType>>
{
	using TaskCallable = std::function<OutputType(unsigned int)>;

	template<typename T, typename ...Args>
	friend struct shared_enabler;

	unsigned int _chunk;
	TaskCallable _callable;
	mutable OutputType _result;
	
	explicit ParallelTaskNode(unsigned int chunk, TaskCallable callable) :
		_chunk(chunk), _callable(callable)
	{
	}
public:

	OutputType GetResult() const override
	{
		return _result;
	}
	void ExecuteInt() override
	{
		_result = _callable(_chunk);
	}
};

template<typename OutputType>
class MultiJoinTaskNode :
	public TaskBase, 
	public TaskResult<OutputType>,
	public TaskFactory<MultiJoinTaskNode<OutputType>>
{
	template<typename T, typename ...Args>
	friend struct shared_enabler;

	using TaskCallable = std::function<OutputType()>;
	TaskCallable _callable;
	OutputType _result;
	mutable std::set<TaskId> _prevTaskIds;
	explicit MultiJoinTaskNode(TaskCallable callable, const std::vector<TaskRef>& prevTasks) : _callable(callable)
	{
		for (const auto& task : prevTasks)
		{
			_prevTaskIds.emplace(task->GetTaskId());
		}
	}

public:
	OutputType GetResult() const override
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

template<>
class MultiJoinTaskNode<void> :
	public TaskBase,
	public TaskFactory<MultiJoinTaskNode<void>>
{
	using TaskCallable = std::function<void()>;
	template<typename T, typename ...Args>
	friend struct shared_enabler;


	TaskCallable _callable;	
	mutable std::set<TaskId> _prevTaskIds;
	explicit MultiJoinTaskNode(TaskCallable callable, const std::vector<TaskRef>& prevTasks) : _callable(callable)
	{
		for (const auto& task : prevTasks)
		{
			_prevTaskIds.emplace(task->GetTaskId());
		}
	}
public:
	
	bool CanRun(TaskId prevtaskId) const override
	{
		//make sure all previous tasks are executed before fetching this one
		_prevTaskIds.erase(prevtaskId);
		return _prevTaskIds.size() == 0;
	}

	void ExecuteInt() override
	{
		_callable();
	}
};
