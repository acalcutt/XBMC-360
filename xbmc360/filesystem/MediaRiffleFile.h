#ifndef H_CMEDIARIFFLEFILE
#define H_CMEDIARIFFLEFILE

#include "IFile.h"

#include <string>

namespace XFILE
{

// A MediaRiffle picture, mediariffle://image/<kind>/<id> (its /api/images/<kind>/<id>, fetched with the console's
// token): whole, into memory, for the thumbnail cache to read
class CMediaRiffleFile : public IFile
{
public:
	CMediaRiffleFile();
	virtual ~CMediaRiffleFile();

	virtual bool Open(const CURL& url);
	virtual bool Exists(const CURL& url);
	virtual int Stat(const CURL& url, struct __stat64* buffer);
	virtual unsigned int Read(void* lpBuf, int64_t uiBufSize);
	virtual int64_t Seek(int64_t iFilePosition, int iWhence = SEEK_SET);
	virtual void Close();
	virtual int64_t GetPosition();
	virtual int64_t GetLength();

private:
	std::string m_data;
	int64_t m_position;
};

}

#endif //H_CMEDIARIFFLEFILE
