/*
 * QEMU ISA IPMI emulation
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
#include "hw/hw.h"
#include "hw/isa/isa.h"
#include "hw/acpi/acpi-elements.h"
#include "hw/acpi/acpi.h"
#include "hw/i386/pc.h"
#include "qemu/timer.h"
#include "sysemu/char.h"
#include "sysemu/sysemu.h"
#include "hw/i386/smbios.h"
#include "ipmi.h"

/* This is the type the user specifies on the -device command line */
#define TYPE_ISA_IPMI           "isa-ipmi"
#define ISA_IPMI(obj) OBJECT_CHECK(ISAIPMIDevice, (obj), TYPE_ISA_IPMI)

typedef struct ISAIPMIDevice {
    ISADevice dev;
    char *interface;
    int intftype;
    uint32_t iobase;
    uint32_t iolength;
    uint8_t regspacing;
    int32 isairq;
    uint8_t slave_addr;
    uint8_t version;
    CharDriverState *chr;
    IPMIInterface *intf;
} ISAIPMIDevice;

/* SMBIOS type 38 - IPMI */
struct smbios_type_38 {
    struct smbios_structure_header header;
    uint8_t interface_type;
    uint8_t ipmi_spec_revision;
    uint8_t i2c_slave_address;
    uint8_t nv_storage_device_address;
    uint64_t base_address;
    uint8_t base_address_modifier;
    uint8_t interrupt_number;
} QEMU_PACKED;

static int
acpi_ipmi_crs_ops(char **data, int dlen, void *opaque)
{
    ISAIPMIDevice *info = opaque;
    int len, rv;
    uint8_t regspacing = info->regspacing;

    if (regspacing == 1) {
        regspacing = 0;
    }

    /* IO(Decode16, x, y, z, c) */
    len = acpi_add_IO16(data, dlen, info->iobase,
                        info->iobase + info->iolength - 1,
                        regspacing, info->iolength);
    if (len < 0) {
        return len;
    }

    if (info->isairq) {
        /* Interrupt(ResourceConsumer,Level,ActiveHigh,Exclusive) {n} */
        rv = acpi_add_Interrupt(data, dlen, info->isairq,
                                ACPI_RESOURCE_CONSUMER,
                                ACPI_INTERRUPT_MODE_LEVEL,
                                ACPI_INTERRUPT_POLARITY_ACTIVE_HIGH,
                                ACPI_INTERRUPT_EXCLUSIVE);
        if (rv < 0) {
            return rv;
        }
        len += rv;
    }
    rv = acpi_add_EndResource(data, dlen);
    if (rv < 0) {
        return rv;
    }
    len += rv;
    return len;
}

static int
acpi_ipmi_crs(char **data, int dlen, void *opaque)
{
    ISAIPMIDevice *info = opaque;
    int len;

    len = acpi_add_BufferOp(NULL, 0, acpi_ipmi_crs_ops, info);
    if (len < 0) {
        return len;
    }
    if (len <= dlen) {
        acpi_add_BufferOp(data, dlen, acpi_ipmi_crs_ops, info);
    }
    return len;
}

static int
acpi_ipmi_dev(char **data, int dlen, void *opaque)
{
    ISAIPMIDevice *info = opaque;
    int len, rv;
    char *name;
    uint64_t val;

    name = g_strdup_printf("ipmi_%s", info->interface);

    /* Name(_HID, EISAID("IPI0001")) */
    len = acpi_add_Name(data, dlen, "_HID", acpi_add_EISAID,
                        (void *) "IPI0001");
    if (len < 0) {
        return len;
    }
    /* Name(_STR, Unicode("ipmi_xxx")) */
    rv = acpi_add_Name(data, dlen, "_STR", acpi_add_Unicode, name);
    if (rv < 0) {
        return rv;
    }
    len += rv;
    val = 0;
    /* Name(_UID, 0) */
    rv = acpi_add_Name(data, dlen, "_UID", acpi_add_Integer, &val);
    if (rv < 0) {
        return rv;
    }
    len += rv;
    /* Name(_CRS, ResourceTemplate() { */
    rv = acpi_add_Name(data, dlen, "_CRS", acpi_ipmi_crs, info);
    if (rv < 0) {
        return rv;
    }
    len += rv;
    val = info->intftype;
    /* Method(_IFT) { Return(i) } */
    rv = acpi_add_Method(data, dlen, "_IFT", 0, acpi_add_Return, &val);
    if (rv < 0) {
        return rv;
    }
    len += rv;
    val = ((info->version & 0xf0) << 4) | (info->version & 0x0f);
    /* Method(_SRV) { Return(version) } */
    rv = acpi_add_Method(data, dlen, "_SRV", 0, acpi_add_Return, &val);
    if (rv < 0) {
        return rv;
    }
    len += rv;
    return len;
}

static int
acpi_ipmi_scope(char **data, int dlen, void *opaque)
{
    ISAIPMIDevice *info = opaque;

    /* Device(MI0) { */
    return acpi_add_Device(data, dlen, "MI0", acpi_ipmi_dev, info);
    /* } */
}

static void
ipmi_encode_acpi(ISAIPMIDevice *info)
{
    char ipmitable[200];
    char *tblptr = ipmitable;
    int rc;
    Error *err = NULL;

    /* Scope(\_SB.PCI0.ISA) { */
    rc = acpi_add_Scope(&tblptr, sizeof(ipmitable), "\\_SB.PCI0.ISA",
                         acpi_ipmi_scope, info);
    /* } */
    if (rc < 0) {
        fprintf(stderr, "Unable to format IPMI ACPI table entry\n");
        return;
    }

    acpi_append_to_table("SSDT", ipmitable, rc, &err);
}

static void ipmi_encode_smbios(void *opaque)
{
    ISAIPMIDevice *info = opaque;
    struct smbios_type_38 smb38;

    smb38.header.type = 38;
    smb38.header.length = sizeof(smb38);
    smb38.header.handle = cpu_to_le16(0x3000);
    smb38.interface_type = info->intftype;
    smb38.ipmi_spec_revision = info->version;
    smb38.i2c_slave_address = info->slave_addr;
    smb38.nv_storage_device_address = 0;

    /* or 1 to set it to I/O space */
    smb38.base_address = cpu_to_le64(info->iobase | 1);

     /* 1-byte boundaries, addr bit0=0, level triggered irq */
    smb38.base_address_modifier = 1;
    smb38.interrupt_number = info->isairq;
    smbios_table_entry_add((struct smbios_structure_header *) &smb38,
                           sizeof(smb38), true);

    ipmi_encode_acpi(info);
}

static void ipmi_isa_realizefn(DeviceState *dev, Error **errp)
{
    ISADevice *isadev = ISA_DEVICE(dev);
    ISAIPMIDevice *ipmi = ISA_IPMI(dev);
    char typename[20];
    Object *intfobj;
    IPMIInterface *intf;
    IPMIInterfaceClass *intfk;
    Object *bmcobj;
    IPMIBmc *bmc;

    if (!ipmi->interface) {
        ipmi->interface = g_strdup("kcs");
    }

    if (ipmi->chr) {
        bmcobj = object_new(TYPE_IPMI_BMC_EXTERN);
    } else {
        bmcobj = object_new(TYPE_IPMI_BMC_SIMULATOR);
    }
    bmc = IPMI_BMC(bmcobj);
    bmc->chr = ipmi->chr;
    snprintf(typename, sizeof(typename),
             TYPE_IPMI_INTERFACE_PREFIX "%s", ipmi->interface);
    intfobj = object_new(typename);
    intf = IPMI_INTERFACE(intfobj);
    intfk = IPMI_INTERFACE_GET_CLASS(intf);
    bmc->intf = intf;
    intf->bmc = bmc;
    ipmi->regspacing = 1;
    intf->io_base = ipmi->iobase;
    intf->slave_addr = ipmi->slave_addr;
    ipmi->intftype = intfk->smbios_type;
    ipmi->version = 0x20; /* Version 2.0 */
    ipmi_interface_init(intf, errp);
    if (*errp) {
        return;
    }
    ipmi->iolength = intf->io_length;
    ipmi_bmc_init(bmc, errp);
    if (*errp) {
        return;
    }

    /* These may be set by the interface. */
    ipmi->iobase = intf->io_base;
    ipmi->slave_addr = intf->slave_addr;

    if (ipmi->isairq > 0) {
        isa_init_irq(isadev, &intf->irq, ipmi->isairq);
        intf->use_irq = 1;
    }

    ipmi->intf = intf;
    object_property_add_child(OBJECT(isadev), "intf", OBJECT(intf), errp);
    if (*errp) {
        return;
    }
    object_property_add_child(OBJECT(isadev), "bmc", OBJECT(bmc), errp);
    if (*errp) {
        return;
    }

    qdev_set_legacy_instance_id(dev, intf->io_base, intf->io_length);

    isa_register_ioport(isadev, &intf->io, intf->io_base);
    smbios_register_device_table_handler(ipmi_encode_smbios, ipmi);
}

static void ipmi_isa_reset(DeviceState *qdev)
{
    ISAIPMIDevice *isa = ISA_IPMI(qdev);

    ipmi_interface_reset(isa->intf);
}

static Property ipmi_isa_properties[] = {
    DEFINE_PROP_STRING("interface", ISAIPMIDevice, interface),
    DEFINE_PROP_UINT32("iobase", ISAIPMIDevice, iobase,  0),
    DEFINE_PROP_INT32("irq",   ISAIPMIDevice, isairq,  5),
    DEFINE_PROP_UINT8("slave_addr", ISAIPMIDevice, slave_addr,  0),
    DEFINE_PROP_CHR("chardev",  ISAIPMIDevice, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_isa_ipmi = {
    .name = TYPE_ISA_IPMI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_STRUCT_POINTER(intf, ISAIPMIDevice, vmstate_IPMIInterface,
                               IPMIInterface),
        VMSTATE_END_OF_LIST()
    }
};

static void ipmi_isa_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = ipmi_isa_realizefn;
    dc->reset = ipmi_isa_reset;
    dc->vmsd = &vmstate_isa_ipmi;
    dc->props = ipmi_isa_properties;
}

static const TypeInfo ipmi_isa_info = {
    .name          = TYPE_ISA_IPMI,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(ISAIPMIDevice),
    .class_init    = ipmi_isa_class_initfn,
};

static void ipmi_register_types(void)
{
    type_register_static(&ipmi_isa_info);
}

type_init(ipmi_register_types)
