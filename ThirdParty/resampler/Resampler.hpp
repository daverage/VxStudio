#ifndef RESAMPLER_HPP
#define RESAMPLER_HPP

#if defined(__aarch64__)
#  include "Resampler-NEON.inl"
#else
#  include "Resampler-Cpp.inl"
#endif

#include <utility>

template <int input_channel_count, int output_channel_count> struct Resampler
{
    Resampler (double source_sample_rate, double target_sample_rate)
        : k_source_sample_time(1.0 / source_sample_rate)
        , k_target_sample_time(1.0 / target_sample_rate)
    {
    }
    
    template <typename Callback> void process
        (
            float * const source_input_buffers [input_channel_count],
            float * const source_output_buffers [output_channel_count],
            float * const target_input_buffers [input_channel_count],
            float * const target_output_buffers [output_channel_count],
            int sample_count,
            Callback && callback
        )
    {
        m_phase += resampler::process<input_channel_count, output_channel_count, Callback>
        (
            source_input_buffers,
            source_output_buffers,
            target_input_buffers,
            target_output_buffers,
            sample_count,
            std::move(callback),
            k_source_sample_time,
            k_target_sample_time,
            m_input_windows,
            m_output_windows,
            m_phase
        );
    }
    
  private:
    
    static constexpr int k_window_size = 4;
    
    alignas(16) float m_input_windows [input_channel_count][k_window_size] {};
    alignas(16) float m_output_windows [output_channel_count][k_window_size] {};
    
    double m_phase {};
    
    double const k_source_sample_time;
    double const k_target_sample_time;
};

#endif /* RESAMPLER_HPP */
