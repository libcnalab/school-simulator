#pragma once

#include <optional>
#include <string>

#include "Microsoft/Xna/Framework/Vector3.hpp"

// Command-line switches: they let the school be driven and verified without a human (screenshots,
// fixed frame counts, scripted walks, population presets).
struct SchoolOptions {
    int width = 1280;
    int height = 720;
    bool fullscreen = false;
    bool vsync = true;
    float mouseSensitivity = 0.0025f;   // radians per pixel
    float volume = 1.0f;
    bool volumeGiven = false;           // --volume (wins over the saved settings)
    bool audio = true;
    bool hud = true;
    bool shadows = true;                // the sun's shadows outdoors (where the device can sample a shadow map)
    bool shadowsGiven = false;          // --no-shadows (wins over the saved settings)
    int maxFrames = 0;                  // 0 = run until the user quits
    std::string screenshotPath;         // written after `screenshotFrame`, then exits
    int screenshotFrame = 10;
    std::string script;                 // semicolon-separated test actions, see SchoolGame::RunScript
    float hitchMs = 0.0f;               // >0: log every frame that took longer than this (milliseconds), with a breakdown
    float fixedTimeStep = 0.0f;         // >0: advance by this many seconds per frame (deterministic runs)

    // the simulation
    std::string preset = "busy";        // empty | light | normal | busy | extreme
    int students = -1;                  // overrides the preset
    unsigned long long seed = 1;
    std::optional<double> startTime;    // seconds since midnight
    float timeScale = 1.0f;
    bool badWeather = false;
    std::string weather = "clear";      // clear | rain | snow | fog
    int maxFull = 700;
    std::string schedulePath;           // a schedule file instead of Content/Data/schedule.cfg

    // where to start
    std::optional<std::string> startRoom;   // a room code such as "S0.03", or a type name such as "Cafeteria"
    std::optional<Microsoft::Xna::Framework::Vector3> startPosition;
    std::optional<float> startYawDegrees;
    std::optional<float> startPitchDegrees;

    bool showHelp = false;
    bool validate = false;              // check plan, timetable and schedule (no window) and exit with the result

    static SchoolOptions Parse(int argc, char** argv);
    static const char* Usage();
};
