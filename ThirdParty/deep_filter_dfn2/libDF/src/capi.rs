use std::boxed::Box;
use std::ffi::{c_char, c_float, CStr};
use std::path::PathBuf;

use ndarray::prelude::*;

use crate::tract::*;

pub struct DF2State {
    m: crate::tract::DfTract,
}

impl DF2State {
    fn new(model_path: &str, channels: usize, atten_lim: f32) -> Self {
        let r_params = RuntimeParams::new(
            channels,
            false,
            atten_lim,
            -15.0f32,
            35.0f32,
            35.0f32,
            ReduceMask::MAX,
        );
        let df_params =
            DfParams::new(PathBuf::from(model_path)).expect("Could not load model from path");
        let m =
            DfTract::new(df_params, &r_params).expect("Could not initialize DeepFilter runtime.");
        DF2State { m }
    }

    fn boxed(self) -> Box<DF2State> {
        Box::new(self)
    }
}

#[no_mangle]
pub unsafe extern "C" fn df2_create(
    path: *const c_char,
    atten_lim: f32,
    _log_level: *const c_char,
) -> *mut DF2State {
    let c_str = CStr::from_ptr(path);
    let path = c_str.to_str().unwrap();
    let df = DF2State::new(path, 1, atten_lim);
    Box::into_raw(df.boxed())
}

#[no_mangle]
pub unsafe extern "C" fn df2_get_frame_length(st: *mut DF2State) -> usize {
    let state = st.as_mut().expect("Invalid pointer");
    state.m.hop_size
}

#[no_mangle]
pub unsafe extern "C" fn df2_set_atten_lim(st: *mut DF2State, lim_db: f32) {
    let state = st.as_mut().expect("Invalid pointer");
    let _ = state.m.set_atten_lim(lim_db);
}

#[no_mangle]
pub unsafe extern "C" fn df2_process_frame(
    st: *mut DF2State,
    input: *mut c_float,
    output: *mut c_float,
) -> c_float {
    let state = st.as_mut().expect("Invalid pointer");
    let input = ArrayView2::from_shape_ptr((1, state.m.hop_size), input);
    let output = ArrayViewMut2::from_shape_ptr((1, state.m.hop_size), output);
    state.m.process(input, output).expect("Failed to process DF2 frame")
}

#[no_mangle]
pub unsafe extern "C" fn df2_free(model: *mut DF2State) {
    let _ = Box::from_raw(model);
}
