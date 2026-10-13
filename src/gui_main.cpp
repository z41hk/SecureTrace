#ifdef _WIN32
#include "GuiApp.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    GuiApp app;
    return app.run();
}
#else
int main() {
    return 0;
}
#endif
