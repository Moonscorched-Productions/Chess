#include "app/App.h"

int main(int argc, char** argv) {
    app::App application(app::Options::parse(argc, argv));
    return application.run();
}
