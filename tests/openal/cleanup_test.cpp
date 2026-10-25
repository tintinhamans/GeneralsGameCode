#include "OpenALAudioDevice/OpenALDevice.h"
#include "OpenALAudioDevice/OpenALPcmStream.h"
#include "cleanup_calls.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAILED %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)
int main(int argc, char **argv)
{
    CHECK(argc == 2);
    CleanupTest::Fault fault = CleanupTest::Fault::None;
    if (std::strcmp(argv[1], "pending") == 0)
        fault = CleanupTest::Fault::Pending;
    else if (std::strcmp(argv[1], "stop") == 0)
        fault = CleanupTest::Fault::Stop;
    else if (std::strcmp(argv[1], "detach") == 0)
        fault = CleanupTest::Fault::Detach;
    else if (std::strcmp(argv[1], "source") == 0)
        fault = CleanupTest::Fault::DeleteSource;
    else if (std::strcmp(argv[1], "buffer") == 0)
        fault = CleanupTest::Fault::DeleteBuffer;
    CHECK(fault != CleanupTest::Fault::None);
    std::set_terminate([] { std::_Exit(73); });
    OpenALDevice device;
    OpenALPcmStream stream;
    CHECK(device.openLoopback() && stream.open(device, 48000, 2, 1024));
    std::vector<short> pcm(2048, 1000);
    CHECK(stream.queue(pcm.data(), 1024) == OpenALPcmStream::QueueResult::Accepted);
    CHECK(stream.play());
    std::vector<short> output(256);
    CHECK(device.render(output.data(), 128));
    CleanupTest::arm(fault);
    CHECK(stream.close() && CleanupTest::injected());
    CHECK(fault != CleanupTest::Fault::Stop);
    CHECK(device.render(output.data(), 128));
    CleanupTest::releaseLeakedObjects();
    CHECK(device.close());
    std::puts("safe cleanup failure reported without termination");
}
