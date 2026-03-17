namespace resampler
{

static inline float interpolate
    (
        float blend,
        float const window [4]
    )
{
    // Uses a Catmull–Rom spline to interpolate between the two middle samples in a
    // four-sample window. Say the window is [w0, w1, w2, w3], `interpolate` will
    // blend between `w1` and `w2` according to the value provided in the `blend`
    // parameter which must be in the interval [0; 1].
    
    static float const transform [4][4] =
    {
        {         0.0f, 0.5f * +2.0f,         0.0f,         0.0f },
        { 0.5f * -1.0f,         0.0f, 0.5f * +1.0f,         0.0f },
        { 0.5f * +2.0f, 0.5f * -5.0f, 0.5f * +4.0f, 0.5f * -1.0f },
        { 0.5f * -1.0f, 0.5f * +3.0f, 0.5f * -3.0f, 0.5f * +1.0f }
    };
    
    float u [4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    
    u[0] += transform[3][0]; u[1] += transform[3][1]; u[2] += transform[3][2]; u[3] += transform[3][3];
    u[0] *= blend;           u[1] *= blend;           u[2] *= blend;           u[3] *= blend;
    u[0] += transform[2][0]; u[1] += transform[2][1]; u[2] += transform[2][2]; u[3] += transform[2][3];
    u[0] *= blend;           u[1] *= blend;           u[2] *= blend;           u[3] *= blend;
    u[0] += transform[1][0]; u[1] += transform[1][1]; u[2] += transform[1][2]; u[3] += transform[1][3];
    u[0] *= blend;           u[1] *= blend;           u[2] *= blend;           u[3] *= blend;
    u[0] += transform[0][0]; u[1] += transform[0][1]; u[2] += transform[0][2]; u[3] += transform[0][3];
    u[0] *= window[0];       u[1] *= window[1];       u[2] *= window[2];       u[3] *= window[3];
    
    return u[0] + u[1] + u[2] + u[3];
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
        float * const source_buffer = source_input_buffers[channel];
        float * const target_buffer = target_input_buffers[channel];
        float * const window = input_windows[channel];
        
        double acc = phase;
        
        source_input_sample_index = 0;
        target_input_sample_index = 0;
        
        while (source_input_sample_index < source_sample_count)
        {
            acc += source_sample_time;
            
            window[0] = window[1];
            window[1] = window[2];
            window[2] = window[3];
            window[3] = source_buffer[source_input_sample_index];
            
            while (acc >= target_sample_time)
            {
                acc -= target_sample_time;
                
                float const blend = (float) (1.0 - acc / source_sample_time);
                
                target_buffer[target_input_sample_index] = interpolate(blend, window);
                
                target_input_sample_index++;
            }
            
            source_input_sample_index++;
        }
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
        float * const target_buffer = target_output_buffers[channel];
        float * const source_buffer = source_output_buffers[channel];
        float * const window = output_windows[channel];
        
        double acc = phase;
        
        source_output_sample_index = 0;
        target_output_sample_index = 0;
        
        while (source_output_sample_index < source_sample_count)
        {
            acc += source_sample_time;
            
            while (acc >= target_sample_time)
            {
                acc -= target_sample_time;
                
                window[0] = window[1];
                window[1] = window[2];
                window[2] = window[3];
                window[3] = target_buffer[target_output_sample_index];
                
                target_output_sample_index++;
            }
            
            float const blend = (float) (acc / target_sample_time);
            
            source_buffer[source_output_sample_index] = interpolate(blend, window);
            
            source_output_sample_index++;
        }
    }
    
    // 4. Compute and return the phase difference:
    
    int const target_sample_count = target_input_sample_index;
    
    return static_cast<double>(source_sample_count) * source_sample_time
         - static_cast<double>(target_sample_count) * target_sample_time;
}

} // namespace
