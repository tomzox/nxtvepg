/*
 *  Declaration of common interfaces to different TV card drivers
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
 *  Description:
 *
 *    This header file declares types which are used to access hardware
 *    driver functionality through a common pointer based interface.
 *
 *
 *  Author: Tom Zoerner
 *
 *  $Id: tvcard.h,v 1.5 2003/03/09 19:31:22 tom Exp tom $
 */

#ifndef __TVCARD_H
#define __TVCARD_H

// ---------------------------------------------------------------------------
// Declaration of common hardware interfaces
//
struct TVCARD_struct;

typedef struct
{
   bool (* IsVideoPresent) ( void );
   bool (* StartAcqThread) ( void );
   void (* StopAcqThread)  ( void );
   bool (* Configure) ( uint threadPrio, uint pllType );
   void (* Close) ( void );
   bool (* Open) ( struct TVCARD_struct * pTvCard );
} TVCARD_CTL;

typedef struct
{
   const char * (* GetCardName) ( struct TVCARD_struct * pTvCard, uint CardId );
   uint         (* AutoDetectCardType) ( struct TVCARD_struct * pTvCard );
   uint         (* AutoDetectTuner) ( struct TVCARD_struct * pTvCard, uint CardId );
   bool         (* GetIffType) ( struct TVCARD_struct * pTvCard, bool * pIsMono );
   uint         (* GetPllType) ( struct TVCARD_struct * pTvCard, uint cardId );
   bool         (* SupportsAcpi) ( struct TVCARD_struct * pTvCard );
   uint         (* GetNumInputs) ( struct TVCARD_struct * pTvCard );
   const char * (* GetInputName) ( struct TVCARD_struct * pTvCard, uint nInput );
   bool         (* IsInputATuner) ( struct TVCARD_struct * pTvCard, uint nInput );
   bool         (* IsInputSVideo) ( struct TVCARD_struct * pTvCard, uint nInput );
   bool         (* SetVideoSource) ( struct TVCARD_struct * pTvCard, uint nInput );
} TVCARD_CFG;

typedef struct
{
   void   (* Sleep)  ( void );
   void   (* SetSDA) ( bool value );
   void   (* SetSCL) ( bool value );
   bool   (* GetSDA) ( void );
   bool   (* GetSCL) ( void );
} I2C_LINE_BUS;

typedef struct
{
   bool (* I2cRead) ( const struct TVCARD_struct * pTvCard,
                      const BYTE *writeBuffer,
                      size_t writeBufferSize,
                      BYTE *readBuffer,
                      size_t readBufferSize );
   bool (* I2cWrite)( const struct TVCARD_struct * pTvCard,
                      const BYTE *writeBuffer,
                      size_t writeBufferSize );
} I2C_RW;

typedef struct
{
   uint   cardId;
   DWORD  BusNumber;
   DWORD  SlotNumber;
   DWORD  VendorId;
   DWORD  DeviceId;
   DWORD  SubSystemId;
} TVCARD_PARAMS;

typedef struct TVCARD_struct
{
   const TVCARD_CTL    * ctl;
   const TVCARD_CFG    * cfg;
   const I2C_LINE_BUS  * i2cLineBus;
   const I2C_RW        * i2cBus;

   TVCARD_PARAMS       params;
} TVCARD;


#endif  // __TVCARD_H
