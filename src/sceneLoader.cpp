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
        bool isPunctual = light.value("type", "orthographic") == "punctual";
        float fov = light.value("fovDeg", 90.0f);
        float coneAngle = light.value("coneAngle", 90.0f) * M_PI / 180.0f;

        scene.lights.push_back(
            {
                { light["position"]["x"], light["position"]["y"], light["position"]["z"] },
                light["resolutionX"], light["resolutionY"],
                isPunctual,
                fov,
                coneAngle
            }
        );
    }

    if (j.contains("sphereLights"))
    {
        auto& sl = j["sphereLights"];
        scene.sphereLights.enabled = sl.value("enabled", false);
        scene.sphereLights.count = sl.value("count", 16);
        scene.sphereLights.marginFactor = sl.value("marginFactor", 2.0f);
        scene.sphereLights.coneAngleMarginDeg = sl.value("coneAngleMarginDeg", 5.0f);
    }

    for (auto& mesh : j["meshes"])
    {
        scene.meshes.push_back(
            {
                mesh["objPath"],
                mesh.value("isCausticsMesh", false),
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