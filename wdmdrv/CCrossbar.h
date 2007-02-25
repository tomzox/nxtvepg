/*
 *  CCrossbar include file
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
 *    This header file declares the CCrossbar Class.
 *
 *
 *  Author: Gérard Chevalier
 *
 */
typedef IPin *IPinPtr;

// The CCrossbarInfo object holds all pins of a given Crossbar.
// there are as much CCrossbarInfo objects as existing Crossbar in the capture graph.
// These ojects are stored in a list, and they are used only during the paths discovery step.
class CCrossbarInfo {
public:
	IAMCrossbar *m_pCrossbar;	// Pt to the Crossbar
	int InputsNb, OutputsNb;	// Nb of Inputs and Outputs pins
	IPin **Inputs, **Outputs;		// The pins
};

// List of Crossbars
typedef CGenericList<CCrossbarInfo> CCrossbarInfoList;

// The CCrossbarRouteInfo object stores a possible route in a Crossbar.
// It holds mainly :
//  - the input & output pins indexes pairs for both the video and associated audio paths
//  - a reference to it's associated IAMCrossbar interface
//  - type of input at beginning of the path and the path's lenghth
// A complete routing path is made  of a set of CCrossbarRouteInfo objects, one for each
// Crossbar crossed. The CCrossbarRouteInfo objects for a complete path are stored in a
// array and the different paths are stored in a list.
class CCrossbarRouteInfo {
public:
    class CCrossbarRouteInfo       *pUpstreamConnection;	// Only used during paths build
    LONG	VideoInputIndex, VideoOutputIndex;
    LONG	AudioInputIndex, AudioOutputIndex;
    IAMCrossbar	*pCrossbar;
    LONG	OutputPhysicalType;
	// Next 2 members only meaningfull in the first element of the path.
	int		PathLength;
    LONG	InputPhysicalType;	// Physical type of input at beginning of path
};

// Typedef for list of possible routes. In this list, a path is an array of CCrossbarRouteInfo
// objects.
typedef CGenericList<CCrossbarRouteInfo> CCrossbarRouteInfoList;


// The CCrossbar object manages the Crossbar filters set associated with a capture device.
// Given the input pin of the video capture filter, the constructor searchs all possible routes
// starting from the output pin connected to input given to all reachable video inputs.
// A Crossbar must be directly connected to the given input pin, which is normally what happens
// in a capture graph built by FindInterface (according to MS documentation).
// This class supports an arbitrary number of Crossbars, provided that they are connected to
// each other directly without intermediate filters.
// During the path discovery, which trace upstream all possible video paths to the given starting
// pin, the assiciated video path is also recorded.
// Once created, CCrossbar members can tell or do for this "Meta Crossbar" :
//  - total number of video inputs
//  - type and name of each video input
//  - connect a path for a given input to the "ultimate" output. This connection can also
//    optionnaly connect the associated audio path or leaves it muted (used for VBI capture only
//    application for instance).
class CCrossbar {
private:
	CCrossbarInfoList	*m_CrossbarInfoList;
	CCrossbarRouteInfoList	*m_PathsList;
	IPin	*pFinalOutput;	// Last output of all Crossbars chained

	HRESULT AddCrossbar(IAMCrossbar *pCrossbar);
	void DestroyCrossbarList();
	CCrossbarInfo *GetCrossbarInfo(IAMCrossbar *pCrossbar);
	int GetPinIndex(CCrossbarInfo *pCrossbarInfo, IPin *pPin, int iPinDirection);
	CCrossbarRouteInfo *GetPathAtIndex(LONG lIndex);
	HRESULT BuildRoutes(IPin *FromOutput, CCrossbarRouteInfo *pUpstreamConnection,
		int iCurrentPathLength);
	HRESULT SavePath(CCrossbarRouteInfo *pFirstPathElement);
	void DestroyPathsList();

public:
    CCrossbar (IPin *pStartingInput, HRESULT *phr);
    ~CCrossbar();

    HRESULT GetInputCount (LONG *pCount);
    HRESULT GetInputInfo(LONG Index, char **pName, LONG *pPhysicalType);
    HRESULT SetInputIndex (LONG Index, BOOL Mute);
};

