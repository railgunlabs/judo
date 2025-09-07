/*
 *  Judo - Embeddable JSON and JSON5 parser.
 *  Copyright (c) 2025 Railgun Labs, LLC
 *
 *  This software is dual-licensed: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation. For the terms of this
 *  license, see <https://www.gnu.org/licenses/>.
 *
 *  Alternatively, you can license this software under a proprietary
 *  license, as set out in <https://railgunlabs.com/judo/license/>.
 */

#ifndef JUDO_UTILS_H
#define JUDO_UTILS_H

#include "judo_config.h"
#include <stdint.h>

#define UNICHAR_C(C) ((unichar)(C))

typedef uint32_t unichar;

#if defined(JUDO_JSON5)
#define IS_SPACE 0x1u
#define ID_START 0x2u
#define ID_EXTEND 0x4u
uint32_t judo_uniflags(unichar cp);
#endif

#endif
