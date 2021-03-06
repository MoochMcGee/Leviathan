#include <set>

//----------------------------------------------------------------------------------------
//Enumerations
//----------------------------------------------------------------------------------------
enum class System::InputEvent
{
	KeyDown,
	KeyUp
};

//----------------------------------------------------------------------------------------
enum class System::SystemStateChangeType
{
	SetSystemLineState,
	SetClockFrequency,
	SetSystemOption
};

//----------------------------------------------------------------------------------------
//Structures
//----------------------------------------------------------------------------------------
struct System::DeviceLibraryEntry
{
	DeviceLibraryEntry()
	:Allocator(0), Destructor(0), assemblyHandle(0), versionNo(0)
	{}

	IDeviceInfo::AllocatorPointer Allocator;
	IDeviceInfo::DestructorPointer Destructor;
	std::wstring className;
	std::wstring implementationName;
	unsigned int versionNo;
	std::wstring copyright;
	std::wstring comments;
	AssemblyHandle assemblyHandle;
};

//----------------------------------------------------------------------------------------
struct System::ExtensionLibraryEntry
{
	ExtensionLibraryEntry()
	:Allocator(0), Destructor(0), assemblyHandle(0), versionNo(0), persistentGlobalExtension(false)
	{}

	IExtensionInfo::AllocatorPointer Allocator;
	IExtensionInfo::DestructorPointer Destructor;
	std::wstring className;
	std::wstring implementationName;
	unsigned int versionNo;
	std::wstring copyright;
	std::wstring comments;
	bool persistentGlobalExtension;
	AssemblyHandle assemblyHandle;
};

//----------------------------------------------------------------------------------------
struct System::LoadedDeviceInfo
{
	IDevice* device;
	DeviceContext* deviceContext;
	std::wstring name;
	std::wstring displayName;
	unsigned int moduleID;
	std::set<IExtension*> menuHandlers;
};

//----------------------------------------------------------------------------------------
struct System::ExportedDeviceInfo
{
	IDevice* device;
	DeviceContext* deviceContext;
	std::wstring exportingModuleDeviceInstanceName;
	std::wstring importName;
};

//----------------------------------------------------------------------------------------
struct System::ImportedDeviceInfo
{
	IDevice* device;
	DeviceContext* deviceContext;
	std::wstring exportingModuleDeviceInstanceName;
	std::wstring importName;
	unsigned int connectorID;
	unsigned int exportingModuleID;
	unsigned int importingModuleID;
	std::wstring importingModuleDeviceInstanceName;
};

//----------------------------------------------------------------------------------------
struct System::LoadedExtensionInfo
{
	IExtension* extension;
	std::wstring name;
	unsigned int moduleID;
	std::set<IExtension*> menuHandlers;
};

//----------------------------------------------------------------------------------------
struct System::LoadedGlobalExtensionInfo
{
	IExtension* extension;
	std::wstring name;
	bool persistentExtension;
	std::set<unsigned int> moduleIDs;
	std::set<IExtension*> menuHandlers;
};

//----------------------------------------------------------------------------------------
struct System::ExportedExtensionInfo
{
	IExtension* extension;
	std::wstring exportingModuleExtensionInstanceName;
	std::wstring importName;
};

//----------------------------------------------------------------------------------------
struct System::ImportedExtensionInfo
{
	IExtension* extension;
	std::wstring exportingModuleExtensionInstanceName;
	std::wstring importName;
	unsigned int connectorID;
	unsigned int exportingModuleID;
	unsigned int importingModuleID;
	std::wstring importingModuleExtensionInstanceName;
};

//----------------------------------------------------------------------------------------
struct System::LoadedBusInfo
{
	BusInterface* busInterface;
	std::wstring name;
	unsigned int moduleID;
};

//----------------------------------------------------------------------------------------
struct System::ExportedBusInfo
{
	BusInterface* busInterface;
	std::wstring exportingModuleBusInterfaceName;
	std::wstring importName;
	std::list<ExportedLineGroupInfo> exportedLineGroups;
};

//----------------------------------------------------------------------------------------
struct System::ImportedBusInfo
{
	BusInterface* busInterface;
	std::wstring exportingModuleBusInterfaceName;
	std::wstring importName;
	unsigned int connectorID;
	unsigned int exportingModuleID;
	unsigned int importingModuleID;
	std::wstring importingModuleBusInterfaceName;
	std::list<ImportedLineGroupInfo> importedLineGroups;
};

//----------------------------------------------------------------------------------------
struct System::LoadedClockSourceInfo
{
	ClockSource* clockSource;
	std::wstring name;
	unsigned int moduleID;
};

//----------------------------------------------------------------------------------------
struct System::ExportedClockSourceInfo
{
	ClockSource* clockSource;
	std::wstring exportingModuleClockSourceName;
	std::wstring importName;
};

//----------------------------------------------------------------------------------------
struct System::ImportedClockSourceInfo
{
	ClockSource* clockSource;
	std::wstring exportingModuleClockSourceName;
	std::wstring importName;
	unsigned int connectorID;
	unsigned int exportingModuleID;
	unsigned int importingModuleID;
	std::wstring importingModuleClockSourceName;
};

//----------------------------------------------------------------------------------------
struct System::ExportedLineGroupInfo
{
	unsigned int lineGroupID;
	std::wstring importName;
	std::wstring localName;
};

//----------------------------------------------------------------------------------------
struct System::LineGroupDetails
{
	BusInterface* busInterface;
	std::wstring lineGroupName;
	unsigned int lineGroupID;
};

//----------------------------------------------------------------------------------------
struct System::ImportedLineGroupInfo
{
	unsigned int lineGroupID;
	std::wstring importName;
	std::wstring localName;
};

//----------------------------------------------------------------------------------------
struct System::LoadedModuleInfoInternal
{
	LoadedModuleInfoInternal()
	:moduleValidated(false), programModule(false)
	{}

	//Internal data
	unsigned int moduleID;
	bool moduleValidated;

	//External information
	std::wstring filePath;

	//Required metadata
	bool programModule;
	std::wstring systemClassName;
	std::wstring className;
	std::wstring instanceName;
	std::wstring displayName;

	//Optional metadata
	std::wstring productionYear;
	std::wstring manufacturerCode;
	std::wstring manufacturerDisplayName;

	//Menu handlers
	std::set<IExtension*> menuHandlers;
};

//----------------------------------------------------------------------------------------
struct System::ConnectorInfoInternal
{
	//Internal data
	unsigned int connectorID;

	//Exporting module info
	unsigned int exportingModuleID;
	std::wstring connectorClassName;
	std::wstring exportingModuleConnectorInstanceName;
	std::wstring systemClassName;

	//Exported objects
	std::map<std::wstring, ExportedDeviceInfo> exportedDeviceInfo;
	std::map<std::wstring, ExportedExtensionInfo> exportedExtensionInfo;
	std::map<std::wstring, ExportedBusInfo> exportedBusInfo;
	std::map<std::wstring, ExportedClockSourceInfo> exportedClockSourceInfo;
	std::map<std::wstring, ExportedSystemLineInfo> exportedSystemLineInfo;
	std::map<std::wstring, ExportedSystemSettingInfo> exportedSystemSettingInfo;

	//Importing module info
	bool connectorUsed;
	unsigned int importingModuleID;
	std::wstring importingModuleConnectorInstanceName;
};

//----------------------------------------------------------------------------------------
struct System::InputMapEntry
{
	KeyCode keyCode;
	IDevice* targetDevice;
	unsigned int targetDeviceKeyCode;
};

//----------------------------------------------------------------------------------------
struct System::InputEventEntry
{
	InputEvent inputEvent;
	KeyCode keyCode;
	bool sent;
};

//----------------------------------------------------------------------------------------
struct System::ViewOpenRequest
{
	IViewPresenter::ViewTarget viewTarget;
	unsigned int moduleID;
	std::wstring viewGroupName;
	std::wstring viewName;
	std::wstring deviceInstanceName;
	std::wstring extensionInstanceName;
	bool globalExtension;
};

//----------------------------------------------------------------------------------------
struct System::InputRegistration
{
	InputRegistration()
	:targetDevice(0), preferredSystemKeyCodeSpecified(false)
	{}

	unsigned int moduleID;
	IDevice* targetDevice;
	unsigned int deviceKeyCode;
	bool preferredSystemKeyCodeSpecified;
	KeyCode systemKeyCode;
};

//----------------------------------------------------------------------------------------
struct System::UnmappedLineStateInfo
{
	UnmappedLineStateInfo(unsigned int amoduleID, IDevice* atargetDevice, unsigned int alineNo, Data alineData)
	:moduleID(amoduleID), targetDevice(atargetDevice), lineNo(alineNo), lineData(alineData)
	{}

	unsigned int moduleID;
	IDevice* targetDevice;
	unsigned int lineNo;
	Data lineData;
};

//----------------------------------------------------------------------------------------
struct System::SystemSettingInfo
{
	SystemSettingInfo()
	:selectedOption(0), defaultOption(0), toggleSetting(false), menuItemEntry(0), onOption(0), offOption(0), settingChangeLeadInTimeRandom(false), settingChangeLeadInTime(0), settingChangeLeadInTimeEnd(0), toggleSettingAutoRevert(false), toggleSettingAutoRevertTimeRandom(0), toggleSettingAutoRevertTime(0), toggleSettingAutoRevertTimeEnd(0)
	{}

	unsigned int moduleID;
	std::wstring name;
	unsigned int systemSettingID;
	std::wstring displayName;
	std::vector<SystemSettingOption> options;
	unsigned int defaultOption;
	unsigned int selectedOption;
	bool toggleSetting;
	//##TODO## Remove this information from here. Instead of a direct link to a single
	//menu item being exposed here, provide a way for observers to subscribe to change
	//notifications with a callback, and trigger that callback when system options change.
	//This will allow our option menu handler to update its own menu checked state in
	//response to a system settings change, without any dependency from the system itself.
	unsigned int menuItemID;
	IMenuSelectableOption* menuItemEntry;

	unsigned int onOption;
	unsigned int offOption;

	bool settingChangeLeadInTimeRandom;
	double settingChangeLeadInTime;
	double settingChangeLeadInTimeEnd;

	bool toggleSettingAutoRevert;
	bool toggleSettingAutoRevertTimeRandom;
	double toggleSettingAutoRevertTime;
	double toggleSettingAutoRevertTimeEnd;

	//##FIX## This collection isn't copyable
	ObserverCollection settingChangeObservers;
};

//----------------------------------------------------------------------------------------
struct System::SystemSettingOption
{
	SystemSettingOption()
	:menuItemEntry(0)
	{}

	std::wstring name;
	std::wstring displayName;
	std::list<SystemStateChange> stateChanges;
	//##TODO## Remove this information from here. Instead of a direct link to a single
	//menu item being exposed here, provide a way for observers to subscribe to change
	//notifications with a callback, and trigger that callback when system options change.
	//This will allow our option menu handler to update its own menu checked state in
	//response to a system settings change, without any dependency from the system itself.
	unsigned int menuItemID;
	IMenuSelectableOption* menuItemEntry;
};

//----------------------------------------------------------------------------------------
struct System::SystemStateChange
{
	unsigned int moduleID;
	SystemStateChangeType type;
	std::wstring targetElementName;
	unsigned int setLineStateValue;
	ClockSource::ClockType setClockRateClockType;
	double setClockRateValue;
	std::wstring setSystemOptionValue;
};

//----------------------------------------------------------------------------------------
struct System::SystemLineInfo
{
	unsigned int moduleID;
	std::wstring lineName;
	unsigned int lineID;
	unsigned int lineWidth;
	unsigned int currentValue;
};

//----------------------------------------------------------------------------------------
struct System::ExportedSystemLineInfo
{
	unsigned int systemLineID;
	std::wstring exportingModuleSystemLineName;
	std::wstring importName;
};

//----------------------------------------------------------------------------------------
struct System::ImportedSystemLineInfo
{
	unsigned int systemLineID;
	std::wstring exportingModuleSystemLineName;
	std::wstring importName;
	unsigned int connectorID;
	unsigned int exportingModuleID;
	unsigned int importingModuleID;
	std::wstring importingModuleSystemLineName;
};

//----------------------------------------------------------------------------------------
struct System::ExportedSystemSettingInfo
{
	unsigned int systemSettingID;
	std::wstring exportingModuleSystemSettingName;
	std::wstring importName;
};

//----------------------------------------------------------------------------------------
struct System::ImportedSystemSettingInfo
{
	unsigned int systemSettingID;
	std::wstring exportingModuleSystemSettingName;
	std::wstring importName;
	unsigned int connectorID;
	unsigned int exportingModuleID;
	unsigned int importingModuleID;
	std::wstring importingModuleSystemSettingName;
};

//----------------------------------------------------------------------------------------
struct System::SystemLineMapping
{
	SystemLineMapping()
	:targetDevice(0),
	 lineMaskAND(0xFFFFFFFF),
	 lineMaskOR(0),
	 lineMaskXOR(0)
	{}

	unsigned int moduleID;
	IDevice* targetDevice;
	unsigned int systemLineID;
	unsigned int systemLineBitCount;
	unsigned int targetLine;
	unsigned int targetLineBitCount;

	unsigned int lineMaskAND;
	unsigned int lineMaskOR;
	unsigned int lineMaskXOR;

	bool remapLines;
	DataRemapTable lineRemapTable;
};

//----------------------------------------------------------------------------------------
struct System::EmbeddedROMInfoInternal
{
	std::wstring embeddedROMName;
	std::wstring displayName;
	unsigned int moduleID;
	IDevice* targetDevice;
	unsigned int interfaceNumber;
	unsigned int romRegionSize;
	unsigned int romEntryBitCount;
	std::wstring filePath;
};
