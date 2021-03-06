#include "S315_5313.h"
#include "Image/Image.pkg"
#include <thread>

//----------------------------------------------------------------------------------------
//Constructors
//----------------------------------------------------------------------------------------
S315_5313::S315_5313(const std::wstring& aimplementationName, const std::wstring& ainstanceName, unsigned int amoduleID)
:Device(aimplementationName, ainstanceName, amoduleID),
reg(registerCount, false, Data(8)),
status(10),
bstatus(10),
hcounter(9),
bhcounter(9),
vcounter(9),
bvcounter(9),
hcounterLatchedData(9),
bhcounterLatchedData(9),
vcounterLatchedData(9),
bvcounterLatchedData(9),
readBuffer(16),
breadBuffer(16),
renderVSRAMCachedRead(16),
vsramLastRenderReadCache(16),
bvsramLastRenderReadCache(16),
originalCommandAddress(16),
boriginalCommandAddress(16),
commandAddress(16),
bcommandAddress(16),
commandCode(6),
bcommandCode(6),
dmaTransferReadCache(16),
bdmaTransferReadCache(16),
dmaTransferInvalidPortWriteDataCache(16),
bdmaTransferInvalidPortWriteDataCache(16),
regSession(Data(8)),
vramSession(0),
cramSession(0),
vsramSession(0),
spriteCacheSession(0),
renderWindowActiveCache(maxCellsPerRow / cellsPerColumn),
renderMappingDataCacheLayerA(maxCellsPerRow, Data(16)),
renderMappingDataCacheLayerB(maxCellsPerRow, Data(16)),
renderMappingDataCacheSourceAddressLayerA(maxCellsPerRow, 0),
renderMappingDataCacheSourceAddressLayerB(maxCellsPerRow, 0),
renderPatternDataCacheLayerA(maxCellsPerRow, Data(32)),
renderPatternDataCacheLayerB(maxCellsPerRow, Data(32)),
renderPatternDataCacheRowNoLayerA(maxCellsPerRow, 0),
renderPatternDataCacheRowNoLayerB(maxCellsPerRow, 0),
renderSpriteDisplayCache(maxSpriteDisplayCacheSize),
renderSpriteDisplayCellCache(maxSpriteDisplayCellCacheSize)
{
	fifoBuffer.resize(fifoBufferSize);
	bfifoBuffer.resize(fifoBufferSize);

	memoryBus = 0;
	vram = 0;
	cram = 0;
	vsram = 0;
	spriteCache = 0;
	psg = 0;
	clockSourceCLK0 = 0;
	clockSourceCLK1 = 0;

	//Initialize the locked register state
	for(unsigned int i = 0; i < registerCount; ++i)
	{
		rawRegisterLocking[i] = false;
	}

	//Initialize our CE line state
	ceLineMaskLowerDataStrobeInput = 0;
	ceLineMaskUpperDataStrobeInput = 0;
	ceLineMaskReadHighWriteLowInput = 0;
	ceLineMaskAddressStrobeInput = 0;
	ceLineMaskRMWCycleInProgress = 0;
	ceLineMaskRMWCycleFirstOperation = 0;
	ceLineMaskLowerDataStrobeOutput = 0;
	ceLineMaskUpperDataStrobeOutput = 0;
	ceLineMaskReadHighWriteLowOutput = 0;
	ceLineMaskAddressStrobeOutput = 0;
	ceLineMaskLWR = 0;
	ceLineMaskUWR = 0;
	ceLineMaskCAS0 = 0;
	ceLineMaskRAS0 = 0;
	ceLineMaskOE0 = 0;

	//We need to initialize these variables here since a commit is triggered before
	//initialization the first time the system is booted.
	pendingRenderOperation = false;
	renderThreadActive = false;
	renderTimeslicePending = false;
	drawingImageBufferPlane = 0;
	lastRenderedFrameToken = 0;
	for(unsigned int bufferPlaneNo = 0; bufferPlaneNo < imageBufferPlanes; ++bufferPlaneNo)
	{
		imageBufferLineCount[bufferPlaneNo] = 0;
		for(unsigned int lineNo = 0; lineNo < imageBufferHeight; ++lineNo)
		{
			imageBufferLineWidth[bufferPlaneNo][lineNo] = 0;
		}
	}

	//Initialize the sprite pixel buffer
	for(unsigned int i = 0; i < renderSpritePixelBufferPlaneCount; ++i)
	{
		spritePixelBuffer[i].resize(spritePixelBufferSize);
	}
	renderSpritePixelBufferAnalogRenderPlane = 0;
	renderSpritePixelBufferDigitalRenderPlane = (renderSpritePixelBufferAnalogRenderPlane + 1) % renderSpritePixelBufferPlaneCount;

	busGranted = false;
	palModeLineState = false;
	resetLineState = false;
	lineStateIPL = 0;
	busRequestLineState = false;

	outputPortAccessDebugMessages = false;
	outputTimingDebugMessages = false;
	outputRenderSyncMessages = false;
	outputInterruptDebugMessages = false;
	videoDisableRenderOutput = false;
	videoEnableSpriteBoxing = false;
	videoHighlightRenderPos = false;
	videoSingleBuffering = false;
	videoFixedAspectRatio = true;
	videoShowStatusBar = true;
	videoEnableLineSmoothing = true;
	videoShowBoundaryActiveImage = false;
	videoShowBoundaryActionSafe = false;
	videoShowBoundaryTitleSafe = false;
	videoEnableFullImageBufferInfo = false;

	enableLayerAHigh = true;
	enableLayerALow = true;
	enableLayerBHigh = true;
	enableLayerBLow = true;
	enableWindowHigh = true;
	enableWindowLow = true;
	enableSpriteHigh = true;
	enableSpriteLow = true;

	logStatusRegisterRead = false;
	logDataPortRead = false;
	logHVCounterRead = false;
	logControlPortWrite = false;
	logDataPortWrite = false;
	portMonitorListSize = 2000;
	portMonitorLastModifiedToken = 0;
}

//----------------------------------------------------------------------------------------
//Interface version functions
//----------------------------------------------------------------------------------------
unsigned int S315_5313::GetIS315_5313Version() const
{
	return ThisIS315_5313Version();
}

//----------------------------------------------------------------------------------------
//Device access functions
//----------------------------------------------------------------------------------------
IDevice* S315_5313::GetDevice()
{
	return static_cast<IDevice*>(this);
}

//----------------------------------------------------------------------------------------
//Initialization functions
//----------------------------------------------------------------------------------------
bool S315_5313::BuildDevice()
{
	//Initialize the layer priority lookup table. We use this table to speed up layer
	//priority selection during rendering.
	layerPriorityLookupTable.resize(layerPriorityLookupTableSize);
	for(unsigned int i = 0; i < layerPriorityLookupTableSize; ++i)
	{
		//Determine the input layer settings for this table index value
		bool shadowHighlightEnabled = (i & (1 << 8)) != 0;
		bool spriteIsShadowOperator = (i & (1 << 7)) != 0;
		bool spriteIsHighlightOperator = (i & (1 << 6)) != 0;
		bool foundSpritePixel = (i & (1 << 5)) != 0;
		bool foundLayerAPixel = (i & (1 << 4)) != 0;
		bool foundLayerBPixel = (i & (1 << 3)) != 0;
		bool prioritySprite = (i & (1 << 2)) != 0;
		bool priorityLayerA = (i & (1 << 1)) != 0;
		bool priorityLayerB = (i & 1) != 0;

		//Resolve the layer priority for this combination of layer settings
		unsigned int layerIndex;
		bool shadow;
		bool highlight;
		CalculateLayerPriorityIndex(layerIndex, shadow, highlight, shadowHighlightEnabled, spriteIsShadowOperator, spriteIsHighlightOperator, foundSpritePixel, foundLayerAPixel, foundLayerBPixel, prioritySprite, priorityLayerA, priorityLayerB);

		//Incorporate the shadow and highlight bits into the layer index value
		layerIndex |= shadow? 1 << 3: 0;
		layerIndex |= highlight? 1 << 2: 0;

		//Write the combined value to the layer priority lookup table
		layerPriorityLookupTable[i] = layerIndex;
	}

	//Register each data source with the generic data access base class
	bool result = true;
	result &= AddGenericDataInfo((new GenericAccessDataInfo(IS315_5313DataSource::SettingsVideoSingleBuffering, IGenericAccessDataValue::DataType::Bool)));
	result &= AddGenericDataInfo((new GenericAccessDataInfo(IS315_5313DataSource::SettingsVideoFixedAspectRatio, IGenericAccessDataValue::DataType::Bool)));
	result &= AddGenericDataInfo((new GenericAccessDataInfo(IS315_5313DataSource::SettingsVideoShowStatusBar, IGenericAccessDataValue::DataType::Bool)));
	result &= AddGenericDataInfo((new GenericAccessDataInfo(IS315_5313DataSource::SettingsVideoEnableLineSmoothing, IGenericAccessDataValue::DataType::Bool)));
	result &= AddGenericDataInfo((new GenericAccessDataInfo(IS315_5313DataSource::SettingsVideoDisableRenderOutput, IGenericAccessDataValue::DataType::Bool)));
	result &= AddGenericDataInfo((new GenericAccessDataInfo(IS315_5313DataSource::SettingsVideoHighlightRenderPos, IGenericAccessDataValue::DataType::Bool)));
	result &= AddGenericDataInfo((new GenericAccessDataInfo(IS315_5313DataSource::SettingsVideoEnableSpriteBoxing, IGenericAccessDataValue::DataType::Bool)));
	result &= AddGenericDataInfo((new GenericAccessDataInfo(IS315_5313DataSource::SettingsVideoShowBoundaryActiveImage, IGenericAccessDataValue::DataType::Bool)));
	result &= AddGenericDataInfo((new GenericAccessDataInfo(IS315_5313DataSource::SettingsVideoShowBoundaryActionSafe, IGenericAccessDataValue::DataType::Bool)));
	result &= AddGenericDataInfo((new GenericAccessDataInfo(IS315_5313DataSource::SettingsVideoShowBoundaryTitleSafe, IGenericAccessDataValue::DataType::Bool)));
	result &= AddGenericDataInfo((new GenericAccessDataInfo(IS315_5313DataSource::SettingsVideoEnableFullImageBufferInfo, IGenericAccessDataValue::DataType::Bool)));
	result &= AddGenericDataInfo((new GenericAccessDataInfo(IS315_5313DataSource::SettingsOutputPortAccessDebugMessages, IGenericAccessDataValue::DataType::Bool)));
	result &= AddGenericDataInfo((new GenericAccessDataInfo(IS315_5313DataSource::SettingsOutputTimingDebugMessages, IGenericAccessDataValue::DataType::Bool)));
	result &= AddGenericDataInfo((new GenericAccessDataInfo(IS315_5313DataSource::SettingsOutputRenderSyncDebugMessages, IGenericAccessDataValue::DataType::Bool)));
	result &= AddGenericDataInfo((new GenericAccessDataInfo(IS315_5313DataSource::SettingsOutputInterruptDebugMessages, IGenericAccessDataValue::DataType::Bool)));
	result &= AddGenericDataInfo((new GenericAccessDataInfo(IS315_5313DataSource::SettingsVideoEnableLayerA, IGenericAccessDataValue::DataType::Bool)));
	result &= AddGenericDataInfo((new GenericAccessDataInfo(IS315_5313DataSource::SettingsVideoEnableLayerAHigh, IGenericAccessDataValue::DataType::Bool)));
	result &= AddGenericDataInfo((new GenericAccessDataInfo(IS315_5313DataSource::SettingsVideoEnableLayerALow, IGenericAccessDataValue::DataType::Bool)));
	result &= AddGenericDataInfo((new GenericAccessDataInfo(IS315_5313DataSource::SettingsVideoEnableLayerB, IGenericAccessDataValue::DataType::Bool)));
	result &= AddGenericDataInfo((new GenericAccessDataInfo(IS315_5313DataSource::SettingsVideoEnableLayerBHigh, IGenericAccessDataValue::DataType::Bool)));
	result &= AddGenericDataInfo((new GenericAccessDataInfo(IS315_5313DataSource::SettingsVideoEnableLayerBLow, IGenericAccessDataValue::DataType::Bool)));
	result &= AddGenericDataInfo((new GenericAccessDataInfo(IS315_5313DataSource::SettingsVideoEnableWindow, IGenericAccessDataValue::DataType::Bool)));
	result &= AddGenericDataInfo((new GenericAccessDataInfo(IS315_5313DataSource::SettingsVideoEnableWindowHigh, IGenericAccessDataValue::DataType::Bool)));
	result &= AddGenericDataInfo((new GenericAccessDataInfo(IS315_5313DataSource::SettingsVideoEnableWindowLow, IGenericAccessDataValue::DataType::Bool)));
	result &= AddGenericDataInfo((new GenericAccessDataInfo(IS315_5313DataSource::SettingsVideoEnableSprite, IGenericAccessDataValue::DataType::Bool)));
	result &= AddGenericDataInfo((new GenericAccessDataInfo(IS315_5313DataSource::SettingsVideoEnableSpriteHigh, IGenericAccessDataValue::DataType::Bool)));
	result &= AddGenericDataInfo((new GenericAccessDataInfo(IS315_5313DataSource::SettingsVideoEnableSpriteLow, IGenericAccessDataValue::DataType::Bool)));

	//Register page layouts for generic access to this device
	GenericAccessPage* systemSettingsPage = new GenericAccessPage(L"SystemSettings", L"System Settings", IGenericAccessPage::Type::Settings);
	systemSettingsPage->AddEntry(new GenericAccessGroupDataEntry(IS315_5313DataSource::SettingsVideoSingleBuffering, L"Single Buffering"))
	                  ->AddEntry(new GenericAccessGroupDataEntry(IS315_5313DataSource::SettingsVideoFixedAspectRatio, L"Fixed Aspect Ratio"))
	                  ->AddEntry(new GenericAccessGroupDataEntry(IS315_5313DataSource::SettingsVideoShowStatusBar, L"Show Status Bar"))
	                  ->AddEntry(new GenericAccessGroupDataEntry(IS315_5313DataSource::SettingsVideoEnableLineSmoothing, L"Enable Line Smoothing"));
	result &= AddGenericAccessPage(systemSettingsPage);
	GenericAccessPage* debugSettingsPage = new GenericAccessPage(L"DebugSettings", L"Debug Settings");
	debugSettingsPage->AddEntry((new GenericAccessGroup(L"Image Debug"))
	                     ->AddEntry(new GenericAccessGroupDataEntry(IS315_5313DataSource::SettingsVideoDisableRenderOutput, L"Disable Rendering"))
	                     ->AddEntry(new GenericAccessGroupDataEntry(IS315_5313DataSource::SettingsVideoHighlightRenderPos, L"Highlight Render Pos"))
	                     ->AddEntry(new GenericAccessGroupDataEntry(IS315_5313DataSource::SettingsVideoEnableSpriteBoxing, L"Sprite Boxing"))
	                     ->AddEntry(new GenericAccessGroupDataEntry(IS315_5313DataSource::SettingsVideoEnableFullImageBufferInfo, L"Show Pixel Info")))
	                 ->AddEntry((new GenericAccessGroup(L"Image Boundaries"))
	                     ->AddEntry(new GenericAccessGroupDataEntry(IS315_5313DataSource::SettingsVideoShowBoundaryActiveImage, L"Active Image"))
	                     ->AddEntry(new GenericAccessGroupDataEntry(IS315_5313DataSource::SettingsVideoShowBoundaryActionSafe, L"Action Safe"))
	                     ->AddEntry(new GenericAccessGroupDataEntry(IS315_5313DataSource::SettingsVideoShowBoundaryTitleSafe, L"Title Safe")))
	                 ->AddEntry((new GenericAccessGroup(L"VDP Core Debugging"))
	                     ->AddEntry(new GenericAccessGroupDataEntry(IS315_5313DataSource::SettingsOutputPortAccessDebugMessages, L"Port Access Debug"))
	                     ->AddEntry(new GenericAccessGroupDataEntry(IS315_5313DataSource::SettingsOutputTimingDebugMessages, L"Timing Debug"))
	                     ->AddEntry(new GenericAccessGroupDataEntry(IS315_5313DataSource::SettingsOutputRenderSyncDebugMessages, L"Render Sync Debug"))
	                     ->AddEntry(new GenericAccessGroupDataEntry(IS315_5313DataSource::SettingsOutputInterruptDebugMessages, L"Interrupt Debug")));
	result &= AddGenericAccessPage(debugSettingsPage);
	GenericAccessPage* layerRemovalPage = new GenericAccessPage(L"LayerVisibility", L"Layer Visibility");
	layerRemovalPage->AddEntry((new GenericAccessGroup(L"Layer A"))
	                    ->AddEntry(new GenericAccessGroupDataEntry(IS315_5313DataSource::SettingsVideoEnableLayerA, L"Both Enabled"))
	                    ->AddEntry(new GenericAccessGroupDataEntry(IS315_5313DataSource::SettingsVideoEnableLayerAHigh, L"High Priority"))
	                    ->AddEntry(new GenericAccessGroupDataEntry(IS315_5313DataSource::SettingsVideoEnableLayerALow, L"Low Priority")))
	                ->AddEntry((new GenericAccessGroup(L"Layer B"))
	                    ->AddEntry(new GenericAccessGroupDataEntry(IS315_5313DataSource::SettingsVideoEnableLayerB, L"Both Enabled"))
	                    ->AddEntry(new GenericAccessGroupDataEntry(IS315_5313DataSource::SettingsVideoEnableLayerBHigh, L"High Priority"))
	                    ->AddEntry(new GenericAccessGroupDataEntry(IS315_5313DataSource::SettingsVideoEnableLayerBLow, L"Low Priority")))
	                ->AddEntry((new GenericAccessGroup(L"Window"))
	                    ->AddEntry(new GenericAccessGroupDataEntry(IS315_5313DataSource::SettingsVideoEnableWindow, L"Both Enabled"))
	                    ->AddEntry(new GenericAccessGroupDataEntry(IS315_5313DataSource::SettingsVideoEnableWindowHigh, L"High Priority"))
	                    ->AddEntry(new GenericAccessGroupDataEntry(IS315_5313DataSource::SettingsVideoEnableWindowLow, L"Low Priority")))
	                ->AddEntry((new GenericAccessGroup(L"Sprite"))
	                    ->AddEntry(new GenericAccessGroupDataEntry(IS315_5313DataSource::SettingsVideoEnableSprite, L"Both Enabled"))
	                    ->AddEntry(new GenericAccessGroupDataEntry(IS315_5313DataSource::SettingsVideoEnableSpriteHigh, L"High Priority"))
	                    ->AddEntry(new GenericAccessGroupDataEntry(IS315_5313DataSource::SettingsVideoEnableSpriteLow, L"Low Priority")));
	result &= AddGenericAccessPage(layerRemovalPage);

	return result;
}

//----------------------------------------------------------------------------------------
bool S315_5313::ValidateDevice()
{
	return (memoryBus != 0) && (vram != 0) && (cram != 0) && (vsram != 0) && (spriteCache != 0);
}

//----------------------------------------------------------------------------------------
void S315_5313::Initialize()
{
	//Initialize our register buffers
	reg.Initialize();

	//Initialize the default external clock divider settings
	//##TODO## Make the clock dividers configurable through the VDP debugger
	static const unsigned int initialClockDividerCLK0 = 15;
	static const unsigned int initialClockDividerCLK1 = 7;
	if(clockSourceCLK0 != 0)
	{
		clockSourceCLK0->TransparentSetClockDivider((double)initialClockDividerCLK0);
	}
	if(clockSourceCLK1 != 0)
	{
		clockSourceCLK1->TransparentSetClockDivider((double)initialClockDividerCLK1);
	}

	//Update state
	currentTimesliceLength = 0;
	lastTimesliceMclkCyclesRemainingTime = 0;
	currentTimesliceMclkCyclesRemainingTime = 0;
	lastTimesliceMclkCyclesOverrun = 0;
	stateLastUpdateTime = 0;
	stateLastUpdateMclk = 0;
	stateLastUpdateMclkUnused = 0;
	stateLastUpdateMclkUnusedFromLastTimeslice = 0;

	//Line state change state
	externalInterruptVideoTriggerPointPending = false;
	lineStateChangePendingVINT = false;
	lineStateChangePendingHINT = false;
	lineStateChangePendingEXINT = false;
	lineStateChangePendingINTAsserted = false;
	lineStateChangePendingINTNegated = false;

	//Command port data
	//##TODO## Initialize the actual data in the FIFO buffer. We can determine the default
	//values for many bits in the FIFO on power-up by immediately setting up a read target
	//from VSRAM and CRAM, and stepping through each entry in the FIFO, saving the values
	//we read out of the initial bits. Most likely, the FIFO is initialized to 0, but we
	//should perform a test to be certain.
	fifoNextReadEntry = 0;
	fifoNextWriteEntry = 0;
	codeAndAddressRegistersModifiedSinceLastWrite = true;
	commandWritePending = false;
	originalCommandAddress = 0;
	commandAddress = 0;
	commandCode = 0;
	for(unsigned int i = 0; i < fifoBufferSize; ++i)
	{
		fifoBuffer[i].codeRegData = 0;
		fifoBuffer[i].addressRegData = 0;
		fifoBuffer[i].dataPortWriteData = 0;
		fifoBuffer[i].dataWriteHalfWritten = false;
		fifoBuffer[i].pendingDataWrite = false;
	}
	renderVSRAMCachedRead = 0;
	readDataAvailable = false;
	readDataHalfCached = false;
	dmaFillOperationRunning = false;

	//DMA state
	workerThreadPaused = false;
	dmaTransferActive = false;
	dmaTransferInvalidPortWriteCached = false;
	dmaAdvanceUntilDMAComplete = false;

	//External line state
	busGranted = false;
	palModeLineState = false;
	resetLineState = false;
	lineStateIPL = 0;
	busRequestLineState = false;

	//Status register
	status = 0;

	//HV counter
	//##FIX## We know for a fact that real VDP powers on with what is essentially a random
	//hcounter and vcounter value. Some Mega Drive games are known to rely on this
	//behaviour, as they use the hcounter as a random seed. Examples given include "X-Men
	//2 Clone Wars" for character assignment, and Eternal Champions and Bonkers for the
	//Sega logo.
	//##NOTE## Subsequent hardware tests have shown this is actually not the case. We were
	//never able to identify a single case, after repeated testing, in which the system
	//could be made to appear to have started with a random HV counter location, when
	//testing from the raw tototek flashcart on a non-TMSS Mega Drive. We tested with
	//Bonkers, and in all cases, we got a Sega screen which showed explosions with the
	//Sega logo then flashing into existence. When testing on a TMSS system, we always saw
	//the Sega logo appear surrounded by flashing stars. Neither of these match the
	//current behaviour of our emulator. We therefore have to perform tests to determine
	//the exact value our HV counter should be initialized to on a cold boot. This is
	//complicated by the fact that on a cold boot, there may be some hidden initialization
	//time or analog system artifacts which affect the exact time at which the VDP and
	//M68000 start executing relative to each other. We need to test until we get the
	//correct behaviour.
	//##NOTE## After hardware tests, here are the values we read for our initial HV
	//counter values: (first read of 0x8400 in mode 4)
	//0x8430, 0x8437, 0x843F, 0x8446, 0x844D, 0x8455, 0x845C, 0x8463, 0x846B, 0x8472,
	//0x8479, 0x8481, 0x8588, ...
	//##NOTE## Hardware tests using a logic analyzer have shown the VDP always starts from
	//the beginning of HSYNC and VSYNC on a cold boot. The M68000 however powers up after
	//the VDP. By the time the M68000 stars using the external bus, the VDP has already
	//rendered over half of the first frame. This fits with our hardware tests, which show
	//the first value read by the M68000 from the VDP VCounter is 0x84. Emulating this
	//accurately is going to require us to introduce a power-on initialization delay into
	//the M68000 execution sequence, which is returned in the execution time for the first
	//execution step of the processor. The execution delay between the VDP and the M68000
	//is measured to be approximately 13 milliseconds with a logic analyzer.
	const HScanSettings& hscanSettings = GetHScanSettings(false, false);
	const VScanSettings& vscanSettings = GetVScanSettings(false, false, false);
	hcounter = hscanSettings.hsyncAsserted;
	vcounter = vscanSettings.vsyncAssertedPoint;

	//Pending interrupt state
	hintCounter = 0;
	vintPending = false;
	hintPending = false;
	exintPending = false;

	//Active register settings
	interlaceEnabled = false;
	interlaceDouble = false;
	screenModeRS0 = false;
	screenModeRS1 = false;
	screenModeV30 = false;
	palMode = false;

	//Cached register settings
	hvCounterLatchEnabled = false;
	vintEnabled = false;
	hintEnabled = false;
	exintEnabled = false;
	hintCounterReloadValue = 0;
	dmaEnabled = false;
	dmd0 = false;
	dmd1 = false;
	dmaLengthCounter = 0;
	dmaSourceAddressByte1 = 0;
	dmaSourceAddressByte2 = 0;
	dmaSourceAddressByte3 = 0;
	autoIncrementData = 0;
	interlaceEnabledCached = false;
	interlaceDoubleCached = false;
	screenModeRS0Cached = false;
	screenModeRS1Cached = false;
	screenModeV30Cached = false;
	screenModeM5Cached = false;
	displayEnabledCached = false;
	spriteAttributeTableBaseAddressDecoded = 0;
	verticalScrollModeCached = false;
	cachedDMASettingsChanged = false;

	//Digital render data buffers
	renderDigitalHCounterPos = hcounter.GetData();
	renderDigitalVCounterPos = vcounter.GetData();
	renderDigitalVCounterPosPreviousLine = (vcounter.GetData() - 1) & vcounter.GetBitMask();
	renderDigitalRemainingMclkCycles = 0;
	renderDigitalScreenModeRS0Active = false;
	renderDigitalScreenModeRS1Active = false;
	renderDigitalScreenModeV30Active = false;
	renderDigitalInterlaceEnabledActive = false;
	renderDigitalInterlaceDoubleActive = false;
	renderDigitalPalModeActive = false;
	renderDigitalOddFlagSet = false;
	renderDigitalMclkCycleProgress = 0;
	renderLayerAHscrollPatternDisplacement = 0;
	renderLayerBHscrollPatternDisplacement = 0;
	renderLayerAHscrollMappingDisplacement = 0;
	renderLayerBHscrollMappingDisplacement = 0;
	renderLayerAVscrollPatternDisplacement = 0;
	renderLayerBVscrollPatternDisplacement = 0;
	renderLayerAVscrollMappingDisplacement = 0;
	renderLayerBVscrollMappingDisplacement = 0;
	currentRenderPosOnScreen = false;

	//Additional render buffers
	for(unsigned int i = 0; i < maxSpriteDisplayCellCacheSize; ++i)
	{
		renderSpriteDisplayCellCache[i].patternCellOffsetX = 0;
		renderSpriteDisplayCellCache[i].patternCellOffsetY = 0;
		renderSpriteDisplayCellCache[i].patternData = 0;
		renderSpriteDisplayCellCache[i].patternRowOffset = 0;
		renderSpriteDisplayCellCache[i].spriteCellColumnNo = 0;
		renderSpriteDisplayCellCache[i].spriteDisplayCacheIndex = 0;
		renderSpriteDisplayCellCache[i].spriteTableEntryAddress = 0;
	}
	for(unsigned int i = 0; i < maxSpriteDisplayCacheSize; ++i)
	{
		renderSpriteDisplayCache[i].spriteTableIndex = 0;
		renderSpriteDisplayCache[i].spriteRowIndex = 0;
		renderSpriteDisplayCache[i].vpos = 0;
		renderSpriteDisplayCache[i].sizeAndLinkData = 0;
		renderSpriteDisplayCache[i].mappingData= 0;
		renderSpriteDisplayCache[i].hpos = 0;
	}

	//Read-modify-write cycle saved output CE line state settings
	lineLWRSavedStateRMW = false;
	lineUWRSavedStateRMW = false;
	lineCAS0SavedStateRMW = false;
	lineRAS0SavedStateRMW = false;
	lineOE0SavedStateRMW = false;
}

//----------------------------------------------------------------------------------------
void S315_5313::Reset(double accessTime)
{
	//After numerous tests performed on the actual system, it appears that all VDP
	//registers are initialized to 0 on a reset. This is supported by Addendum 4 from
	//SEGA in the Genesis Software Manual. Note that since Mode 4 graphics are enabled
	//when bit 2 of reg 1 is 0, the VDP starts in Mode 4. This has been confirmed on the
	//real hardware.
	//##TODO## Perform hardware tests to see if other control port related settings are
	//cleared on a reset. In particular, we need to determine if the pending command port
	//write state is cleared on a reset.
	AccessTarget accessTarget;
	if(reg.DoesLatestTimesliceExist())
	{
		unsigned int accessMclkCycle = ConvertAccessTimeToMclkCount(accessTime + lastTimesliceMclkCyclesRemainingTime);
		accessTarget.AccessTime(accessMclkCycle);
	}
	else
	{
		accessTarget.AccessLatest();
	}
	for(unsigned int i = 0; i < registerCount; ++i)
	{
		Data data(8, 0);
		SetRegisterData(i, accessTarget, data);
		TransparentRegisterSpecialUpdateFunction(i, data);
	}
}

//----------------------------------------------------------------------------------------
void S315_5313::BeginExecution()
{
	//Initialize the render worker thread state
	pendingRenderOperationCount = 0;
	renderThreadLagging = false;
	timesliceRenderInfoList.clear();
	regTimesliceList.clear();
	vramTimesliceList.clear();
	cramTimesliceList.clear();
	vsramTimesliceList.clear();
	spriteCacheTimesliceList.clear();

	//Start the render worker thread
	renderThreadActive = true;
	std::thread renderThread(std::bind(std::mem_fn(&S315_5313::RenderThread), this));
	renderThread.detach();

	//Start the DMA worker thread
	workerThreadActive = true;
	std::thread workerThread(std::bind(std::mem_fn(&S315_5313::DMAWorkerThread), this));
	workerThread.detach();
}

//----------------------------------------------------------------------------------------
void S315_5313::SuspendExecution()
{
	//Suspend the render worker thread
	std::unique_lock<std::mutex> renderLock(renderThreadMutex);
	if(renderThreadActive)
	{
		renderThreadActive = false;
		renderThreadUpdate.notify_all();
		renderThreadStopped.wait(renderLock);
	}

	//Suspend the DMA worker thread
	std::unique_lock<std::mutex> workerLock(workerThreadMutex);
	if(workerThreadActive)
	{
		workerThreadActive = false;
		workerThreadUpdate.notify_all();
		workerThreadStopped.wait(workerLock);
	}
}

//----------------------------------------------------------------------------------------
//Reference functions
//----------------------------------------------------------------------------------------
bool S315_5313::AddReference(const MarshalSupport::Marshal::In<std::wstring>& referenceName, IDevice* target)
{
	bool result = true;
	externalReferenceLock.ObtainWriteLock();
	if(referenceName == L"VRAM")
	{
		ITimedBufferIntDevice* device = dynamic_cast<ITimedBufferIntDevice*>(target);
		if(device != 0)
		{
			vram = device->GetTimedBuffer();
		}
	}
	else if(referenceName == L"CRAM")
	{
		ITimedBufferIntDevice* device = dynamic_cast<ITimedBufferIntDevice*>(target);
		if(device != 0)
		{
			cram = device->GetTimedBuffer();
		}
	}
	else if(referenceName == L"VSRAM")
	{
		ITimedBufferIntDevice* device = dynamic_cast<ITimedBufferIntDevice*>(target);
		if(device != 0)
		{
			vsram = device->GetTimedBuffer();
		}
	}
	else if(referenceName == L"SpriteCache")
	{
		ITimedBufferIntDevice* device = dynamic_cast<ITimedBufferIntDevice*>(target);
		if(device != 0)
		{
			spriteCache = device->GetTimedBuffer();
		}
	}
	else if(referenceName == L"PSG")
	{
		psg = target;
	}
	else
	{
		result = false;
	}
	externalReferenceLock.ReleaseWriteLock();
	return result;
}

//----------------------------------------------------------------------------------------
bool S315_5313::AddReference(const MarshalSupport::Marshal::In<std::wstring>& referenceName, IBusInterface* target)
{
	bool result = true;
	externalReferenceLock.ObtainWriteLock();
	if(referenceName == L"BusInterface")
	{
		memoryBus = target;
	}
	else
	{
		result = false;
	}
	externalReferenceLock.ReleaseWriteLock();
	return result;
}

//----------------------------------------------------------------------------------------
bool S315_5313::AddReference(const MarshalSupport::Marshal::In<std::wstring>& referenceName, IClockSource* target)
{
	bool result = false;
	externalReferenceLock.ObtainWriteLock();
	if(referenceName == L"CLK0")
	{
		if(target->GetClockType() == IClockSource::ClockType::Divider)
		{
			clockSourceCLK0 = target;
			result = true;
		}
	}
	else if(referenceName == L"CLK1")
	{
		if(target->GetClockType() == IClockSource::ClockType::Divider)
		{
			clockSourceCLK1 = target;
			result = true;
		}
	}
	externalReferenceLock.ReleaseWriteLock();
	return result;
}

//----------------------------------------------------------------------------------------
void S315_5313::RemoveReference(IDevice* target)
{
	externalReferenceLock.ObtainWriteLock();
	ITimedBufferIntDevice* targetAsTimedBufferDevice = dynamic_cast<ITimedBufferIntDevice*>(target);
	if(targetAsTimedBufferDevice != 0)
	{
		ITimedBufferInt* timedBuffer = targetAsTimedBufferDevice->GetTimedBuffer();
		if(vram == timedBuffer)
		{
			vram = 0;
		}
		else if(cram == timedBuffer)
		{
			cram = 0;
		}
		else if(vsram == timedBuffer)
		{
			vsram = 0;
		}
		else if(spriteCache == timedBuffer)
		{
			spriteCache = 0;
		}
	}
	else if(psg == target)
	{
		psg = 0;
	}
	externalReferenceLock.ReleaseWriteLock();
}

//----------------------------------------------------------------------------------------
void S315_5313::RemoveReference(IBusInterface* target)
{
	externalReferenceLock.ObtainWriteLock();
	if(memoryBus == target)
	{
		memoryBus = 0;
	}
	externalReferenceLock.ReleaseWriteLock();
}

//----------------------------------------------------------------------------------------
void S315_5313::RemoveReference(IClockSource* target)
{
	externalReferenceLock.ObtainWriteLock();
	if(clockSourceCLK0 == target)
	{
		clockSourceCLK0 = 0;
	}
	else if(clockSourceCLK1 == target)
	{
		clockSourceCLK1 = 0;
	}
	externalReferenceLock.ReleaseWriteLock();
}

//----------------------------------------------------------------------------------------
//Suspend functions
//----------------------------------------------------------------------------------------
bool S315_5313::UsesTransientExecution() const
{
	return true;
}

//----------------------------------------------------------------------------------------
//Execute functions
//----------------------------------------------------------------------------------------
S315_5313::UpdateMethod S315_5313::GetUpdateMethod() const
{
	return UpdateMethod::Timeslice;
}

//----------------------------------------------------------------------------------------
bool S315_5313::SendNotifyUpcomingTimeslice() const
{
	return true;
}

//----------------------------------------------------------------------------------------
void S315_5313::NotifyUpcomingTimeslice(double nanoseconds)
{
	//Adjust the times for any pending IPL line state changes to take into account the new
	//timeslice
	lineStateChangeVINTTime -= currentTimesliceLength;
	lineStateChangeHINTTime -= currentTimesliceLength;
	lineStateChangeEXINTTime -= currentTimesliceLength;
	lineStateChangeINTAssertedTime -= currentTimesliceLength;
	lineStateChangeINTNegatedTime -= currentTimesliceLength;

	currentTimesliceLength = nanoseconds;

	lastAccessTime = 0;

	lastTimesliceMclkCyclesRemainingTime = currentTimesliceMclkCyclesRemainingTime;
	currentTimesliceTotalMclkCycles = ConvertAccessTimeToMclkCount(currentTimesliceLength + lastTimesliceMclkCyclesRemainingTime);
	double mclkCyclesToAddInAccessTime = ConvertMclkCountToAccessTime(currentTimesliceTotalMclkCycles);
	currentTimesliceMclkCyclesRemainingTime = (currentTimesliceLength + lastTimesliceMclkCyclesRemainingTime) - mclkCyclesToAddInAccessTime;

	//Round off error adjustment code in ConvertAccessTimeToMclkCount can result in
	//currentTimesliceTotalMclkCycles being rounded up. In this case, our
	//currentTimesliceMclkCyclesRemainingTime variable would end up with a negative
	//result, the presence of which would bias us towards over-estimating the number of
	//mclk cycles for the next timeslice too, resulting in a compounding error, with the
	//numbers getting further and further off with each successive timeslice. We
	//compensate for the negative error here when it occurs, by removing one mclk cycle to
	//force a positive result.
	while(currentTimesliceMclkCyclesRemainingTime < 0)
	{
		//##DEBUG##
//		std::wcout << "######################################################\n";
//		std::wcout << "VDP NotifyUpcomingTimeslice resulted in negative mclkCyclesRemainingTime!\n";
//		std::wcout << "nanoseconds:\t" << nanoseconds << "\n";
//		std::wcout << "currentTimesliceTotalMclkCycles:\t" << currentTimesliceTotalMclkCycles << "\n";
//		std::wcout << "mclkCyclesToAddInAccessTime:\t" << mclkCyclesToAddInAccessTime << "\n";
//		std::wcout << "currentTimesliceMclkCyclesRemainingTime:\t" << currentTimesliceMclkCyclesRemainingTime << "\n";
//		std::wcout << "lastTimesliceMclkCyclesRemainingTime:\t" << lastTimesliceMclkCyclesRemainingTime << "\n";
//		std::wcout << "######################################################\n";
		if(currentTimesliceTotalMclkCycles > 0)
		{
			currentTimesliceTotalMclkCycles -= 1;
			mclkCyclesToAddInAccessTime = ConvertMclkCountToAccessTime(currentTimesliceTotalMclkCycles);
		}
		currentTimesliceMclkCyclesRemainingTime = (currentTimesliceLength + lastTimesliceMclkCyclesRemainingTime) - mclkCyclesToAddInAccessTime;
	}

	//##DEBUG##
	if(outputTimingDebugMessages)
	{
		std::wcout << "VDPNotifyUpcomingTimeslice:\t" << currentTimesliceLength << '\t' << currentTimesliceTotalMclkCycles << '\t' << mclkCyclesToAddInAccessTime << '\t' << currentTimesliceMclkCyclesRemainingTime << '\n';
	}

	//Flag a new render operation to start at the next commit
	pendingRenderOperation = true;

	//Add the new timeslice to all our timed buffers
	reg.AddTimeslice(currentTimesliceTotalMclkCycles);
	vram->AddTimeslice(currentTimesliceTotalMclkCycles);
	cram->AddTimeslice(currentTimesliceTotalMclkCycles);
	vsram->AddTimeslice(currentTimesliceTotalMclkCycles);
	spriteCache->AddTimeslice(currentTimesliceTotalMclkCycles);

	//Add references to the new timeslice entry from our timed buffers to the uncommitted
	//timeslice lists for the buffers
	regTimesliceListUncommitted.push_back(reg.GetLatestTimeslice());
	vramTimesliceListUncommitted.push_back(vram->GetLatestTimesliceReference());
	cramTimesliceListUncommitted.push_back(cram->GetLatestTimesliceReference());
	vsramTimesliceListUncommitted.push_back(vsram->GetLatestTimesliceReference());
	spriteCacheTimesliceListUncommitted.push_back(spriteCache->GetLatestTimesliceReference());
	timesliceRenderInfoListUncommitted.push_back(TimesliceRenderInfo(lastTimesliceMclkCyclesOverrun));
}

//----------------------------------------------------------------------------------------
bool S315_5313::SendNotifyBeforeExecuteCalled() const
{
	return true;
}

//----------------------------------------------------------------------------------------
void S315_5313::NotifyBeforeExecuteCalled()
{
	//##DEBUG##
	if(outputTimingDebugMessages)
	{
		std::wcout << "VDPNotifyBeforeExecuteCalled: " << hcounter.GetData() << '\t' << vcounter.GetData() << '\t' << stateLastUpdateMclk << '\t' << stateLastUpdateMclkUnused << '\t' << currentTimesliceTotalMclkCycles << '\n';
	}

	//If the DMA worker thread is currently active but paused, resume it here.
	std::unique_lock<std::mutex> lock(workerThreadMutex);
	if(workerThreadActive && workerThreadPaused)
	{
		//##DEBUG##
//		std::wcout << L"NotifyBeforeExecuteCalled is resuming DMA worker thread\n";

		workerThreadUpdate.notify_all();
	}
}

//----------------------------------------------------------------------------------------
bool S315_5313::SendNotifyAfterExecuteCalled() const
{
	return true;
}

//----------------------------------------------------------------------------------------
void S315_5313::NotifyAfterExecuteCalled()
{
	//Ensure that the DMA worker thread has finished executing
	std::unique_lock<std::mutex> workerThreadLock(workerThreadMutex);
	if(workerThreadActive && !workerThreadPaused && busGranted)
	{
		//##DEBUG##
//		std::wcout << L"NotifyAfterExecuteCalled is waiting for DMA worker thread to pause\n";
//		std::wcout << '\t' << workerThreadActive << '\t' << workerThreadPaused << '\t' << busGranted << '\n';

		workerThreadIdle.wait(workerThreadLock);
	}

	//Explicitly release our lock on workerThreadMutex here, since we no longer require
	//synchronization with the worker thread, and the SetLineState method can require
	//workerThreadMutex to be available.
	workerThreadLock.unlock();

	//##DEBUG##
	if(outputTimingDebugMessages)
	{
		std::wcout << "VDP - NotifyAfterExecuteCalled(Before): " << hcounter.GetData() << '\t' << vcounter.GetData() << '\t' << currentTimesliceTotalMclkCycles << '\t' << stateLastUpdateMclk << '\t' << stateLastUpdateMclkUnused << '\t' << stateLastUpdateMclkUnusedFromLastTimeslice << '\t' << std::setprecision(16) << stateLastUpdateTime << '\n';
	}

	//Ensure the VDP is advanced up to the end of its timeslice
	if((currentTimesliceTotalMclkCycles > 0) && (currentTimesliceTotalMclkCycles > GetProcessorStateMclkCurrent()))
	{
		UpdateInternalState(currentTimesliceTotalMclkCycles, false, false, false, false, false, false, false);
	}

	//If a DMA transfer is in progress, calculate the number of mclk cycles which have
	//already been consumed out of the delay time before the next external bus read
	//operation, and reset the next read time to 0. This will allow the result of the DMA
	//read operation to be processed at the correct mclk cycle in the next timeslice.
	if(dmaTransferActive)
	{
		//Calculate the number of mclk wait cycles that have already been served out of
		//the delay before the next read operation is complete.
		unsigned int dmaTransferNextReadCompleteTime = (dmaTransferNextReadMclk + (dmaTransferReadTimeInMclkCycles - dmaTransferLastTimesliceUsedReadDelay));
		if(dmaTransferNextReadCompleteTime > GetProcessorStateMclkCurrent())
		{
			//Calculate the number of mclk cycles that have already been advanced from the
			//point at which the DMA read operation began, to the point we're up to now.
			//Note that this is safe, as dmaTransferNextReadMclk is never greater than the
			//current state time. Also note that we add the calculated value together with
			//any existing value in this variable, so that extremely small timeslices that
			//don't consume all the used read delay from the last timeslice will roll any
			//remaining used delay into the next timeslice.
			dmaTransferLastTimesliceUsedReadDelay += GetProcessorStateMclkCurrent() - dmaTransferNextReadMclk;
		}
		else
		{
			//Since we've already advanced up to or past the point at which the next DMA
			//read operation would have completed, record that the entire read delay time
			//has been consumed.
			dmaTransferLastTimesliceUsedReadDelay = dmaTransferReadTimeInMclkCycles;
		}

		//Reset the next DMA transfer read time to 0. The used read delay, calculated
		//above, will be used to preserve the relative displacement of this value from the
		//current processor state time into the next timeslice.
		dmaTransferNextReadMclk = 0;
	}

	//Record the final position we advanced to in this timeslice. Note that this may be
	//past the end of the timeslice itself, as other devices may access the VDP before or
	//at the end of the timeslice, but may be stalled until a later point in time after
	//the end of the timeslice. This is compensated for by beginning the next timeslice
	//with an offset into that timeslice period.
	timesliceRenderInfoListUncommitted.rbegin()->timesliceEndPosition = GetProcessorStateMclkCurrent();

	//Calculate the number of mclk cycles we ran over the end of the last timeslice
	lastTimesliceMclkCyclesOverrun = 0;
	if(GetProcessorStateMclkCurrent() > currentTimesliceTotalMclkCycles)
	{
		lastTimesliceMclkCyclesOverrun = GetProcessorStateMclkCurrent() - currentTimesliceTotalMclkCycles;
	}

	//##DEBUG##
	if(outputTimingDebugMessages)
	{
		std::wcout << "VDP - NotifyAfterExecuteCalled(After): " << hcounter.GetData() << '\t' << vcounter.GetData() << '\t' << currentTimesliceTotalMclkCycles << '\t' << stateLastUpdateMclk << '\t' << stateLastUpdateMclkUnused << '\t' << stateLastUpdateMclkUnusedFromLastTimeslice << '\t' << std::setprecision(16) << stateLastUpdateTime << '\t' << lastTimesliceMclkCyclesOverrun << '\n';
	}

	//##DEBUG##
	if(lastTimesliceMclkCyclesOverrun > 0x80000000)
	{
		std::wcout << "VDP - Bad value for lastTimesliceMclkCyclesOverrun: " << hcounter.GetData() << '\t' << vcounter.GetData() << '\t' << currentTimesliceTotalMclkCycles << '\t' << stateLastUpdateMclk << '\t' << stateLastUpdateMclkUnused << '\t' << stateLastUpdateMclkUnusedFromLastTimeslice << '\t' << std::setprecision(16) << stateLastUpdateTime << '\t' << lastTimesliceMclkCyclesOverrun << '\n';
	}

	//Record any unused mclk cycles from this timeslice, so we can pass them over into the
	//next timeslice.
	stateLastUpdateMclkUnusedFromLastTimeslice = stateLastUpdateMclkUnused;

	//Calculate initial values for the processor state time leading into the next
	//timeslice. Note that the stateLastUpdateMclkUnusedFromLastTimeslice variable is
	//subtracted from the current processor state MCLK value within the
	//GetProcessorStateMclkCurrent() method, so we can safely pass it in as the initial
	//stateLastUpdateMclkUnused value here.
	stateLastUpdateMclk = lastTimesliceMclkCyclesOverrun;
	stateLastUpdateMclkUnused = stateLastUpdateMclkUnusedFromLastTimeslice;
	stateLastUpdateTime = ConvertMclkCountToAccessTime(lastTimesliceMclkCyclesOverrun);
}

//----------------------------------------------------------------------------------------
void S315_5313::ExecuteTimeslice(double nanoseconds)
{
	//##DEBUG##
	if(outputTimingDebugMessages)
	{
		std::wcout << "VDP - ExecuteTimeslice: " << nanoseconds << '\t' << hcounter.GetData() << '\t' << vcounter.GetData() << '\t' << stateLastUpdateMclkUnused << '\n';
	}

	//Ensure that the DMA worker thread has finished executing. We need to do this here,
	//otherwise the result of returning from this function will override the timeslice
	//progress set by the worker thread, potentially causing waiting devices to execute
	//beyond the DMA execution progress set by the DMA worker thread.
	std::unique_lock<std::mutex> workerThreadLock(workerThreadMutex);
	if(workerThreadActive && !workerThreadPaused && busGranted)
	{
		workerThreadIdle.wait(workerThreadLock);
	}

	//If the render thread is lagging, pause here until it has caught up, so we don't
	//leave the render thread behind with an ever-increasing workload it will never be
	//able to complete.
	if(renderThreadLagging)
	{
		std::unique_lock<std::mutex> lock(timesliceMutex);
		while(renderThreadLagging)
		{
			renderThreadLaggingStateChange.wait(lock);
		}
	}
}

//----------------------------------------------------------------------------------------
void S315_5313::ExecuteTimesliceTimingPointStep(unsigned int accessContext)
{
	//Ensure that the DMA worker thread has finished executing
	std::unique_lock<std::mutex> workerThreadLock(workerThreadMutex);
	if(workerThreadActive && !workerThreadPaused && busGranted)
	{
		//##DEBUG##
		//std::wcout << L"ExecuteTimeslice is on a timing point waiting for DMA worker thread to pause\n";
		//std::wcout << '\t' << workerThreadActive << '\t' << workerThreadPaused << '\t' << busGranted << '\n';

		workerThreadIdle.wait(workerThreadLock);
	}

	//Explicitly release our lock on workerThreadMutex here, since we no longer require
	//synchronization with the worker thread, and the SetLineState method can require
	//workerThreadMutex to be available.
	workerThreadLock.unlock();

	//Ensure the VDP is advanced up to the end of its timeslice
	if((currentTimesliceTotalMclkCycles > 0) && (currentTimesliceTotalMclkCycles > GetProcessorStateMclkCurrent()))
	{
		UpdateInternalState(currentTimesliceTotalMclkCycles, false, false, false, false, false, false, false);
	}

	//Update our line state change predictions at the VSYNC timing point. This is
	//necessary to ensure that we will predict the next occurrence of changes to the INT
	//line correctly. It's possible for the INT line to be triggered once per frame, even
	//if we don't have any interaction from external devices, since the INT line is
	//automatically negated. Re-evaluating line state changes here will ensure that
	//changes to the INT line are predicted again for the next frame.
	UpdatePredictedLineStateChanges(GetDeviceContext(), GetCurrentTimesliceProgress(), (unsigned int)AccessContext::TimingPoint);

	//##DEBUG##
	if((AccessContext)accessContext == AccessContext::TimingPoint)
	{
		if(outputTimingDebugMessages)
		{
			std::wcout << "VDP - ExecuteOnTimingPoint: " << hcounter.GetData() << '\t' << vcounter.GetData() << '\t' << stateLastUpdateMclkUnused << '\n';
		}

		//##TODO## Calculate the time at which we expect the next vertical interrupt
		//trigger point to be reached, and raise line state changes for the Z80 INT line.
		//Note that Z80 vertical interrupts are unaffected by the VINT enable state, and
		//are cleared automatically on the next line, so we can trigger them here
		//conditionally, and we only need to revoke them if the screen mode changes.
	}
}

//----------------------------------------------------------------------------------------
double S315_5313::GetNextTimingPointInDeviceTime(unsigned int& accessContext) const
{
	//Set the access context data for this timing point to a special value, so we can
	//differentiate between rollback events and timing point events.
	accessContext = (unsigned int)AccessContext::TimingPoint;

	//Calculate the time at which the next vsync event will occur
	//##TODO## Take pending screen mode settings changes into account
	const HScanSettings& hscanSettings = GetHScanSettings(screenModeRS0, screenModeRS1);
	const VScanSettings& vscanSettings = GetVScanSettings(screenModeV30, palMode, interlaceEnabled);
	unsigned int pixelClockTicksBeforeVSync = GetPixelClockStepsBetweenHVCounterValues(true, hscanSettings, hcounter.GetData(), hscanSettings.vcounterIncrementPoint, vscanSettings, interlaceEnabled, GetStatusFlagOddInterlaceFrame(), vcounter.GetData(), vscanSettings.vsyncAssertedPoint);
	unsigned int mclkCyclesBeforeVSync = GetMclkTicksForPixelClockTicks(hscanSettings, pixelClockTicksBeforeVSync, hcounter.GetData(), screenModeRS0, screenModeRS1);

	//Adjust the cycle count at which the event will occur to take into account unused
	//mclk cycles from the last timeslice. These cycles have already been executed, so
	//they're available for free. As such, we need to subtract these cycles from the
	//number of cycles we need to execute in order to reach the target event.
	if(stateLastUpdateMclkUnusedFromLastTimeslice >= mclkCyclesBeforeVSync)
	{
		mclkCyclesBeforeVSync = 0;
	}
	else
	{
		mclkCyclesBeforeVSync = mclkCyclesBeforeVSync - stateLastUpdateMclkUnusedFromLastTimeslice;
	}

	//This timing code is rather hard to understand from the code alone. The best way to
	//visualize it is with a diagram. Basically, all we're doing here is converting the
	//time of the next event from a time which is relative to the current VDP state, which
	//may be either before or after the end of the last timeslice, to a time which is
	//relative to the end of the last timeslice, which is what the system needs here.
	unsigned int mclkCyclesUntilNextEventInDeviceTime = mclkCyclesBeforeVSync;
	unsigned int mclkCyclesUntilNextEventFromLastTimesliceMclkCycleEnd = lastTimesliceMclkCyclesOverrun + mclkCyclesUntilNextEventInDeviceTime;
	double extraTimeBetweenLastTimesliceMclkCycleEndAndActualTimesliceEnd = currentTimesliceMclkCyclesRemainingTime;
	double timeUntilNextEventFromLastTimesliceMclkCycleEnd = ConvertMclkCountToAccessTime(mclkCyclesUntilNextEventFromLastTimesliceMclkCycleEnd);
	double timeFromEndOfLastTimesliceToNextEventInSystemTime = timeUntilNextEventFromLastTimesliceMclkCycleEnd - extraTimeBetweenLastTimesliceMclkCycleEndAndActualTimesliceEnd;

	//##DEBUG##
	if(outputTimingDebugMessages)
	{
		std::wcout << "VDP - GetTimingPoint: " << hcounter.GetData() << '\t' << vcounter.GetData() << '\t' << lastTimesliceMclkCyclesOverrun << '\t' << currentTimesliceMclkCyclesRemainingTime << '\t' << mclkCyclesBeforeVSync << '\t' << std::setprecision(16) << timeFromEndOfLastTimesliceToNextEventInSystemTime << '\n';
	}

	return timeFromEndOfLastTimesliceToNextEventInSystemTime;
}

//----------------------------------------------------------------------------------------
void S315_5313::ExecuteRollback()
{
	//Port monitor state
	if(logStatusRegisterRead || logDataPortRead || logHVCounterRead || logControlPortWrite || logDataPortWrite)
	{
		std::unique_lock<std::mutex> lock(portMonitorMutex);
		portMonitorList = bportMonitorList;
	}

	//Update state
	currentTimesliceLength = bcurrentTimesliceLength;
	lastTimesliceMclkCyclesRemainingTime = blastTimesliceMclkCyclesRemainingTime;
	currentTimesliceMclkCyclesRemainingTime = bcurrentTimesliceMclkCyclesRemainingTime;
	lastTimesliceMclkCyclesOverrun = blastTimesliceMclkCyclesOverrun;
	stateLastUpdateTime = bstateLastUpdateTime;
	stateLastUpdateMclk = bstateLastUpdateMclk;
	stateLastUpdateMclkUnused = bstateLastUpdateMclkUnused;
	stateLastUpdateMclkUnusedFromLastTimeslice = bstateLastUpdateMclkUnusedFromLastTimeslice;

	//Bus interface
	busGranted = bbusGranted;
	palModeLineState = bpalModeLineState;
	resetLineState = bresetLineState;
	lineStateIPL = blineStateIPL;
	busRequestLineState = bbusRequestLineState;

	//Clock sources
	clockMclkCurrent = bclockMclkCurrent;

	//Saved CE line state for Read-Modify-Write cycles
	lineLWRSavedStateRMW = blineLWRSavedStateRMW;
	lineUWRSavedStateRMW = blineUWRSavedStateRMW;
	lineCAS0SavedStateRMW = blineCAS0SavedStateRMW;
	lineRAS0SavedStateRMW = blineRAS0SavedStateRMW;
	lineOE0SavedStateRMW = blineOE0SavedStateRMW;

	//Physical registers and memory buffers
	reg.Rollback();
	vram->Rollback();
	cram->Rollback();
	vsram->Rollback();
	spriteCache->Rollback();
	status = bstatus;
	hcounter = bhcounter;
	vcounter = bvcounter;
	hcounterLatchedData = bhcounterLatchedData;
	vcounterLatchedData = bvcounterLatchedData;
	hintCounter = bhintCounter;
	vintPending = bvintPending;
	hintPending = bhintPending;
	exintPending = bexintPending;
	readDataAvailable = breadDataAvailable;
	readDataHalfCached = breadDataHalfCached;
	dmaFillOperationRunning = bdmaFillOperationRunning;
	vsramLastRenderReadCache = bvsramLastRenderReadCache;

	//Active register settings
	interlaceEnabled = binterlaceEnabled;
	interlaceDouble = binterlaceDouble;
	screenModeRS0 = bscreenModeRS0;
	screenModeRS1 = bscreenModeRS1;
	screenModeV30 = bscreenModeV30;
	palMode = bpalMode;

	//Cached register settings
	hvCounterLatchEnabled = bhvCounterLatchEnabled;
	vintEnabled = bvintEnabled;
	hintEnabled = bhintEnabled;
	exintEnabled = bexintEnabled;
	hintCounterReloadValue = bhintCounterReloadValue;
	dmaEnabled = bdmaEnabled;
	dmd0 = bdmd0;
	dmd1 = bdmd1;
	dmaLengthCounter = bdmaLengthCounter;
	dmaSourceAddressByte1 = bdmaSourceAddressByte1;
	dmaSourceAddressByte2 = bdmaSourceAddressByte2;
	dmaSourceAddressByte3 = bdmaSourceAddressByte3;
	autoIncrementData = bautoIncrementData;
	interlaceEnabledCached = binterlaceEnabledCached;
	interlaceDoubleCached = binterlaceDoubleCached;
	screenModeRS0Cached = bscreenModeRS0Cached;
	screenModeRS1Cached = bscreenModeRS1Cached;
	screenModeV30Cached = bscreenModeV30Cached;
	screenModeM5Cached = bscreenModeM5Cached;
	displayEnabledCached = bdisplayEnabledCached;
	spriteAttributeTableBaseAddressDecoded = bspriteAttributeTableBaseAddressDecoded;
	verticalScrollModeCached = bverticalScrollModeCached;

	//FIFO buffer registers
	for(unsigned int i = 0; i < fifoBufferSize; ++i)
	{
		fifoBuffer[i] = bfifoBuffer[i];
	}
	readBuffer = breadBuffer;
	fifoNextReadEntry = bfifoNextReadEntry;
	fifoNextWriteEntry = bfifoNextWriteEntry;

	//Interrupt line rollback data
	lineStateChangePendingVINT = blineStateChangePendingVINT;
	lineStateChangeVINTMClkCountFromCurrent = blineStateChangeVINTMClkCountFromCurrent;
	lineStateChangeVINTTime = blineStateChangeVINTTime;
	lineStateChangePendingHINT = blineStateChangePendingHINT;
	lineStateChangeHINTMClkCountFromCurrent = blineStateChangeHINTMClkCountFromCurrent;
	lineStateChangeHINTTime = blineStateChangeHINTTime;
	lineStateChangePendingEXINT = blineStateChangePendingEXINT;
	lineStateChangeEXINTMClkCountFromCurrent = blineStateChangeEXINTMClkCountFromCurrent;
	lineStateChangeEXINTTime = blineStateChangeEXINTTime;
	lineStateChangePendingINTAsserted = blineStateChangePendingINTAsserted;
	lineStateChangeINTAssertedMClkCountFromCurrent = blineStateChangeINTAssertedMClkCountFromCurrent;
	lineStateChangeINTAssertedTime = blineStateChangeINTAssertedTime;
	lineStateChangePendingINTNegated = blineStateChangePendingINTNegated;
	lineStateChangeINTNegatedMClkCountFromCurrent = blineStateChangeINTNegatedMClkCountFromCurrent;
	lineStateChangeINTNegatedTime = blineStateChangeINTNegatedTime;

	//Control port registers
	codeAndAddressRegistersModifiedSinceLastWrite = bcodeAndAddressRegistersModifiedSinceLastWrite;
	commandWritePending = bcommandWritePending;
	originalCommandAddress = boriginalCommandAddress;
	commandAddress = bcommandAddress;
	commandCode = bcommandCode;

	//DMA worker thread properties
	workerThreadPaused = bworkerThreadPaused;

	//DMA transfer registers
	dmaTransferActive = bdmaTransferActive;
	dmaTransferReadDataCached = bdmaTransferReadDataCached;
	dmaTransferReadCache = bdmaTransferReadCache;
	dmaTransferNextReadMclk = bdmaTransferNextReadMclk;
	dmaTransferLastTimesliceUsedReadDelay = bdmaTransferLastTimesliceUsedReadDelay;
	dmaTransferInvalidPortWriteCached = bdmaTransferInvalidPortWriteCached;
	dmaTransferInvalidPortWriteAddressCache = bdmaTransferInvalidPortWriteAddressCache;
	dmaTransferInvalidPortWriteDataCache = bdmaTransferInvalidPortWriteDataCache;

	//External interrupt settings
	externalInterruptVideoTriggerPointPending = bexternalInterruptVideoTriggerPointPending;
	externalInterruptVideoTriggerPointHCounter = bexternalInterruptVideoTriggerPointHCounter;
	externalInterruptVideoTriggerPointVCounter = bexternalInterruptVideoTriggerPointVCounter;

	//Clear any uncommitted timeslices from our render timeslice buffers
	timesliceRenderInfoListUncommitted.clear();
	regTimesliceListUncommitted.clear();
	for(std::list<ITimedBufferInt::Timeslice*>::const_iterator i = vramTimesliceListUncommitted.begin(); i != vramTimesliceListUncommitted.end(); ++i)
	{
		vram->FreeTimesliceReference(*i);
	}
	vramTimesliceListUncommitted.clear();
	for(std::list<ITimedBufferInt::Timeslice*>::const_iterator i = cramTimesliceListUncommitted.begin(); i != cramTimesliceListUncommitted.end(); ++i)
	{
		cram->FreeTimesliceReference(*i);
	}
	cramTimesliceListUncommitted.clear();
	for(std::list<ITimedBufferInt::Timeslice*>::const_iterator i = vsramTimesliceListUncommitted.begin(); i != vsramTimesliceListUncommitted.end(); ++i)
	{
		vsram->FreeTimesliceReference(*i);
	}
	vsramTimesliceListUncommitted.clear();
	for(std::list<ITimedBufferInt::Timeslice*>::const_iterator i = spriteCacheTimesliceListUncommitted.begin(); i != spriteCacheTimesliceListUncommitted.end(); ++i)
	{
		spriteCache->FreeTimesliceReference(*i);
	}
	spriteCacheTimesliceListUncommitted.clear();

	//##DEBUG##
	if(outputRenderSyncMessages || outputTimingDebugMessages)
	{
		//Wait for the render thread to complete its work
		std::unique_lock<std::mutex> lock(renderThreadMutex);
		while(pendingRenderOperationCount > 0)
		{
			renderThreadLaggingStateChange.wait(lock);
		}

		//Print out render thread synchronization info
		if(outputTimingDebugMessages)
		{
			std::wcout << "VDP Synchronization - Rollback:\n"
			           << "-Digital:\t" << hcounter.GetData() << '\t' << vcounter.GetData() << '\t' << stateLastUpdateMclkUnusedFromLastTimeslice << '\n'
			           << "-Render:\t" << renderDigitalHCounterPos << '\t' << renderDigitalVCounterPos << '\t' << renderDigitalRemainingMclkCycles << '\n';
		}

		//##DEBUG##
		if((hcounter.GetData() != renderDigitalHCounterPos) || (vcounter.GetData() != renderDigitalVCounterPos))
		{
			std::wcout << "*************************************************************\n"
			           << "VDP render thread synchronization lost:\n"
			           << "-Digital(H/V): " << hcounter.GetData() << '\t' << vcounter.GetData() << '\n'
			           << "-Render(H/V):  " << renderDigitalHCounterPos << '\t' << renderDigitalVCounterPos << '\n'
			           << "-renderDigitalRemainingMclkCycles: " << renderDigitalRemainingMclkCycles << '\n'
			           << "-stateLastUpdateMclkUnusedFromLastTimeslice: " << stateLastUpdateMclkUnusedFromLastTimeslice << '\n'
			           << "*************************************************************\n";
		}
	}
}

//----------------------------------------------------------------------------------------
void S315_5313::ExecuteCommit()
{
	//Port monitor state
	if(logStatusRegisterRead || logDataPortRead || logHVCounterRead || logControlPortWrite || logDataPortWrite)
	{
		std::unique_lock<std::mutex> lock(portMonitorMutex);
		bportMonitorList = portMonitorList;
	}

	//Update state
	bcurrentTimesliceLength = currentTimesliceLength;
	blastTimesliceMclkCyclesRemainingTime = lastTimesliceMclkCyclesRemainingTime;
	bcurrentTimesliceMclkCyclesRemainingTime = currentTimesliceMclkCyclesRemainingTime;
	blastTimesliceMclkCyclesOverrun = lastTimesliceMclkCyclesOverrun;
	bstateLastUpdateTime = stateLastUpdateTime;
	bstateLastUpdateMclk = stateLastUpdateMclk;
	bstateLastUpdateMclkUnused = stateLastUpdateMclkUnused;
	bstateLastUpdateMclkUnusedFromLastTimeslice = stateLastUpdateMclkUnusedFromLastTimeslice;

	//Bus interface
	bbusGranted = busGranted;
	bpalModeLineState = palModeLineState;
	bresetLineState = resetLineState;
	blineStateIPL = lineStateIPL;
	bbusRequestLineState = busRequestLineState;

	//Clock sources
	bclockMclkCurrent = clockMclkCurrent;

	//Saved CE line state for Read-Modify-Write cycles
	blineLWRSavedStateRMW = lineLWRSavedStateRMW;
	blineUWRSavedStateRMW = lineUWRSavedStateRMW;
	blineCAS0SavedStateRMW = lineCAS0SavedStateRMW;
	blineRAS0SavedStateRMW = lineRAS0SavedStateRMW;
	blineOE0SavedStateRMW = lineOE0SavedStateRMW;

	//Physical registers and memory buffers
	reg.Commit();
	vram->Commit();
	cram->Commit();
	vsram->Commit();
	spriteCache->Commit();
	bstatus = status;
	bhcounter = hcounter;
	bvcounter = vcounter;
	bhcounterLatchedData = hcounterLatchedData;
	bvcounterLatchedData = vcounterLatchedData;
	bhintCounter = hintCounter;
	bvintPending = vintPending;
	bhintPending = hintPending;
	bexintPending = exintPending;
	breadDataAvailable = readDataAvailable;
	breadDataHalfCached = readDataHalfCached;
	bdmaFillOperationRunning = dmaFillOperationRunning;
	bvsramLastRenderReadCache = vsramLastRenderReadCache;

	//Active register settings
	binterlaceEnabled = interlaceEnabled;
	binterlaceDouble = interlaceDouble;
	bscreenModeRS0 = screenModeRS0;
	bscreenModeRS1 = screenModeRS1;
	bscreenModeV30 = screenModeV30;
	bpalMode = palMode;

	//Cached register settings
	bhvCounterLatchEnabled = hvCounterLatchEnabled;
	bvintEnabled = vintEnabled;
	bhintEnabled = hintEnabled;
	bexintEnabled = exintEnabled;
	bhintCounterReloadValue = hintCounterReloadValue;
	bdmaEnabled = dmaEnabled;
	bdmd0 = dmd0;
	bdmd1 = dmd1;
	bdmaLengthCounter = dmaLengthCounter;
	bdmaSourceAddressByte1 = dmaSourceAddressByte1;
	bdmaSourceAddressByte2 = dmaSourceAddressByte2;
	bdmaSourceAddressByte3 = dmaSourceAddressByte3;
	bautoIncrementData = autoIncrementData;
	binterlaceEnabledCached = interlaceEnabledCached;
	binterlaceDoubleCached = interlaceDoubleCached;
	bscreenModeRS0Cached = screenModeRS0Cached;
	bscreenModeRS1Cached = screenModeRS1Cached;
	bscreenModeV30Cached = screenModeV30Cached;
	bscreenModeM5Cached = screenModeM5Cached;
	bdisplayEnabledCached = displayEnabledCached;
	bspriteAttributeTableBaseAddressDecoded = spriteAttributeTableBaseAddressDecoded;
	bverticalScrollModeCached = verticalScrollModeCached;

	//Propagate any changes to the cached DMA settings back into the reg buffer. This
	//makes changes to DMA settings visible to the debugger.
	if(cachedDMASettingsChanged)
	{
		AccessTarget accessTarget;
		accessTarget.AccessLatest();
		RegSetDMALengthCounter(accessTarget, dmaLengthCounter);
		RegSetDMASourceAddressByte1(accessTarget, dmaSourceAddressByte1);
		RegSetDMASourceAddressByte2(accessTarget, dmaSourceAddressByte2);
		cachedDMASettingsChanged = false;
	}

	//FIFO buffer registers
	for(unsigned int i = 0; i < fifoBufferSize; ++i)
	{
		bfifoBuffer[i] = fifoBuffer[i];
	}
	breadBuffer = readBuffer;
	bfifoNextReadEntry = fifoNextReadEntry;
	bfifoNextWriteEntry = fifoNextWriteEntry;

	//Interrupt line rollback data
	blineStateChangePendingVINT = lineStateChangePendingVINT;
	blineStateChangeVINTMClkCountFromCurrent = lineStateChangeVINTMClkCountFromCurrent;
	blineStateChangeVINTTime = lineStateChangeVINTTime;
	blineStateChangePendingHINT = lineStateChangePendingHINT;
	blineStateChangeHINTMClkCountFromCurrent = lineStateChangeHINTMClkCountFromCurrent;
	blineStateChangeHINTTime = lineStateChangeHINTTime;
	blineStateChangePendingEXINT = lineStateChangePendingEXINT;
	blineStateChangeEXINTMClkCountFromCurrent = lineStateChangeEXINTMClkCountFromCurrent;
	blineStateChangeEXINTTime = lineStateChangeEXINTTime;
	blineStateChangePendingINTAsserted = lineStateChangePendingINTAsserted;
	blineStateChangeINTAssertedMClkCountFromCurrent = lineStateChangeINTAssertedMClkCountFromCurrent;
	blineStateChangeINTAssertedTime = lineStateChangeINTAssertedTime;
	blineStateChangePendingINTNegated = lineStateChangePendingINTNegated;
	blineStateChangeINTNegatedMClkCountFromCurrent = lineStateChangeINTNegatedMClkCountFromCurrent;
	blineStateChangeINTNegatedTime = lineStateChangeINTNegatedTime;

	//Control port registers
	bcodeAndAddressRegistersModifiedSinceLastWrite = codeAndAddressRegistersModifiedSinceLastWrite;
	bcommandWritePending = commandWritePending;
	boriginalCommandAddress = originalCommandAddress;
	bcommandAddress = commandAddress;
	bcommandCode = commandCode;

	//DMA worker thread properties
	bworkerThreadPaused = workerThreadPaused;

	//DMA transfer registers
	bdmaTransferActive = dmaTransferActive;
	bdmaTransferReadDataCached = dmaTransferReadDataCached;
	bdmaTransferReadCache = dmaTransferReadCache;
	bdmaTransferNextReadMclk = dmaTransferNextReadMclk;
	bdmaTransferLastTimesliceUsedReadDelay = dmaTransferLastTimesliceUsedReadDelay;
	bdmaTransferInvalidPortWriteCached = dmaTransferInvalidPortWriteCached;
	bdmaTransferInvalidPortWriteAddressCache = dmaTransferInvalidPortWriteAddressCache;
	bdmaTransferInvalidPortWriteDataCache = dmaTransferInvalidPortWriteDataCache;

	//External interrupt settings
	bexternalInterruptVideoTriggerPointPending = externalInterruptVideoTriggerPointPending;
	bexternalInterruptVideoTriggerPointHCounter = externalInterruptVideoTriggerPointHCounter;
	bexternalInterruptVideoTriggerPointVCounter = externalInterruptVideoTriggerPointVCounter;

	//Ensure that a valid latest timeslice exists in all our buffers. We need this check
	//here, because commits can be triggered by the system at potentially any point in
	//time, whether a timeslice has been issued or not.
	if(!regTimesliceListUncommitted.empty() && !vramTimesliceListUncommitted.empty() && !cramTimesliceListUncommitted.empty() && !vsramTimesliceListUncommitted.empty() && !spriteCacheTimesliceListUncommitted.empty())
	{
		//Obtain a timeslice lock so we can update the data we feed to the render thread
		std::unique_lock<std::mutex> lock(timesliceMutex);

		//Add the number of timeslices we are about to commit to the count of pending
		//render operations. This is used to track if the render thread is lagging.
		pendingRenderOperationCount += (unsigned int)regTimesliceListUncommitted.size();

		//Move all timeslices in our uncommitted timeslice lists over to the committed
		//timeslice lists, for processing by the render thread.
		timesliceRenderInfoList.splice(timesliceRenderInfoList.end(), timesliceRenderInfoListUncommitted);
		regTimesliceList.splice(regTimesliceList.end(), regTimesliceListUncommitted);
		vramTimesliceList.splice(vramTimesliceList.end(), vramTimesliceListUncommitted);
		cramTimesliceList.splice(cramTimesliceList.end(), cramTimesliceListUncommitted);
		vsramTimesliceList.splice(vsramTimesliceList.end(), vsramTimesliceListUncommitted);
		spriteCacheTimesliceList.splice(spriteCacheTimesliceList.end(), spriteCacheTimesliceListUncommitted);

		//Notify the render thread that it's got more work to do
		renderThreadUpdate.notify_all();
	}

	//##DEBUG##
	if(outputRenderSyncMessages || outputTimingDebugMessages)
	{
		//Wait for the render thread to complete its work
		std::unique_lock<std::mutex> lock(renderThreadMutex);
		while(pendingRenderOperationCount > 0)
		{
			renderThreadLaggingStateChange.wait(lock);
		}

		//Print out render thread synchronization info
		if(outputTimingDebugMessages)
		{
			std::wcout << "VDP Synchronization - Commit:\n"
			           << "-Digital:\t" << hcounter.GetData() << '\t' << vcounter.GetData() << '\t' << stateLastUpdateMclkUnusedFromLastTimeslice << '\n'
			           << "-Render:\t" << renderDigitalHCounterPos << '\t' << renderDigitalVCounterPos << '\t' << renderDigitalRemainingMclkCycles << '\n';
		}

		//##DEBUG##
		if((hcounter.GetData() != renderDigitalHCounterPos) || (vcounter.GetData() != renderDigitalVCounterPos))
		{
			std::wcout << "*************************************************************\n"
			           << "VDP render thread synchronization lost:\n"
			           << "-Digital(H/V): " << hcounter.GetData() << '\t' << vcounter.GetData() << '\n'
			           << "-Render(H/V):  " << renderDigitalHCounterPos << '\t' << renderDigitalVCounterPos << '\n'
			           << "-renderDigitalRemainingMclkCycles: " << renderDigitalRemainingMclkCycles << '\n'
			           << "-stateLastUpdateMclkUnusedFromLastTimeslice: " << stateLastUpdateMclkUnusedFromLastTimeslice << '\n'
			           << "*************************************************************\n";
		}
	}
}

//----------------------------------------------------------------------------------------
//DMA functions
//----------------------------------------------------------------------------------------
void S315_5313::DMAWorkerThread()
{
	std::unique_lock<std::mutex> lock(workerThreadMutex);

	//##DEBUG##
//	std::wcout << L"DMAWorkerThread running\n";

	//Begin the DMA work loop
	while(workerThreadActive)
	{
		if(!busGranted)
		{
			//##DEBUG##
//			std::wcout << L"DMAWorkerThread going idle\t" << GetProcessorStateTime() << '\n';
//			std::wcout << '\t' << workerThreadActive << '\t' << workerThreadPaused << '\t' << busGranted << '\n';

			//If we don't currently have the bus, go idle until a DMA work request comes
			//through.
			GetDeviceContext()->SetTransientExecutionActive(false);
			workerThreadIdle.notify_all();
			workerThreadUpdate.wait(lock);
			GetDeviceContext()->SetTransientExecutionActive(busGranted);

			//##DEBUG##
//			std::wcout << L"DMAWorkerThread going active\t" << GetProcessorStateTime() << '\n';
		}
		else
		{
			//##DEBUG##
			if(commandCode.GetBit(5) != GetStatusFlagDMA())
			{
				std::wcout << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n";
				std::wcout << "VDP commandCode.GetBit(5) != GetStatusFlagDMA()\n";
				std::wcout << "__LINE__:\t" << __LINE__ << "\n";
				std::wcout << "stateLastUpdateMclk:\t" << stateLastUpdateMclk << "\n";
				std::wcout << "stateLastUpdateMclkUnused:\t" << stateLastUpdateMclkUnused << "\n";
				std::wcout << "stateLastUpdateMclkUnusedFromLastTimeslice:\t" << stateLastUpdateMclkUnusedFromLastTimeslice << "\n";
				std::wcout << "busGranted:\t" << busGranted << "\n";
				std::wcout << "dmaEnabled:\t" << dmaEnabled << "\n";
				std::wcout << "commandCode:\t" << std::hex << commandCode.GetData() << "\t" << commandCode.GetBitCount() << "\t" << commandCode.GetBitMask() << "\n";
				std::wcout << "status:\t" << status.GetData() << "\n";
				std::wcout << "GetStatusFlagDMA:\t" << GetStatusFlagDMA() << "\n";
				std::wcout << "dmd1:\t" << dmd1 << "\n";
				std::wcout << "dmd0:\t" << dmd1 << "\n";
				std::wcout << "dmaFillOperationRunning:\t" << dmaFillOperationRunning << "\n";
				std::wcout << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n";
			}

			//If a DMA transfer is currently in progress and we currently have the bus,
			//advance the processor state until the DMA operation is complete, or we reach
			//the end of the current timeslice.
			if(dmaTransferActive && busGranted)
			{
				//Advance the DMA operation. Note that we execute up to exactly 1 more
				//MCLK cycle than is available in the current timeslice. We need to do
				//this because there may be additional time remaining in the timeslice
				//which cannot be consumed by a whole MCLK cycle, but in the case where we
				//have the bus, we need to execute the entire length of the current
				//timeslice, so that any waiting devices don't stall waiting for us to
				//reach the end of the timeslice too.
				unsigned int mclkCycleTimeToExecuteTo = currentTimesliceTotalMclkCycles + 1;
				if(dmaAdvanceUntilDMAComplete)
				{
					//If we've been requested to advance the current DMA operation until
					//it is complete, ignore the timeslice length and advance until we
					//reach the desired state.

					//##DEBUG##
					//std::wcout << "VDP - DMAWorkerThreadForce: " << lastTimesliceTotalExecuteTime << '\t' << stateLastUpdateMclk << '\t' << stateLastUpdateMclkUnused << '\t' << stateLastUpdateMclkUnusedFromLastTimeslice << "\n";

					UpdateInternalState(mclkCycleTimeToExecuteTo, true, false, false, false, false, true, true);
				}
				else if(GetProcessorStateMclkCurrent() < mclkCycleTimeToExecuteTo)
				{
					//If we currently have the bus, advance the processor state until the DMA
					//operation is complete, or we reach the end of the current timeslice.

					//##DEBUG##
	//				std::wcout << "VDP - DMAWorkerThread: " << lastTimesliceTotalExecuteTime << '\t' << stateLastUpdateMclk << '\t' << stateLastUpdateMclkUnused << '\t' << stateLastUpdateMclkUnusedFromLastTimeslice << "\n";

					UpdateInternalState(mclkCycleTimeToExecuteTo, true, false, false, false, false, true, false);
				}

				//Update the timeslice execution progress for this device
				GetDeviceContext()->SetCurrentTimesliceProgress(lastTimesliceMclkCyclesRemainingTime + GetProcessorStateTime());

				//If the DMA transfer has been completed, negate BR to release the bus.
				if(!dmaTransferActive)
				{
					//##DEBUG##
	//				if(outputPortAccessDebugMessages)
	//				{
	//					std::wcout << "SetLineState - VDP_LINE_BR:\t" << false << '\t' << GetProcessorStateTime() << '\t' << GetProcessorStateMclkCurrent() << '\n';
	//				}

					//Calculate the time at which the line access change will occur, in
					//system time.
					double accessTime = GetProcessorStateTime() - lastTimesliceMclkCyclesRemainingTime;

					//This is a hack to handle VDP port access while a DMA transfer
					//operation is pending, but the bus hasn't yet been granted. This is
					//probably not the correct way the hardware would handle this kind of
					//event. We have this hack in place to work around a limitation in our
					//current M68000 core, which isn't able to grant the bus between two
					//halves of a long-word operation. Until we have a microcode level
					//M68000 core online, we cache these invalid reads, and process them
					//when the DMA operation is complete.
					if(dmaTransferInvalidPortWriteCached)
					{
						//##DEBUG##
						if(outputPortAccessDebugMessages)
						{
							std::wcout << "VDP Performing a cached write!\n";
						}

						dmaTransferInvalidPortWriteCached = false;
						WriteInterface(0, dmaTransferInvalidPortWriteAddressCache, dmaTransferInvalidPortWriteDataCache, GetDeviceContext(), accessTime, 0);
					}

					//Note that by negating BR, the M68000 should negate BG in response.
					//This may not occur immediately however. In this case, we will have
					//already cleared the dmaTransferActive flag, so our worker thread
					//will continue to run without advancing the processor state until the
					//bus is released.
					busRequestLineState = false;
					memoryBus->SetLineState((unsigned int)LineID::BR, Data(GetLineWidth((unsigned int)LineID::BR), (unsigned int)busRequestLineState), GetDeviceContext(), GetDeviceContext(), accessTime, (unsigned int)AccessContext::BRRelease);

					//Since we've reached the end of this DMA operation, reset the
					//timeslice execution progress to the end of the timeslice. Note that
					//we should do this after signaling the time at which the BR line is
					//negated, so that a dependent device which is listening for the BR
					//state to change won't advance beyond the change before it is
					//signalled.
					GetDeviceContext()->SetCurrentTimesliceProgress(currentTimesliceLength);

					//##DEBUG##
		//			std::wcout << "DMA Transfer complete\n";
				}
			}

			//If the VDP still has the bus, but we've negated BR or reached the end of the
			//current timeslice, suspend the worker thread until the next timeslice is
			//received, or the BR line state changes.
			if(busGranted)
			{
				//##DEBUG##
	//			std::wcout << L"DMAWorkerThread pausing\t" << GetProcessorStateTime() << '\n';
	//			std::wcout << '\t' << workerThreadActive << '\t' << workerThreadPaused << '\t' << busGranted << '\n';

				//Suspend the DMA worker thread until a new timeslice is received.
				workerThreadPaused = true;
				GetDeviceContext()->SetTransientExecutionActive(false);
				workerThreadIdle.notify_all();
				workerThreadUpdate.wait(lock);
				GetDeviceContext()->SetTransientExecutionActive(busGranted);
				workerThreadPaused = false;

				//##DEBUG##
	//			std::wcout << L"DMAWorkerThread resuming\t" << GetProcessorStateTime() << '\n';
			}
		}
	}

	//##DEBUG##
//	std::wcout << L"DMAWorkerThread terminating\t" << GetProcessorStateTime() << '\n';
//	std::wcout << '\t' << workerThreadActive << '\t' << busGranted << '\n';

	workerThreadStopped.notify_all();
}

//----------------------------------------------------------------------------------------
//Processor state advancement functions
//----------------------------------------------------------------------------------------
//The purpose of this function is to advance the internal state of the device up to the
//indicated time. This includes updates to the status register, DMA operations, the FIFO
//buffer, and any other internal changes that occur within the VDP over a given period of
//time. This update function is always called before each register change, so it can
//assume that no register changes have occurred since the last update step.
//----------------------------------------------------------------------------------------
//We need to keep a copy of all state which can affect the number of available slots,
//and refresh them as we advance through the execution. In order to support status
//register updates, we need to fully perform sprite calculations. In order to
//calculate the render slots, we need to track the raster position, and take into
//account screen mode settings. Basically, we need to duplicate some of the render
//logic.

//Settings which affect slots:
//-R1B6(DISP): Disables the display. We know there are no access limitations when the
//display is disabled. We also know that the display can be disabled between lines,
//then enabled again before the next line starts, and the next line will still be
//drawn, so it takes effect immediately. We also know as a side-effect of disabling
//the display during hblank that the maximum number of sprites for the next line is
//reduced, which affects mickey mania. Lets start with this taking effect immediately,
//although the effect it has on the sprite rendering will need to be researched in
//order to emulate mickey mania properly, and to get the status register correct in
//this case too.
//-R1B3(M2): Switches between V28 and V30 modes. No idea when it takes effect.
//-R12B0(RS1): Switches between H32/H40 mode. Not sure if this can be done mid-line
//(notes from charles macdonald suggest this may be possible), but we know it can be
//done mid-frame.

//Settings which MAY affect slots, but need to be tested:
//-R0B0(U2): Disables video output. It's possible that access may be allowed freely like
//when the display is disabled.
//-R1B7(U1): Affects the display mode.
//-R1B0(U2): Affects the display mode.
//-R1B2(M5): Switches between M4 and M5. We know this can happen mid-frame, but we
//don't know if it can happen mid-line.

//Settings which affect status register:
//None known

//Settings which MAY affect status register, but need to be tested:
//-R5(SAT): Defines the location of the sprite attribute table. Obviously it affects
//sprites, but we don't know for sure if it can be updated during a frame or a line.
//Same goes for sprite data in VRAM. Some changes are possible, but they're limited I
//believe. Charles has some notes on this IIRC.

//So there really isn't that much we need to cache at all. We just need to keep track
//of the current render position, and use these few state parameters to advance from
//that point to the next time interval. We don't actually need to evaluate any layers
//at all, and for sprites, we only care about the size and position of each sprite, to
//support dot overflow and collision flags.

//Actually, how does the collision flag work again? Doesn't it get set if
//non-transparent pixels overlap in sprites? That would require us to touch the VRAM
//and evaluate the colour value for each pixel in each sprite. If this is the case, we
//now have to worry about VRAM updates affecting the calculation too. We also still
//don't know exactly when the sprites are rendered, but notes from Charles suggest
//that they're done on the line before the line that they're displayed on. Behaviour
//from Mickey Mania also suggests the sprite table is built during hblank. It's likely
//what really happens is that the sprite table is populated in the hblank before the
//line those sprites are used on begins, and that the actual pixel colours for each
//sprite are only evaluated during rendering, in-sync with the raster position. It
//would then be mid-frame that collision and overflow flags are set, at the exact
//pixel location when they are evalulated (although it might actually be a bit before,
//with results being buffered and then output later).

//For now, let's assume that the collision flag is set if two pixels, non-transparent
//or not, overlap during active scan. This is almost certainly wrong, but let's just
//get our new update function working first, and fix up the sprite details later. It's
//not going to radically modify the implementation anyway, and we need to do testing
//to be sure of the correct behaviour.

//bool AdvanceProcessorState(double advanceTime, bool stopAtNextAccessSlot)
//Advances the processor state, assuming no state changes occur between the current
//processor update time and the advanceTime parameter. Returns false if the
//stotAtNextAccessSlot parameter was set and the target access time wasn't reached,
//otherwise returns true. This function should be used by our UpdateInternalState()
//function to do the actual advance. Our UpdateInternalState() function will manage
//advancing the FIFO and DMA. If the FIFO is empty (or becomes empty during the update
//process), the UpdateInternalState() function can just call AdvanceProcessorState()
//with stopAtNextAccessSlot set to false to finish the update step. Since we need to
//know what the current internal state time is in order to process DMA operations, a
//GetProcessorStateTime() function will be provided to give access to the current
//advancement time, in the case that a request for an access slot means the end of the
//timeblock isn't reached.
//----------------------------------------------------------------------------------------
void S315_5313::UpdateInternalState(unsigned int mclkCyclesTarget, bool checkFifoStateBeforeUpdate, bool stopWhenFifoEmpty, bool stopWhenFifoFull, bool stopWhenFifoNotFull, bool stopWhenReadDataAvailable, bool stopWhenNoDMAOperationInProgress, bool allowAdvancePastCycleTarget)
{
	//Gather some info about the current state
	bool dmaOperationWillRun = commandCode.GetBit(5) && (!dmd1 || dmd0 || dmaFillOperationRunning);
	bool readOperationWillRun = ValidReadTargetInCommandCode() && !readDataAvailable;
	bool writeOperationWillRun = !IsWriteFIFOEmpty();

	//##DEBUG##
	if(stopWhenNoDMAOperationInProgress && !dmaOperationWillRun)
	{
		std::wcout << "######################################################\n";
		std::wcout << "VDP UpdateInternalState called with stopWhenNoDMAOperationInProgress when no DMA operation was running!\n";
		std::wcout << "mclkCyclesTarget:\t" << mclkCyclesTarget << "\n";
		std::wcout << "stateLastUpdateMclk:\t" << stateLastUpdateMclk << "\n";
		std::wcout << "stateLastUpdateMclkUnused:\t" << stateLastUpdateMclkUnused << "\n";
		std::wcout << "busGranted:\t" << busGranted << "\n";
		std::wcout << "dmaEnabled:\t" << dmaEnabled << "\n";
		std::wcout << "commandCode:\t" << std::hex << commandCode.GetData() << "\t" << commandCode.GetBitCount() << "\t" << commandCode.GetBitMask() << "\n";
		std::wcout << "status:\t" << status.GetData() << "\n";
		std::wcout << "GetStatusFlagDMA:\t" << GetStatusFlagDMA() << "\n";
		std::wcout << "dmd1:\t" << dmd1 << "\n";
		std::wcout << "dmd0:\t" << dmd1 << "\n";
		std::wcout << "dmaFillOperationRunning:\t" << dmaFillOperationRunning << "\n";
		std::wcout << "######################################################\n";
	}

	//Check if we're already sitting on one of the target states
	bool targetFifoStateReached = false;
	if(checkFifoStateBeforeUpdate)
	{
		//If we're already at the target state, we exit the function immediately, since
		//there's no more work to do.
		if(TargetProcessorStateReached(stopWhenFifoEmpty, stopWhenFifoFull, stopWhenFifoNotFull, stopWhenReadDataAvailable, stopWhenNoDMAOperationInProgress))
		{
			return;
		}
	}

	//Check if we need to stop at an access slot on the next step
	bool stopAtAccessSlot = writeOperationWillRun || readOperationWillRun || dmaOperationWillRun;

	//Advance the VDP until we reach the target state
	while(!targetFifoStateReached && (!AdvanceProcessorState(mclkCyclesTarget, stopAtAccessSlot, allowAdvancePastCycleTarget) || allowAdvancePastCycleTarget))
	{
		//Advance a DMA transfer operation while the write FIFO or read cache is not full,
		//and there has been enough time since the read cache became empty to fully read
		//another value from external memory.
		//##TODO## We assume here that a read operation is performed immediately, whether
		//there is room in the FIFO or not currently to save it, and that the data then
		//gets held as pending until a FIFO slot opens up, at which time, the pending data
		//write then gets moved into the FIFO, and a new read operation begins
		//immediately. Do some hardware tests to confirm this is the way the real VDP
		//behaves, and confirm the timing of everything.
		//##FIX## This has been shown to be incorrect. When we do this, the refresh cycles
		//in our frame are swallowed up by the caching operation. That said, we do know
		//that DMA transfers use the FIFO, and we've observed DMA transfers to VRAM
		//correctly filling the FIFO at the start of the operation. Wait, hang on a
		//second, a write should take 4SC cycles, not 2. What's happening is that we're
		//moving data out of the FIFO too quickly. Once a write has been allowed out of
		//the FIFO, there's a 4SC cycle delay before another value can be released, or in
		//other words, we need to skip the next hcounter location when an access slot has
		//just been used. We need to emulate that when detecting the next access slot.
		while(commandCode.GetBit(5) && !dmd1 && busGranted && (!dmaTransferReadDataCached || !IsWriteFIFOFull())
		  && ((dmaTransferNextReadMclk + (dmaTransferReadTimeInMclkCycles - dmaTransferLastTimesliceUsedReadDelay)) <= GetProcessorStateMclkCurrent()))
		{
			//If there is space in the DMA transfer read cache, read a new data value into
			//the read cache.
			if(!dmaTransferReadDataCached)
			{
				CacheDMATransferReadData(dmaTransferNextReadMclk);
			}

			//Advance the dmaTransferLastReadMclk counter, and clear the count of used
			//read delay cycles from the last timeslice, which have just been consumed.
			dmaTransferNextReadMclk += (dmaTransferReadTimeInMclkCycles - dmaTransferLastTimesliceUsedReadDelay);
			dmaTransferLastTimesliceUsedReadDelay = 0;

			//If there is space in the write FIFO to store another write value, empty the
			//DMA transfer read cache data into the FIFO.
			if(!IsWriteFIFOFull())
			{
				PerformDMATransferOperation();
				AdvanceDMAState();
			}
		}

		//Advance a DMA fill operation if fill data has been latched to trigger the fill,
		//and the write FIFO is empty. If a data port write has been made during an active
		//DMA fill operation, that data port write is performed first, and we carry on the
		//fill once the FIFO returns to an empty state.
		if(commandCode.GetBit(5) && dmd1 && !dmd0 && dmaFillOperationRunning && IsWriteFIFOEmpty())
		{
			PerformDMAFillOperation();
			AdvanceDMAState();
		}

		//Advance a DMA copy operation
		if(commandCode.GetBit(5) && dmd1 && dmd0)
		{
			PerformDMACopyOperation();
			AdvanceDMAState();
		}

		//Perform a VRAM read cache operation
		bool readOperationPerformed = false;
		if(IsWriteFIFOEmpty() && !readDataAvailable && ValidReadTargetInCommandCode())
		//##TODO## Evaluate this old code, and figure out what to do with it.
		//if(!readDataAvailable && ValidReadTargetInCommandCode())
		{
			PerformReadCacheOperation();
			readOperationPerformed = true;
		}

		//Perform a VRAM write operation
		if(!IsWriteFIFOEmpty() && !readOperationPerformed)
		{
			PerformFIFOWriteOperation();
		}

		//If a DMA transfer operation is in progress, and there's a read value held in the
		//DMA transfer read cache as pending, and the write FIFO now has a slot available,
		//load the cached DMA transfer read data into the write FIFO. This restores the
		//FIFO state back to full at the end of this update step if a DMA transfer has
		//more data pending, which is essential in order to ensure that the FIFO full flag
		//appears as full immediately after a DMA transfer operation has reached the end
		//of the source data.
		if(commandCode.GetBit(5) && !dmd1 && dmaTransferReadDataCached && !IsWriteFIFOFull())
		{
			//If there is space in the write FIFO to store another write value, empty the
			//DMA transfer read cache data into the FIFO.
			PerformDMATransferOperation();
			AdvanceDMAState();
		}

		//Update the FIFO full and empty flags in the status register
		SetStatusFlagFIFOEmpty(IsWriteFIFOEmpty());
		SetStatusFlagFIFOFull(IsWriteFIFOFull());

		//Gather some info about the current state
		dmaOperationWillRun = commandCode.GetBit(5) && (!dmd1 || dmd0 || dmaFillOperationRunning);
		readOperationWillRun = ValidReadTargetInCommandCode() && !readDataAvailable;
		writeOperationWillRun = !IsWriteFIFOEmpty();

		//Check if we need to stop at an access slot on the next step
		stopAtAccessSlot = writeOperationWillRun || readOperationWillRun || dmaOperationWillRun;

		//Stop the update process if one of the target states has been reached
		targetFifoStateReached = TargetProcessorStateReached(stopWhenFifoEmpty, stopWhenFifoFull, stopWhenFifoNotFull, stopWhenReadDataAvailable, stopWhenNoDMAOperationInProgress);
	}
}

//----------------------------------------------------------------------------------------
bool S315_5313::AdvanceProcessorState(unsigned int mclkCyclesTarget, bool stopAtNextAccessSlot, bool allowAdvancePastTargetForAccessSlot)
{
	//Ensure that we aren't trying to trigger an update out of order
	//	if((!stopAtNextAccessSlot || !allowAdvancePastTargetForAccessSlot) && (mclkCyclesTarget < (stateLastUpdateMclk + stateLastUpdateMclkUnused)))
	if((mclkCyclesTarget < GetProcessorStateMclkCurrent()) && (!stopAtNextAccessSlot || !allowAdvancePastTargetForAccessSlot))
	{
		//##TODO## Raise an assert if this occurs
		//##DEBUG##
		//outputPortAccessDebugMessages = true;
		std::wcout << "######################################################\n";
		std::wcout << "VDP AdvanceProcessorState called out of order!\n";
		std::wcout << "mclkCyclesTarget:\t" << mclkCyclesTarget << "\n";
		std::wcout << "stateLastUpdateMclk:\t" << stateLastUpdateMclk << "\n";
		std::wcout << "stateLastUpdateMclkUnused:\t" << stateLastUpdateMclkUnused << "\n";
		std::wcout << "stateLastUpdateMclkUnusedFromLastTimeslice:\t" << stateLastUpdateMclkUnusedFromLastTimeslice << "\n";
		std::wcout << "######################################################\n";
		return true;
	}

	//##DEBUG##
	//std::wcout << "-VDP AdvanceProcessorState called: " << currentTimesliceTotalMclkCycles << "\t" << mclkCyclesTarget << "\t" << stateLastUpdateMclk << "\t" << stateLastUpdateMclkUnused << "\t" << stateLastUpdateMclkUnusedFromLastTimeslice << "\n";

	//Grab the latest settings for any registers which affect the video mode. Note that we
	//only advance the VDP state between register writes, so we know the currently latched
	//video mode settings are valid, and that any video mode register changes which have
	//been made need to be applied the next time those settings are latched.
	bool interlaceEnabledNew = interlaceEnabledCached;
	bool interlaceDoubleNew = interlaceDoubleCached;
	bool screenModeRS0New = screenModeRS0Cached;
	bool screenModeRS1New = screenModeRS1Cached;
	bool screenModeV30New = screenModeV30Cached;
	bool palModeNew = palModeLineState;

	//Check whether any of the relevant video mode settings have changed since they were
	//latched.
	//##NOTE## We used to latch changes to the H40 screen mode at hblank. While we know
	//this was incorrect, we haven't totally mapped out the behaviour of the VDP when this
	//register setting is toggled mid-line. I believe changes to the H40 screen mode
	//setting do take effect immediately, however, we're keeping the old code intact below
	//to hold off the change until hblank, in case it comes in handy for further testing
	//or development.
	//##TODO## Determine exactly how the VDP reacts to the H40 screen mode state being
	//changed at all the various points during a line.
	//##FIX## We've re-enabled the old behaviour. A number of changes need to occur to
	//ensure that these settings can safely be changed mid-line, and since we still don't
	//fully know the correct behaviour of the hardware, we're going to keep these settings
	//being latched at hblank for the time being.
	bool hscanSettingsChanged = (screenModeRS0 != screenModeRS0New) || (screenModeRS1 != screenModeRS1New);
	//	bool hscanSettingsChanged = false;
	//	screenModeRS0 = screenModeRS0New;
	//	screenModeRS1 = screenModeRS1New;
	//##TODO## Currently, changes to the palMode flag are applied at vblank. Test how the
	//real hardware deals with changes to this line by toggling the line state at runtime.
	bool vscanSettingsChanged = (screenModeV30 != screenModeV30New) || (palMode != palModeNew) || (interlaceEnabled != interlaceEnabledNew);

	//Calculate the total number of mclk cycles which need to be advanced
	unsigned int mclkCyclesToExecute = (mclkCyclesTarget - GetProcessorStateMclkCurrent());

	//Advance the device until we reach the target position
	bool stoppedAtAccessSlot = false;
	unsigned int mclkCyclesAdvanced = 0;
	while(((mclkCyclesAdvanced < mclkCyclesToExecute) && (!stopAtNextAccessSlot || !stoppedAtAccessSlot)) || (allowAdvancePastTargetForAccessSlot && stopAtNextAccessSlot && !stoppedAtAccessSlot))
	{
		//Obtain the current hscan and vscan settings
		//##TODO## Only latch these at the start of the search, and when they change, to
		//improve performance.
		const HScanSettings& hscanSettings = GetHScanSettings(screenModeRS0, screenModeRS1);
		const VScanSettings& vscanSettings = GetVScanSettings(screenModeV30, palMode, interlaceEnabled);

		//If the caller has requested the update to stop at the next access slot, gather
		//information on the next access slot update point.
		bool updatePointAccessSlotActive = false;
		unsigned int pixelClockTicksBeforeUpdatePointAccessSlot = 0;
		if(stopAtNextAccessSlot)
		{
			//Calculate the number of pixel clock ticks which will occur before the next
			//access slot occurs
			updatePointAccessSlotActive = true;
			pixelClockTicksBeforeUpdatePointAccessSlot = GetPixelClockTicksUntilNextAccessSlot(hscanSettings, vscanSettings, hcounter.GetData(), screenModeRS0, screenModeRS1, displayEnabledCached, vcounter.GetData());
		}

		//Gather information on the next hblank update point
		bool updatePointHBlankActive = false;
		unsigned int pixelClockTicksBeforeUpdatePointHBlank = 0;
		if(hscanSettingsChanged)
		{
			//Calculate the number of pixel clock ticks until the hblank event occurs, and
			//new screen mode settings are latched.
			updatePointHBlankActive = true;
			pixelClockTicksBeforeUpdatePointHBlank = GetPixelClockStepsBetweenHCounterValues(hscanSettings, hcounter.GetData(), hscanSettings.hblankSetPoint);
		}

		//Gather information on the next vblank update point
		bool updatePointVBlankActive = false;
		unsigned int pixelClockTicksBeforeUpdatePointVBlank = 0;
		if(vscanSettingsChanged)
		{
			//Calculate the number of pixel clock ticks until the vblank event occurs, and
			//new screen mode settings are latched.
			updatePointVBlankActive = true;
			pixelClockTicksBeforeUpdatePointVBlank = GetPixelClockStepsBetweenHVCounterValues(true, hscanSettings, hcounter.GetData(), hscanSettings.vcounterIncrementPoint, vscanSettings, interlaceEnabled, GetStatusFlagOddInterlaceFrame(), vcounter.GetData(), vscanSettings.vblankSetPoint);
		}

		//Gather information on the next vint update point
		bool updatePointVIntActive = true;
		unsigned int pixelClockTicksBeforeUpdatePointVInt = GetPixelClockStepsBetweenHVCounterValues(true, hscanSettings, hcounter.GetData(), hscanSettings.fflagSetPoint, vscanSettings, interlaceEnabled, GetStatusFlagOddInterlaceFrame(), vcounter.GetData(), vscanSettings.vblankSetPoint);

		//Gather information on the next exint update point
		bool updatePointEXIntActive = false;
		unsigned int pixelClockTicksBeforeUpdatePointEXInt = 0;
		if(externalInterruptVideoTriggerPointPending)
		{
			pixelClockTicksBeforeUpdatePointEXInt = GetPixelClockStepsBetweenHVCounterValues(true, hscanSettings, hcounter.GetData(), externalInterruptVideoTriggerPointHCounter, vscanSettings, interlaceEnabled, GetStatusFlagOddInterlaceFrame(), vcounter.GetData(), externalInterruptVideoTriggerPointVCounter);
		}

		//Gather information on the next hint counter advance update point
		bool updatePointHIntCounterAdvanceActive = true;
		//Note that since the HINT counter is advanced on the vcounter increment point, we
		//always need to increment the vcounter by 1 to get the vcounter event pos, since
		//no matter what the current value of the vcounter is, it must always be advanced
		//by 1 before we can reach the target event.
		unsigned int updatePointHIntCounterAdvanceVCounter = AddStepsToVCounter(hscanSettings, hcounter.GetData(), vscanSettings, interlaceEnabled, GetStatusFlagOddInterlaceFrame(), vcounter.GetData(), 1);
		unsigned int pixelClockTicksBeforeUpdatePointHIntCounterAdvance = GetPixelClockStepsBetweenHVCounterValues(true, hscanSettings, hcounter.GetData(), hscanSettings.vcounterIncrementPoint, vscanSettings, interlaceEnabled, GetStatusFlagOddInterlaceFrame(), vcounter.GetData(), updatePointHIntCounterAdvanceVCounter);

		//Calculate the number of mclk cycles available to advance in this next step in
		//order to reach the target mclk cycle count.
		unsigned int mclkCyclesAvailableInUpdateStep = mclkCyclesToExecute - mclkCyclesAdvanced;

		//Calculate the total number of pixel clock cycles which will occur if the VDP is
		//advanced the target number of mclk cycles.
		unsigned int mclkRemainingCycles;
		unsigned int pixelClockCyclesAvailableInUpdateStep = GetPixelClockTicksForMclkTicks(hscanSettings, stateLastUpdateMclkUnused + mclkCyclesAvailableInUpdateStep, hcounter.GetData(), screenModeRS0, screenModeRS1, mclkRemainingCycles);

		//Set this advance operation to stop at the next update point if required
		unsigned int mclkCyclesToAdvanceThisStep = mclkCyclesAvailableInUpdateStep;
		unsigned int pixelClockCyclesToAdvanceThisStep = pixelClockCyclesAvailableInUpdateStep;
		if(updatePointAccessSlotActive && stopAtNextAccessSlot && ((pixelClockTicksBeforeUpdatePointAccessSlot < pixelClockCyclesToAdvanceThisStep) || allowAdvancePastTargetForAccessSlot))
		{
			mclkCyclesToAdvanceThisStep = GetMclkTicksForPixelClockTicks(hscanSettings, pixelClockTicksBeforeUpdatePointAccessSlot, hcounter.GetData(), screenModeRS0, screenModeRS1) - stateLastUpdateMclkUnused;
			mclkRemainingCycles = 0;
			pixelClockCyclesToAdvanceThisStep = pixelClockTicksBeforeUpdatePointAccessSlot;
		}
		if(updatePointHBlankActive && (pixelClockTicksBeforeUpdatePointHBlank < pixelClockCyclesToAdvanceThisStep))
		{
			mclkCyclesToAdvanceThisStep = GetMclkTicksForPixelClockTicks(hscanSettings, pixelClockTicksBeforeUpdatePointHBlank, hcounter.GetData(), screenModeRS0, screenModeRS1) - stateLastUpdateMclkUnused;
			mclkRemainingCycles = 0;
			pixelClockCyclesToAdvanceThisStep = pixelClockTicksBeforeUpdatePointHBlank;
		}
		if(updatePointVBlankActive && (pixelClockTicksBeforeUpdatePointVBlank < pixelClockCyclesToAdvanceThisStep))
		{
			mclkCyclesToAdvanceThisStep = GetMclkTicksForPixelClockTicks(hscanSettings, pixelClockTicksBeforeUpdatePointVBlank, hcounter.GetData(), screenModeRS0, screenModeRS1) - stateLastUpdateMclkUnused;
			mclkRemainingCycles = 0;
			pixelClockCyclesToAdvanceThisStep = pixelClockTicksBeforeUpdatePointVBlank;
		}
		if(updatePointVIntActive && (pixelClockTicksBeforeUpdatePointVInt < pixelClockCyclesToAdvanceThisStep))
		{
			mclkCyclesToAdvanceThisStep = GetMclkTicksForPixelClockTicks(hscanSettings, pixelClockTicksBeforeUpdatePointVInt, hcounter.GetData(), screenModeRS0, screenModeRS1) - stateLastUpdateMclkUnused;
			mclkRemainingCycles = 0;
			pixelClockCyclesToAdvanceThisStep = pixelClockTicksBeforeUpdatePointVInt;
		}
		if(updatePointEXIntActive && (pixelClockTicksBeforeUpdatePointEXInt < pixelClockCyclesToAdvanceThisStep))
		{
			mclkCyclesToAdvanceThisStep = GetMclkTicksForPixelClockTicks(hscanSettings, pixelClockTicksBeforeUpdatePointEXInt, hcounter.GetData(), screenModeRS0, screenModeRS1) - stateLastUpdateMclkUnused;
			mclkRemainingCycles = 0;
			pixelClockCyclesToAdvanceThisStep = pixelClockTicksBeforeUpdatePointEXInt;
		}
		if(updatePointHIntCounterAdvanceActive && (pixelClockTicksBeforeUpdatePointHIntCounterAdvance < pixelClockCyclesToAdvanceThisStep))
		{
			mclkCyclesToAdvanceThisStep = GetMclkTicksForPixelClockTicks(hscanSettings, pixelClockTicksBeforeUpdatePointHIntCounterAdvance, hcounter.GetData(), screenModeRS0, screenModeRS1) - stateLastUpdateMclkUnused;
			mclkRemainingCycles = 0;
			pixelClockCyclesToAdvanceThisStep = pixelClockTicksBeforeUpdatePointHIntCounterAdvance;
		}

		//##DEBUG##
		//##FIX## This check is wrong.
		//if(mclkCyclesToAdvanceThisStep <= 0)
		//{
		//	std::wcout << "######################################################\n";
		//	std::wcout << "VDP AdvanceProcessorState stalled!\n";
		//	std::wcout << "mclkCyclesTarget:\t" << mclkCyclesTarget << "\n";
		//	std::wcout << "stateLastUpdateMclk:\t" << stateLastUpdateMclk << "\n";
		//	std::wcout << "stateLastUpdateMclkUnused:\t" << stateLastUpdateMclkUnused << "\n";
		//	std::wcout << "stateLastUpdateMclkUnusedFromLastTimeslice:\t" << stateLastUpdateMclkUnusedFromLastTimeslice << "\n";
		//	std::wcout << "totalPixelClockCyclesToExecute:\t" << totalPixelClockCyclesToExecute << "\n";
		//	std::wcout << "mclkRemainingCycles:\t" << mclkRemainingCycles << "\n";
		//	std::wcout << "mclkCyclesToAdvanceThisStep:\t" << mclkCyclesToAdvanceThisStep << "\n";
		//	std::wcout << "mclkCyclesAdvanced:\t" << mclkCyclesAdvanced << "\n";
		//	std::wcout << "hcounter:\t" << hcounter.GetData() << "\n";
		//	std::wcout << "vcounter:\t" << vcounter.GetData() << "\n";
		//	std::wcout << "######################################################\n";
		//}

		//Advance the HV counter
		unsigned int hcounterBeforeAdvance = hcounter.GetData();
		unsigned int vcounterBeforeAdvance = vcounter.GetData();
		unsigned int hcounterNew = hcounterBeforeAdvance;
		unsigned int vcounterNew = vcounterBeforeAdvance;
		bool oddFlagSet = GetStatusFlagOddInterlaceFrame();
		AdvanceHVCounters(hscanSettings, hcounterNew, vscanSettings, interlaceEnabled, oddFlagSet, vcounterNew, pixelClockCyclesToAdvanceThisStep);

		//Save the new values for the HV counter
		hcounter = hcounterNew;
		vcounter = vcounterNew;

		//Save the new value for the odd flag
		SetStatusFlagOddInterlaceFrame(oddFlagSet);

		//Update the index of the last value to be read from VSRAM by the render process,
		//based on the HV counter position before and after this advance step, and the
		//current vertical scroll mode. Note that if we weren't in the active scan region
		//as defined by the vertical counter, the current vsram read cache index will
		//remain unchanged.
		//##FIX## We now know this is incorrect. The VDP continues to read entries from
		//VSRAM, even when outside the active scan region.
		//vsramReadCacheIndex = GetUpdatedVSRAMReadCacheIndex(hscanSettings, vscanSettings, vsramReadCacheIndex, hcounterBeforeAdvance, vcounterBeforeAdvance, hcounterNew, vcounterNew, screenModeRS0, screenModeRS1, verticalScrollModeCached);

		//Update the cache of the last value to be read from VSRAM by the render process
		//##TODO## Add better comments here
		unsigned int lastVSRAMReadIndex = (hcounterNew >> 2) & 0x7E;
		if(!verticalScrollModeCached)
		{
			lastVSRAMReadIndex &= 0x03;
		}
		if(lastVSRAMReadIndex < 0x50)
		{
			vsramLastRenderReadCache = ((unsigned int)vsram->ReadLatest(lastVSRAMReadIndex+0) << 8) | (unsigned int)vsram->ReadLatest(lastVSRAMReadIndex+1);
		}
		else
		{
			vsramLastRenderReadCache = ((unsigned int)vsram->ReadLatest(0x4C+0) << 8) | (unsigned int)vsram->ReadLatest(0x4D+1);
			vsramLastRenderReadCache &= ((unsigned int)vsram->ReadLatest(0x4E+0) << 8) | (unsigned int)vsram->ReadLatest(0x4E+1);
		}

		//Note that the sprite overflow and collision flags are cleared when the status
		//register is read.
		//##NOTE## As soon as we come across a case where the sprite overflow or collision
		//flags are set, we can stop searching for more. We know the flag will remain set
		//for the rest of the update cycle.
		//##TODO## Update the sprite overflow flag
		//##TODO## Confirm whether the sprite overflow flag is set just for a sprite
		//overflow, or for a dot overflow as well.
		if(!GetStatusFlagSpriteOverflow())
		{
			//##STUB##
			SetStatusFlagSpriteOverflow(false);
		}

		//##TODO## Update the sprite collision flag
		//##TODO## Verify sprite collision behaviour on the hardware. Test opaque pixels
		//overlapping. Test sprites overlapping off the screen, both partially visible and
		//completely hidden. Determine whether this flag is set during active scan at the
		//pixel location where the overlap occurs, or whether it is set during blanking
		//when the sprites are parsed. Check how sprite overflows and sprite masking
		//interacts with the sprite collision flag.
		//##TODO## A comment by Eke in http://gendev.spritesmind.net/forum/viewtopic.php?t=778
		//says that the sprite collision flag is also cleared during vblank, not just when
		//the status register is read. Most likely, this applies to the sprite overflow
		//flag as well. Perform some hardware tests to confirm the exact time at which
		//these flags are set and cleared.
		//##TODO## Some testing on VRAM access timing shows that the sprite collision flag
		//seems to be actively set during the sprite pattern reads in hblank, so it seems
		//that this flag is set as each sprite pattern block is read. Most likely, the
		//sprite overflow flag is set while performing sprite mapping reads, if there are
		//still blocks remaining when the last sprite mapping read slot is used on a
		//scanline.
		if(!GetStatusFlagSpriteCollision())
		{
			//##STUB##
			SetStatusFlagSpriteCollision(false);
		}

		//Update the vblank and hblank flags
		bool vblankFlag = (vcounterNew >= vscanSettings.vblankSetPoint) && (vcounterNew < vscanSettings.vblankClearedPoint);
		bool hblankFlag = (hcounterNew >= hscanSettings.hblankSetPoint) || (hcounterNew < hscanSettings.hblankClearedPoint);
		//Note that although not mentioned in the official documentation, hardware tests
		//have confirmed that the VBlank flag is always forced to set when the display is
		//disabled. We emulate that here.
		vblankFlag |= !displayEnabledCached;
		SetStatusFlagVBlank(vblankFlag);
		SetStatusFlagHBlank(hblankFlag);

		//Update the current state MCLK cycle counter and remaining MCLK cycles
		stateLastUpdateMclk += ((stateLastUpdateMclkUnused + mclkCyclesToAdvanceThisStep) - mclkRemainingCycles);
		stateLastUpdateMclkUnused = mclkRemainingCycles;

		//If we passed at least one pending IPL line state change in this update step,
		//apply the state of the latest change as the current state of the IPL lines.
		bool vintLineChangeReached = (lineStateChangePendingVINT && (lineStateChangeVINTMClkCountFromCurrent <= mclkCyclesToAdvanceThisStep));
		bool hintLineChangeReached = (lineStateChangePendingHINT && (lineStateChangeHINTMClkCountFromCurrent <= mclkCyclesToAdvanceThisStep));
		bool exintLineChangeReached = (lineStateChangePendingEXINT && (lineStateChangeEXINTMClkCountFromCurrent <= mclkCyclesToAdvanceThisStep));
		if(vintLineChangeReached || hintLineChangeReached || exintLineChangeReached)
		{
			unsigned int newLineStateIPL = ~0u;
			newLineStateIPL = (vintLineChangeReached && (lineStateChangeVINTMClkCountFromCurrent < newLineStateIPL))? vintIPLLineState: newLineStateIPL;
			newLineStateIPL = (hintLineChangeReached && (lineStateChangeHINTMClkCountFromCurrent < newLineStateIPL))? hintIPLLineState: newLineStateIPL;
			newLineStateIPL = (exintLineChangeReached && (lineStateChangeEXINTMClkCountFromCurrent < newLineStateIPL))? exintIPLLineState: newLineStateIPL;
			lineStateIPL = newLineStateIPL;
		}

		//Update the MCLK countdown and line state change pending flags for each pending
		//IPL line state change type
		lineStateChangeVINTMClkCountFromCurrent -= vintLineChangeReached? lineStateChangeVINTMClkCountFromCurrent: mclkCyclesToAdvanceThisStep;
		lineStateChangeHINTMClkCountFromCurrent -= hintLineChangeReached? lineStateChangeHINTMClkCountFromCurrent: mclkCyclesToAdvanceThisStep;
		lineStateChangeEXINTMClkCountFromCurrent -= exintLineChangeReached? lineStateChangeEXINTMClkCountFromCurrent: mclkCyclesToAdvanceThisStep;
		lineStateChangePendingVINT = (lineStateChangePendingVINT && !vintLineChangeReached);
		lineStateChangePendingHINT = (lineStateChangePendingHINT && !hintLineChangeReached);
		lineStateChangePendingEXINT = (lineStateChangePendingEXINT && !exintLineChangeReached);

		//Update the MCLK countdown and line state change pending flags for changes to the
		//INT line
		bool intAssertedLineChangeReached = (lineStateChangePendingINTAsserted && (lineStateChangeINTAssertedMClkCountFromCurrent <= mclkCyclesToAdvanceThisStep));
		bool intNegatedLineChangeReached = (lineStateChangePendingINTNegated && (lineStateChangeINTNegatedMClkCountFromCurrent <= mclkCyclesToAdvanceThisStep));
		lineStateChangeINTAssertedMClkCountFromCurrent -= intAssertedLineChangeReached? lineStateChangeINTAssertedMClkCountFromCurrent: mclkCyclesToAdvanceThisStep;
		lineStateChangeINTNegatedMClkCountFromCurrent -= intNegatedLineChangeReached? lineStateChangeINTNegatedMClkCountFromCurrent: mclkCyclesToAdvanceThisStep;
		lineStateChangePendingINTAsserted = (lineStateChangePendingINTAsserted && !intAssertedLineChangeReached);
		lineStateChangePendingINTNegated = (lineStateChangePendingINTNegated && !intNegatedLineChangeReached);

		//If we're stopping at an access slot, flag that we've reached the target access
		//slot.
		if(updatePointAccessSlotActive && (pixelClockTicksBeforeUpdatePointAccessSlot == pixelClockCyclesToAdvanceThisStep))
		{
			stoppedAtAccessSlot = true;
		}

		//If horizontal scan information has changed, and we've just advanced to hblank,
		//latch the new screen mode settings.
		if(updatePointHBlankActive && (pixelClockTicksBeforeUpdatePointHBlank == pixelClockCyclesToAdvanceThisStep))
		{
			//##FIX## These settings changes are supposed to take effect immediately
			screenModeRS0 = screenModeRS0New;
			screenModeRS1 = screenModeRS1New;

			//Now that we've processed this screen mode settings change, flag that no
			//settings change is required.
			hscanSettingsChanged = false;
		}

		//If vertical scan information has changed, and we've just advanced to vblank,
		//latch the new screen mode settings.
		if(updatePointVBlankActive && (pixelClockTicksBeforeUpdatePointVBlank == pixelClockCyclesToAdvanceThisStep))
		{
			//If the interlace mode has changed, the new setting is latched when the
			//vblank set event occurs. This has been verified in all video modes through
			//hardware tests.
			interlaceEnabled = interlaceEnabledNew;
			interlaceDouble = interlaceDoubleNew;
			//##TODO## Verify that changes to the PAL line state are latched at vblank
			palMode = palModeNew;
			SetStatusFlagPAL(palMode);
			//##TODO## Verify that the V28/V30 mode change is latched at vblank
			screenModeV30 = screenModeV30New;

			//Now that we've processed this screen mode settings change, flag that no
			//settings change is required.
			vscanSettingsChanged = false;
		}

		//If we've just reached the point where VINT is triggered, set the VINT pending
		//flag. Note that the actual analog VINT line state change would have already been
		//raised in advance to occur at this time, we simply need to set the digital state
		//of the VDP to reflect this now.
		if(updatePointVIntActive && (pixelClockTicksBeforeUpdatePointVInt == pixelClockCyclesToAdvanceThisStep))
		{
			//Set the VINT pending flag, to indicate that the VDP needs to trigger a
			//vertical interrupt.
			vintPending = true;

			//Set the VINT occurrence flag for the status register. Note that this flag is
			//set even if vertical interrupts are disabled. If vertical interrupts are
			//enabled, this bit is cleared when the interrupt is acknowledged by the
			//M68000, otherwise this bit remains set from this point on, until an actual
			//vertical interrupt is generated and acknowledged by the M68000. This
			//behaviour has been confirmed through hardware tests.
			SetStatusFlagF(true);
		}

		//If we've just reached the point where an EXINT is being triggered, set the EXINT
		//pending flag. Note that the actual analog EXINT line state change would have
		//already been raised in advance to occur at this time, we simply need to set the
		//digital state of the VDP to reflect this now.
		if(updatePointEXIntActive && (pixelClockTicksBeforeUpdatePointEXInt == pixelClockCyclesToAdvanceThisStep))
		{
			//Since this external interrupt trigger point has now been processed, flag
			//that it is no longer pending.
			externalInterruptVideoTriggerPointPending = false;

			//Set the EXINT pending flag, to indicate that the VDP needs to trigger an
			//external interrupt.
			exintPending = true;

			//Latch the current hcounter and vcounter settings. Note that if HV counter
			//latching is not enabled, this data won't ever be used, as the current state
			//of the HV counter is latched when HV counter latching is enabled.
			//##TODO## Confirm what happens with the latched HV data when the interlace
			//mode changes. Is it the internal value of the hcounter which is latched, or
			//the external value?
			hcounterLatchedData = hcounter.GetData();
			vcounterLatchedData = vcounter.GetData();
		}

		//If we're passing the point at which the hint counter is advanced, advance the
		//counter and calculate its new value. Note that horizontal interrupt generation
		//would have already been raised at the correct time, but we need to change the
		//digital state of the VDP to reflect this.
		if(updatePointHIntCounterAdvanceActive && (pixelClockTicksBeforeUpdatePointHIntCounterAdvance == pixelClockCyclesToAdvanceThisStep))
		{
			if(updatePointHIntCounterAdvanceVCounter > vscanSettings.vblankSetPoint)
			{
				//Latch the initial hintCounter value for the frame
				hintCounter = hintCounterReloadValue;
			}
			else if(hintCounter == 0)
			{
				//Reload the hint counter now that it has expired
				hintCounter = hintCounterReloadValue;

				//Set the HINT pending flag, to indicate that the VDP needs to trigger a
				//horizontal interrupt.
				hintPending = true;
			}
			else
			{
				//Decrement the hint counter
				--hintCounter;
			}
		}

		//Update the total number of mclk cycles advanced so far
		mclkCyclesAdvanced += mclkCyclesToAdvanceThisStep;
	}

	//Calculate the new processor state time. Note that we don't have to do this within
	//the loop above, as internally, this core always tracks the current time in MCLK
	//cycles. We only need to calculate the current processor time as a timeslice progress
	//value for external interaction.
	stateLastUpdateTime = ConvertMclkCountToAccessTime(GetProcessorStateMclkCurrent());

	//If we stopped at an access slot rather than running until the specified time was
	//reached, return false.
	return !stoppedAtAccessSlot;
}

//----------------------------------------------------------------------------------------
void S315_5313::PerformReadCacheOperation()
{
	//Hardware tests have shown that when doing a read from VSRAM or CRAM, not all the
	//bits in the 16-bit result are set as a result of the read. The bits which are not
	//set as a result of the read from VSRAM or CRAM, are set based on the next available
	//entry in the FIFO buffer. There is one interesting and quite special case to note
	//however. If a write is made to the data port, immediately followed by a read, and
	//the write itself has not yet been processed by the time the read is attempted, due
	//to no access slot being available, the read is processed BEFORE the write. The
	//written data overwrites the contents of the next available entry in the FIFO buffer,
	//and uninitialized bits in the read data now get set based on the corresponding bits
	//in the written data. This can be observed by attempting to write to a read target,
	//immediately followed by a read. We emulate this behaviour here by loading the first
	//pending data port write, if one exists, into the FIFO buffer at the next available
	//entry, then using the next available entry in the FIFO as the initial value for the
	//read operation. The data port write will then be processed later, and perform the
	//same write into the FIFO buffer, resulting in no change, and it will then be
	//processed as normal.
	//##TODO## I now suspect the above theory is incorrect. We have found from additional
	//hardware testing that, generally speaking, after writing to a read target,
	//attempting a read locks up the hardware. I suspect the exception to this rule, which
	//we have observed, happens when a read pre-cache is in progress or complete at the
	//time the write occurs. In this case, the next read works, with the returned value
	//possibly being combined with the invalid write data rather than the previous FIFO
	//contents. After this read however, the following read will cause a lock-up. The
	//exact reason for this lockup is unknown, but for our purposes, we will assume CD4 is
	//set after a data port write occurs.
	//if(!IsWriteFIFOEmpty())
	//{
	//	fifoBuffer[fifoNextReadEntry] = *pendingDataPortWrites.begin();
	//}

	//Note that we have confirmed that a VRAM read operation doesn't use the actual write
	//FIFO to store the read value. This has been tested on the hardware by alternating
	//between VRAM, CRAM, and VSRAM read operations, and observing the resulting values of
	//the uninitialized bits from CRAM and VSRAM reads. The results of these tests show
	//that the contents of the FIFO is not modified as a result of a read operation, it is
	//merely the uninitialized bits in the read data which obtain their value from the
	//next entry in the FIFO. The fact that reads are processed before writes, as detailed
	//above, also confirms that the data from the FIFO and the read data is combined at
	//the time the read occurs, IE, it is not the live state of the FIFO that the read
	//value is combined with at the time the data is being output over the data port, as
	//when a write has been combined with a read as described above, the result is the
	//same, no matter how long after the write has occurred the user actually then reads
	//the data port to obtain the latched read value.
	//##TODO## Confirm the above assertion about the live state of the FIFO not being used
	//at the time the read value is output over the data port. Re-running our original
	//test with the NOP operations inserted should give us the answer.
	if(!readDataHalfCached)
	{
		readBuffer = fifoBuffer[fifoNextReadEntry].dataPortWriteData;
	}

	//All possible combinations of the code flags and data port reads have been tested on
	//the hardware. Reads are decoded based on the lower 5 bits of the code data.
	RAMAccessTarget ramAccessTarget;
	ramAccessTarget.AccessTime(GetProcessorStateMclkCurrent());
	switch(commandCode.GetDataSegment(0, 5))
	{
	case 0x00:{ //?00000 VRAM Read
		//Note that hardware tests have shown that the LSB of the address is ignored for
		//16-bit reads from VRAM.
		Data tempAddress(commandAddress);
		tempAddress.SetBit(0, false);

		//##NOTE## Hardware tests have shown that the upper byte of the 16-bit value is
		//read first. This has been observed by examining the results when performing
		//reads from odd VRAM addresses. It appears that when data is read from an odd
		//address, the flag is set indicating that read data is available, even though the
		//data has actually only been half cached. If a data port read is then made before
		//the other byte is cached, the read will return a data value where only the upper
		//byte of the result comes from the target address, and the lower byte of the
		//result retains whatever the previous value was from the last read value to
		//successfully cache a lower byte.
		//##TODO## Implement these results into the way we perform reads.
		//##TODO## Comment what's going on here with the read operations. The
		//M5ReadVRAM8Bit() function inverts the LSB of the address, so this is a bit
		//confusing.
		if(!readDataHalfCached)
		{
			Data tempDataBuffer(8);
			M5ReadVRAM8Bit(tempAddress+1, tempDataBuffer, ramAccessTarget);
			readBuffer.SetByteFromBottomUp(1, tempDataBuffer.GetData());
			readDataHalfCached = true;
		}
		else
		{
			M5ReadVRAM8Bit(tempAddress, readBuffer, ramAccessTarget);
			readDataHalfCached = false;
			readDataAvailable = true;
		}
		break;}
	case 0x04: //?00100 VSRAM Read
		M5ReadVSRAM(commandAddress, readBuffer, ramAccessTarget);
		readDataAvailable = true;
		break;
	case 0x08: //?01000 CRAM Read
		M5ReadCRAM(commandAddress, readBuffer, ramAccessTarget);
		readDataAvailable = true;
		break;
	case 0x0C: //?01100 8-bit VRAM Read (undocumented)
		//This undocumented read mode performs a true 8-bit VRAM read. The lower 8 bits
		//return an 8-bit value read from VRAM, while the upper 8 bits are unaffected.
		M5ReadVRAM8Bit(commandAddress, readBuffer, ramAccessTarget);
		readDataAvailable = true;
		break;
	default: //Invalid
		//##TODO## Update these comments, and the way we handle invalid read attempts.
		//Any attempts to read from the data port when the lower five bits don't match one
		//of the above patterns causes the VDP to lock up. A reset is unable to restore
		//normal operation. Only power cycling the device can bring the VDP back from this
		//state.
		//Update cached data for a read operation. Note that on the real VDP, attempting
		//to read from an invalid target causes the system to lock up when reading from
		//the data port. The reason this occurs is that the VDP never successfully fetches
		//a data word for the read request, so the data port read is waiting for the
		//readDataAvailable flag to be set, which never actually occurs in this case. We
		//catch cases where this would occur in our emulator at the time the data port is
		//read, so we can report the error to the user, and avoid the infinite loop that
		//would otherwise occur.
		//##TODO## Raise some kind of hard error when this occurs.
		break;
	}

	//##FIX## Is this correct? We need to sort out how we track incremented address
	//register data for operations such as reads and DMA fill/transfer operations.
	//Increment the target address
	if(!readDataHalfCached)
	{
		commandAddress += autoIncrementData;
	}
}

//----------------------------------------------------------------------------------------
void S315_5313::PerformFIFOWriteOperation()
{
	//##TODO## Update all the comments here
	FIFOBufferEntry& fifoBufferEntry = fifoBuffer[fifoNextReadEntry];

	//Move the pending write through the physical 4-word FIFO. The physical FIFO is only
	//maintained to correctly support old data being combined and blended with data being
	//read from CRAM or VSRAM in the uninitialized bits.
	//fifoBuffer[fifoNextReadEntry] = data;
	//fifoNextReadEntry = (fifoNextReadEntry+1) % fifoBufferSize;

	//Process the write
	//##FIX## We know from VRAM snooping as well as the official documentation, that
	//access to VRAM is byte-wide, while access to CRAM and VSRAM is word-wide. What this
	//means is it actually takes two access clock cycles to read a word from VRAM, and it
	//takes two access clock cycles to write a word to VRAM. We know from hardware tests
	//that only a single FIFO entry is used to store a full word-wide write to VRAM, so
	//there must be some kind of internal flag which records whether only one byte has
	//been written to VRAM from a pending word-wide write. Note that most likely, the
	//lower byte is written first, owing to the byteswapped layout in VRAM, but this
	//should be tested in hardware by snooping on the VRAM bus during the write operation.

	//All possible combinations of the code flags and data port writes have been tested
	//on the hardware. Writes are decoded based on the lower 4 bits of the code data.
	RAMAccessTarget ramAccessTarget;
	ramAccessTarget.AccessTime(GetProcessorStateMclkCurrent());
	switch(fifoBufferEntry.codeRegData.GetDataSegment(0, 4))
	{
	case 0x01:{ //??0001 VRAM Write
		//-Hardware tests have verified that if the LSB of the address is set, it is
		//ignored when determining the target VRAM address, but it acts as a special flag
		//causing the data to be byteswapped for writes to VRAM. This is true for any
		//write to VRAM, including writes performed as part of a DMA transfer. The LSB of
		//the address is ignored for reads from VRAM, IE, no byteswapping is ever
		//performed on reads.
		//-It should be noted that the real VDP actually stores all VRAM data byteswapped
		//in the physical memory. This has been confirmed by using a logic analyzer to
		//snoop on the VRAM bus during operation. This means that in reality, byteswapping
		//on VRAM writes really occurs when the LSB is unset, while the data is
		//byteswapped if the LSB is set, and all reads are byteswapped. We don't byteswap
		//the actual VRAM data in our emulator, as not only is this byteswapping virtually
		//transparent to the caller except in the case of DMA fills and copies (refer to
		//the implementation for further info), it would be slower for us to byteswap
		//everything on every read and write to VRAM. Since it's faster for us, and more
		//convenient and logical for the user therefore for the data to be stored
		//sequentially, we don't store data as byteswapped in VRAM.
		//-Note that the real VDP also stores the VRAM data in a non-linear fashion, with
		//data within each 0x400 byte block stored in an interleaved format. The
		//byteswapped data is striped in 4-byte groups within each 0x400 byte block, with
		//all the first bytes of each 4-byte set at 0x000-0x100, then the second bytes at
		//0x100-0x200, and so on within each 0x400 byte block. This is necessary in order
		//to support the serial access mode used to read data from VRAM. We also don't
		//implement this interleaved VRAM in our emulator, as it is an implementation
		//detail that has no effect on the logical operation of the VDP.

		//Calculate the VRAM address to read from. Note that the LSB of the address data
		//is discarded, and instead, it is set based on whether we're writing the first or
		//the second byte in a word-wide write operation. This is as per the confirmed
		//behaviour of the real hardware. 16-bit writes to VRAM are always aligned to a
		//16-bit boundary.
		Data tempAddress(fifoBufferEntry.addressRegData);
		tempAddress.SetBit(0, fifoBufferEntry.dataWriteHalfWritten);

		//Calculate the data to be written to VRAM. Note that the LSB of the original
		//VRAM write address specified by the caller is used here to select which byte of
		//the 16-bit data to write to VRAM first. If the bit is clear, the lower byte of
		//the 16-bit value is written first, otherwise, the upper byte of the 16-bit value
		//is written. Note that this is as per the observed behaviour of the real
		//hardware. The apparent lower byte of each 16-bit value is written before the
		//upper byte, IE, if a 16-bit write was occurring at VRAM address 0x20, the byte
		//at address 0x21 would first be written, followed by address 0x20. This is how it
		//appears to the user, however, since the real VRAM data is byteswapped, it's
		//actually writing the data sequentially. Note that we pass a sequential address
		//to our 8-bit VRAM write function below, however, this function inverts the LSB
		//of the target address internally, so the target address will be byteswapped
		//before the write occurs.
		unsigned int dataByteToRead = (fifoBufferEntry.addressRegData.GetBit(0) ^ fifoBufferEntry.dataWriteHalfWritten)? 1: 0;
		Data writeData(8, fifoBufferEntry.dataPortWriteData.GetByteFromBottomUp(dataByteToRead));

		//Perform the VRAM write
		M5WriteVRAM8Bit(tempAddress, writeData, ramAccessTarget);

		//If we've just written the first half of the word-wide write operation, flag that
		//the first half has been completed, and that the second half of the write is
		//still pending, otherwise flag that the write has been completed.
		fifoBufferEntry.dataWriteHalfWritten = !fifoBufferEntry.dataWriteHalfWritten;
		fifoBufferEntry.pendingDataWrite = fifoBufferEntry.dataWriteHalfWritten;
		break;}
	case 0x03: //??0011 CRAM Write
		M5WriteCRAM(fifoBufferEntry.addressRegData, fifoBufferEntry.dataPortWriteData, ramAccessTarget);
		fifoBufferEntry.pendingDataWrite = false;
		break;
	case 0x05: //??0101 VSRAM Write
		M5WriteVSRAM(fifoBufferEntry.addressRegData, fifoBufferEntry.dataPortWriteData, ramAccessTarget);
		fifoBufferEntry.pendingDataWrite = false;
		break;
	default: //Invalid
		//Any attempts to write to the data port when the lower four bits don't match one
		//of the above patterns has no effect. The write is discarded, and the VDP is
		//unaffected.
		fifoBufferEntry.pendingDataWrite = false;
		break;
	}

	//If this pending write has been fully processed, advance to the next entry in the
	//FIFO buffer.
	if(!fifoBufferEntry.pendingDataWrite)
	{
		fifoNextReadEntry = (fifoNextReadEntry+1) % fifoBufferSize;

		//##TODO## CD4 is most likely not set by a DMA fill operation. We can test this
		//though. Try doing a DMA fill to a read target, with a non-empty FIFO at the time
		//the control port data is written. This should trigger an immediate DMA fill
		//operation to a read target, which should do nothing at all. After this, we can
		//attempt a read. Actually, this will probably lock up. Bad things happen when you
		//mix read and write operations. Still, if it locks up, that's enough evidence to
		//indicate that CD4 is not set as a result of a DMA fill operation.
		if(commandCode.GetBit(5) && dmd1 && !dmd0)
		{
			dmaFillOperationRunning = true;
		}

		//Check if a DMA transfer operation is in progress, and the transfer is stalled
		//waiting for a slot to open up in the write FIFO. This will be the case if there
		//is currently a value held in the DMA transfer read cache. In this case, we could
		//not have performed any more external memory reads before this point. Since we
		//just made a write slot available in the FIFO, we set the dmaTransferNextReadMclk
		//variable to the current processor state time, so that the next external memory
		//read for the DMA transfer operation will start no sooner than this time.
		if(commandCode.GetBit(5) && !dmd1 && dmaTransferReadDataCached)
		{
			unsigned int processorStateMclkCurrent = GetProcessorStateMclkCurrent();
			if(dmaTransferLastTimesliceUsedReadDelay == 0)
			{
				dmaTransferNextReadMclk = processorStateMclkCurrent;
			}
			else if(dmaTransferLastTimesliceUsedReadDelay >= processorStateMclkCurrent)
			{
				dmaTransferNextReadMclk = 0;
				dmaTransferLastTimesliceUsedReadDelay -= processorStateMclkCurrent;
			}
			else
			{
				dmaTransferNextReadMclk = (processorStateMclkCurrent - dmaTransferLastTimesliceUsedReadDelay);
				dmaTransferLastTimesliceUsedReadDelay = 0;
			}
		}
	}
}

//----------------------------------------------------------------------------------------
void S315_5313::PerformDMACopyOperation()
{
	//Get the current source address
	unsigned int sourceAddress = (dmaSourceAddressByte1) | (dmaSourceAddressByte2 << 8);

	//Manipulate the source and target addresses for the DMA copy operation.
	//-Note that on the real hardware, the VDP stores its data in VRAM with each 16-bit
	//word byteswapped, so a value of 0x1234 would be stored in the physical memory as
	//0x3412. This has been confirmed through the use of a logic analyzer snooping on the
	//VRAM bus during operation. For most operations, this byteswapping of VRAM memory is
	//transparent to the user, since the VDP automatically byteswaps all word-wide reads
	//and writes to and from VRAM, so the data is read and written as if it was not
	//byteswapped at all. DMA fill and copy operations are the only time where the
	//byteswapping behaviour of the real hardware is visible to the user, as the byte-wide
	//VRAM access that occurs as part of these operations allow reads and writes to and
	//from odd addresses in VRAM. In the real hardware, these addresses are used directly,
	//without modification, to read and write the byteswapped data, meaning that reads and
	//writes from odd addresses actually access the upper byte of a word, and reads and
	//writes to even addresses actually access to the lower byte of the word. For our
	//emulator, we store data in VRAM without byteswapping, to simplify the implementation
	//and present the data to the user in the form they would expect when using the
	//debugger. In order to correctly implement the behaviour of a DMA fill or copy
	//however, we therefore have to swap odd and even addresses when performing byte-wide
	//access, so that we get the correct result.
	Data sourceAddressByteswapped(16, sourceAddress);
	sourceAddressByteswapped.SetBit(0, !sourceAddressByteswapped.GetBit(0));
	Data targetAddressByteswapped(16, commandAddress.GetData());
	targetAddressByteswapped.SetBit(0, !targetAddressByteswapped.GetBit(0));

	//Ensure that CD4 is set when performing a DMA copy. Hardware tests have shown that
	//while the state of CD0-CD3 is completely ignored for DMA copy operations, CD4 must
	//be set, otherwise the VDP locks up hard when the DMA operation is triggered.
	if(!commandCode.GetBit(4))
	{
		//##TODO## Log an error or trigger an assert here
	}

	//Perform the copy. Note that hardware tests have shown that DMA copy operations
	//always target VRAM, regardless of the state of CD0-CD3.
	RAMAccessTarget ramAccessTarget;
	ramAccessTarget.AccessTime(GetProcessorStateMclkCurrent());
	unsigned char data;
	data = vram->Read(sourceAddressByteswapped.GetData(), ramAccessTarget);
	vram->Write(targetAddressByteswapped.GetData(), data, ramAccessTarget);

	//Increment the target address
	commandAddress += autoIncrementData;
}

//----------------------------------------------------------------------------------------
void S315_5313::PerformDMAFillOperation()
{
	//##FIX## We need to determine how the VDP knows a write has been made to the data
	//port. VSRAM and CRAM fill targets grab the next available entry in the FIFO, after
	//the write has been made and the FIFO has been advanced, so they perform the fill
	//operation with the wrong data. A VRAM fill target uses the correct data, but it
	//appears the write is still processed first, possibly even both halves of the write.
	//Further testing is required.
	//##TODO## Hardware tests have shown that if writes are pending in the FIFO at the
	//time a control port write sets up a DMA fill operation, the DMA fill operation will
	//trigger without waiting for a data port write, and will use an entry from the
	//current FIFO buffer instead. Exactly which entry is selected, and whether the DMA
	//fill triggers on the next write or the last write, still needs to be confirmed
	//however.
	//Only advance a DMA fill operation if the FIFO is not empty
	//##FIX## The write needs to be processed first
	//##FIX## Hardware tests have shown that if the data port is written to while a DMA
	//fill operation is already in progress, the data port write is processed immediately
	//at the next access slot, before the fill operation is advanced. The data port write
	//occurs at the current incremented address that the DMA fill operation was up to, and
	//the DMA fill operation continues from the incremented address after the write has
	//been processed. We need to emulate this behaviour. It is also clear from this that
	//pending write entries in the FIFO need to be processed before DMA fill update steps.

	//Manipulate the source target address for the DMA fill operation.
	//-Note that on the real hardware, the VDP stores its data in VRAM with each 16-bit
	//word byteswapped, so a value of 0x1234 would be stored in the physical memory as
	//0x3412. This has been confirmed through the use of a logic analyzer snooping on the
	//VRAM bus during operation. For most operations, this byteswapping of VRAM memory is
	//transparent to the user, since the VDP automatically byteswaps all word-wide reads
	//and writes to and from VRAM, so the data is read and written as if it was not
	//byteswapped at all. DMA fill and copy operations are the only time where the
	//byteswapping behaviour of the real hardware is visible to the user, as the byte-wide
	//VRAM access that occurs as part of these operations allow reads and writes to and
	//from odd addresses in VRAM. In the real hardware, these addresses are used directly,
	//without modification, to read and write the byteswapped data, meaning that reads and
	//writes from odd addresses actually access the upper byte of a word, and reads and
	//writes to even addresses actually access to the lower byte of the word. For our
	//emulator, we store data in VRAM without byteswapping, to simplify the implementation
	//and present the data to the user in the form they would expect when using the
	//debugger. In order to correctly implement the behaviour of a DMA fill or copy
	//however, we therefore have to swap odd and even addresses when performing byte-wide
	//access, so that we get the correct result.
	//-Note that the official documentation from Sega listing the order the data is
	//written to VRAM during a DMA fill operation is technically accurate, but is very
	//misleading, since the order and pattern of the writes that they list shows the
	//actual write pattern to the byteswapped memory, with the order of all bytes being
	//reversed to what the reader may expect. At no point in the official documentation is
	//it mentioned that the data in VRAM is byteswapped.
	//Data targetAddressByteswapped(16, commandAddress.GetData());
	//targetAddressByteswapped.SetBit(0, !targetAddressByteswapped.GetBit(0));

	//Check if this DMA fill operation is targeting VRAM. Right now, we have special case
	//handling for VRAM write targets as opposed to CRAM or VSRAM write targets, as there
	//are some implementation quirks in the VDP that result in different behaviour for
	//CRAM or VSRAM targets.
	//##TODO## Find a way to unify this code, and end up with a clean implementation.
	bool fillTargetsVRAM = (commandCode.GetDataSegment(0, 4) == 0x01);

	//Increment the target address for the last entry
	unsigned int fifoLastReadEntry = (fifoNextReadEntry+(fifoBufferSize-1)) % fifoBufferSize;
	fifoBuffer[fifoLastReadEntry].addressRegData += autoIncrementData;

	//##TODO## We need to determine exactly how the VDP latches the DMA fill data. It
	//seems to me, the most likely explanation is that a DMA fill is triggered after the
	//first half of a data port write is written, and then kicks in and repeats the second
	//half of that write repeatedly. Since a write to CRAM or VSRAM is 16-bit, and
	//completes in one step, this results in the FIFO being advanced, and the entire write
	//being repeated.
	Data fillData(16);
	if(fillTargetsVRAM)
	{
		//Extract the upper byte of the written data. This single byte of data is used to
		//perform the fill. Hardware tests have shown no other data gets written to the
		//FIFO using a DMA fill operation other than the normal write that triggers the
		//fill, and the FIFO is not advanced at all during the fill. Also note that the
		//byteswapping behaviour of VRAM writes has no impact on which data is used for
		//the fill operation. The upper byte of the data written to the data port is
		//always used for the fill, regardless of whether the write is performed to an
		//even or odd VRAM address.
		fillData = fifoBuffer[fifoLastReadEntry].dataPortWriteData.GetUpperBits(8);
	}
	else
	{
		//##FIX## Hardware tests have shown that when a DMA fill is being performed to
		//CRAM or VSRAM, the write to the data port isn't used as the fill data for the
		//operation. What happens instead is that the data write is performed as normal,
		//and the FIFO is advanced, then when the DMA fill operation triggers, it is the
		//data in the next available FIFO slot that is used for the fill data. This only
		//occurs for DMA fills to CRAM or VSRAM, and is no doubt due to the fact that VRAM
		//requires two byte-wide writes to commit a single word-wide write to the VRAM,
		//while CRAM and VSRAM perform a single word-wide operation.
		fillData = fifoBuffer[fifoNextReadEntry].dataPortWriteData;
	}

	//##NOTE## Hardware tests have shown that during a DMA fill operation, the FIFO flags
	//in the status register remain with the empty flag set, and the full flag cleared,
	//throughout the fill operation, with of course the exception of when the data port
	//write occurs to trigger the fill, if that write is waiting in the FIFO at the time
	//of the control port read.
	//Note that the DMA busy flag remains set as soon as the control port is written, even
	//if the data port write to trigger the fill hasn't yet been made. The DMA busy flag
	//remains set until a data port write has been made and the DMA fill is complete.
	//##FIX## Hardware tests have shown that DMA fill operations to CRAM and VSRAM are
	//possible, and work correctly, with the exception that the wrong data is used for the
	//fill, as described above. We need to emulate this by actually using the specified
	//write target when performing the write below. Also note that a single DMA fill write
	//is a 16-bit operation to VSRAM and CRAM, with the full 16-bit fill value being used
	//at each target address, and the LSB of the address being masked.
	//Transfer the next byte of the fill operation
	//##TODO## Test on hardware to determine what happens when the data port is written to
	//while a DMA fill operation is in progress.
	RAMAccessTarget ramAccessTarget;
	ramAccessTarget.AccessTime(GetProcessorStateMclkCurrent());
	switch(commandCode.GetDataSegment(0, 4))
	{
	case 0x01: //??0001 VRAM Write
		M5WriteVRAM8Bit(fifoBuffer[fifoLastReadEntry].addressRegData, fillData, ramAccessTarget);
		break;
	case 0x03: //??0011 CRAM Write
		M5WriteCRAM(fifoBuffer[fifoLastReadEntry].addressRegData, fillData, ramAccessTarget);
		break;
	case 0x05: //??0101 VSRAM Write
		M5WriteVSRAM(fifoBuffer[fifoLastReadEntry].addressRegData, fillData, ramAccessTarget);
		break;
	}

	//##FIX## This is incorrect. We know from testing that if a data port write occurs
	//while a DMA fill operation is in progress, that write occurs at the currently
	//incremented address that the next fill write would go to, and the fill operation
	//proceeds at the incremented address after the write.
	//##NOTE## Fixing this could be as simple as setting the
	//codeAndAddressRegistersModifiedSinceLastWrite register to true here, which should
	//trigger the next data port write to obtain its code and address register data
	//directly from commandAddress and commandCode, rather than using the previous FIFO
	//buffer entry contents. This won't work fully actually, since the DMA fill operation
	//then needs to continue at the incremented address after the write. It does therefore
	//seem as if the actual address data that is used and modified actively by the DMA
	//fill update step is the address data stored in the FIFO entry which triggered the
	//fill. I just noticed above that this is actually what we are using. Why are we
	//incrementing commandAddress here then? We don't actually seem to be using it
	//anywhere. We should update our code, and our comments.
	//Increment the target address
	commandAddress += autoIncrementData;
}

//----------------------------------------------------------------------------------------
void S315_5313::CacheDMATransferReadData(unsigned int mclkTime)
{
	//Get the current source address. Note that for a DMA transfer operation, the source
	//address is stored without the LSB, so we need to shift the complete address data up
	//by one.
	unsigned int sourceAddress = (dmaSourceAddressByte1 << 1) | (dmaSourceAddressByte2 << 9) | (dmaSourceAddressByte3 << 17);

	//Read the next data word to transfer from the source
	memoryBus->ReadMemory(sourceAddress, dmaTransferReadCache, GetDeviceContext(), dmaTransferNextReadMclk, (unsigned int)AccessContext::DMARead);

	//Flag that data has been cached for the DMA transfer operation
	dmaTransferReadDataCached = true;
}

//----------------------------------------------------------------------------------------
void S315_5313::PerformDMATransferOperation()
{
	//Add the data write to the FIFO
	//-Note that we can grab the current working value for the command code and address
	//register data, since we know we obtained exclusive bus access when the DMA transfer
	//command word was written.
	//-Note that a DMA transfer will be triggered regardless of the state of CD4, as this
	//bit isn't tested when deciding whether to trigger an external DMA transfer, and
	//write target decoding only looks at the state of CD3-CD0.
	//-Note that hardware testing has confirmed that DMA transfers move the data through
	//the FIFO in the same manner as normal VRAM write operations, with the same rules for
	//memory wrapping and byte swapping.
	//-Note that genvdp.txt by Charles MacDonald reports that DMA transfers to CRAM are
	//aborted when the target address passes 0x7F. This is incorrect. The palette
	//corruption reported in "Batman and Robin" on the Mega Drive is due to the fact that
	//DMA operations actively update the DMA source count registers as the operation is
	//being performed, meaning that no matter what transfer count was used, the DMA length
	//registers should both be 0 after the DMA operation is completed. Batman and Robin
	//only writes to the lower transfer count byte when performing some transfers to CRAM,
	//meaning that unless the upper byte is already 0, too much data will be transferred
	//to CRAM, corrupting the palette. If the DMA source and DMA length registers are
	//correctly updated by DMA operations, as hardware tests have proven does occur, this
	//bug will not occur.
	if(codeAndAddressRegistersModifiedSinceLastWrite)
	{
		fifoBuffer[fifoNextWriteEntry].codeRegData = commandCode;
		fifoBuffer[fifoNextWriteEntry].addressRegData = commandAddress;
		codeAndAddressRegistersModifiedSinceLastWrite = false;
	}
	else
	{
		unsigned int fifoLastWriteEntry = (fifoNextWriteEntry+(fifoBufferSize-1)) % fifoBufferSize;
		fifoBuffer[fifoNextWriteEntry].codeRegData = fifoBuffer[fifoLastWriteEntry].codeRegData;
		fifoBuffer[fifoNextWriteEntry].addressRegData = fifoBuffer[fifoLastWriteEntry].addressRegData + autoIncrementData;
	}
	fifoBuffer[fifoNextWriteEntry].dataPortWriteData = dmaTransferReadCache;
	fifoBuffer[fifoNextWriteEntry].dataWriteHalfWritten = false;
	fifoBuffer[fifoNextWriteEntry].pendingDataWrite = true;

	//Advance the FIFO to the next slot
	fifoNextWriteEntry = (fifoNextWriteEntry+1) % fifoBufferSize;

	//Now that the cached DMA transfer data has been written to the FIFO, clear the flag
	//indicating that DMA transfer data is currently cached.
	dmaTransferReadDataCached = false;
}

//----------------------------------------------------------------------------------------
void S315_5313::AdvanceDMAState()
{
	//Decrement the DMA transfer count registers. Note that the transfer count is
	//decremented before it is tested against 0, so a transfer count of 0 is equivalent to
	//a transfer count of 0x10000.
	dmaLengthCounter = (dmaLengthCounter - 1) & 0xFFFF;

	//Increment the DMA source address registers. Note that all DMA operations cause the
	//DMA source address registers to be advanced, including a DMA fill operation, even
	//though a DMA fill doesn't actually use the source address. This has been confirmed
	//through hardware tests. Also note that only the lower two DMA source address
	//registers are modified. Register 0x17, which contains the upper 7 bits, of the
	//source address for a DMA transfer operation, is not updated, which prevents a DMA
	//transfer operation from crossing a 0x20000 byte boundary. This behaviour is
	//undocumented but well known, and has been verified through hardware tests.
	unsigned int incrementedDMASourceAddress = dmaSourceAddressByte1 | (dmaSourceAddressByte2 << 8);
	++incrementedDMASourceAddress;
	dmaSourceAddressByte1 = incrementedDMASourceAddress & 0xFF;
	dmaSourceAddressByte2 = (incrementedDMASourceAddress >> 8) & 0xFF;

	//If the DMA length counter is 0 after a DMA operation has been advanced, we've
	//reached the end of the DMA operation. In this case, we clear CD5. This flags the DMA
	//operation as completed. If we're in a DMA transfer operation, this will also cause
	//the bus to be released by the DMA worker thread.
	if(dmaLengthCounter == 0)
	{
		commandCode.SetBit(5, false);
		SetStatusFlagDMA(false);

		//If we were running a DMA fill or DMA transfer operation, flag that it is now
		//completed.
		dmaFillOperationRunning = false;
		dmaTransferActive = false;
	}

	//Flag that the cached DMA settings have been modified
	cachedDMASettingsChanged = true;
}

//----------------------------------------------------------------------------------------
bool S315_5313::TargetProcessorStateReached(bool stopWhenFifoEmpty, bool stopWhenFifoFull, bool stopWhenFifoNotFull, bool stopWhenReadDataAvailable, bool stopWhenNoDMAOperationInProgress)
{
	//Check if a DMA operation is currently running
	bool dmaOperationRunning = commandCode.GetBit(5) && (!dmd1 || dmd0 || dmaFillOperationRunning);

	//Check if we've reached one of the target processor states
	bool targetProcessorStateReached = false;
	if(stopWhenFifoEmpty && GetStatusFlagFIFOEmpty())
	{
		targetProcessorStateReached = true;
	}
	else if(stopWhenFifoFull && GetStatusFlagFIFOFull())
	{
		targetProcessorStateReached = true;
	}
	else if(stopWhenFifoNotFull && !GetStatusFlagFIFOFull())
	{
		targetProcessorStateReached = true;
	}
	else if(stopWhenReadDataAvailable && readDataAvailable)
	{
		targetProcessorStateReached = true;
	}
	else if(stopWhenNoDMAOperationInProgress && !dmaOperationRunning)
	{
		targetProcessorStateReached = true;
	}

	//Return the result of the processor state check
	return targetProcessorStateReached;
}

//----------------------------------------------------------------------------------------
double S315_5313::GetProcessorStateTime() const
{
	return stateLastUpdateTime;
}

//----------------------------------------------------------------------------------------
unsigned int S315_5313::GetProcessorStateMclkCurrent() const
{
	return (stateLastUpdateMclk + stateLastUpdateMclkUnused) - stateLastUpdateMclkUnusedFromLastTimeslice;
}

//----------------------------------------------------------------------------------------
//FIFO functions
//----------------------------------------------------------------------------------------
bool S315_5313::IsWriteFIFOEmpty() const
{
	//The FIFO is empty if the next FIFO entry for read doesn't have a write pending
	return !fifoBuffer[fifoNextReadEntry].pendingDataWrite;
}

//----------------------------------------------------------------------------------------
bool S315_5313::IsWriteFIFOFull() const
{
	//The FIFO is full if the first and last FIFO entries for reading both have a write
	//pending
	unsigned int fifoLastReadEntry = (fifoNextReadEntry+(fifoBufferSize-1)) % fifoBufferSize;
	return fifoBuffer[fifoNextReadEntry].pendingDataWrite && fifoBuffer[fifoLastReadEntry].pendingDataWrite;
}

//----------------------------------------------------------------------------------------
//Mode 5 control functions
//----------------------------------------------------------------------------------------
bool S315_5313::ValidReadTargetInCommandCode() const
{
	//Return true if bits 0, 1, and 4 of the commandCode register are not set. See the
	//mapping of commandCode data to valid read targets in the ReadVideoMemory function to
	//see why this is a valid test.
	return (commandCode & 0x13) == 0;
}

//----------------------------------------------------------------------------------------
void S315_5313::M5ReadVRAM8Bit(const Data& address, Data& data, const RAMAccessTarget& accessTarget)
{
	//Note that in the case of 8-bit VRAM reads, the data is directly read from VRAM at
	//the literal address referenced by the address register. Note however that since the
	//actual VDP stores data in VRAM as byteswapped 16-bit values, this means that reads
	//from odd addresses return the even byte of a 16-bit value, and reads from an even
	//address return the odd byte. Since we don't byteswap the VRAM data in our emulation
	//core, we need to invert the LSB of the address here to get the same result as the
	//real hardware.
	Data tempAddress(address);
	tempAddress.SetBit(0, !tempAddress.GetBit(0));

	//Read the data. Only a single 8-bit read is performed from VRAM in this case. The
	//upper 8 bits retain their previous value.
	//##TODO## Snoop on the VRAM bus to confirm only a single byte is read for this target
	data.SetByteFromBottomUp(0, vram->Read(tempAddress.GetData(), accessTarget));
}

//----------------------------------------------------------------------------------------
void S315_5313::M5ReadCRAM(const Data& address, Data& data, const RAMAccessTarget& accessTarget)
{
	//Note that hardware tests have shown that not only is the LSB of the address ignored
	//for both reads and writes to CRAM and VSRAM, but the upper 9 bits of the address is
	//also masked. The address value wraps to be within the range 0x00-0x7E, where the LSB
	//of the address is always clear.
	unsigned int tempAddress = address.GetData() & 0x7E;

	//Read the data. Note that not all bits in the 16-bit result are affected by the read
	//from CRAM. Hardware tests have shown that CRAM reads return a 9-bit value, with the
	//mask 0x0EEE. The remaining bits retain their previous value, corresponding with the
	//existing value in the FIFO buffer that the read data is being saved into.
	unsigned int dataMask = 0x0EEE;
	Data tempData(16);
	tempData.SetByteFromBottomUp(1, cram->Read(tempAddress, accessTarget));
	tempData.SetByteFromBottomUp(0, cram->Read(tempAddress+1, accessTarget));
	data = (data & ~dataMask) | (tempData & dataMask);
}

//----------------------------------------------------------------------------------------
void S315_5313::M5ReadVSRAM(const Data& address, Data& data, const RAMAccessTarget& accessTarget)
{
	//Note that hardware tests have shown that not only is the LSB of the address ignored
	//for both reads and writes to CRAM and VSRAM, but the upper 9 bits of the address is
	//also masked. The address value wraps to be within the range 0x00-0x7E, where the LSB
	//of the address is always clear.
	unsigned int tempAddress = address.GetData() & 0x7E;

	//##TODO## Hardware testing has shown that the VSRAM buffer is 0x50 bytes in size,
	//while the target address into the VSRAM buffer wraps at 0x80 bytes. This leaves 0x30
	//bytes where reads and writes can be attempted, which are beyond the end of the VSRAM
	//buffer. Hardware testing has shown that that the last read value from VSRAM is
	//cached in an internal register, and attempts to read from VSRAM at an invalid
	//address simply return the cached value which remains in the VSRAM read buffer. Note
	//that the actual VDP rendering process itself reads values from VSRAM, and these
	//reads update the VSRAM read buffer.
	//##TODO## Perform hardware tests to confirm whether manual reads from VSRAM update
	//the VSRAM read buffer, or whether it is only populated by the VDP itself during
	//rendering. Also confirm whether writes to VSRAM update the VSRAM read buffer, IE,
	//whether there is a single buffer which is used for both reads and writes. Note that
	//we should be able to determine this by disabling VDP video output, possibly through
	//the normal display enable/disable bit in register 1, or if not, perhaps through the
	//undocumented bit in reg 0 which disables all video output. This should get the VDP
	//out of the way, so we can read VSRAM without the rendering process affecting the
	//VSRAM read buffer.
	//if(tempAddress >= 0x50)
	//{
	//	tempAddress = vsramReadCacheIndex % 0x50;
	//}

	//Read the data. Note that not all bits in the 16-bit result are affected by the
	//read from VSRAM. Hardware tests have shown that VSRAM reads return an 11-bit
	//value, with the mask 0x07FF. The remaining bits retain their previous value,
	//corresponding with the existing value in the FIFO buffer that the read data is
	//being saved into.
	unsigned int dataMask = 0x07FF;
	Data tempData(16);
	if(tempAddress >= 0x50)
	{
		//##TODO## Comment this
		tempData = vsramLastRenderReadCache;
	}
	else
	{
		tempData.SetByteFromBottomUp(1, vsram->Read(tempAddress, accessTarget));
		tempData.SetByteFromBottomUp(0, vsram->Read(tempAddress+1, accessTarget));
	}
	data = (data & ~dataMask) | (tempData & dataMask);

	//##TODO## Determine whether this is correct
	//vsramReadCacheIndex = tempAddress;
}

//----------------------------------------------------------------------------------------
//This target isn't directly accessible as a write target for the VDP, but we use this
//function to implement the two 8-bit halves of a 16-bit VRAM write performed for the
//16-bit VRAM write target, as well as implement DMA fill and copy functionality.
//----------------------------------------------------------------------------------------
void S315_5313::M5WriteVRAM8Bit(const Data& address, const Data& data, const RAMAccessTarget& accessTarget)
{
	//Note that in the case of 8-bit VRAM writes, the data is directly written to VRAM at
	//the literal address referenced by the address register. Note however that since the
	//actual VDP stores data in VRAM as byteswapped 16-bit values, this means that writes
	//to odd addresses modify the even byte of a 16-bit value, and writes to an even
	//address modify the odd byte. Since we don't byteswap the VRAM data in our emulation
	//core, we need to invert the LSB of the address here to get the same result as the
	//real hardware.
	Data tempAddress(address);
	tempAddress.SetBit(0, !tempAddress.GetBit(0));
	unsigned int tempAddressData = tempAddress.GetData();

	//Update the sprite cache if required. The sprite cache is an internal memory buffer
	//which is designed to maintain a mirror of a portion of the sprite attribute table.
	//The first 4 bytes of each 8-byte table entry are stored in the cache. Since the
	//sprite cache is not reloaded when the sprite attribute table address is changed,
	//correct emulation of the cache is required in order to correctly emulate VDP sprite
	//support. Level 6-3 of "Castlevania Bloodlines" on the Mega Drive is known to rely on
	//the sprite cache not being invalidated by a table address change, in order to
	//implement an "upside down" effect.
	if((tempAddressData >= spriteAttributeTableBaseAddressDecoded) //Target address is at or above the start of the sprite table
	&& (tempAddressData < (spriteAttributeTableBaseAddressDecoded + (spriteCacheSize * 2))) //Target address is before the end of the sprite table
	&& ((tempAddressData & 0x4) == 0)) //Target address is within the first 4 bytes of a sprite table entry
	{
		//Calculate the address of this write in the sprite cache. We first convert the
		//target address into a relative byte index into the sprite attribute table, then
		//we strip out bit 2 from the address, to discard addresses in the upper 4 bytes
		//of each table entry, which we filtered out above.
		unsigned int spriteCacheAddress = (tempAddressData - spriteAttributeTableBaseAddressDecoded);
		spriteCacheAddress = ((spriteCacheAddress >> 1) & ~0x3) | (spriteCacheAddress & 0x3);

		//Perform the write to the sprite cache
		spriteCache->Write(spriteCacheAddress, data.GetByteFromBottomUp(0), accessTarget);
	}

	//Write the data
	vram->Write(tempAddressData, data.GetByteFromBottomUp(0), accessTarget);
}

//----------------------------------------------------------------------------------------
void S315_5313::M5WriteCRAM(const Data& address, const Data& data, const RAMAccessTarget& accessTarget)
{
	//Note that hardware tests have shown that not only is the LSB of the address ignored
	//for both reads and writes to CRAM and VSRAM, but the upper 9 bits of the address is
	//also masked. The address value wraps to be within the range 0x00-0x7E, where the LSB
	//of the address is always clear.
	unsigned int tempAddress = address.GetData() & 0x7E;

	//Build a masked data value to write to CRAM. Hardware tests have shown that reads
	//back from CRAM only return data from bits corresponding with the mask 0x0EEE. We
	//mask the data when saving to CRAM here, to make things more sensible when using the
	//debugger. Since the masked bits are almost certainly discarded when saving to CRAM
	//on the real system, we discard them here on save as well, even though it's not
	//technically necessary, since we never attempt to use the invalid bits.
	unsigned int dataMask = 0x0EEE;
	Data tempData(16);
	tempData = data & dataMask;

	//Write the masked data to CRAM
	cram->Write(tempAddress, tempData.GetByteFromBottomUp(1), accessTarget);
	cram->Write(tempAddress+1, tempData.GetByteFromBottomUp(0), accessTarget);
}

//----------------------------------------------------------------------------------------
void S315_5313::M5WriteVSRAM(const Data& address, const Data& data, const RAMAccessTarget& accessTarget)
{
	//Note that hardware tests have shown that not only is the LSB of the address ignored
	//for both reads and writes to CRAM and VSRAM, but the upper 9 bits of the address is
	//also masked. The address value wraps to be within the range 0x00-0x7E, where the LSB
	//of the address is always clear.
	unsigned int tempAddress = address.GetData() & 0x7E;

	//Build a masked data value to write to VSRAM. Hardware tests have shown that reads
	//back from VSRAM only return data from bits corresponding with the mask 0x07FF. We
	//mask the data when saving to VSRAM here, to make things more sensible when using the
	//debugger. Since the masked bits are almost certainly discarded when saving to VSRAM
	//on the real system, we discard them here on save as well, even though it's not
	//technically necessary, since we never attempt to use the invalid bits.
	unsigned int dataMask = 0x07FF;
	Data tempData(16);
	tempData = data & dataMask;

	//##TODO## Comment this, as per the M5ReadVSRAM function, and perform hardware tests
	//to confirm whether VSRAM writes should update the VSRAM read buffer.
	if(tempAddress < 0x50)
	{
		//Write the masked data to VSRAM
		vsram->Write(tempAddress, tempData.GetByteFromBottomUp(1), accessTarget);
		vsram->Write(tempAddress+1, tempData.GetByteFromBottomUp(0), accessTarget);

		//##TODO## Determine whether this is correct
		//vsramReadCacheIndex = tempAddress;
	}
}

//----------------------------------------------------------------------------------------
//Savestate functions
//----------------------------------------------------------------------------------------
bool S315_5313::GetScreenshot(IImage& targetImage) const
{
	//Determine the index of the current image plane that is being used for display
	unsigned int displayingImageBufferPlane = GetImageCompletedBufferPlaneNo();

	//Obtain a read lock on the image buffer
	imageBufferLock[displayingImageBufferPlane].ObtainReadLock();

	//Calculate the width and height of the output image. We take the line width of the
	//first line as the width of the output image, but it should be noted that the width
	//may actually vary between lines, due to mid-frame changes to the screen settings. We
	//handle this below by resampling lines which don't match the image width to the
	//correct width.
	unsigned int imageWidth = imageBufferLineWidth[displayingImageBufferPlane][0];
	unsigned int imageHeight = imageBufferLineCount[displayingImageBufferPlane];

	//Write the current contents of the image buffer to the output image
	targetImage.SetImageFormat(imageWidth, imageHeight, IImage::PIXELFORMAT_RGB, IImage::DATAFORMAT_8BIT);
	for(unsigned int ypos = 0; ypos < imageHeight; ++ypos)
	{
		//Obtain the width of this line in pixels
		unsigned int lineWidth = imageBufferLineWidth[displayingImageBufferPlane][ypos];

		//If the width of this line doesn't match the width of the image, resample this
		//line and write it to the output image, otherwise write the pixel data directly
		//to the output image.
		if(lineWidth != imageWidth)
		{
			Image lineImage(lineWidth, 1, IImage::PIXELFORMAT_RGB, IImage::DATAFORMAT_8BIT);
			for(unsigned int xpos = 0; xpos < lineWidth; ++xpos)
			{
				ImageBufferColorEntry& imageBufferEntry = *((ImageBufferColorEntry*)&imageBuffer[drawingImageBufferPlane][((ypos * imageBufferWidth) + xpos) * 4]);
				lineImage.WritePixelData(xpos, ypos, 0, imageBufferEntry.r);
				lineImage.WritePixelData(xpos, ypos, 1, imageBufferEntry.g);
				lineImage.WritePixelData(xpos, ypos, 2, imageBufferEntry.b);
			}
			lineImage.ResampleBilinear(imageWidth, 1);
			for(unsigned int xpos = 0; xpos < imageWidth; ++xpos)
			{
				unsigned char r;
				unsigned char g;
				unsigned char b;
				lineImage.ReadPixelData(xpos, 0, 0, r);
				lineImage.ReadPixelData(xpos, 0, 1, g);
				lineImage.ReadPixelData(xpos, 0, 2, b);
				targetImage.WritePixelData(xpos, ypos, 0, r);
				targetImage.WritePixelData(xpos, ypos, 1, g);
				targetImage.WritePixelData(xpos, ypos, 2, b);
			}
		}
		else
		{
			for(unsigned int xpos = 0; xpos < imageWidth; ++xpos)
			{
				ImageBufferColorEntry& imageBufferEntry = *((ImageBufferColorEntry*)&imageBuffer[drawingImageBufferPlane][((ypos * imageBufferWidth) + xpos) * 4]);
				targetImage.WritePixelData(xpos, ypos, 0, imageBufferEntry.r);
				targetImage.WritePixelData(xpos, ypos, 1, imageBufferEntry.g);
				targetImage.WritePixelData(xpos, ypos, 2, imageBufferEntry.b);
			}
		}
	}

	//Release the read lock on the image buffer
	imageBufferLock[displayingImageBufferPlane].ReleaseReadLock();

	return true;
}

//----------------------------------------------------------------------------------------
void S315_5313::LoadState(IHierarchicalStorageNode& node)
{
	std::list<IHierarchicalStorageNode*> childList = node.GetChildList();
	for(std::list<IHierarchicalStorageNode*>::iterator i = childList.begin(); i != childList.end(); ++i)
	{
		if((*i)->GetName() == L"Registers")
		{
			reg.LoadState(*(*i));

			//Update any cached register settings
			AccessTarget accessTarget;
			accessTarget.AccessLatest();
			for(unsigned int i = 0; i < registerCount; ++i)
			{
				TransparentRegisterSpecialUpdateFunction(i, GetRegisterData(i, accessTarget));
			}
		}
		else if((*i)->GetName() == L"Register")
		{
			IHierarchicalStorageAttribute* nameAttribute = (*i)->GetAttribute(L"name");
			if(nameAttribute != 0)
			{
				std::wstring registerName = nameAttribute->GetValue();
				//Bus interface
				if(registerName == L"BusGranted")				busGranted = (*i)->ExtractData<bool>();
				else if(registerName == L"PalModeLineState")	palModeLineState = (*i)->ExtractData<bool>();
				else if(registerName == L"ResetLineState")		resetLineState = (*i)->ExtractData<bool>();
				else if(registerName == L"LineStateIPL")		lineStateIPL = (*i)->ExtractData<unsigned int>();
				else if(registerName == L"BusRequestLineState")	busRequestLineState = (*i)->ExtractData<bool>();
				//Clock sources
				else if(registerName == L"ClockMclkCurrent")		clockMclkCurrent = (*i)->ExtractData<double>();
				//Physical registers and memory buffers
				else if(registerName == L"Status")				status = (*i)->ExtractHexData<unsigned int>();
				else if(registerName == L"HCounter")			hcounter = (*i)->ExtractHexData<unsigned int>();
				else if(registerName == L"VCounter")			vcounter = (*i)->ExtractHexData<unsigned int>();
				else if(registerName == L"HCounterLatchedData")	hcounterLatchedData = (*i)->ExtractHexData<unsigned int>();
				else if(registerName == L"VCounterLatchedData")	vcounterLatchedData = (*i)->ExtractHexData<unsigned int>();
				else if(registerName == L"HIntCounter")			hintCounter = (*i)->ExtractHexData<unsigned int>();
				else if(registerName == L"VIntPending")			vintPending = (*i)->ExtractData<bool>();
				else if(registerName == L"HIntPending")			hintPending = (*i)->ExtractData<bool>();
				else if(registerName == L"EXIntPending")		exintPending = (*i)->ExtractData<bool>();
				//Active register settings
				else if(registerName == L"InterlaceEnabled")	interlaceEnabled = (*i)->ExtractData<bool>();
				else if(registerName == L"InterlaceDouble")		interlaceDouble = (*i)->ExtractData<bool>();
				else if(registerName == L"ScreenModeRS0")		screenModeRS0 = (*i)->ExtractData<bool>();
				else if(registerName == L"ScreenModeRS1")		screenModeRS1 = (*i)->ExtractData<bool>();
				else if(registerName == L"ScreenModeV30")		screenModeV30 = (*i)->ExtractData<bool>();
				else if(registerName == L"PalMode")				palMode = (*i)->ExtractData<bool>();
				//FIFO buffer registers
				else if(registerName == L"FIFONextReadEntry")			fifoNextReadEntry = (*i)->ExtractHexData<unsigned int>();
				else if(registerName == L"FIFONextWriteEntry")			fifoNextWriteEntry = (*i)->ExtractHexData<unsigned int>();
				else if(registerName == L"ReadBuffer")					readBuffer = (*i)->ExtractHexData<unsigned int>();
				else if(registerName == L"ReadDataAvailable")			readDataAvailable = (*i)->ExtractData<bool>();
				else if(registerName == L"ReadDataHalfCached")			readDataHalfCached = (*i)->ExtractData<bool>();
				else if(registerName == L"DMAFillOperationRunning")		dmaFillOperationRunning = (*i)->ExtractData<bool>();
				else if(registerName == L"VSRAMReadCacheIndex")			vsramReadCacheIndex = (*i)->ExtractHexData<unsigned int>();
				else if(registerName == L"VSRAMLastRenderReadCache")	vsramLastRenderReadCache = (*i)->ExtractHexData<unsigned int>();
				//Update state
				else if(registerName == L"CurrentTimesliceLength")					currentTimesliceLength = (*i)->ExtractData<double>();
				else if(registerName == L"LastTimesliceMclkCyclesRemainingTime")	lastTimesliceMclkCyclesRemainingTime = (*i)->ExtractData<double>();
				else if(registerName == L"CurrentTimesliceMclkCyclesRemainingTime")	currentTimesliceMclkCyclesRemainingTime = (*i)->ExtractData<double>();
				else if(registerName == L"LastTimesliceMclkCyclesOverrun")			lastTimesliceMclkCyclesOverrun = (*i)->ExtractData<unsigned int>();
				else if(registerName == L"StateLastUpdateTime")						stateLastUpdateTime = (*i)->ExtractData<double>();
				else if(registerName == L"StateLastUpdateMclk")						stateLastUpdateMclk = (*i)->ExtractData<unsigned int>();
				else if(registerName == L"StateLastUpdateMclkUnused")				stateLastUpdateMclkUnused = (*i)->ExtractData<unsigned int>();
				else if(registerName == L"StateLastUpdateMclkUnusedFromLastTimeslice")	stateLastUpdateMclkUnusedFromLastTimeslice = (*i)->ExtractData<unsigned int>();
				//Interrupt line rollback data
				else if(registerName == L"LineStateChangePendingVINT")				lineStateChangePendingVINT = (*i)->ExtractData<bool>();
				else if(registerName == L"LineStateChangeVINTMClkCountFromCurrent")	lineStateChangeVINTMClkCountFromCurrent = (*i)->ExtractData<unsigned int>();
				else if(registerName == L"LineStateChangeVINTTime")					lineStateChangeVINTTime = (*i)->ExtractData<double>();
				else if(registerName == L"LineStateChangePendingHINT")				lineStateChangePendingHINT = (*i)->ExtractData<bool>();
				else if(registerName == L"LineStateChangeHINTMClkCountFromCurrent")	lineStateChangeHINTMClkCountFromCurrent = (*i)->ExtractData<unsigned int>();
				else if(registerName == L"LineStateChangeHINTTime")					lineStateChangeHINTTime = (*i)->ExtractData<double>();
				else if(registerName == L"LineStateChangePendingEXINT")				lineStateChangePendingEXINT = (*i)->ExtractData<bool>();
				else if(registerName == L"LineStateChangeEXINTMClkCountFromCurrent")	lineStateChangeEXINTMClkCountFromCurrent = (*i)->ExtractData<unsigned int>();
				else if(registerName == L"LineStateChangeEXINTTime")					lineStateChangeEXINTTime = (*i)->ExtractData<double>();
				else if(registerName == L"LineStateChangePendingINTAsserted")				lineStateChangePendingINTAsserted = (*i)->ExtractData<bool>();
				else if(registerName == L"LineStateChangeINTAssertedMClkCountFromCurrent")	lineStateChangeINTAssertedMClkCountFromCurrent = (*i)->ExtractData<unsigned int>();
				else if(registerName == L"LineStateChangeINTAssertedTime")					lineStateChangeINTAssertedTime = (*i)->ExtractData<double>();
				else if(registerName == L"LineStateChangePendingINTNegated")				lineStateChangePendingINTNegated = (*i)->ExtractData<bool>();
				else if(registerName == L"LineStateChangeINTNegatedMClkCountFromCurrent")	lineStateChangeINTNegatedMClkCountFromCurrent = (*i)->ExtractData<unsigned int>();
				else if(registerName == L"LineStateChangeINTNegatedTime")					lineStateChangeINTNegatedTime = (*i)->ExtractData<double>();
				//Control port registers
				else if(registerName == L"CodeAndAddressRegistersModifiedSinceLastWrite")	codeAndAddressRegistersModifiedSinceLastWrite = (*i)->ExtractData<bool>();
				else if(registerName == L"CommandWritePending")								commandWritePending = (*i)->ExtractData<bool>();
				else if(registerName == L"OriginalCommandAddress")							originalCommandAddress = (*i)->ExtractHexData<unsigned int>();
				else if(registerName == L"CommandAddress")									commandAddress = (*i)->ExtractHexData<unsigned int>();
				else if(registerName == L"CommandCode")										commandCode = (*i)->ExtractHexData<unsigned int>();
				//Digital render data buffers
				else if(registerName == L"RenderDigitalHCounterPos")					renderDigitalHCounterPos = (*i)->ExtractHexData<unsigned int>();
				else if(registerName == L"RenderDigitalVCounterPos")					renderDigitalVCounterPos = (*i)->ExtractHexData<unsigned int>();
				else if(registerName == L"RenderDigitalVCounterPosPreviousLine")		renderDigitalVCounterPosPreviousLine = (*i)->ExtractHexData<unsigned int>();
				else if(registerName == L"RenderDigitalRemainingMclkCycles")			renderDigitalRemainingMclkCycles = (*i)->ExtractData<unsigned int>();
				else if(registerName == L"RenderDigitalScreenModeRS0Active")			renderDigitalScreenModeRS0Active = (*i)->ExtractData<bool>();
				else if(registerName == L"RenderDigitalScreenModeRS1Active")			renderDigitalScreenModeRS1Active = (*i)->ExtractData<bool>();
				else if(registerName == L"RenderDigitalScreenModeV30Active")			renderDigitalScreenModeV30Active = (*i)->ExtractData<bool>();
				else if(registerName == L"RenderDigitalInterlaceEnabledActive")			renderDigitalInterlaceEnabledActive = (*i)->ExtractData<bool>();
				else if(registerName == L"RenderDigitalInterlaceDoubleActive")			renderDigitalInterlaceDoubleActive = (*i)->ExtractData<bool>();
				else if(registerName == L"RenderDigitalPalModeActive")					renderDigitalPalModeActive = (*i)->ExtractData<bool>();
				else if(registerName == L"RenderDigitalOddFlagSet")						renderDigitalOddFlagSet = (*i)->ExtractData<bool>();
				else if(registerName == L"RenderLayerAHscrollPatternDisplacement")		renderLayerAHscrollPatternDisplacement = (*i)->ExtractData<unsigned int>();
				else if(registerName == L"RenderLayerBHscrollPatternDisplacement")		renderLayerBHscrollPatternDisplacement = (*i)->ExtractData<unsigned int>();
				else if(registerName == L"RenderLayerAHscrollMappingDisplacement")		renderLayerAHscrollMappingDisplacement = (*i)->ExtractData<unsigned int>();
				else if(registerName == L"RenderLayerBHscrollMappingDisplacement")		renderLayerBHscrollMappingDisplacement = (*i)->ExtractData<unsigned int>();
				else if(registerName == L"RenderLayerAVscrollPatternDisplacement")		renderLayerAVscrollPatternDisplacement = (*i)->ExtractData<unsigned int>();
				else if(registerName == L"RenderLayerBVscrollPatternDisplacement")		renderLayerBVscrollPatternDisplacement = (*i)->ExtractData<unsigned int>();
				else if(registerName == L"RenderLayerAVscrollMappingDisplacement")		renderLayerAVscrollMappingDisplacement = (*i)->ExtractData<unsigned int>();
				else if(registerName == L"RenderLayerBVscrollMappingDisplacement")		renderLayerBVscrollMappingDisplacement = (*i)->ExtractData<unsigned int>();
				else if(registerName == L"RenderWindowActiveCache")						(*i)->ExtractBinaryData(renderWindowActiveCache);
				else if(registerName == L"RenderMappingDataCacheLayerA")				(*i)->ExtractBinaryData(renderMappingDataCacheLayerA);
				else if(registerName == L"RenderMappingDataCacheLayerB")				(*i)->ExtractBinaryData(renderMappingDataCacheLayerB);
				else if(registerName == L"RenderMappingDataCacheSourceAddressLayerA")	(*i)->ExtractBinaryData(renderMappingDataCacheSourceAddressLayerA);
				else if(registerName == L"RenderMappingDataCacheSourceAddressLayerB")	(*i)->ExtractBinaryData(renderMappingDataCacheSourceAddressLayerB);
				else if(registerName == L"RenderPatternDataCacheLayerA")				(*i)->ExtractBinaryData(renderPatternDataCacheLayerA);
				else if(registerName == L"RenderPatternDataCacheLayerB")				(*i)->ExtractBinaryData(renderPatternDataCacheLayerB);
				else if(registerName == L"RenderPatternDataCacheSprite")				(*i)->ExtractBinaryData(renderPatternDataCacheLayerB);
				else if(registerName == L"RenderSpriteDisplayCacheEntryCount")			renderSpriteDisplayCacheEntryCount = (*i)->ExtractData<unsigned int>();
				else if(registerName == L"RenderSpriteDisplayCacheCurrentIndex")		renderSpriteDisplayCacheCurrentIndex = (*i)->ExtractData<unsigned int>();
				else if(registerName == L"RenderSpriteSearchComplete")					renderSpriteSearchComplete = (*i)->ExtractData<bool>();
				else if(registerName == L"RenderSpriteOverflow")						renderSpriteOverflow = (*i)->ExtractData<bool>();
				else if(registerName == L"RenderSpriteNextAttributeTableEntryToRead")	renderSpriteNextAttributeTableEntryToRead = (*i)->ExtractData<unsigned int>();
				else if(registerName == L"RenderSpriteDisplayCellCacheEntryCount")		renderSpriteDisplayCellCacheEntryCount = (*i)->ExtractData<unsigned int>();
				else if(registerName == L"RenderSpriteDisplayCellCacheCurrentIndex")	renderSpriteDisplayCellCacheCurrentIndex = (*i)->ExtractData<unsigned int>();
				else if(registerName == L"RenderSpriteDotOverflow")						renderSpriteDotOverflow = (*i)->ExtractData<bool>();
				else if(registerName == L"RenderSpriteDotOverflowPreviousLine")			renderSpriteDotOverflowPreviousLine = (*i)->ExtractData<bool>();
				else if(registerName == L"RenderSpritePixelBufferDigitalRenderPlane")	renderSpritePixelBufferDigitalRenderPlane = (*i)->ExtractData<unsigned int>();
				else if(registerName == L"RenderSpritePixelBufferAnalogRenderPlane")	renderSpritePixelBufferAnalogRenderPlane = (*i)->ExtractData<unsigned int>();
				else if(registerName == L"NonSpriteMaskCellEncountered")				nonSpriteMaskCellEncountered = (*i)->ExtractData<bool>();
				else if(registerName == L"RenderSpriteMaskActive")						renderSpriteMaskActive = (*i)->ExtractData<bool>();
				else if(registerName == L"RenderSpriteCollision")						renderSpriteCollision = (*i)->ExtractData<bool>();
				else if(registerName == L"RenderVSRAMCachedRead")						renderVSRAMCachedRead = (*i)->ExtractHexData<unsigned int>();
				//##TODO## Image buffer
				//DMA worker thread properties
				else if(registerName == L"WorkerThreadPaused")	workerThreadPaused = (*i)->ExtractData<bool>();
				//DMA transfer registers
				else if(registerName == L"DMATransferActive")			dmaTransferActive = (*i)->ExtractData<bool>();
				else if(registerName == L"DMATransferReadDataCached")	dmaTransferReadDataCached = (*i)->ExtractData<bool>();
				else if(registerName == L"DMATransferReadCache")		dmaTransferReadCache = (*i)->ExtractHexData<unsigned int>();
				else if(registerName == L"DMATransferNextReadMclk")		dmaTransferNextReadMclk = (*i)->ExtractData<unsigned int>();
				else if(registerName == L"DMATransferLastTimesliceUsedReadDelay")	dmaTransferLastTimesliceUsedReadDelay = (*i)->ExtractData<unsigned int>();
				else if(registerName == L"DMATransferInvalidPortWriteCached")	dmaTransferInvalidPortWriteCached = (*i)->ExtractData<bool>();
				else if(registerName == L"DMATransferInvalidPortWriteAddressCache")	dmaTransferInvalidPortWriteAddressCache = (*i)->ExtractHexData<unsigned int>();
				else if(registerName == L"DMATransferInvalidPortWriteDataCache")	dmaTransferInvalidPortWriteDataCache = (*i)->ExtractHexData<unsigned int>();
				//External interrupt settings
				else if(registerName == L"ExternalInterruptVideoTriggerPointPending")	externalInterruptVideoTriggerPointPending = (*i)->ExtractData<bool>();
				else if(registerName == L"ExternalInterruptVideoTriggerPointHCounter")	externalInterruptVideoTriggerPointHCounter = (*i)->ExtractHexData<unsigned int>();
				else if(registerName == L"ExternalInterruptVideoTriggerPointVCounter")	externalInterruptVideoTriggerPointVCounter = (*i)->ExtractHexData<unsigned int>();
			}
		}
		else if((*i)->GetName() == L"FIFOBuffer")
		{
			std::list<IHierarchicalStorageNode*> entryList = (*i)->GetChildList();
			for(std::list<IHierarchicalStorageNode*>::iterator entryNodeIterator = entryList.begin(); entryNodeIterator != entryList.end(); ++entryNodeIterator)
			{
				IHierarchicalStorageNode* entryNode = *entryNodeIterator;
				unsigned int entryIndex;
				if(entryNode->ExtractAttribute(L"index", entryIndex))
				{
					if(entryIndex < fifoBufferSize)
					{
						FIFOBufferEntry& entry = fifoBuffer[entryIndex];
						entryNode->ExtractAttributeHex(L"codeRegData", entry.codeRegData);
						entryNode->ExtractAttributeHex(L"addressRegData", entry.addressRegData);
						entryNode->ExtractAttributeHex(L"dataPortWriteData", entry.dataPortWriteData);
						entryNode->ExtractAttribute(L"dataWriteHalfWritten", entry.dataWriteHalfWritten);
						entryNode->ExtractAttribute(L"pendingDataWrite", entry.pendingDataWrite);
					}
				}
			}
		}
		else if((*i)->GetName() == L"RenderSpriteDisplayCache")
		{
			std::list<IHierarchicalStorageNode*> entryList = (*i)->GetChildList();
			for(std::list<IHierarchicalStorageNode*>::iterator entryNodeIterator = entryList.begin(); entryNodeIterator != entryList.end(); ++entryNodeIterator)
			{
				IHierarchicalStorageNode* entryNode = *entryNodeIterator;
				unsigned int entryIndex;
				if(entryNode->ExtractAttribute(L"index", entryIndex))
				{
					if(entryIndex < maxSpriteDisplayCacheSize)
					{
						SpriteDisplayCacheEntry& entry = renderSpriteDisplayCache[entryIndex];
						entryNode->ExtractAttribute(L"spriteTableIndex", entry.spriteTableIndex);
						entryNode->ExtractAttribute(L"spriteRowIndex", entry.spriteRowIndex);
						entryNode->ExtractAttributeHex(L"vpos", entry.vpos);
						entryNode->ExtractAttributeHex(L"sizeAndLinkData", entry.sizeAndLinkData);
						entryNode->ExtractAttributeHex(L"mappingData", entry.mappingData);
						entryNode->ExtractAttributeHex(L"hpos", entry.hpos);
					}
				}
			}
		}
		else if((*i)->GetName() == L"RenderSpriteDisplayCellCache")
		{
			std::list<IHierarchicalStorageNode*> entryList = (*i)->GetChildList();
			for(std::list<IHierarchicalStorageNode*>::iterator entryNodeIterator = entryList.begin(); entryNodeIterator != entryList.end(); ++entryNodeIterator)
			{
				IHierarchicalStorageNode* entryNode = *entryNodeIterator;
				unsigned int entryIndex;
				if(entryNode->ExtractAttribute(L"index", entryIndex))
				{
					if(entryIndex < maxSpriteDisplayCellCacheSize)
					{
						SpriteCellDisplayCacheEntry& entry = renderSpriteDisplayCellCache[entryIndex];
						entryNode->ExtractAttribute(L"spriteDisplayCacheIndex", entry.spriteDisplayCacheIndex);
						entryNode->ExtractAttribute(L"spriteCellColumnNo", entry.spriteCellColumnNo);
						entryNode->ExtractAttribute(L"patternCellOffsetX", entry.patternCellOffsetX);
						entryNode->ExtractAttribute(L"patternCellOffsetY", entry.patternCellOffsetY);
						entryNode->ExtractAttribute(L"patternRowOffset", entry.patternRowOffset);
						entryNode->ExtractAttributeHex(L"patternData", entry.patternData);
					}
				}
			}
		}
		else if((*i)->GetName() == L"SpritePixelBuffer")
		{
			std::list<IHierarchicalStorageNode*> entryList = (*i)->GetChildList();
			for(std::list<IHierarchicalStorageNode*>::iterator entryNodeIterator = entryList.begin(); entryNodeIterator != entryList.end(); ++entryNodeIterator)
			{
				IHierarchicalStorageNode* entryNode = *entryNodeIterator;
				unsigned int entryIndex;
				unsigned int renderPlane;
				if(entryNode->ExtractAttribute(L"index", entryIndex) && entryNode->ExtractAttribute(L"renderPlane", renderPlane))
				{
					if((entryIndex < spritePixelBufferSize) && (renderPlane < renderSpritePixelBufferPlaneCount))
					{
						SpritePixelBufferEntry& entry = spritePixelBuffer[renderPlane][entryIndex];
						entryNode->ExtractAttribute(L"paletteLine", entry.paletteLine);
						entryNode->ExtractAttribute(L"paletteIndex", entry.paletteIndex);
						entryNode->ExtractAttribute(L"layerPriority", entry.layerPriority);
						entryNode->ExtractAttribute(L"entryWritten", entry.entryWritten);
					}
				}
			}
		}
	}

	Device::LoadState(node);
}

//----------------------------------------------------------------------------------------
void S315_5313::SaveState(IHierarchicalStorageNode& node) const
{
	IHierarchicalStorageNode& regNode = node.CreateChild(L"Registers");
	std::wstring regBufferName = GetFullyQualifiedDeviceInstanceName();
	regBufferName += L".Registers";
	reg.SaveState(regNode, regBufferName, false);

	//Bus interface
	node.CreateChild(L"Register", busGranted).CreateAttribute(L"name", L"BusGranted");
	node.CreateChild(L"Register", palModeLineState).CreateAttribute(L"name", L"PalModeLineState");
	node.CreateChild(L"Register", resetLineState).CreateAttribute(L"name", L"ResetLineState");
	node.CreateChildHex(L"Register", lineStateIPL, 1).CreateAttribute(L"name", L"LineStateIPL");
	node.CreateChild(L"Register", busRequestLineState).CreateAttribute(L"name", L"BusRequestLineState");
	//Clock sources
	node.CreateChild(L"Register", clockMclkCurrent).CreateAttribute(L"name", L"ClockMclkCurrent");
	//Physical registers and memory buffers
	node.CreateChildHex(L"Register", status.GetData(), status.GetHexCharCount()).CreateAttribute(L"name", L"Status");
	node.CreateChildHex(L"Register", hcounter.GetData(), hcounter.GetHexCharCount()).CreateAttribute(L"name", L"HCounter");
	node.CreateChildHex(L"Register", vcounter.GetData(), vcounter.GetHexCharCount()).CreateAttribute(L"name", L"VCounter");
	node.CreateChildHex(L"Register", hcounterLatchedData.GetData(), hcounterLatchedData.GetHexCharCount()).CreateAttribute(L"name", L"HCounterLatchedData");
	node.CreateChildHex(L"Register", vcounterLatchedData.GetData(), vcounterLatchedData.GetHexCharCount()).CreateAttribute(L"name", L"VCounterLatchedData");
	node.CreateChildHex(L"Register", hintCounter, 2).CreateAttribute(L"name", L"HIntCounter");
	node.CreateChild(L"Register", vintPending).CreateAttribute(L"name", L"VIntPending");
	node.CreateChild(L"Register", hintPending).CreateAttribute(L"name", L"HIntPending");
	node.CreateChild(L"Register", exintPending).CreateAttribute(L"name", L"EXIntPending");
	//Active register settings
	node.CreateChild(L"Register", interlaceEnabled).CreateAttribute(L"name", L"InterlaceEnabled");
	node.CreateChild(L"Register", interlaceDouble).CreateAttribute(L"name", L"InterlaceDouble");
	node.CreateChild(L"Register", screenModeRS0).CreateAttribute(L"name", L"ScreenModeRS0");
	node.CreateChild(L"Register", screenModeRS1).CreateAttribute(L"name", L"ScreenModeRS1");
	node.CreateChild(L"Register", screenModeV30).CreateAttribute(L"name", L"ScreenModeV30");
	node.CreateChild(L"Register", palMode).CreateAttribute(L"name", L"PalMode");
	//FIFO buffer registers
	node.CreateChildHex(L"Register", fifoNextReadEntry, 1).CreateAttribute(L"name", L"FIFONextReadEntry");
	node.CreateChildHex(L"Register", fifoNextWriteEntry, 1).CreateAttribute(L"name", L"FIFONextWriteEntry");
	IHierarchicalStorageNode& fifoBufferNode = node.CreateChild(L"FIFOBuffer");
	for(unsigned int i = 0; i < fifoBufferSize; ++i)
	{
		IHierarchicalStorageNode& entryNode = fifoBufferNode.CreateChild(L"FIFOBufferEntry");
		const FIFOBufferEntry& entry = fifoBuffer[i];
		entryNode.CreateAttribute(L"index", i);
		entryNode.CreateAttributeHex(L"codeRegData", entry.codeRegData.GetData(), entry.codeRegData.GetHexCharCount());
		entryNode.CreateAttributeHex(L"addressRegData", entry.addressRegData.GetData(), entry.addressRegData.GetHexCharCount());
		entryNode.CreateAttributeHex(L"dataPortWriteData", entry.dataPortWriteData.GetData(), entry.dataPortWriteData.GetHexCharCount());
		entryNode.CreateAttribute(L"dataWriteHalfWritten", entry.dataWriteHalfWritten);
		entryNode.CreateAttribute(L"pendingDataWrite", entry.pendingDataWrite);
	}
	node.CreateChildHex(L"Register", readBuffer.GetData(), readBuffer.GetHexCharCount()).CreateAttribute(L"name", L"ReadBuffer");
	node.CreateChild(L"Register", readDataAvailable).CreateAttribute(L"name", L"ReadDataAvailable");
	node.CreateChild(L"Register", readDataHalfCached).CreateAttribute(L"name", L"ReadDataHalfCached");
	node.CreateChild(L"Register", dmaFillOperationRunning).CreateAttribute(L"name", L"DMAFillOperationRunning");
	node.CreateChildHex(L"Register", vsramReadCacheIndex, 2).CreateAttribute(L"name", L"VSRAMReadCacheIndex");
	node.CreateChildHex(L"Register", vsramLastRenderReadCache.GetData(), vsramLastRenderReadCache.GetHexCharCount()).CreateAttribute(L"name", L"VSRAMLastRenderReadCache");
	//Update state
	node.CreateChild(L"Register", currentTimesliceLength).CreateAttribute(L"name", L"CurrentTimesliceLength");
	node.CreateChild(L"Register", lastTimesliceMclkCyclesRemainingTime).CreateAttribute(L"name", L"LastTimesliceMclkCyclesRemainingTime");
	node.CreateChild(L"Register", currentTimesliceMclkCyclesRemainingTime).CreateAttribute(L"name", L"CurrentTimesliceMclkCyclesRemainingTime");
	node.CreateChild(L"Register", lastTimesliceMclkCyclesOverrun).CreateAttribute(L"name", L"LastTimesliceMclkCyclesOverrun");
	node.CreateChild(L"Register", stateLastUpdateTime).CreateAttribute(L"name", L"StateLastUpdateTime");
	node.CreateChild(L"Register", stateLastUpdateMclk).CreateAttribute(L"name", L"StateLastUpdateMclk");
	node.CreateChild(L"Register", stateLastUpdateMclkUnused).CreateAttribute(L"name", L"StateLastUpdateMclkUnused");
	node.CreateChild(L"Register", stateLastUpdateMclkUnusedFromLastTimeslice).CreateAttribute(L"name", L"StateLastUpdateMclkUnusedFromLastTimeslice");
	//Interrupt line rollback data
	node.CreateChild(L"Register", lineStateChangePendingVINT).CreateAttribute(L"name", L"LineStateChangePendingVINT");
	node.CreateChild(L"Register", lineStateChangeVINTMClkCountFromCurrent).CreateAttribute(L"name", L"LineStateChangeVINTMClkCountFromCurrent");
	node.CreateChild(L"Register", lineStateChangeVINTTime).CreateAttribute(L"name", L"LineStateChangeVINTTime");
	node.CreateChild(L"Register", lineStateChangePendingHINT).CreateAttribute(L"name", L"LineStateChangePendingHINT");
	node.CreateChild(L"Register", lineStateChangeHINTMClkCountFromCurrent).CreateAttribute(L"name", L"LineStateChangeHINTMClkCountFromCurrent");
	node.CreateChild(L"Register", lineStateChangeHINTTime).CreateAttribute(L"name", L"LineStateChangeHINTTime");
	node.CreateChild(L"Register", lineStateChangePendingEXINT).CreateAttribute(L"name", L"LineStateChangePendingEXINT");
	node.CreateChild(L"Register", lineStateChangeEXINTMClkCountFromCurrent).CreateAttribute(L"name", L"LineStateChangeEXINTMClkCountFromCurrent");
	node.CreateChild(L"Register", lineStateChangeEXINTTime).CreateAttribute(L"name", L"LineStateChangeEXINTTime");
	node.CreateChild(L"Register", lineStateChangePendingINTAsserted).CreateAttribute(L"name", L"LineStateChangePendingINTAsserted");
	node.CreateChild(L"Register", lineStateChangeINTAssertedMClkCountFromCurrent).CreateAttribute(L"name", L"LineStateChangeINTAssertedMClkCountFromCurrent");
	node.CreateChild(L"Register", lineStateChangeINTAssertedTime).CreateAttribute(L"name", L"LineStateChangeINTAssertedTime");
	node.CreateChild(L"Register", lineStateChangePendingINTNegated).CreateAttribute(L"name", L"LineStateChangePendingINTNegated");
	node.CreateChild(L"Register", lineStateChangeINTNegatedMClkCountFromCurrent).CreateAttribute(L"name", L"LineStateChangeINTNegatedMClkCountFromCurrent");
	node.CreateChild(L"Register", lineStateChangeINTNegatedTime).CreateAttribute(L"name", L"LineStateChangeINTNegatedTime");
	//Control port registers
	node.CreateChild(L"Register", codeAndAddressRegistersModifiedSinceLastWrite).CreateAttribute(L"name", L"CodeAndAddressRegistersModifiedSinceLastWrite");
	node.CreateChild(L"Register", commandWritePending).CreateAttribute(L"name", L"CommandWritePending");
	node.CreateChildHex(L"Register", originalCommandAddress.GetData(), originalCommandAddress.GetHexCharCount()).CreateAttribute(L"name", L"OriginalCommandAddress");
	node.CreateChildHex(L"Register", commandAddress.GetData(), commandAddress.GetHexCharCount()).CreateAttribute(L"name", L"CommandAddress");
	node.CreateChildHex(L"Register", commandCode.GetData(), commandCode.GetHexCharCount()).CreateAttribute(L"name", L"CommandCode");
	//Digital render data buffers
	node.CreateChildHex(L"Register", renderDigitalHCounterPos, 3).CreateAttribute(L"name", L"RenderDigitalHCounterPos");
	node.CreateChildHex(L"Register", renderDigitalVCounterPos, 3).CreateAttribute(L"name", L"RenderDigitalVCounterPos");
	node.CreateChildHex(L"Register", renderDigitalVCounterPosPreviousLine, 3).CreateAttribute(L"name", L"RenderDigitalVCounterPosPreviousLine");
	node.CreateChild(L"Register", renderDigitalRemainingMclkCycles).CreateAttribute(L"name", L"RenderDigitalRemainingMclkCycles");
	node.CreateChild(L"Register", renderDigitalScreenModeRS0Active).CreateAttribute(L"name", L"RenderDigitalScreenModeRS0Active");
	node.CreateChild(L"Register", renderDigitalScreenModeRS1Active).CreateAttribute(L"name", L"RenderDigitalScreenModeRS1Active");
	node.CreateChild(L"Register", renderDigitalScreenModeV30Active).CreateAttribute(L"name", L"RenderDigitalScreenModeV30Active");
	node.CreateChild(L"Register", renderDigitalInterlaceEnabledActive).CreateAttribute(L"name", L"RenderDigitalInterlaceEnabledActive");
	node.CreateChild(L"Register", renderDigitalInterlaceDoubleActive).CreateAttribute(L"name", L"RenderDigitalInterlaceDoubleActive");
	node.CreateChild(L"Register", renderDigitalPalModeActive).CreateAttribute(L"name", L"RenderDigitalPalModeActive");
	node.CreateChild(L"Register", renderDigitalOddFlagSet).CreateAttribute(L"name", L"RenderDigitalOddFlagSet");
	node.CreateChild(L"Register", renderLayerAHscrollPatternDisplacement).CreateAttribute(L"name", L"RenderLayerAHscrollPatternDisplacement");
	node.CreateChild(L"Register", renderLayerBHscrollPatternDisplacement).CreateAttribute(L"name", L"RenderLayerBHscrollPatternDisplacement");
	node.CreateChild(L"Register", renderLayerAHscrollMappingDisplacement).CreateAttribute(L"name", L"RenderLayerAHscrollMappingDisplacement");
	node.CreateChild(L"Register", renderLayerBHscrollMappingDisplacement).CreateAttribute(L"name", L"RenderLayerBHscrollMappingDisplacement");
	node.CreateChild(L"Register", renderLayerAVscrollPatternDisplacement).CreateAttribute(L"name", L"RenderLayerAVscrollPatternDisplacement");
	node.CreateChild(L"Register", renderLayerBVscrollPatternDisplacement).CreateAttribute(L"name", L"RenderLayerBVscrollPatternDisplacement");
	node.CreateChild(L"Register", renderLayerAVscrollMappingDisplacement).CreateAttribute(L"name", L"RenderLayerAVscrollMappingDisplacement");
	node.CreateChild(L"Register", renderLayerBVscrollMappingDisplacement).CreateAttribute(L"name", L"RenderLayerBVscrollMappingDisplacement");
	node.CreateChildBinary(L"Register", renderWindowActiveCache, GetFullyQualifiedDeviceInstanceName() + L"RenderWindowActiveCache").CreateAttribute(L"name", L"RenderWindowActiveCache");
	node.CreateChildBinary(L"Register", renderMappingDataCacheLayerA, GetFullyQualifiedDeviceInstanceName() + L"RenderMappingDataCacheLayerA").CreateAttribute(L"name", L"RenderMappingDataCacheLayerA");
	node.CreateChildBinary(L"Register", renderMappingDataCacheLayerB, GetFullyQualifiedDeviceInstanceName() + L"RenderMappingDataCacheLayerB").CreateAttribute(L"name", L"RenderMappingDataCacheLayerB");
	node.CreateChildBinary(L"Register", renderMappingDataCacheSourceAddressLayerA, GetFullyQualifiedDeviceInstanceName() + L"RenderMappingDataCacheLayerA").CreateAttribute(L"name", L"RenderMappingDataCacheSourceAddressLayerA");
	node.CreateChildBinary(L"Register", renderMappingDataCacheSourceAddressLayerB, GetFullyQualifiedDeviceInstanceName() + L"RenderMappingDataCacheLayerB").CreateAttribute(L"name", L"RenderMappingDataCacheSourceAddressLayerB");
	node.CreateChildBinary(L"Register", renderPatternDataCacheLayerA, GetFullyQualifiedDeviceInstanceName() + L"RenderPatternDataCacheLayerA").CreateAttribute(L"name", L"RenderPatternDataCacheLayerA");
	node.CreateChildBinary(L"Register", renderPatternDataCacheLayerB, GetFullyQualifiedDeviceInstanceName() + L"RenderPatternDataCacheLayerB").CreateAttribute(L"name", L"RenderPatternDataCacheLayerB");
	node.CreateChildBinary(L"Register", renderPatternDataCacheLayerB, GetFullyQualifiedDeviceInstanceName() + L"RenderPatternDataCacheSprite").CreateAttribute(L"name", L"RenderPatternDataCacheSprite");
	IHierarchicalStorageNode& renderSpriteDisplayCacheNode = node.CreateChild(L"RenderSpriteDisplayCache");
	for(unsigned int i = 0; i < maxSpriteDisplayCacheSize; ++i)
	{
		IHierarchicalStorageNode& entryNode = renderSpriteDisplayCacheNode.CreateChild(L"RenderSpriteDisplayCacheEntry");
		const SpriteDisplayCacheEntry& entry = renderSpriteDisplayCache[i];
		entryNode.CreateAttribute(L"index", i);
		entryNode.CreateAttribute(L"spriteTableIndex", entry.spriteTableIndex);
		entryNode.CreateAttribute(L"spriteRowIndex", entry.spriteRowIndex);
		entryNode.CreateAttributeHex(L"vpos", entry.vpos.GetData(), entry.vpos.GetHexCharCount());
		entryNode.CreateAttributeHex(L"sizeAndLinkData", entry.sizeAndLinkData.GetData(), entry.sizeAndLinkData.GetHexCharCount());
		entryNode.CreateAttributeHex(L"mappingData", entry.mappingData.GetData(), entry.mappingData.GetHexCharCount());
		entryNode.CreateAttributeHex(L"hpos", entry.hpos.GetData(), entry.hpos.GetHexCharCount());
	}
	node.CreateChild(L"Register", renderSpriteDisplayCacheEntryCount).CreateAttribute(L"name", L"RenderSpriteDisplayCacheEntryCount");
	node.CreateChild(L"Register", renderSpriteDisplayCacheCurrentIndex).CreateAttribute(L"name", L"RenderSpriteDisplayCacheCurrentIndex");
	node.CreateChild(L"Register", renderSpriteSearchComplete).CreateAttribute(L"name", L"RenderSpriteSearchComplete");
	node.CreateChild(L"Register", renderSpriteOverflow).CreateAttribute(L"name", L"RenderSpriteOverflow");
	node.CreateChild(L"Register", renderSpriteNextAttributeTableEntryToRead).CreateAttribute(L"name", L"RenderSpriteNextAttributeTableEntryToRead");
	IHierarchicalStorageNode& renderSpriteDisplayCellCacheNode = node.CreateChild(L"RenderSpriteDisplayCellCache");
	for(unsigned int i = 0; i < maxSpriteDisplayCellCacheSize; ++i)
	{
		IHierarchicalStorageNode& entryNode = renderSpriteDisplayCellCacheNode.CreateChild(L"RenderSpriteDisplayCellCacheEntry");
		const SpriteCellDisplayCacheEntry& entry = renderSpriteDisplayCellCache[i];
		entryNode.CreateAttribute(L"index", i);
		entryNode.CreateAttribute(L"spriteDisplayCacheIndex", entry.spriteDisplayCacheIndex);
		entryNode.CreateAttribute(L"spriteCellColumnNo", entry.spriteCellColumnNo);
		entryNode.CreateAttribute(L"patternCellOffsetX", entry.patternCellOffsetX);
		entryNode.CreateAttribute(L"patternCellOffsetY", entry.patternCellOffsetY);
		entryNode.CreateAttribute(L"patternRowOffset", entry.patternRowOffset);
		entryNode.CreateAttributeHex(L"patternData", entry.patternData.GetData(), entry.patternData.GetHexCharCount());
	}
	node.CreateChild(L"Register", renderSpriteDisplayCellCacheEntryCount).CreateAttribute(L"name", L"RenderSpriteDisplayCellCacheEntryCount");
	node.CreateChild(L"Register", renderSpriteDisplayCellCacheCurrentIndex).CreateAttribute(L"name", L"RenderSpriteDisplayCellCacheCurrentIndex");
	node.CreateChild(L"Register", renderSpriteDotOverflow).CreateAttribute(L"name", L"RenderSpriteDotOverflow");
	node.CreateChild(L"Register", renderSpriteDotOverflowPreviousLine).CreateAttribute(L"name", L"RenderSpriteDotOverflowPreviousLine");
	node.CreateChild(L"Register", renderSpritePixelBufferDigitalRenderPlane).CreateAttribute(L"name", L"RenderSpritePixelBufferDigitalRenderPlane");
	node.CreateChild(L"Register", renderSpritePixelBufferAnalogRenderPlane).CreateAttribute(L"name", L"RenderSpritePixelBufferAnalogRenderPlane");
	IHierarchicalStorageNode& spritePixelBufferNode = node.CreateChild(L"SpritePixelBuffer");
	for(unsigned int renderPlane = 0; renderPlane < renderSpritePixelBufferPlaneCount; ++renderPlane)
	{
		for(unsigned int i = 0; i < spritePixelBufferSize; ++i)
		{
			IHierarchicalStorageNode& entryNode = spritePixelBufferNode.CreateChild(L"SpritePixelBufferEntry");
			const SpritePixelBufferEntry& entry = spritePixelBuffer[renderPlane][i];
			entryNode.CreateAttribute(L"renderPlane", renderPlane);
			entryNode.CreateAttribute(L"index", i);
			entryNode.CreateAttribute(L"paletteLine", entry.paletteLine);
			entryNode.CreateAttribute(L"paletteIndex", entry.paletteIndex);
			entryNode.CreateAttribute(L"layerPriority", entry.layerPriority);
			entryNode.CreateAttribute(L"entryWritten", entry.entryWritten);
		}
	}
	node.CreateChild(L"Register", nonSpriteMaskCellEncountered).CreateAttribute(L"name", L"NonSpriteMaskCellEncountered");
	node.CreateChild(L"Register", renderSpriteMaskActive).CreateAttribute(L"name", L"RenderSpriteMaskActive");
	node.CreateChild(L"Register", renderSpriteCollision).CreateAttribute(L"name", L"RenderSpriteCollision");
	node.CreateChildHex(L"Register", renderVSRAMCachedRead.GetData(), renderVSRAMCachedRead.GetHexCharCount()).CreateAttribute(L"name", L"RenderVSRAMCachedRead");
	//##TODO## Image buffer
	//DMA worker thread properties
	node.CreateChild(L"Register", workerThreadPaused).CreateAttribute(L"name", L"WorkerThreadPaused");
	//DMA transfer registers
	node.CreateChild(L"Register", dmaTransferActive).CreateAttribute(L"name", L"DMATransferActive");
	node.CreateChild(L"Register", dmaTransferReadDataCached).CreateAttribute(L"name", L"DMATransferReadDataCached");
	node.CreateChildHex(L"Register", dmaTransferReadCache.GetData(), dmaTransferReadCache.GetHexCharCount()).CreateAttribute(L"name", L"DMATransferReadCache");
	node.CreateChild(L"Register", dmaTransferNextReadMclk).CreateAttribute(L"name", L"DMATransferNextReadMclk");
	node.CreateChild(L"Register", dmaTransferLastTimesliceUsedReadDelay).CreateAttribute(L"name", L"DMATransferLastTimesliceUsedReadDelay");
	node.CreateChild(L"Register", dmaTransferInvalidPortWriteCached).CreateAttribute(L"name", L"DMATransferInvalidPortWriteCached");
	node.CreateChildHex(L"Register", dmaTransferInvalidPortWriteAddressCache, 2).CreateAttribute(L"name", L"DMATransferInvalidPortWriteAddressCache");
	node.CreateChildHex(L"Register", dmaTransferInvalidPortWriteDataCache.GetData(), dmaTransferReadCache.GetHexCharCount()).CreateAttribute(L"name", L"DMATransferInvalidPortWriteDataCache");
	//External interrupt settings
	node.CreateChild(L"Register", externalInterruptVideoTriggerPointPending).CreateAttribute(L"name", L"ExternalInterruptVideoTriggerPointPending");
	node.CreateChildHex(L"Register", externalInterruptVideoTriggerPointHCounter, 3).CreateAttribute(L"name", L"ExternalInterruptVideoTriggerPointHCounter");
	node.CreateChildHex(L"Register", externalInterruptVideoTriggerPointVCounter, 3).CreateAttribute(L"name", L"ExternalInterruptVideoTriggerPointVCounter");

	Device::SaveState(node);
}

//----------------------------------------------------------------------------------------
void S315_5313::LoadSettingsState(IHierarchicalStorageNode& node)
{
	std::list<IHierarchicalStorageNode*> childList = node.GetChildList();
	for(std::list<IHierarchicalStorageNode*>::iterator i = childList.begin(); i != childList.end(); ++i)
	{
		std::wstring keyName = (*i)->GetName();
		if(keyName == L"Register")
		{
			IHierarchicalStorageAttribute* nameAttribute = (*i)->GetAttribute(L"name");
			if(nameAttribute != 0)
			{
				std::wstring registerName = nameAttribute->GetValue();
				if(registerName == L"VideoSingleBuffering")          videoSingleBuffering = (*i)->ExtractData<bool>();
				else if(registerName == L"VideoFixedAspectRatio")    videoFixedAspectRatio = (*i)->ExtractData<bool>();
				else if(registerName == L"VideoShowStatusBar")       videoShowStatusBar = (*i)->ExtractData<bool>();
				else if(registerName == L"VideoEnableLineSmoothing") videoEnableLineSmoothing = (*i)->ExtractData<bool>();
			}
		}
	}

	Device::LoadSettingsState(node);
}

//----------------------------------------------------------------------------------------
void S315_5313::SaveSettingsState(IHierarchicalStorageNode& node) const
{
	node.CreateChild(L"Register", videoSingleBuffering).CreateAttribute(L"name", L"VideoSingleBuffering");
	node.CreateChild(L"Register", videoFixedAspectRatio).CreateAttribute(L"name", L"VideoFixedAspectRatio");
	node.CreateChild(L"Register", videoShowStatusBar).CreateAttribute(L"name", L"VideoShowStatusBar");
	node.CreateChild(L"Register", videoEnableLineSmoothing).CreateAttribute(L"name", L"VideoEnableLineSmoothing");

	Device::SaveSettingsState(node);
}

//----------------------------------------------------------------------------------------
void S315_5313::LoadDebuggerState(IHierarchicalStorageNode& node)
{
	std::list<IHierarchicalStorageNode*> childList = node.GetChildList();
	for(std::list<IHierarchicalStorageNode*>::iterator i = childList.begin(); i != childList.end(); ++i)
	{
		std::wstring keyName = (*i)->GetName();
		if(keyName == L"Register")
		{
			IHierarchicalStorageAttribute* nameAttribute = (*i)->GetAttribute(L"name");
			if(nameAttribute != 0)
			{
				std::wstring registerName = nameAttribute->GetValue();
				//Debug output
				if(registerName == L"OutputPortAccessDebugMessages")		outputPortAccessDebugMessages = (*i)->ExtractData<bool>();
				else if(registerName == L"OutputTimingDebugMessages")		outputTimingDebugMessages = (*i)->ExtractData<bool>();
				else if(registerName == L"OutputRenderSyncMessages")		outputRenderSyncMessages = (*i)->ExtractData<bool>();
				else if(registerName == L"OutputInterruptDebugMessages")	outputInterruptDebugMessages = (*i)->ExtractData<bool>();
				else if(registerName == L"VideoDisableRenderOutput")		videoDisableRenderOutput = (*i)->ExtractData<bool>();
				else if(registerName == L"VideoEnableSpriteBoxing")			videoEnableSpriteBoxing = (*i)->ExtractData<bool>();
				else if(registerName == L"VideoHighlightRenderPos")			videoHighlightRenderPos = (*i)->ExtractData<bool>();
				else if(registerName == L"VideoShowBoundaryActiveImage")	videoShowBoundaryActiveImage = (*i)->ExtractData<bool>();
				else if(registerName == L"VideoShowBoundaryActionSafe")		videoShowBoundaryActionSafe = (*i)->ExtractData<bool>();
				else if(registerName == L"VideoShowBoundaryTitleSafe")		videoShowBoundaryTitleSafe = (*i)->ExtractData<bool>();
				else if(registerName == L"VideoEnableFullImageBufferInfo")	videoEnableFullImageBufferInfo = (*i)->ExtractData<bool>();
				//Layer removal settings
				else if(registerName == L"EnableLayerAHigh")		enableLayerAHigh = (*i)->ExtractData<bool>();
				else if(registerName == L"EnableLayerALow")			enableLayerALow = (*i)->ExtractData<bool>();
				else if(registerName == L"EnableLayerBHigh")		enableLayerBHigh = (*i)->ExtractData<bool>();
				else if(registerName == L"EnableLayerBLow")			enableLayerBLow = (*i)->ExtractData<bool>();
				else if(registerName == L"EnableWindowHigh")		enableWindowHigh = (*i)->ExtractData<bool>();
				else if(registerName == L"EnableWindowLow")			enableWindowLow = (*i)->ExtractData<bool>();
				else if(registerName == L"EnableSpriteHigh")		enableSpriteHigh = (*i)->ExtractData<bool>();
				else if(registerName == L"EnableSpriteLow")			enableSpriteLow = (*i)->ExtractData<bool>();
			}
		}
	}

	Device::LoadDebuggerState(node);
}

//----------------------------------------------------------------------------------------
void S315_5313::SaveDebuggerState(IHierarchicalStorageNode& node) const
{
	//Debug output
	node.CreateChild(L"Register", outputPortAccessDebugMessages).CreateAttribute(L"name", L"OutputPortAccessDebugMessages");
	node.CreateChild(L"Register", outputTimingDebugMessages).CreateAttribute(L"name", L"OutputTimingDebugMessages");
	node.CreateChild(L"Register", outputRenderSyncMessages).CreateAttribute(L"name", L"OutputRenderSyncMessages");
	node.CreateChild(L"Register", outputInterruptDebugMessages).CreateAttribute(L"name", L"OutputInterruptDebugMessages");
	node.CreateChild(L"Register", videoDisableRenderOutput).CreateAttribute(L"name", L"VideoDisableRenderOutput");
	node.CreateChild(L"Register", videoEnableSpriteBoxing).CreateAttribute(L"name", L"VideoEnableSpriteBoxing");
	node.CreateChild(L"Register", videoHighlightRenderPos).CreateAttribute(L"name", L"VideoHighlightRenderPos");
	node.CreateChild(L"Register", videoShowBoundaryActiveImage).CreateAttribute(L"name", L"VideoShowBoundaryActiveImage");
	node.CreateChild(L"Register", videoShowBoundaryActionSafe).CreateAttribute(L"name", L"VideoShowBoundaryActionSafe");
	node.CreateChild(L"Register", videoShowBoundaryTitleSafe).CreateAttribute(L"name", L"VideoShowBoundaryTitleSafe");
	node.CreateChild(L"Register", videoEnableFullImageBufferInfo).CreateAttribute(L"name", L"VideoEnableFullImageBufferInfo");

	//Layer removal settings
	node.CreateChild(L"Register", enableLayerAHigh).CreateAttribute(L"name", L"EnableLayerAHigh");
	node.CreateChild(L"Register", enableLayerALow).CreateAttribute(L"name", L"EnableLayerALow");
	node.CreateChild(L"Register", enableLayerBHigh).CreateAttribute(L"name", L"EnableLayerBHigh");
	node.CreateChild(L"Register", enableLayerBLow).CreateAttribute(L"name", L"EnableLayerBLow");
	node.CreateChild(L"Register", enableWindowHigh).CreateAttribute(L"name", L"EnableWindowHigh");
	node.CreateChild(L"Register", enableWindowLow).CreateAttribute(L"name", L"EnableWindowLow");
	node.CreateChild(L"Register", enableSpriteHigh).CreateAttribute(L"name", L"EnableSpriteHigh");
	node.CreateChild(L"Register", enableSpriteLow).CreateAttribute(L"name", L"EnableSpriteLow");

	Device::SaveDebuggerState(node);
}

//----------------------------------------------------------------------------------------
//Data read/write functions
//----------------------------------------------------------------------------------------
bool S315_5313::ReadGenericData(unsigned int dataID, const DataContext* dataContext, IGenericAccessDataValue& dataValue) const
{
	ApplyGenericDataValueDisplaySettings(dataID, dataValue);
	AccessTarget accessTarget = AccessTarget().AccessLatest();
	switch((IS315_5313DataSource)dataID)
	{
	case IS315_5313DataSource::RawRegister:{
		const RegisterDataContext& registerDataContext = *((RegisterDataContext*)dataContext);
		Data registerData = GetRegisterData(registerDataContext.registerNo, accessTarget);
		return dataValue.SetValue(registerData.GetData());}
	case IS315_5313DataSource::RegStatus:
		return dataValue.SetValue(status.GetData());
	case IS315_5313DataSource::FlagFIFOEmpty:
		return dataValue.SetValue(GetStatusFlagFIFOEmpty());
	case IS315_5313DataSource::FlagFIFOFull:
		return dataValue.SetValue(GetStatusFlagFIFOFull());
	case IS315_5313DataSource::FlagF:
		return dataValue.SetValue(GetStatusFlagF());
	case IS315_5313DataSource::FlagSpriteOverflow:
		return dataValue.SetValue(GetStatusFlagSpriteOverflow());
	case IS315_5313DataSource::FlagSpriteCollision:
		return dataValue.SetValue(GetStatusFlagSpriteCollision());
	case IS315_5313DataSource::FlagOddInterlaceFrame:
		return dataValue.SetValue(GetStatusFlagOddInterlaceFrame());
	case IS315_5313DataSource::FlagVBlank:
		return dataValue.SetValue(GetStatusFlagVBlank());
	case IS315_5313DataSource::FlagHBlank:
		return dataValue.SetValue(GetStatusFlagHBlank());
	case IS315_5313DataSource::FlagDMA:
		return dataValue.SetValue(GetStatusFlagDMA());
	case IS315_5313DataSource::FlagPAL:
		return dataValue.SetValue(GetStatusFlagPAL());
	case IS315_5313DataSource::RegVSI:
		return dataValue.SetValue(RegGetVSI(accessTarget));
	case IS315_5313DataSource::RegHSI:
		return dataValue.SetValue(RegGetHSI(accessTarget));
	case IS315_5313DataSource::RegLCB:
		return dataValue.SetValue(RegGetLCB(accessTarget));
	case IS315_5313DataSource::RegIE1:
		return dataValue.SetValue(RegGetIE1(accessTarget));
	case IS315_5313DataSource::RegSS:
		return dataValue.SetValue(RegGetSS(accessTarget));
	case IS315_5313DataSource::RegPS:
		return dataValue.SetValue(RegGetPS(accessTarget));
	case IS315_5313DataSource::RegM2:
		return dataValue.SetValue(RegGetM2(accessTarget));
	case IS315_5313DataSource::RegES:
		return dataValue.SetValue(RegGetES(accessTarget));
	case IS315_5313DataSource::RegEVRAM:
		return dataValue.SetValue(RegGetEVRAM(accessTarget));
	case IS315_5313DataSource::RegDisplayEnabled:
		return dataValue.SetValue(RegGetDisplayEnabled(accessTarget));
	case IS315_5313DataSource::RegIE0:
		return dataValue.SetValue(RegGetIE0(accessTarget));
	case IS315_5313DataSource::RegDMAEnabled:
		return dataValue.SetValue(RegGetDMAEnabled(accessTarget));
	case IS315_5313DataSource::RegM3:
		return dataValue.SetValue(RegGetM3(accessTarget));
	case IS315_5313DataSource::RegMode5:
		return dataValue.SetValue(RegGetMode5(accessTarget));
	case IS315_5313DataSource::RegSZ:
		return dataValue.SetValue(RegGetSZ(accessTarget));
	case IS315_5313DataSource::RegMAG:
		return dataValue.SetValue(RegGetMAG(accessTarget));
	case IS315_5313DataSource::RegNameTableBaseA:
		return dataValue.SetValue(RegGetNameTableBaseScrollA(accessTarget, !RegGetMode5(accessTarget), RegGetEVRAM(accessTarget)));
	case IS315_5313DataSource::RegNameTableBaseWindow:
		return dataValue.SetValue(RegGetNameTableBaseWindow(accessTarget, RegGetRS1(accessTarget), RegGetEVRAM(accessTarget)));
	case IS315_5313DataSource::RegNameTableBaseB:
		return dataValue.SetValue(RegGetNameTableBaseScrollB(accessTarget, RegGetEVRAM(accessTarget)));
	case IS315_5313DataSource::RegNameTableBaseSprite:
		return dataValue.SetValue(RegGetNameTableBaseSprite(accessTarget, !RegGetMode5(accessTarget), RegGetRS1(accessTarget), RegGetEVRAM(accessTarget)));
	case IS315_5313DataSource::RegPatternBaseSprite:
		return dataValue.SetValue(RegGetPatternBaseSprite(accessTarget, !RegGetMode5(accessTarget), RegGetEVRAM(accessTarget)));
	case IS315_5313DataSource::Reg077:
		return dataValue.SetValue(RegGet077(accessTarget));
	case IS315_5313DataSource::Reg076:
		return dataValue.SetValue(RegGet076(accessTarget));
	case IS315_5313DataSource::RegBackgroundPaletteRow:
		return dataValue.SetValue(RegGetBackgroundPaletteRow(accessTarget));
	case IS315_5313DataSource::RegBackgroundPaletteColumn:
		return dataValue.SetValue(RegGetBackgroundPaletteColumn(accessTarget));
	case IS315_5313DataSource::RegBackgroundScrollX:
		return dataValue.SetValue(RegGetBackgroundScrollX(accessTarget));
	case IS315_5313DataSource::RegBackgroundScrollY:
		return dataValue.SetValue(RegGetBackgroundScrollY(accessTarget));
	case IS315_5313DataSource::RegHINTData:
		return dataValue.SetValue(RegGetHInterruptData(accessTarget));
	case IS315_5313DataSource::Reg0B7:
		return dataValue.SetValue(RegGet0B7(accessTarget));
	case IS315_5313DataSource::Reg0B6:
		return dataValue.SetValue(RegGet0B6(accessTarget));
	case IS315_5313DataSource::Reg0B5:
		return dataValue.SetValue(RegGet0B5(accessTarget));
	case IS315_5313DataSource::Reg0B4:
		return dataValue.SetValue(RegGet0B4(accessTarget));
	case IS315_5313DataSource::RegIE2:
		return dataValue.SetValue(RegGetIE2(accessTarget));
	case IS315_5313DataSource::RegVSCR:
		return dataValue.SetValue(RegGetVSCR(accessTarget));
	case IS315_5313DataSource::RegHSCR:
		return dataValue.SetValue(RegGetHSCR(accessTarget));
	case IS315_5313DataSource::RegLSCR:
		return dataValue.SetValue(RegGetLSCR(accessTarget));
	case IS315_5313DataSource::RegRS0:
		return dataValue.SetValue(RegGetRS0(accessTarget));
	case IS315_5313DataSource::RegU1:
		return dataValue.SetValue(RegGetU1(accessTarget));
	case IS315_5313DataSource::RegU2:
		return dataValue.SetValue(RegGetU2(accessTarget));
	case IS315_5313DataSource::RegU3:
		return dataValue.SetValue(RegGetU3(accessTarget));
	case IS315_5313DataSource::RegSTE:
		return dataValue.SetValue(RegGetSTE(accessTarget));
	case IS315_5313DataSource::RegLSM1:
		return dataValue.SetValue(RegGetLSM1(accessTarget));
	case IS315_5313DataSource::RegLSM0:
		return dataValue.SetValue(RegGetLSM0(accessTarget));
	case IS315_5313DataSource::RegRS1:
		return dataValue.SetValue(RegGetRS1(accessTarget));
	case IS315_5313DataSource::RegHScrollDataBase:
		return dataValue.SetValue(RegGetHScrollDataBase(accessTarget, RegGetEVRAM(accessTarget)));
	case IS315_5313DataSource::Reg0E57:
		return dataValue.SetValue(RegGet0E57(accessTarget));
	case IS315_5313DataSource::RegPatternBaseA:
		return dataValue.SetValue(RegGetPatternBaseScrollA(accessTarget, RegGetEVRAM(accessTarget)));
	case IS315_5313DataSource::Reg0E13:
		return dataValue.SetValue(RegGet0E13(accessTarget));
	case IS315_5313DataSource::RegPatternBaseB:
		return dataValue.SetValue(RegGetPatternBaseScrollB(accessTarget, RegGetEVRAM(accessTarget)));
	case IS315_5313DataSource::RegAutoIncrementData:
		return dataValue.SetValue(RegGetAutoIncrementData(accessTarget));
	case IS315_5313DataSource::Reg1067:
		return dataValue.SetValue(RegGet1067(accessTarget));
	case IS315_5313DataSource::RegVSZ:
		return dataValue.SetValue(RegGetVSZ(accessTarget));
	case IS315_5313DataSource::RegVSZ1:
		return dataValue.SetValue(RegGetVSZ1(accessTarget));
	case IS315_5313DataSource::RegVSZ0:
		return dataValue.SetValue(RegGetVSZ0(accessTarget));
	case IS315_5313DataSource::Reg1023:
		return dataValue.SetValue(RegGet1023(accessTarget));
	case IS315_5313DataSource::RegHSZ:
		return dataValue.SetValue(RegGetHSZ(accessTarget));
	case IS315_5313DataSource::RegHSZ1:
		return dataValue.SetValue(RegGetHSZ1(accessTarget));
	case IS315_5313DataSource::RegHSZ0:
		return dataValue.SetValue(RegGetHSZ0(accessTarget));
	case IS315_5313DataSource::Reg1156:
		return dataValue.SetValue(RegGet1156(accessTarget));
	case IS315_5313DataSource::RegWindowRight:
		return dataValue.SetValue(RegGetWindowRightAligned(accessTarget));
	case IS315_5313DataSource::RegWindowBaseX:
		return dataValue.SetValue(RegGetWindowBasePointX(accessTarget));
	case IS315_5313DataSource::Reg1256:
		return dataValue.SetValue(RegGet1256(accessTarget));
	case IS315_5313DataSource::RegWindowBottom:
		return dataValue.SetValue(RegGetWindowBottomAligned(accessTarget));
	case IS315_5313DataSource::RegWindowBaseY:
		return dataValue.SetValue(RegGetWindowBasePointY(accessTarget));
	case IS315_5313DataSource::RegDMALength:
		return dataValue.SetValue(RegGetDMALengthCounter(accessTarget));
	case IS315_5313DataSource::RegDMASource:
		return dataValue.SetValue(RegGetDMASourceAddress(accessTarget));
	case IS315_5313DataSource::RegDMASourceData1:
		return dataValue.SetValue(RegGetDMASourceAddressByte1(accessTarget));
	case IS315_5313DataSource::RegDMASourceData2:
		return dataValue.SetValue(RegGetDMASourceAddressByte2(accessTarget));
	case IS315_5313DataSource::RegDMASourceData3:
		return dataValue.SetValue(RegGetDMASourceAddressByte3(accessTarget));
	case IS315_5313DataSource::RegDMD1:
		return dataValue.SetValue(RegGetDMD1(accessTarget));
	case IS315_5313DataSource::RegDMD0:
		return dataValue.SetValue(RegGetDMD0(accessTarget));
	case IS315_5313DataSource::RegCode:
		return dataValue.SetValue(commandCode.GetData());
	case IS315_5313DataSource::RegAddress:
		return dataValue.SetValue(commandAddress.GetData());
	case IS315_5313DataSource::RegPortWritePending:
		return dataValue.SetValue(commandWritePending);
	case IS315_5313DataSource::RegReadBuffer:
		return dataValue.SetValue(readBuffer.GetData());
	case IS315_5313DataSource::RegReadHalfCached:
		return dataValue.SetValue(readDataHalfCached);
	case IS315_5313DataSource::RegReadFullyCached:
		return dataValue.SetValue(readDataAvailable);
	case IS315_5313DataSource::RegVINTPending:
		return dataValue.SetValue(vintPending);
	case IS315_5313DataSource::RegHINTPending:
		return dataValue.SetValue(hintPending);
	case IS315_5313DataSource::RegEXINTPending:
		return dataValue.SetValue(exintPending);
	case IS315_5313DataSource::RegHVCounterExternal:
		return dataValue.SetValue(GetHVCounter().GetData());
	case IS315_5313DataSource::RegHCounterInternal:
		return dataValue.SetValue(hcounter.GetData());
	case IS315_5313DataSource::RegVCounterInternal:
		return dataValue.SetValue(vcounter.GetData());
	case IS315_5313DataSource::RegHCounterLatched:
		return dataValue.SetValue(hcounterLatchedData.GetData());
	case IS315_5313DataSource::RegVCounterLatched:
		return dataValue.SetValue(vcounterLatchedData.GetData());
	case IS315_5313DataSource::RegFIFOCode:{
		const FIFOEntryDataContext& fifoEntryDataContext = *((FIFOEntryDataContext*)dataContext);
		return dataValue.SetValue(fifoBuffer[fifoEntryDataContext.entryNo].codeRegData.GetData());}
	case IS315_5313DataSource::RegFIFOAddress:{
		const FIFOEntryDataContext& fifoEntryDataContext = *((FIFOEntryDataContext*)dataContext);
		return dataValue.SetValue(fifoBuffer[fifoEntryDataContext.entryNo].addressRegData.GetData());}
	case IS315_5313DataSource::RegFIFOData:{
		const FIFOEntryDataContext& fifoEntryDataContext = *((FIFOEntryDataContext*)dataContext);
		return dataValue.SetValue(fifoBuffer[fifoEntryDataContext.entryNo].dataPortWriteData.GetData());}
	case IS315_5313DataSource::RegFIFOWritePending:{
		const FIFOEntryDataContext& fifoEntryDataContext = *((FIFOEntryDataContext*)dataContext);
		return dataValue.SetValue(fifoBuffer[fifoEntryDataContext.entryNo].pendingDataWrite);}
	case IS315_5313DataSource::RegFIFOWriteHalfWritten:{
		const FIFOEntryDataContext& fifoEntryDataContext = *((FIFOEntryDataContext*)dataContext);
		return dataValue.SetValue(fifoBuffer[fifoEntryDataContext.entryNo].dataWriteHalfWritten);}
	case IS315_5313DataSource::RegNextFIFOReadEntry:
		return dataValue.SetValue(fifoNextReadEntry);
	case IS315_5313DataSource::RegNextFIFOWriteEntry:
		return dataValue.SetValue(fifoNextWriteEntry);
	case IS315_5313DataSource::SettingsOutputPortAccessDebugMessages:
		return dataValue.SetValue(outputPortAccessDebugMessages);
	case IS315_5313DataSource::SettingsOutputTimingDebugMessages:
		return dataValue.SetValue(outputTimingDebugMessages);
	case IS315_5313DataSource::SettingsOutputRenderSyncDebugMessages:
		return dataValue.SetValue(outputRenderSyncMessages);
	case IS315_5313DataSource::SettingsOutputInterruptDebugMessages:
		return dataValue.SetValue(outputInterruptDebugMessages);
	case IS315_5313DataSource::SettingsVideoDisableRenderOutput:
		return dataValue.SetValue(videoDisableRenderOutput);
	case IS315_5313DataSource::SettingsVideoEnableSpriteBoxing:
		return dataValue.SetValue(videoEnableSpriteBoxing);
	case IS315_5313DataSource::SettingsVideoHighlightRenderPos:
		return dataValue.SetValue(videoHighlightRenderPos);
	case IS315_5313DataSource::SettingsVideoSingleBuffering:
		return dataValue.SetValue(videoSingleBuffering);
	case IS315_5313DataSource::SettingsVideoFixedAspectRatio:
		return dataValue.SetValue(videoFixedAspectRatio);
	case IS315_5313DataSource::SettingsVideoShowStatusBar:
		return dataValue.SetValue(videoShowStatusBar);
	case IS315_5313DataSource::SettingsVideoEnableLineSmoothing:
		return dataValue.SetValue(videoEnableLineSmoothing);
	case IS315_5313DataSource::SettingsCurrentRenderPosOnScreen:
		return dataValue.SetValue(currentRenderPosOnScreen);
	case IS315_5313DataSource::SettingsCurrentRenderPosScreenX:
		return dataValue.SetValue(currentRenderPosScreenX);
	case IS315_5313DataSource::SettingsCurrentRenderPosScreenY:
		return dataValue.SetValue(currentRenderPosScreenY);
	case IS315_5313DataSource::SettingsVideoShowBoundaryActiveImage:
		return dataValue.SetValue(videoShowBoundaryActiveImage);
	case IS315_5313DataSource::SettingsVideoShowBoundaryActionSafe:
		return dataValue.SetValue(videoShowBoundaryActionSafe);
	case IS315_5313DataSource::SettingsVideoShowBoundaryTitleSafe:
		return dataValue.SetValue(videoShowBoundaryTitleSafe);
	case IS315_5313DataSource::SettingsVideoEnableFullImageBufferInfo:
		return dataValue.SetValue(videoEnableFullImageBufferInfo);
	case IS315_5313DataSource::SettingsVideoEnableLayerA:
		return dataValue.SetValue(enableLayerAHigh && enableLayerALow);
	case IS315_5313DataSource::SettingsVideoEnableLayerAHigh:
		return dataValue.SetValue(enableLayerAHigh);
	case IS315_5313DataSource::SettingsVideoEnableLayerALow:
		return dataValue.SetValue(enableLayerALow);
	case IS315_5313DataSource::SettingsVideoEnableLayerB:
		return dataValue.SetValue(enableLayerBHigh && enableLayerBLow);
	case IS315_5313DataSource::SettingsVideoEnableLayerBHigh:
		return dataValue.SetValue(enableLayerBHigh);
	case IS315_5313DataSource::SettingsVideoEnableLayerBLow:
		return dataValue.SetValue(enableLayerBLow);
	case IS315_5313DataSource::SettingsVideoEnableWindow:
		return dataValue.SetValue(enableWindowHigh && enableWindowLow);
	case IS315_5313DataSource::SettingsVideoEnableWindowHigh:
		return dataValue.SetValue(enableWindowHigh);
	case IS315_5313DataSource::SettingsVideoEnableWindowLow:
		return dataValue.SetValue(enableWindowLow);
	case IS315_5313DataSource::SettingsVideoEnableSprite:
		return dataValue.SetValue(enableSpriteHigh && enableSpriteLow);
	case IS315_5313DataSource::SettingsVideoEnableSpriteHigh:
		return dataValue.SetValue(enableSpriteHigh);
	case IS315_5313DataSource::SettingsVideoEnableSpriteLow:
		return dataValue.SetValue(enableSpriteLow);
	}
	return false;
}

//----------------------------------------------------------------------------------------
bool S315_5313::WriteGenericData(unsigned int dataID, const DataContext* dataContext, IGenericAccessDataValue& dataValue)
{
	//Note that if you update this logic, you also need to update the corresponding logic
	//in RegisterSpecialUpdateFunction and TransparentRegisterSpecialUpdateFunction.
	ApplyGenericDataValueLimitSettings(dataID, dataValue);
	IGenericAccessDataValue::DataType dataType = dataValue.GetType();
	AccessTarget accessTarget = AccessTarget().AccessLatest();
	switch((IS315_5313DataSource)dataID)
	{
	case IS315_5313DataSource::RawRegister:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		const RegisterDataContext& registerDataContext = *((RegisterDataContext*)dataContext);
		Data registerData(8, dataValueAsUInt.GetValue());
		std::unique_lock<std::mutex> lock(accessMutex);
		TransparentRegisterSpecialUpdateFunction(registerDataContext.registerNo, registerData);
		SetRegisterData(registerDataContext.registerNo, accessTarget, registerData);
		return true;}
	case IS315_5313DataSource::RegStatus:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		status = dataValueAsUInt.GetValue();
		return true;}
	case IS315_5313DataSource::FlagFIFOEmpty:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		SetStatusFlagFIFOEmpty(dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::FlagFIFOFull:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		SetStatusFlagFIFOEmpty(dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::FlagF:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		SetStatusFlagF(dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::FlagSpriteOverflow:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		SetStatusFlagSpriteOverflow(dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::FlagSpriteCollision:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		SetStatusFlagSpriteCollision(dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::FlagOddInterlaceFrame:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		SetStatusFlagOddInterlaceFrame(dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::FlagVBlank:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		SetStatusFlagVBlank(dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::FlagHBlank:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		SetStatusFlagHBlank(dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::FlagDMA:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		SetStatusFlagDMA(dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::FlagPAL:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		SetStatusFlagPAL(dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::RegVSI:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		RegSetVSI(accessTarget, dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::RegHSI:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		RegSetHSI(accessTarget, dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::RegLCB:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		RegSetLCB(accessTarget, dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::RegIE1:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		RegSetIE1(accessTarget, dataValueAsBool.GetValue());
		hintEnabled = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::RegSS:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		RegSetSS(accessTarget, dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::RegPS:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		RegSetPS(accessTarget, dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::RegM2:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		RegSetM2(accessTarget, dataValueAsBool.GetValue());
		hvCounterLatchEnabled = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::RegES:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		RegSetES(accessTarget, dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::RegEVRAM:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		RegSetEVRAM(accessTarget, dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::RegDisplayEnabled:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		RegSetDisplayEnabled(accessTarget, dataValueAsBool.GetValue());
		displayEnabledCached = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::RegIE0:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		RegSetIE0(accessTarget, dataValueAsBool.GetValue());
		vintEnabled = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::RegDMAEnabled:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		RegSetDMAEnabled(accessTarget, dataValueAsBool.GetValue());
		dmaEnabled = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::RegM3:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		RegSetM3(accessTarget, dataValueAsBool.GetValue());
		screenModeV30Cached  = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::RegMode5:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		RegSetMode5(accessTarget, dataValueAsBool.GetValue());
		screenModeM5Cached = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::RegSZ:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		RegSetSZ(accessTarget, dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::RegMAG:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		RegSetMAG(accessTarget, dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::RegNameTableBaseA:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		RegSetNameTableBaseScrollA(accessTarget, dataValueAsUInt.GetValue());
		return true;}
	case IS315_5313DataSource::RegNameTableBaseWindow:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		RegSetNameTableBaseWindow(accessTarget, dataValueAsUInt.GetValue());
		return true;}
	case IS315_5313DataSource::RegNameTableBaseB:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		RegSetNameTableBaseScrollB(accessTarget, dataValueAsUInt.GetValue());
		return true;}
	case IS315_5313DataSource::RegNameTableBaseSprite:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		RegSetNameTableBaseSprite(accessTarget, dataValueAsUInt.GetValue(), !RegGetMode5(accessTarget));
		spriteAttributeTableBaseAddressDecoded = (dataValueAsUInt.GetValue() << 9);
		return true;}
	case IS315_5313DataSource::RegPatternBaseSprite:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		RegSetPatternBaseSprite(accessTarget, dataValueAsUInt.GetValue(), !RegGetMode5(accessTarget));
		return true;}
	case IS315_5313DataSource::Reg077:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		RegSet077(accessTarget, dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::Reg076:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		RegSet076(accessTarget, dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::RegBackgroundPaletteRow:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		RegSetBackgroundPaletteRow(accessTarget, dataValueAsUInt.GetValue());
		return true;}
	case IS315_5313DataSource::RegBackgroundPaletteColumn:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		RegSetBackgroundPaletteColumn(accessTarget, dataValueAsUInt.GetValue());
		return true;}
	case IS315_5313DataSource::RegBackgroundScrollX:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		RegSetBackgroundScrollX(accessTarget, dataValueAsUInt.GetValue());
		return true;}
	case IS315_5313DataSource::RegBackgroundScrollY:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		RegSetBackgroundScrollY(accessTarget, dataValueAsUInt.GetValue());
		return true;}
	case IS315_5313DataSource::RegHINTData:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		RegSetHInterruptData(accessTarget, dataValueAsUInt.GetValue());
		hintCounterReloadValue = dataValueAsUInt.GetValue();
		return true;}
	case IS315_5313DataSource::Reg0B7:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		RegSet0B7(accessTarget, dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::Reg0B6:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		RegSet0B6(accessTarget, dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::Reg0B5:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		RegSet0B5(accessTarget, dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::Reg0B4:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		RegSet0B4(accessTarget, dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::RegIE2:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		RegSetIE2(accessTarget, dataValueAsBool.GetValue());
		exintEnabled = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::RegVSCR:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		RegSetVSCR(accessTarget, dataValueAsBool.GetValue());
		verticalScrollModeCached = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::RegHSCR:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		RegSetHSCR(accessTarget, dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::RegLSCR:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		RegSetLSCR(accessTarget, dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::RegRS0:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		RegSetRS0(accessTarget, dataValueAsBool.GetValue());
		screenModeRS0Cached = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::RegU1:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		RegSetU1(accessTarget, dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::RegU2:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		RegSetU2(accessTarget, dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::RegU3:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		RegSetU3(accessTarget, dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::RegSTE:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		RegSetSTE(accessTarget, dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::RegLSM1:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		RegSetLSM1(accessTarget, dataValueAsBool.GetValue());
		interlaceDoubleCached = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::RegLSM0:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		RegSetLSM0(accessTarget, dataValueAsBool.GetValue());
		interlaceEnabledCached = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::RegRS1:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		RegSetRS1(accessTarget, dataValueAsBool.GetValue());
		screenModeRS1Cached = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::RegHScrollDataBase:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		RegSetHScrollDataBase(accessTarget, dataValueAsUInt.GetValue());
		return true;}
	case IS315_5313DataSource::Reg0E57:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		RegSet0E57(accessTarget, dataValueAsUInt.GetValue());
		return true;}
	case IS315_5313DataSource::RegPatternBaseA:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		RegSetPatternBaseScrollA(accessTarget, dataValueAsUInt.GetValue());
		return true;}
	case IS315_5313DataSource::Reg0E13:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		RegSet0E13(accessTarget, dataValueAsUInt.GetValue());
		return true;}
	case IS315_5313DataSource::RegPatternBaseB:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		RegSetPatternBaseScrollB(accessTarget, dataValueAsUInt.GetValue());
		return true;}
	case IS315_5313DataSource::RegAutoIncrementData:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		RegSetAutoIncrementData(accessTarget, dataValueAsUInt.GetValue());
		autoIncrementData = dataValueAsUInt.GetValue();
		return true;}
	case IS315_5313DataSource::Reg1067:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		RegSet1067(accessTarget, dataValueAsUInt.GetValue());
		return true;}
	case IS315_5313DataSource::RegVSZ:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		RegSetVSZ(accessTarget, dataValueAsUInt.GetValue());
		return true;}
	case IS315_5313DataSource::RegVSZ1:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		RegSetVSZ1(accessTarget, dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::RegVSZ0:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		RegSetVSZ0(accessTarget, dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::Reg1023:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		RegSet1023(accessTarget, dataValueAsUInt.GetValue());
		return true;}
	case IS315_5313DataSource::RegHSZ:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		RegSetHSZ(accessTarget, dataValueAsUInt.GetValue());
		return true;}
	case IS315_5313DataSource::RegHSZ1:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		RegSetHSZ1(accessTarget, dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::RegHSZ0:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		RegSetHSZ0(accessTarget, dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::Reg1156:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		RegSet1156(accessTarget, dataValueAsUInt.GetValue());
		return true;}
	case IS315_5313DataSource::RegWindowRight:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		RegSetWindowRightAligned(accessTarget, dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::RegWindowBaseX:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		RegSetWindowBasePointX(accessTarget, dataValueAsUInt.GetValue());
		return true;}
	case IS315_5313DataSource::Reg1256:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		RegSet1256(accessTarget, dataValueAsUInt.GetValue());
		return true;}
	case IS315_5313DataSource::RegWindowBottom:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		RegSetWindowBottomAligned(accessTarget, dataValueAsBool.GetValue());
		return true;}
	case IS315_5313DataSource::RegWindowBaseY:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		RegSetWindowBasePointY(accessTarget, dataValueAsUInt.GetValue());
		return true;}
	case IS315_5313DataSource::RegDMALength:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		RegSetDMALengthCounter(accessTarget, dataValueAsUInt.GetValue());
		dmaLengthCounter = dataValueAsUInt.GetValue();
		return true;}
	case IS315_5313DataSource::RegDMASource:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		RegSetDMASourceAddress(accessTarget, dataValueAsUInt.GetValue());
		dmaSourceAddressByte1 = dataValueAsUInt.GetValue() & 0xFF;
		dmaSourceAddressByte2 = (dataValueAsUInt.GetValue() >> 8) & 0xFF;
		dmaSourceAddressByte3 = (dataValueAsUInt.GetValue() >> 16) & 0x7F;
		return true;}
	case IS315_5313DataSource::RegDMASourceData1:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		RegSetDMASourceAddressByte1(accessTarget, dataValueAsUInt.GetValue());
		dmaSourceAddressByte1 = dataValueAsUInt.GetValue();
		return true;}
	case IS315_5313DataSource::RegDMASourceData2:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		RegSetDMASourceAddressByte2(accessTarget, dataValueAsUInt.GetValue());
		dmaSourceAddressByte2 = dataValueAsUInt.GetValue();
		return true;}
	case IS315_5313DataSource::RegDMASourceData3:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		RegSetDMASourceAddressByte3(accessTarget, dataValueAsUInt.GetValue());
		dmaSourceAddressByte3 = dataValueAsUInt.GetValue() & 0x7F;
		return true;}
	case IS315_5313DataSource::RegDMD1:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		RegSetDMD1(accessTarget, dataValueAsBool.GetValue());
		dmd1 = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::RegDMD0:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		RegSetDMD0(accessTarget, dataValueAsBool.GetValue());
		dmd0 = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::RegCode:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		commandCode = dataValueAsUInt.GetValue();
		return true;}
	case IS315_5313DataSource::RegAddress:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		commandAddress = dataValueAsUInt.GetValue();
		return true;}
	case IS315_5313DataSource::RegPortWritePending:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		commandWritePending = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::RegReadBuffer:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		readBuffer = dataValueAsUInt.GetValue();
		return true;}
	case IS315_5313DataSource::RegReadHalfCached:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		readDataHalfCached = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::RegReadFullyCached:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		readDataAvailable = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::RegVINTPending:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		vintPending = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::RegHINTPending:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		hintPending = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::RegEXINTPending:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		exintPending = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::RegHCounterInternal:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		hcounter = dataValueAsUInt.GetValue();
		return true;}
	case IS315_5313DataSource::RegVCounterInternal:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		vcounter = dataValueAsUInt.GetValue();
		return true;}
	case IS315_5313DataSource::RegHCounterLatched:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		hcounterLatchedData = dataValueAsUInt.GetValue();
		return true;}
	case IS315_5313DataSource::RegVCounterLatched:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		vcounterLatchedData = dataValueAsUInt.GetValue();
		return true;}
	case IS315_5313DataSource::RegFIFOCode:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		const FIFOEntryDataContext& fifoEntryDataContext = *((FIFOEntryDataContext*)dataContext);
		std::unique_lock<std::mutex> lock(accessMutex);
		fifoBuffer[fifoEntryDataContext.entryNo].codeRegData = dataValueAsUInt.GetValue();
		return true;}
	case IS315_5313DataSource::RegFIFOAddress:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		const FIFOEntryDataContext& fifoEntryDataContext = *((FIFOEntryDataContext*)dataContext);
		std::unique_lock<std::mutex> lock(accessMutex);
		fifoBuffer[fifoEntryDataContext.entryNo].addressRegData = dataValueAsUInt.GetValue();
		return true;}
	case IS315_5313DataSource::RegFIFOData:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		const FIFOEntryDataContext& fifoEntryDataContext = *((FIFOEntryDataContext*)dataContext);
		std::unique_lock<std::mutex> lock(accessMutex);
		fifoBuffer[fifoEntryDataContext.entryNo].dataPortWriteData = dataValueAsUInt.GetValue();
		return true;}
	case IS315_5313DataSource::RegFIFOWritePending:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		const FIFOEntryDataContext& fifoEntryDataContext = *((FIFOEntryDataContext*)dataContext);
		std::unique_lock<std::mutex> lock(accessMutex);
		fifoBuffer[fifoEntryDataContext.entryNo].pendingDataWrite = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::RegFIFOWriteHalfWritten:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		const FIFOEntryDataContext& fifoEntryDataContext = *((FIFOEntryDataContext*)dataContext);
		std::unique_lock<std::mutex> lock(accessMutex);
		fifoBuffer[fifoEntryDataContext.entryNo].dataWriteHalfWritten = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::RegNextFIFOReadEntry:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		fifoNextReadEntry = dataValueAsUInt.GetValue();
		return true;}
	case IS315_5313DataSource::RegNextFIFOWriteEntry:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		std::unique_lock<std::mutex> lock(accessMutex);
		fifoNextWriteEntry = dataValueAsUInt.GetValue();
		return true;}
	case IS315_5313DataSource::SettingsOutputPortAccessDebugMessages:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		outputPortAccessDebugMessages = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::SettingsOutputTimingDebugMessages:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		outputTimingDebugMessages = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::SettingsOutputRenderSyncDebugMessages:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		outputRenderSyncMessages = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::SettingsOutputInterruptDebugMessages:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		outputInterruptDebugMessages = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::SettingsVideoDisableRenderOutput:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		videoDisableRenderOutput = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::SettingsVideoEnableSpriteBoxing:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		videoEnableSpriteBoxing = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::SettingsVideoHighlightRenderPos:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		videoHighlightRenderPos = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::SettingsVideoSingleBuffering:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		videoSingleBuffering = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::SettingsVideoFixedAspectRatio:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		videoFixedAspectRatio = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::SettingsVideoShowStatusBar:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		videoShowStatusBar = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::SettingsVideoEnableLineSmoothing:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		videoEnableLineSmoothing = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::SettingsCurrentRenderPosOnScreen:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		currentRenderPosOnScreen = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::SettingsCurrentRenderPosScreenX:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		currentRenderPosScreenX = dataValueAsUInt.GetValue();
		return true;}
	case IS315_5313DataSource::SettingsCurrentRenderPosScreenY:{
		if(dataType != IGenericAccessDataValue::DataType::UInt) return false;
		IGenericAccessDataValueUInt& dataValueAsUInt = (IGenericAccessDataValueUInt&)dataValue;
		currentRenderPosScreenY = dataValueAsUInt.GetValue();
		return true;}
	case IS315_5313DataSource::SettingsVideoShowBoundaryActiveImage:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		videoShowBoundaryActiveImage = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::SettingsVideoShowBoundaryActionSafe:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		videoShowBoundaryActionSafe = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::SettingsVideoShowBoundaryTitleSafe:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		videoShowBoundaryTitleSafe = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::SettingsVideoEnableFullImageBufferInfo:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		videoEnableFullImageBufferInfo = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::SettingsVideoEnableLayerA:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		enableLayerAHigh = enableLayerALow = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::SettingsVideoEnableLayerAHigh:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		enableLayerAHigh = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::SettingsVideoEnableLayerALow:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		enableLayerALow = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::SettingsVideoEnableLayerB:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		enableLayerBHigh = enableLayerBLow = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::SettingsVideoEnableLayerBHigh:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		enableLayerBHigh = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::SettingsVideoEnableLayerBLow:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		enableLayerBLow = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::SettingsVideoEnableWindow:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		enableWindowHigh = enableWindowLow = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::SettingsVideoEnableWindowHigh:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		enableWindowHigh = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::SettingsVideoEnableWindowLow:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		enableWindowLow = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::SettingsVideoEnableSprite:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		enableSpriteHigh = enableSpriteLow = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::SettingsVideoEnableSpriteHigh:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		enableSpriteHigh = dataValueAsBool.GetValue();
		return true;}
	case IS315_5313DataSource::SettingsVideoEnableSpriteLow:{
		if(dataType != IGenericAccessDataValue::DataType::Bool) return false;
		IGenericAccessDataValueBool& dataValueAsBool = (IGenericAccessDataValueBool&)dataValue;
		enableSpriteLow = dataValueAsBool.GetValue();
		return true;}
	}
	return false;
}

//----------------------------------------------------------------------------------------
//Data locking functions
//----------------------------------------------------------------------------------------
bool S315_5313::GetGenericDataLocked(unsigned int dataID, const DataContext* dataContext) const
{
	std::unique_lock<std::mutex> lock(registerLockMutex);
	switch((IS315_5313DataSource)dataID)
	{
	case IS315_5313DataSource::RawRegister:{
		const RegisterDataContext& registerDataContext = *((RegisterDataContext*)dataContext);
		return rawRegisterLocking[registerDataContext.registerNo];}
	case IS315_5313DataSource::FlagFIFOEmpty:
	case IS315_5313DataSource::FlagFIFOFull:
	case IS315_5313DataSource::FlagF:
	case IS315_5313DataSource::FlagSpriteOverflow:
	case IS315_5313DataSource::FlagSpriteCollision:
	case IS315_5313DataSource::FlagOddInterlaceFrame:
	case IS315_5313DataSource::FlagVBlank:
	case IS315_5313DataSource::FlagHBlank:
	case IS315_5313DataSource::FlagDMA:
	case IS315_5313DataSource::FlagPAL:
	case IS315_5313DataSource::RegVSI:
	case IS315_5313DataSource::RegHSI:
	case IS315_5313DataSource::RegLCB:
	case IS315_5313DataSource::RegIE1:
	case IS315_5313DataSource::RegSS:
	case IS315_5313DataSource::RegPS:
	case IS315_5313DataSource::RegM2:
	case IS315_5313DataSource::RegES:
	case IS315_5313DataSource::RegEVRAM:
	case IS315_5313DataSource::RegDisplayEnabled:
	case IS315_5313DataSource::RegIE0:
	case IS315_5313DataSource::RegDMAEnabled:
	case IS315_5313DataSource::RegM3:
	case IS315_5313DataSource::RegMode5:
	case IS315_5313DataSource::RegSZ:
	case IS315_5313DataSource::RegMAG:
	case IS315_5313DataSource::RegNameTableBaseA:
	case IS315_5313DataSource::RegNameTableBaseWindow:
	case IS315_5313DataSource::RegNameTableBaseB:
	case IS315_5313DataSource::RegNameTableBaseSprite:
	case IS315_5313DataSource::RegPatternBaseSprite:
	case IS315_5313DataSource::Reg077:
	case IS315_5313DataSource::Reg076:
	case IS315_5313DataSource::RegBackgroundPaletteRow:
	case IS315_5313DataSource::RegBackgroundPaletteColumn:
	case IS315_5313DataSource::RegBackgroundScrollX:
	case IS315_5313DataSource::RegBackgroundScrollY:
	case IS315_5313DataSource::RegHINTData:
	case IS315_5313DataSource::Reg0B7:
	case IS315_5313DataSource::Reg0B6:
	case IS315_5313DataSource::Reg0B5:
	case IS315_5313DataSource::Reg0B4:
	case IS315_5313DataSource::RegIE2:
	case IS315_5313DataSource::RegVSCR:
	case IS315_5313DataSource::RegHSCR:
	case IS315_5313DataSource::RegLSCR:
	case IS315_5313DataSource::RegRS0:
	case IS315_5313DataSource::RegU1:
	case IS315_5313DataSource::RegU2:
	case IS315_5313DataSource::RegU3:
	case IS315_5313DataSource::RegSTE:
	case IS315_5313DataSource::RegLSM1:
	case IS315_5313DataSource::RegLSM0:
	case IS315_5313DataSource::RegRS1:
	case IS315_5313DataSource::RegHScrollDataBase:
	case IS315_5313DataSource::Reg0E57:
	case IS315_5313DataSource::RegPatternBaseA:
	case IS315_5313DataSource::Reg0E13:
	case IS315_5313DataSource::RegPatternBaseB:
	case IS315_5313DataSource::RegAutoIncrementData:
	case IS315_5313DataSource::Reg1067:
	case IS315_5313DataSource::RegVSZ:
	case IS315_5313DataSource::RegVSZ1:
	case IS315_5313DataSource::RegVSZ0:
	case IS315_5313DataSource::Reg1023:
	case IS315_5313DataSource::RegHSZ:
	case IS315_5313DataSource::RegHSZ1:
	case IS315_5313DataSource::RegHSZ0:
	case IS315_5313DataSource::Reg1156:
	case IS315_5313DataSource::RegWindowRight:
	case IS315_5313DataSource::RegWindowBaseX:
	case IS315_5313DataSource::Reg1256:
	case IS315_5313DataSource::RegWindowBottom:
	case IS315_5313DataSource::RegWindowBaseY:
	case IS315_5313DataSource::RegDMALength:
	case IS315_5313DataSource::RegDMASource:
	case IS315_5313DataSource::RegDMASourceData1:
	case IS315_5313DataSource::RegDMASourceData2:
	case IS315_5313DataSource::RegDMASourceData3:
	case IS315_5313DataSource::RegDMD1:
	case IS315_5313DataSource::RegDMD0:
		return (lockedRegisterState.find(dataID) != lockedRegisterState.end());
	}
	return false;
}

//----------------------------------------------------------------------------------------
bool S315_5313::SetGenericDataLocked(unsigned int dataID, const DataContext* dataContext, bool state)
{
	switch((IS315_5313DataSource)dataID)
	{
	case IS315_5313DataSource::RawRegister:{
		const RegisterDataContext& registerDataContext = *((RegisterDataContext*)dataContext);
		rawRegisterLocking[registerDataContext.registerNo] = state;
		return true;}
	case IS315_5313DataSource::FlagFIFOEmpty:
	case IS315_5313DataSource::FlagFIFOFull:
	case IS315_5313DataSource::FlagF:
	case IS315_5313DataSource::FlagSpriteOverflow:
	case IS315_5313DataSource::FlagSpriteCollision:
	case IS315_5313DataSource::FlagOddInterlaceFrame:
	case IS315_5313DataSource::FlagVBlank:
	case IS315_5313DataSource::FlagHBlank:
	case IS315_5313DataSource::FlagDMA:
	case IS315_5313DataSource::FlagPAL:
	case IS315_5313DataSource::RegVSI:
	case IS315_5313DataSource::RegHSI:
	case IS315_5313DataSource::RegLCB:
	case IS315_5313DataSource::RegIE1:
	case IS315_5313DataSource::RegSS:
	case IS315_5313DataSource::RegPS:
	case IS315_5313DataSource::RegM2:
	case IS315_5313DataSource::RegES:
	case IS315_5313DataSource::RegEVRAM:
	case IS315_5313DataSource::RegDisplayEnabled:
	case IS315_5313DataSource::RegIE0:
	case IS315_5313DataSource::RegDMAEnabled:
	case IS315_5313DataSource::RegM3:
	case IS315_5313DataSource::RegMode5:
	case IS315_5313DataSource::RegSZ:
	case IS315_5313DataSource::RegMAG:
	case IS315_5313DataSource::RegNameTableBaseA:
	case IS315_5313DataSource::RegNameTableBaseWindow:
	case IS315_5313DataSource::RegNameTableBaseB:
	case IS315_5313DataSource::RegNameTableBaseSprite:
	case IS315_5313DataSource::RegPatternBaseSprite:
	case IS315_5313DataSource::Reg077:
	case IS315_5313DataSource::Reg076:
	case IS315_5313DataSource::RegBackgroundPaletteRow:
	case IS315_5313DataSource::RegBackgroundPaletteColumn:
	case IS315_5313DataSource::RegBackgroundScrollX:
	case IS315_5313DataSource::RegBackgroundScrollY:
	case IS315_5313DataSource::RegHINTData:
	case IS315_5313DataSource::Reg0B7:
	case IS315_5313DataSource::Reg0B6:
	case IS315_5313DataSource::Reg0B5:
	case IS315_5313DataSource::Reg0B4:
	case IS315_5313DataSource::RegIE2:
	case IS315_5313DataSource::RegVSCR:
	case IS315_5313DataSource::RegHSCR:
	case IS315_5313DataSource::RegLSCR:
	case IS315_5313DataSource::RegRS0:
	case IS315_5313DataSource::RegU1:
	case IS315_5313DataSource::RegU2:
	case IS315_5313DataSource::RegU3:
	case IS315_5313DataSource::RegSTE:
	case IS315_5313DataSource::RegLSM1:
	case IS315_5313DataSource::RegLSM0:
	case IS315_5313DataSource::RegRS1:
	case IS315_5313DataSource::RegHScrollDataBase:
	case IS315_5313DataSource::Reg0E57:
	case IS315_5313DataSource::RegPatternBaseA:
	case IS315_5313DataSource::Reg0E13:
	case IS315_5313DataSource::RegPatternBaseB:
	case IS315_5313DataSource::RegAutoIncrementData:
	case IS315_5313DataSource::Reg1067:
	case IS315_5313DataSource::RegVSZ:
	case IS315_5313DataSource::RegVSZ1:
	case IS315_5313DataSource::RegVSZ0:
	case IS315_5313DataSource::Reg1023:
	case IS315_5313DataSource::RegHSZ:
	case IS315_5313DataSource::RegHSZ1:
	case IS315_5313DataSource::RegHSZ0:
	case IS315_5313DataSource::Reg1156:
	case IS315_5313DataSource::RegWindowRight:
	case IS315_5313DataSource::RegWindowBaseX:
	case IS315_5313DataSource::Reg1256:
	case IS315_5313DataSource::RegWindowBottom:
	case IS315_5313DataSource::RegWindowBaseY:
	case IS315_5313DataSource::RegDMALength:
	case IS315_5313DataSource::RegDMASource:
	case IS315_5313DataSource::RegDMASourceData1:
	case IS315_5313DataSource::RegDMASourceData2:
	case IS315_5313DataSource::RegDMASourceData3:
	case IS315_5313DataSource::RegDMD1:
	case IS315_5313DataSource::RegDMD0:{
		std::unique_lock<std::mutex> lock(registerLockMutex);
		if(!state)
		{
			lockedRegisterState.erase(dataID);
		}
		else if(lockedRegisterState.find(dataID) == lockedRegisterState.end())
		{
			std::wstring lockedDataValue;
			if(!ReadGenericData(dataID, dataContext, lockedDataValue))
			{
				return false;
			}
			lockedRegisterState[dataID] = lockedDataValue;
		}
		return true;}
	}
	return false;
}
