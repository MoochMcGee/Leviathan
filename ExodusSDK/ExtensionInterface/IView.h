#ifndef __IVIEW_H__
#define __IVIEW_H__
#include "HierarchicalStorageInterface/HierarchicalStorageInterface.pkg"
#include "MarshalSupport/MarshalSupport.pkg"
#include <string>
class IViewPresenter;
class IUIManager;

class IView
{
public:
	//Enumerations
	enum class DockPos;
	enum class DialogPos;
	enum class ViewType;
	enum class DialogMode;

public:
	//Constructors
	virtual ~IView() = 0 {}

	//Interface version functions
	static inline unsigned int ThisIViewVersion() { return 1; }
	virtual unsigned int GetIViewVersion() const = 0;

	//View management functions
	virtual bool OpenView(IHierarchicalStorageNode* viewState) = 0;
	virtual void CloseView() = 0;
	virtual void ShowView() = 0;
	virtual void HideView() = 0;
	virtual void ActivateView() = 0;

	//State functions
	virtual bool LoadViewState(IHierarchicalStorageNode& viewState) = 0;
	virtual bool SaveViewState(IHierarchicalStorageNode& viewState) const = 0;

	//New window state
	virtual MarshalSupport::Marshal::Ret<std::wstring> GetViewDockingGroup() const = 0;
	virtual bool IsViewInitiallyDocked() const = 0;
	virtual bool IsViewInitiallyCollapsed() const = 0;
	virtual DockPos GetViewInitialDockPosition() const = 0;
	virtual ViewType GetViewType() const = 0;
	virtual DialogMode GetViewDialogMode() const = 0;
	virtual DialogPos GetViewInitialDialogPosition() const = 0;
	virtual bool CanResizeDialog() const = 0;
};

#include "IView.inl"
#endif
