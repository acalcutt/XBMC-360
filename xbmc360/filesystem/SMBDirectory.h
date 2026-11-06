#ifndef H_CSMBDIRECTORY
#define H_CSMBDIRECTORY

#include "IFile.h"
#include "iDirectory.h"
#include "FileSmb.h"
#include <fcntl.h>

namespace XFILE
{

class CSMBDirectory : public IDirectory
{
public:
	CSMBDirectory(void);
	virtual ~CSMBDirectory(void);

	virtual bool GetDirectory(const CStdString& strPath, CFileItemList &items);
	virtual bool Create(const char* strPath);
	virtual bool Exists(const char* strPath);
	virtual bool Remove(const char* strPath);

	int Open(const CURL &url);

	// Why the last share or folder listed couldn't be opened, as libsmb2 put it; empty when it opened
	static CStdString GetLastError();

private:
};
}

#endif //H_CSMBDIRECTORY