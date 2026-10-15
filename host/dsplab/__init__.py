"""Mac-side filter design and analysis for the SC589 audio firmware.

    ccode       write coefficient tables as C source for firmware/audio_loopback/dsp/
    response    magnitude, phase and group delay from coefficients or an impulse response
    boardlog    read the measurements the firmware prints on the UART console
    plot        figure style and the comparison plots the lab reports share
    cdsp        run firmware/audio_loopback/dsp/ on the Mac through ctypes
    wavrun      run a wav file through that code and listen to the result
"""
