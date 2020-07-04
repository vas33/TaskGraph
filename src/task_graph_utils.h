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
void ParallelFor(TaskGraph& graph, unsigned int chunksCount, CallableType&& callable)
{
	std::vector<TaskRef> tasks;
	for (unsigned int taskNumber = 0; taskNumber < chunksCount; ++taskNumber)
	{
		auto task = std::make_shared<ParallelTaskNode<OutputType>>
			(
				taskNumber,
				std::forward<CallableType>(callable)
		);

		tasks.push_back(task);
		graph.AddTask(task);
	}
}


template <typename OutputType, int numThreads = 5, typename CallableType>
void ParallelFor(unsigned int chunksCount, CallableType&& callable)
{
	TaskGraph graph(numThreads);

	ParallelFor<OutputType, CallableType>(graph, chunksCount, std::forward<CallableType>(callable));

	graph.WaitAll();
}


template <typename OutputType, typename CallableType, typename ReduceCallableType>
TaskRef ParallelReduce(TaskGraph& graph, unsigned int chnksCount,
	CallableType&& callable,
	ReduceCallableType && reduceCallable)
{
	std::vector<TaskRef> parTasks;
	for (unsigned int taskNumber = 0; taskNumber < chnksCount; ++taskNumber)
	{
		auto task = std::make_shared<ParallelTaskNode<OutputType>>
			(
				taskNumber,
				std::forward<CallableType>(callable)
				);

		parTasks.push_back(task);
	}
	auto reduceTask = std::make_shared<MultiJoinTaskNode<OutputType>>
		(
			std::forward<ReduceCallableType>(reduceCallable),
			parTasks
			);

	for (const auto& parTask : parTasks)
	{
		graph.AddTask(parTask);
	}

	graph.AddTaskEdges(parTasks, reduceTask);

	return reduceTask;
}

template <typename OutputType, unsigned int numThreads = 5, typename CallableType, typename ReduceCallableType>
void ParallelReduce(unsigned int chunksCount, 
	CallableType&& callable,
	ReduceCallableType&& reduceCallable)
{
	TaskGraph graph(numThreads);

	ParallelReduce<OutputType, CallableType, ReduceCallableType>(graph, chunksCount, std::forward<CallableType>(callable), std::forward<ReduceCallableType>(reduceCallable));

	graph.WaitAll();
}