/*----------------------------------------------------------------------
    InferenceGuard.h - Pre/Post Processing for Model Inference

    Pre-processing: Routes queries to lookup table or canned responses
    before model inference when appropriate.

    Post-processing: Cleans up model output (truncate repetition,
    remove artifacts).


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

#ifndef INFERENCEGUARD_H
#define INFERENCEGUARD_H

#include <Types.h>
#include "MacSpecsTable.h"

/* Result of pre-processing: what to do with this query */
typedef enum {
    kRouteToModel,      /* No match, run the neural model */
    kRouteToLookup,     /* Hardware spec question, use lookup table */
    kRouteToCanned      /* Boundary/refusal, use canned response */
} InferenceRoute;

/* Pre-processing result */
typedef struct {
    InferenceRoute route;
    const MacSpec *spec;            /* non-nil if kRouteToLookup */
    char cannedResponse[256];       /* filled if kRouteToCanned */
} PreProcessResult;

/*----------------------------------------------------------------------
    InferenceGuard_PreProcess - Analyze query before model inference

    Examines the user query and determines the best route:
    1. If it mentions a Mac model name -> lookup table (100% accurate)
    2. If it matches a refusal pattern -> canned response
    3. Otherwise -> pass to model

    Returns: PreProcessResult with route and data
----------------------------------------------------------------------*/
PreProcessResult InferenceGuard_PreProcess(const char *query);

/*----------------------------------------------------------------------
    InferenceGuard_PostProcess - Clean model output

    Fixes common model output problems:
    - Truncates repetitive text (same phrase repeated)
    - Removes trailing garbage/artifacts
    - Ensures clean sentence ending

    Modifies the output buffer in place.
    Returns: new string length
----------------------------------------------------------------------*/
short InferenceGuard_PostProcess(char *output, short len);

#endif /* INFERENCEGUARD_H */
