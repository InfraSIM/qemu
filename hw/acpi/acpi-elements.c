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

#include "hw/acpi/acpi-elements.h"
#include <string.h>

static int
acpi_add_byte(char **data, uint8_t val)
{
    **data = val;
    (*data)++;
    return 1;
}

static int
acpi_add_Pkglen(char **data, int dlen, int length)
{
    int pkglen;

    /*
     * The funny length values following are because we include the length
     * bytes in the full length.
     */
    if (length <= 0x3e) {
        if (dlen > 0) {
            acpi_add_byte(data, length + 1);
        }
        return 1;
    } else if (length <= 0xffd) {
        pkglen = 2;
    } else if (length <= 0xffffc) {
        pkglen = 3;
    } else if (length <= 0xffffffb) {
        pkglen = 4;
    } else {
        return -1;
    }
    length += pkglen;
    if (pkglen <= dlen) {
        pkglen -= acpi_add_byte(data, ((pkglen - 1) << 6) | (length & 0xf));
        length >>= 4;
        while (pkglen > 0) {
            pkglen -= acpi_add_byte(data, length & 0xff);
            length >>= 8;
        }
    }
    return pkglen;
}

static int
acpi_add_NameSeg(char **data, int dlen, const char *name, int length)
{
    int i;

    if (dlen >= 4) {
        i = 0;
        while ((i < 4) && (i < length)) {
            i += acpi_add_byte(data, *name++);
        }
        while (i < 4) {
            i += acpi_add_byte(data, '_');
        }
    }
    return 4;
}

static int
acpi_add_NameString(char **data, int dlen, const char *name)
{
    int totlen;
    int segs = 1;
    const char *s, *n;

    /* Calculate the total number of segments. */
    s = name;
    while (*s) {
        if (*s++ == '.') {
            segs++;
         }
    }
    totlen = segs * 4;

    /* Now find a '\' or '^' prefix and add it. */
    s = name;
    if (*s == '\\') {
        totlen += 1;
    }  else {
        while (*s == '^') {
            s++;
            totlen++;
        }
    }

    if (dlen >= totlen) {
        s = name;
        if (*s == '\\') {
            dlen -= acpi_add_byte(data, *s++);
        } else {
            while (*s == '^') {
                dlen -= acpi_add_byte(data, *s++);
            }
        }

        if (segs == 1) {
            acpi_add_NameSeg(data, dlen, s, strlen(s));
        } else if (segs == 2) {
            dlen -= acpi_add_byte(data, 0x2e);
            n = strchr(s, '.');
            dlen -= acpi_add_NameSeg(data, dlen, s, n - s);
            s = n + 1;
            dlen -= acpi_add_NameSeg(data, dlen, s, strlen(s));
        } else {
            dlen -= acpi_add_byte(data, 0x2f);
            dlen -= acpi_add_byte(data, segs);
            while (segs > 1) {
                n = strchr(s, '.');
                dlen -= acpi_add_NameSeg(data, dlen, s, n - s);
                s = n + 1;
                segs--;
            }
            dlen -= acpi_add_NameSeg(data, dlen, s, strlen(s));
        }
    }

    return totlen;
}

int
acpi_add_Device(char **data, int dlen, const char *name,
                contained_acpi_elem e, void *opaque)
{
    int length, plen, totlen, nlen;

    length = e(NULL, 0, opaque);
    if (length < 0) {
        return length;
    }

    plen = acpi_add_Pkglen(NULL, 0, length);
    if (plen < 0) {
        return plen;
    }

    nlen = acpi_add_NameString(data, 0, name);
    if (nlen < 0) {
        return nlen;
    }

    totlen = length + plen + nlen + 2;
    if (dlen >= totlen) {
        dlen -= acpi_add_byte(data, 0x5b);
        dlen -= acpi_add_byte(data, 0x82);
        dlen -= acpi_add_Pkglen(data, dlen, length);
        dlen -= acpi_add_NameString(data, dlen, name);
        dlen -= e(data, dlen, opaque);
    }

    return totlen;
}

int
acpi_add_Name(char **data, int dlen, const char *name,
              contained_acpi_elem e, void *opaque)
{
    int rv, nlen;

    nlen = acpi_add_NameString(data, 0, name);
    if (nlen < 0) {
        return nlen;
    }

    if (dlen >= nlen + 1) {
        dlen -= acpi_add_byte(data, 0x8);
        dlen -= acpi_add_NameString(data, dlen, name);
    }
    rv = e(data, dlen, opaque);
    if (rv < 0) {
        return rv;
    }
    return rv + nlen + 1;
}

int
acpi_add_Method(char **data, int dlen, const char *name, uint8_t flags,
                contained_acpi_elem e, void *opaque)
{
    int elen, plen, nlen;

    elen = e(NULL, 0, opaque);
    if (elen < 0) {
        return elen;
    }

    plen = acpi_add_Pkglen(NULL, 0, elen + 5);
    if (plen < 0) {
        return plen;
    }

    nlen = acpi_add_NameString(data, 0, name);
    if (nlen < 0) {
        return nlen;
    }

    if (plen + elen + nlen + 2 <= dlen) {
        dlen -= acpi_add_byte(data, 0x14);
        dlen -= acpi_add_Pkglen(data, dlen, elen + 5);
        dlen -= acpi_add_NameString(data, dlen, name);
        dlen -= acpi_add_byte(data, flags);
        dlen -= e(data, dlen, opaque);
    }
    return plen + elen + nlen + 2;
}

int
acpi_add_Integer(char **data, int dlen, void *vval)
{
    uint64_t val = *((uint64_t *) vval);
    int length, i;
    unsigned char op;

    if ((val == 0) || (val == 1)) {
        /* ZeroOp or OneOp */
        if (dlen > 0) {
            acpi_add_byte(data, val);
        }
        return 1;
    }
    if (val <= 0xff) {
        length = 1;
        op = 0x0a;
    } else if (val <= 0xffff) {
        length = 2;
        op = 0x0b;
    } else if (val <= 0xffffffff) {
        length = 4;
        op = 0x0c;
    } else {
        length = 8;
        op = 0x0e;
    }

    if (dlen >= length + 1) {
        acpi_add_byte(data, op);
        for (i = 0; i < length; i++) {
            acpi_add_byte(data, val & 0xff);
            val >>= 8;
        }
    }
    return length + 1;
}

/*
 * A compressed EISA ID has the top bit reserved, the next 15 bits as
 * compressed ASCII upper case letters, and the bottom 16 bits as four
 * hex digits.
 */
int
acpi_add_EISAID(char **data, int dlen, void *val)
{
    char *str = val;
    uint32_t ival = 0;
    int i;

    if (dlen >= 5) {
        if (strlen(val) != 7) {
            return -1;
        }
        for (i = 0; i < 3; i++) {
            if (str[i] < 'A' || str[i] > 'Z') {
                return -1;
            }
            ival = (ival << 5) | (str[i] - 0x40);
        }
        for (; i < 7; i++) {
            int v;
            if (str[i] >= '0' && str[i] <= '9') {
                v = str[i] - '0';
            } else if (str[i] >= 'A' && str[i] <= 'F') {
                v = str[i] - 'A' + 10;
            } else {
                return -1;
            }
            ival = (ival << 4) | v;
        }
        acpi_add_byte(data, 0xc); /* 32-bit integer */
        /* Note that for some reason this is big endian */
        for (i = 0; i < 4; i++) {
            acpi_add_byte(data, (ival >> 24) & 0xff);
            ival <<= 8;
        }
    }

    return 5;
}

int
acpi_add_BufferOp(char **data, int dlen,
                  contained_acpi_elem e, void *opaque)
{
    int blen, slen, plen, tlen;
    uint64_t val;

    blen = e(NULL, 0, opaque);
    if (blen < 0) {
        return blen;
    }

    val = blen;
    slen = acpi_add_Integer(NULL, 0, &val);
    if (slen < 0) {
        return slen;
    }
    plen = acpi_add_Pkglen(NULL, 0, slen + blen);
    if (plen < 0) {
        return plen;
    }
    tlen = blen + slen + plen + 1;
    if (tlen <= dlen) {
        dlen -= acpi_add_byte(data, 0x11);
        dlen -= acpi_add_Pkglen(data, dlen, slen + blen);
        dlen -= acpi_add_Integer(data, dlen, &val);
        dlen -= e(data, dlen, opaque);
    }
    return tlen;
}

int
acpi_add_Return(char **data, int dlen, void *val)
{
    int blen;

    blen = acpi_add_Integer(NULL, 0, val);
    if (blen + 1 <= dlen) {
        dlen -= acpi_add_byte(data, 0xa4);
        dlen -= acpi_add_Integer(data, dlen, val);
    }
    return blen + 1;
}

/*
 * Note that str is void*, not char*, so it can be passed as a
 * contained element.
 */
static int
unicode_helper(char **data, int dlen, void *vstr)
{
    char *str = vstr;
    int len = strlen(str) + 1;

    if (len * 2 <= dlen) {
        while (*str) {
            acpi_add_byte(data, *str++);
            acpi_add_byte(data, 0);
        }
        acpi_add_byte(data, 0);
        acpi_add_byte(data, 0);
    }
    return len * 2;
}
int
acpi_add_Unicode(char **data, int dlen, void *vstr)
{
    int len;

    len = acpi_add_BufferOp(NULL, 0, unicode_helper, vstr);
    if (len < 0) {
        return len;
    }
    if (len <= dlen) {
        acpi_add_BufferOp(data, dlen, unicode_helper, vstr);
    }
    return len;
}

int
acpi_add_IO16(char **data, int dlen,
              uint16_t minaddr, uint16_t maxaddr,
              uint8_t align, uint8_t range)
{
    if (dlen >= 8) {
        acpi_add_byte(data, 0x47);
        acpi_add_byte(data, 1);
        acpi_add_byte(data, minaddr & 0xff);
        acpi_add_byte(data, minaddr >> 8);
        acpi_add_byte(data, maxaddr & 0xff);
        acpi_add_byte(data, maxaddr >> 8);
        acpi_add_byte(data, align);
        acpi_add_byte(data, range);
    }
    return 8;
}

int
acpi_add_Interrupt(char **data, int dlen, int irq,
                   int consumer, int mode, int polarity, int sharing)
{
    if (dlen >= 9) {
        acpi_add_byte(data, 0x89);
        acpi_add_byte(data, 6);
        acpi_add_byte(data, 0);
        acpi_add_byte(data, (consumer | (mode << 1) | (polarity << 2)
                             | (sharing << 3)));
        acpi_add_byte(data, 1); /* Only 1 irq */
        acpi_add_byte(data, irq);
        acpi_add_byte(data, 0);
        acpi_add_byte(data, 0);
        acpi_add_byte(data, 0);
    }
    return 9;
}

int
acpi_add_Scope(char **data, int dlen,
               const char *name, contained_acpi_elem e, void *opaque)
{
    int length, plen, totlen, nlen;

    length = e(NULL, 0, opaque);
    if (length < 0) {
        return length;
    }

    plen = acpi_add_Pkglen(NULL, 0, length);
    if (plen < 0) {
        return plen;
    }

    nlen = acpi_add_NameString(data, 0, name);
    if (nlen < 0) {
        return nlen;
    }

    totlen = length + plen + nlen + 1;
    if (dlen >= totlen) {
        dlen -= acpi_add_byte(data, 0x10);
        dlen -= acpi_add_Pkglen(data, dlen, length);
        dlen -= acpi_add_NameString(data, dlen, name);
        dlen -= e(data, dlen, opaque);
    }

    return totlen;
}

int
acpi_add_EndResource(char **data, int dlen)
{
    if (dlen >= 2) {
        acpi_add_byte(data, 0x79);
        acpi_add_byte(data, 0);
    }
    return 2;
}
