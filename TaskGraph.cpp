// TaskGraph.cpp : Defines the entry point for the console application.
//
#include <assert.h>

#include <iostream>
#include <map>

#include "src/task_graph.h"
#include "src/task_graph_utils.h"

#include "ext/ch1.h"
#include "ext/ch2.h"

using namespace std;

void Test1()
{	
	cout << "\n Test 1 start \n";
	TaskGraph graph;

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

			TaskGraph subGraph(1);
			////add dynamic task
			////Spawn th 3
			auto  node = make_shared<InitialTaskNode<int>>(
				[]()->int
			{
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

	prduceSomeInt->SetAffinity({ 2 });
	graph.AddTask(prduceSomeInt);
	graph.AddTaskEdge(prduceSomeInt, task2);
	
	graph.WaitAll();

	assert(task2->GetResult() == 40000501);
	cout << "result " << task2->GetResult() << " \n";

	cout << "\n Test 1 done \n";
}

void Test2()
{
	cout << "\nTest2 Start\n";

	const int numTasks = 5;
	std::map<int, std::vector<int>> mapVectors;
	TaskGraph graph;
	ParallelFor<int>(graph, numTasks,
		[&](int chunk)->int
		{
			for (int i = 0; i < 100;i++)
			{
				cout << "Add chunk " << chunk << " value " << i << "\n";
				mapVectors[chunk].push_back(i);
			}
			return 0;
		},
		{ 1 , 2 , 3 ,4 ,5}		
	);

	//graph.PrintTasksExecution();
	graph.WaitAll();
	cout << "\nTest2 Done\n";
}

using ImagePtr = std::shared_ptr<ch01::Image>;
auto applyGamma(TaskGraph& graph, TaskRef& parentTask, ImagePtr& image_ptr, ImagePtr& output_image_ptr, double gamma) {
	
	const int height = ch01::IMAGE_HEIGHT;

	return ParallelReduce<int>(graph, parentTask, height,
		[&image_ptr, &output_image_ptr, gamma](unsigned int chunk)->int
	{
		auto in_rows = image_ptr->rows();
		const int width = in_rows[1] - in_rows[0];
		
		auto in_row = image_ptr->rows()[chunk];
		auto out_row = output_image_ptr->rows()[chunk];

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

		std::cout << "Gama tasks done\n";
		return 0;
	}
	);	
}

auto applyTint(TaskGraph& graph, TaskRef& parentTask, ImagePtr& image_ptr, ImagePtr& output_image_ptr, const double *tints) {
	
	//auto in_rows = image_ptr->rows();
	//const int height = in_rows.size();
	const int height = ch01::IMAGE_HEIGHT;

	return ParallelReduce<int>(graph, parentTask, height,
		[&image_ptr, &output_image_ptr, tints](unsigned int chunk)->int
	{
		auto in_rows = image_ptr->rows();
		auto out_rows = output_image_ptr->rows();
		const int width = in_rows[1] - in_rows[0];

		auto in_row = in_rows[chunk];
		auto out_row = out_rows[chunk];
	
		std::transform(in_row, in_row + width,
			out_row, [tints](const ch01::Image::Pixel& p) {
			std::uint8_t b = (double)p.bgra[0] +
				(ch01::MAX_BGR_VALUE - p.bgra[0])*tints[0];
			std::uint8_t g = (double)p.bgra[1] +
				(ch01::MAX_BGR_VALUE - p.bgra[1])*tints[1];
			std::uint8_t r = (double)p.bgra[2] +
				(ch01::MAX_BGR_VALUE - p.bgra[2])*tints[2];
			return ch01::Image::Pixel(
				(b > ch01::MAX_BGR_VALUE) ? ch01::MAX_BGR_VALUE : b,
				(g > ch01::MAX_BGR_VALUE) ? ch01::MAX_BGR_VALUE : g,
				(r > ch01::MAX_BGR_VALUE) ? ch01::MAX_BGR_VALUE : r
				);
		});

		return 0;
	},
	[&output_image_ptr]()->int
	{
		string outputPath("./");
		outputPath.append(output_image_ptr->name());
		outputPath.append(".png");

		output_image_ptr->write(outputPath.c_str());

		std::cout << "Tint tasks done\n";
		return 0;
	});
}

void Test3()
{
	cout << "\n\nTest 3 start ..\n";
	cout << "Generating fractal image(serial)..\n";

	TaskGraph graph;
	ImagePtr image;
	auto imageWithGamma =
		std::make_shared<ch01::Image>("fractal_gamma",
			ch01::IMAGE_WIDTH, ch01::IMAGE_HEIGHT);

	auto imageWithTint =
		std::make_shared<ch01::Image>("fractal_tinted",
			ch01::IMAGE_WIDTH, ch01::IMAGE_HEIGHT);

	TaskRef generateImageTask = std::make_shared<InitialTaskNode<int>>
		(
			[&image]()->int
			{
				image = ch01::makeFractalImage(2000000);
				//save original image image
				image->write("./fractal0.png");
				return 0;
			}
			);

	graph.AddTask(generateImageTask);

	//Apply gamma stage
	auto gammaTask = applyGamma(graph, generateImageTask, image, imageWithGamma, 1.4f);

	//Apply tint stage
	const double tint_array[] = { 0.75, 0, 0 };
	applyTint(graph, gammaTask, imageWithGamma, imageWithTint, tint_array);

	graph.WaitAll();

	cout << "Test 3 Done. \n";
}

void Test4()
{
	cout << "\nTest4 4 Start \n";

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

	//graph.PrintTasksExecution();
	graph.WaitAll();
	
	assert(result == 201);
	cout << "Resut " << result<<"\n";
	cout << "Test4 4 Done \n";
}

void Test5()
{
	cout <<"\nTest 5 Start \n";

	using Image = PNGImage;
	using ImagePair = std::pair<PNGImage, PNGImage>;

	PNGImage leftImage;
	PNGImage rightImage;

	TaskRef getLeftImageTask = std::make_shared<InitialTaskNode<int>>
	(
			[&leftImage]()->int
			{
				leftImage = getLeftImage(0);
				std::cout << "Done loading left image \n";
				return 0;
			}
	);

	TaskRef getRightImageTask = std::make_shared<InitialTaskNode<int>>
		(
		[&leftImage, &rightImage]()->int
		{
			rightImage = getRightImage(0);
			std::cout << "Done loading right image \n";
			return 0;
		}
	);

	TaskRef increasePngChannelLeftTask = std::make_shared<InitialTaskNode<int>>
		(
			[ &leftImage]()->int
			{
				increasePNGChannel(leftImage, Image::redOffset, 10);
				std::cout << "Done increase PNG channel left \n";
				return 0;
			}
	);
	TaskRef increasePngChannelRightTask = std::make_shared<InitialTaskNode<int>>
		(
			[&rightImage]() -> int {
				increasePNGChannel(rightImage, Image::blueOffset, 10);
				std::cout << "Done increase PNG channel right \n";
				return 0;
			}
	);
	TaskRef mergeImages = std::make_shared<MultiJoinTaskNode<int>>
		(
			[&leftImage, &rightImage]() -> int {
				mergePNGImages(leftImage, rightImage);
				std::cout << "Done merging images\n";
				return 0;
				},
				std::vector<TaskRef>{ increasePngChannelLeftTask, increasePngChannelRightTask }
		);
	TaskRef writeResult = std::make_shared<InitialTaskNode<int>>
		(
			[&leftImage]() -> int {
				leftImage.write();
				std::cout <<"Done writing image Out0.png \n";
				return 0;
				}
	);

	TaskGraph graph;

	graph.AddTask(getLeftImageTask);
	graph.AddTask(getRightImageTask);

	graph.AddTaskEdge(getRightImageTask, increasePngChannelLeftTask);
	graph.AddTaskEdge(getRightImageTask, increasePngChannelRightTask);

	graph.AddTaskEdges({ increasePngChannelLeftTask, increasePngChannelRightTask }, mergeImages);
	graph.AddTaskEdge(mergeImages, writeResult);

	graph.WaitAll();
	
	cout <<"\nTest 5 Done \n";
}


int main()
{
	Test1();
	Test2();
	Test3();
	Test4();
	Test5();

	cout << "\nType a word and pres [Enter] to exit\n";
	char z;
	cin >> z;
    return 0;
}

