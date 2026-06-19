use std::boxed::Box;
use std::ffi::{c_char, c_float, CStr};
use std::path::PathBuf;

use ndarray::prelude::*;

use crate::tract::{DfParams, DfTract, ReduceMask, RuntimeParams};

pub struct DFState {
    m: DfTract,
}

impl DFState {
    fn new(model_path: Option<&str>, atten_lim: f32) -> anyhow::Result<Self> {
        let mut r_params = RuntimeParams::default_with_ch(1)
            .with_atten_lim(atten_lim)
            .with_thresholds(-15.0, 35.0, 35.0)
            .with_post_filter(0.0)
            .with_mask_reduce(ReduceMask::MAX);

        r_params.n_ch = 1;

        let df_params = if let Some(path) = model_path {
            DfParams::new(PathBuf::from(path))?
        } else {
            DfParams::default()
        };

        Ok(Self {
            m: DfTract::new(df_params, &r_params)?,
        })
    }

    fn boxed(self) -> Box<DFState> {
        Box::new(self)
    }
}

/// Create a DeepFilterNet model.
///
/// Args:
///     - path: File path to a DeepFilterNet tar.gz model, or NULL/empty to use the embedded
///             default DFN3 model.
///     - atten_lim: Attenuation limit in dB.
///     - _log_level: Unused (retained for ABI compatibility).
#[no_mangle]
pub unsafe extern "C" fn df_create(
    path: *const c_char,
    atten_lim: f32,
    _log_level: *const c_char,
) -> *mut DFState {
    let model_path = if path.is_null() {
        None
    } else {
        match CStr::from_ptr(path).to_str() {
            Ok("") => None,
            Ok(s) => Some(s),
            Err(e) => {
                eprintln!("df_create failed: invalid model path: {e}");
                return std::ptr::null_mut();
            }
        }
    };

    match DFState::new(model_path, atten_lim) {
        Ok(df) => Box::into_raw(df.boxed()),
        Err(e) => {
            eprintln!("df_create failed: {e}");
            std::ptr::null_mut()
        }
    }
}

/// Get DeepFilterNet frame size in samples.
#[no_mangle]
pub unsafe extern "C" fn df_get_frame_length(st: *mut DFState) -> usize {
    let state = st.as_mut().expect("Invalid pointer");
    state.m.hop_size
}

/// Set attenuation limit in dB.
#[no_mangle]
pub unsafe extern "C" fn df_set_atten_lim(st: *mut DFState, lim_db: f32) {
    let state = st.as_mut().expect("Invalid pointer");
    state.m.set_atten_lim(lim_db);
}

/// Process one frame. input and output must be df_get_frame_length() samples long.
/// Returns local SNR estimate.
#[no_mangle]
pub unsafe extern "C" fn df_process_frame(
    st: *mut DFState,
    input: *mut c_float,
    output: *mut c_float,
) -> c_float {
    let state = st.as_mut().expect("Invalid pointer");
    let input_view = ArrayView2::from_shape_ptr((1, state.m.hop_size), input);
    let output_view = ArrayViewMut2::from_shape_ptr((1, state.m.hop_size), output);

    match state.m.process(input_view, output_view) {
        Ok(lsnr) => lsnr,
        Err(e) => {
            eprintln!("df_process_frame error: {e}");
            std::ptr::copy_nonoverlapping(input, output, state.m.hop_size);
            -15.0
        }
    }
}

/// Free a DeepFilterNet model.
#[no_mangle]
pub unsafe extern "C" fn df_free(model: *mut DFState) {
    if !model.is_null() {
        let _ = Box::from_raw(model);
    }
}

/// Stub kept for ABI compatibility.
#[no_mangle]
pub unsafe extern "C" fn df_next_log_msg(_st: *mut DFState) -> *mut c_char {
    std::ptr::null_mut()
}

#[no_mangle]
pub unsafe extern "C" fn df_free_log_msg(_ptr: *mut c_char) {}

/// Set DeepFilterNet post-filter beta.
#[no_mangle]
pub unsafe extern "C" fn df_set_post_filter_beta(st: *mut DFState, beta: f32) {
    let state = st.as_mut().expect("Invalid pointer");
    state.m.set_pf_beta(beta);
}
