/*
 * PC SMBus implementation
 * splitted from acpi.c
 *
 * Copyright (c) 2006 Fabrice Bellard
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
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/>.
 */
#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "hw/i2c/pm_smbus.h"
#include "hw/i2c/smbus.h"

#define SMBHSTSTS       0x00
#define SMBHSTCNT       0x02
#define SMBHSTCMD       0x03
#define SMBHSTADD       0x04
#define SMBHSTDAT0      0x05
#define SMBHSTDAT1      0x06
#define SMBBLKDAT       0x07
#define SMBAUXCTL       0x0d

#define STS_HOST_BUSY   (1<<0)
#define STS_INTR        (1<<1)
#define STS_DEV_ERR     (1<<2)
#define STS_BUS_ERR     (1<<3)
#define STS_FAILED      (1<<4)
#define STS_SMBALERT    (1<<5)
#define STS_INUSE_STS   (1<<6)
#define STS_BYTE_DONE   (1<<7)
/* Signs of successfully transaction end :
*  ByteDoneStatus = 1 (STS_BYTE_DONE) and INTR = 1 (STS_INTR )
*/

#define CTL_INTREN      (1<<0)
#define CTL_KILL        (1<<1)
#define CTL_LAST_BYTE   (1<<5)
#define CTL_START       (1<<6)
#define CTL_PEC_EN      (1<<7)

#define PROT_QUICK          0
#define PROT_BYTE           1
#define PROT_BYTE_DATA      2
#define PROT_WORD_DATA      3
#define PROT_PROC_CALL      4
#define PROT_BLOCK_DATA     5
#define PROT_I2C_BLOCK_DATA 6

#define AUX_PEC       (1<<0)
#define AUX_BLK       (1<<1)
#define AUX_MASK      0x3

/*#define DEBUG*/

#ifdef DEBUG
# define SMBUS_DPRINTF(format, ...)     fprintf(stderr, format, ## __VA_ARGS__)
#else
# define SMBUS_DPRINTF(format, ...)     do { } while (0)
#endif


static void smb_transaction(PMSMBus *s)
{
    uint8_t prot = (s->smb_ctl >> 2) & 0x07;
    uint8_t read = s->smb_addr & 0x01;
    uint8_t cmd = s->smb_cmd;
    uint8_t addr = s->smb_addr >> 1;
    I2CBus *bus = s->smbus;
    bool i2c_enable = s->i2c_enable;
    int ret;

    SMBUS_DPRINTF("SMBus trans addr=0x%02x prot=0x%02x\n", addr, prot);
    /* Transaction isn't exec if STS_DEV_ERR bit set */
    if ((s->smb_stat & STS_DEV_ERR) != 0)  {
        goto error;
    }

    switch(prot) {
    case PROT_QUICK:
        ret = smbus_quick_command(bus, addr, read);
        goto done;
    case PROT_BYTE:
        if (read) {
            ret = smbus_receive_byte(bus, addr);
            goto data8;
        } else {
            ret = smbus_send_byte(bus, addr, cmd);
            goto done;
        }
    case PROT_BYTE_DATA:
        if (read) {
            ret = smbus_read_byte(bus, addr, cmd);
            goto data8;
        } else {
            ret = smbus_write_byte(bus, addr, cmd, s->smb_data0);
            goto done;
        }
        break;
    case PROT_WORD_DATA:
        if (read) {
            ret = smbus_read_word(bus, addr, cmd);
            goto data16;
        } else {
            ret = smbus_write_word(bus, addr, cmd,
                                   (s->smb_data1 << 8) | s->smb_data0);
            goto done;
        }
        break;
    case PROT_I2C_BLOCK_DATA:
        cmd = s->smb_data1;
        if (s->smb_ctl & CTL_LAST_BYTE) {
            s->smb_data0 = 1;
        } else {
            s->smb_data0 = PM_SMBUS_MAX_MSG_SIZE;
        }
        read = true;
        i2c_enable = true;
        /* Fallthrough */
    case PROT_BLOCK_DATA:
        if (read) {
            ret = smbus_read_block(bus, addr, cmd, s->smb_data,
                                   sizeof(s->smb_data), !i2c_enable);
            s->smb_index = 0;
            s->op_done = false;
            if (s->smb_auxctl & AUX_BLK) {
                s->smb_stat |= STS_INTR;
            } else {
                s->smb_stat |= STS_HOST_BUSY;
            }
            goto datablk;
        } else {
            if (s->smb_auxctl & AUX_BLK || s->smb_index == s->smb_data0) {
                if (s->smb_index != s->smb_data0) {
                    s->smb_index = 0;
                    goto error;
                }
                /* Data is already all written to the queue, just do
                   the operation. */
                ret = smbus_write_block(bus, addr, cmd, s->smb_data,
                                        s->smb_data0, !i2c_enable);
                s->op_done = true;
                s->smb_index = 0;
                s->smb_stat |= STS_INTR;
            } else {
                s->op_done = false;
                s->smb_stat |= STS_HOST_BUSY;
                ret = 0;
            }
            goto doneblk;
        }
        break;
    default:
        goto error;
    }
    abort();

data16:
    if (ret < 0) {
        goto error;
    }
    s->smb_data1 = ret >> 8;
data8:
    if (ret < 0) {
        goto error;
    }
    s->smb_data0 = ret;
done:
    if (ret < 0) {
        goto error;
    }
    s->smb_stat |= STS_BYTE_DONE | STS_INTR;
    return;
datablk:
    if (ret < 0) {
        goto error;
    }
    s->smb_data0 = ret;
doneblk:
    if (ret < 0) {
        goto error;
    }
    s->smb_stat |= STS_BYTE_DONE;
    return;

error:
    s->smb_stat |= STS_DEV_ERR;
    return;
}

static bool
smb_irq_value(PMSMBus *s)
{
    return ((s->smb_stat & ~STS_HOST_BUSY) != 0) && (s->smb_ctl & CTL_INTREN);
}

static void smb_ioport_writeb(void *opaque, hwaddr addr, uint64_t val,
                              unsigned width)
{
    PMSMBus *s = opaque;

    SMBUS_DPRINTF("SMB writeb port=0x%04" HWADDR_PRIx " val=0x%02" PRIx64 "\n",
                  addr, val);
    switch(addr) {
    case SMBHSTSTS:
        s->smb_stat &= ~(val & ~STS_HOST_BUSY);
        if (!s->op_done && !(s->smb_auxctl & AUX_BLK)) {
            s->smb_stat |= STS_BYTE_DONE;
        }
        break;
    case SMBHSTCNT:
        s->smb_ctl = val;
        if (s->smb_ctl & CTL_START) {
            if (!s->op_done) {
                s->smb_index = 0;
                s->op_done = true;
            }
            smb_transaction(s);
        }
        if (s->smb_ctl & CTL_KILL) {
            s->op_done = true;
            s->smb_index = 0;
            s->smb_stat |= STS_FAILED;
            s->smb_stat &= ~STS_HOST_BUSY;
        }
        break;
    case SMBHSTCMD:
        s->smb_cmd = val;
        break;
    case SMBHSTADD:
        s->smb_addr = val;
        break;
    case SMBHSTDAT0:
        s->smb_data0 = val;
        break;
    case SMBHSTDAT1:
        s->smb_data1 = val;
        break;
    case SMBBLKDAT:
        if (s->smb_index >= PM_SMBUS_MAX_MSG_SIZE) {
            s->smb_index = 0;
        }
        s->smb_data[s->smb_index++] = val;
        if (!(s->smb_auxctl & AUX_BLK) && s->smb_ctl & CTL_START &&
            !s->op_done && s->smb_index == s->smb_data0) {
            smb_transaction(s);
            s->op_done = true;
            s->smb_stat |= STS_INTR;
        }
        break;
    case SMBAUXCTL:
        s->smb_auxctl = val & AUX_MASK;
        break;
    default:
        break;
    }

    if (s->set_irq) {
        s->set_irq(s, smb_irq_value(s));
    }
}

static uint64_t smb_ioport_readb(void *opaque, hwaddr addr, unsigned width)
{
    PMSMBus *s = opaque;
    uint32_t val;

    switch(addr) {
    case SMBHSTSTS:
        val = s->smb_stat;
        break;
    case SMBHSTCNT:
        val = s->smb_ctl & 0x1f;
        break;
    case SMBHSTCMD:
        val = s->smb_cmd;
        break;
    case SMBHSTADD:
        val = s->smb_addr;
        break;
    case SMBHSTDAT0:
        val = s->smb_data0;
        break;
    case SMBHSTDAT1:
        val = s->smb_data1;
        break;
    case SMBBLKDAT:
        if (s->smb_index >= PM_SMBUS_MAX_MSG_SIZE) {
            s->smb_index = 0;
        }
        val = s->smb_data[s->smb_index++];
        if (s->smb_ctl & CTL_START && !s->op_done &&
            s->smb_index == s->smb_data0) {
            s->op_done = true;
            s->smb_index = 0;
            s->smb_stat &= ~STS_HOST_BUSY;
        }
        if (s->smb_ctl & CTL_LAST_BYTE) {
            s->op_done = true;
            s->smb_index = 0;
            s->smb_stat |= STS_INTR;
            s->smb_stat &= ~STS_HOST_BUSY;
        }
        break;
    case SMBAUXCTL:
        val = s->smb_auxctl;
        break;
    default:
        val = 0;
        break;
    }
    SMBUS_DPRINTF("SMB readb port=0x%04" HWADDR_PRIx " val=0x%02x\n",
                  addr, val);

    if (s->set_irq) {
        s->set_irq(s, smb_irq_value(s));
    }

    return val;
}

static void pm_smbus_reset(PMSMBus *s)
{
    s->op_done = true;
    s->smb_index = 0;
    s->smb_stat = 0;
}

static const MemoryRegionOps pm_smbus_ops = {
    .read = smb_ioport_readb,
    .write = smb_ioport_writeb,
    .valid.min_access_size = 1,
    .valid.max_access_size = 1,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

const VMStateDescription pmsmb_vmstate = {
    .name = "pmsmb",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(smb_stat, PMSMBus),
        VMSTATE_UINT8(smb_ctl, PMSMBus),
        VMSTATE_UINT8(smb_cmd, PMSMBus),
        VMSTATE_UINT8(smb_addr, PMSMBus),
        VMSTATE_UINT8(smb_data0, PMSMBus),
        VMSTATE_UINT8(smb_data1, PMSMBus),
        VMSTATE_VBUFFER_UINT32(smb_data, PMSMBus, 1, NULL, 0, smb_index),
        VMSTATE_UINT8(smb_auxctl, PMSMBus),
        VMSTATE_BOOL(i2c_enable, PMSMBus),
        VMSTATE_BOOL(op_done, PMSMBus),
        VMSTATE_END_OF_LIST()
    }
};

void pm_smbus_init(DeviceState *parent, PMSMBus *smb)
{
    smb->op_done = true;
    smb->reset = pm_smbus_reset;
    smb->smbus = i2c_init_bus(parent, "i2c");
    memory_region_init_io(&smb->io, OBJECT(parent), &pm_smbus_ops, smb,
                          "pm-smbus", 64);
}
