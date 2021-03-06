#include "MenuSubmenu.h"
#include "MenuSegment.h"
#include "MenuSelectableOption.h"

//----------------------------------------------------------------------------------------
//Constructors
//----------------------------------------------------------------------------------------
MenuSubmenu::MenuSubmenu(const std::wstring& atitle)
:title(atitle)
{}

//----------------------------------------------------------------------------------------
MenuSubmenu::~MenuSubmenu()
{
	for(std::list<IMenuItem*>::const_iterator i = menuItems.begin(); i != menuItems.end(); ++i)
	{
		delete *i;
	}
}

//----------------------------------------------------------------------------------------
//Interface version functions
//----------------------------------------------------------------------------------------
unsigned int MenuSubmenu::GetIMenuItemVersion() const
{
	return ThisIMenuItemVersion();
}

//----------------------------------------------------------------------------------------
unsigned int MenuSubmenu::GetIMenuSubmenuVersion() const
{
	return ThisIMenuSubmenuVersion();
}

//----------------------------------------------------------------------------------------
//Type functions
//----------------------------------------------------------------------------------------
MenuSubmenu::Type MenuSubmenu::GetType() const
{
	return Type::SubMenu;
}

//----------------------------------------------------------------------------------------
//Menu title functions
//----------------------------------------------------------------------------------------
MarshalSupport::Marshal::Ret<std::wstring> MenuSubmenu::GetMenuTitle() const
{
	return title;
}

//----------------------------------------------------------------------------------------
//Item management functions
//----------------------------------------------------------------------------------------
bool MenuSubmenu::NoMenuItemsExist() const
{
	for(std::list<IMenuItem*>::const_iterator i = menuItems.begin(); i != menuItems.end(); ++i)
	{
		const IMenuItem* menuItem = *i;
		IMenuItem::Type menuItemType = menuItem->GetType();
		if(menuItemType == IMenuItem::Type::Segment)
		{
			const IMenuSegment* menuItemAsSegment = (IMenuSegment*)menuItem;
			if(!menuItemAsSegment->NoMenuItemsExist())
			{
				return false;
			}
		}
		else if(menuItemType == IMenuItem::Type::SubMenu)
		{
			const IMenuSubmenu* menuItemAsSubmenu = (IMenuSubmenu*)menuItem;
			if(!menuItemAsSubmenu->NoMenuItemsExist())
			{
				return false;
			}
		}
		else
		{
			return false;
		}
	}
	return true;
}

//----------------------------------------------------------------------------------------
MarshalSupport::Marshal::Ret<std::list<IMenuItem*>> MenuSubmenu::GetMenuItems() const
{
	return menuItems;
}

//----------------------------------------------------------------------------------------
//Menu item creation functions
//----------------------------------------------------------------------------------------
IMenuSegment& MenuSubmenu::AddMenuItemSegment(bool asurroundWithSeparators, IMenuSegment::SortMode sortMode)
{
	IMenuSegment* newMenuItem = new MenuSegment(asurroundWithSeparators, sortMode);
	menuItems.push_back(newMenuItem);
	return *newMenuItem;
}

//----------------------------------------------------------------------------------------
IMenuSubmenu& MenuSubmenu::AddMenuItemSubmenu(const MarshalSupport::Marshal::In<std::wstring>& title)
{
	IMenuSubmenu* newMenuItem = new MenuSubmenu(title);
	menuItems.push_back(newMenuItem);
	return *newMenuItem;
}

//----------------------------------------------------------------------------------------
IMenuSelectableOption& MenuSubmenu::AddMenuItemSelectableOption(IMenuHandler& menuHandler, int menuItemID, const MarshalSupport::Marshal::In<std::wstring>& title)
{
	IMenuSelectableOption* newMenuItem = new MenuSelectableOption(menuHandler, menuItemID, title);
	menuItems.push_back(newMenuItem);
	return *newMenuItem;
}

//----------------------------------------------------------------------------------------
void MenuSubmenu::DeleteMenuItem(IMenuItem& menuItem)
{
	IMenuItem* menuItemPointer = &menuItem;
	bool done = false;
	std::list<IMenuItem*>::iterator i = menuItems.begin();
	while(!done && (i != menuItems.end()))
	{
		if(*i == menuItemPointer)
		{
			menuItems.erase(i);
			delete menuItemPointer;
			done = true;
			continue;
		}
		++i;
	}
}

//----------------------------------------------------------------------------------------
void MenuSubmenu::DeleteAllMenuItems()
{
	for(std::list<IMenuItem*>::iterator i = menuItems.begin(); i != menuItems.end(); ++i)
	{
		delete *i;
	}
	menuItems.clear();
}
