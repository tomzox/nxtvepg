/*
 *  DVB PAT & PMT scan interface definition
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
 *    This header file defines the interface between the DTV driver
 *    module and the DVB PAT & PMT scan module.
 *
 *  Author: T. Zoerner
 */
#if !defined (__DVB_SCAN_PMT_H)
#define __DVB_SCAN_PMT_H

typedef struct
{
   int serviceId;
   int ttxPid;
} T_DVB_SCAN_PMT;

int  DvbScanPmt_Start(const char * pDemuxDev, const int * srv, int scvCnt);
void DvbScanPmt_Stop(void);
int DvbScanPmt_GetFds(fd_set *set, int max_fd);
int DvbScanPmt_ProcessFds(fd_set *set);
int DvbScanPmt_GetResults(T_DVB_SCAN_PMT * srv, int srvCnt);

#endif  /* __DVB_SCAN_PMT_H */
