/*
 *  VBI capture driver for the Booktree 848/878/879 family
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License Version 2 as
 *  published by the Free Software Foundation. You find a copy of this
 *  license in the file COPYRIGHT in the root directory of this release.
 *
 *  THIS PROGRAM IS DISTRIBUTED IN THE HOPE THAT IT WILL BE USEFUL,
 *  BUT WITHOUT ANY WARRANTY; WITHOUT EVEN THE IMPLIED WARRANTY OF
 *  MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *
 *  Description: see according C source file.
 *
 *  Author: see according C source file.
 *
 *  $Id: bt848.h,v 1.1 2000/12/02 18:12:30 tom Exp tom $
 */

#ifndef __BT848_H
#define __BT848_H

// ---------------------------------------------------------------------------
// Brooktree 848 registers

#define BT848_DSTATUS          0x000
#define BT848_DSTATUS_PRES     (1<<7)
#define BT848_DSTATUS_HLOC     (1<<6)
#define BT848_DSTATUS_FIELD    (1<<5)
#define BT848_DSTATUS_NUML     (1<<4)
#define BT848_DSTATUS_CSEL     (1<<3)
#define BT848_DSTATUS_LOF      (1<<1)
#define BT848_DSTATUS_COF      (1<<0)

#define BT848_IFORM            0x004
#define BT848_IFORM_HACTIVE    (1<<7)
#define BT848_IFORM_MUXSEL     (3<<5)
#define BT848_IFORM_MUX0       (2<<5)
#define BT848_IFORM_MUX1       (3<<5)
#define BT848_IFORM_MUX2       (1<<5)
#define BT848_IFORM_XTSEL      (3<<3)
#define BT848_IFORM_XT0        (1<<3)
#define BT848_IFORM_XT1        (2<<3)
#define BT848_IFORM_XTAUTO     (3<<3)
#define BT848_IFORM_XTBOTH     (3<<3)
#define BT848_IFORM_NTSC       1
#define BT848_IFORM_PAL_BDGHI  3
#define BT848_IFORM_PAL_M      4
#define BT848_IFORM_PAL_N      5
#define BT848_IFORM_SECAM      6
#define BT848_IFORM_AUTO       0
#define BT848_IFORM_NORM       7


#define BT848_FCNTR            0x0E8
#define BT848_PLL_F_LO         0x0F0
#define BT848_PLL_F_HI         0x0F4
#define BT848_PLL_XCI          0x0F8

#define BT848_TGCTRL           0x084


#define BT848_TDEC             0x008
#define BT848_TDEC_DEC_FIELD   (1<<7)
#define BT848_TDEC_FLDALIGN    (1<<6)
#define BT848_TDEC_DEC_RAT     (0x1f)

#define BT848_E_CROP           0x00C
#define BT848_O_CROP           0x08C

#define BT848_E_VDELAY_LO      0x010
#define BT848_O_VDELAY_LO      0x090

#define BT848_E_VACTIVE_LO     0x014
#define BT848_O_VACTIVE_LO     0x094

#define BT848_E_HDELAY_LO      0x018
#define BT848_O_HDELAY_LO      0x098

#define BT848_E_HACTIVE_LO     0x01C
#define BT848_O_HACTIVE_LO     0x09C

#define BT848_E_HSCALE_HI      0x020
#define BT848_O_HSCALE_HI      0x0A0

#define BT848_E_HSCALE_LO      0x024
#define BT848_O_HSCALE_LO      0x0A4

#define BT848_BRIGHT           0x028

#define BT848_E_CONTROL        0x02C
#define BT848_O_CONTROL        0x0AC
#define BT848_CONTROL_LNOTCH    (1<<7)
#define BT848_CONTROL_COMP      (1<<6)
#define BT848_CONTROL_LDEC      (1<<5)
#define BT848_CONTROL_CBSENSE   (1<<4)
#define BT848_CONTROL_CON_MSB   (1<<2)
#define BT848_CONTROL_SAT_U_MSB (1<<1)
#define BT848_CONTROL_SAT_V_MSB (1<<0)

#define BT848_CONTRAST_LO      0x030
#define BT848_SAT_U_LO         0x034
#define BT848_SAT_V_LO         0x038
#define BT848_HUE              0x03C

#define BT848_E_SCLOOP         0x040
#define BT848_O_SCLOOP         0x0C0
#define BT848_SCLOOP_CAGC       (1<<6)
#define BT848_SCLOOP_CKILL      (1<<5)
#define BT848_SCLOOP_HFILT_AUTO (0<<3)
#define BT848_SCLOOP_HFILT_CIF  (1<<3)
#define BT848_SCLOOP_HFILT_QCIF (2<<3)
#define BT848_SCLOOP_HFILT_ICON (3<<3)

#define BT848_OFORM            0x048
#define BT848_OFORM_RANGE      (1<<7)
#define BT848_OFORM_CORE0      (0<<5)
#define BT848_OFORM_CORE8      (1<<5)
#define BT848_OFORM_CORE16     (2<<5)
#define BT848_OFORM_CORE32     (3<<5)

#define BT848_E_VSCALE_HI      0x04C
#define BT848_O_VSCALE_HI      0x0CC
#define BT848_VSCALE_YCOMB     (1<<7)
#define BT848_VSCALE_COMB      (1<<6)
#define BT848_VSCALE_INT       (1<<5)
#define BT848_VSCALE_HI        15

#define BT848_E_VSCALE_LO      0x050
#define BT848_O_VSCALE_LO      0x0D0
#define BT848_TEST             0x054
#define BT848_ADELAY           0x060
#define BT848_BDELAY           0x064

#define BT848_ADC              0x068
#define BT848_ADC_RESERVED     (2<<6)
#define BT848_ADC_SYNC_T       (1<<5)
#define BT848_ADC_AGC_EN       (1<<4)
#define BT848_ADC_CLK_SLEEP    (1<<3)
#define BT848_ADC_Y_SLEEP      (1<<2)
#define BT848_ADC_C_SLEEP      (1<<1)
#define BT848_ADC_CRUSH        (1<<0)

#define BT848_E_VTC            0x06C
#define BT848_O_VTC            0x0EC
#define BT848_VTC_HSFMT        (1<<7)
#define BT848_VTC_VFILT_2TAP   0
#define BT848_VTC_VFILT_3TAP   1
#define BT848_VTC_VFILT_4TAP   2
#define BT848_VTC_VFILT_5TAP   3

#define BT848_SRESET           0x07C

#define BT848_COLOR_FMT             0x0D4
#define BT848_COLOR_FMT_O_RGB32     (0<<4)
#define BT848_COLOR_FMT_O_RGB24     (1<<4)
#define BT848_COLOR_FMT_O_RGB16     (2<<4)
#define BT848_COLOR_FMT_O_RGB15     (3<<4)
#define BT848_COLOR_FMT_O_YUY2      (4<<4)
#define BT848_COLOR_FMT_O_BtYUV     (5<<4)
#define BT848_COLOR_FMT_O_Y8        (6<<4)
#define BT848_COLOR_FMT_O_RGB8      (7<<4)
#define BT848_COLOR_FMT_O_YCrCb422  (8<<4)
#define BT848_COLOR_FMT_O_YCrCb411  (9<<4)
#define BT848_COLOR_FMT_O_RAW       (14<<4)
#define BT848_COLOR_FMT_E_RGB32     0
#define BT848_COLOR_FMT_E_RGB24     1
#define BT848_COLOR_FMT_E_RGB16     2
#define BT848_COLOR_FMT_E_RGB15     3
#define BT848_COLOR_FMT_E_YUY2      4
#define BT848_COLOR_FMT_E_BtYUV     5
#define BT848_COLOR_FMT_E_Y8        6
#define BT848_COLOR_FMT_E_RGB8      7
#define BT848_COLOR_FMT_E_YCrCb422  8
#define BT848_COLOR_FMT_E_YCrCb411  9
#define BT848_COLOR_FMT_E_RAW       14

#define BT848_COLOR_FMT_RGB32       0x00
#define BT848_COLOR_FMT_RGB24       0x11
#define BT848_COLOR_FMT_RGB16       0x22
#define BT848_COLOR_FMT_RGB15       0x33
#define BT848_COLOR_FMT_YUY2        0x44
#define BT848_COLOR_FMT_BtYUV       0x55
#define BT848_COLOR_FMT_Y8          0x66
#define BT848_COLOR_FMT_RGB8        0x77
#define BT848_COLOR_FMT_YCrCb422    0x88
#define BT848_COLOR_FMT_YCrCb411    0x99
#define BT848_COLOR_FMT_RAW         0xee

#define BT848_COLOR_CTL                0x0D8
#define BT848_COLOR_CTL_EXT_FRMRATE    (1<<7)
#define BT848_COLOR_CTL_COLOR_BARS     (1<<6)
#define BT848_COLOR_CTL_RGB_DED        (1<<5)
#define BT848_COLOR_CTL_GAMMA          (1<<4)
#define BT848_COLOR_CTL_WSWAP_ODD      (1<<3)
#define BT848_COLOR_CTL_WSWAP_EVEN     (1<<2)
#define BT848_COLOR_CTL_BSWAP_ODD      (1<<1)
#define BT848_COLOR_CTL_BSWAP_EVEN     (1<<0)

#define BT848_CAP_CTL                  0x0DC
#define BT848_CAP_CTL_DITH_FRAME       (1<<4)
#define BT848_CAP_CTL_CAPTURE_VBI_ODD  (1<<3)
#define BT848_CAP_CTL_CAPTURE_VBI_EVEN (1<<2)
#define BT848_CAP_CTL_CAPTURE_ODD      (1<<1)
#define BT848_CAP_CTL_CAPTURE_EVEN     (1<<0)

#define BT848_VBI_PACK_SIZE    0x0E0

#define BT848_VBI_PACK_DEL     0x0E4
#define BT848_VBI_PACK_DEL_VBI_HDELAY 0xfc
#define BT848_VBI_PACK_DEL_EXT_FRAME  2
#define BT848_VBI_PACK_DEL_VBI_PKT_HI 1

#define BT848_INT_STAT         0x100
#define BT848_INT_MASK         0x104

#define BT848_INT_ETBF         (1<<23)

#define BT848_INT_RISCS   (0xf<<28)
#define BT848_INT_RISC_EN (1<<27)
#define BT848_INT_RACK    (1<<25)
#define BT848_INT_FIELD   (1<<24)
#define BT848_INT_SCERR   (1<<19)
#define BT848_INT_OCERR   (1<<18)
#define BT848_INT_PABORT  (1<<17)
#define BT848_INT_RIPERR  (1<<16)
#define BT848_INT_PPERR   (1<<15)
#define BT848_INT_FDSR    (1<<14)
#define BT848_INT_FTRGT   (1<<13)
#define BT848_INT_FBUS    (1<<12)
#define BT848_INT_RISCI   (1<<11)
#define BT848_INT_GPINT   (1<<9)
#define BT848_INT_I2CDONE (1<<8)
#define BT848_INT_VPRES   (1<<5)
#define BT848_INT_HLOCK   (1<<4)
#define BT848_INT_OFLOW   (1<<3)
#define BT848_INT_HSYNC   (1<<2)
#define BT848_INT_VSYNC   (1<<1)
#define BT848_INT_FMTCHG  (1<<0)


#define BT848_GPIO_DMA_CTL             0x10C
#define BT848_GPIO_DMA_CTL_GPINTC      (1<<15)
#define BT848_GPIO_DMA_CTL_GPINTI      (1<<14)
#define BT848_GPIO_DMA_CTL_GPWEC       (1<<13)
#define BT848_GPIO_DMA_CTL_GPIOMODE    (3<<11)
#define BT848_GPIO_DMA_CTL_GPCLKMODE   (1<<10)
#define BT848_GPIO_DMA_CTL_PLTP23_4    (0<<6)
#define BT848_GPIO_DMA_CTL_PLTP23_8    (1<<6)
#define BT848_GPIO_DMA_CTL_PLTP23_16   (2<<6)
#define BT848_GPIO_DMA_CTL_PLTP23_32   (3<<6)
#define BT848_GPIO_DMA_CTL_PLTP1_4     (0<<4)
#define BT848_GPIO_DMA_CTL_PLTP1_8     (1<<4)
#define BT848_GPIO_DMA_CTL_PLTP1_16    (2<<4)
#define BT848_GPIO_DMA_CTL_PLTP1_32    (3<<4)
#define BT848_GPIO_DMA_CTL_PKTP_4      (0<<2)
#define BT848_GPIO_DMA_CTL_PKTP_8      (1<<2)
#define BT848_GPIO_DMA_CTL_PKTP_16     (2<<2)
#define BT848_GPIO_DMA_CTL_PKTP_32     (3<<2)
#define BT848_GPIO_DMA_CTL_RISC_ENABLE (1<<1)
#define BT848_GPIO_DMA_CTL_FIFO_ENABLE (1<<0)

#define BT848_I2C              0x110
#define BT848_I2C_DIV          (0xf<<4)
#define BT848_I2C_SYNC         (1<<3)
#define BT848_I2C_W3B          (1<<2)
#define BT848_I2C_SCL          (1<<1)
#define BT848_I2C_SDA          (1<<0)


#define BT848_RISC_STRT_ADD    0x114
#define BT848_GPIO_OUT_EN      0x118
#define BT848_GPIO_REG_INP     0x11C
#define BT848_RISC_COUNT       0x120
#define BT848_GPIO_DATA        0x200
#define BT848_GPIO_DATA_HIBYTE 0x202


/* Bt848 RISC commands */

/* only for the SYNC RISC command */
#define BT848_FIFO_STATUS_FM1  0x06
#define BT848_FIFO_STATUS_FM3  0x0e
#define BT848_FIFO_STATUS_SOL  0x02
#define BT848_FIFO_STATUS_EOL4 0x01
#define BT848_FIFO_STATUS_EOL3 0x0d
#define BT848_FIFO_STATUS_EOL2 0x09
#define BT848_FIFO_STATUS_EOL1 0x05
#define BT848_FIFO_STATUS_VRE  0x04
#define BT848_FIFO_STATUS_VRO  0x0c
#define BT848_FIFO_STATUS_PXV  0x00

#define BT848_RISC_RESYNC      (1<<15)

/* WRITE and SKIP */
/* disable which bytes of each DWORD */
#define BT848_RISC_BYTE0       (1<<12)
#define BT848_RISC_BYTE1       (1<<13)
#define BT848_RISC_BYTE2       (1<<14)
#define BT848_RISC_BYTE3       (1<<15)
#define BT848_RISC_BYTE_ALL    (0x0f<<12)
#define BT848_RISC_BYTE_NONE   0
/* cause RISCI */
#define BT848_RISC_IRQ         (1<<24)
/* RISC command is last one in this line */
#define BT848_RISC_EOL         (1<<26)
/* RISC command is first one in this line */
#define BT848_RISC_SOL         (1<<27)

#define BT848_RISC_WRITE       (0x01<<28)
#define BT848_RISC_SKIP        (0x02<<28)
#define BT848_RISC_WRITEC      (0x05<<28)
#define BT848_RISC_JUMP        (0x07<<28)
#define BT848_RISC_SYNC        (0x08<<28)

#define BT848_RISC_WRITE123    (0x09<<28)
#define BT848_RISC_SKIP123     (0x0a<<28)
#define BT848_RISC_WRITE1S23   (0x0b<<28)


// ---------------------------------------------------------------------------
// WinDriver interface

typedef enum
{
    BT8X8_MODE_BYTE   = 0,
    BT8X8_MODE_WORD   = 1,
    BT8X8_MODE_DWORD  = 2
} BT8X8_MODE;


typedef enum
{
    BT8X8_AD_BAR0 = 0,
    BT8X8_AD_BAR1 = 1,
    BT8X8_AD_BAR2 = 2,
    BT8X8_AD_BAR3 = 3,
    BT8X8_AD_BAR4 = 4,
    BT8X8_AD_BAR5 = 5,
    BT8X8_AD_EPROM = 6,
    BT8X8_ITEMS = 7
} BT8X8_ADDR;

typedef struct BT8X8_STRUCT *BT8X8_HANDLE;


typedef struct
{
    DWORD dwCounter;   // number of interrupts received
    DWORD dwLost;      // number of interrupts not yet dealt with
    BOOL fStopped;     // was interrupt disabled during wait
} BT8X8_INT_RESULT;
typedef void (WINAPI *BT8X8_INT_HANDLER)( BT8X8_HANDLE hBT8X8, BT8X8_INT_RESULT *intResult);

typedef struct
{
    WD_INTERRUPT Int;
    HANDLE hThread;
    WD_TRANSFER Trans[3];
    BT8X8_INT_HANDLER funcIntHandler;
} BT8X8_INT_INTERRUPT;

typedef struct
{
    DWORD index;
    DWORD dwMask;
    BOOL  fIsMemory;
    BOOL  fActive;
} BT8X8_ADDR_DESC;

typedef struct BT8X8_STRUCT
{
    HANDLE hWD;
    BOOL   fUseInt;
    BT8X8_INT_INTERRUPT Int;
    WD_PCI_SLOT pciSlot;
    BT8X8_ADDR_DESC addrDesc[BT8X8_ITEMS];
    WD_CARD_REGISTER cardReg;
} BT8X8_STRUCT;

enum {
    PCI_IDR  = 0x00,
    PCI_CR   = 0x04,
    PCI_SR   = 0x06,
    PCI_REV  = 0x08,
    PCI_CCR  = 0x09,
    PCI_LSR  = 0x0c,
    PCI_LTR  = 0x0d,
    PCI_HTR  = 0x0e,
    PCI_BISTR= 0x0f,
    PCI_BAR0 = 0x10,
    PCI_BAR1 = 0x14,
    PCI_BAR2 = 0x18,
    PCI_BAR3 = 0x1c,
    PCI_BAR4 = 0x20,
    PCI_BAR5 = 0x24,
    PCI_CIS  = 0x28,
    PCI_SVID = 0x2c,
    PCI_SID  = 0x2e,
    PCI_ERBAR= 0x30,
    PCI_ILR  = 0x3c,
    PCI_IPR  = 0x3d,
    PCI_MGR  = 0x3e,
    PCI_MLR  = 0x3f
};

typedef enum
{
   BT8X8_OPEN_RESULT_OK,
   BT8X8_OPEN_RESULT_DRIVER,
   BT8X8_OPEN_RESULT_CARDIDX,
   BT8X8_OPEN_RESULT_REGISTER,
   BT8X8_OPEN_RESULT_ELEMS,
   BT8X8_OPEN_RESULT_MALLOC,
   BT8X8_OPEN_RESULT_VERSION,
};

// ids for identification of Booktree chips on the PCI bus
#define PCI_VENDOR_ID_BROOKTREE 0x109e
#define PCI_DEVICE_ID_BT848     0x350
#define PCI_DEVICE_ID_BT849     0x351
#define PCI_DEVICE_ID_BT878     0x36e
#define PCI_DEVICE_ID_BT879     0x36f


// ---------------------------------------------------------------------------
// forward declarations for WinDriver interface functions
//
static int BT8X8_Open (BT8X8_HANDLE *phBT8X8);
static void BT8X8_Close(BT8X8_HANDLE hBT8X8);
//static DWORD BT8X8_CountCards (DWORD dwVendorID, DWORD dwDeviceID);
static BOOL BT8X8_IsAddrSpaceActive(BT8X8_HANDLE hBT8X8, BT8X8_ADDR addrSpace);

// General read/write function
static void BT8X8_ReadWriteBlock(BT8X8_HANDLE hBT8X8, BT8X8_ADDR addrSpace, DWORD dwOffset, BOOL fRead, PVOID buf, DWORD dwBytes, BT8X8_MODE mode);
static BYTE BT8X8_ReadByte (BT8X8_HANDLE hBT8X8, BT8X8_ADDR addrSpace, DWORD dwOffset);
static WORD BT8X8_ReadWord (BT8X8_HANDLE hBT8X8, BT8X8_ADDR addrSpace, DWORD dwOffset);
static DWORD BT8X8_ReadDword (BT8X8_HANDLE hBT8X8, BT8X8_ADDR addrSpace, DWORD dwOffset);
static void BT8X8_WriteByte (BT8X8_HANDLE hBT8X8, BT8X8_ADDR addrSpace, DWORD dwOffset, BYTE data);
static void BT8X8_WriteWord (BT8X8_HANDLE hBT8X8, BT8X8_ADDR addrSpace, DWORD dwOffset, WORD data);
static void BT8X8_WriteDword (BT8X8_HANDLE hBT8X8, BT8X8_ADDR addrSpace, DWORD dwOffset, DWORD data);
// handle interrupts
static void WINAPI BT8X8_IntHandlerRoutine(BT8X8_HANDLE hBT8X8, BT8X8_INT_RESULT *intResult);
static BOOL BT8X8_IntIsEnabled (BT8X8_HANDLE hBT8X8);
static BOOL BT8X8_IntEnable (BT8X8_HANDLE hBT8X8, BT8X8_INT_HANDLER funcIntHandler);
static void BT8X8_IntDisable (BT8X8_HANDLE hBT8X8);
// access to PCI configuration registers
static void BT8X8_WritePCIReg(BT8X8_HANDLE hBT8X8, DWORD dwReg, DWORD dwData);
static DWORD BT8X8_ReadPCIReg(BT8X8_HANDLE hBT8X8, DWORD dwReg);
static BOOL BT8X8_DetectCardElements(BT8X8_HANDLE hBT8X8);


#endif  // __BT848_H

