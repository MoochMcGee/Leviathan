#include "Stream.h"
#ifndef __FILE_H__
#define __FILE_H__
#include <string>
#include <vector>
#include "WindowsSupport/WindowsSupport.pkg"
namespace Stream {

class File :public Stream<IStream>
{
public:
	//Enumerations
	enum class OpenMode;
	enum class CreateMode;

	//Make sure the File object is non-copyable
	protected: File(const File& object) {} public:

	//Constructors
	inline File();
	inline File(TextEncoding atextEncoding);
	inline File(TextEncoding atextEncoding, NewLineEncoding anewLineEncoding);
	inline File(TextEncoding atextEncoding, NewLineEncoding anewLineEncoding, ByteOrder abyteOrder);
	virtual ~File();

	//File binding
	inline bool Open(const std::wstring& filename, OpenMode openMode, CreateMode createMode, SizeType abufferSize = 8192);
	inline void Close();
	inline bool IsOpen() const;

	//File position
	virtual bool IsAtEnd() const;
	virtual SizeType Size() const;
	virtual SizeType GetStreamPos() const;
	virtual void SetStreamPos(SizeType position);
	virtual bool SkipBytes(SizeType byteCount);

protected:
	using Stream::ReadBinaryNativeByteOrder;
	using Stream::WriteBinaryNativeByteOrder;

	//Native byte order read functions
	virtual bool ReadBinaryNativeByteOrder(char& data);
	virtual bool ReadBinaryNativeByteOrder(signed char& data);
	virtual bool ReadBinaryNativeByteOrder(unsigned char& data);
	virtual bool ReadBinaryNativeByteOrder(wchar_t& data);
	virtual bool ReadBinaryNativeByteOrder(short& data);
	virtual bool ReadBinaryNativeByteOrder(unsigned short& data);
	virtual bool ReadBinaryNativeByteOrder(int& data);
	virtual bool ReadBinaryNativeByteOrder(unsigned int& data);
	virtual bool ReadBinaryNativeByteOrder(long& data);
	virtual bool ReadBinaryNativeByteOrder(unsigned long& data);
	virtual bool ReadBinaryNativeByteOrder(long long& data);
	virtual bool ReadBinaryNativeByteOrder(unsigned long long& data);
	virtual bool ReadBinaryNativeByteOrder(float& data);
	virtual bool ReadBinaryNativeByteOrder(double& data);
	virtual bool ReadBinaryNativeByteOrder(long double& data);

	//Native byte order array read functions
	virtual bool ReadBinaryNativeByteOrder(char* data, SizeType length);
	virtual bool ReadBinaryNativeByteOrder(signed char* data, SizeType length);
	virtual bool ReadBinaryNativeByteOrder(unsigned char* data, SizeType length);
	virtual bool ReadBinaryNativeByteOrder(wchar_t* data, SizeType length);
	virtual bool ReadBinaryNativeByteOrder(short* data, SizeType length);
	virtual bool ReadBinaryNativeByteOrder(unsigned short* data, SizeType length);
	virtual bool ReadBinaryNativeByteOrder(int* data, SizeType length);
	virtual bool ReadBinaryNativeByteOrder(unsigned int* data, SizeType length);
	virtual bool ReadBinaryNativeByteOrder(long* data, SizeType length);
	virtual bool ReadBinaryNativeByteOrder(unsigned long* data, SizeType length);
	virtual bool ReadBinaryNativeByteOrder(long long* data, SizeType length);
	virtual bool ReadBinaryNativeByteOrder(unsigned long long* data, SizeType length);
	virtual bool ReadBinaryNativeByteOrder(float* data, SizeType length);
	virtual bool ReadBinaryNativeByteOrder(double* data, SizeType length);
	virtual bool ReadBinaryNativeByteOrder(long double* data, SizeType length);

	//Native byte order write functions
	virtual bool WriteBinaryNativeByteOrder(char data);
	virtual bool WriteBinaryNativeByteOrder(signed char data);
	virtual bool WriteBinaryNativeByteOrder(unsigned char data);
	virtual bool WriteBinaryNativeByteOrder(wchar_t data);
	virtual bool WriteBinaryNativeByteOrder(short data);
	virtual bool WriteBinaryNativeByteOrder(unsigned short data);
	virtual bool WriteBinaryNativeByteOrder(int data);
	virtual bool WriteBinaryNativeByteOrder(unsigned int data);
	virtual bool WriteBinaryNativeByteOrder(long data);
	virtual bool WriteBinaryNativeByteOrder(unsigned long data);
	virtual bool WriteBinaryNativeByteOrder(long long data);
	virtual bool WriteBinaryNativeByteOrder(unsigned long long data);
	virtual bool WriteBinaryNativeByteOrder(float data);
	virtual bool WriteBinaryNativeByteOrder(double data);
	virtual bool WriteBinaryNativeByteOrder(long double data);

	//Native byte order array write functions
	virtual bool WriteBinaryNativeByteOrder(const char* data, SizeType length);
	virtual bool WriteBinaryNativeByteOrder(const signed char* data, SizeType length);
	virtual bool WriteBinaryNativeByteOrder(const unsigned char* data, SizeType length);
	virtual bool WriteBinaryNativeByteOrder(const wchar_t* data, SizeType length);
	virtual bool WriteBinaryNativeByteOrder(const short* data, SizeType length);
	virtual bool WriteBinaryNativeByteOrder(const unsigned short* data, SizeType length);
	virtual bool WriteBinaryNativeByteOrder(const int* data, SizeType length);
	virtual bool WriteBinaryNativeByteOrder(const unsigned int* data, SizeType length);
	virtual bool WriteBinaryNativeByteOrder(const long* data, SizeType length);
	virtual bool WriteBinaryNativeByteOrder(const unsigned long* data, SizeType length);
	virtual bool WriteBinaryNativeByteOrder(const long long* data, SizeType length);
	virtual bool WriteBinaryNativeByteOrder(const unsigned long long* data, SizeType length);
	virtual bool WriteBinaryNativeByteOrder(const float* data, SizeType length);
	virtual bool WriteBinaryNativeByteOrder(const double* data, SizeType length);
	virtual bool WriteBinaryNativeByteOrder(const long double* data, SizeType length);

private:
	//Internal read/write functions
	inline bool ReadBinary(void* rawData, SizeType bytesToRead);
	inline bool WriteBinary(const void* rawData, SizeType bytesToWrite);
	inline bool ReadBinaryUnbuffered(void* rawData, SizeType bytesToRead);
	inline bool WriteBinaryUnbuffered(const void* rawData, SizeType bytesToWrite);

	//Data buffer functions
	inline bool EmptyDataBuffer();
	inline bool PrepareDataBufferForReads();
	inline bool PrepareDataBufferForWrites();

private:
	//File handling
	bool fileOpen;
	HANDLE fileHandle;

	//Data buffering
	unsigned char* fileBuffer;
	SizeType bufferSize;
	SizeType bufferPosOffset;
	SizeType bytesRemainingInBuffer;
	bool bufferInWriteMode;
};

} //Close namespace Stream
#include "File.inl"
#endif
