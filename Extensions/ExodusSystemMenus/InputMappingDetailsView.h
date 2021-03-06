#ifndef __INPUTMAPPINGDETAILSVIEW_H__
#define __INPUTMAPPINGDETAILSVIEW_H__
#include "WindowsSupport/WindowsSupport.pkg"
#include "DeviceInterface/DeviceInterface.pkg"
#include "InputMappingDetailsViewPresenter.h"
#include <mutex>
#include <condition_variable>

class InputMappingDetailsView :public ViewBase
{
public:
	//Constructors
	InputMappingDetailsView(IUIManager& auiManager, InputMappingDetailsViewPresenter& apresenter, ISystemGUIInterface& amodel, IDevice* atargetDevice);

	//Target device functions
	void SetTargetDevice(IDevice* atargetDevice);

protected:
	//Member window procedure
	virtual INT_PTR WndProcDialog(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

private:
	//Event handlers
	INT_PTR msgWM_INITDIALOG(HWND hwnd, WPARAM wParam, LPARAM lParam);
	INT_PTR msgWM_DESTROY(HWND hwnd, WPARAM wParam, LPARAM lParam);
	INT_PTR msgWM_COMMAND(HWND hwnd, WPARAM wParam, LPARAM lParam);
	INT_PTR msgWM_BOUNCE(HWND hwnd, WPARAM wParam, LPARAM lParam);
	INT_PTR msgWM_USER(HWND hwnd, WPARAM wParam, LPARAM lParam);

	//Target device update functions
	void UpdateTargetDeviceInputMappingsDisplay(HWND hwnd);
	void UpdateSelectedInputRegistration(HWND hwnd);

	//Joystick functions
	void JoystickKeyMappingWorkerThread();

private:
	InputMappingDetailsViewPresenter& presenter;
	ISystemGUIInterface& model;
	bool initializedDialog;
	bool systemKeyInputFieldInFocus;
	HWND hwndInternal;
	IDevice* targetDevice;
	std::vector<unsigned int> deviceKeyCodeMap;
	int selectedInputRegistration;
	volatile bool autoKeyMappingActive;
	mutable std::mutex joystickWorkerThreadMutex;
	volatile bool joystickWorkerThreadActive;
	std::condition_variable joystickWorkerThreadStopped;
};

#endif
