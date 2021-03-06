#include "glfw_view.hpp"
#include "glfw_renderer_frontend.hpp"
#include "settings_json.hpp"

#include <mbgl/util/default_styles.hpp>
#include <mbgl/util/logging.hpp>
#include <mbgl/util/platform.hpp>
#include <mbgl/util/default_thread_pool.hpp>
#include <mbgl/storage/default_file_source.hpp>
#include <mbgl/style/style.hpp>
#include <mbgl/renderer/renderer.hpp>

#include <csignal>
#include <getopt.h>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstdio>
#include <array>

namespace {

GLFWView* view = nullptr;

} // namespace

void quit_handler(int) {
    if (view) {
        mbgl::Log::Info(mbgl::Event::Setup, "waiting for quit...");
        view->setShouldClose();
    } else {
        exit(0);
    }
}

int main(int argc, char *argv[]) {
    // Load settings
    mbgl::Settings_JSON settings;

    bool fullscreen = false;
    bool benchmark = false;
    std::string style;

    const struct option long_options[] = {
        {"fullscreen", no_argument, nullptr, 'f'},
        {"benchmark", no_argument, nullptr, 'b'},
        {"offline", no_argument, nullptr, 'o'},
        {"style", required_argument, nullptr, 's'},
        {"lon", required_argument, nullptr, 'x'},
        {"lat", required_argument, nullptr, 'y'},
        {"zoom", required_argument, nullptr, 'z'},
        {"bearing", required_argument, nullptr, 'r'},
        {"pitch", required_argument, nullptr, 'p'},
        {nullptr, 0, nullptr, 0}
    };

    while (true) {
        int option_index = 0;
        int opt = getopt_long(argc, argv, "fbs:", long_options, &option_index);
        if (opt == -1) break;
        switch (opt)
        {
        case 'f':
            fullscreen = true;
            break;
        case 'b':
            benchmark = true;
            break;
        case 'o':
            settings.online = false;
            break;
        case 's':
            style = std::string(optarg);
            break;
        case 'x':
            settings.longitude = atof(optarg);
            break;
        case 'y':
            settings.latitude = atof(optarg);
            break;
        case 'z':
            settings.zoom = atof(optarg);
            break;
        case 'r':
            settings.bearing = atof(optarg);
            break;
        case 'p':
            settings.pitch = atof(optarg);
            break;
        default:
            break;
        }

    }

    // sigint handling
    struct sigaction sigIntHandler;
    sigIntHandler.sa_handler = quit_handler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;
    sigaction(SIGINT, &sigIntHandler, nullptr);

    if (benchmark) {
        mbgl::Log::Info(mbgl::Event::General, "BENCHMARK MODE: Some optimizations are disabled.");
    }

    GLFWView backend(fullscreen, benchmark);
    view = &backend;

    mbgl::DefaultFileSource fileSource("/tmp/mbgl-cache.db", ".");
    if (!settings.online) {
        fileSource.setOnlineStatus(false);
        mbgl::Log::Warning(mbgl::Event::Setup, "Application is offline. Press `O` to toggle online status.");
    }

    // Set access token if present
    const char *token = getenv("MAPBOX_ACCESS_TOKEN");
    if (token == nullptr) {
        mbgl::Log::Warning(mbgl::Event::Setup, "no access token set. mapbox.com tiles won't work.");
    } else {
        fileSource.setAccessToken(std::string(token));
    }

    mbgl::ThreadPool threadPool(4);
    GLFWRendererFrontend rendererFrontend { std::make_unique<mbgl::Renderer>(backend, view->getPixelRatio(), fileSource, threadPool), backend };
    mbgl::Map map(rendererFrontend, backend, view->getSize(), view->getPixelRatio(), fileSource, threadPool);

    backend.setMap(&map);

    if (!style.empty() && style.find("://") == std::string::npos) {
        style = std::string("file://") + style;
    }

    map.setLatLngZoom(mbgl::LatLng(settings.latitude, settings.longitude), settings.zoom);
    map.setBearing(settings.bearing);
    map.setPitch(settings.pitch);
    map.setDebug(mbgl::MapDebugOptions(settings.debug));

    view->setOnlineStatusCallback([&settings, &fileSource]() {
        settings.online = !settings.online;
        fileSource.setOnlineStatus(settings.online);
        mbgl::Log::Info(mbgl::Event::Setup, "Application is %s. Press `O` to toggle online status.", settings.online ? "online" : "offline");
    });

    view->setChangeStyleCallback([&map] () {
        static uint8_t currentStyleIndex;

        if (++currentStyleIndex == mbgl::util::default_styles::numOrderedStyles) {
            currentStyleIndex = 0;
        }

        mbgl::util::default_styles::DefaultStyle newStyle = mbgl::util::default_styles::orderedStyles[currentStyleIndex];
        map.getStyle().loadURL(newStyle.url);
        view->setWindowTitle(newStyle.name);

        mbgl::Log::Info(mbgl::Event::Setup, "Changed style to: %s", newStyle.name);
    });

    view->setPauseResumeCallback([&fileSource] () {
        static bool isPaused = false;

        if (isPaused) {
            fileSource.resume();
        } else {
            fileSource.pause();
        }

        isPaused = !isPaused;
    });

    // Load style
    if (style.empty()) {
        const char *url = getenv("MAPBOX_STYLE_URL");
        if (url == nullptr) {
            mbgl::util::default_styles::DefaultStyle newStyle = mbgl::util::default_styles::orderedStyles[0];
            style = newStyle.url;
            view->setWindowTitle(newStyle.name);
        } else {
            style = url;
            view->setWindowTitle(url);
        }
    }

    map.getStyle().loadURL(style);

    view->run();

    // Save settings
    mbgl::LatLng latLng = map.getLatLng();
    settings.latitude = latLng.latitude();
    settings.longitude = latLng.longitude();
    settings.zoom = map.getZoom();
    settings.bearing = map.getBearing();
    settings.pitch = map.getPitch();
    settings.debug = mbgl::EnumType(map.getDebug());
    settings.save();
    mbgl::Log::Info(mbgl::Event::General,
                    R"(Exit location: --lat="%f" --lon="%f" --zoom="%f" --bearing "%f")",
                    settings.latitude, settings.longitude, settings.zoom, settings.bearing);

    view = nullptr;
    return 0;
}
