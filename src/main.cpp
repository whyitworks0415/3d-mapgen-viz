#include "app/Application.h"
#include "core/Logger.h"

#include <exception>

int main() {
    try {
        mgv::Application app;
        app.run();
    } catch (const std::exception& e) {
        mgv::Logger::error(std::string("Fatal: ") + e.what());
        return EXIT_FAILURE;
    } catch (...) {
        mgv::Logger::error("Fatal: unknown exception");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
