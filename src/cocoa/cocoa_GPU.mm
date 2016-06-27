/*
	Copyright (C) 2013-2016 DeSmuME team

	This file is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	This file is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with the this software.  If not, see <http://www.gnu.org/licenses/>.
 */

#import "cocoa_GPU.h"
#import "cocoa_output.h"
#import "cocoa_globals.h"
#include "utilities.h"

#include "../NDSSystem.h"
#include "../GPU.h"
#include "../rasterize.h"

#ifdef MAC_OS_X_VERSION_10_7
#include "../OGLRender_3_2.h"
#else
#include "../OGLRender.h"
#endif

#include <OpenGL/OpenGL.h>

#undef BOOL

GPU3DInterface *core3DList[] = {
	&gpu3DNull,
	&gpu3DRasterize,
	&gpu3Dgl,
	NULL
};

class GPUEventHandlerOSX : public GPUEventHandlerDefault
{
private:
	pthread_rwlock_t _rwlockFrame;
	pthread_mutex_t _mutex3DRender;
	pthread_mutex_t *_mutexOutputList;
	NSMutableArray *_cdsOutputList;
	bool _render3DNeedsFinish;
	
public:
	GPUEventHandlerOSX();
	~GPUEventHandlerOSX();
	
	void FramebufferLockWrite();
	void FramebufferLockRead();
	void FramebufferUnlock();
	void Render3DLock();
	void Render3DUnlock();
	
	void FrameFinish();
	void SetVideoBuffers();
	
	pthread_rwlock_t* GetFrameRWLock();
	NSMutableArray* GetOutputList();
	void SetOutputList(NSMutableArray *outputList, pthread_mutex_t *theMutex);
	bool GetRender3DNeedsFinish();
	
	virtual void DidFrameBegin(bool isFrameSkipRequested);
	virtual void DidFrameEnd(bool isFrameSkipped);
	virtual void DidRender3DBegin();
	virtual void DidRender3DEnd();
};

@implementation CocoaDSGPU

@dynamic gpuStateFlags;
@dynamic gpuDimensions;
@dynamic gpuScale;
@dynamic gpuColorFormat;
@dynamic gpuFrameRWLock;

@dynamic layerMainGPU;
@dynamic layerMainBG0;
@dynamic layerMainBG1;
@dynamic layerMainBG2;
@dynamic layerMainBG3;
@dynamic layerMainOBJ;
@dynamic layerSubGPU;
@dynamic layerSubBG0;
@dynamic layerSubBG1;
@dynamic layerSubBG2;
@dynamic layerSubBG3;
@dynamic layerSubOBJ;

@dynamic render3DRenderingEngine;
@dynamic render3DHighPrecisionColorInterpolation;
@dynamic render3DEdgeMarking;
@dynamic render3DFog;
@dynamic render3DTextures;
@dynamic render3DDepthComparisonThreshold;
@dynamic render3DThreads;
@dynamic render3DLineHack;
@dynamic render3DMultisample;
@dynamic render3DTextureDeposterize;
@dynamic render3DTextureSmoothing;
@dynamic render3DTextureScalingFactor;
@dynamic render3DFragmentSamplingHack;


- (id)init
{
	self = [super init];
	if (self == nil)
	{
		return self;
	}
	
	spinlockGpuState = OS_SPINLOCK_INIT;
	
	_gpuScale = 1;
	gpuStateFlags	= GPUSTATE_MAIN_GPU_MASK |
					  GPUSTATE_MAIN_BG0_MASK |
					  GPUSTATE_MAIN_BG1_MASK |
					  GPUSTATE_MAIN_BG2_MASK |
					  GPUSTATE_MAIN_BG3_MASK |
					  GPUSTATE_MAIN_OBJ_MASK |
					  GPUSTATE_SUB_GPU_MASK |
					  GPUSTATE_SUB_BG0_MASK |
					  GPUSTATE_SUB_BG1_MASK |
					  GPUSTATE_SUB_BG2_MASK |
					  GPUSTATE_SUB_BG3_MASK |
					  GPUSTATE_SUB_OBJ_MASK;
	
	isCPUCoreCountAuto = NO;
	_needRestoreFrameLock = NO;
	_needRestoreRender3DLock = NO;
		
	SetOpenGLRendererFunctions(&OSXOpenGLRendererInit,
							   &OSXOpenGLRendererBegin,
							   &OSXOpenGLRendererEnd,
							   &OSXOpenGLRendererFramebufferDidResize);
	
	gpuEvent = new GPUEventHandlerOSX;
	GPU->SetEventHandler(gpuEvent);
	GPU->SetWillAutoResolveToCustomBuffer(false);
	
	return self;
}

- (void)dealloc
{
	DestroyOpenGLRenderer();
	
	delete gpuEvent;
	
	[super dealloc];
}

- (void) setGpuStateFlags:(UInt32)flags
{
	OSSpinLockLock(&spinlockGpuState);
	gpuStateFlags = flags;
	OSSpinLockUnlock(&spinlockGpuState);
	
	[self setLayerMainGPU:((flags & GPUSTATE_MAIN_GPU_MASK) != 0)];
	[self setLayerMainBG0:((flags & GPUSTATE_MAIN_BG0_MASK) != 0)];
	[self setLayerMainBG1:((flags & GPUSTATE_MAIN_BG1_MASK) != 0)];
	[self setLayerMainBG2:((flags & GPUSTATE_MAIN_BG2_MASK) != 0)];
	[self setLayerMainBG3:((flags & GPUSTATE_MAIN_BG3_MASK) != 0)];
	[self setLayerMainOBJ:((flags & GPUSTATE_MAIN_OBJ_MASK) != 0)];
	
	[self setLayerSubGPU:((flags & GPUSTATE_SUB_GPU_MASK) != 0)];
	[self setLayerSubBG0:((flags & GPUSTATE_SUB_BG0_MASK) != 0)];
	[self setLayerSubBG1:((flags & GPUSTATE_SUB_BG1_MASK) != 0)];
	[self setLayerSubBG2:((flags & GPUSTATE_SUB_BG2_MASK) != 0)];
	[self setLayerSubBG3:((flags & GPUSTATE_SUB_BG3_MASK) != 0)];
	[self setLayerSubOBJ:((flags & GPUSTATE_SUB_OBJ_MASK) != 0)];
}

- (UInt32) gpuStateFlags
{
	OSSpinLockLock(&spinlockGpuState);
	const UInt32 flags = gpuStateFlags;
	OSSpinLockUnlock(&spinlockGpuState);
	
	return flags;
}

- (void) setGpuDimensions:(NSSize)theDimensions
{
	gpuEvent->FrameFinish();
	gpuEvent->Render3DLock();
	gpuEvent->FramebufferLockWrite();
	
	GPU->SetCustomFramebufferSize(theDimensions.width, theDimensions.height);
	
	gpuEvent->SetVideoBuffers();
	gpuEvent->FramebufferUnlock();
	gpuEvent->Render3DUnlock();
}

- (NSSize) gpuDimensions
{
	gpuEvent->Render3DLock();
	gpuEvent->FramebufferLockRead();
	const NSSize dimensions = NSMakeSize(GPU->GetCustomFramebufferWidth(), GPU->GetCustomFramebufferHeight());
	gpuEvent->FramebufferUnlock();
	gpuEvent->Render3DUnlock();
	
	return dimensions;
}

- (void) setGpuScale:(NSUInteger)theScale
{
	_gpuScale = (uint8_t)theScale;
	[self setGpuDimensions:NSMakeSize(GPU_DISPLAY_WIDTH * theScale, GPU_DISPLAY_HEIGHT * theScale)];
}

- (NSUInteger) gpuScale
{
	return (NSUInteger)_gpuScale;
}

- (void) setGpuColorFormat:(NSUInteger)colorFormat
{
	gpuEvent->FrameFinish();
	gpuEvent->Render3DLock();
	gpuEvent->FramebufferLockWrite();
	
	GPU->SetColorFormat((NDSColorFormat)colorFormat);
	
	gpuEvent->SetVideoBuffers();
	gpuEvent->FramebufferUnlock();
	gpuEvent->Render3DUnlock();
}

- (NSUInteger) gpuColorFormat
{
	gpuEvent->Render3DLock();
	gpuEvent->FramebufferLockRead();
	const NSUInteger colorFormat = (NSUInteger)GPU->GetDisplayInfo().colorFormat;
	gpuEvent->FramebufferUnlock();
	gpuEvent->Render3DUnlock();
	
	return colorFormat;
}

- (pthread_rwlock_t *) gpuFrameRWLock
{
	return gpuEvent->GetFrameRWLock();
}

- (void) setOutputList:(NSMutableArray *)theOutputList mutexPtr:(pthread_mutex_t *)theMutex
{
	gpuEvent->SetOutputList(theOutputList, theMutex);
}

- (void) setRender3DRenderingEngine:(NSInteger)methodID
{
	gpuEvent->Render3DLock();
	NDS_3D_ChangeCore(methodID);
	gpuEvent->Render3DUnlock();
}

- (NSInteger) render3DRenderingEngine
{
	gpuEvent->Render3DLock();
	const NSInteger methodID = (NSInteger)cur3DCore;
	gpuEvent->Render3DUnlock();
	
	return methodID;
}

- (void) setRender3DHighPrecisionColorInterpolation:(BOOL)state
{
	gpuEvent->Render3DLock();
	CommonSettings.GFX3D_HighResolutionInterpolateColor = state ? true : false;
	gpuEvent->Render3DUnlock();
}

- (BOOL) render3DHighPrecisionColorInterpolation
{
	gpuEvent->Render3DLock();
	const BOOL state = CommonSettings.GFX3D_HighResolutionInterpolateColor ? YES : NO;
	gpuEvent->Render3DUnlock();
	
	return state;
}

- (void) setRender3DEdgeMarking:(BOOL)state
{
	gpuEvent->Render3DLock();
	CommonSettings.GFX3D_EdgeMark = state ? true : false;
	gpuEvent->Render3DUnlock();
}

- (BOOL) render3DEdgeMarking
{
	gpuEvent->Render3DLock();
	const BOOL state = CommonSettings.GFX3D_EdgeMark ? YES : NO;
	gpuEvent->Render3DUnlock();
	
	return state;
}

- (void) setRender3DFog:(BOOL)state
{
	gpuEvent->Render3DLock();
	CommonSettings.GFX3D_Fog = state ? true : false;
	gpuEvent->Render3DUnlock();
}

- (BOOL) render3DFog
{
	gpuEvent->Render3DLock();
	const BOOL state = CommonSettings.GFX3D_Fog ? YES : NO;
	gpuEvent->Render3DUnlock();
	
	return state;
}

- (void) setRender3DTextures:(BOOL)state
{
	gpuEvent->Render3DLock();
	CommonSettings.GFX3D_Texture = state ? true : false;
	gpuEvent->Render3DUnlock();
}

- (BOOL) render3DTextures
{
	gpuEvent->Render3DLock();
	const BOOL state = CommonSettings.GFX3D_Texture ? YES : NO;
	gpuEvent->Render3DUnlock();
	
	return state;
}

- (void) setRender3DDepthComparisonThreshold:(NSUInteger)threshold
{
	gpuEvent->Render3DLock();
	CommonSettings.GFX3D_Zelda_Shadow_Depth_Hack = threshold;
	gpuEvent->Render3DUnlock();
}

- (NSUInteger) render3DDepthComparisonThreshold
{
	gpuEvent->Render3DLock();
	const NSUInteger threshold = (NSUInteger)CommonSettings.GFX3D_Zelda_Shadow_Depth_Hack;
	gpuEvent->Render3DUnlock();
	
	return threshold;
}

- (void) setRender3DThreads:(NSUInteger)numberThreads
{
	NSUInteger numberCores = [[NSProcessInfo processInfo] activeProcessorCount];
	if (numberThreads == 0)
	{
		isCPUCoreCountAuto = YES;
		
		if (numberCores >= 8)
		{
			numberCores = 8;
		}
		else if (numberCores >= 4)
		{
			numberCores = 4;
		}
		else if (numberCores >= 2)
		{
			numberCores = 2;
		}
		else
		{
			numberCores = 1;
		}
	}
	else
	{
		isCPUCoreCountAuto = NO;
		numberCores = numberThreads;
	}
	
	const NSInteger renderingEngineID = [self render3DRenderingEngine];
	
	gpuEvent->Render3DLock();
	
	CommonSettings.num_cores = numberCores;
	
	if (renderingEngineID == CORE3DLIST_SWRASTERIZE)
	{
		NDS_3D_ChangeCore(renderingEngineID);
	}
	
	gpuEvent->Render3DUnlock();
}

- (NSUInteger) render3DThreads
{
	gpuEvent->Render3DLock();
	const NSUInteger numberThreads = isCPUCoreCountAuto ? 0 : (NSUInteger)CommonSettings.num_cores;
	gpuEvent->Render3DUnlock();
	
	return numberThreads;
}

- (void) setRender3DLineHack:(BOOL)state
{
	gpuEvent->Render3DLock();
	CommonSettings.GFX3D_LineHack = state ? true : false;
	gpuEvent->Render3DUnlock();
}

- (BOOL) render3DLineHack
{
	gpuEvent->Render3DLock();
	const BOOL state = CommonSettings.GFX3D_LineHack ? YES : NO;
	gpuEvent->Render3DUnlock();
	
	return state;
}

- (void) setRender3DMultisample:(BOOL)state
{
	gpuEvent->Render3DLock();
	CommonSettings.GFX3D_Renderer_Multisample = state ? true : false;
	gpuEvent->Render3DUnlock();
}

- (BOOL) render3DMultisample
{
	gpuEvent->Render3DLock();
	const BOOL state = CommonSettings.GFX3D_Renderer_Multisample ? YES : NO;
	gpuEvent->Render3DUnlock();
	
	return state;
}

- (void) setRender3DTextureDeposterize:(BOOL)state
{
	gpuEvent->Render3DLock();
	CommonSettings.GFX3D_Renderer_TextureDeposterize = state ? true : false;
	gpuEvent->Render3DUnlock();
}

- (BOOL) render3DTextureDeposterize
{
	gpuEvent->Render3DLock();
	const BOOL state = CommonSettings.GFX3D_Renderer_TextureDeposterize ? YES : NO;
	gpuEvent->Render3DUnlock();
	
	return state;
}

- (void) setRender3DTextureSmoothing:(BOOL)state
{
	gpuEvent->Render3DLock();
	CommonSettings.GFX3D_Renderer_TextureSmoothing = state ? true : false;
	gpuEvent->Render3DUnlock();
}

- (BOOL) render3DTextureSmoothing
{
	gpuEvent->Render3DLock();
	const BOOL state = CommonSettings.GFX3D_Renderer_TextureSmoothing ? YES : NO;
	gpuEvent->Render3DUnlock();
	
	return state;
}

- (void) setRender3DTextureScalingFactor:(NSUInteger)scalingFactor
{
	int newScalingFactor = (int)scalingFactor;
	
	if (scalingFactor < 1)
	{
		newScalingFactor = 1;
	}
	else if (scalingFactor > 4)
	{
		newScalingFactor = 4;
	}
	
	gpuEvent->Render3DLock();
	
	if (newScalingFactor == 3)
	{
		newScalingFactor = (newScalingFactor < CommonSettings.GFX3D_Renderer_TextureScalingFactor) ? 2 : 4;
	}
	
	CommonSettings.GFX3D_Renderer_TextureScalingFactor = newScalingFactor;
	gpuEvent->Render3DUnlock();
}

- (NSUInteger) render3DTextureScalingFactor
{
	gpuEvent->Render3DLock();
	const NSUInteger scalingFactor = (NSUInteger)CommonSettings.GFX3D_Renderer_TextureScalingFactor;
	gpuEvent->Render3DUnlock();
	
	return scalingFactor;
}

- (void) setRender3DFragmentSamplingHack:(BOOL)state
{
	gpuEvent->Render3DLock();
	CommonSettings.GFX3D_TXTHack = state ? true : false;
	gpuEvent->Render3DUnlock();
}

- (BOOL) render3DFragmentSamplingHack
{
	gpuEvent->Render3DLock();
	const BOOL state = CommonSettings.GFX3D_TXTHack ? YES : NO;
	gpuEvent->Render3DUnlock();
	
	return state;
}

- (void) setLayerMainGPU:(BOOL)gpuState
{
	gpuEvent->FramebufferLockWrite();
	GPU->GetEngineMain()->SetEnableState((gpuState) ? true : false);
	gpuEvent->FramebufferUnlock();
	
	OSSpinLockLock(&spinlockGpuState);
	gpuStateFlags = (gpuState) ? (gpuStateFlags | GPUSTATE_MAIN_GPU_MASK) : (gpuStateFlags & ~GPUSTATE_MAIN_GPU_MASK);
	OSSpinLockUnlock(&spinlockGpuState);
}

- (BOOL) layerMainGPU
{
	gpuEvent->FramebufferLockRead();
	const BOOL gpuState = GPU->GetEngineMain()->GetEnableState() ? YES : NO;
	gpuEvent->FramebufferUnlock();
	
	return gpuState;
}

- (void) setLayerMainBG0:(BOOL)layerState
{
	gpuEvent->FramebufferLockWrite();
	GPU->GetEngineMain()->SetLayerEnableState(GPULayerID_BG0, (layerState) ? true : false);
	gpuEvent->FramebufferUnlock();
	
	OSSpinLockLock(&spinlockGpuState);
	gpuStateFlags = (layerState) ? (gpuStateFlags | GPUSTATE_MAIN_BG0_MASK) : (gpuStateFlags & ~GPUSTATE_MAIN_BG0_MASK);
	OSSpinLockUnlock(&spinlockGpuState);
}

- (BOOL) layerMainBG0
{
	gpuEvent->FramebufferLockRead();
	const BOOL layerState = GPU->GetEngineMain()->GetLayerEnableState(GPULayerID_BG0);
	gpuEvent->FramebufferUnlock();
	
	return layerState;
}

- (void) setLayerMainBG1:(BOOL)layerState
{
	gpuEvent->FramebufferLockWrite();
	GPU->GetEngineMain()->SetLayerEnableState(GPULayerID_BG1, (layerState) ? true : false);
	gpuEvent->FramebufferUnlock();
	
	OSSpinLockLock(&spinlockGpuState);
	gpuStateFlags = (layerState) ? (gpuStateFlags | GPUSTATE_MAIN_BG1_MASK) : (gpuStateFlags & ~GPUSTATE_MAIN_BG1_MASK);
	OSSpinLockUnlock(&spinlockGpuState);
}

- (BOOL) layerMainBG1
{
	gpuEvent->FramebufferLockRead();
	const BOOL layerState = GPU->GetEngineMain()->GetLayerEnableState(GPULayerID_BG1);
	gpuEvent->FramebufferUnlock();
	
	return layerState;
}

- (void) setLayerMainBG2:(BOOL)layerState
{
	gpuEvent->FramebufferLockWrite();
	GPU->GetEngineMain()->SetLayerEnableState(GPULayerID_BG2, (layerState) ? true : false);
	gpuEvent->FramebufferUnlock();
	
	OSSpinLockLock(&spinlockGpuState);
	gpuStateFlags = (layerState) ? (gpuStateFlags | GPUSTATE_MAIN_BG2_MASK) : (gpuStateFlags & ~GPUSTATE_MAIN_BG2_MASK);
	OSSpinLockUnlock(&spinlockGpuState);
}

- (BOOL) layerMainBG2
{
	gpuEvent->FramebufferLockRead();
	const BOOL layerState = GPU->GetEngineMain()->GetLayerEnableState(GPULayerID_BG2);
	gpuEvent->FramebufferUnlock();
	
	return layerState;
}

- (void) setLayerMainBG3:(BOOL)layerState
{
	gpuEvent->FramebufferLockWrite();
	GPU->GetEngineMain()->SetLayerEnableState(GPULayerID_BG3, (layerState) ? true : false);
	gpuEvent->FramebufferUnlock();
	
	OSSpinLockLock(&spinlockGpuState);
	gpuStateFlags = (layerState) ? (gpuStateFlags | GPUSTATE_MAIN_BG3_MASK) : (gpuStateFlags & ~GPUSTATE_MAIN_BG3_MASK);
	OSSpinLockUnlock(&spinlockGpuState);
}

- (BOOL) layerMainBG3
{
	gpuEvent->FramebufferLockRead();
	const BOOL layerState = GPU->GetEngineMain()->GetLayerEnableState(GPULayerID_BG3);
	gpuEvent->FramebufferUnlock();
	
	return layerState;
}

- (void) setLayerMainOBJ:(BOOL)layerState
{
	gpuEvent->FramebufferLockWrite();
	GPU->GetEngineMain()->SetLayerEnableState(GPULayerID_OBJ, (layerState) ? true : false);
	gpuEvent->FramebufferUnlock();
	
	OSSpinLockLock(&spinlockGpuState);
	gpuStateFlags = (layerState) ? (gpuStateFlags | GPUSTATE_MAIN_OBJ_MASK) : (gpuStateFlags & ~GPUSTATE_MAIN_OBJ_MASK);
	OSSpinLockUnlock(&spinlockGpuState);
}

- (BOOL) layerMainOBJ
{
	gpuEvent->FramebufferLockRead();
	const BOOL layerState = GPU->GetEngineMain()->GetLayerEnableState(GPULayerID_OBJ);
	gpuEvent->FramebufferUnlock();
	
	return layerState;
}

- (void) setLayerSubGPU:(BOOL)gpuState
{
	gpuEvent->FramebufferLockWrite();
	GPU->GetEngineSub()->SetEnableState((gpuState) ? true : false);
	gpuEvent->FramebufferUnlock();
	
	OSSpinLockLock(&spinlockGpuState);
	gpuStateFlags = (gpuState) ? (gpuStateFlags | GPUSTATE_SUB_GPU_MASK) : (gpuStateFlags & ~GPUSTATE_SUB_GPU_MASK);
	OSSpinLockUnlock(&spinlockGpuState);
}

- (BOOL) layerSubGPU
{
	gpuEvent->FramebufferLockRead();
	const BOOL gpuState = GPU->GetEngineSub()->GetEnableState() ? YES : NO;
	gpuEvent->FramebufferUnlock();
	
	return gpuState;
}

- (void) setLayerSubBG0:(BOOL)layerState
{
	gpuEvent->FramebufferLockWrite();
	GPU->GetEngineSub()->SetLayerEnableState(GPULayerID_BG0, (layerState) ? true : false);
	gpuEvent->FramebufferUnlock();
	
	OSSpinLockLock(&spinlockGpuState);
	gpuStateFlags = (layerState) ? (gpuStateFlags | GPUSTATE_SUB_BG0_MASK) : (gpuStateFlags & ~GPUSTATE_SUB_BG0_MASK);
	OSSpinLockUnlock(&spinlockGpuState);
}

- (BOOL) layerSubBG0
{
	gpuEvent->FramebufferLockRead();
	const BOOL layerState = GPU->GetEngineSub()->GetLayerEnableState(GPULayerID_BG0);
	gpuEvent->FramebufferUnlock();
	
	return layerState;
}

- (void) setLayerSubBG1:(BOOL)layerState
{
	gpuEvent->FramebufferLockWrite();
	GPU->GetEngineSub()->SetLayerEnableState(GPULayerID_BG1, (layerState) ? true : false);
	gpuEvent->FramebufferUnlock();
	
	OSSpinLockLock(&spinlockGpuState);
	gpuStateFlags = (layerState) ? (gpuStateFlags | GPUSTATE_SUB_BG1_MASK) : (gpuStateFlags & ~GPUSTATE_SUB_BG1_MASK);
	OSSpinLockUnlock(&spinlockGpuState);
}

- (BOOL) layerSubBG1
{
	gpuEvent->FramebufferLockRead();
	const BOOL layerState = GPU->GetEngineSub()->GetLayerEnableState(GPULayerID_BG1);
	gpuEvent->FramebufferUnlock();
	
	return layerState;
}

- (void) setLayerSubBG2:(BOOL)layerState
{
	gpuEvent->FramebufferLockWrite();
	GPU->GetEngineSub()->SetLayerEnableState(GPULayerID_BG2, (layerState) ? true : false);
	gpuEvent->FramebufferUnlock();
	
	OSSpinLockLock(&spinlockGpuState);
	gpuStateFlags = (layerState) ? (gpuStateFlags | GPUSTATE_SUB_BG2_MASK) : (gpuStateFlags & ~GPUSTATE_SUB_BG2_MASK);
	OSSpinLockUnlock(&spinlockGpuState);
}

- (BOOL) layerSubBG2
{
	gpuEvent->FramebufferLockRead();
	const BOOL layerState = GPU->GetEngineSub()->GetLayerEnableState(GPULayerID_BG2);
	gpuEvent->FramebufferUnlock();
	
	return layerState;
}

- (void) setLayerSubBG3:(BOOL)layerState
{
	gpuEvent->FramebufferLockWrite();
	GPU->GetEngineSub()->SetLayerEnableState(GPULayerID_BG3, (layerState) ? true : false);
	gpuEvent->FramebufferUnlock();
	
	OSSpinLockLock(&spinlockGpuState);
	gpuStateFlags = (layerState) ? (gpuStateFlags | GPUSTATE_SUB_BG3_MASK) : (gpuStateFlags & ~GPUSTATE_SUB_BG3_MASK);
	OSSpinLockUnlock(&spinlockGpuState);
}

- (BOOL) layerSubBG3
{
	gpuEvent->FramebufferLockRead();
	const BOOL layerState = GPU->GetEngineSub()->GetLayerEnableState(GPULayerID_BG3);
	gpuEvent->FramebufferUnlock();
	
	return layerState;
}

- (void) setLayerSubOBJ:(BOOL)layerState
{
	gpuEvent->FramebufferLockWrite();
	GPU->GetEngineSub()->SetLayerEnableState(GPULayerID_OBJ, (layerState) ? true : false);
	gpuEvent->FramebufferUnlock();
	
	OSSpinLockLock(&spinlockGpuState);
	gpuStateFlags = (layerState) ? (gpuStateFlags | GPUSTATE_SUB_OBJ_MASK) : (gpuStateFlags & ~GPUSTATE_SUB_OBJ_MASK);
	OSSpinLockUnlock(&spinlockGpuState);
}

- (BOOL) layerSubOBJ
{
	gpuEvent->FramebufferLockRead();
	const BOOL layerState = GPU->GetEngineSub()->GetLayerEnableState(GPULayerID_OBJ);
	gpuEvent->FramebufferUnlock();
	
	return layerState;
}

- (BOOL) gpuStateByBit:(const UInt32)stateBit
{
	return ([self gpuStateFlags] & (1 << stateBit)) ? YES : NO;
}

- (NSString *) render3DRenderingEngineString
{
	NSString *theString = @"Uninitialized";
	
	gpuEvent->Render3DLock();
	
	if(gpu3D == NULL)
	{
		gpuEvent->Render3DUnlock();
		return theString;
	}
	
	const char *theName = gpu3D->name;
	theString = [NSString stringWithCString:theName encoding:NSUTF8StringEncoding];
	
	gpuEvent->Render3DUnlock();
	
	return theString;
}

- (void) clearWithColor:(const uint16_t)colorBGRA5551
{
	gpuEvent->FramebufferLockWrite();
	GPU->ClearWithColor(colorBGRA5551);
	gpuEvent->FramebufferUnlock();
}

- (void) respondToPauseState:(BOOL)isPaused
{
	if (isPaused)
	{
		if (!_needRestoreRender3DLock && gpuEvent->GetRender3DNeedsFinish())
		{
			gpuEvent->Render3DUnlock();
			_needRestoreRender3DLock = YES;
		}
	}
	else
	{
		if (_needRestoreRender3DLock)
		{
			gpuEvent->Render3DLock();
			_needRestoreRender3DLock = NO;
		}
	}
}

@end

GPUEventHandlerOSX::GPUEventHandlerOSX()
{
	_render3DNeedsFinish = false;
	_mutexOutputList = NULL;
	pthread_rwlock_init(&_rwlockFrame, NULL);
	pthread_mutex_init(&_mutex3DRender, NULL);
}

GPUEventHandlerOSX::~GPUEventHandlerOSX()
{
	if (this->_render3DNeedsFinish)
	{
		pthread_mutex_unlock(&this->_mutex3DRender);
	}
	
	pthread_rwlock_destroy(&this->_rwlockFrame);
	pthread_mutex_destroy(&this->_mutex3DRender);
}

void GPUEventHandlerOSX::DidFrameBegin(bool isFrameSkipRequested)
{
	this->FramebufferLockWrite();
}

void GPUEventHandlerOSX::DidFrameEnd(bool isFrameSkipped)
{
	this->FramebufferUnlock();
	
#if !defined(PORT_VERSION_OPENEMU)
	if (!isFrameSkipped)
	{
		if (this->_mutexOutputList != NULL)
		{
			pthread_mutex_lock(this->_mutexOutputList);
		}
		
		NSMutableArray *outputList = this->_cdsOutputList;
		
		for (CocoaDSOutput *cdsOutput in outputList)
		{
			if ([cdsOutput isKindOfClass:[CocoaDSDisplay class]])
			{
				[(CocoaDSDisplay *)cdsOutput doReceiveGPUFrame];
			}
		}
		
		if (this->_mutexOutputList != NULL)
		{
			pthread_mutex_unlock(this->_mutexOutputList);
		}
	}
#endif
}

void GPUEventHandlerOSX::DidRender3DBegin()
{
	this->Render3DLock();
	this->_render3DNeedsFinish = true;
}

void GPUEventHandlerOSX::DidRender3DEnd()
{
	this->_render3DNeedsFinish = false;
	this->Render3DUnlock();
}

void GPUEventHandlerOSX::FramebufferLockWrite()
{
	pthread_rwlock_wrlock(&this->_rwlockFrame);
}

void GPUEventHandlerOSX::FramebufferLockRead()
{
	pthread_rwlock_rdlock(&this->_rwlockFrame);
}

void GPUEventHandlerOSX::FramebufferUnlock()
{
	pthread_rwlock_unlock(&this->_rwlockFrame);
}

void GPUEventHandlerOSX::Render3DLock()
{
	pthread_mutex_lock(&this->_mutex3DRender);
}

void GPUEventHandlerOSX::Render3DUnlock()
{
	pthread_mutex_unlock(&this->_mutex3DRender);
}

void GPUEventHandlerOSX::FrameFinish()
{
#if !defined(PORT_VERSION_OPENEMU)
	if (this->_mutexOutputList != NULL)
	{
		pthread_mutex_lock(this->_mutexOutputList);
	}
	
	NSMutableArray *outputList = this->_cdsOutputList;
	
	for (CocoaDSOutput *cdsOutput in outputList)
	{
		if ([cdsOutput isKindOfClass:[CocoaDSDisplay class]])
		{
			[(CocoaDSDisplay *)cdsOutput finishFrame];
		}
	}
	
	if (this->_mutexOutputList != NULL)
	{
		pthread_mutex_unlock(this->_mutexOutputList);
	}
#endif
}

void GPUEventHandlerOSX::SetVideoBuffers()
{
#if !defined(PORT_VERSION_OPENEMU)
	if (this->_mutexOutputList != NULL)
	{
		pthread_mutex_lock(this->_mutexOutputList);
	}
	
	NSMutableArray *outputList = this->_cdsOutputList;
	
	for (CocoaDSOutput *cdsOutput in outputList)
	{
		if ([cdsOutput isKindOfClass:[CocoaDSDisplayVideo class]])
		{
			[(CocoaDSDisplayVideo *)cdsOutput resetVideoBuffers];
		}
	}
	
	if (this->_mutexOutputList != NULL)
	{
		pthread_mutex_unlock(this->_mutexOutputList);
	}
#endif
}

bool GPUEventHandlerOSX::GetRender3DNeedsFinish()
{
	return this->_render3DNeedsFinish;
}

pthread_rwlock_t* GPUEventHandlerOSX::GetFrameRWLock()
{
	return &this->_rwlockFrame;
}

NSMutableArray* GPUEventHandlerOSX::GetOutputList()
{
	return this->_cdsOutputList;
}

void GPUEventHandlerOSX::SetOutputList(NSMutableArray *outputList, pthread_mutex_t *theMutex)
{
	this->_cdsOutputList = outputList;
	this->_mutexOutputList = theMutex;
}

CGLContextObj OSXOpenGLRendererContext = NULL;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
CGLPBufferObj OSXOpenGLRendererPBuffer = NULL;
#pragma GCC diagnostic pop

bool OSXOpenGLRendererInit()
{
	static bool isContextAlreadyCreated = false;
	
	if (!isContextAlreadyCreated)
	{
		isContextAlreadyCreated = CreateOpenGLRenderer();
	}
	
	return true;
}

bool OSXOpenGLRendererBegin()
{
	CGLSetCurrentContext(OSXOpenGLRendererContext);
	
	return true;
}

void OSXOpenGLRendererEnd()
{
	
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

bool OSXOpenGLRendererFramebufferDidResize(size_t w, size_t h)
{
	bool result = false;
	
	// Create a PBuffer for legacy contexts since the availability of FBOs
	// is not guaranteed.
	if (OGLCreateRenderer_3_2_Func == NULL)
	{
		CGLPBufferObj newPBuffer = NULL;
		
		CGLCreatePBuffer(w, h, GL_TEXTURE_2D, GL_RGBA, 0, &newPBuffer);
		
		if (newPBuffer == NULL)
		{
			return result;
		}
		else
		{
			GLint virtualScreenID = 0;
			CGLGetVirtualScreen(OSXOpenGLRendererContext, &virtualScreenID);
			CGLSetPBuffer(OSXOpenGLRendererContext, newPBuffer, 0, 0, virtualScreenID);
		}
		
		CGLPBufferObj oldPBuffer = OSXOpenGLRendererPBuffer;
		OSXOpenGLRendererPBuffer = newPBuffer;
		CGLReleasePBuffer(oldPBuffer);
		
		result = true;
	}
	
	return result;
}

#pragma GCC diagnostic pop

bool CreateOpenGLRenderer()
{
	bool result = false;
	bool useContext_3_2 = false;
	CGLPixelFormatObj cglPixFormat = NULL;
	CGLContextObj newContext = NULL;
	GLint virtualScreenCount = 0;
	
	CGLPixelFormatAttribute attrs[] = {
		kCGLPFAColorSize, (CGLPixelFormatAttribute)24,
		kCGLPFAAlphaSize, (CGLPixelFormatAttribute)8,
		kCGLPFADepthSize, (CGLPixelFormatAttribute)24,
		kCGLPFAStencilSize, (CGLPixelFormatAttribute)8,
		kCGLPFAAccelerated,
		(CGLPixelFormatAttribute)0, (CGLPixelFormatAttribute)0,
		(CGLPixelFormatAttribute)0
	};
	
#ifdef MAC_OS_X_VERSION_10_7
	// If we can support a 3.2 Core Profile context, then request that in our
	// pixel format attributes.
	useContext_3_2 = IsOSXVersionSupported(10, 7, 0);
	if (useContext_3_2)
	{
		attrs[9] = kCGLPFAOpenGLProfile;
		attrs[10] = (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core;
	}
#endif
	
	CGLChoosePixelFormat(attrs, &cglPixFormat, &virtualScreenCount);
	if (cglPixFormat == NULL)
	{
		// Remove the HW rendering requirement and try again. Note that this will
		// result in SW rendering, which will cause a substantial speed hit.
		attrs[8] = (CGLPixelFormatAttribute)0;
		CGLChoosePixelFormat(attrs, &cglPixFormat, &virtualScreenCount);
		if (cglPixFormat == NULL)
		{
			return result;
		}
	}
	
	CGLCreateContext(cglPixFormat, NULL, &newContext);
	CGLReleasePixelFormat(cglPixFormat);
	
	RequestOpenGLRenderer_3_2(useContext_3_2);
	OSXOpenGLRendererContext = newContext;
	
	result = true;
	return result;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

void DestroyOpenGLRenderer()
{
	if (OSXOpenGLRendererContext == NULL)
	{
		return;
	}
	
	if (OGLCreateRenderer_3_2_Func == NULL)
	{
		CGLReleasePBuffer(OSXOpenGLRendererPBuffer);
		OSXOpenGLRendererPBuffer = NULL;
	}
	
	CGLReleaseContext(OSXOpenGLRendererContext);
	OSXOpenGLRendererContext = NULL;
}

#pragma GCC diagnostic pop

void RequestOpenGLRenderer_3_2(bool request_3_2)
{
#ifdef OGLRENDER_3_2_H
	if (request_3_2)
	{
		OGLLoadEntryPoints_3_2_Func = &OGLLoadEntryPoints_3_2;
		OGLCreateRenderer_3_2_Func = &OGLCreateRenderer_3_2;
		return;
	}
#endif
	OGLLoadEntryPoints_3_2_Func = NULL;
	OGLCreateRenderer_3_2_Func = NULL;
}

void SetOpenGLRendererFunctions(bool (*initFunction)(),
								bool (*beginOGLFunction)(),
								void (*endOGLFunction)(),
								bool (*resizeOGLFunction)(size_t w, size_t h))
{
	oglrender_init = initFunction;
	oglrender_beginOpenGL = beginOGLFunction;
	oglrender_endOpenGL = endOGLFunction;
	oglrender_framebufferDidResizeCallback = resizeOGLFunction;
}
