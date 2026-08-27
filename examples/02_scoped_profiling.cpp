#include "memsentry/memsentry.hpp"
#include <thread>
#include <vector>

void worker_audio() {
    MEMSENTRY_SCOPE_TAG("AudioEngine");
    for (int i = 0; i < 50; ++i) {
        char* audio_sample = new char[512];
        if (i % 5 != 0) {
            delete[] audio_sample;
        }
    }
}

void worker_physics() {
    MEMSENTRY_SCOPE_TAG("PhysicsEngine");
    for (int i = 0; i < 30; ++i) {
        double* rigid_body = new double[64];
        if (i % 3 != 0) {
            delete[] rigid_body;
        }
    }
}

void worker_renderer() {
    MEMSENTRY_SCOPE_TAG("RenderPipeline");
    for (int i = 0; i < 20; ++i) {
        float* vertex_stream = new float[1024];
        if (i % 2 != 0) {
            delete[] vertex_stream;
        }
    }
}

int main() {
    memsentry::init();

    std::thread t1(worker_audio);
    std::thread t2(worker_physics);
    std::thread t3(worker_renderer);

    t1.join();
    t2.join();
    t3.join();

    memsentry::export_html("report_02_scoped.html");

    return 0;
}
