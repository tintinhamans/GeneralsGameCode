#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <vector>

#include "altypes.hpp"

template<typename T>
void LoadSamples(std::span<float>, std::span<T const>, std::size_t, std::size_t,
    std::size_t, std::size_t) noexcept;

#include "../../scripts/openal/openal-soft-1.25.2-float-loader.inc"

static void require(bool condition)
{
    if (!condition) {
        std::fputs("Float loader changed sample bits or crossed a boundary\n", stderr);
        std::abort();
    }
}

int main()
{
    constexpr std::array<uint32_t, 12> patterns = {
        0, 0x80000000, 0x3f800000, 0xbf800000, 0x7f800000, 0xff800000,
        0x7fc12345, 0xffc54321, 1, 0x007fffff, 0x7f7fffff, 0xff7fffff
    };
    constexpr uint32_t guard = 0x12345678;
    unsigned cases = 0;
    for (std::size_t stride : {1u, 2u, 3u, 5u}) {
        std::vector<f32> input(128 * stride);
        for (std::size_t i = 0; i < input.size(); ++i)
            input[i].c_val = std::bit_cast<float>(patterns[i % patterns.size()]);
        for (std::size_t channel = 0; channel < stride; ++channel) {
            for (std::size_t offset : {0u, 1u, 17u, 65u}) {
                for (std::size_t count : {0u, 1u, 2u, 3u, 7u, 31u, 63u}) {
                    std::vector<float> output(count + 2, std::bit_cast<float>(guard));
                    LoadSamples<f32>(std::span{output}.subspan(1, count), input,
                        channel, offset, stride, 1);
                    require(std::bit_cast<uint32_t>(output.front()) == guard);
                    require(std::bit_cast<uint32_t>(output.back()) == guard);
                    for (std::size_t i = 0; i < count; ++i) {
                        const auto expected = patterns[((offset + i) * stride + channel) % patterns.size()];
                        require(std::bit_cast<uint32_t>(output[i + 1]) == expected);
                    }
                    ++cases;
                }
            }
        }
        for (std::size_t i = 0; i < input.size(); ++i)
            require(std::bit_cast<uint32_t>(input[i].c_val) == patterns[i % patterns.size()]);
    }
    std::printf("Float loader: %u bit-preserving stride/offset/count cases passed\n", cases);
}
