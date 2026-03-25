#include "sceneLoader.h"
#include "json.hpp"
#include <fstream>

using json = nlohmann::json;

SceneConfig loadScene(const std::string& path)
{
    std::ifstream f(path);
    json j = json::parse(f);

    SceneConfig scene;

    for (auto& light : j["lights"])
    {
        scene.ligths.push_back(
            {
                { light["position"]["x"], light["position"]["y"], light["position"]["z"] },
                light["resolutionX"], light["resolutionY"]
            }
        );
    }

    for (auto& mesh : j["meshes"])
    {
        scene.meshes.push_back(
            {
                mesh["objPath"],
                { mesh["position"]["x"], mesh["position"]["y"], mesh["position"]["z"] },
                { mesh["rotation"]["x"], mesh["rotation"]["y"], mesh["rotation"]["z"] },
                { mesh["scale"]["x"],    mesh["scale"]["y"],    mesh["scale"]["z"] }
            }
        );
    }

    auto& g = j["grid"];

    scene.grid = {
        { g["origin"]["x"], g["origin"]["y"], g["origin"]["z"] },
        { g["size"]["x"],   g["size"]["y"],   g["size"]["z"]},
        { g["resolution"]["x"], g["resolution"]["y"], g["resolution"]["z"] }
    };

    return scene;
}