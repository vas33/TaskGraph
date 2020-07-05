#pragma once
#include "task_graph.h"
#include "task_items.h"

template <typename OutputType, typename FirstCallable, typename  ... Callables>
void AddTaskSequence(TaskGraph& graph, FirstCallable&& firstCallable,  Callables&& ... callables)
{
	auto task = std::make_shared<InitialTaskNode<OutputType>>
		(
			std::forward<FirstCallable>(firstCallable)
		);

	graph.AddTask(task);
	AddSubtaskSequence<OutputType>(graph, task, callables...);
}

template <typename OutputType, typename ParentTask, typename FirstCallable, typename ... Callable>
void AddSubtaskSequence(TaskGraph& graph, ParentTask&& parentTask, FirstCallable&& firstCallable, Callable&& ...  callables)
{
	auto childTask = std::make_shared<InitialTaskNode<OutputType>>
		(
			std::forward<FirstCallable>(firstCallable)
		);

	graph.AddTaskEdge(std::forward<ParentTask>(parentTask), childTask);

	AddSubtaskSequence<OutputType>(graph, childTask, callables ...);
}

template <typename OutputType, typename ParentTask,  typename  Callable>
void AddSubtaskSequence(TaskGraph& graph, ParentTask&& parentTask, Callable&&  callable)
{
	auto childTask = std::make_shared<InitialTaskNode<OutputType>>
		(
			std::forward<Callable>(callable)
		);

	graph.AddTaskEdge(std::forward<ParentTask>(parentTask), childTask);
}

template <typename OutputType, typename CallableType>
void ParallelFor(TaskGraph& graph, unsigned int chunksCount, CallableType&& callable, TaskAffinity affinity = {})
{
	std::vector<TaskRef> tasks;
	unsigned affinityNumber = 0;

	for (unsigned int taskNumber = 0; taskNumber < chunksCount; ++taskNumber)
	{
		auto task = std::make_shared<ParallelTaskNode<OutputType>>
			(
				taskNumber,
				std::forward<CallableType>(callable)
		);
		
		if (affinity.HasAffinity())
		{
			affinityNumber = affinity.GetNextAffinity(affinityNumber);

			task->SetAffinity({ affinityNumber });
		}

		tasks.push_back(task);

		graph.AddTask(task);
	
	}
}

template <typename OutputType, int numThreads = 5, typename CallableType>
void ParallelFor(unsigned int chunksCount, CallableType&& callable, TaskAffinity affinity = {})
{
	TaskGraph graph(numThreads);

	ParallelFor<OutputType, CallableType>(graph, chunksCount, std::forward<CallableType>(callable, affinity));

	graph.WaitAll();
}

template <typename OutputType, typename CallableType, typename ReduceCallableType>
TaskRef ParallelReduce(TaskGraph& graph, TaskRef& parent, unsigned int chnksCount,
	CallableType&& callable,
	ReduceCallableType && reduceCallable, TaskAffinity&& affinity = {})
{
	std::vector<TaskRef> parTasks;
	unsigned affinityNumber = 0;

	for (unsigned int taskNumber = 0; taskNumber < chnksCount; ++taskNumber)
	{
		auto task = std::make_shared<ParallelTaskNode<OutputType>>
			(
				taskNumber,
				std::forward<CallableType>(callable)
				);

		if (affinity.HasAffinity())
		{
			affinityNumber = affinity.GetNextAffinity(affinityNumber);

			task->SetAffinity({ affinityNumber });
		}

		parTasks.push_back(task);
	}
	auto reduceTask = std::make_shared<MultiJoinTaskNode<OutputType>>
		(
			std::forward<ReduceCallableType>(reduceCallable),
			parTasks
			);

	for (const auto& parTask : parTasks)
	{
		//if parent chain tasks after it
		if (parent)
		{
			graph.AddTaskEdge(parent, parTask);
		}
		else
		{
			graph.AddTask(parTask);
		}	
	}

	graph.AddTaskEdges(parTasks, reduceTask);

	return reduceTask;
}

template <typename OutputType, unsigned int numThreads = 5, typename CallableType, typename ReduceCallableType>
void ParallelReduce(unsigned int chunksCount, 
	CallableType&& callable,
	ReduceCallableType&& reduceCallable, TaskAffinity&& affinity = {})
{
	TaskGraph graph(numThreads);

	ParallelReduce<OutputType, CallableType, ReduceCallableType>(graph, chunksCount, std::forward<CallableType>(callable), std::forward<ReduceCallableType>(reduceCallable), std::forward<TaskAffinity>(affinity));

	graph.WaitAll();
}