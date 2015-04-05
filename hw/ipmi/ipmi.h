/*
 * IPMI base class
 *
 * Copyright (c) 2012 Corey Minyard, MontaVista Software, LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef HW_IPMI_H
#define HW_IPMI_H

#include "exec/memory.h"
#include "qemu-common.h"
#include "hw/qdev.h"
#include "qemu/thread.h"

#define MAX_IPMI_MSG_SIZE 300

enum ipmi_op {
    IPMI_RESET_CHASSIS,
    IPMI_POWEROFF_CHASSIS,
    IPMI_POWERON_CHASSIS,
    IPMI_POWERCYCLE_CHASSIS,
    IPMI_PULSE_DIAG_IRQ,
    IPMI_SHUTDOWN_VIA_ACPI_OVERTEMP,
    IPMI_SEND_NMI
};

#define IPMI_CC_INVALID_CMD                              0xc1
#define IPMI_CC_COMMAND_INVALID_FOR_LUN                  0xc2
#define IPMI_CC_TIMEOUT                                  0xc3
#define IPMI_CC_OUT_OF_SPACE                             0xc4
#define IPMI_CC_INVALID_RESERVATION                      0xc5
#define IPMI_CC_REQUEST_DATA_TRUNCATED                   0xc6
#define IPMI_CC_REQUEST_DATA_LENGTH_INVALID              0xc7
#define IPMI_CC_PARM_OUT_OF_RANGE                        0xc9
#define IPMI_CC_CANNOT_RETURN_REQ_NUM_BYTES              0xca
#define IPMI_CC_REQ_ENTRY_NOT_PRESENT                    0xcb
#define IPMI_CC_INVALID_DATA_FIELD                       0xcc
#define IPMI_CC_BMC_INIT_IN_PROGRESS                     0xd2
#define IPMI_CC_COMMAND_NOT_SUPPORTED                    0xd5

#define IPMI_NETFN_APP                0x06

#define IPMI_DEBUG 1

/* Specified in the SMBIOS spec. */
#define IPMI_SMBIOS_KCS         0x01
#define IPMI_SMBIOS_SMIC        0x02
#define IPMI_SMBIOS_BT          0x03
#define IPMI_SMBIOS_SSIF        0x04

/* IPMI Interface types (KCS, SMIC, BT) are prefixed with this */
#define TYPE_IPMI_INTERFACE_PREFIX "ipmi-interface-"

typedef struct IPMIBmc IPMIBmc;

/*
 * An IPMI Interface, the interface for talking between the target
 * and the BMC.
 */
#define TYPE_IPMI_INTERFACE "ipmi-interface"
#define IPMI_INTERFACE(obj) \
     OBJECT_CHECK(IPMIInterface, (obj), TYPE_IPMI_INTERFACE)
#define IPMI_INTERFACE_CLASS(klass) \
     OBJECT_CLASS_CHECK(IPMIInterfaceClass, (klass), TYPE_IPMI_INTERFACE)
#define IPMI_INTERFACE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(IPMIInterfaceClass, (obj), TYPE_IPMI_INTERFACE)

typedef struct IPMIInterface {
    Object parent_obj;

    IPMIBmc *bmc;

    bool threaded_bmc;

    /* For threaded BMC */
    QemuThread thread;
    QemuCond waker;
    QemuMutex lock;

    /* For non-threaded BMC */
    int lockcount;

    bool do_wake;

    qemu_irq irq;

    unsigned long io_base;
    unsigned long io_length;
    MemoryRegion io;

    unsigned char slave_addr;

    bool obf_irq_set;
    bool atn_irq_set;
    bool use_irq;
    bool irqs_enabled;

    uint8_t outmsg[MAX_IPMI_MSG_SIZE];
    uint32_t outpos;
    uint32_t outlen;

    uint8_t inmsg[MAX_IPMI_MSG_SIZE];
    uint32_t inlen;
    bool write_end;
} IPMIInterface;

typedef struct IPMIInterfaceClass {
    ObjectClass parent_class;

    unsigned int smbios_type;

    void (*init)(struct IPMIInterface *s, Error **errp);

    /*
     * Perform various operations on the hardware.  If checkonly is
     * true, it will return if the operation can be performed, but it
     * will not do the operation.
     */
    int (*do_hw_op)(struct IPMIInterface *s, enum ipmi_op op, int checkonly);

    /*
     * Enable/disable irqs on the interface when the BMC requests this.
     */
    void (*set_irq_enable)(struct IPMIInterface *s, int val);

    /*
     * Handle an event that occurred on the interface, generally the.
     * target writing to a register.
     * Must be called with ipmi_lock held.
     */
    void (*handle_if_event)(struct IPMIInterface *s);

    /*
     * The interfaces use this to perform certain ops
     */
    void (*set_atn)(struct IPMIInterface *s, int val, int irq);

    /*
     * Got an IPMI warm/cold reset.
     */
    void (*reset)(struct IPMIInterface *s, bool is_cold);

    /*
     * Handle a response from the bmc.
     * Must be called with ipmi_lock held.
     */
    void (*handle_rsp)(struct IPMIInterface *s, uint8_t msg_id,
                       unsigned char *rsp, unsigned int rsp_len);
} IPMIInterfaceClass;

extern const VMStateDescription vmstate_IPMIInterface;

void ipmi_interface_init(IPMIInterface *s, Error **errp);
void ipmi_interface_reset(IPMIInterface *s);

/*
 * Define a BMC simulator (or perhaps a connection to a real BMC)
 */
#define TYPE_IPMI_BMC "ipmi-bmc"
#define IPMI_BMC(obj) \
     OBJECT_CHECK(IPMIBmc, (obj), TYPE_IPMI_BMC)
#define IPMI_BMC_CLASS(klass) \
     OBJECT_CLASS_CHECK(IPMIBmcClass, (klass), TYPE_IPMI_BMC)
#define IPMI_BMC_GET_CLASS(obj) \
     OBJECT_GET_CLASS(IPMIBmcClass, (obj), TYPE_IPMI_BMC)

#define TYPE_IPMI_BMC_EXTERN    "ipmi-bmc-extern"
#define TYPE_IPMI_BMC_SIMULATOR "ipmi-bmc-sim"

static inline void ipmi_lock(IPMIInterface *s)
{
    if (s->threaded_bmc) {
        qemu_mutex_lock(&s->lock);
    } else {
        s->lockcount++;
    }
}

static inline void ipmi_unlock(IPMIInterface *s)
{
    if (s->threaded_bmc) {
        qemu_mutex_unlock(&s->lock);
    } else {
        s->lockcount--;
    }
}

static inline void ipmi_signal(IPMIInterface *s)
{
    if (s->threaded_bmc) {
        s->do_wake = 1;
        qemu_cond_signal(&s->waker);
    } else {
        s->do_wake = 1;
        s->lockcount++;
        while (s->do_wake) {
            s->do_wake = 0;
            (IPMI_INTERFACE_GET_CLASS(s))->handle_if_event(s);
        }
        s->lockcount--;
    }
}

struct IPMIBmc {
    Object parent_obj;

    IPMIInterface *intf;
    CharDriverState *chr;
};

typedef struct IPMIBmcClass {
    ObjectClass parent_class;

    void (*init)(IPMIBmc *s, Error **errp);

    /* Called when the system resets to report to the bmc. */
    void (*handle_reset)(struct IPMIBmc *s);

    /*
     * Handle a command to the bmc.
     * Must be called with ipmi_lock held.
     */
    void (*handle_command)(struct IPMIBmc *s,
                           uint8_t *cmd, unsigned int cmd_len,
                           unsigned int max_cmd_len,
                           uint8_t msg_id);
} IPMIBmcClass;

void ipmi_bmc_init(IPMIBmc *s, Error **errp);

#ifdef IPMI_DEBUG
#define ipmi_debug(fs, ...) \
    fprintf(stderr, "IPMI (%s): " fs, __func__, ##__VA_ARGS__)
#else
#define ipmi_debug(fs, ...)
#endif

#endif
