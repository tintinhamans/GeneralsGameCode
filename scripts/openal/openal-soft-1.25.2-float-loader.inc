template<>
void LoadSamples<f32>(std::span<float> const dstSamples, std::span<f32 const> const srcData,
    std::size_t const srcChan, std::size_t const srcOffset, std::size_t const srcStep,
    std::size_t const samplesPerBlock [[maybe_unused]]) noexcept
{
    Expects(srcChan < srcStep);
    auto const *source = srcData.data() + srcOffset*srcStep + srcChan;
    auto *destination = dstSamples.data();
    const auto count = dstSamples.size();
    if(srcStep == 1)
    {
        for(std::size_t i{0};i < count;++i)
            destination[i] = source[i].c_val;
    }
    else if(srcStep == 2)
    {
        for(std::size_t i{0};i < count;++i)
            destination[i] = source[i*2].c_val;
    }
    else
    {
        for(std::size_t i{0};i < count;++i)
            destination[i] = source[i*srcStep].c_val;
    }
}

