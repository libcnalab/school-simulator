#include "App/SchoolOptions.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

bool Match(const char* arg, const char* name, const char*& value, int& i, int argc, char** argv)
{
    const std::size_t n = std::strlen(name);
    if (std::strncmp(arg, name, n) != 0) return false;
    if (arg[n] == '=') { value = arg + n + 1; return true; }
    if (arg[n] == '\0' && i + 1 < argc) { value = argv[++i]; return true; }
    return false;
}

bool ParseClock(const char* text, double& seconds)
{
    int h = 0, m = 0;
    if (std::sscanf(text, "%d:%d", &h, &m) == 2) {
        seconds = h * 3600.0 + m * 60.0;
        return true;
    }
    char* end = nullptr;
    const double hours = std::strtod(text, &end);
    if (end == text) return false;
    seconds = hours * 3600.0;
    return true;
}

}  // namespace

const char* SchoolOptions::Usage()
{
    return
        "cna-school-simulator [options]\n"
        "  --width N --height N      window size (default 1280x720)      --fullscreen   start fullscreen\n"
        "  --no-vsync                disable vertical sync                --no-hud       draw only the 3D scene\n"
        "  --no-shadows              no sun shadows outdoors\n"
        "  --sensitivity F           mouse sensitivity multiplier (default 1.0)\n"
        "  --volume F                master volume 0..1                  --no-audio     no sound at all\n"
        "  --preset NAME             population: empty, light, normal, busy, extreme (default busy: 1000 students)\n"
        "  --students N              exact number of students (overrides the preset)\n"
        "  --seed N                  simulation seed (default 1)\n"
        "  --time HH:MM              start the school clock there (default 07:50)\n"
        "  --speed F                 time scale at start (default 1)\n"
        "  --weather NAME            clear, rain, snow or fog (rain and snow keep the breaks indoors)\n"
        "  --max-full N              cap on NPCs walked at full fidelity (default 700)\n"
        "  --schedule FILE           bell schedule file (default Content/Data/schedule.cfg, else built in)\n"
        "  --room CODE|TYPE          start in a room, e.g. S0.03, Cafeteria, Gym, Library\n"
        "  --pos X,Y,Z --yaw DEG --pitch DEG   start pose (metres, degrees)\n"
        "  --frames N                exit after N frames\n"
        "  --screenshot FILE.png [--screenshot-frame N]   save the back buffer and exit\n"
        "  --hitch-log MS            log every frame longer than MS with where the time went (stutter hunting)\n"
        "  --fixed-step SECONDS      advance by a fixed step per frame (deterministic runs)\n"
        "  --script \"a;b;c\"          scripted actions (see src/App/SchoolGame.cpp, RunScript)\n"
        "  --validate                check the plan, timetable and schedule (no window); exit status 1 if invalid\n";
}

SchoolOptions SchoolOptions::Parse(int argc, char** argv)
{
    SchoolOptions o;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        const char* v = nullptr;
        if (Match(a, "--width", v, i, argc, argv)) o.width = std::atoi(v);
        else if (Match(a, "--height", v, i, argc, argv)) o.height = std::atoi(v);
        else if (!std::strcmp(a, "--fullscreen")) o.fullscreen = true;
        else if (!std::strcmp(a, "--no-vsync")) o.vsync = false;
        else if (!std::strcmp(a, "--no-hud")) o.hud = false;
        else if (!std::strcmp(a, "--no-shadows")) {
            o.shadows = false;
            o.shadowsGiven = true;
        }
        else if (Match(a, "--sensitivity", v, i, argc, argv)) o.mouseSensitivity = 0.0025f * static_cast<float>(std::atof(v));
        else if (Match(a, "--volume", v, i, argc, argv)) {
            o.volume = static_cast<float>(std::atof(v));
            o.volumeGiven = true;
        }
        else if (!std::strcmp(a, "--no-audio")) o.audio = false;
        else if (Match(a, "--preset", v, i, argc, argv)) o.preset = v;
        else if (Match(a, "--students", v, i, argc, argv)) o.students = std::atoi(v);
        else if (Match(a, "--seed", v, i, argc, argv)) o.seed = std::strtoull(v, nullptr, 10);
        else if (Match(a, "--time", v, i, argc, argv)) {
            double s = 0.0;
            if (ParseClock(v, s)) o.startTime = s;
            else std::fprintf(stderr, "cna-school-simulator: bad --time '%s' (use HH:MM)\n", v);
        }
        else if (Match(a, "--speed", v, i, argc, argv)) o.timeScale = static_cast<float>(std::atof(v));
        else if (Match(a, "--weather", v, i, argc, argv)) o.weather = v;
        else if (!std::strcmp(a, "--bad-weather")) o.weather = "rain";
        else if (Match(a, "--max-full", v, i, argc, argv)) o.maxFull = std::atoi(v);
        else if (Match(a, "--schedule", v, i, argc, argv)) o.schedulePath = v;
        else if (Match(a, "--room", v, i, argc, argv)) o.startRoom = v;
        else if (Match(a, "--yaw", v, i, argc, argv)) o.startYawDegrees = static_cast<float>(std::atof(v));
        else if (Match(a, "--pitch", v, i, argc, argv)) o.startPitchDegrees = static_cast<float>(std::atof(v));
        else if (Match(a, "--pos", v, i, argc, argv)) {
            float x = 0, y = 0, z = 0;
            if (std::sscanf(v, "%f,%f,%f", &x, &y, &z) == 3) o.startPosition = Microsoft::Xna::Framework::Vector3(x, y, z);
        }
        else if (Match(a, "--frames", v, i, argc, argv)) o.maxFrames = std::atoi(v);
        else if (Match(a, "--screenshot-frame", v, i, argc, argv)) o.screenshotFrame = std::atoi(v);
        else if (Match(a, "--screenshot", v, i, argc, argv)) o.screenshotPath = v;
        else if (Match(a, "--hitch-log", v, i, argc, argv)) o.hitchMs = static_cast<float>(std::atof(v));
        else if (Match(a, "--fixed-step", v, i, argc, argv)) o.fixedTimeStep = static_cast<float>(std::atof(v));
        else if (Match(a, "--script", v, i, argc, argv)) o.script = v;
        else if (!std::strcmp(a, "--validate")) o.validate = true;
        else if (!std::strcmp(a, "--help") || !std::strcmp(a, "-h")) o.showHelp = true;
        else std::fprintf(stderr, "cna-school-simulator: ignoring unknown option '%s'\n", a);
    }
    o.badWeather = o.weather == "rain" || o.weather == "snow";
    if (o.width < 320) o.width = 320;
    if (o.height < 200) o.height = 200;
    return o;
}
