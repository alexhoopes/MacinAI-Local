/*----------------------------------------------------------------------
    MathCompat.h - Portable Math Functions for Cross-Platform Reproducibility

    Provides expf_compat, cosf_compat, sinf_compat, sqrtf_compat that
    produce identical results to glibc/x86 implementations, ensuring
    the inference engine matches PyTorch output regardless of platform.

    The Metrowerks Standard Library (MSL) on PPC uses different polynomial
    approximations than glibc, causing 1-ULP rounding differences that
    compound through 18 transformer layers over 150+ autoregressive tokens.

    These implementations use the same minimax polynomial coefficients
    as musl libc (MIT licensed), computed in double precision then
    rounded to float, matching IEEE 754 correctly-rounded behavior.

    For CodeWarrior Pro 5 on System 7.5.3+

    Written by Alex Hoopes
    Copyright (c) 2026 OldAppleStuff / Alex Hoopes

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program. If not, see <https://www.gnu.org/licenses/>.
----------------------------------------------------------------------*/

#ifndef MATHCOMPAT_H
#define MATHCOMPAT_H

/* Drop-in replacements for math.h functions.
   Use these instead of exp/cos/sin/sqrt in inference code. */
float expf_compat(float x);
float sinf_compat(float x);
float cosf_compat(float x);
float sqrtf_compat(float x);

#endif /* MATHCOMPAT_H */
