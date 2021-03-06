#include "VRAMViewPresenter.h"
#include "VRAMView.h"

//----------------------------------------------------------------------------------------
//Constructors
//----------------------------------------------------------------------------------------
VRAMViewPresenter::VRAMViewPresenter(const std::wstring& aviewGroupName, const std::wstring& aviewName, int aviewID, S315_5313Menus& aowner, const IDevice& amodelInstanceKey, IS315_5313& amodel)
:ViewPresenterBase(aowner.GetAssemblyHandle(), aviewGroupName, aviewName, aviewID, amodelInstanceKey.GetDeviceInstanceName(), amodelInstanceKey.GetDeviceModuleID(), amodelInstanceKey.GetModuleDisplayName()), owner(aowner), modelInstanceKey(amodelInstanceKey), model(amodel)
{}

//----------------------------------------------------------------------------------------
//View title functions
//----------------------------------------------------------------------------------------
std::wstring VRAMViewPresenter::GetUnqualifiedViewTitle()
{
	return L"VRAM Pattern Viewer";
}

//----------------------------------------------------------------------------------------
//View creation and deletion
//----------------------------------------------------------------------------------------
IView* VRAMViewPresenter::CreateView(IUIManager& uiManager)
{
	return new VRAMView(uiManager, *this, model);
}

//----------------------------------------------------------------------------------------
void VRAMViewPresenter::DeleteView(IView* aview)
{
	delete aview;
}
