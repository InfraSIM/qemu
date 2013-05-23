/*
 * Dynamically construct ACPI elements
 *
 * Copyright (C) 2012  Corey Minyard <cminyard@mvista.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#ifndef __ACPI_ELEMENTS_H
#define __ACPI_ELEMENTS_H

#include <stdint.h>

typedef int (*contained_acpi_elem)(char **data, int length,
                                   void *opaque);

int acpi_add_Device(char **data, int dlen, const char *name,
                    contained_acpi_elem e, void *opaque);

int acpi_add_Name(char **data, int dlen, const char *name,
                  contained_acpi_elem e, void *opaque);

int acpi_add_Method(char **data, int dlen, const char *name, uint8_t flags,
                    contained_acpi_elem e, void *opaque);

int acpi_add_Scope(char **data, int dlen,
                   const char *name, contained_acpi_elem e, void *opaque);

/* Pass in a pointer to a u64 */
int acpi_add_Integer(char **data, int dlen, void *val);

/* Pass in a pointer to a string */
int acpi_add_EISAID(char **data, int dlen, void *val);

int acpi_add_BufferOp(char **data, int dlen,
                      contained_acpi_elem e, void *opaque);

/* Pass in a pointer to a u64 */
int acpi_add_Return(char **data, int dlen, void *);

/*
 * Note that str is void*, not char*, so it can be passed as a
 * contained element.
 */
int acpi_add_Unicode(char **data, int dlen, void *vstr);

int acpi_add_IO16(char **data, int dlen,
                  uint16_t minaddr, uint16_t maxaddr,
                  uint8_t align, uint8_t range);

#define ACPI_RESOURCE_PRODUCER 0
#define ACPI_RESOURCE_CONSUMER 1
#define ACPI_INTERRUPT_MODE_LEVEL 0
#define ACPI_INTERRUPT_MODE_EDGE  1
#define ACPI_INTERRUPT_POLARITY_ACTIVE_HIGH 0
#define ACPI_INTERRUPT_POLARITY_ACTIVE_LOW  1
#define ACPI_INTERRUPT_EXCLUSIVE      0
#define ACPI_INTERRUPT_SHARED         1
#define ACPI_INTERRUPT_EXCLUSIVE_WAKE 2
#define ACPI_INTERRUPT_SHARED_WAKE    3

int acpi_add_Interrupt(char **data, int dlen, int irq,
                       int consumer, int mode, int polarity, int sharing);

int acpi_add_EndResource(char **data, int dlen);

#endif
