#include "GUIDialogNetworkSetup.h"
#include "guilib\GUISpinControlEx.h"
#include "guilib\dialogs\GUIDialogNumeric.h"
#include "guilib\dialogs\GUIDialogKeyboard.h"
#include "guilib\dialogs\GUIDialogFileBrowser.h"
#include "guilib\GUIWindowManager.h"
#include "guilib\GUIEditControl.h"
#include "MediaSource.h"
#include "utils\URIUtils.h"
#include "guilib\LocalizeStrings.h"
#include "URL.h"

#define CONTROL_PROTOCOL        10
#define CONTROL_SERVER_ADDRESS  11
#define CONTROL_SERVER_BROWSE   12
#define CONTROL_PORT_NUMBER     13
#define CONTROL_USERNAME        14
#define CONTROL_PASSWORD        15
#define CONTROL_REMOTE_PATH     16
#define CONTROL_OK              18
#define CONTROL_CANCEL          19

CGUIDialogNetworkSetup::CGUIDialogNetworkSetup(void)
	: CGUIDialog(WINDOW_DIALOG_NETWORK_SETUP, "DialogNetworkSetup.xml")
{
	m_protocol = NET_PROTOCOL_SMB;
	m_confirmed = false;
}

CGUIDialogNetworkSetup::~CGUIDialogNetworkSetup()
{
}

bool CGUIDialogNetworkSetup::OnBack(int actionID)
{
	m_confirmed = false;
	return CGUIDialog::OnBack(actionID);
}

bool CGUIDialogNetworkSetup::OnMessage(CGUIMessage& message)
{
	switch ( message.GetMessage() )
	{
		case GUI_MSG_CLICKED:
		{
			int iControl = message.GetSenderId();
			if (iControl == CONTROL_PROTOCOL)
			{
				m_server.Empty();
				m_path.Empty();
				m_username.Empty();
				m_password.Empty();
				OnProtocolChange();
			}
			else if (iControl == CONTROL_SERVER_BROWSE)
				OnServerBrowse();
			else if (iControl == CONTROL_SERVER_ADDRESS)
				OnEditChanged(iControl, m_server);
			else if (iControl == CONTROL_REMOTE_PATH)
				OnEditChanged(iControl, m_path);
			else if (iControl == CONTROL_PORT_NUMBER)
				OnEditChanged(iControl, m_port);
			else if (iControl == CONTROL_USERNAME)
				OnEditChanged(iControl, m_username);
			else if (iControl == CONTROL_PASSWORD)
				OnEditChanged(iControl, m_password);
			else if (iControl == CONTROL_OK)
				OnOK();
			else if (iControl == CONTROL_CANCEL)
				OnCancel();

			return true;
		}
		break;
	}
	return CGUIDialog::OnMessage(message);
}

// Show CGUIDialogNetworkSetup dialog and prompt for a new network address.
// return True if the network address is valid, false otherwise.
bool CGUIDialogNetworkSetup::ShowAndGetNetworkAddress(CStdString &path)
{
	CGUIDialogNetworkSetup *dialog = (CGUIDialogNetworkSetup *)g_windowManager.GetWindow(WINDOW_DIALOG_NETWORK_SETUP);
	if (!dialog) return false;
	
	dialog->Initialize();
	dialog->SetPath(path);
	dialog->DoModal();
	path = dialog->ConstructPath();
	
	return dialog->IsConfirmed();
}

void CGUIDialogNetworkSetup::OnInitWindow()
{
	// Replace our buttons with edits
	ChangeButtonToEdit(CONTROL_SERVER_ADDRESS);
	ChangeButtonToEdit(CONTROL_REMOTE_PATH);
	ChangeButtonToEdit(CONTROL_USERNAME);
	ChangeButtonToEdit(CONTROL_PORT_NUMBER);
	ChangeButtonToEdit(CONTROL_PASSWORD);

	// Start as unconfirmed
	m_confirmed = false;

	CGUIDialog::OnInitWindow();
	// Add our protocols
	CGUISpinControlEx *pSpin = (CGUISpinControlEx *)GetControl(CONTROL_PROTOCOL);
	if (!pSpin)
		return;

	pSpin->Clear();
	pSpin->AddLabel(g_localizeStrings.Get(20171), NET_PROTOCOL_SMB);
	pSpin->AddLabel(g_localizeStrings.Get(33530), NET_PROTOCOL_MEDIARIFFLE);
	pSpin->AddLabel(g_localizeStrings.Get(33531), NET_PROTOCOL_MEDIARIFFLES);
	pSpin->AddLabel(g_localizeStrings.Get(33541), NET_PROTOCOL_JELLYFIN);
	pSpin->AddLabel(g_localizeStrings.Get(33542), NET_PROTOCOL_JELLYFINS);
	// TODO: Add more!

	pSpin->SetValue(m_protocol);

	// A path being edited keeps its port; a new one starts with the protocol's
	if (m_port.IsEmpty() || m_port.Equals("0"))
		m_port = DefaultPort();
	UpdateButtons();
}

CStdString CGUIDialogNetworkSetup::DefaultPort() const
{
	if (m_protocol == NET_PROTOCOL_MEDIARIFFLE)
		return "5080";
	if (m_protocol == NET_PROTOCOL_MEDIARIFFLES)
		return "443";
	if (m_protocol == NET_PROTOCOL_JELLYFIN)
		return "8096";
	if (m_protocol == NET_PROTOCOL_JELLYFINS)
		return "8920";
	return "445";
}

void CGUIDialogNetworkSetup::OnServerBrowse()
{
	// Open a filebrowser dialog with the current address
	VECSOURCES shares;
	
	CStdString path = ConstructPath();

	// Get the share as the base path
	CMediaSource share;
	CStdString basePath = path;
	CStdString tempPath;
	
	while (URIUtils::GetParentPath(basePath, tempPath))
		basePath = tempPath;
	
	share.strPath = basePath;
	
	// Don't include the user details in the share name
	CURL url(share.strPath);
	share.strName = url.GetWithoutUserDetails();
	shares.push_back(share);
	
	if (CGUIDialogFileBrowser::ShowAndGetDirectory(shares, g_localizeStrings.Get(1015), path))
	{
		SetPath(path);
		UpdateButtons();
	}
}

void CGUIDialogNetworkSetup::OnOK()
{
	m_confirmed = true;
	Close();
}

void CGUIDialogNetworkSetup::OnCancel()
{
	m_confirmed = false;
	Close();
}

void CGUIDialogNetworkSetup::OnProtocolChange()
{
	CGUISpinControlEx *pSpin = (CGUISpinControlEx *)GetControl(CONTROL_PROTOCOL);
	if (!pSpin)
		return;
	
	m_protocol = (NET_PROTOCOL)pSpin->GetValue();

	// Set defaults for the port
	m_port = DefaultPort();

	UpdateButtons();
}

void CGUIDialogNetworkSetup::UpdateButtons()
{
	// Address label
	SET_CONTROL_LABEL2(CONTROL_SERVER_ADDRESS, m_server);
	
	if (m_protocol == NET_PROTOCOL_SMB)
	{
		SET_CONTROL_LABEL(CONTROL_SERVER_ADDRESS, 1010); // Server name
	}
	else
	{
		SET_CONTROL_LABEL(CONTROL_SERVER_ADDRESS, 1009); // Server Address
	}
//	if (m_protocol == NET_PROTOCOL_XBMSP || m_protocol == NET_PROTOCOL_DAAP)
//		SendMessage(GUI_MSG_SET_TYPE, CONTROL_SERVER_ADDRESS, CGUIEditControl::INPUT_TYPE_IPADDRESS, 1016); // TODO
//	else
		SendMessage(GUI_MSG_SET_TYPE, CONTROL_SERVER_ADDRESS, CGUIEditControl::INPUT_TYPE_TEXT, 1016);
	
	// Remote path
	SET_CONTROL_LABEL2(CONTROL_REMOTE_PATH, m_path);
/*	CONTROL_ENABLE_ON_CONDITION(CONTROL_REMOTE_PATH, m_protocol != NET_PROTOCOL_DAAP && // TODO
													m_protocol != NET_PROTOCOL_UPNP &&
													m_protocol != NET_PROTOCOL_TUXBOX &&
													m_protocol != NET_PROTOCOL_MYTH);
	if (m_protocol == NET_PROTOCOL_FTP ||
		m_protocol == NET_PROTOCOL_HTTP ||
		m_protocol == NET_PROTOCOL_HTTPS ||
		m_protocol == NET_PROTOCOL_RSS ||
		m_protocol == NET_PROTOCOL_DAV ||
		m_protocol == NET_PROTOCOL_DAVS)
	{
		SET_CONTROL_LABEL(CONTROL_REMOTE_PATH, 1011); // Remote Path
	}
	else
*/	{
		SET_CONTROL_LABEL(CONTROL_REMOTE_PATH, 1012);  // Shared Folder
	}
	
	SendMessage(GUI_MSG_SET_TYPE, CONTROL_REMOTE_PATH, CGUIEditControl::INPUT_TYPE_TEXT, 1017);

	// Username
	SET_CONTROL_LABEL2(CONTROL_USERNAME, m_username);
//	CONTROL_ENABLE_ON_CONDITION(CONTROL_USERNAME, m_protocol != NET_PROTOCOL_DAAP && m_protocol != NET_PROTOCOL_UPNP); // TODO
	// A share's keyboard says how a domain account is written
	SendMessage(GUI_MSG_SET_TYPE, CONTROL_USERNAME, CGUIEditControl::INPUT_TYPE_TEXT, m_protocol == NET_PROTOCOL_SMB ? 13188 : 1019);

	// Port
	SET_CONTROL_LABEL2(CONTROL_PORT_NUMBER, m_port);
/*	CONTROL_ENABLE_ON_CONDITION(CONTROL_PORT_NUMBER, m_protocol == NET_PROTOCOL_XBMSP || // TODO
													m_protocol == NET_PROTOCOL_FTP ||
													m_protocol == NET_PROTOCOL_HTTP ||
													m_protocol == NET_PROTOCOL_HTTPS ||
													m_protocol == NET_PROTOCOL_DAV ||
													m_protocol == NET_PROTOCOL_DAVS ||
													m_protocol == NET_PROTOCOL_TUXBOX ||
													m_protocol == NET_PROTOCOL_MYTH ||
													m_protocol == NET_PROTOCOL_RSS ||
													m_protocol == NET_PROTOCOL_DAAP);
*/
	SendMessage(GUI_MSG_SET_TYPE, CONTROL_PORT_NUMBER, CGUIEditControl::INPUT_TYPE_NUMBER, 1018);

	// Password
	SET_CONTROL_LABEL2(CONTROL_PASSWORD, m_password);
//	CONTROL_ENABLE_ON_CONDITION(CONTROL_PASSWORD, m_protocol != NET_PROTOCOL_DAAP && m_protocol != NET_PROTOCOL_UPNP); // TODO
	SendMessage(GUI_MSG_SET_TYPE, CONTROL_PASSWORD, CGUIEditControl::INPUT_TYPE_PASSWORD, 12326);

	// MediaRiffle and Jellyfin: the server's address and port only, so the rest isn't shown at all (greyed out, a
	// user name and password still looked asked for). The console links or signs in when it's first opened.
	int forShares[] = { CONTROL_REMOTE_PATH, CONTROL_USERNAME, CONTROL_PASSWORD, CONTROL_SERVER_BROWSE };
	for (unsigned int i = 0; i < sizeof(forShares) / sizeof(forShares[0]); i++)
	{
		if (IsMediaServer())
			SET_CONTROL_HIDDEN(forShares[i]);
		else
			SET_CONTROL_VISIBLE(forShares[i]);
	}

	// TODO: FIX BETTER DAAP SUPPORT
	// Server browse should be disabled if we are in DAAP, FTP, HTTP, HTTPS, RSS, TUXBOX, DAV or DAVS
/*	CONTROL_ENABLE_ON_CONDITION(CONTROL_SERVER_BROWSE, !m_server.IsEmpty() || !(m_protocol == NET_PROTOCOL_FTP || // TODO
																			m_protocol == NET_PROTOCOL_HTTP ||
																			m_protocol == NET_PROTOCOL_HTTPS ||
																			m_protocol == NET_PROTOCOL_DAV ||
																			m_protocol == NET_PROTOCOL_DAVS ||
																			m_protocol == NET_PROTOCOL_DAAP ||
																			m_protocol == NET_PROTOCOL_RSS ||
																			m_protocol == NET_PROTOCOL_MYTH ||
																			m_protocol == NET_PROTOCOL_TUXBOX));
*/
}

CStdString CGUIDialogNetworkSetup::ConstructPath() const
{
	CURL url;

	if (m_protocol == NET_PROTOCOL_SMB)
		url.SetProtocol("smb");
	// TODO - Add the other protocols

	// MediaRiffle and Jellyfin: mediariffle://host:port/ or jellyfin://host:port/, from the address however it was typed
	// ("http://media01:5080/" too)
	if (IsMediaServer())
	{
		CStdString host = m_server;
		host.Trim();
		if (host.Find("://") >= 0)
			host = host.Mid(host.Find("://") + 3);
		if (host.Find("/") >= 0)
			host = host.Left(host.Find("/"));
		if (host.Find(":") >= 0)
			host = host.Left(host.Find(":"));
		if (host.IsEmpty())
			return "";

		if (IsJellyfin())
			url.SetProtocol(m_protocol == NET_PROTOCOL_JELLYFINS ? "jellyfins" : "jellyfin");
		else
			url.SetProtocol(m_protocol == NET_PROTOCOL_MEDIARIFFLES ? "mediariffles" : "mediariffle");
		url.SetHostName(host);
		if (atoi(m_port.c_str()) > 0)
			url.SetPort(atoi(m_port.c_str()));

		CStdString path = url.Get();
		URIUtils::AddSlashAtEnd(path);
		return path;
	}

	if (!m_username.IsEmpty())
	{
		url.SetUserName(m_username);
		if (!m_password.IsEmpty())
			url.SetPassword(m_password);
	}
	
	if(!m_server.IsEmpty())
		url.SetHostName(m_server);
	
	if (!m_port.IsEmpty() && atoi(m_port.c_str()) > 0)
	{
		url.SetPort(atoi(m_port));
	}
	
	if (!m_path.IsEmpty())
		url.SetFileName(m_path);
	
	return url.Get();
}

void CGUIDialogNetworkSetup::SetPath(const CStdString &path)
{
	CURL url(path);
	const CStdString &protocol = url.GetProtocol();
	
	if (protocol == "smb")
		m_protocol = NET_PROTOCOL_SMB;
	else if (protocol == "mediariffle")
		m_protocol = NET_PROTOCOL_MEDIARIFFLE;
	else if (protocol == "mediariffles")
		m_protocol = NET_PROTOCOL_MEDIARIFFLES;
	else if (protocol == "jellyfin")
		m_protocol = NET_PROTOCOL_JELLYFIN;
	else if (protocol == "jellyfins")
		m_protocol = NET_PROTOCOL_JELLYFINS;
	// TODO: Add other protocols back as we support them!
	else
		m_protocol = NET_PROTOCOL_SMB; // Default to SMB
	
	m_username = url.GetUserName();
	m_password = url.GetPassWord();
	m_port.Format("%i", url.GetPort());
	m_server = url.GetHostName();
	m_path = url.GetFileName();
}