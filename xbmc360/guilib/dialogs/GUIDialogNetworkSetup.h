#ifndef CGUIDIALOGNETWORKSETUP_H
#define CGUIDIALOGNETWORKSETUP_H

#include "guilib\GUIDialog.h"

class CGUIDialogNetworkSetup :
	public CGUIDialog
{
public:
	enum NET_PROTOCOL { NET_PROTOCOL_SMB = 0,
	                    NET_PROTOCOL_MEDIARIFFLE,   // mediariffle://host:port/ (network\MediaRiffle.h)
	                    NET_PROTOCOL_MEDIARIFFLES,  // the same over HTTPS
	                    NET_PROTOCOL_JELLYFIN,      // jellyfin://host:port/ (network\Jellyfin.h)
	                    NET_PROTOCOL_JELLYFINS      // the same over HTTPS
	                  }; // TODO - Add more!
	
	CGUIDialogNetworkSetup(void);
	virtual ~CGUIDialogNetworkSetup(void);
	virtual bool OnMessage(CGUIMessage& message);
	virtual bool OnBack(int actionID);
	virtual void OnInitWindow();

	// path starts it off (its protocol, server, port…). With fixedKind, only protocols of the same kind are offered:
	// a share, a MediaRiffle server or a Jellyfin server, each with its HTTPS form.
	static bool ShowAndGetNetworkAddress(CStdString &path, bool fixedKind = false);

	CStdString ConstructPath() const;
	void SetPath(const CStdString &path);
	bool IsConfirmed() const { return m_confirmed; };

protected:
	void OnProtocolChange();
	bool IsMediaRiffle() const { return m_protocol == NET_PROTOCOL_MEDIARIFFLE || m_protocol == NET_PROTOCOL_MEDIARIFFLES; }
	bool IsJellyfin() const { return m_protocol == NET_PROTOCOL_JELLYFIN || m_protocol == NET_PROTOCOL_JELLYFINS; }
	// A media server: just its address and port, signed in to when it's first opened
	bool IsMediaServer() const { return IsMediaRiffle() || IsJellyfin(); }
	CStdString DefaultPort() const;
	void OnServerBrowse();
	void OnOK();
	void OnCancel();
	void UpdateButtons();

	NET_PROTOCOL m_protocol;
	CStdString m_server;
	CStdString m_path;
	CStdString m_username;
	CStdString m_password;
	CStdString m_port;

	bool m_confirmed;
	bool m_fixedKind;
};

#endif //CGUIDIALOGNETWORKSETUP_H