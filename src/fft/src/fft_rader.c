/*
 * Copyright (c) 2007, 2008, 2009, 2010, 2011, 2012 Joseph Gaeddert
 * Copyright (c) 2007, 2008, 2009, 2010, 2011, 2012 Virginia Polytechnic
 *                                      Institute & State University
 *
 * This file is part of liquid.
 *
 * liquid is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * liquid is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with liquid.  If not, see <http://www.gnu.org/licenses/>.
 */

//
// fft_rader.c : definitions for transforms of prime length using
//               Rader's algorithm
//
// References:
//  [Rader:1968] Charles M. Rader, "Discrete Fourier Transforms When
//      the Number of Data Samples Is Prime," Proceedings of the IEEE,
//      vol. 56, number 6, pp. 1107--1108, June 1968
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "liquid.internal.h"

#define FFT_DEBUG_RADER 0

// create FFT plan for regular DFT
//  _nfft   :   FFT size
//  _x      :   input array [size: _nfft x 1]
//  _y      :   output array [size: _nfft x 1]
//  _dir    :   fft direction: {FFT_FORWARD, FFT_REVERSE}
//  _method :   fft method
FFT(plan) FFT(_create_plan_rader)(unsigned int _nfft,
                                  TC *         _x,
                                  TC *         _y,
                                  int          _dir,
                                  int          _flags)
{
    // allocate plan and initialize all internal arrays to NULL
    FFT(plan) q = (FFT(plan)) malloc(sizeof(struct FFT(plan_s)));

    q->nfft      = _nfft;
    q->x         = _x;
    q->y         = _y;
    q->flags     = _flags;
    q->kind      = LIQUID_FFT_DFT_1D;
    q->direction = (_dir == FFT_FORWARD) ? FFT_FORWARD : FFT_REVERSE;
    q->method    = LIQUID_FFT_METHOD_RADER;

    q->execute   = FFT(_execute_rader);

    // allocate memory for sub-transforms
    q->data.rader.x_prime = (TC*)malloc((q->nfft-1)*sizeof(TC));
    q->data.rader.X_prime = (TC*)malloc((q->nfft-1)*sizeof(TC));

    // create sub-FFT of size nfft-1
    q->data.rader.fft = FFT(_create_plan)(q->nfft-1,
                                          q->data.rader.x_prime,
                                          q->data.rader.X_prime,
                                          FFT_FORWARD,
                                          q->flags);

    // create sub-IFFT of size nfft-1
    q->data.rader.ifft = FFT(_create_plan)(q->nfft-1,
                                           q->data.rader.X_prime,
                                           q->data.rader.x_prime,
                                           FFT_REVERSE,
                                           q->flags);

    // compute primitive root of nfft
    unsigned int g = liquid_primitive_root_prime(q->nfft);

    // create and initialize sequence
    q->data.rader.seq = (unsigned int *)malloc((q->nfft-1)*sizeof(unsigned int));
    unsigned int i;
    for (i=0; i<q->nfft-1; i++)
        q->data.rader.seq[i] = liquid_modpow(g, i+1, q->nfft);
    
    // compute DFT of sequence { exp(-j*2*pi*g^i/nfft }, size: nfft-1
    // NOTE: R[0] = -1, |R[k]| = sqrt(nfft) for k != 0
    // (use newly-created FFT plan of length nfft-1)
    T d = (q->direction == FFT_FORWARD) ? -1.0 : 1.0;
    for (i=0; i<q->nfft-1; i++) {
        float complex t = cexpf(_Complex_I*d*2*M_PI*q->data.rader.seq[i]/(T)(q->nfft));
#if LIQUID_FPM
        q->data.rader.x_prime[i] = CQ(_float_to_fixed)(t);
#else
        q->data.rader.x_prime[i] = t;
#endif
    }
    FFT(_execute)(q->data.rader.fft);

    // copy result to R
    q->data.rader.R = (TC*)malloc((q->nfft-1)*sizeof(TC));
    memmove(q->data.rader.R, q->data.rader.X_prime, (q->nfft-1)*sizeof(TC));
    
    // return main object
    return q;
}

// destroy FFT plan
void FFT(_destroy_plan_rader)(FFT(plan) _q)
{
    // free data specific to Rader's algorithm
    free(_q->data.rader.seq);       // sequence
    free(_q->data.rader.R);         // pre-computed transform of exp(j*2*pi*seq)
    free(_q->data.rader.x_prime);   // sub-transform input array
    free(_q->data.rader.X_prime);   // sub-transform output array

    FFT(_destroy_plan)(_q->data.rader.fft);
    FFT(_destroy_plan)(_q->data.rader.ifft);

    // free main object memory
    free(_q);
}

// execute Rader's algorithm
void FFT(_execute_rader)(FFT(plan) _q)
{
    unsigned int i;

    // compute DFT of permuted sequence, size: nfft-1
    for (i=0; i<_q->nfft-1; i++) {
        // reverse sequence
        unsigned int k = _q->data.rader.seq[_q->nfft-1-i-1];
        _q->data.rader.x_prime[i] = _q->x[k];
    }
    // compute sub-FFT
    // equivalent to: FFT(_run)(_q->nfft-1, xp, Xp, FFT_FORWARD, 0);
    FFT(_execute)(_q->data.rader.fft);

    // compute inverse FFT of product
    for (i=0; i<_q->nfft-1; i++) {
#if LIQUID_FPM
        TC t = CQ(_mul)(_q->data.rader.X_prime[i], _q->data.rader.R[i]);
        _q->data.rader.X_prime[i].real += t.real;
        _q->data.rader.X_prime[i].imag += t.imag;
#else
        _q->data.rader.X_prime[i] *= _q->data.rader.R[i];
#endif
    }

    // compute sub-IFFT
    // equivalent to: FFT(_run)(_q->nfft-1, Xp, xp, FFT_REVERSE, 0);
    FFT(_execute)(_q->data.rader.ifft);

    // set DC value
#if LIQUID_FPM
    _q->y[0].real = 0;
    _q->y[0].imag = 0;
    for (i=0; i<_q->nfft; i++) {
        _q->y[0].real += _q->x[i].real;
        _q->y[0].imag += _q->x[i].imag;
    }
#else
    _q->y[0] = 0.0f;
    for (i=0; i<_q->nfft; i++)
        _q->y[0] += _q->x[i];
#endif

    // reverse permute result, scale, and add offset x[0]
    for (i=0; i<_q->nfft-1; i++) {
        unsigned int k = _q->data.rader.seq[i];

#if LIQUID_FPM
        _q->y[k].real = _q->data.rader.x_prime[i].real / (T)(_q->nfft-1) + _q->x[0].real;
        _q->y[k].imag = _q->data.rader.x_prime[i].imag / (T)(_q->nfft-1) + _q->x[0].imag;
#else
        _q->y[k] = _q->data.rader.x_prime[i] / (T)(_q->nfft-1) + _q->x[0];
#endif
    }
}

