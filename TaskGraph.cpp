// TaskGraph.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <iostream>
#include <map>

#include "src\task_graph.h"
#include "src\task_graph_utils.h"

#include "ch1.h"


using namespace std;

void Test1()
{
	cout << "\n Test 1 start \n";
	TaskGraph graph(5);

	int result = 0;

	//Spawn th 1
	auto prduceSomeInt = make_shared<InitialTaskNode<int>>(
		[]()->int
	{
		return 1000;
	}
	);

	//Spawn th 2
	std::shared_ptr<TaskNode<int, int>> task2 = make_shared<TaskNode<int, int>>(
		prduceSomeInt,
		[&](int input) ->int
	{

			TaskGraph subGraph(2);
			////add dynamic task
			////Spawn th 3
			auto  node = make_shared<InitialTaskNode<int>>(
				[]()->int
			{
				//std::this_thread::sleep_for(1000ms);
				return 500;
			}
			);
			subGraph.AddTask(node);

			auto nodePlusOne = make_shared<TaskNode<int, int>>
				(	node,
					[](int input)
					{
						return input + 1;
					}
				);

			subGraph.AddTaskEdge(node, nodePlusOne);
			subGraph.WaitAll();

			int z = 1000;
		
			return input * 40 * z + nodePlusOne->GetResult();
	}
	);

	graph.AddTask(prduceSomeInt);
	graph.AddTaskEdge(prduceSomeInt, task2);
	graph.WaitAll();

	cout << "result " << task2->GetResult() << " \n";

	cout << "\n Test 1 done \n";
}

void Test2()
{
	cout << "\nTest2 Start\n";

	const int numTasks = 5;
	std::map<int, std::vector<int>> mapVectors;

	ParallelFor<int, 5>(numTasks,
		[&](int chunk)->int
		{
			for (int i = 0; i < 10000;i++)
			{
				cout << "Add chunk " << chunk << " value " << i << "\n";
				mapVectors[chunk].push_back(i);
			}
			return 0;
		}
	);


	cout << "\nTest2 Done\n";
}



using ImagePtr = std::shared_ptr<ch01::Image>;
ImagePtr applyGammaParallel(ImagePtr image_ptr, double gamma) {
	auto output_image_ptr =
		std::make_shared<ch01::Image>(image_ptr->name() + "_gamma",
			ch01::IMAGE_WIDTH, ch01::IMAGE_HEIGHT);
	auto in_rows = image_ptr->rows();
	auto out_rows = output_image_ptr->rows();
	const int height = in_rows.size();
	const int width = in_rows[1] - in_rows[0];

	//TaskGraph graph(5);
	ParallelReduce<int, 5>(height,
		[&in_rows, &out_rows, width, gamma](unsigned int chunk)->int
	{
		auto in_row = in_rows[chunk];
		auto out_row = out_rows[chunk];

		//transform
		std::transform(in_row, in_row + width,
			out_row, [gamma](const ch01::Image::Pixel& p) {
			double v = 0.3*p.bgra[2] + 0.59*p.bgra[1] + 0.11*p.bgra[0];
			double res = pow(v, gamma);
			if (res > ch01::MAX_BGR_VALUE) res = ch01::MAX_BGR_VALUE;
			return ch01::Image::Pixel(res, res, res);
		});
		return 0;
	},
	[&output_image_ptr]()->int
	{
		string outputPath("./");
		outputPath.append(output_image_ptr->name());
		outputPath.append(".png");

		output_image_ptr->write(outputPath.c_str());

		cout << "All tasks done\n";
		return 0;
	}
	);
	//graph.WaitAll();
	return output_image_ptr;
}

void Test3()
{
	cout << "\n\nTest 3 start ..\n";
	cout << "Generating fractal image..\n";

	ImagePtr image(ch01::makeFractalImage(20000));

	if(!image)
	{
		return;
	}
	
	//save original image image
	image->write("./fractal0.png");

	cout << "Apply gamma to image \n";

	applyGammaParallel(image, 1.4f);

	cout << "Test 3 Done. \n";
}

void Test4()
{
	cout << "Test4 4 Start \n";

	int result = 0;

	auto initialize = [&]()->int
	{
		result = 100;
	
		return 0;
	};

	auto doubleResult = [&]()->int
	{
		result *= 2;
		return 0;
	};

	auto plusOne = [&]()->int
	{
		result += 1;
		return 0;
	};

	TaskGraph graph(5);

	AddTaskSequence<int>(graph, initialize, doubleResult, plusOne);

	graph.WaitAll();
	cout << "Resut " << result<<"\n";

	cout << "Test4 4 Done \n";
}

int main()
{
	//Test1();
	//Test2();
	//Test3();
	Test4();

	cout << "\nType a word and pres [Enter] to exit\n";
	char z;
	cin >> z;
    return 0;
}

