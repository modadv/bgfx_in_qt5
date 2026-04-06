#include "render/pipeline/render_graph.h"

#include <iostream>
#include <string>
#include <vector>

namespace
{
bool expectOrder(const std::vector<std::string>& actual,
                 const std::vector<std::string>& expected,
                 const char* label)
{
    if (actual == expected)
    {
        return true;
    }

    std::cerr << label << " failed" << std::endl;
    std::cerr << "expected:";
    for (const std::string& item : expected)
    {
        std::cerr << ' ' << item;
    }
    std::cerr << std::endl;
    std::cerr << "actual:";
    for (const std::string& item : actual)
    {
        std::cerr << ' ' << item;
    }
    std::cerr << std::endl;
    return false;
}
}

int main()
{
    {
        RenderGraph graph;
        std::vector<std::string> executed;

        RenderGraph::PassDesc post;
        post.name = "post";
        post.stage = RenderGraph::PassStage::PostProcess;
        post.callback = [&]() { executed.push_back("post"); };
        graph.addPass(post);

        RenderGraph::PassDesc geometry;
        geometry.name = "geometry";
        geometry.stage = RenderGraph::PassStage::Geometry;
        geometry.callback = [&]() { executed.push_back("geometry"); };
        graph.addPass(geometry);

        RenderGraph::PassDesc present;
        present.name = "present";
        present.stage = RenderGraph::PassStage::Present;
        present.callback = [&]() { executed.push_back("present"); };
        graph.addPass(present);

        graph.execute();
        if (!expectOrder(executed, { "geometry", "post", "present" }, "stage ordering"))
        {
            return 1;
        }
    }

    {
        RenderGraph graph;
        std::vector<std::string> executed;

        RenderGraph::PassDesc writer;
        writer.name = "gbuffer";
        writer.stage = RenderGraph::PassStage::Geometry;
        writer.writes = { "scene.color" };
        writer.callback = [&]() { executed.push_back("gbuffer"); };
        graph.declareTransientResource("scene.color");
        graph.declareHistoryResource("scene.history");
        graph.addPass(writer);

        RenderGraph::PassDesc reader;
        reader.name = "taa";
        reader.stage = RenderGraph::PassStage::PostProcess;
        reader.reads = { "scene.color" };
        reader.writes = { "scene.history" };
        reader.callback = [&]() { executed.push_back("taa"); };
        graph.addPass(reader);

        if (!graph.execute())
        {
            std::cerr << "resource graph should be valid" << std::endl;
            return 1;
        }
        if (!expectOrder(executed, { "gbuffer", "taa" }, "resource ordering"))
        {
            return 1;
        }
    }

    {
        RenderGraph graph;
        RenderGraph::PassDesc invalid;
        invalid.name = "invalid";
        invalid.stage = RenderGraph::PassStage::PostProcess;
        invalid.reads = { "missing.resource" };
        invalid.callback = []() {};
        graph.addPass(invalid);

        if (graph.execute())
        {
            std::cerr << "graphs with undeclared resources should fail validation" << std::endl;
            return 1;
        }
    }

    return 0;
}
