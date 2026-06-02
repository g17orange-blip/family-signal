#pragma once

#include "Config.h"
#include "Contact.h"

#include <QMainWindow>

class CallWindow;
class ChatHeader;
class ChatModel;
class ChatView;
class ContactsModel;
class MessageHistory;
class MessageInputBar;
class QListView;
class QSplitter;
class SignalingClient;
class WebRtcSession;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    bool loadConfigOrShowError();
    void start();

    // Demo mode: skips signaling and GStreamer entirely, seeds a fake
    // online contact and sample chat history so the UI can be evaluated
    // without any server infrastructure or camera permissions.
    void startDemo();

private slots:
    void onSignalingConnected();
    void onSignalingDisconnected();
    void onSignalingError(const QString &reason);
    void onPresenceChanged(const QStringList &online);

    void onContactSelected(const QModelIndex &index);
    void onSendText(const QString &text);

    void onIncomingOffer(const QString &fromPeerId, const QString &sdp);
    void onIncomingAnswer(const QString &fromPeerId, const QString &sdp);
    void onIncomingIce(const QString &fromPeerId, const QString &candidate,
                       const QString &sdpMid, int sdpMLineIndex);
    void onIncomingBye(const QString &fromPeerId);

    void onLocalOffer(const QString &peerId, const QString &sdp);
    void onLocalAnswer(const QString &peerId, const QString &sdp);
    void onLocalIce(const QString &peerId, const QString &candidate,
                    const QString &sdpMid, int sdpMLineIndex);
    void onCallConnected();
    void onCallEnded();
    void onWebRtcError(const QString &message);

    void onTextReceived(const QString &fromPeerId, const QString &text);
    void onTextDelivered(const QString &text);

    void onStartVideoCall();
    void onStartAudioCall();
    void onHangupRequested();

private:
    void selectContactById(const QString &id);
    void appendMessage(const QString &peerId, const QString &senderId, const QString &text);
    Contact currentContact() const;

    Config           m_config;
    SignalingClient *m_signaling   = nullptr;
    WebRtcSession   *m_webrtc      = nullptr;
    MessageHistory  *m_history     = nullptr;

    ContactsModel   *m_contactsModel = nullptr;
    ChatModel       *m_chatModel     = nullptr;

    QSplitter       *m_splitter     = nullptr;
    QListView       *m_contactsView = nullptr;
    ChatHeader      *m_chatHeader   = nullptr;
    ChatView        *m_chatView     = nullptr;
    MessageInputBar *m_inputBar     = nullptr;
    CallWindow      *m_callWindow   = nullptr;

    Contact          m_currentContact;
};
