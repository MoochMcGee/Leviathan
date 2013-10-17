#ifndef __IHIERARCHICALSTORAGEATTRIBUTE_H__
#define __IHIERARCHICALSTORAGEATTRIBUTE_H__
#include "StreamInterface/StreamInterface.pkg"
#include "InteropSupport/InteropSupport.pkg"
#include <string>

class IHierarchicalStorageAttribute
{
public:
	//Constructors
	virtual ~IHierarchicalStorageAttribute() = 0 {}

	//Name functions
	inline std::wstring GetName() const;
	inline void SetName(const std::wstring& aname);

	//Value read functions
	//##FIX## Why do we reset the stream position when extracting data, but not when
	//inserting data?
	inline std::wstring GetValue() const;
	template<class T> T ExtractValue();
	template<class T> T ExtractHexValue();
	template<class T> void ExtractValue(T& target);
	template<class T> void ExtractHexValue(T& target);

	//Value write functions
	template<class T> void SetValue(const T& adata);
	template<class T> void InsertValue(const T& adata);
	template<class T> void InsertHexValue(const T& adata, unsigned int length);

protected:
	//Name functions
	virtual void GetNameInternal(const InteropSupport::ISTLObjectTarget<std::wstring>& marshaller) const = 0;
	virtual void SetNameInternal(const InteropSupport::ISTLObjectSource<std::wstring>& marshaller) = 0;

	//Stream functions
	virtual void ResetInternalStreamPosition() const = 0;
	virtual void EmptyInternalStream() = 0;
	virtual Stream::IStream& GetInternalStream() const = 0;
};

#include "IHierarchicalStorageAttribute.inl"
#endif