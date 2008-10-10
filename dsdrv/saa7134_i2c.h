/*
 *  Win32 I2C driver for the Philips SAA7134 chip
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
 *  Author: see C file
 *
 *  $Id: saa7134_i2c.h,v 1.3 2003/01/19 08:09:12 tom Exp tom $
 */

#ifndef __SAA7134_I2C_H
#define __SAA7134_I2C_H

// ---------------------------------------------------------------------------
// Interface declaration
//
void SAA7134_I2cGetInterface( TVCARD * pTvCard );

#endif  // __SAA7134_I2C_H