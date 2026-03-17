#if !defined(__ARM_NEON__)
#  error
#endif

#include <arm_neon.h>

namespace resampler
{

static inline float32x4_t interpolate
    (
        float32_t blend,
        float32x4_t window
    )
{
    // Uses a Catmull–Rom spline to interpolate between the two middle samples in a
    // four-sample window. Say the window is [w0, w1, w2, w3], `interpolate` will
    // blend between `w1` and `w2` according to the value provided in the `blend`
    // parameter which must be in the interval [0; 1].
    
    alignas(16) static float const transform [4][4] =
    {
        {         0.0f, 0.5f * +2.0f,         0.0f,         0.0f },
        { 0.5f * -1.0f,         0.0f, 0.5f * +1.0f,         0.0f },
        { 0.5f * +2.0f, 0.5f * -5.0f, 0.5f * +4.0f, 0.5f * -1.0f },
        { 0.5f * -1.0f, 0.5f * +3.0f, 0.5f * -3.0f, 0.5f * +1.0f }
    };
    
    float32x4_t const s = vdupq_n_f32(blend);
    
    float32x4_t u;
    
    u = vld1q_f32(transform[3]);
    u = vmulq_f32(u, s);
    u = vaddq_f32(u, vld1q_f32(transform[2]));
    u = vmulq_f32(u, s);
    u = vaddq_f32(u, vld1q_f32(transform[1]));
    u = vmulq_f32(u, s);
    u = vaddq_f32(u, vld1q_f32(transform[0]));
    u = vmulq_f32(u, window);
    
    u = vpaddq_f32(u, u);
    u = vpaddq_f32(u, u);
    
    return u;
}

template <int input_channel_count, int output_channel_count, typename Callback>
static inline double process
    (
        float * const source_input_buffers [input_channel_count],
        float * const source_output_buffers [output_channel_count],
        float * const target_input_buffers [input_channel_count],
        float * const target_output_buffers [output_channel_count],
        int const source_sample_count,
        Callback && callback,
        double const source_sample_time,
        double const target_sample_time,
        float input_windows [input_channel_count][4],
        float output_windows [output_channel_count][4],
        double const phase
    )
{
    // 1. Resample from the source sample rate to the target sample rate:
    
    int source_input_sample_index, target_input_sample_index = 0 /* suppress warning */;
    
    for (int channel = 0; channel < input_channel_count; channel++)
    {
        float32_t * const source_buffer = source_input_buffers[channel];
        float32_t * const target_buffer = target_input_buffers[channel];
        float32x4_t window = vld1q_f32(input_windows[channel]);
        
        float64_t acc = phase;
        
        source_input_sample_index = 0;
        target_input_sample_index = 0;
        
        while (source_input_sample_index < source_sample_count)
        {
            acc += source_sample_time;
            
            window = vextq_f32(window, window, 1);
            window = vld1q_lane_f32(source_buffer + source_input_sample_index, window, 3);
            
            while (acc >= target_sample_time)
            {
                acc -= target_sample_time;
                
                float32_t const blend = (float32_t) (1.0 - acc / source_sample_time);
                
                float32x4_t const beta = interpolate(blend, window);
                
                vst1q_lane_f32(target_buffer + target_input_sample_index, beta, 0);
                
                target_input_sample_index++;
            }
            
            source_input_sample_index++;
        }
        
        vst1q_f32(input_windows[channel], window);
    }
    
    // 2. Process the target buffers (at the target sample rate):
    
    callback
    (
        target_input_buffers,
        target_output_buffers,
        target_input_sample_index
    );
    
    // 3. Resample from the target sample rate to the source sample rate:
    
    int source_output_sample_index, target_output_sample_index;
    
    for (int channel = 0; channel < output_channel_count; channel++)
    {
        float32_t * const target_buffer = target_output_buffers[channel];
        float32_t * const source_buffer = source_output_buffers[channel];
        float32x4_t window = vld1q_f32(output_windows[channel]);
        
        float64_t acc = phase;
        
        source_output_sample_index = 0;
        target_output_sample_index = 0;
        
        while (source_output_sample_index < source_sample_count)
        {
            acc += source_sample_time;
            
            while (acc >= target_sample_time)
            {
                acc -= target_sample_time;
                
                window = vextq_f32(window, window, 1);
                window = vld1q_lane_f32(target_buffer + target_output_sample_index, window, 3);
                
                target_output_sample_index++;
            }
            
            float32_t const blend = (float32_t) (acc / target_sample_time);
            
            float32x4_t const beta = interpolate(blend, window);
            
            vst1q_lane_f32(source_buffer + source_output_sample_index, beta, 0);
            
            source_output_sample_index++;
        }
        
        vst1q_f32(output_windows[channel], window);
    }
    
    // 4. Compute and return the phase difference:
    
    int const target_sample_count = target_input_sample_index;
    
    return static_cast<double>(source_sample_count) * source_sample_time
         - static_cast<double>(target_sample_count) * target_sample_time;
}

} // namespace
