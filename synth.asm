; ---- Metadata ----
SAMPLE_RATE 48000
REGISTERS   8
STATES      4

; All instructions are:
; OP dst src1 src2/immediate

; --- Constants ---
CONST   rX   value          ; rX = constant float

; --- Arithmetic ---
ADD     rX   rA   rB        ; rX = rA + rB
MUL     rX   rA   rB        ; rX = rA * rB
SUB     rX   rA   rB
DIV     rX   rA   rB

; --- Oscillator ---
; state slot holds phase
OSC     rX   sY   freq      ; rX = sin(phase)
                             ; phase += 2π*freq/SR

; --- Noise ---
NOISE   rX   sY              ; sY used as RNG state

; --- Nonlinearities ---
SIN     rX   rA
TANH    rX   rA
CLIP    rX   rA

; --- Mixing ---
OUT     rX                   ; final output register

