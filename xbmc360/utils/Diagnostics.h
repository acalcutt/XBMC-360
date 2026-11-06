#ifndef H_CDIAGNOSTICS
#define H_CDIAGNOSTICS

#include "utils\StdString.h"

// Diagnostics for a bug report, saved to a USB drive: the logs (this run's and the last one's), and system.txt with
// what System info shows (the build, the console, its drives and network), the sources and the media servers linked,
// without passwords or tokens.
class CDiagnostics
{
public:
	// Into <USB drive>\360Riffle-diagnostics\<date and time>\. On the GUI thread: a dialog says where it went, or why it
	// couldn't be saved (no USB drive, or one that can't be written).
	static bool SaveToUsb();

private:
	static CStdString FindUsbDrive();
	static CStdString SystemReport();
};

#endif //H_CDIAGNOSTICS
