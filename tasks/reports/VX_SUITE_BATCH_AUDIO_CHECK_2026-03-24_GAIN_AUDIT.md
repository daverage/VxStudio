# VX Suite Batch Audio Check

Corpus: `/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/wav`

Products checked in `Voice` mode with representative tuning presets.

## Summary

| Product | Avg spread in (dB) | Avg spread out (dB) | Avg spread improvement (dB) | Avg corr | Avg speech-band corr | Avg residual ratio | Avg target corr | Avg target speech corr | Avg target residual | Avg delta RMS (dB) | Avg peak out (dBFS) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| leveler | 16.459 | 14.495 | 1.964 | 0.924 | 0.924 | 0.381 | 0.924 | 0.924 | 0.381 | -57.177 | -27.664 |
| cleanup | 16.459 | 16.838 | -0.379 | 0.996 | 0.996 | 0.091 | 0.996 | 0.996 | 0.091 | -68.459 | -30.292 |
| denoiser | 5.642 | 6.385 | -0.743 | 0.992 | 0.996 | 0.120 | 0.946 | 0.977 | 0.293 | -62.182 | -31.276 |
| deverb | 16.089 | 16.289 | -0.200 | 0.962 | 0.962 | 0.274 | 0.887 | 0.891 | 0.460 | -58.029 | -27.076 |
| finish | 16.459 | 13.684 | 2.775 | 0.985 | 0.986 | 0.167 | 0.985 | 0.986 | 0.167 | -52.478 | -39.108 |
| optocomp | 16.459 | 13.756 | 2.703 | 0.985 | 0.986 | 0.166 | 0.985 | 0.986 | 0.166 | -52.662 | -38.802 |
| tone | 16.459 | 16.265 | 0.194 | 0.999 | 0.999 | 0.040 | 0.999 | 0.999 | 0.040 | -76.675 | -30.054 |
| proximity | 16.459 | 15.889 | 0.570 | 0.990 | 0.991 | 0.137 | 0.990 | 0.991 | 0.137 | -65.976 | -29.771 |
| subtract | 4.955 | 13.232 | -8.277 | -0.004 | -0.005 | 1.000 | -0.006 | -0.006 | 1.000 | -46.123 | -31.306 |

## leveler

| File | Spread in (dB) | Spread out (dB) | Improvement (dB) | In RMS (dB) | Out RMS (dB) | Delta RMS (dB) | Corr | Speech-band corr | Residual ratio | Target corr | Target speech corr | Target residual | Peak out (dBFS) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| churchill_be_ye_men_of_valour.wav | 22.843 | 20.636 | 2.207 | -17.472 | -17.442 | -27.394 | 0.949 | 0.950 | 0.314 | 0.949 | 0.950 | 0.314 | -0.131 |
| edward_viii_abdication.wav | 10.194 | 7.901 | 2.293 | -30.241 | -31.517 | -37.900 | 0.912 | 0.914 | 0.411 | 0.912 | 0.914 | 0.411 | -6.479 |
| old_letters_librivox.wav | 19.881 | 18.288 | 1.593 | -23.822 | -24.559 | -32.071 | 0.922 | 0.922 | 0.387 | 0.922 | 0.922 | 0.387 | -3.280 |
| princess_elizabeth_21st_birthday.wav | 12.919 | 11.156 | 1.763 | -23.712 | -24.003 | -31.344 | 0.911 | 0.912 | 0.412 | 0.911 | 0.912 | 0.412 | -0.764 |

## cleanup

| File | Spread in (dB) | Spread out (dB) | Improvement (dB) | In RMS (dB) | Out RMS (dB) | Delta RMS (dB) | Corr | Speech-band corr | Residual ratio | Target corr | Target speech corr | Target residual | Peak out (dBFS) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| churchill_be_ye_men_of_valour.wav | 22.843 | 22.739 | 0.104 | -17.472 | -18.174 | -36.118 | 0.996 | 0.996 | 0.091 | 0.996 | 0.996 | 0.091 | -3.509 |
| edward_viii_abdication.wav | 10.194 | 11.241 | -1.047 | -30.241 | -30.959 | -47.097 | 0.992 | 0.994 | 0.124 | 0.992 | 0.994 | 0.124 | -8.645 |
| old_letters_librivox.wav | 19.881 | 19.961 | -0.080 | -23.822 | -24.233 | -44.383 | 0.997 | 0.997 | 0.083 | 0.997 | 0.997 | 0.083 | -5.721 |
| princess_elizabeth_21st_birthday.wav | 12.919 | 13.414 | -0.494 | -23.712 | -24.060 | -46.236 | 0.998 | 0.998 | 0.065 | 0.998 | 0.998 | 0.065 | -3.294 |

## denoiser

| File | Spread in (dB) | Spread out (dB) | Improvement (dB) | In RMS (dB) | Out RMS (dB) | Delta RMS (dB) | Corr | Speech-band corr | Residual ratio | Target corr | Target speech corr | Target residual | Peak out (dBFS) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| churchill_be_ye_men_of_valour.wav | 8.084 | 9.027 | -0.944 | -17.348 | -18.440 | -34.006 | 0.996 | 0.997 | 0.093 | 0.990 | 0.994 | 0.143 | -4.000 |
| edward_viii_abdication.wav | 3.509 | 4.130 | -0.621 | -28.345 | -30.233 | -40.693 | 0.988 | 0.994 | 0.158 | 0.866 | 0.944 | 0.499 | -9.325 |
| old_letters_librivox.wav | 5.344 | 6.077 | -0.732 | -23.311 | -25.120 | -36.705 | 0.994 | 0.997 | 0.113 | 0.964 | 0.986 | 0.266 | -7.255 |
| princess_elizabeth_21st_birthday.wav | 5.632 | 6.306 | -0.675 | -23.212 | -24.782 | -37.325 | 0.993 | 0.996 | 0.117 | 0.964 | 0.986 | 0.266 | -4.524 |

## deverb

| File | Spread in (dB) | Spread out (dB) | Improvement (dB) | In RMS (dB) | Out RMS (dB) | Delta RMS (dB) | Corr | Speech-band corr | Residual ratio | Target corr | Target speech corr | Target residual | Peak out (dBFS) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| churchill_be_ye_men_of_valour.wav | 22.730 | 22.909 | -0.180 | -16.130 | -15.250 | -26.066 | 0.959 | 0.960 | 0.282 | 0.875 | 0.879 | 0.484 | -0.131 |
| edward_viii_abdication.wav | 10.048 | 10.296 | -0.248 | -28.963 | -28.610 | -40.064 | 0.964 | 0.963 | 0.267 | 0.885 | 0.889 | 0.466 | -5.318 |
| old_letters_librivox.wav | 18.730 | 18.916 | -0.186 | -22.463 | -21.685 | -32.898 | 0.963 | 0.963 | 0.271 | 0.889 | 0.891 | 0.459 | -2.722 |
| princess_elizabeth_21st_birthday.wav | 12.850 | 13.037 | -0.186 | -22.259 | -21.845 | -33.088 | 0.962 | 0.963 | 0.274 | 0.902 | 0.903 | 0.432 | -0.131 |

## finish

| File | Spread in (dB) | Spread out (dB) | Improvement (dB) | In RMS (dB) | Out RMS (dB) | Delta RMS (dB) | Corr | Speech-band corr | Residual ratio | Target corr | Target speech corr | Target residual | Peak out (dBFS) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| churchill_be_ye_men_of_valour.wav | 22.843 | 19.855 | 2.988 | -17.472 | -28.685 | -20.231 | 0.992 | 0.993 | 0.123 | 0.992 | 0.993 | 0.123 | -10.688 |
| edward_viii_abdication.wav | 10.194 | 7.691 | 2.503 | -30.241 | -37.204 | -35.193 | 0.983 | 0.985 | 0.186 | 0.983 | 0.985 | 0.186 | -15.104 |
| old_letters_librivox.wav | 19.881 | 17.238 | 2.643 | -23.822 | -32.999 | -27.469 | 0.991 | 0.991 | 0.135 | 0.991 | 0.991 | 0.135 | -15.848 |
| princess_elizabeth_21st_birthday.wav | 12.919 | 9.955 | 2.965 | -23.712 | -33.362 | -27.020 | 0.974 | 0.975 | 0.225 | 0.974 | 0.975 | 0.225 | -14.794 |

## optocomp

| File | Spread in (dB) | Spread out (dB) | Improvement (dB) | In RMS (dB) | Out RMS (dB) | Delta RMS (dB) | Corr | Speech-band corr | Residual ratio | Target corr | Target speech corr | Target residual | Peak out (dBFS) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| churchill_be_ye_men_of_valour.wav | 22.843 | 19.933 | 2.910 | -17.472 | -28.352 | -20.359 | 0.993 | 0.993 | 0.122 | 0.993 | 0.993 | 0.122 | -10.389 |
| edward_viii_abdication.wav | 10.194 | 7.749 | 2.445 | -30.241 | -36.880 | -35.449 | 0.983 | 0.986 | 0.183 | 0.983 | 0.986 | 0.183 | -14.879 |
| old_letters_librivox.wav | 19.881 | 17.320 | 2.561 | -23.822 | -32.644 | -27.659 | 0.991 | 0.992 | 0.133 | 0.991 | 0.992 | 0.133 | -15.493 |
| princess_elizabeth_21st_birthday.wav | 12.919 | 10.022 | 2.898 | -23.712 | -33.022 | -27.182 | 0.975 | 0.975 | 0.223 | 0.975 | 0.975 | 0.223 | -14.449 |

## tone

| File | Spread in (dB) | Spread out (dB) | Improvement (dB) | In RMS (dB) | Out RMS (dB) | Delta RMS (dB) | Corr | Speech-band corr | Residual ratio | Target corr | Target speech corr | Target residual | Peak out (dBFS) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| churchill_be_ye_men_of_valour.wav | 22.843 | 22.638 | 0.205 | -17.472 | -17.332 | -43.094 | 0.999 | 0.999 | 0.049 | 0.999 | 0.999 | 0.049 | -3.269 |
| edward_viii_abdication.wav | 10.194 | 9.957 | 0.236 | -30.241 | -30.126 | -56.649 | 0.999 | 0.999 | 0.046 | 0.999 | 0.999 | 0.046 | -8.208 |
| old_letters_librivox.wav | 19.881 | 19.666 | 0.215 | -23.822 | -23.746 | -51.708 | 0.999 | 0.999 | 0.039 | 0.999 | 0.999 | 0.039 | -5.638 |
| princess_elizabeth_21st_birthday.wav | 12.919 | 12.798 | 0.121 | -23.712 | -23.687 | -55.249 | 1.000 | 1.000 | 0.026 | 1.000 | 1.000 | 0.026 | -3.100 |

## proximity

| File | Spread in (dB) | Spread out (dB) | Improvement (dB) | In RMS (dB) | Out RMS (dB) | Delta RMS (dB) | Corr | Speech-band corr | Residual ratio | Target corr | Target speech corr | Target residual | Peak out (dBFS) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| churchill_be_ye_men_of_valour.wav | 22.843 | 22.079 | 0.765 | -17.472 | -16.928 | -31.521 | 0.983 | 0.986 | 0.181 | 0.983 | 0.986 | 0.181 | -2.712 |
| edward_viii_abdication.wav | 10.194 | 9.817 | 0.377 | -30.241 | -29.816 | -45.712 | 0.988 | 0.989 | 0.156 | 0.988 | 0.989 | 0.156 | -7.508 |
| old_letters_librivox.wav | 19.881 | 19.170 | 0.710 | -23.822 | -23.600 | -41.665 | 0.992 | 0.993 | 0.124 | 0.992 | 0.993 | 0.124 | -5.777 |
| princess_elizabeth_21st_birthday.wav | 12.919 | 12.490 | 0.430 | -23.712 | -23.633 | -45.006 | 0.996 | 0.997 | 0.085 | 0.996 | 0.997 | 0.085 | -3.087 |

## subtract

| File | Spread in (dB) | Spread out (dB) | Improvement (dB) | In RMS (dB) | Out RMS (dB) | Delta RMS (dB) | Corr | Speech-band corr | Residual ratio | Target corr | Target speech corr | Target residual | Peak out (dBFS) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| churchill_be_ye_men_of_valour.wav | 7.302 | 16.554 | -9.252 | -17.280 | -17.765 | -14.599 | 0.021 | 0.024 | 1.000 | 0.022 | 0.024 | 1.000 | -0.131 |
| edward_viii_abdication.wav | 2.891 | 10.845 | -7.954 | -27.557 | -34.098 | -26.619 | -0.021 | -0.028 | 1.000 | -0.028 | -0.031 | 1.000 | -9.666 |
| old_letters_librivox.wav | 4.648 | 12.454 | -7.807 | -23.048 | -28.663 | -22.007 | 0.003 | 0.003 | 1.000 | 0.003 | 0.002 | 1.000 | -10.286 |
| princess_elizabeth_21st_birthday.wav | 4.979 | 13.075 | -8.095 | -22.954 | -26.428 | -21.268 | -0.019 | -0.020 | 1.000 | -0.020 | -0.020 | 1.000 | -5.139 |
